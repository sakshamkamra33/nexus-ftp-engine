// ============================================================================
// logger.h — Thread-safe structured logger (MinGW-compatible)
// ============================================================================
#pragma once

#include "win32_threads.h"
#include <string>
#include <fstream>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <ctime>

namespace ftp {

enum class LogLevel { LVL_DEBUG = 0, LVL_INFO = 1, LVL_WARN = 2, LVL_ERROR = 3, LVL_FATAL = 4 };

class Logger {
public:
    static Logger& instance() {
        static Logger inst;
        return inst;
    }

    void setLevel(LogLevel level) { minLevel_ = level; }

    void setLogFile(const std::string& path) {
        threading::LockGuard lock(mutex_);
        if (file_.is_open()) file_.close();
        file_.open(path, std::ios::app);
    }

    void log(LogLevel level, const std::string& comp, const std::string& msg) {
        if ((int)level < (int)minLevel_) return;
        std::string line = formatLine(level, comp, msg);
        threading::LockGuard lock(mutex_);
        std::cout << line << "\n";
        if (file_.is_open()) file_ << line << "\n";
    }

    void debug(const std::string& c, const std::string& m) { log(LogLevel::LVL_DEBUG, c, m); }
    void info (const std::string& c, const std::string& m) { log(LogLevel::LVL_INFO,  c, m); }
    void warn (const std::string& c, const std::string& m) { log(LogLevel::LVL_WARN,  c, m); }
    void error(const std::string& c, const std::string& m) { log(LogLevel::LVL_ERROR, c, m); }
    void fatal(const std::string& c, const std::string& m) { log(LogLevel::LVL_FATAL, c, m); }

private:
    Logger() : minLevel_(LogLevel::LVL_INFO) {}

    std::string formatLine(LogLevel level, const std::string& comp, const std::string& msg) {
        time_t tt = time(nullptr);
        std::tm* tm_ptr = localtime(&tt);
        char timebuf[32] = {0};
        if (tm_ptr) strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", tm_ptr);

        std::ostringstream ss;
        ss << "[" << timebuf << "] "
           << "[" << lvlStr(level) << "] "
           << "[" << comp << "] "
           << msg;
        return ss.str();
    }

    static const char* lvlStr(LogLevel l) {
        switch (l) {
            case LogLevel::LVL_DEBUG: return "DEBUG";
            case LogLevel::LVL_INFO:  return "INFO ";
            case LogLevel::LVL_WARN:  return "WARN ";
            case LogLevel::LVL_ERROR: return "ERROR";
            case LogLevel::LVL_FATAL: return "FATAL";
        }
        return "?????";
    }

    LogLevel          minLevel_;
    threading::Mutex  mutex_;
    std::ofstream     file_;
};

#define LOG_DEBUG(comp, msg) ftp::Logger::instance().debug(comp, msg)
#define LOG_INFO(comp, msg)  ftp::Logger::instance().info(comp, msg)
#define LOG_WARN(comp, msg)  ftp::Logger::instance().warn(comp, msg)
#define LOG_ERROR(comp, msg) ftp::Logger::instance().error(comp, msg)
#define LOG_FATAL(comp, msg) ftp::Logger::instance().fatal(comp, msg)

} // namespace ftp
