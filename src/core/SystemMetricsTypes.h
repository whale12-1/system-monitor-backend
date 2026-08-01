#pragma once
#include <string>
struct StartupItem {
	std::string Name;
	std::string Command;
	std::string Location;
	bool IsEnabled = true; // ← Добавить это поле
};

struct TemperatureMetrics {
	std::string SensorName; // Название датчика (например, "CPU Thermal Zone")
	double Celsius;         // Температура в градусах Цельсия
};

struct OSMetrics {
	std::string OsName;       // Например: "Windows 11 Home" или "Windows 10"
	std::string Architecture; // "x64", "x86", "ARM64"
	std::string ComputerName; // Имя ПК в сети
	std::string UserName;     // Имя текущего пользователя
	std::string BuildNumber;  // Номер сборки (например, "22631")
};

struct ProcessTimeSnapshot {
	uint64_t processTime = 0;
	uint64_t systemTime = 0;
};

struct DiskMetrics {
	std::string DriveModel;
	std::string DriveLetter;  // "C:\"
	std::string DriveType;    // "Fixed", "Removable" и т.д.
	uint64_t TotalBytes = 0;
	uint64_t FreeBytes = 0;
	double PercentOfUsage = 0.0;
};

struct ProcessMetrics {
	unsigned long Pid;
	std::string Name;
	std::string ExecutablePath;
	uint64_t MemoryUsage = 0;
	double CpuUsage = 0.0;
};

struct NetworkMetrics {
	uint64_t BytesReceived = 0; // Скачано байт (InOctets)
	uint64_t BytesSent = 0;     // Отправлено байт (OutOctets)
};

struct MemoryMetrics {
	int PercentOfUsage;
	uint64_t MemoryAmount;
	uint64_t MemoryFree;
};

struct CPUModel {
	std::string Model;
	unsigned long CoreAmount;
};

struct GPUMetrics {
	std::string Name = "N/A";
	double UsagePercent = 0.0;
	double Temperature = 0.0;
	uint64_t MemoryTotal = 0;
	uint64_t MemoryUsed = 0;
	bool IsAvailable = false;
};