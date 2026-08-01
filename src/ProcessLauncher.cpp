#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <tlhelp32.h>
#include <iostream>
#include <string>
#include "ProcessLauncher.h"

// Глобальный хэндл процесса GUI (виден только в этом .cpp)
static PROCESS_INFORMATION g_uiProcessInfo = { 0 };

bool LaunchFrontendUI() {
    STARTUPINFOW si = { sizeof(si) };
    wchar_t cmdLine[] = L"SystemMonitorUI.exe";

    BOOL success = CreateProcessW(
        NULL,
        cmdLine,
        NULL,
        NULL,
        FALSE,
        0,
        NULL,
        NULL,
        &si,
        &g_uiProcessInfo
    );

    if (success) {
        std::cout << "[Backend] SystemMonitorUI.exe успешно запущен!\n";
        return true;
    }
    else {
        std::cerr << "[Backend] Ошибка запуска SystemMonitorUI.exe. Код: "
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
    }
}