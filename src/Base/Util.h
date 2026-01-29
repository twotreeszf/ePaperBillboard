#ifndef UTIL_H
#define UTIL_H

#include <functional>
#include <cstdarg>
#include <string>

#pragma once

namespace Util
{
    // Run a function once after specified delay
    void runOnceAfter(std::function<void()> func, int32_t delayMS);
    
    // Trim small floating point values to 0
    float trimSmallValue(float v);
    
    // Format string with printf-style arguments
    std::string format(const char* fmt, ...);
    std::string format(const char* fmt, va_list args);
    
    // Disable brownout detector to prevent resets during high power operations
    void disableBrownoutDetector();

    // Print ESP chip and flash info to log
    void printChipInfo();
};

#endif