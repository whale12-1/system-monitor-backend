#include "LinuxStartupManager.h"
#include <fstream>
#include <sstream>

#if !defined(_WIN32)
#include <unistd.h>
#include <filesystem>
namespace fs = std::filesystem;
#endif

std::vector<StartupItem> LinuxStartupManager::GetStartupItems() const {
#if !defined(_WIN32)
    return GetXdgStartupItems();
#else
    std::vector<StartupItem> items;
    StartupItem dummy{};
    dummy.Name = "Discord";
    dummy.Command = "discord --start-minimized";
    dummy.Location = "~/.config/autostart/discord.desktop";
    dummy.IsEnabled = true;
    items.push_back(dummy);
    return items;
#endif
}

std::vector<StartupItem> LinuxStartupManager::GetXdgStartupItems() const {
    std::vector<StartupItem> items;

#if !defined(_WIN32)
    std::vector<std::string> searchPaths;
    const char* home = std::getenv("HOME");
    if (home) {
        searchPaths.push_back(std::string(home) + "/.config/autostart");
    }
    searchPaths.push_back("/etc/xdg/autostart");

    for (const auto& path : searchPaths) {
        if (!fs::exists(path) || !fs::is_directory(path)) continue;

        for (const auto& entry : fs::directory_iterator(path)) {
            if (entry.path().extension() == ".desktop") {
                std::ifstream file(entry.path());
                if (!file.is_open()) continue;

                StartupItem item{};
                item.Location = entry.path().string();
                item.IsEnabled = true;

                std::string line;
                while (std::getline(file, line)) {
                    if (line.rfind("Name=", 0) == 0) {
                        item.Name = line.substr(5);
                    }
                    else if (line.rfind("Exec=", 0) == 0) {
                        item.Command = line.substr(5);
                    }
                    else if (line.rfind("X-GNOME-Autostart-enabled=false", 0) == 0 ||
                        line.rfind("Hidden=true", 0) == 0) {
                        item.IsEnabled = false;
                    }
                }

                if (!item.Name.empty()) {
                    items.push_back(item);
                }
            }
        }
    }
#endif

    return items;
}

bool LinuxStartupManager::RemoveStartupItem(const std::string& name, const std::string& location) const {
#if !defined(_WIN32)
    if (fs::exists(location)) {
        return fs::remove(location);
    }
#endif
    return false;
}

bool LinuxStartupManager::EnableStartupItem(const std::string& name, const std::string& location) const {
#if !defined(_WIN32)
    if (fs::exists(location)) {
        std::ifstream inFile(location);
        if (!inFile.is_open()) return false;

        std::string content;
        std::string line;
        bool foundKey = false;

        while (std::getline(inFile, line)) {
            if (line.rfind("X-GNOME-Autostart-enabled=", 0) == 0) {
                content += "X-GNOME-Autostart-enabled=true\n";
                foundKey = true;
            }
            else {
                content += line + "\n";
            }
        }
        inFile.close();

        if (!foundKey) {
            content += "X-GNOME-Autostart-enabled=true\n";
        }

        std::ofstream outFile(location, std::ios::trunc);
        if (!outFile.is_open()) return false;
        outFile << content;
        return true;
    }
#endif
    return false;
}