#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#include "WinProcessManager.h"
#include "../WindowsUtils.h" // Относительный путь к WindowsUtils.h в родительской папке

#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <shellapi.h>
#include <unordered_map>
#include <stdexcept>

static uint64_t GetTotalSystemTime() {
    FILETIME idleTime, kernelTime, userTime;
    if (GetSystemTimes(&idleTime, &kernelTime, &userTime)) {
        return FileTimeToUint64(kernelTime) + FileTimeToUint64(userTime);
    }
    return 0;
}

std::vector<ProcessMetrics> WinProcessManager::GetProcesses() const {
    std::vector<ProcessMetrics> ProcessVector;

    static std::unordered_map<DWORD, uint64_t> prevProcessTimes;
    static uint64_t g_previousTotalSystemTime = 0;

    uint64_t currentSystemTime = GetTotalSystemTime();
    uint64_t systemTimeDelta = currentSystemTime - g_previousTotalSystemTime;

    std::unordered_map<DWORD, uint64_t> nextProcessTimes;

    UniqueHandle CurrentS(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (CurrentS.get() == INVALID_HANDLE_VALUE) {
        throw std::runtime_error("Error getting System processes snapshot");
    }

    PROCESSENTRY32W Process;
    Process.dwSize = sizeof(PROCESSENTRY32W);
    Process32FirstW(CurrentS.get(), &Process);

    do {
        ProcessMetrics info;
        info.Pid = Process.th32ProcessID;

        std::wstring wName(Process.szExeFile);
        info.Name = std::string(wName.begin(), wName.end());

        if (info.Pid != 0 && info.Pid != 4) {
            UniqueHandle hProcess(OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, info.Pid));
            if (hProcess.get() != NULL) {
                PROCESS_MEMORY_COUNTERS pmc;
                if (GetProcessMemoryInfo(hProcess.get(), &pmc, sizeof(pmc))) {
                    info.MemoryUsage = static_cast<uint64_t>(pmc.WorkingSetSize);
                }

                wchar_t pathBuffer[MAX_PATH];
                DWORD size = MAX_PATH;
                if (QueryFullProcessImageNameW(hProcess.get(), 0, pathBuffer, &size)) {
                    std::wstring wPath(pathBuffer);
                    info.ExecutablePath = std::string(wPath.begin(), wPath.end());
                }
                else {
                    info.ExecutablePath = "N/A";
                }

                FILETIME ftCreation, ftExit, ftKernel, ftUser;
                if (GetProcessTimes(hProcess.get(), &ftCreation, &ftExit, &ftKernel, &ftUser)) {
                    uint64_t currentProcTime = FileTimeToUint64(ftKernel) + FileTimeToUint64(ftUser);
                    nextProcessTimes[info.Pid] = currentProcTime;

                    if (prevProcessTimes.count(info.Pid) > 0 && systemTimeDelta > 0) {
                        uint64_t procTimeDelta = currentProcTime - prevProcessTimes[info.Pid];
                        info.CpuUsage = (static_cast<double>(procTimeDelta) / systemTimeDelta) * 100.0;
                    }
                    else {
                        info.CpuUsage = 0.0;
                    }
                }
            }
        }

        ProcessVector.push_back(info);

    } while (Process32NextW(CurrentS.get(), &Process));

    prevProcessTimes = std::move(nextProcessTimes);
    g_previousTotalSystemTime = currentSystemTime;

    return ProcessVector;
}

bool WinProcessManager::KillProcess(unsigned long pid) const {
    if (pid <= 4) return false;
    UniqueHandle hProcess(OpenProcess(PROCESS_TERMINATE, FALSE, static_cast<DWORD>(pid)));
    if (hProcess.get() == NULL) return false;

    return TerminateProcess(hProcess.get(), 1) != 0;
}

bool WinProcessManager::OpenFileLocation(unsigned long pid) const {
    if (pid <= 4) return false;

    UniqueHandle hProcess(OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid));
    if (!hProcess.get()) return false;

    wchar_t pathBuffer[MAX_PATH];
    DWORD size = MAX_PATH;
    if (QueryFullProcessImageNameW(hProcess.get(), 0, pathBuffer, &size)) {
        std::wstring param = L"/select,\"" + std::wstring(pathBuffer) + L"\"";

        SHELLEXECUTEINFOW sei = { sizeof(sei) };
        sei.lpVerb = L"open";
        sei.lpFile = L"explorer.exe";
        sei.lpParameters = param.c_str();
        sei.nShow = SW_SHOWNORMAL;

        return ShellExecuteExW(&sei);
    }

    return false;
}