#include "Util.h"
#include "Logger.h"
#ifdef ESP32
#include <Arduino.h>
#include <LittleFS.h>
#include <esp_timer.h>
#include "soc/soc.h"           // Disable brownout problems
#include "soc/rtc_cntl_reg.h"  // Disable brownout problems
#elif defined(ESP8266)
#include <Ticker.h>
#endif
#include <vector>
#include <cstdarg>

namespace Util  
{
    std::string format(const char* fmt, va_list args) {
        va_list args_copy;
        va_copy(args_copy, args);
        int length = vsnprintf(nullptr, 0, fmt, args);
        va_end(args);

        std::vector<char> buffer(length + 1);
        vsnprintf(buffer.data(), buffer.size(), fmt, args_copy);
        return std::string(buffer.data(), length);
    }

    std::string format(const char* fmt, ...) {
        va_list args;
        va_start(args, fmt);
        std::string result = format(fmt, args);
        va_end(args);

        return result;
    }

    void disableBrownoutDetector() {
#ifdef ESP32
        WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
#endif
    }

    void printChipInfo() {
#ifdef ESP32
        LOG_I("--- Chip Info ---");
        LOG_I("Chip Model: %s", ESP.getChipModel());
        LOG_I("Chip Revision: %d", ESP.getChipRevision());
        LOG_I("CPU Freq: %u MHz", ESP.getCpuFreqMHz());
        LOG_I("Flash Size: %u KB (%u MB)",
            ESP.getFlashChipSize() / 1024,
            ESP.getFlashChipSize() / 1024 / 1024);
        LOG_I("Flash Speed: %u MHz", ESP.getFlashChipSpeed() / 1000000);

        if (LittleFS.begin()) {
            LOG_I("LittleFS Total: %u KB", LittleFS.totalBytes() / 1024);
            LOG_I("LittleFS Used: %u KB", LittleFS.usedBytes() / 1024);
        }

        LOG_I("Free Heap: %u bytes", ESP.getFreeHeap());
        LOG_I("SDK Version: %s", ESP.getSdkVersion());
        LOG_I("-----------------");
#endif
    }
}