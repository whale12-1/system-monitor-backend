#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN

#include "SystemMonitorBridge.h"
#include "platform/MetricsProviderFactory.h"
#include "state/SharedState.h"
#include "server/ServerApp.h"
#include "Logger.h"
#include "ProcessLauncher.h"

#include <thread>
#include <chrono>

int main(int argc, char* argv[]) {
    Logger::Init();
    LOG_INFO("System Monitor Backend starting...");

    // 1. Инициализация C-API моста
    if (!InitSystemMonitor()) {
        LOG_ERROR("Failed to initialize System Monitor backend!");
        return -1;
    }

    // 2. Инициализация Crow сервера (для Windows/Linux Web API)
    auto provider = MetricsProviderFactory::Create();
    SharedState state;
    ServerApp server(state, std::move(provider));

    std::thread serverThread([&server]() {
        server.Run(18080);
        });

    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    // 3. Запуск внешнего процесса UI на Windows
    if (!LaunchFrontendUI(argc, argv)) {
        std::cerr << "Не удалось запустить Frontend UI\n";
    }

    if (serverThread.joinable()) {
        serverThread.join();
    }

    TerminateFrontendUI();
    return 0;
}