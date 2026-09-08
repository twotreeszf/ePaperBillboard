#include "TTRtc.h"
#include "Logger.h"
#include "ErrorCheck.h"
#include "TTInstance.h"
#include "TTPreference.h"
#include <WiFi.h>
#include <WiFiUdp.h>
#include <Wire.h>
#include <sys/time.h>
#include <cstring>

static uint8_t decToBcd(uint8_t v) {
    return (uint8_t)(((v / 10) << 4) | (v % 10));
}

static uint8_t bcdToDec(uint8_t v) {
    return (uint8_t)(((v >> 4) * 10) + (v & 0x0f));
}

static bool timezoneLooksValid(const char* posixTz) {
    if (posixTz == nullptr || posixTz[0] == '\0') {
        return false;
    }
    size_t n = 0;
    for (const char* p = posixTz; *p != '\0'; ++p, ++n) {
        if (n >= TT_TZ_MAX) {
            return false;
        }
        const char c = *p;
        if (c < 32 || c > 126 || c == ' ' || c == '<' || c == '>' || c == '"' || c == '\'') {
            return false;
        }
    }
    return n > 0;
}

bool TTRtc::begin() {
    if (!TTInstanceOf<TTPreference>().begin()) {
        LOG_E("RTC: preference begin failed");
    }
    _hasDs3231 = _probeDs3231();
    LOG_I("RTC: DS3231 %s", _hasDs3231 ? "found" : "not found");

    char tz[TT_TZ_MAX + 1];
    if (loadTimezone(tz, sizeof(tz))) {
        applyTimezone(tz);
    } else {
        LOG_I("RTC: no saved timezone");
    }
    if (_hasDs3231) {
        loadFromHardware();
    }
    return true;
}

bool TTRtc::applyTimezone(const char* posixTz) {
    if (!timezoneLooksValid(posixTz)) {
        LOG_W("RTC: reject timezone");
        return false;
    }
    setenv("TZ", posixTz, 1);
    tzset();
    LOG_I("RTC: apply TZ=%s", posixTz);
    return true;
}

bool TTRtc::loadTimezone(char* out, size_t outMax) {
    if (out == nullptr || outMax == 0) {
        return false;
    }
    out[0] = '\0';
    String tz;
    ERR_CHECK_RET(TTInstanceOf<TTPreference>().get(PREF_TIMEZONE, tz, String("")));
    if (tz.isEmpty() || !timezoneLooksValid(tz.c_str())) {
        return false;
    }
    strncpy(out, tz.c_str(), outMax - 1);
    out[outMax - 1] = '\0';
    return true;
}

bool TTRtc::loadTimezoneLabel(char* out, size_t outMax) {
    if (out == nullptr || outMax == 0) {
        return false;
    }
    out[0] = '\0';
    String label;
    ERR_CHECK_RET(TTInstanceOf<TTPreference>().get(PREF_TIMEZONE_LABEL, label, String("")));
    if (label.isEmpty()) {
        return false;
    }
    strncpy(out, label.c_str(), outMax - 1);
    out[outMax - 1] = '\0';
    return true;
}

bool TTRtc::saveTimezone(const char* posixTz, const char* label) {
    ERR_CHECK_RET(applyTimezone(posixTz));
    auto& pref = TTInstanceOf<TTPreference>();
    ERR_CHECK_RET(pref.set(PREF_TIMEZONE, String(posixTz)));
    if (label != nullptr && label[0] != '\0') {
        ERR_CHECK_RET(pref.set(PREF_TIMEZONE_LABEL, String(label)));
    } else {
        pref.remove(PREF_TIMEZONE_LABEL);
    }
    ERR_CHECK_RET(pref.sync());
    LOG_I("RTC: saved TZ=%s label=%s", posixTz, label != nullptr ? label : "");
    return true;
}

bool TTRtc::loadFromHardware() {
    if (!_hasDs3231) {
        _hasDs3231 = _probeDs3231();
    }
    if (!_hasDs3231) {
        return false;
    }
    struct tm local;
    if (!_readDs3231(local) || local.tm_year < (TT_RTC_MIN_YEAR - 1900)) {
        LOG_W("RTC: DS3231 time not valid");
        return false;
    }
    time_t now = mktime(&local);
    if (now <= 0) {
        LOG_W("RTC: mktime failed");
        return false;
    }
    struct timeval tv;
    tv.tv_sec = now;
    tv.tv_usec = 0;
    settimeofday(&tv, nullptr);
    LOG_I("RTC: loaded DS3231 %04d-%02d-%02d %02d:%02d:%02d",
          local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
          local.tm_hour, local.tm_min, local.tm_sec);
    return true;
}

bool TTRtc::writeHardware(time_t utc) {
    if (!_hasDs3231) {
        _hasDs3231 = _probeDs3231();
    }
    if (!_hasDs3231) {
        return false;
    }
    struct tm local;
    localtime_r(&utc, &local);
    return _writeDs3231(local);
}

bool TTRtc::setUnixTime(time_t utc) {
    if (utc < 0) {
        return false;
    }
    struct timeval tv;
    tv.tv_sec = utc;
    tv.tv_usec = 0;
    settimeofday(&tv, nullptr);
    LOG_I("RTC: set unix=%ld", (long)utc);
    return true;
}

bool TTRtc::getLocalTime(struct tm& out) const {
    time_t now = 0;
    time(&now);
    if (now <= 0) {
        return false;
    }
    localtime_r(&now, &out);
    return out.tm_year >= (TT_RTC_MIN_YEAR - 1900);
}

bool TTRtc::isTimeValid() const {
    struct tm t;
    return getLocalTime(t);
}

bool TTRtc::formatLocal(char* out, size_t outMax) const {
    if (out == nullptr || outMax == 0) {
        return false;
    }
    struct tm t;
    if (!getLocalTime(t)) {
        out[0] = '\0';
        return false;
    }
    snprintf(out, outMax, "%04d-%02d-%02d %02d:%02d",
             t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min);
    return true;
}

bool TTRtc::syncFromNtp() {
    time_t utc = 0;
    for (int i = 0; i < TT_NTP_RETRY; ++i) {
        utc = _queryNtp(TT_NTP_HOST_PRIMARY);
        if (utc > 0) {
            break;
        }
        LOG_W("RTC: NTP primary retry %d", i + 1);
        delay(500);
    }
    if (utc <= 0) {
        LOG_W("RTC: try fallback NTP %s", TT_NTP_HOST_FALLBACK);
        utc = _queryNtp(TT_NTP_HOST_FALLBACK);
    }
    if (utc <= 0 || utc == (time_t)0xFFFFFFFF) {
        LOG_E("RTC: NTP failed");
        return false;
    }
    return setUnixTime(utc);
}

bool TTRtc::_probeDs3231() {
    Wire.beginTransmission(TT_RTC_DS3231_ADDR);
    return Wire.endTransmission() == 0;
}

bool TTRtc::_readDs3231(struct tm& out) {
    Wire.beginTransmission(TT_RTC_DS3231_ADDR);
    Wire.write(0);
    if (Wire.endTransmission() != 0) {
        LOG_E("RTC: DS3231 read start failed");
        return false;
    }
    if (Wire.requestFrom((uint8_t)TT_RTC_DS3231_ADDR, (uint8_t)7) != 7) {
        LOG_E("RTC: DS3231 read short");
        return false;
    }
    const uint8_t sec = bcdToDec(Wire.read() & 0x7f);
    const uint8_t min = bcdToDec(Wire.read() & 0x7f);
    const uint8_t hour = bcdToDec(Wire.read() & 0x3f);
    const uint8_t dow = bcdToDec(Wire.read() & 0x07);
    const uint8_t day = bcdToDec(Wire.read() & 0x3f);
    const uint8_t month = bcdToDec(Wire.read() & 0x1f);
    const uint8_t year = bcdToDec(Wire.read());

    memset(&out, 0, sizeof(out));
    out.tm_sec = sec;
    out.tm_min = min;
    out.tm_hour = hour;
    out.tm_mday = day;
    out.tm_mon = month - 1;
    out.tm_year = year + 100;
    out.tm_wday = dow > 0 ? (dow - 1) : 0;
    return month >= 1 && month <= 12 && day >= 1 && day <= 31;
}

bool TTRtc::_writeDs3231(const struct tm& local) {
    Wire.beginTransmission(TT_RTC_DS3231_ADDR);
    Wire.write(0);
    Wire.write(decToBcd((uint8_t)local.tm_sec));
    Wire.write(decToBcd((uint8_t)local.tm_min));
    Wire.write(decToBcd((uint8_t)local.tm_hour));
    Wire.write(decToBcd((uint8_t)(local.tm_wday + 1)));
    Wire.write(decToBcd((uint8_t)local.tm_mday));
    Wire.write(decToBcd((uint8_t)(local.tm_mon + 1)));
    Wire.write(decToBcd((uint8_t)(local.tm_year - 100)));
    if (Wire.endTransmission() != 0) {
        LOG_E("RTC: DS3231 write failed");
        return false;
    }
    LOG_I("RTC: wrote DS3231 %04d-%02d-%02d %02d:%02d:%02d",
          local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
          local.tm_hour, local.tm_min, local.tm_sec);
    return true;
}

time_t TTRtc::_queryNtp(const char* host) {
    WiFiUDP udp;
    uint8_t packet[TT_NTP_PACKET_SIZE];
    IPAddress ntpIp;
    if (WiFi.hostByName(host, ntpIp) != 1) {
        LOG_E("RTC: NTP resolve failed host=%s", host);
        return 0;
    }
    while (udp.parsePacket() > 0) {
    }
    if (!udp.begin(TT_NTP_LOCAL_PORT)) {
        LOG_E("RTC: UDP begin failed");
        return 0;
    }

    memset(packet, 0, sizeof(packet));
    packet[0] = 0b11100011;
    packet[1] = 0;
    packet[2] = 6;
    packet[3] = 0xEC;
    packet[12] = 49;
    packet[13] = 0x4E;
    packet[14] = 49;
    packet[15] = 52;

    if (!udp.beginPacket(ntpIp, TT_NTP_PORT) || udp.write(packet, TT_NTP_PACKET_SIZE) != TT_NTP_PACKET_SIZE || !udp.endPacket()) {
        LOG_E("RTC: NTP send failed host=%s", host);
        udp.stop();
        return 0;
    }

    const uint32_t start = millis();
    while (millis() - start < TT_NTP_TIMEOUT_MS) {
        const int size = udp.parsePacket();
        if (size >= TT_NTP_PACKET_SIZE) {
            udp.read(packet, TT_NTP_PACKET_SIZE);
            udp.stop();
            unsigned long secsSince1900 = ((unsigned long)packet[40] << 24)
                | ((unsigned long)packet[41] << 16)
                | ((unsigned long)packet[42] << 8)
                | ((unsigned long)packet[43]);
            if (secsSince1900 == 0 || secsSince1900 == 0xFFFFFFFFUL) {
                return 0;
            }
            const time_t utc = (time_t)(secsSince1900 - TT_NTP_UNIX_OFFSET);
            LOG_I("RTC: NTP %s unix=%ld", host, (long)utc);
            return utc;
        }
        delay(20);
    }
    udp.stop();
    LOG_W("RTC: NTP timeout host=%s", host);
    return 0;
}
