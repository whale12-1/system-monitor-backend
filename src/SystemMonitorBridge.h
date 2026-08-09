#pragma once
#include <stdint.h>
#include <stdbool.h>

#if defined(_WIN32) || defined(_WIN64)
#if defined(SYSTEM_MONITOR_EXPORTS) || defined(BUILDING_SYSTEM_MONITOR)
#define EXPORT_API __declspec(dllexport)
#else
#define EXPORT_API __declspec(dllexport)
#endif
#else
#define EXPORT_API __attribute__((visibility("default")))
#endif

extern "C" {

    EXPORT_API bool InitSystemMonitor();

    // Ѕыстрые скал€рные метрики
    EXPORT_API double GetCpuUsage();
    EXPORT_API uint64_t GetMemoryUsed();
    EXPORT_API uint64_t GetMemoryTotal();
    EXPORT_API uint64_t GetTickTime();

    // ”правление
    EXPORT_API bool KillProcessByPid(unsigned long pid);
    EXPORT_API bool SetProcessPriorityLevel(unsigned long pid, int priority);

    // —ложные списки в формате JSON (возвращает указатель на C-строку)
    EXPORT_API const char* GetProcessesJson();
    EXPORT_API const char* GetDiskMetricsJson();
    EXPORT_API void FreeJsonString(const char* ptr); // ƒл€ очистки пам€ти на стороне C++
    EXPORT_API void StartCrowServer(uint16_t port);
    EXPORT_API void StopCrowServer();
}