#pragma once

#pragma once
#include "SystemMetricsTypes.h"
#include <vector>
#include <memory>

class ISystemMetricsProvider {
public:
    virtual ~ISystemMetricsProvider() = default;

    virtual OSMetrics GetOSMetrics() const = 0;
    virtual MemoryMetrics GetMemoryMetrics() const = 0;
    virtual double GetCPUMetrics() const = 0;
    virtual NetworkMetrics GetNetworkMetrics() const = 0;
    virtual std::vector<ProcessMetrics> GetProcesses() const = 0;
    virtual std::vector<DiskMetrics> GetDiskMetrics() const = 0;
    virtual uint64_t GetTickTime() const = 0;
    virtual bool KillProcess(unsigned long pid) const = 0;
    virtual CPUModel GetCPUModel() const = 0;
    virtual std::vector<StartupItem> GetStartupItems() const = 0;
    virtual std::vector<TemperatureMetrics> GetTemperatures() const = 0;
    virtual bool RemoveStartupItem(const std::string& name, const std::string& location) const = 0;
    virtual bool EnableStartupItem(const std::string& name, const std::string& location) const = 0;
    virtual bool OpenFileLocation(unsigned long pid) const = 0;
    virtual std::vector<GPUMetrics> GetGPUMetrics() const = 0;

    virtual bool DeleteServiceItem(const std::string& serviceName) const = 0;
    virtual bool EnableServiceItem(const std::string& serviceName) const = 0;
    virtual bool DisableServiceItem(const std::string& serviceName) const = 0;
    virtual std::vector<ServiceItem> GetServiceItems() const = 0;
    virtual bool CreateWinService(const std::string& serviceName,
        const std::string& displayName,
        const std::string& binaryPath,
        bool autoStart = true) const = 0;
};
