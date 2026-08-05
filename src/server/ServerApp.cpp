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
        LOG_INFO("Collector thread started (interval: {} ms)", m_Rate.load());
        auto prevTime = std::chrono::steady_clock::now();
        NetworkMetrics prevNet = m_Provider->GetNetworkMetrics();

        while (!stopToken.stop_requested()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(m_Rate.load()));
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
            Com["is_enabled"] = S.IsEnabled;
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


    // POST /api/process/kill
    CROW_ROUTE(m_App, "/api/process/kill").methods(crow::HTTPMethod::POST)([this](const crow::request& req) {
        auto body = crow::json::load(req.body);
        if (!body || !body.has("pid")) {
            LOG_WARN("[HTTP POST /api/process/kill] Bad JSON or missing pid");
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

    // POST /api/startup_item/kill
    CROW_ROUTE(m_App, "/api/startup_item/kill").methods(crow::HTTPMethod::POST)([this](const crow::request& req) {
        auto body = crow::json::load(req.body);
        if (!body || !body.has("name") || !body.has("location")) {
            LOG_WARN("[HTTP POST /api/startup_item/kill] Bad JSON or missing name or location");
            crow::response response(400, "{\"error\": \"Bad JSON or missing name ot location\"}");
            return response;
        }

        std::string name = body["name"].s();
        std::string location = body["location"].s();

        bool ok = m_Provider->RemoveStartupItem(name, location);

        if (ok) {
            LOG_INFO("[HTTP POST /api/startup_item/kill] Process PID {} killed successfully", name);
        }
        else {
            LOG_ERROR("[HTTP POST /api/startup_item/kill] Failed to kill process PID {}", name);
        }

        crow::json::wvalue responseJson;
        responseJson["success"] = ok;
        responseJson["name"] = name;
        responseJson["location"] = location;

        crow::response response(ok ? 200 : 500, responseJson);
        return response;
        });

    //POST /api/startup_item/enable
    CROW_ROUTE(m_App, "/api/startup_item/enable").methods(crow::HTTPMethod::POST)([this](const crow::request& req) {
        auto body = crow::json::load(req.body);
        if (!body || !body.has("name") || !body.has("location")) {
            LOG_WARN("[HTTP POST /api/startup_item/enable] Bad JSON or missing name or location");
            crow::response response(400, "{\"error\": \"Bad JSON or missing name ot location\"}");
            return response;
        }

        std::string name = body["name"].s();
        std::string location = body["location"].s();

        bool ok = m_Provider->EnableStartupItem(name, location);

        if (ok) {
            LOG_INFO("[HTTP POST /api/startup_item/enable] Process PID {} killed successfully", name);
        }
        else {
            LOG_ERROR("[HTTP POST /api/startup_item/enable] Failed to kill process PID {}", name);
        }

        crow::json::wvalue responseJson;
        responseJson["success"] = ok;
        responseJson["name"] = name;
        responseJson["location"] = location;

        crow::response response(ok ? 200 : 500, responseJson);
        return response;
        });

    //GET /api/services
    CROW_ROUTE(m_App, "/api/services")([this]() {
        std::vector<ServiceItem> Services = m_Provider->GetServiceItems();
        std::vector<crow::json::wvalue> ServiceList;
        ServiceList.reserve(Services.size());
        for (const auto& S : Services) {
            crow::json::wvalue Com;
            Com["name"] = S.Name;
            Com["display_name"] = S.DisplayName;
            Com["status"] = S.Status;
            Com["start_type"] = S.StartType;
            Com["path"] = S.Path;
            ServiceList.push_back(std::move(Com));
        }
        crow::json::wvalue ServiceItems;
        ServiceItems["count"] = ServiceList.size();
        ServiceItems["service_items"] = std::move(ServiceList);
        crow::response response(ServiceItems);
        return response;
        });

    //POST /api/service_item/enable
    CROW_ROUTE(m_App, "/api/service_item/enable").methods(crow::HTTPMethod::POST)([this](const crow::request& req) {
        auto body = crow::json::load(req.body);
        if (!body || !body.has("name")) {
            LOG_WARN("[HTTP POST /api/service_item/enable] Bad JSON or missing name");
            crow::response response(400, "{\"error\": \"Bad JSON or missing name\"}");
            return response;
        }

        std::string Name = body["name"].s();

        bool ok = m_Provider->EnableServiceItem(Name);

        if (ok) {
            LOG_INFO("[HTTP POST /api/service_item/enable] Process name {} was enabled successfully", Name);
        }
        else {
            LOG_ERROR("[HTTP POST /api/service_item/enable] Failed to enable service name {}", Name);
        }

        crow::json::wvalue responseJson;
        responseJson["success"] = ok;
        responseJson["name"] = Name;

        crow::response response(ok ? 200 : 500, responseJson);
        return response;
        });

    //POST /api/service_item/disable
    CROW_ROUTE(m_App, "/api/service_item/disable").methods(crow::HTTPMethod::POST)([this](const crow::request& req) {
        auto body = crow::json::load(req.body);
        if (!body || !body.has("name")) {
            LOG_WARN("[HTTP POST /api/service_item/disable] Bad JSON or missing name");
            crow::response response(400, "{\"error\": \"Bad JSON or missing name\"}");
            return response;
        }

        std::string Name = body["name"].s();

        bool ok = m_Provider->DisableServiceItem(Name);

        if (ok) {
            LOG_INFO("[HTTP POST /api/service_item/disable] Process name {} was disabled successfully", Name);
        }
        else {
            LOG_ERROR("[HTTP POST /api/service_item/disable] Failed to disable service name {}", Name);
        }

        crow::json::wvalue responseJson;
        responseJson["success"] = ok;
        responseJson["name"] = Name;

        crow::response response(ok ? 200 : 500, responseJson);
        return response;
        });

    // POST /api/service_item/create_new
    CROW_ROUTE(m_App, "/api/service_item/create_new").methods(crow::HTTPMethod::POST)([this](const crow::request& req) {
        auto body = crow::json::load(req.body);
        if (!body || !body.has("name") || !body.has("display_name") || !body.has("path") || !body.has("auto_start")) {
            LOG_WARN("[HTTP POST /api/service_item/create_new] Bad JSON or missing paramethers");
            crow::response response(400, "{\"error\": \"Bad JSON or missing paramethers\"}");
            return response;
        }

        std::string Name = body["name"].s();
        std::string DisplayName = body["display_name"].s();
        std::string Path = body["path"].s();
        bool AutoStart =body["auto_start"].b();

        bool ok = m_Provider->CreateWinService(Name, DisplayName, Path, AutoStart);

        if (ok) {
            LOG_INFO("[HTTP POST /api/service_item/create_new] Process name {} was created successfully", Name);
        }
        else {
            LOG_ERROR("[HTTP POST /api/service_item/create_new] Failed to create service name {}", Name);
        }

        crow::json::wvalue responseJson;
        responseJson["success"] = ok;
        responseJson["name"] = Name;
        responseJson["display_name"] = DisplayName;
        responseJson["path"] = Path;
        responseJson["auto_start"] = AutoStart;

        crow::response response(ok ? 200 : 500, responseJson);
        return response;
        });

    // POST /api/service_item/delete
    CROW_ROUTE(m_App, "/api/service_item/delete").methods(crow::HTTPMethod::POST)([this](const crow::request& req) {
        auto body = crow::json::load(req.body);
        if (!body || !body.has("name")) {
            LOG_WARN("[HTTP POST /api/service_item/delete] Bad JSON or missing name");
            return crow::response(400, "{\"error\": \"Bad JSON or missing name\"}");
        }

        std::string Name = body["name"].s();

        bool ok = m_Provider->DeleteServiceItem(Name);

        if (ok) {
            LOG_INFO("[HTTP POST /api/service_item/delete] Service {} was deleted successfully", Name);
        }
        else {
            LOG_ERROR("[HTTP POST /api/service_item/delete] Failed to delete service {}", Name);
        }

        crow::json::wvalue responseJson;
        responseJson["success"] = ok;
        responseJson["name"] = Name;

        return crow::response(ok ? 200 : 500, responseJson);
        });

    //POST /api/change_rate
    CROW_ROUTE(m_App, "/api/change_rate").methods(crow::HTTPMethod::POST)([this](const crow::request& req) {
        auto body = crow::json::load(req.body);
        if (!body || !body.has("rate")) {
            LOG_WARN("[HTTP POST /api/change_rate] bad JSON or missing rate");
            return crow::response(400, "{\"error\": \"Bad JSON or missing rate\"}");
        }

        int newRate = body["rate"].i();
        if (newRate < 50) { // Валидация разумного минимума
            return crow::response(400, "{\"error\": \"Rate is too small\"}");
        }

        // Потокобезопасная запись
        m_Rate.store(newRate);

        LOG_INFO("[HTTP POST /api/change_rate] Rate updated to {} ms", newRate);

        crow::json::wvalue res;
        res["success"] = true;
        res["new_rate"] = newRate;
        return crow::response(200, res);
        });

    //POST /api/process/new
    CROW_ROUTE(m_App, "/api/process/new").methods(crow::HTTPMethod::POST)([this](const crow::request& req) {
        auto body = crow::json::load(req.body);
        if (!body || !body.has("path") || !body.has("arguments") || !body.has("is_admin")) {
            LOG_WARN("[HTTP POST /api/process/new] bad JSON or missing paramethers");
            return crow::response(400, "{\"error\": \"Bad JSON or missing paramethers\"}");
        }

        std::string Path = body["path"].s();
        std::string Args = body["arguments"].s();
        bool IsAdmin = body["is_admin"].b();

        bool ok = m_Provider->CreateNewProcess(Path, Args, IsAdmin);

        if (ok) {
            LOG_INFO("[HTTP POST /api/process/new] Process {} was launched successfully", Path);
        }
        else {
            LOG_ERROR("[HTTP POST /api/process/new] Failed to launch process {}", Path);
        }

        crow::json::wvalue responseJson;
        responseJson["success"] = ok;
        responseJson["path"] = Path;
        responseJson["arguments"] = Args;
        responseJson["is_admin"] = IsAdmin;

        return crow::response(ok ? 200 : 500, responseJson);
        });

    // ------------------------------------------------------------------------
    // Process Details & Management Routes
    // ------------------------------------------------------------------------

    // GET /api/process/details
    CROW_ROUTE(m_App, "/api/process/details")([this](const crow::request& req) {
        auto pidStr = req.url_params.get("pid");
        if (!pidStr) {
            LOG_WARN("[HTTP GET /api/process/details] Missing 'pid' query parameter");
            return crow::response(400, "{\"error\": \"Missing 'pid' parameter\"}");
        }

        unsigned long pid = 0;
        try {
            pid = std::stoul(pidStr);
        }
        catch (...) {
            return crow::response(400, "{\"error\": \"Invalid 'pid' parameter\"}");
        }

        ProcessDetails details = m_Provider->GetProcessDetails(pid);

        if (!details.Success) {
            LOG_ERROR("[HTTP GET /api/process/details] Failed to get details for PID {}", pid);
            return crow::response(404, "{\"error\": \"Failed to retrieve process details or process not found\"}");
        }

        crow::json::wvalue res;
        res["pid"] = details.Pid;
        res["priority"] = static_cast<int>(details.Priority);
        res["affinity_mask"] = details.AffinityMask;
        res["is_eco_mode"] = details.IsEcoModeEnabled;

        return crow::response(200, res);
        });

    // POST /api/process/set_priority
    CROW_ROUTE(m_App, "/api/process/set_priority").methods(crow::HTTPMethod::POST)([this](const crow::request& req) {
        auto body = crow::json::load(req.body);
        if (!body || !body.has("pid") || !body.has("priority")) {
            LOG_WARN("[HTTP POST /api/process/set_priority] Bad JSON or missing parameters");
            return crow::response(400, "{\"error\": \"Bad JSON or missing parameters\"}");
        }

        unsigned long pid = static_cast<unsigned long>(body["pid"].i());
        int priorityInt = body["priority"].i();

        if (priorityInt < 0 || priorityInt > 5) {
            return crow::response(400, "{\"error\": \"Invalid priority level range (0-5)\"}");
        }

        ProcessPriorityLevel priority = static_cast<ProcessPriorityLevel>(priorityInt);
        bool ok = m_Provider->SetProcessPriority(pid, priority);

        if (ok) {
            LOG_INFO("[HTTP POST /api/process/set_priority] Set priority level {} for PID {}", priorityInt, pid);
        }
        else {
            LOG_ERROR("[HTTP POST /api/process/set_priority] Failed to set priority for PID {}", pid);
        }

        crow::json::wvalue res;
        res["success"] = ok;
        res["pid"] = pid;
        res["priority"] = priorityInt;

        return crow::response(ok ? 200 : 500, res);
        });

    // POST /api/process/set_affinity
    CROW_ROUTE(m_App, "/api/process/set_affinity").methods(crow::HTTPMethod::POST)([this](const crow::request& req) {
        auto body = crow::json::load(req.body);
        if (!body || !body.has("pid") || !body.has("affinity_mask")) {
            LOG_WARN("[HTTP POST /api/process/set_affinity] Bad JSON or missing parameters");
            return crow::response(400, "{\"error\": \"Bad JSON or missing parameters\"}");
        }

        unsigned long pid = static_cast<unsigned long>(body["pid"].i());
        uint64_t mask = static_cast<uint64_t>(body["affinity_mask"].i());

        bool ok = m_Provider->SetProcessAffinity(pid, mask);

        if (ok) {
            LOG_INFO("[HTTP POST /api/process/set_affinity] Set affinity mask {} for PID {}", mask, pid);
        }
        else {
            LOG_ERROR("[HTTP POST /api/process/set_affinity] Failed to set affinity mask for PID {}", pid);
        }

        crow::json::wvalue res;
        res["success"] = ok;
        res["pid"] = pid;
        res["affinity_mask"] = mask;

        return crow::response(ok ? 200 : 500, res);
        });

    // POST /api/process/set_eco_mode
    CROW_ROUTE(m_App, "/api/process/set_eco_mode").methods(crow::HTTPMethod::POST)([this](const crow::request& req) {
        auto body = crow::json::load(req.body);
        if (!body || !body.has("pid") || !body.has("enable")) {
            LOG_WARN("[HTTP POST /api/process/set_eco_mode] Bad JSON or missing parameters");
            return crow::response(400, "{\"error\": \"Bad JSON or missing parameters\"}");
        }

        unsigned long pid = static_cast<unsigned long>(body["pid"].i());
        bool enable = body["enable"].b();

        bool ok = m_Provider->SetProcessEcoMode(pid, enable);

        if (ok) {
            LOG_INFO("[HTTP POST /api/process/set_eco_mode] Eco mode {} for PID {}", enable ? "enabled" : "disabled", pid);
        }
        else {
            LOG_ERROR("[HTTP POST /api/process/set_eco_mode] Failed to set eco mode for PID {}", pid);
        }

        crow::json::wvalue res;
        res["success"] = ok;
        res["pid"] = pid;
        res["is_eco_mode"] = enable;

        return crow::response(ok ? 200 : 500, res);
        });
}

void ServerApp::Run(uint16_t port) {
    LOG_INFO("Starting HTTP/WebSocket Server on port {}", port);

    m_App.port(port).multithreaded().run();
}