#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN

#include "platform/MetricsProviderFactory.h"
#include "state/SharedState.h"
#include "server/ServerApp.h"
#include "Logger.h"
#include "ProcessLauncher.h" // <-- Наш чистый заголовок

#include <thread>
#include <chrono>

int main(int argc, char* argv[]) {
    Logger::Init();
    LOG_INFO("System Monitor Backend starting...");

    // 1. Создаем провайдер метрик через фабрику
    auto provider = MetricsProviderFactory::Create();

    // 2. Инициализируем хранилище метрик
    SharedState state;

    // 3. Создаем сервер
    ServerApp server(state, std::move(provider));

    // 4. Запускаем сервер в фоновом потоке
    std::thread serverThread([&server]() {
        server.Run(18080);
        });

    // Пауза 150 мс, чтобы Crow забинддил порт
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    // 5. Автоматически запускаем UI
    if (!LaunchFrontendUI(argc, argv)) {
        std::cerr << "Не удалось запустить Frontend UI\n";
    }

    // 6. Ожидаем завершения сервера
    if (serverThread.joinable()) {
        serverThread.join();
    }

    // 7. Закрываем UI при остановке
    TerminateFrontendUI();

    return 0;
}