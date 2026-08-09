#include "LinuxGPUProvider.h"
#include <fstream>
#include <string>
#include <vector>

#if !defined(_WIN32)
#include <filesystem>
namespace fs = std::filesystem;
#endif

std::vector<GPUMetrics> LinuxGPUProvider::GetGPUMetrics() const {
    std::vector<GPUMetrics> gpuList;

#if !defined(_WIN32)
    // Сканирование /sys/class/drm для нативных графических адаптеров Linux
    const std::string drmPath = "/sys/class/drm";
    if (fs::exists(drmPath) && fs::is_directory(drmPath)) {
        for (const auto& entry : fs::directory_iterator(drmPath)) {
            std::string dirName = entry.path().filename().string();
            if (dirName.rfind("card", 0) == 0 && dirName.find('-') == std::string::npos) {
                GPUMetrics gpu{};
                gpu.IsAvailable = true;
                gpu.Name = "Generic Linux GPU (" + dirName + ")";

                std::string deviceNamePath = entry.path().string() + "/device/vendor";
                std::ifstream vendorFile(deviceNamePath);
                if (vendorFile.is_open()) {
                    std::string vendorId;
                    vendorFile >> vendorId;
                    if (vendorId == "0x10de") gpu.Name = "NVIDIA GPU";
                    else if (vendorId == "0x1002" || vendorId == "0x1006") gpu.Name = "AMD Radeon GPU";
                    else if (vendorId == "0x8086") gpu.Name = "Intel Integrated Graphics";
                }

                std::string busyPath = entry.path().string() + "/device/gpu_busy_percent";
                std::ifstream busyFile(busyPath);
                if (busyFile.is_open()) {
                    busyFile >> gpu.UsagePercent;
                }
                else {
                    gpu.UsagePercent = 0.0;
                }

                gpuList.push_back(gpu);
            }
        }
    }

    if (gpuList.empty()) {
        GPUMetrics fallbackGpu{};
        fallbackGpu.Name = "Linux GPU (Not Found)";
        fallbackGpu.IsAvailable = false;
        gpuList.push_back(fallbackGpu);
    }
#else
    GPUMetrics dummy{};
    dummy.Name = "Linux GPU (Stub)";
    dummy.UsagePercent = 15.0;
    dummy.Temperature = 55.0;
    dummy.IsAvailable = true;
    gpuList.push_back(dummy);
#endif

    return gpuList;
}