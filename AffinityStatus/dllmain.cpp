// AffinityStatus.cpp - DLL to check current display affinity
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

// Get affinity status string
const char* GetAffinityString(DWORD affinity) {
    switch (affinity) {
    case WDA_NONE:
        return "WDA_NONE (Normal display)";
    case WDA_MONITOR:
        return "WDA_MONITOR (Protected from capture)";
    default:
        return "Unknown";
    }
}

void DebugLog(const char* format, const char* windowTitle, const char* affinityStatus) {
    char buffer[512];
    snprintf(buffer, sizeof(buffer), format, windowTitle, affinityStatus);
    MessageBoxA(NULL, buffer, "Debug Info", MB_OK | MB_ICONINFORMATION);
}

// Check affinity of all windows in current process
void CheckAffinity() {
    std::string procName = GetProcessName();
    DebugLog("====== Checking Display Affinity for %s ======", procName.c_str());

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

            // Get display affinity
            DWORD affinity = 0;
            if (GetWindowDisplayAffinity(hwnd, &affinity)) {
                DebugLog("Window: %s\nStatus: %s",
                    (title[0] ? title : "<No Title>"),
                    GetAffinityString(affinity));
                windowCount++;
            }
            else {
                DebugLog("Window: %s - Unable to get status (Error: %d)",
                    (title[0] ? title : "<No Title>"),
                    GetLastError());
            }
        }
    }

    DebugLog("====== Checked %d windows for %s ======", windowCount, procName.c_str());
}

DWORD WINAPI UnloadSelf(LPVOID param) {
    Sleep(10);
    FreeLibraryAndExitThread((HMODULE)param, 0);
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved) {
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        // Check affinity when DLL is loaded
        CheckAffinity();

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