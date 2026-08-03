#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#include "WinSystemInfoProvider.h"
#include "../WindowsUtils.h" // Путь к WindowsUtils.h в родительской директории

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winternl.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <winioctl.h>

#include <iostream>
#include <chrono>
#include <thread>
#include <stdexcept>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")

static std::string GetDiskModelName(const wchar_t* driveLetter) {
    wchar_t devicePath[10] = L"\\\\.\\";
    devicePath[4] = driveLetter[0];
    devicePath[5] = L':';
    devicePath[6] = L'\0';

    HANDLE hDevice = CreateFileW(devicePath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (hDevice == INVALID_HANDLE_VALUE) return "Unknown Model";

    STORAGE_PROPERTY_QUERY query{};
    query.PropertyId = StorageDeviceProperty;
    query.QueryType = PropertyStandardQuery;

    BYTE buffer[1024] = { 0 };
    DWORD bytesReturned = 0;

    BOOL result = DeviceIoControl(hDevice, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query), buffer, sizeof(buffer), &bytesReturned, NULL);
    CloseHandle(hDevice);

    if (!result) return "Unknown Model";

    STORAGE_DEVICE_DESCRIPTOR* desc = reinterpret_cast<STORAGE_DEVICE_DESCRIPTOR*>(buffer);
    if (desc->ProductIdOffset != 0 && desc->ProductIdOffset < bytesReturned) {
        const char* model = reinterpret_cast<const char*>(buffer + desc->ProductIdOffset);
        std::string modelStr(model);
        size_t first = modelStr.find_first_not_of(" \t\n\r");
        size_t last = modelStr.find_last_not_of(" \t\n\r");
        if (first != std::string::npos && last != std::string::npos) {
            return modelStr.substr(first, (last - first + 1));
        }
        return modelStr;
    }

    return "Generic Drive";
}

static std::string GetCPUName() {
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        char buffer[256];
        DWORD bufferSize = sizeof(buffer);
        if (RegQueryValueExA(hKey, "ProcessorNameString", NULL, NULL, (LPBYTE)buffer, &bufferSize) == ERROR_SUCCESS) {
            RegCloseKey(hKey);
            return std::string(buffer);
        }
        RegCloseKey(hKey);
    }
    return "Unknown CPU";
}

MemoryMetrics WinSystemInfoProvider::GetMemoryMetrics() const {
    MEMORYSTATUSEX Status;
    Status.dwLength = sizeof(Status);
    if (!GlobalMemoryStatusEx(&Status)) {
        throw std::runtime_error("Error while getting memory metrics");
    }
    MemoryMetrics res;
    res.PercentOfUsage = static_cast<int>(Status.dwMemoryLoad);
    res.MemoryAmount = static_cast<uint64_t>(Status.ullTotalPhys);
    res.MemoryFree = static_cast<uint64_t>(Status.ullAvailPhys);
    return res;
}

double WinSystemInfoProvider::GetCPUMetrics() const {
    FILETIME idle1, kernel1, user1;
    if (!GetSystemTimes(&idle1, &kernel1, &user1)) {
        throw std::runtime_error("Error getting CPU times");
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    FILETIME idle2, kernel2, user2;
    if (!GetSystemTimes(&idle2, &kernel2, &user2)) {
        throw std::runtime_error("Error getting CPU times");
    }

    uint64_t idleDelta = FileTimeToUint64(idle2) - FileTimeToUint64(idle1);
    uint64_t kernelDelta = FileTimeToUint64(kernel2) - FileTimeToUint64(kernel1);
    uint64_t userDelta = FileTimeToUint64(user2) - FileTimeToUint64(user1);

    uint64_t totalDelta = kernelDelta + userDelta;
    if (totalDelta == 0) return 0.0;

    double idleFraction = static_cast<double>(idleDelta) / totalDelta;
    return (1.0 - idleFraction) * 100.0;
}

NetworkMetrics WinSystemInfoProvider::GetNetworkMetrics() const {
    NetworkMetrics metrics;
    PMIB_IF_TABLE2 ifTable = NULL;

    if (GetIfTable2(&ifTable) == NO_ERROR) {
        for (ULONG i = 0; i < ifTable->NumEntries; ++i) {
            const MIB_IF_ROW2& row = ifTable->Table[i];
            if (row.Type == IF_TYPE_SOFTWARE_LOOPBACK || row.OperStatus != IfOperStatusUp) {
                continue;
            }
            metrics.BytesReceived += row.InOctets;
            metrics.BytesSent += row.OutOctets;
        }
        FreeMibTable(ifTable);
    }

    return metrics;
}

std::vector<DiskMetrics> WinSystemInfoProvider::GetDiskMetrics() const {
    std::vector<DiskMetrics> disks;
    wchar_t buffer[256];
    DWORD size = GetLogicalDriveStringsW(256, buffer);
    if (size == 0) return disks;

    wchar_t* drive = buffer;
    while (*drive) {
        DiskMetrics dm;
        std::wstring wDrive(drive);
        dm.DriveLetter = std::string(wDrive.begin(), wDrive.end());

        UINT type = GetDriveTypeW(drive);
        if (type == DRIVE_FIXED) dm.DriveType = "Fixed";
        else if (type == DRIVE_REMOVABLE) dm.DriveType = "USB/Removable";
        else dm.DriveType = "Other";

        ULARGE_INTEGER freeBytesAvailable, totalNumberOfBytes, totalNumberOfFreeBytes;
        if (GetDiskFreeSpaceExW(drive, &freeBytesAvailable, &totalNumberOfBytes, &totalNumberOfFreeBytes)) {
            dm.TotalBytes = totalNumberOfBytes.QuadPart;
            dm.FreeBytes = totalNumberOfFreeBytes.QuadPart;

            if (dm.TotalBytes > 0) {
                uint64_t usedBytes = dm.TotalBytes - dm.FreeBytes;
                dm.PercentOfUsage = (static_cast<double>(usedBytes) / dm.TotalBytes) * 100.0;
            }
        }
        dm.DriveModel = GetDiskModelName(drive);
        disks.push_back(dm);

        drive += wcslen(drive) + 1;
    }

    return disks;
}

typedef LONG(NTAPI* RtlGetVersionPtr)(PRTL_OSVERSIONINFOW);

OSMetrics WinSystemInfoProvider::GetOSMetrics() const {
    OSMetrics os;

    wchar_t compName[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD compSize = sizeof(compName) / sizeof(compName[0]);
    if (GetComputerNameW(compName, &compSize)) {
        char buffer[256];
        WideCharToMultiByte(CP_UTF8, 0, compName, -1, buffer, sizeof(buffer), NULL, NULL);
        os.ComputerName = buffer;
    }

    wchar_t userName[256];
    DWORD userSize = sizeof(userName) / sizeof(userName[0]);
    if (GetUserNameW(userName, &userSize)) {
        char buffer[256];
        WideCharToMultiByte(CP_UTF8, 0, userName, -1, buffer, sizeof(buffer), NULL, NULL);
        os.UserName = buffer;
    }

    SYSTEM_INFO sysInfo;
    GetNativeSystemInfo(&sysInfo);
    switch (sysInfo.wProcessorArchitecture) {
    case PROCESSOR_ARCHITECTURE_AMD64: os.Architecture = "x64"; break;
    case PROCESSOR_ARCHITECTURE_ARM64: os.Architecture = "ARM64"; break;
    case PROCESSOR_ARCHITECTURE_INTEL: os.Architecture = "x86"; break;
    default: os.Architecture = "Unknown"; break;
    }

    HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
    if (hNtdll) {
        auto pRtlGetVersion = (RtlGetVersionPtr)GetProcAddress(hNtdll, "RtlGetVersion");
        if (pRtlGetVersion) {
            RTL_OSVERSIONINFOW rovi = { 0 };
            rovi.dwOSVersionInfoSize = sizeof(rovi);
            if (pRtlGetVersion(&rovi) == 0) {
                os.BuildNumber = std::to_string(rovi.dwBuildNumber);

                if (rovi.dwMajorVersion == 10) {
                    os.OsName = (rovi.dwBuildNumber >= 22000) ? "Windows 11" : "Windows 10";
                }
                else if (rovi.dwMajorVersion == 6 && rovi.dwMinorVersion == 3) {
                    os.OsName = "Windows 8.1";
                }
                else if (rovi.dwMajorVersion == 6 && rovi.dwMinorVersion == 1) {
                    os.OsName = "Windows 7";
                }
                else {
                    os.OsName = "Windows";
                }
            }
        }
    }

    return os;
}

CPUModel WinSystemInfoProvider::GetCPUModel() const {
    CPUModel Model;
    Model.Model = GetCPUName();
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    Model.CoreAmount = sysInfo.dwNumberOfProcessors;
    return Model;
}

uint64_t WinSystemInfoProvider::GetTickTime() const {
    return (static_cast<uint64_t>(GetTickCount64()) / 1000);
}
