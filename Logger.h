#pragma once
#include <windows.h>
#include <string>
#include <fstream>
#include <mutex>
#include <ctime>
#include <chrono>
#include <sstream>
#include <vector>

enum LogLevel { LOG_DEBUG, LOG_INFO, LOG_WARNING, LOG_ERROR };

class Logger {
public:
    static Logger& Instance();

    void SetLogFile(const std::wstring& path, LogLevel minLevel = LOG_INFO);
    void SetMinLevel(LogLevel level);

    void Log(LogLevel level, const std::wstring& message, DWORD errorCode = 0);

    void Info(const std::wstring& msg) { Log(LOG_INFO, msg); }
    void Warning(const std::wstring& msg, DWORD err = 0) { Log(LOG_WARNING, msg, err); }
    void Error(const std::wstring& msg, DWORD err = 0) { Log(LOG_ERROR, msg, err); }
    void Debug(const std::wstring& msg) { Log(LOG_DEBUG, msg); }

private:
    Logger() = default;
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    std::wstring logFile_;
    std::ofstream file_;
    std::mutex mutex_;
    LogLevel minLevel_ = LOG_INFO;
    HANDLE mutexHandle_ = nullptr;   // 跨进程互斥句柄（在 .cpp 中初始化）

    bool CheckRotate();
    void Rotate();
    std::string WStringToUTF8(const std::wstring& wstr);   // 声明私有方法
    std::wstring GetCurrentTime();
    std::wstring GetLevelString(LogLevel level);
    std::wstring GetLastErrorString(DWORD err);
};