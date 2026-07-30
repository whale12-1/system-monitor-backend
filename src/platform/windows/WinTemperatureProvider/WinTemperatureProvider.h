#pragma once
#include <vector>
#include "core/SystemMetricsTypes.h"

class WinTemperatureProvider {
public:
    WinTemperatureProvider() = default;

    std::vector<TemperatureMetrics> GetTemperatures(double fallbackCpuUsage) const;
    std::vector<TemperatureMetrics> GetTemperaturesUsingWinApi() const;
};
