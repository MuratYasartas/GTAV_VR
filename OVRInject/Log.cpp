#include "targetver.h"

#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <mutex>

#include "Log.hpp"

#include <Windows.h>

namespace {

constexpr char kLogFileName[] = "gtavrInjectLog.txt";

enum LogLevel {
	LOG_LEVEL_INFO,
	LOG_LEVEL_WARN,
	LOG_LEVEL_ERR,
};

const char* LevelTag(LogLevel level) {
	switch (level) {
	case LOG_LEVEL_WARN: return "WARN";
	case LOG_LEVEL_ERR:  return "ERR";
	default:             return "INFO";
	}
}

void GetLogPath(char* outPath, size_t outSize, const char* fileName) {
	if (!outPath || outSize == 0 || !fileName) return;
	outPath[0] = '\0';

	char envPath[MAX_PATH] = {};
	DWORD envLen = GetEnvironmentVariableA("GTAVR_LOG_PATH", envPath, MAX_PATH);
	if (envLen > 0 && envLen < MAX_PATH) {
		strncpy_s(outPath, outSize, envPath, _TRUNCATE);
		return;
	}

	envLen = GetEnvironmentVariableA("GTAVR_LOG_DIR", envPath, MAX_PATH);
	if (envLen > 0 && envLen < MAX_PATH) {
		snprintf(outPath, outSize, "%s\\%s", envPath, fileName);
		return;
	}

	char modulePath[MAX_PATH] = {};
	DWORD len = GetModuleFileNameA(nullptr, modulePath, MAX_PATH);
	if (len > 0 && len < MAX_PATH) {
		char* lastSlash = strrchr(modulePath, '\\');
		if (lastSlash) {
			*(lastSlash + 1) = '\0';
			snprintf(outPath, outSize, "%s%s", modulePath, fileName);
			return;
		}
	}

	DWORD tempLen = GetTempPathA(static_cast<DWORD>(outSize), outPath);
	if (tempLen > 0 && tempLen < outSize) {
		strncat_s(outPath, outSize, fileName, _TRUNCATE);
		return;
	}

	strncpy_s(outPath, outSize, fileName, _TRUNCATE);
}

FILE* OpenLogFile() {
	char logPath[MAX_PATH] = {};
	GetLogPath(logPath, sizeof(logPath), kLogFileName);

	FILE* fp = nullptr;
	if (fopen_s(&fp, logPath, "a") == 0 && fp) {
		return fp;
	}

	// Fallback: temp directory
	char tempPath[MAX_PATH] = {};
	DWORD tempLen = GetTempPathA(static_cast<DWORD>(sizeof(tempPath)), tempPath);
	if (tempLen > 0 && tempLen < sizeof(tempPath)) {
		strncat_s(tempPath, sizeof(tempPath), kLogFileName, _TRUNCATE);
		if (fopen_s(&fp, tempPath, "a") == 0 && fp) {
			return fp;
		}
	}

	return nullptr;
}

// Opened once on first use and kept open for the process lifetime; every
// write is flushed immediately so nothing is lost on a crash.
FILE* GetLogFile() {
	static FILE* logFile = OpenLogFile();
	return logFile;
}

std::mutex& GetLogMutex() {
	static std::mutex logMutex;
	return logMutex;
}

void LogWrite(LogLevel level, const char* format, va_list args) {
	if (!format) return;

	char message[4096] = {};
	vsnprintf(message, sizeof(message) - 1, format, args);
	message[sizeof(message) - 1] = '\0';

	SYSTEMTIME st = {};
	GetLocalTime(&st);

	char line[4352] = {};
	snprintf(line, sizeof(line) - 1,
		"[%04u-%02u-%02u %02u:%02u:%02u.%03u] [tid %5lu] [%s] %s",
		st.wYear, st.wMonth, st.wDay,
		st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
		static_cast<unsigned long>(GetCurrentThreadId()),
		LevelTag(level), message);
	line[sizeof(line) - 1] = '\0';

	// Keep one line per call so the next prefix starts on its own line.
	size_t lineLen = strlen(line);
	if (lineLen == 0 || line[lineLen - 1] != '\n') {
		if (lineLen < sizeof(line) - 2) {
			line[lineLen] = '\n';
			line[lineLen + 1] = '\0';
		}
	}

	std::lock_guard<std::mutex> lock(GetLogMutex());

	FILE* fp = GetLogFile();
	if (fp) {
		fputs(line, fp);
		fflush(fp);
	}

	OutputDebugStringA(line);
}

} // namespace

void LOGSTRF(const char* format, ...)
{
	va_list args;
	va_start(args, format);
	LogWrite(LOG_LEVEL_INFO, format, args);
	va_end(args);
}

void LOGWNDF(const char* format, ...)
{
	va_list args;
	va_start(args, format);
	LogWrite(LOG_LEVEL_WARN, format, args);
	va_end(args);
}

void LOGOUTF(const char* format, ...)
{
	va_list args;
	va_start(args, format);
	LogWrite(LOG_LEVEL_INFO, format, args);
	va_end(args);
}

void LOGFATALF(const char* format, ...)
{
	// Never terminate the host game: log at ERR level and return.
	va_list args;
	va_start(args, format);
	LogWrite(LOG_LEVEL_ERR, format, args);
	va_end(args);
}
