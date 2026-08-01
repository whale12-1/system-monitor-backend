#pragma once
#include <vector>
#include <cstdint>
#include "core/SystemMetricsTypes.h"

class WinSystemInfoProvider {
public:
    WinSystemInfoProvider() = default;

    MemoryMetrics GetMemoryMetrics() const;
    double GetCPUMetrics() const;
    NetworkMetrics GetNetworkMetrics() const;
    std::vector<DiskMetrics> GetDiskMetrics() const;
    OSMetrics GetOSMetrics() const;
    CPUModel GetCPUModel() const;
    uint64_t GetTickTime() const;
};