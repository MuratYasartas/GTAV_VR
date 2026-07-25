#pragma once

// Safe logging for OVRInject.
//
// All functions are thread-safe and format through vsnprintf into bounded
// buffers. Every call emits one log line prefixed with a timestamp, the
// calling thread id and a level tag (INFO/WARN/ERR). The log file is opened
// once and kept open; it is flushed after every write.
//
// Log path resolution: GTAVR_LOG_PATH (full file path) wins, then
// GTAVR_LOG_DIR\<file>, then gtavrInjectLog.txt beside the host executable,
// then the temp directory.

void LOGSTRF(const char* format, ...);   // INFO -> log file + debug output
void LOGWNDF(const char* format, ...);   // WARN -> log file + debug output (no longer shows a message box)
void LOGOUTF(const char* format, ...);   // INFO -> log file + debug output
void LOGFATALF(const char* format, ...); // ERR  -> log file + debug output (never terminates the host process)

// Debug/diagnostic channel. Written ONLY when verbose logging is enabled via
// env GTAVR_VERBOSE=1 or gtavr_settings.ini [Debug] verbose=1 (ini searched
// in GTAVR_SETTINGS_DIR, then beside the injected module, then beside the
// host exe; first existing file wins). Use for per-decision traces (pattern
// scans, guard polls, camera resolution) that would be too noisy at INFO.
void LOGDBGF(const char* format, ...);   // DBG  -> log file + debug output (filtered)
bool LOG_IsVerbose();                    // true when the debug channel is on
const char* LOGGetPath();                // resolved log file path (never null)

inline void LOGSTR(const char* message) {
	LOGSTRF("%s", message);
}

inline void LOGDBG(const char* message) {
	if (LOG_IsVerbose()) LOGDBGF("%s", message);
}
