#pragma once 

#include <iostream>
#include <string>
#include <memory>

enum LogLevel {
    INFO,
    WARNING,
    ERROR,
    FATAL
};

class Logger {
public:
    Logger() {}

    static std::string LogLevelToString(LogLevel level) {
        switch (level) {
        case INFO:
            return "[Info]";
        case WARNING:
            return "[Warning]";
        case ERROR:
            return "[Error]";
        case FATAL:
            return "[Fatal]";
        default:
            return "[Unknown]";
        }
    }

    void Log(LogLevel level, const std::string& message) {
        std::cout << LogLevelToString(level) << " " << message << std::endl;
    }

    // 支持格式化日志消息
    template <typename... Args>
    void LogFormatted(LogLevel level, const std::string& format, Args... args) {
        std::string msg = FormatString(format, args...);
        Log(level, msg);
    }

private:
    template <typename... Args>
    std::string FormatString(const std::string& format, Args... args) {
        int size_s = snprintf(nullptr, 0, format.c_str(), args...) + 1; // Extra space for '\0'
        if (size_s <= 0) { throw std::runtime_error("Error during formatting."); }
        auto size = static_cast<size_t>(size_s);
        std::unique_ptr<char[]> buf(new char[size]);
        snprintf(buf.get(), size, format.c_str(), args...);
        return std::string(buf.get(), buf.get() + size - 1); // We remove the '\0'
    }
};

