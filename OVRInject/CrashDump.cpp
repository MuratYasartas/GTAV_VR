#include "targetver.h"

#include "CrashDump.hpp"

#include <Windows.h>
#include <dbghelp.h>

#include <atomic>
#include <cstdio>
#include <cstring>

#include "Log.hpp"

namespace OVRInject {
namespace CrashDump {
namespace {

HMODULE g_module = nullptr;
ULONG_PTR g_moduleBase = 0;
ULONG_PTR g_moduleSize = 0;
PVOID g_handler = nullptr;
std::atomic<bool> g_dumped{false};

// Only genuinely fatal exceptions justify a dump; breakpoints and other
// diagnostic exceptions are chained through untouched.
bool IsFatalCode(DWORD code) {
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:
    case EXCEPTION_IN_PAGE_ERROR:
    case EXCEPTION_ILLEGAL_INSTRUCTION:
    case EXCEPTION_PRIV_INSTRUCTION:
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
    case EXCEPTION_INT_OVERFLOW:
    case EXCEPTION_STACK_OVERFLOW:
    case 0xC0000374L: // STATUS_HEAP_CORRUPTION
    case 0xC0000409L: // STATUS_STACK_BUFFER_OVERRUN / fail-fast
        return true;
    default:
        return false;
    }
}

bool IsInOurModule(const void* address) {
    ULONG_PTR value = reinterpret_cast<ULONG_PTR>(address);
    return g_moduleBase != 0 && value >= g_moduleBase && value < g_moduleBase + g_moduleSize;
}

// Mirror Log.cpp's path resolution: GTAVR_LOG_DIR, else beside the host
// executable, else the temp directory.
void GetDumpDirectory(char* outPath, size_t outSize) {
    if (!outPath || outSize == 0) {
        return;
    }
    outPath[0] = '\0';

    DWORD len = GetEnvironmentVariableA("GTAVR_LOG_DIR", outPath, static_cast<DWORD>(outSize));
    if (len > 0 && len < outSize) {
        return;
    }

    char modulePath[MAX_PATH] = {};
    len = GetModuleFileNameA(nullptr, modulePath, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        char* lastSlash = strrchr(modulePath, '\\');
        if (lastSlash) {
            *(lastSlash + 1) = '\0';
            strncpy_s(outPath, outSize, modulePath, _TRUNCATE);
            return;
        }
    }

    GetTempPathA(static_cast<DWORD>(outSize), outPath);
}

void WriteDump(EXCEPTION_POINTERS* exceptionInfo) {
    char dir[MAX_PATH] = {};
    GetDumpDirectory(dir, sizeof(dir));
    if (dir[0] == '\0') {
        return;
    }

    SYSTEMTIME st = {};
    GetLocalTime(&st);

    char path[MAX_PATH] = {};
    snprintf(path, sizeof(path) - 1, "%sgtavr_crash_%04u%02u%02u_%02u%02u%02u_pid%lu.dmp",
             dir, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
             static_cast<unsigned long>(GetCurrentProcessId()));
    path[sizeof(path) - 1] = '\0';

    HANDLE file = CreateFileA(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }

    MINIDUMP_EXCEPTION_INFORMATION dumpInfo = {};
    dumpInfo.ThreadId = GetCurrentThreadId();
    dumpInfo.ExceptionPointers = exceptionInfo;
    dumpInfo.ClientPointers = FALSE;

    // Best effort: MiniDumpWriteDump is not strictly crash-safe, but it is
    // the standard tool for this and failures here only lose the dump.
    BOOL ok = MiniDumpWriteDump(GetCurrentProcess(),
                                GetCurrentProcessId(),
                                file,
                                MiniDumpNormal,
                                &dumpInfo,
                                nullptr,
                                nullptr);
    CloseHandle(file);

    // The log mutex could in theory be held by a crashed thread; accepting
    // that small deadlock risk so the crash is at least visible in the log.
    if (ok) {
        LOGSTRF("CrashDump: wrote minidump to %s\n", path);
    } else {
        LOGSTRF("CrashDump: MiniDumpWriteDump failed (gle=%lu)\n", static_cast<unsigned long>(GetLastError()));
    }
}

LONG CALLBACK VectoredHandler(PEXCEPTION_POINTERS exceptionInfo) {
    if (!exceptionInfo || !exceptionInfo->ExceptionRecord) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    DWORD code = exceptionInfo->ExceptionRecord->ExceptionCode;
    PVOID address = exceptionInfo->ExceptionRecord->ExceptionAddress;

    if (!IsFatalCode(code) || !IsInOurModule(address)) {
        return EXCEPTION_CONTINUE_SEARCH; // chain to previous handlers
    }

    // First crash only; a nested crash while dumping must not recurse.
    if (g_dumped.exchange(true)) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    LOGSTRF("CrashDump: fatal exception 0x%08lx at %p inside OVRInject.dll\n",
            static_cast<unsigned long>(code), address);
    WriteDump(exceptionInfo);

    return EXCEPTION_CONTINUE_SEARCH; // chain to previous handlers
}

} // namespace

void Install(HMODULE module) {
    if (g_handler || !module) {
        return;
    }
    g_module = module;
    g_moduleBase = reinterpret_cast<ULONG_PTR>(module);

    // Module size from the PE header (no psapi dependency).
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(module);
    if (dos->e_magic == IMAGE_DOS_SIGNATURE) {
        auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(g_moduleBase + dos->e_lfanew);
        if (nt->Signature == IMAGE_NT_SIGNATURE) {
            g_moduleSize = nt->OptionalHeader.SizeOfImage;
        }
    }

    // first = 0: append politely; we always chain on after dumping.
    g_handler = AddVectoredExceptionHandler(0, VectoredHandler);
    if (g_handler) {
        LOGSTRF("CrashDump: vectored exception handler installed (module %p, size 0x%Ix)\n",
                module, g_moduleSize);
    }
}

void Uninstall() {
    if (g_handler) {
        RemoveVectoredExceptionHandler(g_handler);
        g_handler = nullptr;
    }
}

} // namespace CrashDump
} // namespace OVRInject
