#include "targetver.h"

#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <mutex>

#include "Log.hpp"

#include <Windows.h>

// Same module-handle idiom as VRManager.cpp: &__ImageBase is the HMODULE of
// the binary this code is linked into (the injected OVRInject.dll), so config
// can be found beside the DLL rather than only beside the host exe.
EXTERN_C IMAGE_DOS_HEADER __ImageBase;

namespace {

constexpr char kLogFileName[] = "gtavrInjectLog.txt";

enum LogLevel {
	LOG_LEVEL_DEBUG,
	LOG_LEVEL_INFO,
	LOG_LEVEL_WARN,
	LOG_LEVEL_ERR,
};

const char* LevelTag(LogLevel level) {
	switch (level) {
	case LOG_LEVEL_DEBUG: return "DBG";
	case LOG_LEVEL_WARN: return "WARN";
	case LOG_LEVEL_ERR:  return "ERR";
	default:             return "INFO";
	}
}

// Verbose (debug-channel) flag, evaluated once. On when env GTAVR_VERBOSE is
// set to anything but "0"/empty, or when gtavr_settings.ini contains
// [Debug] verbose=1. Ini search order: GTAVR_SETTINGS_DIR (when set), then
// beside THIS module (the injected DLL - the control panel writes the ini
// there before injecting into an already-running game, whose environment we
// cannot change), then beside the host exe. The first file that exists is
// authoritative: an existing ini without [Debug] verbose means verbose OFF,
// lower-priority copies are not consulted.
// NOTE: the Windows INI API silently fails on LF-only files, so the INI is
// scanned manually.

// Builds "<dir of module>\gtavr_settings.ini" into out. `module` may be null
// (host exe) or &__ImageBase (this binary). Returns false when the path
// could not be resolved.
bool ModuleDirIniPath(HMODULE module, char* out, size_t outSize) {
	char modulePath[MAX_PATH] = {};
	DWORD mlen = GetModuleFileNameA(module, modulePath, MAX_PATH);
	if (mlen == 0 || mlen >= MAX_PATH) return false;
	char* lastSlash = strrchr(modulePath, '\\');
	if (!lastSlash) return false;
	*(lastSlash + 1) = '\0';
	snprintf(out, outSize, "%sgtavr_settings.ini", modulePath);
	return true;
}

// Parses [Debug] verbose= from one ini. Returns true when the file could be
// opened (making it authoritative); sets `verbose` to the parsed flag.
bool ParseVerboseIni(const char* iniPath, bool* verbose) {
	*verbose = false;
	FILE* fp = nullptr;
	if (fopen_s(&fp, iniPath, "rb") != 0 || !fp) return false;
	bool inDebug = false;
	char line[512];
	while (fgets(line, sizeof(line), fp)) {
		char* p = line;
		while (*p == ' ' || *p == '\t') ++p;
		if (_strnicmp(p, "[Debug]", 7) == 0) { inDebug = true; continue; }
		if (p[0] == '[') { inDebug = false; continue; }
		if (inDebug && _strnicmp(p, "verbose", 7) == 0) {
			char* eq = strchr(p, '=');
			if (eq) {
				while (*++eq == ' ') {}
				*verbose = (*eq == '1' || *eq == 't' || *eq == 'T' || *eq == 'y' || *eq == 'Y');
			}
		}
	}
	fclose(fp);
	return true;
}

bool ComputeVerbose() {
	char envVal[16] = {};
	DWORD len = GetEnvironmentVariableA("GTAVR_VERBOSE", envVal, sizeof(envVal));
	if (len > 0 && len < sizeof(envVal) && !(envVal[0] == '0' && envVal[1] == '\0')) {
		return true;
	}

	char candidates[3][MAX_PATH] = {};
	int count = 0;

	len = GetEnvironmentVariableA("GTAVR_SETTINGS_DIR", candidates[count], MAX_PATH);
	if (len > 0 && len < MAX_PATH) {
		strncat_s(candidates[count], sizeof(candidates[0]), "\\gtavr_settings.ini", _TRUNCATE);
		++count;
	}
	// Beside the injected DLL: the only location the control panel can write
	// that an already-running game will actually read back.
	if (ModuleDirIniPath(reinterpret_cast<HMODULE>(&__ImageBase), candidates[count], sizeof(candidates[0]))) {
		++count;
	}
	// Legacy location beside the host exe.
	if (ModuleDirIniPath(nullptr, candidates[count], sizeof(candidates[0]))) {
		++count;
	}

	for (int i = 0; i < count; ++i) {
		bool verbose = false;
		if (ParseVerboseIni(candidates[i], &verbose)) {
			return verbose;
		}
	}
	return false;
}

bool IsVerbose() {
	static const bool verbose = ComputeVerbose();
	return verbose;
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

	// _fsopen with _SH_DENYNO: support tooling (and the user, mid-session)
	// must be able to read the log while we hold it open.
	FILE* fp = _fsopen(logPath, "a", _SH_DENYNO);
	if (fp) {
		return fp;
	}

	// Fallback: temp directory
	char tempPath[MAX_PATH] = {};
	DWORD tempLen = GetTempPathA(static_cast<DWORD>(sizeof(tempPath)), tempPath);
	if (tempLen > 0 && tempLen < sizeof(tempPath)) {
		strncat_s(tempPath, sizeof(tempPath), kLogFileName, _TRUNCATE);
		fp = _fsopen(tempPath, "a", _SH_DENYNO);
		if (fp) {
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

void LOGDBGF(const char* format, ...)
{
	if (!IsVerbose()) return;
	va_list args;
	va_start(args, format);
	LogWrite(LOG_LEVEL_DEBUG, format, args);
	va_end(args);
}

bool LOG_IsVerbose() {
	return IsVerbose();
}

const char* LOGGetPath() {
	static char path[MAX_PATH] = {};
	if (path[0] == '\0') {
		GetLogPath(path, sizeof(path), kLogFileName);
	}
	return path;
}
