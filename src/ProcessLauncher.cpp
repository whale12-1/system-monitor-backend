#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <tlhelp32.h>
#include <iostream>
#include <string>
#include "ProcessLauncher.h"
#include <vector>

static PROCESS_INFORMATION g_uiProcessInfo = { 0 };

bool LaunchFrontendUI() {
    STARTUPINFOW si = { sizeof(si) };

    // 1. Получаем полный путь к папке, где находится сам SystemMonitor.exe
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);

    std::wstring currentDir(exePath);
    size_t lastSlash = currentDir.find_last_of(L"\\/");
    if (lastSlash != std::string::npos) {
        currentDir = currentDir.substr(0, lastSlash + 1); // Оставляем путь с слэшем на конце
    }

    // 2. Формируем полный путь к UI: "C:/.../x64/Release/SystemMonitorUI.exe"
    std::wstring uiPath = currentDir + L"SystemMonitorUI.exe";

    // CreateProcessW требует мутируемый буфер wchar_t
    std::vector<wchar_t> cmdLine(uiPath.begin(), uiPath.end());
    cmdLine.push_back(L'\0');

    // 3. Запускаем процесс с явным указанием рабочей директории (currentDir)
    BOOL success = CreateProcessW(
        NULL,
        cmdLine.data(),
        NULL,
        NULL,
        FALSE,
        0,
        NULL,
        currentDir.c_str(), // Рабочая директория = папка с бинарниками
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
        g_uiProcessInfo.hThread = NULL;
    }
}