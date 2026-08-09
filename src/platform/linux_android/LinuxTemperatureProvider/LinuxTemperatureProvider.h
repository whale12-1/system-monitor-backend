#pragma once
#include "core/SystemMetricsTypes.h"
#include <vector>

class LinuxTemperatureProvider {
public:
    LinuxTemperatureProvider() = default;

    std::vector<TemperatureMetrics> GetTemperatures(double cpuUsageFallback = 0.0) const;
};