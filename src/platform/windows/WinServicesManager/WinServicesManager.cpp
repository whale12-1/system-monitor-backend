#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#include "WinServicesManager.h"
#include <windows.h>
#include <vector>
#include <iostream>

// --- Вспомогательная функция для перевода CP1251 (ANSI) -> UTF-8 ---
static std::string Cp1251ToUtf8(const std::string& str) {
    if (str.empty()) return "";

    // 1. Узнаем размер буфера для WCHAR (UTF-16)
    int wsize = MultiByteToWideChar(1251, 0, str.c_str(), -1, NULL, 0);
    if (wsize <= 0) return "";

    std::wstring wstr(wsize, 0);
    MultiByteToWideChar(1251, 0, str.c_str(), -1, &wstr[0], wsize);

    // 2. Узнаем размер буфера для UTF-8
    int utf8size = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, NULL, 0, NULL, NULL);
    if (utf8size <= 0) return "";

    std::string utf8str(utf8size - 1, 0); // -1 чтобы убрать завершающий null-терминатор
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &utf8str[0], utf8size, NULL, NULL);

    return utf8str;
}

// Вспомогательная функция конвертации строкового статуса SCM в читаемый вид
static std::string ServiceStateToString(DWORD state) {
    switch (state) {
    case SERVICE_RUNNING:       return "Running";
    case SERVICE_STOPPED:       return "Stopped";
    case SERVICE_START_PENDING: return "Starting...";
    case SERVICE_STOP_PENDING:  return "Stopping...";
    case SERVICE_PAUSED:        return "Paused";
    default:                    return "Unknown";
    }
}

// Вспомогательная функция конвертации типа запуска
static std::string StartTypeToString(DWORD startType) {
    switch (startType) {
    case SERVICE_AUTO_START:   return "Automatic";
    case SERVICE_DEMAND_START: return "Manual";
    case SERVICE_DISABLED:     return "Disabled";
    case SERVICE_BOOT_START:   return "Boot";
    case SERVICE_SYSTEM_START: return "System";
    default:                   return "Unknown";
    }
}

// Получение пути и типа запуска для конкретной службы
static void QueryServiceDetails(SC_HANDLE scmHandle, const std::string& serviceName, std::string& outPath, std::string& outStartType) {
    SC_HANDLE hService = OpenServiceA(scmHandle, serviceName.c_str(), SERVICE_QUERY_CONFIG);
    if (!hService) return;

    DWORD bytesNeeded = 0;
    (void)QueryServiceConfigA(hService, NULL, 0, &bytesNeeded);

    if (GetLastError() == ERROR_INSUFFICIENT_BUFFER && bytesNeeded > 0) {
        std::vector<BYTE> buffer(bytesNeeded);
        LPQUERY_SERVICE_CONFIGA pConfig = reinterpret_cast<LPQUERY_SERVICE_CONFIGA>(buffer.data());

        if (QueryServiceConfigA(hService, pConfig, bytesNeeded, &bytesNeeded)) {
            if (pConfig->lpBinaryPathName) {
                // 🔥 Конвертируем путь из CP1251 в UTF-8 (в путях тоже бывают русские имена папок)
                outPath = Cp1251ToUtf8(pConfig->lpBinaryPathName);
            }
            outStartType = StartTypeToString(pConfig->dwStartType);
        }
    }

    CloseServiceHandle(hService);
}

// --- 1. Просмотр служб ---
std::vector<ServiceItem> WinServicesManager::GetServices() const {
    std::vector<ServiceItem> services;

    SC_HANDLE scmHandle = OpenSCManagerA(NULL, NULL, SC_MANAGER_ENUMERATE_SERVICE);
    if (!scmHandle) {
        std::cerr << "[WinServicesManager] OpenSCManagerA failed. Error: " << GetLastError() << std::endl;
        return services;
    }

    DWORD bytesNeeded = 0;
    DWORD servicesReturned = 0;
    DWORD resumeHandle = 0;
    DWORD serviceType = SERVICE_WIN32;

    EnumServicesStatusExA(
        scmHandle,
        SC_ENUM_PROCESS_INFO,
        serviceType,
        SERVICE_STATE_ALL,
        NULL,
        0,
        &bytesNeeded,
        &servicesReturned,
        &resumeHandle,
        NULL
    );

    DWORD err = GetLastError();

    if ((err == ERROR_INSUFFICIENT_BUFFER || err == ERROR_MORE_DATA) && bytesNeeded > 0) {
        DWORD bufferSize = bytesNeeded + 4096;
        std::vector<BYTE> buffer(bufferSize);

        ENUM_SERVICE_STATUS_PROCESSA* pServices = reinterpret_cast<ENUM_SERVICE_STATUS_PROCESSA*>(buffer.data());

        resumeHandle = 0;

        BOOL result = EnumServicesStatusExA(
            scmHandle,
            SC_ENUM_PROCESS_INFO,
            serviceType,
            SERVICE_STATE_ALL,
            buffer.data(),
            bufferSize,
            &bytesNeeded,
            &servicesReturned,
            &resumeHandle,
            NULL
        );

        if (result) {
            services.reserve(servicesReturned);

            for (DWORD i = 0; i < servicesReturned; ++i) {
                ServiceItem item;
                // Имя системного идентификатора (обычно на латинице):
                item.Name = pServices[i].lpServiceName ? pServices[i].lpServiceName : "";

                // 🔥 Отображаемое имя (русский текст!) конвертируем из CP1251 в UTF-8:
                std::string rawDisplayName = pServices[i].lpDisplayName ? pServices[i].lpDisplayName : "";
                item.DisplayName = Cp1251ToUtf8(rawDisplayName);

                item.Status = ServiceStateToString(pServices[i].ServiceStatusProcess.dwCurrentState);

                QueryServiceDetails(scmHandle, item.Name, item.Path, item.StartType);

                services.push_back(item);
            }
        }
        else {
            std::cerr << "[WinServicesManager] Second EnumServicesStatusExA failed. Error: " << GetLastError() << std::endl;
        }
    }
    else {
        std::cerr << "[WinServicesManager] First EnumServicesStatusExA failed. Error: " << err << std::endl;
    }

    CloseServiceHandle(scmHandle);
    return services;
}

// --- 2. Добавление (Создание) службы ---
// --- 2. Добавление (Создание) службы с подробным логом ---
bool WinServicesManager::CreateWinService(const std::string& serviceName, const std::string& displayName, const std::string& binaryPath, bool autoStart) const {

    // 1. Открываем SCM с явными правами на создание
    SC_HANDLE scmHandle = OpenSCManagerA(NULL, NULL, SC_MANAGER_CREATE_SERVICE);
    if (!scmHandle) {
        DWORD err = GetLastError();
        std::cerr << "[WinServicesManager] OpenSCManagerA failed. Error code: " << err << std::endl;
        return false;
    }

    DWORD dwStartType = autoStart ? SERVICE_AUTO_START : SERVICE_DEMAND_START;

    // 2. Пробуем создать службу
    SC_HANDLE hService = CreateServiceA(
        scmHandle,
        serviceName.c_str(),
        displayName.c_str(),
        SERVICE_ALL_ACCESS,
        SERVICE_WIN32_OWN_PROCESS,
        dwStartType,
        SERVICE_ERROR_NORMAL,
        binaryPath.c_str(),
        NULL, NULL, NULL, NULL, NULL
    );

    if (hService == NULL) {
        DWORD err = GetLastError();
        std::cerr << "[WinServicesManager] CreateServiceA failed! "
            << "Name: '" << serviceName << "', "
            << "Path: '" << binaryPath << "'. "
            << "Win32 Error Code: " << err << std::endl;

        // Расшифровка наиболее частых ошибок WinAPI
        switch (err) {
        case ERROR_ACCESS_DENIED: // Код 5
            std::cerr << "  -> Причина: Отказано в доступе (ERROR_ACCESS_DENIED). Проверьте UAC/Права." << std::endl;
            break;
        case ERROR_DUPLICATE_TAG:
        case ERROR_SERVICE_EXISTS: // Код 1073
            std::cerr << "  -> Причина: Служба с таким именем уже существует!" << std::endl;
            break;
        case ERROR_INVALID_NAME: // Код 123
            std::cerr << "  -> Причина: Недопустимое имя службы или путь." << std::endl;
            break;
        case ERROR_INVALID_PARAMETER: // Код 87
            std::cerr << "  -> Причина: Передан неверный параметр в CreateServiceA." << std::endl;
            break;
        case ERROR_SERVICE_MARKED_FOR_DELETE: // Код 1072
            std::cerr << "  -> Причина: Служба находится в процессе удаления." << std::endl;
            break;
        default:
            std::cerr << "  -> Неизвестная ошибка Win32." << std::endl;
            break;
        }

        CloseServiceHandle(scmHandle);
        return false;
    }

    std::cout << "[WinServicesManager] Служба '" << serviceName << "' успешно создана!" << std::endl;

    CloseServiceHandle(hService);
    CloseServiceHandle(scmHandle);
    return true;
}

// --- 3. Удаление службы ---
bool WinServicesManager::DeleteWinService(const std::string& serviceName) const {
    SC_HANDLE scmHandle = OpenSCManagerA(NULL, NULL, SC_MANAGER_CONNECT);
    if (!scmHandle) return false;

    SC_HANDLE hService = OpenServiceA(scmHandle, serviceName.c_str(), DELETE);
    if (!hService) {
        CloseServiceHandle(scmHandle);
        return false;
    }

    bool result = DeleteService(hService);

    CloseServiceHandle(hService);
    CloseServiceHandle(scmHandle);
    return result;
}

// --- 4. Запуск службы ---
bool WinServicesManager::StartWinService(const std::string& serviceName) const {
    SC_HANDLE scmHandle = OpenSCManagerA(NULL, NULL, SC_MANAGER_CONNECT);
    if (!scmHandle) return false;

    SC_HANDLE hService = OpenServiceA(scmHandle, serviceName.c_str(), SERVICE_START);
    if (!hService) {
        CloseServiceHandle(scmHandle);
        return false;
    }

    bool result = StartServiceA(hService, 0, NULL);

    CloseServiceHandle(hService);
    CloseServiceHandle(scmHandle);
    return result;
}

// --- 5. Остановка службы ---
bool WinServicesManager::StopWinService(const std::string& serviceName) const {
    SC_HANDLE scmHandle = OpenSCManagerA(NULL, NULL, SC_MANAGER_CONNECT);
    if (!scmHandle) return false;

    SC_HANDLE hService = OpenServiceA(scmHandle, serviceName.c_str(), SERVICE_STOP);
    if (!hService) {
        CloseServiceHandle(scmHandle);
        return false;
    }

    SERVICE_STATUS status;
    bool result = ControlService(hService, SERVICE_CONTROL_STOP, &status);

    CloseServiceHandle(hService);
    CloseServiceHandle(scmHandle);
    return result;
}