#pragma once

#include <string>
#include <fstream>
#include <mutex>
#include <chrono>
#include <sstream>
#include <iomanip>

namespace GTA5VR {

enum class LogLevel {
    Debug = 0,
    Verbose = 1,
    Info = 2,
    Warning = 3,
    Error = 4,
    Critical = 5
};

class Logger {
public:
    static Logger& GetInstance();

    // Initialization
    bool Initialize(const std::string& logFilePath, LogLevel minLevel = LogLevel::Info);
    void Shutdown();

    // Logging methods
    void Log(LogLevel level, const std::string& message);
    void Debug(const std::string& message);
    void Verbose(const std::string& message);
    void Info(const std::string& message);
    void Warning(const std::string& message);
    void Error(const std::string& message);
    void Critical(const std::string& message);

    // Configuration
    void SetLogLevel(LogLevel level);
    LogLevel GetLogLevel() const;
    void SetConsoleOutput(bool enabled);
    void SetFileOutput(bool enabled);

    // Utility
    void Flush();
    std::string GetLogFilePath() const;

private:
    Logger() = default;
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    std::string GetTimestamp() const;
    std::string LogLevelToString(LogLevel level) const;
    void WriteToFile(const std::string& formattedMessage);
    void WriteToConsole(const std::string& formattedMessage, LogLevel level);

    std::ofstream m_logFile;
    std::string m_logFilePath;
    LogLevel m_minLevel = LogLevel::Info;
    bool m_consoleOutput = true;
    bool m_fileOutput = true;
    bool m_initialized = false;
    std::mutex m_mutex;
};

// Convenience macros
#define LOG_DEBUG(msg) GTA5VR::Logger::GetInstance().Debug(msg)
#define LOG_VERBOSE(msg) GTA5VR::Logger::GetInstance().Verbose(msg)
#define LOG_INFO(msg) GTA5VR::Logger::GetInstance().Info(msg)
#define LOG_WARNING(msg) GTA5VR::Logger::GetInstance().Warning(msg)
#define LOG_ERROR(msg) GTA5VR::Logger::GetInstance().Error(msg)
#define LOG_CRITICAL(msg) GTA5VR::Logger::GetInstance().Critical(msg)

} // namespace GTA5VR
