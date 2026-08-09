#pragma once
#include "core/SystemMetricsTypes.h"
#include <vector>
#include <string>

class LinuxStartupManager {
public:
    LinuxStartupManager() = default;

    std::vector<StartupItem> GetStartupItems() const;
    bool RemoveStartupItem(const std::string& name, const std::string& location) const;
    bool EnableStartupItem(const std::string& name, const std::string& location) const;

private:
    std::vector<StartupItem> GetXdgStartupItems() const;
};