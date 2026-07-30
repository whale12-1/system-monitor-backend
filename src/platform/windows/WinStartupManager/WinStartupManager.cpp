#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#include "WinStartupManager.h"
#include <windows.h>

static void ReadRegistryStartup(HKEY hKeyRoot, const char* subKey, const std::string& locationName, std::vector<StartupItem>& items) {
    HKEY hKey;
    if (RegOpenKeyExA(hKeyRoot, subKey, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
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
                items.push_back(item);
            }
            index++;
            valueNameSize = sizeof(valueName);
            valueDataSize = sizeof(valueData);
        }
        RegCloseKey(hKey);
    }
}

std::vector<StartupItem> WinStartupManager::GetStartupItems() const {
    std::vector<StartupItem> items;

    ReadRegistryStartup(HKEY_CURRENT_USER, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", "HKCU Registry", items);
    ReadRegistryStartup(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", "HKLM Registry", items);
    ReadRegistryStartup(HKEY_LOCAL_MACHINE, "SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Run", "HKLM (x86) Registry", items);

    return items;
}

bool WinStartupManager::RemoveStartupItem(const std::string& name, const std::string& location) const {
    HKEY hKeyRoot = NULL;
    const char* subKey = nullptr;

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
        subKey = "SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Run";
    }
    else {
        return false;
    }

    HKEY hKey;
    if (RegOpenKeyExA(hKeyRoot, subKey, 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        LSTATUS status = RegDeleteValueA(hKey, name.c_str());
        RegCloseKey(hKey);
        return (status == ERROR_SUCCESS);
    }

    return false;
}
