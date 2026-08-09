#pragma once
#include "core/SystemMetricsTypes.h"
#include <vector>
#include <string>

class LinuxServiceManager {
public:
    LinuxServiceManager() = default;

    std::vector<ServiceItem> GetServiceItems() const;
    bool DeleteServiceItem(const std::string& serviceName) const;
    bool EnableServiceItem(const std::string& serviceName) const;
    bool DisableServiceItem(const std::string& serviceName) const;
    bool CreateServiceItem(const std::string& serviceName,
        const std::string& displayName,
        const std::string& binaryPath,
        bool autoStart = true) const;

private:
    std::vector<ServiceItem> GetSystemdServices() const;
};