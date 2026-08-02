#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include "ProcessLauncher.h"
#include <windows.h>
#include <tlhelp32.h>
#include <iostream>
#include <string>
#include <vector>

static PROCESS_INFORMATION g_uiProcessInfo = { 0 };

bool LaunchFrontendUI(int argc, char* argv[]) {
    // 0. Проверяем флаг --no-ui (или -noui)
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--no-ui" || arg == "-noui") {
            std::cout << "[Backend] Dev Mode: no UI start (--no-ui).\n";
            return true; // Возвращаем true, будто запуск прошёл штатно
        }
    }

    STARTUPINFOW si = { sizeof(si) };

    // 1. Получаем полный путь к папке, где находится сам SystemMonitor.exe
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);

    std::wstring currentDir(exePath);
    size_t lastSlash = currentDir.find_last_of(L"\\/");
    if (lastSlash != std::string::npos) {
        currentDir = currentDir.substr(0, lastSlash + 1);
    }

    // 2. Формируем полный путь к UI
    std::wstring uiPath = currentDir + L"SystemMonitorUI.exe";

    std::vector<wchar_t> cmdLine(uiPath.begin(), uiPath.end());
    cmdLine.push_back(L'\0');

    // 3. Запускаем процесс
    BOOL success = CreateProcessW(
        NULL,
        cmdLine.data(),
        NULL,
        NULL,
        FALSE,
        0,
        NULL,
        currentDir.c_str(),
        &si,
        &g_uiProcessInfo
    );

    if (success) {
        std::cout << "[Backend] SystemMonitorUI.exe is launched successfully!\n";
        return true;
    }
    else {
        std::cerr << "[Backend] Erroe while starting SystemMonitorUI.exe. Code: "
            << GetLastError() << "\n";
        return false;
    }
}

void TerminateFrontendUI() {
    if (g_uiProcessInfo.hProcess != NULL) {
        TerminateProcess(g_uiProcessInfo.hProcess, 0);
        CloseHandle(g_uiProcessInfo.hProcess);
        CloseHandle(g_uiProcessInfo.hThread);
        g_uiProcessInfo.hProcess = NULL;
        g_uiProcessInfo.hThread = NULL;
    }
}