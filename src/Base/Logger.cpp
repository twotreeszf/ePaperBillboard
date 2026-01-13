//
// Created by fanzhang on 2024/6/25.
//
#include "Logger.h"
#include <iostream>
#include <map>
#include <cstdarg>
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

    // Division constants
    const unsigned long MSECS_PER_SEC       = 1000;
    const unsigned long SECS_PER_MIN        = 60;
    const unsigned long SECS_PER_HOUR       = 3600;
    const unsigned long SECS_PER_DAY        = 86400;

    // Total time
    const unsigned long msecs               =  millis() ;
    const unsigned long secs                =  msecs / MSECS_PER_SEC;

    // Time in components
    const unsigned long MiliSeconds         =  msecs % MSECS_PER_SEC;
    const unsigned long Seconds             =  secs  % SECS_PER_MIN ;
    const unsigned long Minutes             = (secs  / SECS_PER_MIN) % SECS_PER_MIN;
    const unsigned long Hours               = (secs  % SECS_PER_DAY) / SECS_PER_HOUR;

    // Time as string
    std::string timestamp = Util::format("%02d:%02d:%02d.%03d", Hours, Minutes, Seconds, MiliSeconds);
    std::string output = Util::format(
            "%s[%s]%s[%s:%d]: %s\033[0m",
            colorMap[level].c_str(), timestamp.c_str(), levelMap[level].c_str(), file, line, msg.c_str());
    std::cout << output << std::endl;
}

Logger _logger;