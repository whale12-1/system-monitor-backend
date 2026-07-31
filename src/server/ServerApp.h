#pragma once
#include <memory>
#include <mutex>
#include <unordered_set>
#include <thread>

#include "crow.h"
#include "crow/middlewares/cors.h"
#include "core/ISystemMetricsProvider.h"
#include "state/SharedState.h"


class ServerApp {
public:
    ServerApp(SharedState& state, std::unique_ptr<ISystemMetricsProvider> provider);
    ~ServerApp();

    // Запрещаем копирование
    ServerApp(const ServerApp&) = delete;
    ServerApp& operator=(const ServerApp&) = delete;

    // Запуск сервера на указанном порту
    void Run(uint16_t port = 18080);

private:
    void SetupRoutes();
    void StartCollectorThread();

private:
    SharedState& m_State;
    std::unique_ptr<ISystemMetricsProvider> m_Provider;
    crow::App<crow::CORSHandler> m_App;

    // WebSocket соединения и мьютекс
    std::mutex m_WsMutex;
    std::unordered_set<crow::websocket::connection*> m_ActiveConnections;

    // Фоновый поток сбора
    std::jthread m_CollectorThread;
};
