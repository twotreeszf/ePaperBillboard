#include "TTCalendarService.h"
#include "Logger.h"
#include "TTHttpsClient.h"
#include "TTFile.h"
#include "TTInstance.h"
#include "TTNotificationPayloads.h"
#include "TTPreference.h"
#include "../Tasks/TTUITask.h"
#include "../Tasks/TTWiFiTask.h"
#include <WiFi.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>

namespace {

const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

size_t base64Encode(const uint8_t* in, size_t len, char* out, size_t outMax) {
    size_t written = 0;
    for (size_t i = 0; i < len; i += 3) {
        const uint32_t b0 = in[i];
        const uint32_t b1 = (i + 1 < len) ? in[i + 1] : 0;
        const uint32_t b2 = (i + 2 < len) ? in[i + 2] : 0;
        const uint32_t n = (b0 << 16) | (b1 << 8) | b2;
        if (written + 4 >= outMax) {
            return 0;
        }
        out[written++] = kB64[(n >> 18) & 63];
        out[written++] = kB64[(n >> 12) & 63];
        out[written++] = (i + 1 < len) ? kB64[(n >> 6) & 63] : '=';
        out[written++] = (i + 2 < len) ? kB64[n & 63] : '=';
    }
    out[written] = '\0';
    return written;
}

bool buildAuthHeader(const char* user, const char* pass, char* out, size_t outMax) {
    char raw[TT_CAL_USER_MAX + TT_CAL_PASS_MAX + 2];
    const int rawLen = snprintf(raw, sizeof(raw), "%s:%s", user, pass);
    if (rawLen <= 0 || rawLen >= (int)sizeof(raw)) {
        return false;
    }
    char encoded[96];
    if (base64Encode((const uint8_t*)raw, (size_t)rawLen, encoded, sizeof(encoded)) == 0) {
        return false;
    }
    return snprintf(out, outMax, "Authorization: Basic %s\r\nDepth: 1\r\n", encoded) > 0
        && strlen(out) + 1 < outMax;
}

void formatUtc(time_t t, char* out, size_t outMax) {
    struct tm utc = {};
    gmtime_r(&t, &utc);
    snprintf(out, outMax, "%04d%02d%02dT%02d%02d%02dZ",
             utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday,
             utc.tm_hour, utc.tm_min, utc.tm_sec);
}

time_t localMidnight(time_t now) {
    struct tm local = {};
    localtime_r(&now, &local);
    local.tm_hour = 0;
    local.tm_min = 0;
    local.tm_sec = 0;
    return mktime(&local);
}

bool httpOk(int status) {
    return status == 200 || status == 207;
}

struct XmlDec {
    File* file;
    int hold;

    int raw() {
        if (hold >= 0) {
            const int c = hold;
            hold = -1;
            return c;
        }
        if (file == nullptr || !file->available()) {
            return -1;
        }
        return file->read();
    }

    int next() {
        const int c = raw();
        if (c != '&') {
            return c;
        }
        char ent[8];
        size_t n = 0;
        while (n + 1 < sizeof(ent)) {
            const int e = raw();
            if (e < 0 || e == ';') {
                break;
            }
            ent[n++] = (char)e;
        }
        ent[n] = '\0';
        if (strcmp(ent, "#xD") == 0 || strcmp(ent, "#x0D") == 0 || strcmp(ent, "#13") == 0) {
            return '\r';
        }
        if (strcmp(ent, "#xA") == 0 || strcmp(ent, "#x0A") == 0 || strcmp(ent, "#10") == 0) {
            return '\n';
        }
        if (strcmp(ent, "#39") == 0 || strcmp(ent, "#x27") == 0) {
            return '\'';
        }
        if (strcmp(ent, "amp") == 0) {
            return '&';
        }
        if (strcmp(ent, "lt") == 0) {
            return '<';
        }
        if (strcmp(ent, "gt") == 0) {
            return '>';
        }
        if (strcmp(ent, "quot") == 0) {
            return '"';
        }
        return '?';
    }
};

bool readIcsLine(XmlDec* dec, char* buf, size_t maxLen) {
    size_t n = 0;
    bool any = false;
    while (true) {
        const int c = dec->next();
        if (c < 0) {
            buf[n] = '\0';
            return any;
        }
        if (c == '\r') {
            continue;
        }
        if (c == '\n') {
            const int peek = dec->next();
            if (peek == ' ' || peek == '\t') {
                continue;
            }
            if (peek >= 0) {
                dec->hold = peek;
            }
            buf[n] = '\0';
            return true;
        }
        any = true;
        if (n + 1 < maxLen) {
            buf[n++] = (char)c;
        }
    }
}

void copyUtf8(char* dst, size_t dstMax, const char* src) {
    if (dstMax == 0) {
        return;
    }
    size_t n = 0;
    while (src[n] != '\0' && n + 1 < dstMax) {
        n++;
    }
    while (n > 0 && (src[n] & 0xC0) == 0x80) {
        n--;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

void icsUnescape(char* text) {
    size_t w = 0;
    for (size_t r = 0; text[r] != '\0'; r++) {
        if (text[r] == '\\' && text[r + 1] != '\0') {
            const char n = text[++r];
            if (n == 'n' || n == 'N') {
                text[w++] = ' ';
            } else {
                text[w++] = n;
            }
            continue;
        }
        text[w++] = text[r];
    }
    text[w] = '\0';
}

const char* propValue(const char* line) {
    const char* colon = strrchr(line, ':');
    return colon != nullptr ? colon + 1 : line;
}

int daysFromCivil(int year, unsigned month, unsigned day) {
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = (unsigned)(year - era * 400);
    const unsigned doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int)doe - 719468;
}

time_t unixFromUtc(int year, int month, int day, int hour, int minute, int second) {
    return (time_t)daysFromCivil(year, (unsigned)month, (unsigned)day) * 86400
        + (time_t)hour * 3600 + (time_t)minute * 60 + second;
}

bool parseStamp(const char* line, time_t* out, bool* allDay) {
    const char* value = propValue(line);
    *allDay = strstr(line, "VALUE=DATE") != nullptr;
    int y = 0;
    int mo = 0;
    int d = 0;
    int hh = 0;
    int mm = 0;
    int ss = 0;
    if (sscanf(value, "%4d%2d%2d", &y, &mo, &d) != 3) {
        return false;
    }
    const bool dateOnly = strlen(value) <= 8 || *allDay;
    if (!dateOnly) {
        if (sscanf(value, "%4d%2d%2dT%2d%2d%2d", &y, &mo, &d, &hh, &mm, &ss) < 6) {
            return false;
        }
    } else {
        *allDay = true;
    }
    struct tm t = {};
    t.tm_year = y - 1900;
    t.tm_mon = mo - 1;
    t.tm_mday = d;
    t.tm_hour = hh;
    t.tm_min = mm;
    t.tm_sec = ss;
    const bool zulu = !dateOnly && strchr(value, 'Z') != nullptr;
    *out = zulu ? unixFromUtc(y, mo, d, hh, mm, ss) : mktime(&t);
    return *out > 0;
}

int weekdayBit(const char* token) {
    while (*token >= '0' && *token <= '9') {
        token++;
    }
    if (*token == '+' || *token == '-') {
        token++;
        while (*token >= '0' && *token <= '9') {
            token++;
        }
    }
    if (strncmp(token, "SU", 2) == 0) return 0;
    if (strncmp(token, "MO", 2) == 0) return 1;
    if (strncmp(token, "TU", 2) == 0) return 2;
    if (strncmp(token, "WE", 2) == 0) return 3;
    if (strncmp(token, "TH", 2) == 0) return 4;
    if (strncmp(token, "FR", 2) == 0) return 5;
    if (strncmp(token, "SA", 2) == 0) return 6;
    return -1;
}

struct CalRule {
    int freq;
    int interval;
    int count;
    time_t until;
    uint8_t byday;
};

void parseRule(const char* text, CalRule* rule, time_t start) {
    memset(rule, 0, sizeof(*rule));
    rule->interval = 1;
    char buf[96];
    strncpy(buf, text, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    char* save = nullptr;
    for (char* part = strtok_r(buf, ";", &save); part != nullptr; part = strtok_r(nullptr, ";", &save)) {
        if (strncmp(part, "FREQ=", 5) == 0) {
            if (strcmp(part + 5, "DAILY") == 0) {
                rule->freq = 1;
            } else if (strcmp(part + 5, "WEEKLY") == 0) {
                rule->freq = 2;
            } else if (strcmp(part + 5, "MONTHLY") == 0) {
                rule->freq = 3;
            }
        } else if (strncmp(part, "INTERVAL=", 9) == 0) {
            rule->interval = atoi(part + 9);
            if (rule->interval < 1) {
                rule->interval = 1;
            }
        } else if (strncmp(part, "COUNT=", 6) == 0) {
            rule->count = atoi(part + 6);
        } else if (strncmp(part, "UNTIL=", 6) == 0) {
            char stamp[40];
            snprintf(stamp, sizeof(stamp), "DTSTART:%s", part + 6);
            bool allDay = false;
            parseStamp(stamp, &rule->until, &allDay);
        } else if (strncmp(part, "BYDAY=", 6) == 0) {
            char* daySave = nullptr;
            for (char* day = strtok_r(part + 6, ",", &daySave); day != nullptr;
                 day = strtok_r(nullptr, ",", &daySave)) {
                const int bit = weekdayBit(day);
                if (bit >= 0) {
                    rule->byday = (uint8_t)(rule->byday | (1u << bit));
                }
            }
        }
    }
    if (rule->freq == 2 && rule->byday == 0) {
        struct tm local = {};
        localtime_r(&start, &local);
        rule->byday = (uint8_t)(1u << local.tm_wday);
    }
}

bool exchange(const char* url, const char* method, const char* authHeader, const char* body,
              TTHttpsResult* out) {
    TTHttpsRequest request = {};
    request.url = url;
    request.method = method;
    request.contentType = "application/xml; charset=utf-8";
    request.body = body;
    request.extraHeaders = authHeader;
    request.bodyMax = TT_CAL_BODY_MAX;
    LOG_I("CalDAV: %s %s", method, url);
    if (!tt_https_exchange_file(&request, TT_CAL_TMP, out)) {
        LOG_E("CalDAV: exchange failed");
        return false;
    }
    LOG_I("CalDAV: status=%d body=%u", out->status, (unsigned)out->bodyLen);
    if (!httpOk(out->status)) {
        tt_file_remove(TT_CAL_TMP);
        return false;
    }
    return true;
}

int collectHrefs(char hrefs[][TT_CAL_HREF_LEN], int maxCount, bool icsOnly) {
    File file = tt_file_open(TT_CAL_TMP, "r");
    if (!file) {
        return 0;
    }
    const char* key = "<D:href>";
    int matched = 0;
    int collecting = 0;
    bool truncated = false;
    char buf[TT_CAL_HREF_LEN];
    int len = 0;
    int count = 0;
    while (file.available()) {
        const int c = file.read();
        if (collecting) {
            if (c == '<') {
                buf[len] = '\0';
                if (truncated) {
                    LOG_W("CalDAV: href longer than %u", (unsigned)(TT_CAL_HREF_LEN - 1));
                    collecting = 0;
                    truncated = false;
                    matched = 0;
                    len = 0;
                    continue;
                }
                const bool isIcs = strstr(buf, ".ics") != nullptr;
                const bool isCalendar = strncmp(buf, "/calendars/", 11) == 0
                    && strlen(buf) > 11 && !isIcs;
                if ((icsOnly ? isIcs : isCalendar) && count < maxCount) {
                    strncpy(hrefs[count], buf, TT_CAL_HREF_LEN - 1);
                    hrefs[count][TT_CAL_HREF_LEN - 1] = '\0';
                    count++;
                }
                collecting = 0;
                truncated = false;
                matched = 0;
                len = 0;
            } else if (len + 1 < TT_CAL_HREF_LEN) {
                buf[len++] = (char)c;
            } else {
                truncated = true;
            }
            continue;
        }
        if (c == key[matched]) {
            matched++;
            if (key[matched] == '\0') {
                collecting = 1;
                matched = 0;
                len = 0;
            }
        } else {
            matched = (c == key[0]) ? 1 : 0;
        }
    }
    file.close();
    tt_file_remove(TT_CAL_TMP);
    return count;
}

}  // namespace

uint8_t TTCalendarService::copyEvents(TTCalEvent* out, uint8_t maxCount) const {
    if (out == nullptr || maxCount == 0) {
        return 0;
    }
    const uint8_t n = _count < maxCount ? _count : maxCount;
    if (n > 0) {
        memcpy(out, _events, sizeof(TTCalEvent) * n);
    }
    return n;
}

void TTCalendarService::publish(TTCalendarState state, const char* message, bool extended) {
    TTCalendarPayload payload = {};
    payload.state = state;
    payload.rangeStart = _rangeStart;
    payload.rangeEnd = _rangeEnd;
    payload.count = _count;
    payload.extended = extended ? 1 : 0;
    if (message != nullptr) {
        strncpy(payload.message, message, sizeof(payload.message) - 1);
    }
    LOG_I("CalDAV: publish state=%d events=%u range=%ld..%ld extend=%d",
          (int)state, (unsigned)_count, (long)_rangeStart, (long)_rangeEnd, extended ? 1 : 0);
    TTInstanceOf<TTUITask>().postNotification(TT_NOTIFICATION_CALENDAR, payload);
}

void TTCalendarService::requestFetch(bool extend) {
    TTInstanceOf<TTWiFiTask>().runWithRadio(
        "calendar",
        [this, extend]() {
            fetch(extend);
        },
        [this, extend]() {
            LOG_W("CalDAV: radio failed");
            if (extend && _count > 0) {
                publish(TT_CAL_OK, "", true);
                return;
            }
            publish(TT_CAL_NEED_WIFI, "未连接 Wi-Fi", false);
        });
}

bool TTCalendarService::loadAccount(char* host, size_t hostMax, char* user, size_t userMax,
                                    char* pass, size_t passMax) {
    String hostValue;
    String userValue;
    String passValue;
    TTPreference& prefs = TTInstanceOf<TTPreference>();
    prefs.get(PREF_CALDAV_HOST, hostValue, String(""));
    prefs.get(PREF_CALDAV_USER, userValue, String(""));
    prefs.get(PREF_CALDAV_PASS, passValue, String(""));
    if (hostValue.length() == 0 || userValue.length() == 0 || passValue.length() == 0) {
        return false;
    }
    strncpy(host, hostValue.c_str(), hostMax - 1);
    host[hostMax - 1] = '\0';
    strncpy(user, userValue.c_str(), userMax - 1);
    user[userMax - 1] = '\0';
    strncpy(pass, passValue.c_str(), passMax - 1);
    pass[passMax - 1] = '\0';
    if (strcmp(host, _accountHost) != 0 || strcmp(user, _accountUser) != 0) {
        const bool changed = _accountHost[0] != '\0' || _accountUser[0] != '\0';
        _discovered = false;
        _calCount = 0;
        strncpy(_accountHost, host, sizeof(_accountHost) - 1);
        _accountHost[sizeof(_accountHost) - 1] = '\0';
        strncpy(_accountUser, user, sizeof(_accountUser) - 1);
        _accountUser[sizeof(_accountUser) - 1] = '\0';
        if (changed) {
            LOG_I("CalDAV: account changed host=%s user=%s", _accountHost, _accountUser);
        }
    }
    return true;
}

bool TTCalendarService::discover(const char* host, const char* authHeader) {
    if (_discovered && _calCount > 0) {
        return true;
    }
    char url[96];
    snprintf(url, sizeof(url), "https://%s/calendars/", host);
    TTHttpsResult result = {};
    if (!exchange(url, "PROPFIND", authHeader, nullptr, &result)) {
        return false;
    }
    _calCount = (uint8_t)collectHrefs(_hrefs, TT_CAL_CALS_MAX, false);
    for (uint8_t i = 0; i < _calCount; i++) {
        strncpy(_cals[i], _hrefs[i], TT_CAL_PATH_MAX - 1);
        _cals[i][TT_CAL_PATH_MAX - 1] = '\0';
    }
    _discovered = _calCount > 0;
    LOG_I("CalDAV: calendars=%u", (unsigned)_calCount);
    return _discovered;
}

int TTCalendarService::queryHrefs(const char* host, const char* authHeader, const char* calendarPath,
                                  time_t rangeStart, time_t rangeEnd) {
    char startText[20];
    char endText[20];
    formatUtc(rangeStart, startText, sizeof(startText));
    formatUtc(rangeEnd, endText, sizeof(endText));
    char body[512];
    snprintf(body, sizeof(body),
             "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
             "<c:calendar-query xmlns:c=\"urn:ietf:params:xml:ns:caldav\" xmlns:d=\"DAV:\">"
             "<d:prop><d:getetag/></d:prop>"
             "<c:filter><c:comp-filter name=\"VCALENDAR\">"
             "<c:comp-filter name=\"VEVENT\">"
             "<c:time-range start=\"%s\" end=\"%s\"/>"
             "</c:comp-filter></c:comp-filter></c:filter>"
             "</c:calendar-query>",
             startText, endText);
    char url[128];
    snprintf(url, sizeof(url), "https://%s%s", host, calendarPath);
    TTHttpsResult result = {};
    if (!exchange(url, "REPORT", authHeader, body, &result)) {
        return -1;
    }
    return collectHrefs(_hrefs, TT_CAL_HREF_MAX, true);
}

bool TTCalendarService::pullEvents(const char* host, const char* authHeader, const char* calendarPath,
                                   int hrefCount, time_t rangeStart, time_t rangeEnd) {
    char url[128];
    snprintf(url, sizeof(url), "https://%s%s", host, calendarPath);
    for (int offset = 0; offset < hrefCount; offset += TT_CAL_MULTIGET_BATCH) {
        const int batch = (offset + TT_CAL_MULTIGET_BATCH <= hrefCount)
            ? TT_CAL_MULTIGET_BATCH : hrefCount - offset;
        char body[1400];
        int used = snprintf(body, sizeof(body),
                            "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
                            "<c:calendar-multiget xmlns:c=\"urn:ietf:params:xml:ns:caldav\" xmlns:d=\"DAV:\">"
                            "<d:prop><d:getetag/><c:calendar-data/></d:prop>");
        if (used <= 0 || used >= (int)sizeof(body)) {
            return false;
        }
        for (int i = 0; i < batch; i++) {
            const int extra = snprintf(body + used, sizeof(body) - (size_t)used,
                                       "<d:href>%s</d:href>", _hrefs[offset + i]);
            if (extra <= 0 || used + extra >= (int)sizeof(body)) {
                return false;
            }
            used += extra;
        }
        const int tail = snprintf(body + used, sizeof(body) - (size_t)used, "</c:calendar-multiget>");
        if (tail <= 0 || used + tail >= (int)sizeof(body)) {
            return false;
        }
        TTHttpsResult result = {};
        if (!exchange(url, "REPORT", authHeader, body, &result)) {
            LOG_W("CalDAV: multiget failed offset=%d", offset);
            continue;
        }
        File file = tt_file_open(TT_CAL_TMP, "r");
        if (!file) {
            continue;
        }
        if (!file.seek(result.bodyOffset)) {
            file.close();
            tt_file_remove(TT_CAL_TMP);
            continue;
        }
        XmlDec dec = {};
        dec.file = &file;
        dec.hold = -1;
        char line[96];
        char title[TT_CAL_TITLE_MAX];
        char ruleText[96];
        time_t start = 0;
        time_t end = 0;
        bool allDay = false;
        bool inEvent = false;
        bool cancelled = false;
        bool haveStart = false;
        title[0] = '\0';
        ruleText[0] = '\0';
        auto commit = [&]() {
            if (!inEvent || cancelled || !haveStart || title[0] == '\0') {
                return;
            }
            if (end <= start) {
                end = start + (allDay ? 86400 : 3600);
            }
            CalRule rule = {};
            if (ruleText[0] != '\0') {
                parseRule(ruleText, &rule, start);
            }
            const time_t duration = end - start;
            auto accept = [&](time_t inst) {
                if (inst + duration <= rangeStart || inst >= rangeEnd || _count >= TT_CAL_EVENT_MAX) {
                    return;
                }
                for (uint8_t i = 0; i < _count; i++) {
                    if (_events[i].startUnix == (int32_t)inst && strcmp(_events[i].title, title) == 0) {
                        return;
                    }
                }
                uint8_t at = _count;
                while (at > 0 && _events[at - 1].startUnix > (int32_t)inst) {
                    _events[at] = _events[at - 1];
                    at--;
                }
                _events[at].startUnix = (int32_t)inst;
                _events[at].endUnix = (int32_t)(inst + duration);
                _events[at].allDay = allDay ? 1 : 0;
                copyUtf8(_events[at].title, sizeof(_events[at].title), title);
                _count++;
            };
            if (rule.freq == 0) {
                accept(start);
            } else {
                struct tm startLocal = {};
                localtime_r(&start, &startLocal);
                const time_t day0 = localMidnight(start);
                int seen = 0;
                for (int i = 0; i < 1200; i++) {
                    const time_t day = day0 + (time_t)i * 86400;
                    if (rule.until > 0 && day > rule.until + 86400) {
                        break;
                    }
                    struct tm local = {};
                    localtime_r(&day, &local);
                    bool match = false;
                    if (rule.freq == 1) {
                        match = (i % rule.interval) == 0;
                    } else if (rule.freq == 2) {
                        const int week = i / 7;
                        match = (rule.byday & (1u << local.tm_wday)) != 0 && (week % rule.interval) == 0;
                    } else if (rule.freq == 3) {
                        match = local.tm_mday == startLocal.tm_mday
                            && ((local.tm_year * 12 + local.tm_mon) - (startLocal.tm_year * 12 + startLocal.tm_mon))
                                % rule.interval == 0;
                    }
                    if (!match) {
                        continue;
                    }
                    seen++;
                    if (rule.count > 0 && seen > rule.count) {
                        break;
                    }
                    const time_t inst = allDay ? day : day + (time_t)startLocal.tm_hour * 3600
                        + (time_t)startLocal.tm_min * 60 + startLocal.tm_sec;
                    if (rule.until > 0 && inst > rule.until) {
                        break;
                    }
                    accept(inst);
                    if (day > rangeEnd) {
                        break;
                    }
                }
            }
        };
        while (readIcsLine(&dec, line, sizeof(line))) {
            if (strcmp(line, "BEGIN:VEVENT") == 0) {
                inEvent = true;
                cancelled = false;
                haveStart = false;
                allDay = false;
                start = 0;
                end = 0;
                title[0] = '\0';
                ruleText[0] = '\0';
                continue;
            }
            if (!inEvent) {
                continue;
            }
            if (strcmp(line, "END:VEVENT") == 0) {
                commit();
                inEvent = false;
                continue;
            }
            if (strncmp(line, "SUMMARY", 7) == 0) {
                copyUtf8(title, sizeof(title), propValue(line));
                icsUnescape(title);
            } else if (strncmp(line, "DTSTART", 7) == 0) {
                bool date = false;
                haveStart = parseStamp(line, &start, &date);
                allDay = date;
            } else if (strncmp(line, "DTEND", 5) == 0) {
                bool date = false;
                parseStamp(line, &end, &date);
            } else if (strncmp(line, "RRULE", 5) == 0) {
                strncpy(ruleText, propValue(line), sizeof(ruleText) - 1);
                ruleText[sizeof(ruleText) - 1] = '\0';
            } else if (strncmp(line, "STATUS", 6) == 0 && strstr(propValue(line), "CANCELLED") != nullptr) {
                cancelled = true;
            }
        }
        file.close();
        tt_file_remove(TT_CAL_TMP);
    }
    return true;
}

void TTCalendarService::fetch(bool extend) {
    if (WiFi.status() != WL_CONNECTED) {
        LOG_W("CalDAV: need Wi-Fi");
        publish(TT_CAL_NEED_WIFI, "未连接 Wi-Fi", false);
        return;
    }
    char host[TT_CAL_HOST_MAX];
    char user[TT_CAL_USER_MAX];
    char pass[TT_CAL_PASS_MAX];
    if (!loadAccount(host, sizeof(host), user, sizeof(user), pass, sizeof(pass))) {
        publish(TT_CAL_FAILED, "未配置日历账号", false);
        return;
    }
    char auth[160];
    if (!buildAuthHeader(user, pass, auth, sizeof(auth))) {
        publish(TT_CAL_FAILED, "日历账号无效", false);
        return;
    }
    memset(pass, 0, sizeof(pass));

    const time_t now = time(nullptr);
    time_t rangeStart = localMidnight(now);
    time_t rangeEnd = rangeStart + (time_t)TT_CAL_FETCH_DAYS * 86400;
    if (extend && _rangeEnd > _rangeStart) {
        rangeStart = _rangeEnd;
        rangeEnd = rangeStart + (time_t)TT_CAL_FETCH_DAYS * 86400;
    } else {
        extend = false;
        _count = 0;
        _rangeStart = (int32_t)localMidnight(now);
    }
    if (!discover(host, auth)) {
        if (extend && _count > 0) {
            publish(TT_CAL_OK, "", true);
        } else {
            publish(TT_CAL_FAILED, "找不到日历", false);
        }
        return;
    }
    LOG_I("CalDAV: fetch %ld..%ld extend=%d heap=%u",
          (long)rangeStart, (long)rangeEnd, extend ? 1 : 0, (unsigned)ESP.getFreeHeap());
    for (uint8_t i = 0; i < _calCount; i++) {
        const int hrefs = queryHrefs(host, auth, _cals[i], rangeStart, rangeEnd);
        LOG_I("CalDAV: calendar %u hrefs=%d", (unsigned)i, hrefs);
        if (hrefs <= 0) {
            continue;
        }
        pullEvents(host, auth, _cals[i], hrefs, rangeStart, rangeEnd);
        if (_count >= TT_CAL_EVENT_MAX) {
            LOG_W("CalDAV: event cap %u", (unsigned)TT_CAL_EVENT_MAX);
            break;
        }
    }
    _rangeEnd = (int32_t)rangeEnd;
    if (!extend) {
        _rangeStart = (int32_t)localMidnight(now);
    }
    publish(TT_CAL_OK, _count > 0 ? "" : "这7天没有日程", extend);
}
