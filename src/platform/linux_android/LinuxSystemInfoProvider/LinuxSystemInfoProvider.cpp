#include "LinuxSystemInfoProvider.h"
#include <fstream>
#include <sstream>
#include <string>
#include <algorithm>
#include <chrono>

#if !defined(_WIN32)
#include <unistd.h>
#include <sys/utsname.h>
#include <sys/statvfs.h>
#endif

OSMetrics LinuxSystemInfoProvider::GetOSMetrics() const {
    OSMetrics metrics{};

#if !defined(_WIN32)
    struct utsname buf {};
    if (uname(&buf) == 0) {
        metrics.OsName = buf.sysname;
        metrics.Architecture = buf.machine;
        metrics.BuildNumber = buf.release;
    }
    else {
        metrics.OsName = "Linux";
        metrics.Architecture = "N/A";
        metrics.BuildNumber = "N/A";
    }

    char hostname[256] = { 0 };
    if (gethostname(hostname, sizeof(hostname)) == 0) {
        metrics.ComputerName = hostname;
    }
    else {
        metrics.ComputerName = "localhost";
    }

    const char* user = getenv("USER");
    if (!user) user = getenv("LOGNAME");
    if (!user) {
        metrics.UserName = "user_" + std::to_string(getuid());
    }
    else {
        metrics.UserName = user;
    }
#else
    metrics.OsName = "Linux (Stub for MSVC Build)";
    metrics.Architecture = "x64";
    metrics.BuildNumber = "N/A";
    metrics.ComputerName = "localhost";
    metrics.UserName = "user";
#endif

    return metrics;
}

MemoryMetrics LinuxSystemInfoProvider::GetMemoryMetrics() const {
    MemoryMetrics metrics{};
    std::string output;
    std::ifstream file("/proc/meminfo");
    if (file.is_open()) {
        std::string line;
        while (std::getline(file, line)) output += line + "\n";
    }

    if (output.empty()) return metrics;

    std::istringstream fileStream(output);
    std::string key;
    uint64_t value = 0;
    std::string unit;
#if !defined(_WIN32)
    uint64_t totalKb = 0, availableKb = 0;
    while (fileStream >> key >> value) {
        if (fileStream.peek() != '\n' && fileStream.peek() != EOF) {
            fileStream >> unit;
        }

        if (key == "MemTotal:") totalKb = value;
        else if (key == "MemAvailable:") availableKb = value;
    }

    if (totalKb > 0) {
        metrics.MemoryAmount = totalKb * 1024;
        metrics.MemoryFree = availableKb * 1024;
        uint64_t used = metrics.MemoryAmount - metrics.MemoryFree;
        metrics.PercentOfUsage = static_cast<int>((static_cast<double>(used) / metrics.MemoryAmount) * 100.0);
    }
#endif
    return metrics;
}

double LinuxSystemInfoProvider::GetCPUMetrics() const {
    static uint64_t prevIdle = 0;
    static uint64_t prevTotal = 0;

    std::string output;
    std::ifstream file("/proc/stat");
    if (file.is_open()) {
        std::string line;
        if (std::getline(file, line)) {
            output = line;
        }
    }

    if (output.empty()) return 0.0;
#if !defined(_WIN32)
    std::istringstream fileStream(output);
    std::string label;
    fileStream >> label;
    if (label != "cpu") return 0.0;

    uint64_t u = 0, n = 0, s = 0, i = 0, io = 0, irq = 0, sirq = 0, steal = 0;
    if (fileStream >> u >> n >> s >> i >> io >> irq >> sirq >> steal) {
        uint64_t idle = i + io;
        uint64_t total = idle + u + n + s + irq + sirq + steal;

        uint64_t dTotal = total - prevTotal;
        uint64_t dIdle = idle - prevIdle;

        prevTotal = total;
        prevIdle = idle;

        if (dTotal == 0) return 0.0;
        double usage = ((static_cast<double>(dTotal - dIdle) / dTotal) * 100.0);
        return (usage < 0.0) ? 0.0 : ((usage > 100.0) ? 100.0 : usage);
    }
#endif
    return 0.0;
}

NetworkMetrics LinuxSystemInfoProvider::GetNetworkMetrics() const {
    NetworkMetrics metrics{};
    std::string output;

    std::ifstream file("/proc/net/dev");
    if (file.is_open()) {
        std::string line;
        while (std::getline(file, line)) output += line + "\n";
    }

    if (output.empty()) return metrics;

    std::istringstream fileStream(output);
    std::string line;
    std::getline(fileStream, line);
    std::getline(fileStream, line);

    while (std::getline(fileStream, line)) {
        std::istringstream ss(line);
        std::string iface;
        ss >> iface;

        if (!iface.empty() && iface.back() == ':') {
            iface.pop_back();
        }

        if (iface == "lo") continue;

        uint64_t rxBytes = 0, txBytes = 0, dummy = 0;
        ss >> rxBytes;
        for (int k = 0; k < 7; ++k) ss >> dummy;
        ss >> txBytes;

        metrics.BytesReceived += rxBytes;
        metrics.BytesSent += txBytes;
    }
    return metrics;
}

std::vector<DiskMetrics> LinuxSystemInfoProvider::GetDiskMetrics() const {
    std::vector<DiskMetrics> disks;
#if !defined(_WIN32)
    struct statvfs stat {};
    if (statvfs("/", &stat) == 0) {
        DiskMetrics disk{};
        disk.DriveModel = "Root Partition";
        disk.DriveLetter = "/";
        disk.DriveType = "Fixed";

        disk.TotalBytes = static_cast<uint64_t>(stat.f_blocks) * stat.f_frsize;
        disk.FreeBytes = static_cast<uint64_t>(stat.f_bavail) * stat.f_frsize;

        if (disk.TotalBytes > 0) {
            uint64_t usedBytes = disk.TotalBytes - disk.FreeBytes;
            disk.PercentOfUsage = (static_cast<double>(usedBytes) / disk.TotalBytes) * 100.0;
        }

        disks.push_back(disk);
    }
#endif
    return disks;
}

CPUModel LinuxSystemInfoProvider::GetCPUModel() const {
    CPUModel model{};
    model.Model = "Generic Linux CPU";
#if !defined(_WIN32)
    long cores = sysconf(_SC_NPROCESSORS_ONLN);
    model.CoreAmount = (cores > 0) ? static_cast<int>(cores) : 4;
#else
    model.CoreAmount = 4;
#endif
    return model;
}

uint64_t LinuxSystemInfoProvider::GetTickTime() const {
    std::string output;
    std::ifstream file("/proc/uptime");
    if (file.is_open()) {
        std::string line;
        std::getline(file, line);
        output = line;
    }

    if (!output.empty()) {
        std::istringstream ss(output);
        double uptimeSeconds = 0.0;
        if (ss >> uptimeSeconds) {
            return static_cast<uint64_t>(uptimeSeconds);
        }
    }
    return 0;
}