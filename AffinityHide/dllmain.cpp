// AffinityHide.cpp - DLL to set WDA_MONITOR
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

// Apply WDA_MONITOR to all windows in current process
void ApplyMonitorAffinity() {
    std::string procName = GetProcessName();
    DebugLog("====== Setting WDA_MONITOR for %s ======", procName.c_str());

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
            if (SetWindowDisplayAffinity(hwnd, WDA_MONITOR)) {
                DebugLog("[SUCCESS] Window: %s - Set to WDA_MONITOR", (title[0] ? title : "<No Title>"));
                windowCount++;
            }
            else {
                DebugLog("[FAILED] Window: %s - Error: %d", (title[0] ? title : "<No Title>"), GetLastError());
            }
        }
    }

    DebugLog("====== Modified %d windows for %s ======", windowCount, procName.c_str());
}

DWORD WINAPI UnloadSelf(LPVOID param) {
    Sleep(10);
    FreeLibraryAndExitThread((HMODULE)param, 0);
    return 0;
}


BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved) {
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        // Apply affinity when DLL is loaded
        ApplyMonitorAffinity();

        // Detach DLL when finished the work
        DWORD threadId;
        CreateThread(NULL, 0, UnloadSelf, hModule, 0, &threadId);
        break;

    case DLL_PROCESS_DETACH:
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
        break;
    }
    return TRUE;
}