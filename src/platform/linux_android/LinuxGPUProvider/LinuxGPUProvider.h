#pragma once
#include "core/SystemMetricsTypes.h"
#include <vector>

class LinuxGPUProvider {
public:
    LinuxGPUProvider() = default;

    std::vector<GPUMetrics> GetGPUMetrics() const;
};