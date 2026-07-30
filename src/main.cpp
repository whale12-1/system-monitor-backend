#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN

#include "platform/MetricsProviderFactory.h"
#include "state/SharedState.h"
#include "server/ServerApp.h"

int main() {
    // 1. Создаем провайдер метрик через фабрику (под нужную ОС)
    auto provider = MetricsProviderFactory::Create();

    // 2. Инициализируем хранилище метрик
    SharedState state;

    // 3. Создаем и запускаем HTTP/WebSocket сервер
    ServerApp server(state, std::move(provider));
    server.Run(18080);

    return 0;
}