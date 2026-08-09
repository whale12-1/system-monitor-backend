#include "LinuxProcessManager.h"
#include <fstream>
#include <sstream>
#include <iostream>

#if !defined(_WIN32)
#include <sys/types.h>
#include <signal.h>
#include <unistd.h>
#include <sys/resource.h>
#include <sched.h>
#include <filesystem>
namespace fs = std::filesystem;
#endif

std::vector<ProcessMetrics> LinuxProcessManager::GetProcesses() const {
#if !defined(_WIN32)
    return GetProcessesFromProcFs();
#else
    std::vector<ProcessMetrics> list;
    ProcessMetrics dummy{};
    dummy.Pid = 1000;
    dummy.Name = "systemd";
    dummy.ExecutablePath = "/usr/lib/systemd/systemd";
    dummy.MemoryUsage = 1024 * 1024 * 150;
    dummy.CpuUsage = 2.5;
    list.push_back(dummy);
    return list;
#endif
}

std::vector<ProcessMetrics> LinuxProcessManager::GetProcessesFromProcFs() const {
    std::vector<ProcessMetrics> processes;

#if !defined(_WIN32)
    if (!fs::exists("/proc")) return processes;

    for (const auto& entry : fs::directory_iterator("/proc")) {
        if (!entry.is_directory()) continue;

        std::string dirName = entry.path().filename().string();
        if (dirName.find_first_not_of("0123456789") != std::string::npos) continue;

        unsigned long pid = std::stoul(dirName);
        ProcessMetrics pm{};
        pm.Pid = pid;

        std::ifstream commFile(entry.path().string() + "/comm");
        if (commFile.is_open()) {
            std::getline(commFile, pm.Name);
        }

        std::ifstream cmdFile(entry.path().string() + "/cmdline");
        if (cmdFile.is_open()) {
            std::getline(cmdFile, pm.ExecutablePath, '\0');
        }
        if (pm.ExecutablePath.empty()) {
            pm.ExecutablePath = pm.Name;
        }

        std::ifstream statusFile(entry.path().string() + "/status");
        if (statusFile.is_open()) {
            std::string line;
            while (std::getline(statusFile, line)) {
                if (line.rfind("VmRSS:", 0) == 0) {
                    std::istringstream ss(line);
                    std::string key;
                    uint64_t rssKb = 0;
                    ss >> key >> rssKb;
                    pm.MemoryUsage = rssKb * 1024;
                    break;
                }
            }
        }

        pm.CpuUsage = 0.0;
        processes.push_back(pm);
    }
#endif

    return processes;
}

bool LinuxProcessManager::KillProcess(unsigned long pid) const {
#if !defined(_WIN32)
    return ::kill(static_cast<pid_t>(pid), SIGKILL) == 0;
#else
    return false;
#endif
}

bool LinuxProcessManager::OpenFileLocation(unsigned long pid) const {
    return false;
}

bool LinuxProcessManager::CreateNewProcess(const std::string& executablePath,
    const std::string& arguments,
    bool asAdmin) const {
#if !defined(_WIN32)
    pid_t pid = fork();
    if (pid == 0) {
        execl(executablePath.c_str(), executablePath.c_str(), arguments.c_str(), NULL);
        _exit(127);
    }
    return pid > 0;
#else
    return false;
#endif
}

ProcessDetails LinuxProcessManager::GetProcessDetails(unsigned long pid) const {
    ProcessDetails details{};
    details.Pid = pid;
    details.Success = false;

#if !defined(_WIN32)
    errno = 0;
    int priority = getpriority(PRIO_PROCESS, static_cast<id_t>(pid));
    if (errno == 0) {
        details.Success = true;
        if (priority < -10) details.Priority = ProcessPriorityLevel::Realtime;
        else if (priority < -5) details.Priority = ProcessPriorityLevel::High;
        else if (priority < 0) details.Priority = ProcessPriorityLevel::AboveNormal;
        else if (priority == 0) details.Priority = ProcessPriorityLevel::Normal;
        else if (priority < 10) details.Priority = ProcessPriorityLevel::BelowNormal;
        else details.Priority = ProcessPriorityLevel::Idle;
    }

    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    if (sched_getaffinity(static_cast<pid_t>(pid), sizeof(cpu_set_t), &cpuset) == 0) {
        uint64_t mask = 0;
        for (int i = 0; i < 64; ++i) {
            if (CPU_ISSET(i, &cpuset)) {
                mask |= (1ULL << i);
            }
        }
        details.AffinityMask = mask;
    }
#endif

    return details;
}

bool LinuxProcessManager::SetProcessPriority(unsigned long pid, ProcessPriorityLevel priority) const {
#if !defined(_WIN32)
    int niceVal = 0;
    switch (priority) {
    case ProcessPriorityLevel::Realtime:    niceVal = -19; break;
    case ProcessPriorityLevel::High:        niceVal = -10; break;
    case ProcessPriorityLevel::AboveNormal:  niceVal = -5;  break;
    case ProcessPriorityLevel::Normal:       niceVal = 0;   break;
    case ProcessPriorityLevel::BelowNormal: niceVal = 5;   break;
    case ProcessPriorityLevel::Idle:        niceVal = 19;  break;
    }
    return setpriority(PRIO_PROCESS, static_cast<id_t>(pid), niceVal) == 0;
#else
    return false;
#endif
}

bool LinuxProcessManager::SetProcessAffinity(unsigned long pid, uint64_t affinityMask) const {
#if !defined(_WIN32)
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    for (int i = 0; i < 64; ++i) {
        if (affinityMask & (1ULL << i)) {
            CPU_SET(i, &cpuset);
        }
    }
    return sched_setaffinity(static_cast<pid_t>(pid), sizeof(cpu_set_t), &cpuset) == 0;
#else
    return false;
#endif
}

bool LinuxProcessManager::SetProcessEcoMode(unsigned long pid, bool enableEcoMode) const {
    return SetProcessPriority(pid, enableEcoMode ? ProcessPriorityLevel::Idle : ProcessPriorityLevel::Normal);
}