#pragma once

#include <Windows.h>

namespace OVRInject {
namespace CrashDump {

/**
 * CrashDump - best-effort minidump writer for crashes inside OVRInject.dll.
 *
 * Install() registers a vectored exception handler. When a fatal exception
 * (access violation, illegal instruction, stack overflow, heap corruption,
 * ...) originates inside this module's address range, the handler writes a
 * minidump next to the log (GTAVR_LOG_DIR, else beside the host executable,
 * else the temp directory), logs the crash, and chains on to the previous
 * handlers by returning EXCEPTION_CONTINUE_SEARCH. It never terminates the
 * host process itself. Only the first crash is dumped.
 *
 * Registered from DllMain(DLL_PROCESS_ATTACH) and removed on detach.
 */
void Install(HMODULE module);
void Uninstall();

} // namespace CrashDump
} // namespace OVRInject
