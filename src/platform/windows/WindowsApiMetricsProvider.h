#pragma once
#include "core/ISystemMetricsProvider.h" // или "../../core/ISystemMetricsProvider.h" в зависимости от настройки include path

// Подключаем заголовки из соответствующих папок-модулей
#include "WinProcessManager/WinProcessManager.h"
#include "WinStartupManager/WinStartupManager.h"
#include "WinSystemInfoProvider/WinSystemInfoProvider.h"
#include "WinTemperatureProvider/WinTemperatureProvider.h"
#include "WinGPUProvider/WinGPUProvider.h"

class WindowsApiMetricsProvider : public ISystemMetricsProvider {
private:
    WinProcessManager     m_ProcessManager;
    WinStartupManager     m_StartupManager;
    WinSystemInfoProvider m_SystemInfoProvider;
    WinTemperatureProvider m_TemperatureProvider;
    WinGPUProvider m_GPUProvider;

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

};
