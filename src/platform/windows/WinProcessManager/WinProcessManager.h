#pragma once
#include <vector>
#include <cstdint>
#include "core/SystemMetricsTypes.h" // Укажи актуальный путь к типам данных

class WinProcessManager {
public:
    WinProcessManager() = default;

    std::vector<ProcessMetrics> GetProcesses() const;
    bool KillProcess(unsigned long pid) const;
    bool OpenFileLocation(unsigned long pid) const;
};