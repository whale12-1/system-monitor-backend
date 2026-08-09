#include "LinuxServiceManager.h"
#include <cstdlib>
#include <fstream>
#include <sstream>

#if !defined(_WIN32)
#include <unistd.h>
#endif

std::vector<ServiceItem> LinuxServiceManager::GetServiceItems() const {
#if !defined(_WIN32)
    return GetSystemdServices();
#else
    std::vector<ServiceItem> services;
    ServiceItem dummy{};
    dummy.Name = "bluetooth.service";
    dummy.DisplayName = "Bluetooth Service";
    dummy.Status = "Running";
    dummy.StartType = "Automatic";
    dummy.Path = "/usr/libexec/bluetooth/bluetoothd";
    services.push_back(dummy);
    return services;
#endif
}

std::vector<ServiceItem> LinuxServiceManager::GetSystemdServices() const {
    std::vector<ServiceItem> services;

#if !defined(_WIN32)
    FILE* pipe = popen("systemctl list-units --type=service --all --no-legend --no-pager 2>/dev/null", "r");
    if (!pipe) return services;

    char buffer[512];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        std::istringstream ss(buffer);
        std::string name, loaded, active, sub, description;

        ss >> name >> loaded >> active >> sub;
        std::getline(ss, description);

        if (!name.empty()) {
            ServiceItem item{};
            item.Name = name;
            item.DisplayName = description.empty() ? name : description;
            item.Status = (active == "active") ? "Running" : "Stopped";
            item.StartType = (loaded == "loaded") ? "Automatic" : "Disabled";
            item.Path = "Systemd Unit";

            services.push_back(item);
        }
    }
    pclose(pipe);
#endif

    return services;
}

bool LinuxServiceManager::EnableServiceItem(const std::string& serviceName) const {
#if !defined(_WIN32)
    std::string command = "systemctl start " + serviceName + " 2>/dev/null";
    return std::system(command.c_str()) == 0;
#else
    return false;
#endif
}

bool LinuxServiceManager::DisableServiceItem(const std::string& serviceName) const {
#if !defined(_WIN32)
    std::string command = "systemctl stop " + serviceName + " 2>/dev/null";
    return std::system(command.c_str()) == 0;
#else
    return false;
#endif
}

bool LinuxServiceManager::DeleteServiceItem(const std::string& serviceName) const {
#if !defined(_WIN32)
    std::string command = "systemctl disable " + serviceName + " 2>/dev/null";
    return std::system(command.c_str()) == 0;
#else
    return false;
#endif
}

bool LinuxServiceManager::CreateServiceItem(const std::string& serviceName,
    const std::string& displayName,
    const std::string& binaryPath,
    bool autoStart) const {
    return false;
}