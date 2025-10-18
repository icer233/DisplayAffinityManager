#define _CRT_SECURE_NO_WARNINGS

#include <windows.h>
#include <tchar.h>
#include <iostream>
#include <string>
#include <vector>
#include <TlHelp32.h>
#include <Shlwapi.h>
#pragma comment(lib,"shlwapi.lib")

#define RESET   "\033[0m"
#define RED     "\033[31m"
#define GREEN   "\033[32m"
#define YELLOW  "\033[33m"
#define BLUE    "\033[34m"
#define MAGENTA "\033[35m"
#define CYAN    "\033[36m"
#define WHITE   "\033[37m"

using namespace std;

// Affinity types
enum class AffinityType {
    NONE = 0,       // WDA_NONE - Normal display
    MONITOR = 1,     // WDA_MONITOR - Protected from screen capture
    EXCLUDEFROMCAPTURE = 2 // WDA_EXCLUDEFROMCAPTURE - Exclude from screencapture
};

// Process info structure
struct ProcessInfo {
    DWORD pid;
    std::string name;
    bool hasWindows;
};

// Function to log messages
void Log(const char* format, ...) {
    char msg[512] = { 0 };
    va_list ap;
    va_start(ap, format);
    vsprintf(msg, format, ap);
    va_end(ap);
    cout << msg << endl;
}

// Enable debug privilege for accessing processes
bool SetDebugPrivilege() {
    HANDLE hToken;
    TOKEN_PRIVILEGES NewState;
    LUID luidPrivilegeLUID;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES, &hToken) ||
        !LookupPrivilegeValue(NULL, SE_DEBUG_NAME, &luidPrivilegeLUID)) {
        Log("SetDebugPrivilege Error: %d", GetLastError());
        return false;
    }

    NewState.PrivilegeCount = 1;
    NewState.Privileges[0].Luid = luidPrivilegeLUID;
    NewState.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    if (!AdjustTokenPrivileges(hToken, FALSE, &NewState, NULL, NULL, NULL)) {
        Log("AdjustTokenPrivilege Error: %d", GetLastError());
        return false;
    }

    CloseHandle(hToken);
    return true;
}

// Get full path to a file in the same directory as the executable
std::wstring GetFullFilePath(const std::wstring& filename) {
    wchar_t fullPath[MAX_PATH] = { 0 };
    GetModuleFileName(NULL, fullPath, MAX_PATH);
    PathRemoveFileSpec(fullPath);
    PathAppend(fullPath, filename.c_str());
    return std::wstring{ fullPath };
}

// Get a list of running processes
std::vector<ProcessInfo> GetProcessList() {
    std::vector<ProcessInfo> processes;

    HANDLE hProcessSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hProcessSnap == INVALID_HANDLE_VALUE) {
        Log("Failed to create process snapshot: %d", GetLastError());
        return processes;
    }

    PROCESSENTRY32 pe32;
    pe32.dwSize = sizeof(PROCESSENTRY32);  // 关键设置

    if (!Process32First(hProcessSnap, &pe32)) {
        CloseHandle(hProcessSnap);
        Log("Failed to get first process: %d", GetLastError());
        return processes;
    }

    do {
        ProcessInfo pi;
        pi.pid = pe32.th32ProcessID;

        // 转换进程名（宽字符 → 多字节）
        char exeName[MAX_PATH];
        WideCharToMultiByte(CP_ACP, 0, pe32.szExeFile, -1, exeName, MAX_PATH, NULL, NULL);
        pi.name = exeName;

        // 窗口检查逻辑
        pi.hasWindows = false;
        EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
            ProcessInfo* pPi = reinterpret_cast<ProcessInfo*>(lParam);
            DWORD pid;
            GetWindowThreadProcessId(hwnd, &pid);

            if (pid == pPi->pid && IsWindowVisible(hwnd)) {
                pPi->hasWindows = true;
                return FALSE;  // 找到窗口后立即停止枚举
            }
            return TRUE;
            }, reinterpret_cast<LPARAM>(&pi));

        processes.push_back(pi);
    } while (Process32Next(hProcessSnap, &pe32));

    CloseHandle(hProcessSnap);
    return processes;
}

// Inject DLL into a process
bool InjectDLL(DWORD pid, const std::wstring& dllPath) {
    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!hProcess) {
        Log("Failed to open process (PID %d): %d", pid, GetLastError());
        return false;
    }

    // Allocate memory for DLL path in target process
    LPVOID pDllPath = VirtualAllocEx(hProcess, NULL, (dllPath.length() + 1) * sizeof(wchar_t),
        MEM_COMMIT, PAGE_READWRITE);
    if (!pDllPath) {
        Log("Failed to allocate memory in process: %d", GetLastError());
        CloseHandle(hProcess);
        return false;
    }

    // Write DLL path to target process memory
    if (!WriteProcessMemory(hProcess, pDllPath, dllPath.c_str(),
        (dllPath.length() + 1) * sizeof(wchar_t), NULL)) {
        Log("Failed to write to process memory: %d", GetLastError());
        VirtualFreeEx(hProcess, pDllPath, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    // Get address of LoadLibraryW
    HMODULE hKernel32 = GetModuleHandle(L"kernel32.dll");
    if (!hKernel32) {
        Log("Failed to get kernel32 handle: %d", GetLastError());
        VirtualFreeEx(hProcess, pDllPath, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    LPTHREAD_START_ROUTINE pLoadLibrary = (LPTHREAD_START_ROUTINE)GetProcAddress(hKernel32, "LoadLibraryW");
    if (!pLoadLibrary) {
        Log("Failed to get LoadLibraryW address: %d", GetLastError());
        VirtualFreeEx(hProcess, pDllPath, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    // Create remote thread to load DLL
    HANDLE hThread = CreateRemoteThread(hProcess, NULL, 0, pLoadLibrary, pDllPath, 0, NULL);
    if (!hThread) {
        Log("Failed to create remote thread: %d", GetLastError());
        VirtualFreeEx(hProcess, pDllPath, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    // Wait for thread to finish
    WaitForSingleObject(hThread, INFINITE);

    // Clean up
    VirtualFreeEx(hProcess, pDllPath, 0, MEM_RELEASE);
    CloseHandle(hThread);
    CloseHandle(hProcess);

    return true;
}

// Main menu
void ShowMainMenu() {
    system("cls");
    cout << GREEN << "    ____ _________ ____  ____  __________ " << endl;
    cout << "    |_ _/ ___| ____|  _ \\|___ \\|___ /___ / " << endl;
    cout << "     | | |   |  _| | |_) | __) | |_ \\ |_ \\" << endl;
    cout << "     | | |___| |___|  _ < / __/ ___) |__) |" << endl;
    cout << "    |___\\____|_____|_| \\_\\_____|____/____/" << endl << endl;
    cout << RESET << "======== Window Display Affinity Manager ========" << endl;
    cout << YELLOW << "1. List all processes with windows" << endl;
    cout << CYAN << "2. Set to WDA_NONE (normal display)" << endl;
    cout << "3. Set to WDA_MONITOR (protected from capture)" << endl;
    cout << "4. Set to WDA_EXCLUDEFROMCAPTURE (exluded from capture)" << endl;
    cout << YELLOW << "5. Get current display affinity status" << endl;
    cout << RED << "0. Exit" << endl;
    cout << RESET << "============= ICER233 @ LCG-52POJIE =============" << endl << endl;
    cout << "Selection: ";
}

int main() {
    MessageBoxA(NULL, "This software is completely FREE\nhttps://github.com/icer233/DisplayAffinityManager", "DisplayAffinityManager", MB_OK);

    // Enable debug privileges for accessing all processes
    if (!SetDebugPrivilege()) {
        Log("Failed to set debug privilege. Some processes might not be accessible.");
    }

    // Check if DLLs exist
    std::wstring hideDllPath = GetFullFilePath(L"AffinityHide.dll");
    std::wstring unhideDllPath = GetFullFilePath(L"AffinityUnhide.dll");
    std::wstring statusDllPath = GetFullFilePath(L"AffinityStatus.dll");
    std::wstring transDllPath = GetFullFilePath(L"AffinityTrans.dll");
    std::wstring hideDll32Path = GetFullFilePath(L"AffinityHide32.dll");
    std::wstring unhideDll32Path = GetFullFilePath(L"AffinityUnhide32.dll");
    std::wstring statusDll32Path = GetFullFilePath(L"AffinityStatus32.dll");
    std::wstring transDll32Path = GetFullFilePath(L"AffinityTrans32.dll");

    if (GetFileAttributes(hideDllPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        Log("Error: AffinityHide.dll not found in the application directory.");
        system("pause");
        //return 1;
    }

    if (GetFileAttributes(unhideDllPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        Log("Error: AffinityUnhide.dll not found in the application directory.");
        system("pause");
        //return 1;
    }
    if (GetFileAttributes(transDllPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        Log("Error: AffinityTrans.dll not found in the application directory.");
        system("pause");
        //return 1;
    }

    if (GetFileAttributes(statusDllPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        Log("Error: AffinityStatus.dll not found in the application directory.");
        system("pause");
        //return 1;
    }
    if (GetFileAttributes(hideDll32Path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        Log("Error: AffinityHide32.dll not found in the application directory.");
        system("pause");
        //return 1;
    }

    if (GetFileAttributes(unhideDll32Path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        Log("Error: AffinityUnhide32.dll not found in the application directory.");
        system("pause");
        //return 1;
    }
    if (GetFileAttributes(transDll32Path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        Log("Error: AffinityTrans32.dll not found in the application directory.");
        system("pause");
        //return 1;
    }

    if (GetFileAttributes(statusDll32Path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        Log("Error: AffinityStatus32.dll not found in the application directory.");
        system("pause");
        //return 1;
    }
    int choice = -1;
    while (choice != 0) {
        ShowMainMenu();
        cin >> choice;

        if (choice == 1) {
            // List processes
            system("cls");
            cout << "===== Processes with Windows =====" << endl;
            cout << "ID\tPID\tProcess Name" << endl;

            auto processes = GetProcessList();
            int index = 0;
            for (const auto& proc : processes) {
                if (proc.hasWindows) {
                    cout << index << "\t" << proc.pid << "\t" << proc.name << endl;
                    index++;
                }
            }

            cout << "=================================" << endl;
            system("pause");
        }
        else if (choice == 2 || choice == 3 || choice == 4 || choice == 5) {
            system("cls");
            cout << "===== Processes with Windows =====" << endl;
            cout << "ID\tPID\tProcess Name" << endl;

            auto processes = GetProcessList();
            vector<ProcessInfo> windowProcesses;
            int index = 0;

            for (const auto& proc : processes) {
                if (proc.hasWindows) {
                    cout << index << "\t" << proc.pid << "\t" << proc.name << endl;
                    windowProcesses.push_back(proc);
                    index++;
                }
            }

            cout << "=================================" << endl;
            cout << "Enter process ID number (0-" << (index - 1) << "): ";
            int procIndex;
            cin >> procIndex;

            if (procIndex >= 0 && procIndex < windowProcesses.size()) {
                ProcessInfo selectedProc = windowProcesses[procIndex];

                if (choice == 2) {
                    // Set WDA_NONE
                    Log("Setting display affinity to WDA_NONE for %s (PID: %d)...",
                        selectedProc.name.c_str(), selectedProc.pid);
                    if (InjectDLL(selectedProc.pid, unhideDllPath) && InjectDLL(selectedProc.pid, unhideDll32Path)) {
                        Log("Successfully set display affinity to normal mode.");
                    }
                    else {
                        Log("Failed to set display affinity.");
                    }
                }
                else if (choice == 3) {
                    // Set WDA_MONITOR
                    Log("Setting display affinity to WDA_MONITOR for %s (PID: %d)...",
                        selectedProc.name.c_str(), selectedProc.pid);
                    if (InjectDLL(selectedProc.pid, hideDllPath) && InjectDLL(selectedProc.pid, hideDll32Path)) {
                        Log("Successfully set display affinity to protected mode.");
                    }
                    else {
                        Log("Failed to set display affinity.");
                    }
                }
                else if (choice == 4) {
                    // Set WDA_EXCLUDEFROMCAPTURE
                    Log("Setting display affinity to WDA_EXCLUDEFROMCAPTURE for %s (PID: %d)...",
                        selectedProc.name.c_str(), selectedProc.pid);
                    if (InjectDLL(selectedProc.pid, transDllPath) && InjectDLL(selectedProc.pid, transDll32Path)) {
                        Log("Successfully set display affinity to excluded mode.");
                    }
                    else {
                        Log("Failed to set display affinity.");
                    }
                }
                else if (choice == 5) {
                    // Get status
                    Log("Getting display affinity status for %s (PID: %d)...",
                        selectedProc.name.c_str(), selectedProc.pid);
                    if (InjectDLL(selectedProc.pid, statusDllPath) && InjectDLL(selectedProc.pid, statusDll32Path)) {
                        Log("Status check completed. Check the MessageBox to see results.");
                    }
                    else {
                        Log("Failed to check display affinity status.");
                    }
                }
            }
            else {
                Log("Invalid process ID selected.");
            }

            system("pause");
        }
    }

    return 0;
}