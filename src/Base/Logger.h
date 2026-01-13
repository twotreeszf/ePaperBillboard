//
// Created by fanzhang on 2024/6/25.
//

#ifndef NIKONQCPRO_LOGGER_H
#define NIKONQCPRO_LOGGER_H

#include <cstdarg>
#include <string.h>

#ifndef __FILENAME__
#define __FILENAME__ (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)
#endif

enum LogLevel {
    LOG_LEVEL_NONE,       /*!< No log output */
    LOG_LEVEL_ERROR,      /*!< Critical errors, software module can not recover on its own */
    LOG_LEVEL_WARN,       /*!< Error conditions from which recovery measures have been taken */
    LOG_LEVEL_INFO,       /*!< Information messages which describe normal flow of events */
    LOG_LEVEL_DEBUG,      /*!< Extra information which is not necessary for normal use (values, pointers, sizes, etc). */
    LOG_LEVEL_VERBOSE     /*!< Bigger chunks of debugging information, or frequent messages which can potentially flood the output. */
};

#define LOG_E(fmt, ...) do { \
    _logger.logLevel(__FILENAME__, __LINE__, LOG_LEVEL_ERROR, fmt, ##__VA_ARGS__); \
} while(0)

#define LOG_W(fmt, ...) do { \
    _logger.logLevel(__FILENAME__, __LINE__, LOG_LEVEL_WARN, fmt, ##__VA_ARGS__); \
} while(0)

#define LOG_I(fmt, ...) do { \
    _logger.logLevel(__FILENAME__, __LINE__, LOG_LEVEL_INFO, fmt, ##__VA_ARGS__); \
} while(0)

#define LOG_D(fmt, ...) do { \
    _logger.logLevel(__FILENAME__, __LINE__, LOG_LEVEL_DEBUG, fmt, ##__VA_ARGS__); \
} while(0)

#define LOG_V(fmt, ...) do { \
    _logger.logLevel(__FILENAME__, __LINE__, LOG_LEVEL_VERBOSE, fmt, ##__VA_ARGS__); \
} while(0)

#define LOG_I(fmt, ...) do { \
    _logger.logLevel(__FILENAME__, __LINE__, LOG_LEVEL_INFO, fmt, ##__VA_ARGS__); \
} while(0)

class Logger {
public:
    Logger();
    void setLevel(LogLevel level);
    void logLevel(const char* file, int line, LogLevel level, const char* fmt, ...);

private:
    LogLevel _level;
};

extern Logger _logger;


#endif //NIKONQCPRO_LOGGER_H
