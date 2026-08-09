#pragma once
#include "../../core/ISystemMetricsProvider.h"

#include "LinuxGPUProvider/LinuxGPUProvider.h"
#include "LinuxProcessManager/LinuxProcessManager.h"
#include "LinuxServiceManager/LinuxServiceManager.h"
#include "LinuxStartupManager/LinuxStartupManager.h"
#include "LinuxSystemInfoProvider/LinuxSystemInfoProvider.h"
#include "LinuxTemperatureProvider/LinuxTemperatureProvider.h"

class LinuxMetricsProvider : public ISystemMetricsProvider {
private:
    LinuxProcessManager      m_ProcessManager;
    LinuxStartupManager      m_StartupManager;
    LinuxSystemInfoProvider  m_SystemInfoProvider;
    LinuxTemperatureProvider m_TemperatureProvider;
    LinuxGPUProvider         m_GPUProvider;
    LinuxServiceManager      m_ServiceManager;

public:
    LinuxMetricsProvider() = default;
    ~LinuxMetricsProvider() override = default;

    // --- Метрики системы ---
    OSMetrics GetOSMetrics() const override {
        return m_SystemInfoProvider.GetOSMetrics();
    }

    MemoryMetrics GetMemoryMetrics() const override {
        return m_SystemInfoProvider.GetMemoryMetrics();
    }

    double GetCPUMetrics() const override {
        return m_SystemInfoProvider.GetCPUMetrics();
    }

    NetworkMetrics GetNetworkMetrics() const override {
        return m_SystemInfoProvider.GetNetworkMetrics();
    }

    std::vector<DiskMetrics> GetDiskMetrics() const override {
        return m_SystemInfoProvider.GetDiskMetrics();
    }

    CPUModel GetCPUModel() const override {
        return m_SystemInfoProvider.GetCPUModel();
    }

    uint64_t GetTickTime() const override {
        return m_SystemInfoProvider.GetTickTime();
    }

    // --- Управление процессами ---
    std::vector<ProcessMetrics> GetProcesses() const override {
        return m_ProcessManager.GetProcesses();
    }

    bool KillProcess(unsigned long pid) const override {
        return m_ProcessManager.KillProcess(pid);
    }

    bool OpenFileLocation(unsigned long pid) const override {
        return m_ProcessManager.OpenFileLocation(pid);
    }

    bool CreateNewProcess(const std::string& executablePath,
        const std::string& arguments = "",
        bool asAdmin = false) const override {
        return m_ProcessManager.CreateNewProcess(executablePath, arguments, asAdmin);
    }

    ProcessDetails GetProcessDetails(unsigned long pid) const override {
        return m_ProcessManager.GetProcessDetails(pid);
    }

    bool SetProcessPriority(unsigned long pid, ProcessPriorityLevel priority) const override {
        return m_ProcessManager.SetProcessPriority(pid, priority);
    }

    bool SetProcessAffinity(unsigned long pid, uint64_t affinityMask) const override {
        return m_ProcessManager.SetProcessAffinity(pid, affinityMask);
    }

    bool SetProcessEcoMode(unsigned long pid, bool enableEcoMode) const override {
        return m_ProcessManager.SetProcessEcoMode(pid, enableEcoMode);
    }

    // --- Автозагрузка ---
    std::vector<StartupItem> GetStartupItems() const override {
        return m_StartupManager.GetStartupItems();
    }

    bool RemoveStartupItem(const std::string& name, const std::string& location) const override {
        return m_StartupManager.RemoveStartupItem(name, location);
    }

    bool EnableStartupItem(const std::string& name, const std::string& location) const override {
        return m_StartupManager.EnableStartupItem(name, location);
    }

    // --- Температуры и Видеокарта ---
    std::vector<TemperatureMetrics> GetTemperatures() const override {
        return m_TemperatureProvider.GetTemperatures(m_SystemInfoProvider.GetCPUMetrics());
    }

    std::vector<GPUMetrics> GetGPUMetrics() const override {
        return m_GPUProvider.GetGPUMetrics();
    }

    // --- Службы ---
    bool DeleteServiceItem(const std::string& serviceName) const override {
        return m_ServiceManager.DeleteServiceItem(serviceName);
    }

    bool EnableServiceItem(const std::string& serviceName) const override {
        return m_ServiceManager.EnableServiceItem(serviceName);
    }

    bool DisableServiceItem(const std::string& serviceName) const override {
        return m_ServiceManager.DisableServiceItem(serviceName);
    }

    std::vector<ServiceItem> GetServiceItems() const override {
        return m_ServiceManager.GetServiceItems();
    }

    bool CreateWinService(const std::string& serviceName,
        const std::string& displayName,
        const std::string& binaryPath,
        bool autoStart = true) const override {
        return m_ServiceManager.CreateServiceItem(serviceName, displayName, binaryPath, autoStart);
    }
};