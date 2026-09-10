//
// Created by fanzhang on 2024/6/25.
//
#include "Logger.h"
#include <iostream>
#include <map>
#include <cstdarg>
#include <ctime>
#include <sys/time.h>
#include "Util.h"
#include <Arduino.h>

Logger::Logger() : _level(LOG_LEVEL_INFO) { }

void Logger::setLevel(LogLevel level) {
    _level = level;
}

void Logger::logLevel(const char* file, int line, LogLevel level, const char* fmt, ...) {
    if (level > _level)
        return;

    va_list args;
    va_start(args, fmt);
    std::string msg = Util::format(fmt, args);
    va_end(args);

    static std::map<LogLevel, std::string> levelMap = {
            { LOG_LEVEL_ERROR, "[E]" },
            { LOG_LEVEL_WARN, "[W]" },
            { LOG_LEVEL_INFO, "[I]" },
            { LOG_LEVEL_DEBUG, "[D]" },
            { LOG_LEVEL_VERBOSE, "[V]" },
    };

    static std::map<LogLevel, std::string> colorMap = {
            { LOG_LEVEL_ERROR, "\033[31m" },
            { LOG_LEVEL_WARN, "\033[35m" },
            { LOG_LEVEL_INFO, "\033[32m" },
            { LOG_LEVEL_DEBUG, "\033[34m" },
            { LOG_LEVEL_VERBOSE, "" },
    };

    std::string timestamp;
    struct timeval tv = {};
    gettimeofday(&tv, nullptr);
    struct tm local = {};
    if (localtime_r(&tv.tv_sec, &local) != nullptr
        && (local.tm_year + 1900) >= TT_LOG_TIME_MIN_YEAR) {
        timestamp = Util::format("%04d-%02d-%02d %02d:%02d:%02d.%03d",
                                 local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
                                 local.tm_hour, local.tm_min, local.tm_sec,
                                 (int)(tv.tv_usec / 1000));
    } else {
        const unsigned long msecs = millis();
        const unsigned long secs = msecs / 1000UL;
        timestamp = Util::format("%02d:%02d:%02d.%03d",
                                 (int)((secs % 86400UL) / 3600UL),
                                 (int)((secs / 60UL) % 60UL),
                                 (int)(secs % 60UL),
                                 (int)(msecs % 1000UL));
    }
    std::string output = Util::format(
            "%s[%s]%s[%s:%d]: %s\033[0m",
            colorMap[level].c_str(), timestamp.c_str(), levelMap[level].c_str(), file, line, msg.c_str());
    std::cout << output << std::endl;
}

Logger _logger;