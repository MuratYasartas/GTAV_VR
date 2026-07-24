#include <cstdio>
#include <stdarg.h>
#include <cstring>

#include "Log.hpp"

#include <Windows.h>

#pragma warning(push, 0)

void LOGSTRF(const char* format, ...)
{
  va_list args;
  va_start(args, format);

  char logPath[MAX_PATH] = {};
  logPath[0] = '\0';

  char envPath[MAX_PATH] = {};
  DWORD envLen = GetEnvironmentVariableA("GTAVR_LOG_PATH", envPath, MAX_PATH);
  if (envLen > 0 && envLen < MAX_PATH) {
    strncpy_s(logPath, sizeof(logPath), envPath, _TRUNCATE);
  } else {
    envLen = GetEnvironmentVariableA("GTAVR_LOG_DIR", envPath, MAX_PATH);
    if (envLen > 0 && envLen < MAX_PATH) {
      snprintf(logPath, sizeof(logPath), "%s\\%s", envPath, "gtavrInjectShimLog.txt");
    }
  }

  if (logPath[0] == '\0') {
    char modulePath[MAX_PATH] = {};
    DWORD len = GetModuleFileNameA(nullptr, modulePath, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
      char* lastSlash = strrchr(modulePath, '\\');
      if (lastSlash) {
        *(lastSlash + 1) = '\0';
        snprintf(logPath, sizeof(logPath), "%sgtavrInjectShimLog.txt", modulePath);
      }
    }
  }

  if (logPath[0] == '\0') {
    DWORD tempLen = GetTempPathA(static_cast<DWORD>(sizeof(logPath)), logPath);
    if (tempLen > 0 && tempLen < sizeof(logPath)) {
      strncat_s(logPath, sizeof(logPath), "gtavrInjectShimLog.txt", _TRUNCATE);
    } else {
      strncpy_s(logPath, sizeof(logPath), "gtavrInjectShimLog.txt", _TRUNCATE);
    }
  }

  FILE* fp = fopen(logPath, "a");
  if (!fp) {
    char tempPath[MAX_PATH] = {};
    DWORD tempLen = GetTempPathA(static_cast<DWORD>(sizeof(tempPath)), tempPath);
    if (tempLen > 0 && tempLen < sizeof(tempPath)) {
      strncat_s(tempPath, sizeof(tempPath), "gtavrInjectShimLog.txt", _TRUNCATE);
      fp = fopen(tempPath, "a");
    }
  }
  if (fp) {
    vfprintf(fp, format, args);
    fclose(fp);
  } else {
    char buffer[1024] = {};
    vsnprintf(buffer, sizeof(buffer) - 1, format, args);
    OutputDebugStringA(buffer);
  }

  va_end(args);
};

void LOGWNDF(const char* format, ...)
{
  char* buf_fmtted = (char*)malloc(strlen(format) * 4 + 1);

  va_list args;
  va_start(args, format);
  vsprintf(buf_fmtted, format, args);

  va_end(args);

  MessageBoxA(0, buf_fmtted, "Error Logged", MB_OK);

  free(buf_fmtted);
};

void LOGFATALF(const char* format, ...)
{
  char* buf_fmtted = (char*)malloc(strlen(format) * 4 + 1);

  va_list args;
  va_start(args, format);
  vsprintf(buf_fmtted, format, args);

  va_end(args);

  MessageBoxA(0, buf_fmtted, "Fatal Error Logged", MB_OK);

  free(buf_fmtted);

  exit(1);
};

void LOGOUTF(const char* format, ...) {
  char buf_fmtted[4096];

  va_list args;
  va_start(args, format);
  vsprintf(buf_fmtted, format, args);

  va_end(args);

  OutputDebugStringA(buf_fmtted);
};

#pragma warning(pop)
