#include "ProcessLauncher.h"
#include <iostream>
#include <string>
#include <vector>

#if defined(_WIN32) || defined(_WIN64)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>

static PROCESS_INFORMATION g_uiProcessInfo = { 0 };
#endif

bool LaunchFrontendUI(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--no-ui" || arg == "-noui") {
            std::cout << "[Backend] Dev Mode: no UI start (--no-ui).\n";
            return true;
        }
    }

#if defined(_WIN32) || defined(_WIN64)
    STARTUPINFOW si = { sizeof(si) };

    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);

    std::wstring currentDir(exePath);
    size_t lastSlash = currentDir.find_last_of(L"\\/");
    if (lastSlash != std::string::npos) {
        currentDir = currentDir.substr(0, lastSlash + 1);
    }

    std::wstring uiPath = currentDir + L"SystemMonitorUI.exe";
    std::vector<wchar_t> cmdLine(uiPath.begin(), uiPath.end());
    cmdLine.push_back(L'\0');

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
        std::cerr << "[Backend] Error while starting SystemMonitorUI.exe. Code: "
            << GetLastError() << "\n";
        return false;
    }
#else
    std::cout << "[Backend] POSIX/Android environment. Skiped launching standalone EXE UI.\n";
    return true;
#endif
}

void TerminateFrontendUI() {
#if defined(_WIN32) || defined(_WIN64)
    if (g_uiProcessInfo.hProcess != NULL) {
        TerminateProcess(g_uiProcessInfo.hProcess, 0);
        CloseHandle(g_uiProcessInfo.hProcess);
        CloseHandle(g_uiProcessInfo.hThread);
        g_uiProcessInfo.hProcess = NULL;
        g_uiProcessInfo.hThread = NULL;
    }
#endif
}