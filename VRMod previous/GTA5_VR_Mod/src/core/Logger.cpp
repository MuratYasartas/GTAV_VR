#include "Logger.h"
#include <iostream>
#include <Windows.h>

namespace GTA5VR {

Logger& Logger::GetInstance() {
    static Logger instance;
    return instance;
}

Logger::~Logger() {
    Shutdown();
}

bool Logger::Initialize(const std::string& logFilePath, LogLevel minLevel) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_initialized) {
        return true;
    }

    m_logFilePath = logFilePath;
    m_minLevel = minLevel;

    m_logFile.open(logFilePath, std::ios::out | std::ios::trunc);
    if (!m_logFile.is_open()) {
        return false;
    }

    m_initialized = true;

    // Write header
    m_logFile << "========================================\n";
    m_logFile << "  GTA5 VR Mod - Log File\n";
    m_logFile << "  Started: " << GetTimestamp() << "\n";
    m_logFile << "========================================\n\n";
    m_logFile.flush();

    return true;
}

void Logger::Shutdown() {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_initialized && m_logFile.is_open()) {
        m_logFile << "\n========================================\n";
        m_logFile << "  Log ended: " << GetTimestamp() << "\n";
        m_logFile << "========================================\n";
        m_logFile.close();
    }

    m_initialized = false;
}

void Logger::Log(LogLevel level, const std::string& message) {
    if (level < m_minLevel || !m_initialized) {
        return;
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    std::stringstream ss;
    ss << "[" << GetTimestamp() << "] ";
    ss << "[" << LogLevelToString(level) << "] ";
    ss << message;

    std::string formattedMessage = ss.str();

    if (m_fileOutput) {
        WriteToFile(formattedMessage);
    }

    if (m_consoleOutput) {
        WriteToConsole(formattedMessage, level);
    }
}

void Logger::Debug(const std::string& message) {
    Log(LogLevel::Debug, message);
}

void Logger::Verbose(const std::string& message) {
    Log(LogLevel::Verbose, message);
}

void Logger::Info(const std::string& message) {
    Log(LogLevel::Info, message);
}

void Logger::Warning(const std::string& message) {
    Log(LogLevel::Warning, message);
}

void Logger::Error(const std::string& message) {
    Log(LogLevel::Error, message);
}

void Logger::Critical(const std::string& message) {
    Log(LogLevel::Critical, message);
}

void Logger::SetLogLevel(LogLevel level) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_minLevel = level;
}

LogLevel Logger::GetLogLevel() const {
    return m_minLevel;
}

void Logger::SetConsoleOutput(bool enabled) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_consoleOutput = enabled;
}

void Logger::SetFileOutput(bool enabled) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_fileOutput = enabled;
}

void Logger::Flush() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_logFile.is_open()) {
        m_logFile.flush();
    }
}

std::string Logger::GetLogFilePath() const {
    return m_logFilePath;
}

std::string Logger::GetTimestamp() const {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    std::tm tm;
    localtime_s(&tm, &time);

    std::stringstream ss;
    ss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    ss << "." << std::setfill('0') << std::setw(3) << ms.count();

    return ss.str();
}

std::string Logger::LogLevelToString(LogLevel level) const {
    switch (level) {
        case LogLevel::Debug:    return "DEBUG";
        case LogLevel::Verbose:  return "VERBOSE";
        case LogLevel::Info:     return "INFO";
        case LogLevel::Warning:  return "WARNING";
        case LogLevel::Error:    return "ERROR";
        case LogLevel::Critical: return "CRITICAL";
        default:                 return "UNKNOWN";
    }
}

void Logger::WriteToFile(const std::string& formattedMessage) {
    if (m_logFile.is_open()) {
        m_logFile << formattedMessage << "\n";
        m_logFile.flush();
    }
}

void Logger::WriteToConsole(const std::string& formattedMessage, LogLevel level) {
    // Set console color based on log level
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    WORD color;

    switch (level) {
        case LogLevel::Debug:    color = FOREGROUND_INTENSITY; break;
        case LogLevel::Verbose:  color = FOREGROUND_GREEN | FOREGROUND_BLUE; break;
        case LogLevel::Info:     color = FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_RED; break;
        case LogLevel::Warning:  color = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY; break;
        case LogLevel::Error:    color = FOREGROUND_RED | FOREGROUND_INTENSITY; break;
        case LogLevel::Critical: color = FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY; break;
        default:                 color = FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_RED; break;
    }

    SetConsoleTextAttribute(hConsole, color);
    std::cout << formattedMessage << std::endl;
    SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_RED);
}

} // namespace GTA5VR
