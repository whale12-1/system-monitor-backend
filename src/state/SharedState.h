#pragma once
#include "./core/SystemMetricsTypes.h"
#include <shared_mutex>
#include <mutex>
// Структура скорости сети
struct NetworkUsage {
    double DownloadBytesPerSec = 0.0;
    double UploadBytesPerSec = 0.0;
};

struct SystemSnapshot {
    MemoryMetrics MemorySnap{};
    double CPUUsage = 0.0;
    NetworkUsage NetworkSnap{}; // <-- Добавили сеть
    std::vector<ProcessMetrics> Processes;
    std::vector<TemperatureMetrics> Temperatures;
    uint64_t UptimeSeconds = 0;
    std::vector<GPUMetrics> GPUMetrics;
};

class SharedState {
private:
    SystemSnapshot m_CurrentSnapshot;
    mutable std::shared_mutex m_RWMutex;

public:
    SharedState() = default;

    // Сбор данных снаружи (фоновым потоком)
    void Update(
        const MemoryMetrics& mem,
        double cpu,
        const NetworkUsage& net,
        std::vector<ProcessMetrics> processes,
        std::vector<TemperatureMetrics> temperature,
        uint64_t uptime,
        std::vector<GPUMetrics> gpumetrics) 
    {
        std::unique_lock<std::shared_mutex> lock(m_RWMutex);
        m_CurrentSnapshot.MemorySnap = mem;
        m_CurrentSnapshot.CPUUsage = cpu;
        m_CurrentSnapshot.NetworkSnap = net;
        m_CurrentSnapshot.Processes = std::move(processes);
        m_CurrentSnapshot.Temperatures = std::move(temperature);
        m_CurrentSnapshot.UptimeSeconds = uptime;
        m_CurrentSnapshot.GPUMetrics = gpumetrics;
    }

    // Потокобезопасное чтение для Crow-хэндлеров
    SystemSnapshot GetSnapshot() const {
        std::shared_lock<std::shared_mutex> lock(m_RWMutex);
        return m_CurrentSnapshot;
    }
};
