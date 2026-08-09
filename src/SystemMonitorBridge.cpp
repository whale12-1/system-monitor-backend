#include "SystemMonitorBridge.h"
#include "platform/MetricsProviderFactory.h"
#include "server/ServerApp.h"
#include <memory>
#include <string>
#include "Logger.h"

static std::unique_ptr<ISystemMetricsProvider> g_provider = nullptr;
static std::string g_jsonBuffer; // Буфер для хранения строки до очистки
static std::unique_ptr<ServerApp> g_serverApp = nullptr;
static SharedState g_sharedState;

extern "C" {

    EXPORT_API bool InitSystemMonitor() {
        if (!g_provider) {
            g_provider = MetricsProviderFactory::Create();
        }
        return g_provider != nullptr;
    }

    EXPORT_API double GetCpuUsage() {
        return g_provider ? g_provider->GetCPUMetrics() : 0.0;
    }

    EXPORT_API uint64_t GetMemoryUsed() {
        return g_provider ?( g_provider->GetMemoryMetrics().MemoryAmount - g_provider->GetMemoryMetrics().MemoryFree) : 0;
    }

    EXPORT_API uint64_t GetMemoryTotal() {
        return g_provider ? g_provider->GetMemoryMetrics().MemoryAmount : 0;
    }

    EXPORT_API uint64_t GetTickTime() {
        return g_provider ? g_provider->GetTickTime() : 0;
    }

    EXPORT_API bool KillProcessByPid(unsigned long pid) {
        return g_provider ? g_provider->KillProcess(pid) : false;
    }

    EXPORT_API bool SetProcessPriorityLevel(unsigned long pid, int priority) {
        if (!g_provider) return false;
        return g_provider->SetProcessPriority(pid, static_cast<ProcessPriorityLevel>(priority));
    }
    EXPORT_API const char* GetProcessesJson() {
        if (!g_provider) return "[]";

        auto processes = g_provider->GetProcesses();
        g_jsonBuffer = "[";
        for (size_t i = 0; i < processes.size(); ++i) {
            g_jsonBuffer += "{\"pid\":" + std::to_string(processes[i].Pid) +
                ",\"name\":\"" + processes[i].Name + "\"" +
                ",\"memory\":" + std::to_string(processes[i].MemoryUsage) + "}";
            if (i + 1 < processes.size()) g_jsonBuffer += ",";
        }
        g_jsonBuffer += "]";
        return g_jsonBuffer.c_str();
    }

    EXPORT_API void FreeJsonString(const char* ptr) {
        g_jsonBuffer.clear();
        g_jsonBuffer.shrink_to_fit();
    }

    EXPORT_API void StartCrowServer(uint16_t port) {
        if (g_serverApp) {
            std::cout << "[C++ Bridge] Server already running!" << std::endl;
            return;
        }

        std::cout << "[C++ Bridge] Creating MetricsProvider..." << std::endl;
        auto provider = MetricsProviderFactory::Create();

        if (!provider) {
            std::cout << "[C++ Bridge ERROR] MetricsProviderFactory returned NULLPTR!" << std::endl;
            LOG_ERROR("Failed to create MetricsProvider");
            return;
        }

        std::cout << "[C++ Bridge SUCCESS] MetricsProvider created. Starting Crow on port " << port << "..." << std::endl;

        g_serverApp = std::make_unique<ServerApp>(g_sharedState, std::move(provider));
        g_serverApp->Run(port);
    }

    EXPORT_API void StopCrowServer() {
        if (g_serverApp) {
            g_serverApp.reset();
        }
    }


}