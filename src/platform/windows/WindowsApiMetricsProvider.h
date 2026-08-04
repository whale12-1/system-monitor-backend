#pragma once
#include "core/ISystemMetricsProvider.h" // или "../../core/ISystemMetricsProvider.h" в зависимости от настройки include path

// Подключаем заголовки из соответствующих папок-модулей
#include "WinProcessManager/WinProcessManager.h"
#include "WinStartupManager/WinStartupManager.h"
#include "WinSystemInfoProvider/WinSystemInfoProvider.h"
#include "WinTemperatureProvider/WinTemperatureProvider.h"
#include "WinGPUProvider/WinGPUProvider.h"
#include "WinServicesManager/WinServicesManager.h"

class WindowsApiMetricsProvider : public ISystemMetricsProvider {
private:
    WinProcessManager     m_ProcessManager;
    WinStartupManager     m_StartupManager;
    WinSystemInfoProvider m_SystemInfoProvider;
    WinTemperatureProvider m_TemperatureProvider;
    WinGPUProvider m_GPUProvider;
    WinServicesManager m_ServiceManager;

public:
    WindowsApiMetricsProvider() = default;

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

    // --- Автозагрузка ---
    std::vector<StartupItem> GetStartupItems() const override {
        return m_StartupManager.GetStartupItems();
    }

    bool RemoveStartupItem(const std::string& name, const std::string& location) const override {
        return m_StartupManager.RemoveStartupItem(name, location);
    }
    bool EnableStartupItem(const std::string& name, const std::string& location) const {
        return m_StartupManager.EnableStartupItem(name, location);
    }

    // --- Температуры ---
    std::vector<TemperatureMetrics> GetTemperatures() const override {
        // Передаем загрузку ЦП в качестве fallback для расчетов
        return m_TemperatureProvider.GetTemperatures(m_SystemInfoProvider.GetCPUMetrics());
    }
    std::vector<TemperatureMetrics> GetTemperaturesUsingWinApi() const {
        return m_TemperatureProvider.GetTemperaturesUsingWinApi();
    }
    std::vector<GPUMetrics> GetGPUMetrics() const {
        return m_GPUProvider.GetGPUMetrics();
    }
    bool DeleteServiceItem(const std::string& serviceName) const {
        return m_ServiceManager.DeleteWinService(serviceName);
    }
    bool EnableServiceItem(const std::string& serviceName) const {
        return m_ServiceManager.StartWinService(serviceName);
    }
    bool DisableServiceItem(const std::string& serviceName) const {
        return m_ServiceManager.StopWinService(serviceName);
    }
    std::vector<ServiceItem> GetServiceItems() const {
        return m_ServiceManager.GetServices();
    }
    bool CreateWinService(const std::string& serviceName,
        const std::string& displayName,
        const std::string& binaryPath,
        bool autoStart = true) const 
    {
        return m_ServiceManager.CreateWinService(serviceName, displayName, binaryPath, autoStart);
    }

    bool CreateNewProcess(const std::string& executablePath,
        const std::string& arguments = "",
        bool asAdmin = false) const
    {
        return m_ProcessManager.CreateNewProcess(executablePath, arguments, asAdmin);
    }
};
