#pragma once
#include "core/SystemMetricsTypes.h"
#include <vector>

class LinuxSystemInfoProvider {
public:
    LinuxSystemInfoProvider() = default;

    OSMetrics GetOSMetrics() const;
    MemoryMetrics GetMemoryMetrics() const;
    double GetCPUMetrics() const;
    NetworkMetrics GetNetworkMetrics() const;
    std::vector<DiskMetrics> GetDiskMetrics() const;
    CPUModel GetCPUModel() const;
    uint64_t GetTickTime() const;
};