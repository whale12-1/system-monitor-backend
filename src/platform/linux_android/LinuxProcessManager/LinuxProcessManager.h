#pragma once
#include "core/SystemMetricsTypes.h"
#include <vector>
#include <string>

class LinuxProcessManager {
public:
    LinuxProcessManager() = default;

    std::vector<ProcessMetrics> GetProcesses() const;
    bool KillProcess(unsigned long pid) const;
    bool OpenFileLocation(unsigned long pid) const;
    bool CreateNewProcess(const std::string& executablePath,
        const std::string& arguments = "",
        bool asAdmin = false) const;
    ProcessDetails GetProcessDetails(unsigned long pid) const;
    bool SetProcessPriority(unsigned long pid, ProcessPriorityLevel priority) const;
    bool SetProcessAffinity(unsigned long pid, uint64_t affinityMask) const;
    bool SetProcessEcoMode(unsigned long pid, bool enableEcoMode) const;

private:
    // Базовый парсинг процессов через /proc/
    std::vector<ProcessMetrics> GetProcessesFromProcFs() const;
};