#pragma once
#include "core/ISystemMetricsProvider.h"
#include <memory>

#if defined(_WIN32) || defined(_WIN64)
#include "platform/windows/WindowsApiMetricsProvider.h" 
#elif defined(__linux__) || defined(__ANDROID__)
#include "platform/linux_android/LinuxMetricsProvider.h"
#endif

class MetricsProviderFactory {
public:
    static std::unique_ptr<ISystemMetricsProvider> Create() {
#if defined(_WIN32) || defined(_WIN64)
        return std::make_unique<WindowsApiMetricsProvider>();
#elif defined(__linux__) || defined(__ANDROID__)
        return std::make_unique<LinuxMetricsProvider>();
#else
#error "Unsupported Platform"
#endif
    }
};