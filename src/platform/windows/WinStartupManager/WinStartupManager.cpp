#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#include "WinStartupManager.h"
#include <windows.h>
#include <shlobj.h>
#include <filesystem>
#include <map>
#include <algorithm>

namespace fs = std::filesystem;

// Вспомогательная функция для проверки статуса (Enabled/Disabled) в StartupApproved
static bool IsItemEnabled(HKEY hKeyRoot, const std::string& name, bool isFolder = false) {
    HKEY hKey;
    // Разные ветки для реестра и для папок автозагрузки
    const char* approvedPath = isFolder
        ? "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\StartupFolder"
        : "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run";

    if (RegOpenKeyExA(hKeyRoot, approvedPath, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        BYTE data[12];
        DWORD dataSize = sizeof(data);
        DWORD type = 0;

        if (RegQueryValueExA(hKey, name.c_str(), NULL, &type, data, &dataSize) == ERROR_SUCCESS) {
            RegCloseKey(hKey);
            // Если первый байт четный (например 0x02, 0x06, 0x00) — включено.
            // Если нечетный (например 0x01, 0x03) — отключено пользователем в Диспетчере задач.
            return (data[0] % 2 == 0);
        }
        RegCloseKey(hKey);
    }
    // Если записи в StartupApproved нет, по умолчанию считается включенным
    return true;
}

// Вспомогательная функция для чтения реестра
static void ReadRegistryStartup(HKEY hKeyRoot, const char* subKey, const std::string& locationName, std::vector<StartupItem>& items, DWORD extraAccessFlags = 0) {
    HKEY hKey;
    // Важно передавать extraAccessFlags (например KEY_WOW64_32KEY)
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

                // Проверяем статус сперва в HKCU, затем в HKLM
                bool enabled = IsItemEnabled(HKEY_CURRENT_USER, item.Name);
                if (enabled && hKeyRoot == HKEY_LOCAL_MACHINE) {
                    enabled = IsItemEnabled(HKEY_LOCAL_MACHINE, item.Name);
                }

                item.IsEnabled = enabled;
                items.push_back(item);
            }
            index++;
            valueNameSize = sizeof(valueName);
            valueDataSize = sizeof(valueData);
        }
        RegCloseKey(hKey);
    }
}

// Чтение системных папок "Автозагрузка" (Startup Folders)
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
                            item.Name = entry.path().filename().string(); // Берем имя файла с расширением для точности
                            item.Command = entry.path().string();
                            item.Location = locationName;

                            // Проверяем статус отключения ярлыка в Диспетчере задач
                            item.IsEnabled = IsItemEnabled(HKEY_CURRENT_USER, item.Name, true);

                            items.push_back(item);
                        }
                    }
                }
            }
        }
        catch (...) {
            // Игнорируем ошибки доступа к директории
        }
    }
}

std::vector<StartupItem> WinStartupManager::GetStartupItems() const {
    std::vector<StartupItem> items;

    // 1. Реестр (User & System)
    ReadRegistryStartup(HKEY_CURRENT_USER, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", "HKCU Registry", items);
    ReadRegistryStartup(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", "HKLM Registry", items);

    // 2. Реестр 32-битный (WOW6432Node) — передаем KEY_WOW64_32KEY!
    ReadRegistryStartup(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", "HKLM (x86) Registry", items, KEY_WOW64_32KEY);

    // 3. Папки Автозагрузки
    ReadFolderStartup(CSIDL_STARTUP, "User Startup Folder", items);
    ReadFolderStartup(CSIDL_COMMON_STARTUP, "System Startup Folder", items);

    return items;
}

bool WinStartupManager::RemoveStartupItem(const std::string& name, const std::string& location) const {
    // 1. Если удаляем из реестра
    HKEY hKeyRoot = NULL;
    const char* subKey = nullptr;
    DWORD extraFlags = 0;

    if (location == "HKCU Registry") {
        hKeyRoot = HKEY_CURRENT_USER;
        subKey = "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run";
    }
    else if (location == "HKLM Registry") {
        hKeyRoot = HKEY_LOCAL_MACHINE;
        subKey = "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run";
    }
    else if (location == "HKLM (x86) Registry") {
        hKeyRoot = HKEY_LOCAL_MACHINE;
        subKey = "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run";
        extraFlags = KEY_WOW64_32KEY; // Обязательно для 32-битной ветки
    }

    if (hKeyRoot != NULL) {
        HKEY hKey;
        if (RegOpenKeyExA(hKeyRoot, subKey, 0, KEY_SET_VALUE | extraFlags, &hKey) == ERROR_SUCCESS) {
            LSTATUS status = RegDeleteValueA(hKey, name.c_str());
            RegCloseKey(hKey);
            return (status == ERROR_SUCCESS);
        }
        return false;
    }

    // 2. Если удаляем файл из папки Автозагрузки
    int csidl = -1;
    if (location == "User Startup Folder") csidl = CSIDL_STARTUP;
    if (location == "System Startup Folder") csidl = CSIDL_COMMON_STARTUP;

    if (csidl != -1) {
        char path[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathA(NULL, csidl, NULL, 0, path))) {
            fs::path folderPath(path);
            if (fs::exists(folderPath)) {
                for (const auto& entry : fs::directory_iterator(folderPath)) {
                    if (entry.path().filename().string() == name || entry.path().stem().string() == name) {
                        std::error_code ec;
                        return fs::remove(entry.path(), ec);
                    }
                }
            }
        }
    }

    return false;
}