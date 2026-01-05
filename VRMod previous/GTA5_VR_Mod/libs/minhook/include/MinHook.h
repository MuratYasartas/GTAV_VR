// MinHook - The Minimalistic x86/x64 API Hooking Library
// Placeholder header for compilation
// Download actual library from https://github.com/TsudaKageyu/minhook

#ifndef MINHOOK_H
#define MINHOOK_H

#include <windows.h>

// MinHook Error Codes
typedef enum MH_STATUS {
    MH_UNKNOWN = -1,
    MH_OK = 0,
    MH_ERROR_ALREADY_INITIALIZED,
    MH_ERROR_NOT_INITIALIZED,
    MH_ERROR_ALREADY_CREATED,
    MH_ERROR_NOT_CREATED,
    MH_ERROR_ENABLED,
    MH_ERROR_DISABLED,
    MH_ERROR_NOT_EXECUTABLE,
    MH_ERROR_UNSUPPORTED_FUNCTION,
    MH_ERROR_MEMORY_ALLOC,
    MH_ERROR_MEMORY_PROTECT,
    MH_ERROR_MODULE_NOT_FOUND,
    MH_ERROR_FUNCTION_NOT_FOUND
} MH_STATUS;

// Special target values
#define MH_ALL_HOOKS NULL

#ifdef __cplusplus
extern "C" {
#endif

// Initialize the MinHook library
// Must be called before any other function
MH_STATUS WINAPI MH_Initialize(void);

// Uninitialize the MinHook library
// Must be called after all hooks are removed
MH_STATUS WINAPI MH_Uninitialize(void);

// Creates a hook for the specified target function
// pTarget    [in]  Address of the target function
// pDetour    [in]  Address of the detour function
// ppOriginal [out] Address of the trampoline function (optional)
MH_STATUS WINAPI MH_CreateHook(LPVOID pTarget, LPVOID pDetour, LPVOID* ppOriginal);

// Creates a hook for the specified API function
// pszModule  [in]  Module name containing the target function
// pszTarget  [in]  Target function name
// pDetour    [in]  Address of the detour function
// ppOriginal [out] Address of the trampoline function (optional)
MH_STATUS WINAPI MH_CreateHookApi(
    LPCWSTR pszModule, LPCSTR pszProcName,
    LPVOID pDetour, LPVOID* ppOriginal);

// Creates a hook for the specified API function (ANSI version)
MH_STATUS WINAPI MH_CreateHookApiEx(
    LPCWSTR pszModule, LPCSTR pszProcName,
    LPVOID pDetour, LPVOID* ppOriginal, LPVOID* ppTarget);

// Removes an already created hook
MH_STATUS WINAPI MH_RemoveHook(LPVOID pTarget);

// Enables an already created hook
MH_STATUS WINAPI MH_EnableHook(LPVOID pTarget);

// Disables an already created hook
MH_STATUS WINAPI MH_DisableHook(LPVOID pTarget);

// Queues to enable an already created hook
MH_STATUS WINAPI MH_QueueEnableHook(LPVOID pTarget);

// Queues to disable an already created hook
MH_STATUS WINAPI MH_QueueDisableHook(LPVOID pTarget);

// Applies all queued changes
MH_STATUS WINAPI MH_ApplyQueued(void);

// Gets the status string for the given status code
const char* WINAPI MH_StatusToString(MH_STATUS status);

#ifdef __cplusplus
}
#endif

#endif // MINHOOK_H
