#pragma once
#include <vector>
#include <cstdint>
#include "core/SystemMetricsTypes.h" // Укажи актуальный путь к типам данных

#pragma once
#include <string>


// Константы приоритета процесса для более удобного API

class WinProcessManager {
public:
    WinProcessManager() = default;

    std::vector<ProcessMetrics> GetProcesses() const;
    bool KillProcess(unsigned long pid) const;
    bool CreateNewProcess(const std::string& executablePath,
        const std::string& arguments = "",
        bool asAdmin = false) const;
    bool OpenFileLocation(unsigned long pid) const;

    bool SetProcessPriority(unsigned long pid, ProcessPriorityLevel priority) const;

    // Изменение affinity mask (привязка к логическим ядрам через маску, где bit 0 = Core 0, bit 1 = Core 1 и т.д.)
    bool SetProcessAffinity(unsigned long pid, uint64_t affinityMask) const;

    // Включение/выключение режима энергосбережения (EcoMode / Power Throttling)
    bool SetProcessEcoMode(unsigned long pid, bool enableEcoMode) const;

    ProcessDetails GetProcessDetails(unsigned long pid) const;
};