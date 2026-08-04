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

#include <vector>
#include <cstdint>

// Явно объявляем структуру EX2, если в текущей версии WinSDK её нет в заголовочных файлах
typedef struct _PROCESS_MEMORY_COUNTERS_EX2_CUSTOM {
    DWORD cb;
    DWORD PageFaultCount;
    SIZE_T PeakWorkingSetSize;
    SIZE_T WorkingSetSize;
    SIZE_T QuotaPeakPagedPoolUsage;
    SIZE_T QuotaPagedPoolUsage;
    SIZE_T QuotaPeakNonPagedPoolUsage;
    SIZE_T QuotaNonPagedPoolUsage;
    SIZE_T PagefileUsage;
    SIZE_T PeakPagefileUsage;
    SIZE_T PrivateUsage;
    SIZE_T PrivateWorkingSetSize; // <--- Точное значение из Диспетчера задач
    SIZE_T SharedWorkingSetSize;
} PROCESS_MEMORY_COUNTERS_EX2_CUSTOM;

static uint64_t GetExactPrivateWorkingSet(HANDLE hProcess) {
    PROCESS_MEMORY_COUNTERS_EX2_CUSTOM pmcEx2{};
    pmcEx2.cb = sizeof(PROCESS_MEMORY_COUNTERS_EX2_CUSTOM);

    // Запрашиваем метрики через расширенный буфер EX2
    if (GetProcessMemoryInfo(hProcess, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmcEx2), sizeof(pmcEx2))) {
        // Если ОС заполнила PrivateWorkingSetSize — возвращаем его
        if (pmcEx2.PrivateWorkingSetSize > 0) {
            return static_cast<uint64_t>(pmcEx2.PrivateWorkingSetSize);
        }
    }

    // Резервный вариант, если процесс находится в глубоком с Animate/Suspended состоянии
    PROCESS_MEMORY_COUNTERS pmc{};
    if (GetProcessMemoryInfo(hProcess, &pmc, sizeof(pmc))) {
        return static_cast<uint64_t>(pmc.WorkingSetSize);
    }

    return 0;
}

#include <sysinfoapi.h>

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

    if (!Process32FirstW(CurrentS.get(), &Process)) {
        return ProcessVector;
    }

    do {
        ProcessMetrics info;
        info.Pid = Process.th32ProcessID;

        std::wstring wName(Process.szExeFile);
        info.Name = std::string(wName.begin(), wName.end());

        // Игнорируем System Idle Process (0) и System (4)
        if (info.Pid != 0 && info.Pid != 4) {
            // Флаг PROCESS_QUERY_LIMITED_INFORMATION более безопасен для системных процессов
            UniqueHandle hProcess(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, info.Pid));

            if (hProcess.get() != NULL) {
                // Точный приватный рабочий набор
                info.MemoryUsage = GetExactPrivateWorkingSet(hProcess.get());

                // 2. Получение полного пути к исполняемому файлу
                wchar_t pathBuffer[MAX_PATH];
                DWORD size = MAX_PATH;
                if (QueryFullProcessImageNameW(hProcess.get(), 0, pathBuffer, &size)) {
                    std::wstring wPath(pathBuffer);
                    info.ExecutablePath = std::string(wPath.begin(), wPath.end());
                }
                else {
                    info.ExecutablePath = "N/A";
                }

                // 3. Корректный расчет CPU (без умножения на numCores!)
                FILETIME ftCreation, ftExit, ftKernel, ftUser;
                if (GetProcessTimes(hProcess.get(), &ftCreation, &ftExit, &ftKernel, &ftUser)) {
                    uint64_t currentProcTime = FileTimeToUint64(ftKernel) + FileTimeToUint64(ftUser);
                    nextProcessTimes[info.Pid] = currentProcTime;

                    if (prevProcessTimes.count(info.Pid) > 0 && systemTimeDelta > 0) {
                        uint64_t procTimeDelta = currentProcTime - prevProcessTimes[info.Pid];

                        // systemTimeDelta — это суммарное время всех ядер.
                        // Деление procTimeDelta / systemTimeDelta уже дает корректную долю от 100% всей системы.
                        info.CpuUsage = (static_cast<double>(procTimeDelta) / static_cast<double>(systemTimeDelta)) * 100.0;

                        // Ограничение диапазона от 0% до 100%
                        if (info.CpuUsage > 100.0) info.CpuUsage = 100.0;
                        if (info.CpuUsage < 0.0) info.CpuUsage = 0.0;
                    }
                    else {
                        info.CpuUsage = 0.0;
                    }
                }
            }
        }

        ProcessVector.push_back(info);

    } while (Process32NextW(CurrentS.get(), &Process));

    // Обновляем статические данные для следующего тика
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

bool WinProcessManager::CreateNewProcess(const std::string& executablePath,
    const std::string& arguments,
    bool asAdmin) const
{
    if (executablePath.empty()) {
        return false;
    }

    // Преобразуем std::string в std::wstring для корректной работы с Unicode
    std::wstring wPath(executablePath.begin(), executablePath.end());
    std::wstring wArgs(arguments.begin(), arguments.end());

    if (asAdmin) {
        // Запуск от имени администратора через UAC
        SHELLEXECUTEINFOW sei = { sizeof(sei) };
        sei.lpVerb = L"runas";
        sei.lpFile = wPath.c_str();
        sei.lpParameters = wArgs.empty() ? nullptr : wArgs.c_str();
        sei.nShow = SW_SHOWNORMAL;
        sei.fMask = SEE_MASK_NOCLOSEPROCESS;

        if (ShellExecuteExW(&sei)) {
            if (sei.hProcess) {
                CloseHandle(sei.hProcess);
            }
            return true;
        }
        return false;
    }
    else {
        // Обычный запуск через CreateProcessW
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};

        // Для CreateProcessW аргументы должны передаваться единой командной строкой
        std::wstring commandLine = L"\"" + wPath + L"\"";
        if (!wArgs.empty()) {
            commandLine += L" " + wArgs;
        }

        BOOL success = CreateProcessW(
            nullptr,
            &commandLine[0], // Буфер должен быть изменяемым
            nullptr,
            nullptr,
            FALSE,
            0,
            nullptr,
            nullptr,
            &si,
            &pi
        );

        if (success) {
            // Закрываем дескрипторы, так как управление процессом нам дальше не требуется
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            return true;
        }
        return false;
    }
}