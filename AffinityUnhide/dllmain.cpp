﻿// AffinityUnhide.cpp - DLL to set WDA_NONE
#include <windows.h>
#include <string>
#include <shlwapi.h>
#pragma comment(lib, "shlwapi.lib")

// Log function
void DebugLog(const char* format, ...) {
    char buffer[512];
    va_list args;
    va_start(args, format);
    vsprintf_s(buffer, format, args);
    va_end(args);
    OutputDebugStringA(buffer);
}

// Get process name
std::string GetProcessName() {
    char path[MAX_PATH] = { 0 };
    if (GetModuleFileNameA(NULL, path, MAX_PATH)) {
        PathStripPathA(path);
        return std::string(path);
    }
    return "unknown";
}

// Apply WDA_NONE to all windows in current process
void ApplyNormalAffinity() {
    std::string procName = GetProcessName();
    DebugLog("====== Setting WDA_NONE for %s ======", procName.c_str());

    // Enumerate windows that belong to this process
    HWND hwnd = NULL;
    int windowCount = 0;

    while ((hwnd = FindWindowEx(NULL, hwnd, NULL, NULL)) != NULL) {
        DWORD pid;
        GetWindowThreadProcessId(hwnd, &pid);

        if (pid == GetCurrentProcessId() && IsWindowVisible(hwnd)) {
            // Get window title
            char title[MAX_PATH] = { 0 };
            GetWindowTextA(hwnd, title, MAX_PATH);

            // Set display affinity
            if (SetWindowDisplayAffinity(hwnd, WDA_NONE)) {
                DebugLog("[SUCCESS] Window: %s - Set to WDA_NONE", (title[0] ? title : "<No Title>"));
                windowCount++;
            }
            else {
                DebugLog("[FAILED] Window: %s - Error: %d", (title[0] ? title : "<No Title>"), GetLastError());
            }
        }
    }

    DebugLog("====== Modified %d windows for %s ======", windowCount, procName.c_str());
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved) {
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        // Apply affinity when DLL is loaded
        ApplyNormalAffinity();
        break;

    case DLL_PROCESS_DETACH:
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
        break;
    }
    return TRUE;
}