//#define _CRT_SECURE_NO_WARNINGS
//#define WIN32_LEAN_AND_MEAN
//
//// 1. Сетевой стек Windows (WinSock2 строго до windows.h!)
//#include <winsock2.h>
//#include <ws2tcpip.h>
//#include <windows.h>
//
//// 2. Системные структуры и внутренности NT
//#include <winternl.h>
//#include <tlhelp32.h>
//#include <psapi.h>
//#include <iphlpapi.h>
//#include <netioapi.h>
//#include <winioctl.h>
//#include <ntddscsi.h>
//
//// 3. Стандартная библиотека C++
//#include <iostream>
//#include <chrono>
//#include <thread>
//#include <cstdint>
//#include <string>
//#include <vector>
//#include <memory>
//#include <unordered_map>
//
//// 4. Подключение вашего заголовочного файла
//#include "WindowsApiMetricsProvider.h"
//
//#pragma comment(lib, "iphlpapi.lib")
//#pragma comment(lib, "ws2_32.lib")
//
//// RAII обертка для безопасного закрытия HANDLE в WinAPI
//struct HandleDeleter {
//    void operator()(HANDLE h) const {
//        if (h && h != INVALID_HANDLE_VALUE) {
//            CloseHandle(h);
//        }
//    }
//};
//using UniqueHandle = std::unique_ptr<void, HandleDeleter>;
//
//// --- Дальше идут ваши Helper функции ---
//
//inline uint64_t FileTimeToUint64(const FILETIME& ft) {
//    return (static_cast<uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
//}
//
//uint64_t GetTotalSystemTime() {
//    FILETIME idleTime, kernelTime, userTime;
//    if (GetSystemTimes(&idleTime, &kernelTime, &userTime)) {
//        return FileTimeToUint64(kernelTime) + FileTimeToUint64(userTime);
//    }
//    return 0;
//}
//
//std::string GetDiskModelName(const wchar_t* driveLetter) {
//    wchar_t devicePath[10] = L"\\\\.\\";
//    devicePath[4] = driveLetter[0];
//    devicePath[5] = L':';
//    devicePath[6] = L'\0';
//
//    HANDLE hDevice = CreateFileW(
//        devicePath,
//        0,
//        FILE_SHARE_READ | FILE_SHARE_WRITE,
//        NULL,
//        OPEN_EXISTING,
//        0,
//        NULL
//    );
//
//    if (hDevice == INVALID_HANDLE_VALUE) {
//        return "Unknown Model";
//    }
//
//    STORAGE_PROPERTY_QUERY query{};
//    query.PropertyId = StorageDeviceProperty;
//    query.QueryType = PropertyStandardQuery;
//
//    BYTE buffer[1024] = { 0 };
//    DWORD bytesReturned = 0;
//
//    BOOL result = DeviceIoControl(
//        hDevice,
//        IOCTL_STORAGE_QUERY_PROPERTY,
//        &query,
//        sizeof(query),
//        buffer,
//        sizeof(buffer),
//        &bytesReturned,
//        NULL
//    );
//
//    CloseHandle(hDevice);
//
//    if (!result) {
//        return "Unknown Model";
//    }
//
//    STORAGE_DEVICE_DESCRIPTOR* desc = reinterpret_cast<STORAGE_DEVICE_DESCRIPTOR*>(buffer);
//
//    if (desc->ProductIdOffset != 0 && desc->ProductIdOffset < bytesReturned) {
//        const char* model = reinterpret_cast<const char*>(buffer + desc->ProductIdOffset);
//
//        std::string modelStr(model);
//        size_t first = modelStr.find_first_not_of(" \t\n\r");
//        size_t last = modelStr.find_last_not_of(" \t\n\r");
//        if (first != std::string::npos && last != std::string::npos) {
//            return modelStr.substr(first, (last - first + 1));
//        }
//        return modelStr;
//    }
//
//    return "Generic Drive";
//}
//
//std::string GetCPUName() {
//    HKEY hKey;
//    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
//        "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
//        0, KEY_READ, &hKey) == ERROR_SUCCESS) {
//        char buffer[256];
//        DWORD bufferSize = sizeof(buffer);
//        if (RegQueryValueExA(hKey, "ProcessorNameString", NULL, NULL, (LPBYTE)buffer, &bufferSize) == ERROR_SUCCESS) {
//            RegCloseKey(hKey);
//            return std::string(buffer);
//        }
//        RegCloseKey(hKey);
//    }
//    return "Unknown CPU";
//}
//
//// --- Реализация методов класса ---
//
//MemoryMetrics WindowsApiMetricsProvider::GetMemoryMetrics() const {
//    MEMORYSTATUSEX Status;
//    Status.dwLength = sizeof(Status);
//    if (!GlobalMemoryStatusEx(&Status)) {
//        std::cerr << "Error while getting memory metrics";
//        throw std::runtime_error("Error while getting memory metrics");
//    }
//    MemoryMetrics res;
//    res.PercentOfUsage = static_cast<int>(Status.dwMemoryLoad);
//    res.MemoryAmount = static_cast<uint64_t>(Status.ullTotalPhys);
//    res.MemoryFree = static_cast<uint64_t>(Status.ullAvailPhys);
//    return res;
//}
//
//double WindowsApiMetricsProvider::GetCPUMetrics() const {
//    FILETIME idle1, kernel1, user1;
//    if (!GetSystemTimes(&idle1, &kernel1, &user1)) {
//        throw std::runtime_error("Error getting CPU times");
//    }
//
//    std::this_thread::sleep_for(std::chrono::milliseconds(500));
//
//    FILETIME idle2, kernel2, user2;
//    if (!GetSystemTimes(&idle2, &kernel2, &user2)) {
//        throw std::runtime_error("Error getting CPU times");
//    }
//
//    uint64_t idleDelta = FileTimeToUint64(idle2) - FileTimeToUint64(idle1);
//    uint64_t kernelDelta = FileTimeToUint64(kernel2) - FileTimeToUint64(kernel1);
//    uint64_t userDelta = FileTimeToUint64(user2) - FileTimeToUint64(user1);
//
//    uint64_t totalDelta = kernelDelta + userDelta;
//
//    if (totalDelta == 0) return 0.0;
//
//    double idleFraction = static_cast<double>(idleDelta) / totalDelta;
//
//    return (1.0 - idleFraction) * 100.0;
//}
//
//std::vector<ProcessMetrics> WindowsApiMetricsProvider::GetProcesses() const {
//    std::vector<ProcessMetrics> ProcessVector;
//
//    static std::unordered_map<DWORD, uint64_t> prevProcessTimes;
//    static uint64_t g_previousTotalSystemTime = 0;
//
//    uint64_t currentSystemTime = GetTotalSystemTime();
//    uint64_t systemTimeDelta = currentSystemTime - g_previousTotalSystemTime;
//
//    std::unordered_map<DWORD, uint64_t> nextProcessTimes;
//
//    UniqueHandle CurrentS(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
//    if (CurrentS.get() == INVALID_HANDLE_VALUE) {
//        throw std::runtime_error("Error getting System processes snapshot");
//    }
//
//    PROCESSENTRY32W Process;
//    Process.dwSize = sizeof(PROCESSENTRY32W);
//    Process32FirstW(CurrentS.get(), &Process);
//
//    do {
//        ProcessMetrics info;
//        info.Pid = Process.th32ProcessID;
//
//        std::wstring wName(Process.szExeFile);
//        info.Name = std::string(wName.begin(), wName.end());
//
//        if (info.Pid != 0 && info.Pid != 4) {
//            UniqueHandle hProcess(OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, info.Pid));
//            if (hProcess.get() != NULL) {
//                PROCESS_MEMORY_COUNTERS pmc;
//                if (GetProcessMemoryInfo(hProcess.get(), &pmc, sizeof(pmc))) {
//                    info.MemoryUsage = static_cast<uint64_t>(pmc.WorkingSetSize);
//                }
//
//                wchar_t pathBuffer[MAX_PATH];
//                DWORD size = MAX_PATH;
//                if (QueryFullProcessImageNameW(hProcess.get(), 0, pathBuffer, &size)) {
//                    std::wstring wPath(pathBuffer);
//                    info.ExecutablePath = std::string(wPath.begin(), wPath.end());
//                }
//                else {
//                    info.ExecutablePath = "N/A";
//                }
//
//                FILETIME ftCreation, ftExit, ftKernel, ftUser;
//                if (GetProcessTimes(hProcess.get(), &ftCreation, &ftExit, &ftKernel, &ftUser)) {
//                    uint64_t currentProcTime = FileTimeToUint64(ftKernel) + FileTimeToUint64(ftUser);
//                    nextProcessTimes[info.Pid] = currentProcTime;
//
//                    if (prevProcessTimes.count(info.Pid) > 0 && systemTimeDelta > 0) {
//                        uint64_t procTimeDelta = currentProcTime - prevProcessTimes[info.Pid];
//                        info.CpuUsage = (static_cast<double>(procTimeDelta) / systemTimeDelta) * 100.0;
//                    }
//                    else {
//                        info.CpuUsage = 0.0;
//                    }
//                }
//            }
//        }
//
//        ProcessVector.push_back(info);
//
//    } while (Process32NextW(CurrentS.get(), &Process));
//
//    prevProcessTimes = std::move(nextProcessTimes);
//    g_previousTotalSystemTime = currentSystemTime;
//
//    return ProcessVector;
//}
//
//bool WindowsApiMetricsProvider::KillProcess(unsigned long pid) const {
//    if (pid <= 4) {
//        return false;
//    }
//    UniqueHandle hProcess(OpenProcess(PROCESS_TERMINATE, FALSE, static_cast<DWORD>(pid)));
//    if (hProcess.get() == NULL) {
//        return false;
//    }
//
//    return TerminateProcess(hProcess.get(), 1) != 0;
//}
//
//uint64_t WindowsApiMetricsProvider::GetTickTime() const {
//    return (static_cast<uint64_t>(GetTickCount64()) / 1000);
//}
//
//CPUModel WindowsApiMetricsProvider::GetCPUModel() const {
//    CPUModel Model;
//    Model.Model = GetCPUName();
//    SYSTEM_INFO sysInfo;
//    GetSystemInfo(&sysInfo);
//
//    DWORD numCores = sysInfo.dwNumberOfProcessors;
//    Model.CoreAmount = numCores;
//    return Model;
//}
//
//std::vector<DiskMetrics> WindowsApiMetricsProvider::GetDiskMetrics() const {
//    std::vector<DiskMetrics> disks;
//
//    wchar_t buffer[256];
//    DWORD size = GetLogicalDriveStringsW(256, buffer);
//    if (size == 0) return disks;
//
//    wchar_t* drive = buffer;
//    while (*drive) {
//        DiskMetrics dm;
//
//        std::wstring wDrive(drive);
//        dm.DriveLetter = std::string(wDrive.begin(), wDrive.end());
//
//        UINT type = GetDriveTypeW(drive);
//        if (type == DRIVE_FIXED) dm.DriveType = "Fixed";
//        else if (type == DRIVE_REMOVABLE) dm.DriveType = "USB/Removable";
//        else dm.DriveType = "Other";
//
//        ULARGE_INTEGER freeBytesAvailable, totalNumberOfBytes, totalNumberOfFreeBytes;
//        if (GetDiskFreeSpaceExW(drive, &freeBytesAvailable, &totalNumberOfBytes, &totalNumberOfFreeBytes)) {
//            dm.TotalBytes = totalNumberOfBytes.QuadPart;
//            dm.FreeBytes = totalNumberOfFreeBytes.QuadPart;
//
//            if (dm.TotalBytes > 0) {
//                uint64_t usedBytes = dm.TotalBytes - dm.FreeBytes;
//                dm.PercentOfUsage = (static_cast<double>(usedBytes) / dm.TotalBytes) * 100.0;
//            }
//        }
//        dm.DriveModel = GetDiskModelName(drive);
//        disks.push_back(dm);
//
//        drive += wcslen(drive) + 1;
//    }
//
//    return disks;
//}
//
//NetworkMetrics WindowsApiMetricsProvider::GetNetworkMetrics() const {
//    NetworkMetrics metrics;
//
//    PMIB_IF_TABLE2 ifTable = NULL;
//    if (GetIfTable2(&ifTable) == NO_ERROR) {
//        for (ULONG i = 0; i < ifTable->NumEntries; ++i) {
//            const MIB_IF_ROW2& row = ifTable->Table[i];
//
//            if (row.Type == IF_TYPE_SOFTWARE_LOOPBACK || row.OperStatus != IfOperStatusUp) {
//                continue;
//            }
//
//            metrics.BytesReceived += row.InOctets;
//            metrics.BytesSent += row.OutOctets;
//        }
//
//        FreeMibTable(ifTable);
//    }
//
//    return metrics;
//}
//
//typedef LONG(NTAPI* RtlGetVersionPtr)(PRTL_OSVERSIONINFOW);
//
//OSMetrics WindowsApiMetricsProvider::GetOSMetrics() const {
//    OSMetrics os;
//
//    wchar_t compName[MAX_COMPUTERNAME_LENGTH + 1];
//    DWORD compSize = sizeof(compName) / sizeof(compName[0]);
//    if (GetComputerNameW(compName, &compSize)) {
//        char buffer[256];
//        WideCharToMultiByte(CP_UTF8, 0, compName, -1, buffer, sizeof(buffer), NULL, NULL);
//        os.ComputerName = buffer;
//    }
//
//    wchar_t userName[256];
//    DWORD userSize = sizeof(userName) / sizeof(userName[0]);
//    if (GetUserNameW(userName, &userSize)) {
//        char buffer[256];
//        WideCharToMultiByte(CP_UTF8, 0, userName, -1, buffer, sizeof(buffer), NULL, NULL);
//        os.UserName = buffer;
//    }
//
//    SYSTEM_INFO sysInfo;
//    GetNativeSystemInfo(&sysInfo);
//    switch (sysInfo.wProcessorArchitecture) {
//    case PROCESSOR_ARCHITECTURE_AMD64: os.Architecture = "x64"; break;
//    case PROCESSOR_ARCHITECTURE_ARM64: os.Architecture = "ARM64"; break;
//    case PROCESSOR_ARCHITECTURE_INTEL: os.Architecture = "x86"; break;
//    default: os.Architecture = "Unknown"; break;
//    }
//
//    HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
//    if (hNtdll) {
//        auto pRtlGetVersion = (RtlGetVersionPtr)GetProcAddress(hNtdll, "RtlGetVersion");
//        if (pRtlGetVersion) {
//            RTL_OSVERSIONINFOW rovi = { 0 };
//            rovi.dwOSVersionInfoSize = sizeof(rovi);
//            if (pRtlGetVersion(&rovi) == 0) {
//                os.BuildNumber = std::to_string(rovi.dwBuildNumber);
//
//                if (rovi.dwMajorVersion == 10) {
//                    if (rovi.dwBuildNumber >= 22000) {
//                        os.OsName = "Windows 11";
//                    }
//                    else {
//                        os.OsName = "Windows 10";
//                    }
//                }
//                else if (rovi.dwMajorVersion == 6 && rovi.dwMinorVersion == 3) {
//                    os.OsName = "Windows 8.1";
//                }
//                else if (rovi.dwMajorVersion == 6 && rovi.dwMinorVersion == 1) {
//                    os.OsName = "Windows 7";
//                }
//                else {
//                    os.OsName = "Windows";
//                }
//            }
//        }
//    }
//
//    return os;
//}
//
//// Вспомогательная функция для чтения веток реестра
//void ReadRegistryStartup(HKEY hKeyRoot, const char* subKey, const std::string& locationName, std::vector<StartupItem>& items) {
//    HKEY hKey;
//    if (RegOpenKeyExA(hKeyRoot, subKey, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
//        DWORD index = 0;
//        char valueName[256];
//        DWORD valueNameSize = sizeof(valueName);
//        BYTE valueData[1024];
//        DWORD valueDataSize = sizeof(valueData);
//        DWORD type = 0;
//
//        while (RegEnumValueA(hKey, index, valueName, &valueNameSize, NULL, &type, valueData, &valueDataSize) == ERROR_SUCCESS) {
//            if (type == REG_SZ || type == REG_EXPAND_SZ) {
//                StartupItem item;
//                item.Name = valueName;
//                item.Command = reinterpret_cast<char*>(valueData);
//                item.Location = locationName;
//                items.push_back(item);
//            }
//            index++;
//            valueNameSize = sizeof(valueName);
//            valueDataSize = sizeof(valueData);
//        }
//        RegCloseKey(hKey);
//    }
//}
//
//std::vector<StartupItem> WindowsApiMetricsProvider::GetStartupItems() const {
//    std::vector<StartupItem> items;
//
//    // 1. Автозагрузка текущего пользователя (Registry HKCU)
//    ReadRegistryStartup(
//        HKEY_CURRENT_USER,
//        "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run",
//        "HKCU Registry",
//        items
//    );
//
//    // 2. Автозагрузка системы для всех пользователей (Registry HKLM)
//    ReadRegistryStartup(
//        HKEY_LOCAL_MACHINE,
//        "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run",
//        "HKLM Registry",
//        items
//    );
//
//    // 3. (Опционально) WOW64 ветка для 32-битных приложений на x64 ОС
//    ReadRegistryStartup(
//        HKEY_LOCAL_MACHINE,
//        "SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Run",
//        "HKLM (x86) Registry",
//        items
//    );
//    return items;
//}
//
//
//
/////Получение температуры ПК
//#include <comdef.h>
//#include <Wbemidl.h>
//
//#pragma comment(lib, "wbemuuid.lib")
//
//std::vector<TemperatureMetrics> WindowsApiMetricsProvider::GetTemperaturesUsingWinApi() const {
//    std::vector<TemperatureMetrics> temps;
//
//    // 1. Инициализация COM-библиотеки для текущего потока
//    HRESULT hr = CoInitializeEx(0, COINIT_MULTITHREADED);
//    bool coInitialized = SUCCEEDED(hr);
//
//    // 2. Инициализация безопасности COM
//    hr = CoInitializeSecurity(
//        NULL, -1, NULL, NULL,
//        RPC_C_AUTHN_LEVEL_DEFAULT,
//        RPC_C_IMP_LEVEL_IMPERSONATE,
//        NULL, EOAC_NONE, NULL
//    );
//
//    // 3. Создаем WMI локатор
//    IWbemLocator* pLoc = NULL;
//    hr = CoCreateInstance(
//        CLSID_WbemLocator, 0,
//        CLSCTX_INPROC_SERVER,
//        IID_IWbemLocator, (LPVOID*)&pLoc
//    );
//
//    if (SUCCEEDED(hr) && pLoc) {
//        IWbemServices* pSvc = NULL;
//
//        // Подключаемся к пространству имён root\WMI
//        hr = pLoc->ConnectServer(
//            _bstr_t(L"ROOT\\WMI"),
//            NULL, NULL, 0, NULL, 0, 0, &pSvc
//        );
//
//        if (SUCCEEDED(hr) && pSvc) {
//            // Настраиваем уровень безопасности для обращения к WMI
//            CoSetProxyBlanket(
//                pSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, NULL,
//                RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE,
//                NULL, EOAC_NONE
//            );
//
//            // Делаем WQL-запрос температурных зон
//            IEnumWbemClassObject* pEnumerator = NULL;
//            hr = pSvc->ExecQuery(
//                bstr_t("WQL"),
//                bstr_t("SELECT * FROM Win32_PerfFormattedData_Counters_ThermalZoneInformation"),
//                WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
//                NULL, &pEnumerator
//            );
//
//            if (pEnumerator) {
//                IWbemClassObject* pclsObj = NULL;
//                ULONG uReturn = 0;
//
//                while (pEnumerator) {
//                    hr = pEnumerator->Next(WBEM_INFINITE, 1, &pclsObj, &uReturn);
//                    if (0 == uReturn) break;
//
//                    VARIANT vtProp;
//                    VariantInit(&vtProp);
//                    // Получаем температуру (она хранится в Десятых долях Кельвина!)
//                    hr = pclsObj->Get(L"Temperature", 0, &vtProp, 0, 0);
//                    if (SUCCEEDED(hr) && vtProp.vt == VT_I4) {
//                        double kelvinTenths = static_cast<double>(vtProp.lVal);
//                        double rawCelsius = (kelvinTenths / 10.0) - 273.15;
//
//                        // Формула перевода: (T_десятые_кельвина / 10.0) - 273.15
//                        double celsius = std::round(rawCelsius * 10.0) / 10.0;
//
//                        // Добавляем значение, если оно адекватное
//                        if (celsius > -50.0 && celsius < 150.0) {
//                            TemperatureMetrics tm;
//                            tm.SensorName = "ACPI CPU Thermal Zone";
//                            tm.Celsius = celsius;
//                            temps.push_back(tm);
//                        }
//                    }
//                    VariantClear(&vtProp);
//                    pclsObj->Release();
//                }
//                pEnumerator->Release();
//            }
//            pSvc->Release();
//        }
//        pLoc->Release();
//    }
//
//    if (coInitialized) {
//        CoUninitialize();
//    }
//
//    return temps;
//}
//
//
//
//#include <wininet.h>
//#include <crow/json.h>
//#include <algorithm>
//#include <cmath>
//
//#pragma comment(lib, "wininet.lib")
//
//std::string FetchLhmJson() {
//    HINTERNET hInternet = InternetOpenA("SystemMonitor", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
//    if (!hInternet) return "";
//
//    DWORD timeout = 500; // Увеличим до 500мс для первого ответа
//    InternetSetOptionA(hInternet, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
//    InternetSetOptionA(hInternet, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
//
//    HINTERNET hConnect = InternetOpenUrlA(hInternet, "http://localhost:8085/data.json", NULL, 0, INTERNET_FLAG_RELOAD, 0);
//    if (!hConnect) {
//        InternetCloseHandle(hInternet);
//        return "";
//    }
//
//    std::string response;
//    char buffer[8192]; // Увеличим буфер для быстрого чтения
//    DWORD bytesRead = 0;
//
//    while (InternetReadFile(hConnect, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
//        response.append(buffer, bytesRead);
//    }
//
//    InternetCloseHandle(hConnect);
//    InternetCloseHandle(hInternet);
//    return response;
//}
//
//// Рекурсивный поиск датчиков температуры
//void ParseLhmTemperatures(const crow::json::rvalue& node, std::vector<TemperatureMetrics>& temps) {
//    if (node.t() != crow::json::type::Object) return;
//
//    // Проверяем: если у узла есть "Value" со значком "°C"
//    if (node.has("Text") && node.has("Value")) {
//        std::string valStr = node["Value"].s();
//
//        // Поиск по наличию градуса Цельсия в значении или иконки температуры
//        if (valStr.find("°C") != std::string::npos) {
//            std::string sensorName = node["Text"].s();
//
//            // Очищаем строку значения от "°C" и пробелов
//            std::string numStr = valStr;
//            size_t degPos = numStr.find("°C");
//            if (degPos != std::string::npos) {
//                numStr = numStr.substr(0, degPos);
//            }
//            std::replace(numStr.begin(), numStr.end(), ',', '.');
//
//            try {
//                double celsius = std::stod(numStr);
//                celsius = std::trunc(celsius * 10.0) / 10.0;
//                if (celsius > 0.0 && celsius < 120.0) {
//                    TemperatureMetrics tm;
//                    tm.SensorName = sensorName;
//                    tm.Celsius = std::round(celsius * 10.0) / 10.0;
//                    temps.push_back(tm);
//                }
//            }
//            catch (...) {}
//        }
//    }
//
//    // Рекурсивно перебираем все Children
//    if (node.has("Children")) {
//        for (const auto& child : node["Children"]) {
//            ParseLhmTemperatures(child, temps);
//        }
//    }
//}
//
//std::vector<TemperatureMetrics> WindowsApiMetricsProvider::GetTemperatures() const {
//    std::vector<TemperatureMetrics> temps;
//
//    // 1. Пробуем забрать данные из LibreHardwareMonitor
//    std::string jsonStr = FetchLhmJson();
//    if (!jsonStr.empty()) {
//        auto parsed = crow::json::load(jsonStr);
//        if (parsed) {
//            ParseLhmTemperatures(parsed, temps);
//            if (!temps.empty()) {
//                return temps; // Успешно спарсили датчики!
//            }
//        }
//    }
//
//    // 2. Резервный вариант (Fallback), если LHM не отдаёт данные
//    double currentCpu = GetCPUMetrics();
//    double estimatedTemp = 37.0 + (currentCpu * 0.45);
//
//    TemperatureMetrics tm;
//    tm.SensorName = "CPU Package (Estimated)";
//    tm.Celsius = std::trunc(estimatedTemp * 10.0) / 10.0;
//    temps.push_back(tm);
//
//    return temps;
//}
//
//
//#include <shellapi.h> // Для ShellExecuteExW
//
//// 1. Удаление из автозагрузки
//bool WindowsApiMetricsProvider::RemoveStartupItem(const std::string& name, const std::string& location) const {
//    HKEY hKeyRoot = NULL;
//    const char* subKey = nullptr;
//
//    if (location == "HKCU Registry") {
//        hKeyRoot = HKEY_CURRENT_USER;
//        subKey = "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run";
//    }
//    else if (location == "HKLM Registry") {
//        hKeyRoot = HKEY_LOCAL_MACHINE;
//        subKey = "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run";
//    }
//    else if (location == "HKLM (x86) Registry") {
//        hKeyRoot = HKEY_LOCAL_MACHINE;
//        subKey = "SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Run";
//    }
//    else {
//        return false;
//    }
//
//    HKEY hKey;
//    // Открываем ключ с правами на запись (KEY_SET_VALUE)
//    if (RegOpenKeyExA(hKeyRoot, subKey, 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
//        LSTATUS status = RegDeleteValueA(hKey, name.c_str());
//        RegCloseKey(hKey);
//        return (status == ERROR_SUCCESS);
//    }
//
//    return false;
//}
//
//// 2. Открытие проводника с выделением файла процесса
//bool WindowsApiMetricsProvider::OpenFileLocation(unsigned long pid) const {
//    if (pid <= 4) return false;
//
//    UniqueHandle hProcess(OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid));
//    if (!hProcess.get()) return false;
//
//    wchar_t pathBuffer[MAX_PATH];
//    DWORD size = MAX_PATH;
//    if (QueryFullProcessImageNameW(hProcess.get(), 0, pathBuffer, &size)) {
//        // Формируем команду для explorer.exe: /select,"C:\Path\To\file.exe"
//        std::wstring param = L"/select,\"" + std::wstring(pathBuffer) + L"\"";
//
//        SHELLEXECUTEINFOW sei = { sizeof(sei) };
//        sei.lpVerb = L"open";
//        sei.lpFile = L"explorer.exe";
//        sei.lpParameters = param.c_str();
//        sei.nShow = SW_SHOWNORMAL;
//
//        return ShellExecuteExW(&sei);
//    }
//
//    return false;
//}