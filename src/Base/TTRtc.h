#pragma once

#include <Arduino.h>
#include <ctime>

#define PREF_TIMEZONE           "timezone"
#define PREF_TIMEZONE_LABEL     "timezone_label"
#define TT_TZ_MAX               32
#define TT_RTC_DS3231_ADDR      0x68
#define TT_RTC_MIN_YEAR         2020
#define TT_NTP_HOST_PRIMARY     "ntp.aliyun.com"
#define TT_NTP_HOST_FALLBACK    "pool.ntp.org"
#define TT_NTP_PORT             123
#define TT_NTP_LOCAL_PORT       1337
#define TT_NTP_PACKET_SIZE      48
#define TT_NTP_TIMEOUT_MS       2000
#define TT_NTP_RETRY            5
#define TT_NTP_UNIX_OFFSET      2208988800UL
#define TT_RTC_TIME_TEXT_MAX    20

class TTRtc {
public:
    bool begin();
    bool hasHardwareRtc() const { return _hasDs3231; }

    static bool applyTimezone(const char* posixTz);
    static bool loadTimezone(char* out, size_t outMax);
    static bool loadTimezoneLabel(char* out, size_t outMax);
    static bool saveTimezone(const char* posixTz, const char* label = nullptr);

    bool loadFromHardware();
    bool writeHardware(time_t utc);
    bool setUnixTime(time_t utc);
    bool getLocalTime(struct tm& out) const;
    bool isTimeValid() const;
    bool formatLocal(char* out, size_t outMax) const;
    bool syncFromNtp();

private:
    bool _probeDs3231();
    bool _readDs3231(struct tm& out);
    bool _writeDs3231(const struct tm& local);
    time_t _queryNtp(const char* host);

    bool _hasDs3231 = false;
};
