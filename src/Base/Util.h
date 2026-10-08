#ifndef UTIL_H
#define UTIL_H

#include <functional>
#include <cstdarg>
#include <cstdint>
#include <string>

#pragma once

// IRAM heap is 32-bit access only and unusable by malloc/new, so heap logs report byte-addressable memory.
#define TT_HEAP_CAPS MALLOC_CAP_8BIT

namespace Util
{    
    // Format string with printf-style arguments
    std::string format(const char* fmt, ...);
    std::string format(const char* fmt, va_list args);
    
    // Disable brownout detector to prevent resets during high power operations
    void disableBrownoutDetector();

    // Print ESP chip and flash info to log
    void printChipInfo();

    uint32_t heapFree();
    uint32_t heapLargest();
};

#endif