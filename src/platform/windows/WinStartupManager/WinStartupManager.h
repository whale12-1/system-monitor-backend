#pragma once
#include <vector>
#include <string>
#include "core/SystemMetricsTypes.h"

class WinStartupManager {
public:
    WinStartupManager() = default;

    std::vector<StartupItem> GetStartupItems() const;
    bool RemoveStartupItem(const std::string& name, const std::string& location) const;
};