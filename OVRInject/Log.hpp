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

inline void LOGSTR(const char* message) {
	LOGSTRF("%s", message);
}
