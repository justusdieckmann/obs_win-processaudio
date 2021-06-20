#undef UNICODE

#include <vector>
#include <string>
#include <windows.h>
#include <Tlhelp32.h>
#include <iostream>
#include "obsconfig.h"
#include "injector.hpp"

#include <obs-module.h>

using std::vector;
using std::string;

static inline bool is_64bit_windows()
{
#ifdef _WIN64
    return true;
#else
    BOOL x86 = false;
	bool success = !!IsWow64Process(GetCurrentProcess(), &x86);
	return success && !!x86;
#endif
}

static inline bool is_64bit_process(HANDLE process)
{
    BOOL x86 = true;
    if (is_64bit_windows()) {
        bool success = !!IsWow64Process(process, &x86);
        if (!success) {
            return false;
        }
    }

    return !x86;
}

static void getHookPath(char* path, int maxlength, HANDLE handle) {
    if (is_64bit_process(handle)) {
        _fullpath(path, OBS_DATA_PATH "/obs-plugins/win-processaudio/audio-hook64.dll", maxlength);
    } else {
        _fullpath(path, OBS_DATA_PATH "/obs-plugins/win-processaudio/audio-hook32.dll", maxlength);
    }
}

static void getInjectHelperPath(char* path, int maxlength, HANDLE handle) {
    if (is_64bit_process(handle)) {
        _fullpath(path, OBS_DATA_PATH "/obs-plugins/win-capture/inject-helper64.exe", maxlength);
    } else {
        _fullpath(path, OBS_DATA_PATH "/obs-plugins/win-capture/inject-helper32.exe", maxlength);
    }
}

DWORD findProcess(const char* processname) {
    PROCESSENTRY32 entry;
    entry.dwSize = sizeof(PROCESSENTRY32);
    HANDLE hTool32 = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, NULL);
    BOOL bProcess = Process32First(hTool32, &entry);
    if(bProcess == TRUE)
    {
        while((Process32Next(hTool32, &entry)) == TRUE)
        {
            if(strcmp(entry.szExeFile, processname) == 0)
            {
                CloseHandle(hTool32);
                return entry.th32ProcessID;
            }
        }
    }
    CloseHandle(hTool32);
    return 0;
}

void injectProcessDirect(DWORD procID, HANDLE handle) {
    char fullpath[MAX_PATH];
    getHookPath(fullpath, MAX_PATH, handle);

    blog(LOG_INFO, "Inject Direct: %s", fullpath);

    LPVOID LoadLibraryAddr = (LPVOID)GetProcAddress(GetModuleHandle("kernel32.dll"),
                                                    "LoadLibraryA");
    if (LoadLibraryAddr == NULL) {
        blog(LOG_INFO, "GetProcAddress: %lu", GetLastError());
    }
    LPVOID LLParam = (LPVOID)VirtualAllocEx(handle, NULL, strlen(fullpath),
                                            MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (LoadLibraryAddr == NULL) {
        blog(LOG_INFO, "VirtualAllocEx: %lu", GetLastError());
    }

    if (!WriteProcessMemory(handle, LLParam, fullpath, strlen(fullpath), NULL)) {
        blog(LOG_INFO, "WriteProcessMemory: %lu", GetLastError());
    }

    if (!CreateRemoteThread(handle, NULL, NULL, (LPTHREAD_START_ROUTINE)LoadLibraryAddr,
                       LLParam, NULL, NULL)) {
        blog(LOG_INFO, "CreateRemoteThread: %lu", GetLastError());
    }
}

static inline bool createInjectHelper(DWORD procID, HANDLE handle)
{
    char *command_line = (char*) malloc(4096 * sizeof(char));
    char inject_path[MAX_PATH];
    char hook_dll[MAX_PATH];

    getHookPath(hook_dll, MAX_PATH, handle);
    getInjectHelperPath(inject_path, MAX_PATH, handle);
    blog(LOG_INFO, "With Helper: %s, %s", inject_path, hook_dll);

    PROCESS_INFORMATION pi = {0};
    STARTUPINFO si = {0};
    bool success = false;

    si.cb = sizeof(si);

    sprintf_s(command_line, 4096, R"("%s" "%s" %lu %lu)", inject_path,
             hook_dll, 0, procID);

    success = !!CreateProcessA(inject_path, command_line, NULL, NULL,
                               false, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    if (success) {
        CloseHandle(pi.hThread);
    } else {
        blog(LOG_WARNING, "Failed to create inject helper process: %lu",
             GetLastError());
    }

    free(command_line);
    return success;
}

bool inject_hook(DWORD procID, HANDLE* handle)
{
    bool success = false;

    *handle = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION |
                          PROCESS_VM_WRITE, FALSE, procID);

    bool matching_architecture;

#ifdef _WIN64
    matching_architecture = is_64bit_process(*handle);
#else
    matching_architecture = !is_64bit_process(*handle);
#endif

    if (matching_architecture) {
        injectProcessDirect(procID, *handle);
    } else {
        createInjectHelper(procID, *handle);
    }
    return success;
}