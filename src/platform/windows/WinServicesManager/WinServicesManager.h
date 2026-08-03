#pragma once
#include <vector>
#include <string>
#include "core/SystemMetricsTypes.h"

class WinServicesManager {
public:
    WinServicesManager() = default;

    // 1. Просмотр всех служб Windows
    std::vector<ServiceItem> GetServices() const;

    // 2. Добавление (Создание) новой службы
    // (Требует прав Администратора)
    bool CreateWinService(const std::string& serviceName,
        const std::string& displayName,
        const std::string& binaryPath,
        bool autoStart = true) const;

    // 3. Удаление службы
    // (Требует прав Администратора)
    bool DeleteWinService(const std::string& serviceName) const;

    // 4. Дополнительное управление состоянием (Запуск / Остановка)
    bool StartWinService(const std::string& serviceName) const;
    bool StopWinService(const std::string& serviceName) const;
};