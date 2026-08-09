#include "LinuxTemperatureProvider.h"
#include <fstream>
#include <string>
#include <vector>

#if !defined(_WIN32)
#include <filesystem>
namespace fs = std::filesystem;
#endif

std::vector<TemperatureMetrics> LinuxTemperatureProvider::GetTemperatures(double cpuUsageFallback) const {
    std::vector<TemperatureMetrics> temperatures;

#if !defined(_WIN32)
    const std::string thermalBasePath = "/sys/class/thermal";

    if (fs::exists(thermalBasePath) && fs::is_directory(thermalBasePath)) {
        for (const auto& entry : fs::directory_iterator(thermalBasePath)) {
            std::string dirName = entry.path().filename().string();

            if (dirName.rfind("thermal_zone", 0) == 0) {
                std::string typePath = entry.path().string() + "/type";
                std::string tempPath = entry.path().string() + "/temp";

                std::ifstream typeFile(typePath);
                std::ifstream tempFile(tempPath);

                if (typeFile.is_open() && tempFile.is_open()) {
                    std::string sensorName;
                    double rawTemp = 0.0;

                    std::getline(typeFile, sensorName);
                    tempFile >> rawTemp;

                    if (!sensorName.empty() && rawTemp > 0.0) {
                        TemperatureMetrics metric{};
                        metric.SensorName = sensorName;
                        metric.Celsius = (rawTemp > 1000.0) ? (rawTemp / 1000.0) : rawTemp;

                        temperatures.push_back(metric);
                    }
                }
            }
        }
    }
#else
    TemperatureMetrics cpuTemp{};
    cpuTemp.SensorName = "CPU Thermal Zone (Stub)";
    cpuTemp.Celsius = 42.5;
    temperatures.push_back(cpuTemp);
#endif

    return temperatures;
}