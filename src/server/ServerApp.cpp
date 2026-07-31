#define _CRT_SECURE_NO_WARNINGS
#include "ServerApp.h"
#include "Logger.h"
#include <iostream>
#include <chrono>

ServerApp::ServerApp(SharedState& state, std::unique_ptr<ISystemMetricsProvider> provider)
    : m_State(state), m_Provider(std::move(provider))
{
    auto& cors = m_App.get_middleware<crow::CORSHandler>();

    cors.global()
        .headers("Origin", "Content-Type", "Accept", "Authorization")
        .methods(crow::HTTPMethod::POST, crow::HTTPMethod::GET, crow::HTTPMethod::OPTIONS)
        .origin("*");

    SetupRoutes();
    StartCollectorThread();
}

ServerApp::~ServerApp() {
    if (m_CollectorThread.joinable()) {
        m_CollectorThread.request_stop();
    }
}

auto SerializeGpuMetrics = [](const std::vector<GPUMetrics>& gpuList) {
    std::vector<crow::json::wvalue> gpuArray;
    gpuArray.reserve(gpuList.size());

    for (const auto& g : gpuList) {
        crow::json::wvalue item;
        item["name"] = g.Name;
        item["percent_of_usage"] = g.UsagePercent;
        item["temperature"] = g.Temperature;
        item["memory_total"] = g.MemoryTotal;
        item["memory_used"] = g.MemoryUsed;
        item["is_available"] = g.IsAvailable;
        gpuArray.push_back(std::move(item));
    }
    return gpuArray;
    };


void ServerApp::StartCollectorThread() {
    m_CollectorThread = std::jthread([this](std::stop_token stopToken) {
        LOG_INFO("Collector thread started (interval: 500ms)");
        auto prevTime = std::chrono::steady_clock::now();
        NetworkMetrics prevNet = m_Provider->GetNetworkMetrics();

        while (!stopToken.stop_requested()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));

            try {
                auto currentTime = std::chrono::steady_clock::now();
                double deltaTime = std::chrono::duration<double>(currentTime - prevTime).count();

                NetworkMetrics currentNet = m_Provider->GetNetworkMetrics();
                NetworkUsage netUsage;

                if (deltaTime > 0.0) {
                    netUsage.DownloadBytesPerSec = (currentNet.BytesReceived - prevNet.BytesReceived) / deltaTime;
                    netUsage.UploadBytesPerSec = (currentNet.BytesSent - prevNet.BytesSent) / deltaTime;
                }

                prevTime = currentTime;
                prevNet = currentNet;

                // Сбор метрик через абстрактный провайдер
                MemoryMetrics mem = m_Provider->GetMemoryMetrics();
                double cpuUsage = m_Provider->GetCPUMetrics();
                auto processes = m_Provider->GetProcesses();
                auto tempreture = m_Provider->GetTemperatures();
                uint64_t uptime = m_Provider->GetTickTime();
                auto gpuUsage = m_Provider->GetGPUMetrics();
                // Обновляем состояние
                m_State.Update(mem, cpuUsage, netUsage, std::move(processes),std::move(tempreture), uptime,std::move(gpuUsage));

                // Рассылка по WebSocket
                std::lock_guard<std::mutex> lock(m_WsMutex);
                if (!m_ActiveConnections.empty()) {
                    SystemSnapshot lastSnap = m_State.GetSnapshot();
                    crow::json::wvalue wsMsg;
                    wsMsg["cpu_usage"] = lastSnap.CPUUsage;
                    wsMsg["ram"]["percent"] = lastSnap.MemorySnap.PercentOfUsage;
                    wsMsg["ram"]["total_bytes"] = lastSnap.MemorySnap.MemoryAmount;
                    wsMsg["ram"]["free_bytes"] = lastSnap.MemorySnap.MemoryFree;
                    wsMsg["network"]["download_bytes_psec"] = lastSnap.NetworkSnap.DownloadBytesPerSec;
                    wsMsg["network"]["upload_bytes_psec"] = lastSnap.NetworkSnap.UploadBytesPerSec;
                    wsMsg["uptime_seconds"] = lastSnap.UptimeSeconds;

                    std::vector<crow::json::wvalue> tempArray;
                    tempArray.reserve(lastSnap.Temperatures.size()); // Оптимизация выделения памяти

                    for (const auto& t : lastSnap.Temperatures) {
                        crow::json::wvalue item;
                        item["sensor"] = t.SensorName;
                        item["celsius"] = t.Celsius;
                        tempArray.push_back(std::move(item));
                    }

                    // 3. Кладем вектор в итоговое сообщение (если вектор пуст, улетит пустой массив [])
                    wsMsg["temperatures"] = std::move(tempArray);

                    wsMsg["gpu_metrics"] = SerializeGpuMetrics(lastSnap.GPUMetrics);

                    std::string payload = wsMsg.dump();

                    for (auto* conn : m_ActiveConnections) {
                        if (conn) {
                            try {
                                conn->send_text(payload);
                            }
                            catch (const std::exception& e) {
                                LOG_WARN("Failed to send WebSocket message: {}", e.what());
                            }
                        }
                    }
                }
            }
            catch (const std::exception& e) {
                LOG_ERROR("[Collector Thread Exception] {}", e.what());
            }
            catch (...) {
                LOG_ERROR("[Collector Thread Exception] Unknown error");
            }
        }
        });
}

void ServerApp::SetupRoutes() {
    // ------------------------------------------------------------------------
    // WebSocket Route
    // ------------------------------------------------------------------------
    CROW_WEBSOCKET_ROUTE(m_App, "/ws")
        .onopen([this](crow::websocket::connection& conn) {
        std::lock_guard<std::mutex> lock(m_WsMutex);
        m_ActiveConnections.insert(&conn);
        LOG_INFO("[WebSocket] Client connected! Total clients: {}", m_ActiveConnections.size());
            })
        .onclose([this](crow::websocket::connection& conn, const std::string& reason, uint16_t status_code) {
        std::lock_guard<std::mutex> lock(m_WsMutex);
        m_ActiveConnections.erase(&conn);
        LOG_INFO("[WebSocket] Client disconnected ({}, code: {}). Total clients: {}", reason, status_code, m_ActiveConnections.size());
            })
        .onmessage([](crow::websocket::connection&, const std::string& data, bool) {
        LOG_DEBUG("[WebSocket] Received message: {}", data);
            });

    // ------------------------------------------------------------------------
    // HTTP REST Routes
    // ------------------------------------------------------------------------

    // GET /api/system/info
    CROW_ROUTE(m_App, "/api/system/info")([this]() {
        crow::json::wvalue res;

        OSMetrics os = m_Provider->GetOSMetrics();
        res["os"]["name"] = os.OsName;
        res["os"]["architecture"] = os.Architecture;
        res["os"]["computer_name"] = os.ComputerName;
        res["os"]["user_name"] = os.UserName;
        res["os"]["build_number"] = os.BuildNumber;

        CPUModel processor = m_Provider->GetCPUModel();
        res["cpu"]["model"] = processor.Model;
        res["cpu"]["num_cores"] = processor.CoreAmount;

        auto disks = m_Provider->GetDiskMetrics();
        std::vector<crow::json::wvalue> diskList;
        for (const auto& D : disks) {
            crow::json::wvalue disk;
            disk["drive_model"] = D.DriveModel;
            disk["drive_letter"] = D.DriveLetter;
            disk["drive_type"] = D.DriveType;
            disk["total_bytes"] = D.TotalBytes;
            disk["free_bytes"] = D.FreeBytes;
            disk["percent_of_usage"] = D.PercentOfUsage;
            diskList.push_back(disk);
        }
        res["disks"] = std::move(diskList);

        crow::response response(res);
        return response;
        });
    // GET /api/metrics
    CROW_ROUTE(m_App, "/api/metrics")([this]() {
        SystemSnapshot lastSnap = m_State.GetSnapshot();
        crow::json::wvalue res;

        res["cpu_usage"] = lastSnap.CPUUsage;
        res["ram"]["percent"] = lastSnap.MemorySnap.PercentOfUsage;
        res["ram"]["total_bytes"] = lastSnap.MemorySnap.MemoryAmount;
        res["ram"]["free_bytes"] = lastSnap.MemorySnap.MemoryFree;
        res["network"]["download_bytes_psec"] = lastSnap.NetworkSnap.DownloadBytesPerSec;
        res["network"]["upload_bytes_psec"] = lastSnap.NetworkSnap.UploadBytesPerSec;
        res["uptime_seconds"] = lastSnap.UptimeSeconds;
        res["gpu_metrics"] = SerializeGpuMetrics(lastSnap.GPUMetrics);
        crow::response response(res);
        return response;
        });

    // GET /api/processes
    CROW_ROUTE(m_App, "/api/processes")([this]() {
        SystemSnapshot lastSnap = m_State.GetSnapshot();
        crow::json::wvalue res;

        std::vector<crow::json::wvalue> procList;
        procList.reserve(lastSnap.Processes.size());

        for (const auto& P : lastSnap.Processes) {
            crow::json::wvalue proc;
            proc["pid"] = P.Pid;
            proc["name"] = P.Name;
            proc["path"] = P.ExecutablePath;
            proc["memory_usage"] = P.MemoryUsage;
            proc["cpu_usage"] = P.CpuUsage;
            procList.push_back(std::move(proc));
        }

        res["count"] = procList.size();
        res["processes"] = std::move(procList);

        crow::response response(res);
        return response;
        });

    //GET /api/startup
    CROW_ROUTE(m_App, "/api/startup")([this]() {
        std::vector<StartupItem> Startup = m_Provider->GetStartupItems();
        std::vector<crow::json::wvalue> StartupList;
        StartupList.reserve(Startup.size());
        for (const auto& S : Startup) {
            crow::json::wvalue Com;
            Com["name"] = S.Name;
            Com["command"] = S.Command;
            Com["location"] = S.Location;
            StartupList.push_back(std::move(Com));
        }
        crow::json::wvalue StartUpItems;
        StartUpItems["count"] = StartupList.size();
        StartUpItems["startup_items"] = std::move(StartupList);
        crow::response response(StartUpItems);
        return response;
        });

    // GET /api/temperatures
    CROW_ROUTE(m_App, "/api/temperatures")([this]() {
        SystemSnapshot lastSnap = m_State.GetSnapshot();
        auto temps = lastSnap.Temperatures;

        crow::json::wvalue res;

        std::vector<crow::json::wvalue> list;
        list.reserve(temps.size());
        for (const auto& t : temps) {
            crow::json::wvalue item;
            item["sensor"] = t.SensorName;
            item["celsius"] = t.Celsius;
            list.push_back(std::move(item));
        }

        res["sensors"] = std::move(list);

        crow::response response(res);
        return response;
        });


    // POST /api/kill
    CROW_ROUTE(m_App, "/api/kill").methods(crow::HTTPMethod::POST)([this](const crow::request& req) {
        auto body = crow::json::load(req.body);
        if (!body || !body.has("pid")) {
            LOG_WARN("[HTTP POST /api/kill] Bad JSON or missing pid");
            crow::response response(400, "{\"error\": \"Bad JSON or missing pid\"}");
            return response;
        }

        unsigned long pid = static_cast<unsigned long>(body["pid"].i());
        bool ok = m_Provider->KillProcess(pid);

        if (ok) {
            LOG_INFO("[HTTP POST /api/kill] Process PID {} killed successfully", pid);
        }
        else {
            LOG_ERROR("[HTTP POST /api/kill] Failed to kill process PID {}", pid);
        }

        crow::json::wvalue responseJson;
        responseJson["success"] = ok;
        responseJson["pid"] = pid;

        crow::response response(ok ? 200 : 500, responseJson);
        return response;
        });

}

void ServerApp::Run(uint16_t port) {
    LOG_INFO("Starting HTTP/WebSocket Server on port {}", port);

    m_App.port(port).multithreaded().run();
}