#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#include "WinStartupManager.h"
#include <windows.h>
#include <shlobj.h>
#include <taskschd.h> // Для работы с Планировщиком задач
#include <comdef.h>
#include <filesystem>
#include <algorithm>
#include <iostream>

#pragma comment(lib, "taskschd.lib")
#pragma comment(lib, "comsuppw.lib")

namespace fs = std::filesystem;

// Вспомогательная функция проверки включен ли пункт в Диспетчере задач
static bool IsItemEnabled(HKEY hKeyRoot, const std::string& name, bool isFolder = false, DWORD extraFlags = 0) {
    HKEY hKey;
    const char* approvedPath = isFolder
        ? "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\StartupFolder"
        : "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run";

    if (RegOpenKeyExA(hKeyRoot, approvedPath, 0, KEY_READ | extraFlags, &hKey) == ERROR_SUCCESS) {
        BYTE data[12];
        DWORD dataSize = sizeof(data);
        DWORD type = 0;

        if (RegQueryValueExA(hKey, name.c_str(), NULL, &type, data, &dataSize) == ERROR_SUCCESS && dataSize >= 1) {
            RegCloseKey(hKey);
            return (data[0] % 2 == 0); // Четный первый байт = Включен
        }
        RegCloseKey(hKey);
    }
    return true;
}

// Вспомогательное чтение реестра
static void ReadRegistryStartup(HKEY hKeyRoot, const char* subKey, const std::string& locationName, std::vector<StartupItem>& items, DWORD extraAccessFlags = 0) {
    HKEY hKey;
    if (RegOpenKeyExA(hKeyRoot, subKey, 0, KEY_READ | extraAccessFlags, &hKey) == ERROR_SUCCESS) {
        DWORD index = 0;
        char valueName[256];
        DWORD valueNameSize = sizeof(valueName);
        BYTE valueData[1024];
        DWORD valueDataSize = sizeof(valueData);
        DWORD type = 0;

        while (RegEnumValueA(hKey, index, valueName, &valueNameSize, NULL, &type, valueData, &valueDataSize) == ERROR_SUCCESS) {
            if (type == REG_SZ || type == REG_EXPAND_SZ) {
                StartupItem item;
                item.Name = valueName;
                item.Command = reinterpret_cast<char*>(valueData);
                item.Location = locationName;

                // Проверяем статус ровно в той ветке и с той разрядностью, где лежат данные
                item.IsEnabled = IsItemEnabled(hKeyRoot, item.Name, false, extraAccessFlags);

                items.push_back(item);
            }
            index++;
            valueNameSize = sizeof(valueName);
            valueDataSize = sizeof(valueData);
        }
        RegCloseKey(hKey);
    }
}

// Чтение системных папок автозагрузки
static void ReadFolderStartup(int csidlFolder, const std::string& locationName, std::vector<StartupItem>& items) {
    char path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(NULL, csidlFolder, NULL, 0, path))) {
        try {
            if (fs::exists(path) && fs::is_directory(path)) {
                for (const auto& entry : fs::directory_iterator(path)) {
                    if (entry.is_regular_file()) {
                        std::string ext = entry.path().extension().string();
                        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

                        if (ext == ".lnk" || ext == ".exe" || ext == ".bat" || ext == ".cmd") {
                            StartupItem item;
                            item.Name = entry.path().filename().string();
                            item.Command = entry.path().string();
                            item.Location = locationName;
                            item.IsEnabled = IsItemEnabled(HKEY_CURRENT_USER, item.Name, true);

                            items.push_back(item);
                        }
                    }
                }
            }
        }
        catch (...) {}
    }
}

// Чтение автозапуска из Планировщика задач (Task Scheduler)
static void ReadScheduledTasks(std::vector<StartupItem>& items) {
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    bool coInitialized = SUCCEEDED(hr);

    ITaskService* pService = NULL;
    hr = CoCreateInstance(CLSID_TaskScheduler, NULL, CLSCTX_INPROC_SERVER, IID_ITaskService, (void**)&pService);
    if (FAILED(hr)) {
        if (coInitialized) CoUninitialize();
        return;
    }

    hr = pService->Connect(_variant_t(), _variant_t(), _variant_t(), _variant_t());
    if (FAILED(hr)) {
        pService->Release();
        if (coInitialized) CoUninitialize();
        return;
    }

    ITaskFolder* pRootFolder = NULL;
    hr = pService->GetFolder(_bstr_t(L"\\"), &pRootFolder);
    if (SUCCEEDED(hr)) {
        IRegisteredTaskCollection* pTaskCollection = NULL;
        hr = pRootFolder->GetTasks(TASK_ENUM_HIDDEN, &pTaskCollection);

        if (SUCCEEDED(hr)) {
            LONG numTasks = 0;
            pTaskCollection->get_Count(&numTasks);

            for (LONG i = 1; i <= numTasks; i++) {
                IRegisteredTask* pRegisteredTask = NULL;
                if (SUCCEEDED(pTaskCollection->get_Item(_variant_t(i), &pRegisteredTask))) {
                    BSTR bstrTaskName;
                    pRegisteredTask->get_Name(&bstrTaskName);

                    TASK_STATE taskState;
                    pRegisteredTask->get_State(&taskState);

                    ITaskDefinition* pTaskDef = NULL;
                    if (SUCCEEDED(pRegisteredTask->get_Definition(&pTaskDef))) {
                        ITriggerCollection* pTriggers = NULL;
                        if (SUCCEEDED(pTaskDef->get_Triggers(&pTriggers))) {
                            LONG triggerCount = 0;
                            pTriggers->get_Count(&triggerCount);

                            bool isLogonTrigger = false;
                            for (LONG t = 1; t <= triggerCount; t++) {
                                ITrigger* pTrigger = NULL;
                                if (SUCCEEDED(pTriggers->get_Item(t, &pTrigger))) {
                                    TASK_TRIGGER_TYPE2 triggerType;
                                    pTrigger->get_Type(&triggerType);
                                    if (triggerType == TASK_TRIGGER_LOGON || triggerType == TASK_TRIGGER_BOOT) {
                                        isLogonTrigger = true;
                                    }
                                    pTrigger->Release();
                                }
                            }
                            pTriggers->Release();

                            // Если задача срабатывает при входе в систему
                            if (isLogonTrigger) {
                                IActionCollection* pActions = NULL;
                                if (SUCCEEDED(pTaskDef->get_Actions(&pActions))) {
                                    IAction* pAction = NULL;
                                    // Берем первое действие
                                    if (SUCCEEDED(pActions->get_Item(1, &pAction))) {
                                        TASK_ACTION_TYPE actionType;
                                        pAction->get_Type(&actionType);
                                        if (actionType == TASK_ACTION_EXEC) {
                                            IExecAction* pExecAction = NULL;
                                            if (SUCCEEDED(pAction->QueryInterface(IID_IExecAction, (void**)&pExecAction))) {
                                                BSTR bstrPath;
                                                pExecAction->get_Path(&bstrPath);

                                                StartupItem item;
                                                item.Name = (_bstr_t)bstrTaskName;
                                                item.Command = (_bstr_t)bstrPath;
                                                item.Location = "Task Scheduler (Logon)";
                                                item.IsEnabled = (taskState != TASK_STATE_DISABLED);

                                                items.push_back(item);
                                                SysFreeString(bstrPath);
                                                pExecAction->Release();
                                            }
                                        }
                                        pAction->Release();
                                    }
                                    pActions->Release();
                                }
                            }
                        }
                        pTaskDef->Release();
                    }
                    SysFreeString(bstrTaskName);
                    pRegisteredTask->Release();
                }
            }
            pTaskCollection->Release();
        }
        pRootFolder->Release();
    }
    pService->Release();
    if (coInitialized) CoUninitialize();
}

std::vector<StartupItem> WinStartupManager::GetStartupItems() const {
    std::vector<StartupItem> items;

    // 1. Стандартные ветки Run (HKCU & HKLM)
    ReadRegistryStartup(HKEY_CURRENT_USER, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", "HKCU Registry", items);
    ReadRegistryStartup(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", "HKLM Registry", items);

    // 2. Ветки RunOnce
    ReadRegistryStartup(HKEY_CURRENT_USER, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\RunOnce", "HKCU RunOnce", items);
    ReadRegistryStartup(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\RunOnce", "HKLM RunOnce", items);

    // 3. 32-битные ветки в 64-битной Windows (WOW6432Node)
    ReadRegistryStartup(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", "HKLM (x86) Registry", items, KEY_WOW64_32KEY);
    ReadRegistryStartup(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\RunOnce", "HKLM (x86) RunOnce", items, KEY_WOW64_32KEY);

    // 4. Папки автозагрузки
    ReadFolderStartup(CSIDL_STARTUP, "User Startup Folder", items);
    ReadFolderStartup(CSIDL_COMMON_STARTUP, "System Startup Folder", items);

    // 5. Запланированные задачи Windows (Task Scheduler)
    ReadScheduledTasks(items);

    return items;
}

// Вспомогательная функция для установки флага включено (0x02) в ветке StartupApproved
static bool SetStartupApprovedEnabled(HKEY hKeyRoot, const std::string& name, bool isFolder) {
    const char* approvedPath = isFolder
        ? "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\StartupFolder"
        : "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run";

    HKEY hKey;
    if (RegOpenKeyExA(hKeyRoot, approvedPath, 0, KEY_READ | KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        BYTE data[12] = { 0 };
        DWORD dataSize = sizeof(data);
        DWORD type = REG_BINARY;

        // Если параметр уже есть в ветке Approved, меняем байт состояния на четный (0x02 — включено)
        if (RegQueryValueExA(hKey, name.c_str(), NULL, &type, data, &dataSize) == ERROR_SUCCESS && dataSize >= 1) {
            if (data[0] % 2 != 0) {
                data[0] -= 1; // Например, 0x03 (выкл) превращаем в 0x02 (вкл)
            }
        }
        else {
            // Если записи еще не было, прописываем дефолтный заголовок включенной записи Windows
            dataSize = 12;
            data[0] = 0x02;
        }

        LSTATUS status = RegSetValueExA(hKey, name.c_str(), 0, REG_BINARY, data, dataSize);
        RegCloseKey(hKey);
        return (status == ERROR_SUCCESS);
    }
    return false;
}

// Вспомогательная функция для установки флага отключено (0x03) в ветке StartupApproved
static bool SetStartupApprovedDisabled(HKEY hKeyRoot, const std::string& name, bool isFolder) {
    const char* approvedPath = isFolder
        ? "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\StartupFolder"
        : "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run";

    HKEY hKey;
    if (RegOpenKeyExA(hKeyRoot, approvedPath, 0, KEY_READ | KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        BYTE data[12] = { 0 };
        DWORD dataSize = sizeof(data);
        DWORD type = REG_BINARY;

        if (RegQueryValueExA(hKey, name.c_str(), NULL, &type, data, &dataSize) == ERROR_SUCCESS && dataSize >= 1) {
            if (data[0] % 2 == 0) {
                data[0] += 1; // 0x02 (вкл) -> 0x03 (выкл)
            }
        }
        else {
            dataSize = 12;
            data[0] = 0x03; // Дефолтный флаг "Отключено"
        }

        LSTATUS status = RegSetValueExA(hKey, name.c_str(), 0, REG_BINARY, data, dataSize);
        RegCloseKey(hKey);
        return (status == ERROR_SUCCESS);
    }
    return false;
}

// Вспомогательная функция включения задачи в Планировщике
static bool EnableTaskSchedulerTask(const std::string& taskName) {
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    bool coInitialized = SUCCEEDED(hr);

    ITaskService* pService = NULL;
    hr = CoCreateInstance(CLSID_TaskScheduler, NULL, CLSCTX_INPROC_SERVER, IID_ITaskService, (void**)&pService);
    if (FAILED(hr)) {
        if (coInitialized) CoUninitialize();
        return false;
    }

    hr = pService->Connect(_variant_t(), _variant_t(), _variant_t(), _variant_t());
    if (FAILED(hr)) {
        pService->Release();
        if (coInitialized) CoUninitialize();
        return false;
    }

    bool success = false;
    ITaskFolder* pRootFolder = NULL;
    hr = pService->GetFolder(_bstr_t(L"\\"), &pRootFolder);
    if (SUCCEEDED(hr)) {
        IRegisteredTask* pRegisteredTask = NULL;
        _bstr_t bstrName(taskName.c_str());

        if (SUCCEEDED(pRootFolder->GetTask(bstrName, &pRegisteredTask))) {
            hr = pRegisteredTask->put_Enabled(VARIANT_TRUE);
            success = SUCCEEDED(hr);
            pRegisteredTask->Release();
        }
        pRootFolder->Release();
    }

    pService->Release();
    if (coInitialized) CoUninitialize();
    return success;
}

// Вспомогательная функция отключения задачи в Планировщике
static bool DisableTaskSchedulerTask(const std::string& taskName) {
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    bool coInitialized = SUCCEEDED(hr);

    ITaskService* pService = NULL;
    hr = CoCreateInstance(CLSID_TaskScheduler, NULL, CLSCTX_INPROC_SERVER, IID_ITaskService, (void**)&pService);
    if (FAILED(hr)) {
        if (coInitialized) CoUninitialize();
        return false;
    }

    hr = pService->Connect(_variant_t(), _variant_t(), _variant_t(), _variant_t());
    if (FAILED(hr)) {
        pService->Release();
        if (coInitialized) CoUninitialize();
        return false;
    }

    bool success = false;
    ITaskFolder* pRootFolder = NULL;
    if (SUCCEEDED(pService->GetFolder(_bstr_t(L"\\"), &pRootFolder))) {
        IRegisteredTask* pRegisteredTask = NULL;
        _bstr_t bstrName(taskName.c_str());

        if (SUCCEEDED(pRootFolder->GetTask(bstrName, &pRegisteredTask))) {
            hr = pRegisteredTask->put_Enabled(VARIANT_FALSE);
            success = SUCCEEDED(hr);
            pRegisteredTask->Release();
        }
        pRootFolder->Release();
    }

    pService->Release();
    if (coInitialized) CoUninitialize();
    return success;
}

// Мягкое отключение элемента вместо физического удаления
bool WinStartupManager::RemoveStartupItem(const std::string& name, const std::string& location) const {
    // 1. Ветки реестра (HKCU / HKLM)
    if (location == "HKCU Registry" || location == "HKCU RunOnce") {
        return SetStartupApprovedDisabled(HKEY_CURRENT_USER, name, false);
    }
    if (location == "HKLM Registry" || location == "HKLM (x86) Registry" || location == "HKLM RunOnce") {
        return SetStartupApprovedDisabled(HKEY_LOCAL_MACHINE, name, false);
    }

    // 2. Папки автозагрузки
    if (location == "User Startup Folder") {
        return SetStartupApprovedDisabled(HKEY_CURRENT_USER, name, true);
    }
    if (location == "System Startup Folder") {
        return SetStartupApprovedDisabled(HKEY_LOCAL_MACHINE, name, true);
    }

    // 3. Планировщик задач
    if (location == "Task Scheduler (Logon)") {
        return DisableTaskSchedulerTask(name);
    }

    return false;
}

// Включение элемента обратно
bool WinStartupManager::EnableStartupItem(const std::string& name, const std::string& location) const {
    // 1. Ветки реестра (HKCU / HKLM)
    if (location == "HKCU Registry" || location == "HKCU RunOnce") {
        return SetStartupApprovedEnabled(HKEY_CURRENT_USER, name, false);
    }
    if (location == "HKLM Registry" || location == "HKLM (x86) Registry" || location == "HKLM RunOnce") {
        return SetStartupApprovedEnabled(HKEY_LOCAL_MACHINE, name, false);
    }

    // 2. Папки автозагрузки
    if (location == "User Startup Folder") {
        return SetStartupApprovedEnabled(HKEY_CURRENT_USER, name, true);
    }
    if (location == "System Startup Folder") {
        return SetStartupApprovedEnabled(HKEY_LOCAL_MACHINE, name, true);
    }

    // 3. Планировщик задач
    if (location == "Task Scheduler (Logon)") {
        return EnableTaskSchedulerTask(name);
    }

    return false;
}