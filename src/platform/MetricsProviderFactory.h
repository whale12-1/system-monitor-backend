#pragma once
#include "core/ISystemMetricsProvider.h"
#include <memory>
#include "windows/WindowsApiMetricsProvider.h"

class MetricsProviderFactory {
public:
    static std::unique_ptr<ISystemMetricsProvider> Create() {
#if defined(_WIN32) || defined(_WIN64)
        return std::make_unique<WindowsApiMetricsProvider>();
#elif defined(__linux__)
        return std::make_unique<LinuxMetricsProvider>(); // В будущем для /proc
#else
#error "Unsupported Platform"
#endif
    }
};