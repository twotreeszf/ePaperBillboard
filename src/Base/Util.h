#ifndef UTIL_H
#define UTIL_H

#include <functional>
#include <cstdarg>
#include <string>

#pragma once

namespace Util
{    
    // Format string with printf-style arguments
    std::string format(const char* fmt, ...);
    std::string format(const char* fmt, va_list args);
    
    // Disable brownout detector to prevent resets during high power operations
    void disableBrownoutDetector();

    // Print ESP chip and flash info to log
    void printChipInfo();
};

#endif