#pragma once

#include "../Models/TTCalendarTypes.h"
#include "../Base/TTFile.h"
#include <ctime>

#define TT_CAL_CACHE_PATH        TT_FS_TMP_DIR "/calendar.bin"
#define TT_CAL_CACHE_MAGIC       0x444C4143u
#define TT_CAL_CACHE_VERSION     1
#define TT_CAL_CACHE_MAX_AGE_SEC (30 * 60)

class TTCalendarService {
public:
    bool readTodayCache(char* message, size_t messageMax);
    bool requestFetch(bool extend, bool force = false, bool publishFresh = true);
    uint8_t copyEvents(TTCalEvent* out, uint8_t maxCount) const;
    int32_t rangeStart() const { return _rangeStart; }
    int32_t rangeEnd() const { return _rangeEnd; }

private:
    void publish(TTCalendarState state, const char* message, bool extended);
    void saveCache(const char* host, const char* user, const char* message);
    bool cacheIsFresh(uint32_t fetchedAt) const;
    bool cacheIsToday(uint32_t fetchedAt) const;
    void adoptCache(uint8_t count, int32_t rangeStart, int32_t rangeEnd, const TTCalEvent* events);
    void fetch(bool extend);
    bool loadAccount(char* host, size_t hostMax, char* user, size_t userMax, char* pass, size_t passMax);
    bool discover(const char* host, const char* authHeader, char hrefs[][TT_CAL_HREF_LEN]);
    int queryHrefs(const char* host, const char* authHeader, const char* calendarPath,
                   time_t rangeStart, time_t rangeEnd, char hrefs[][TT_CAL_HREF_LEN]);
    bool pullEvents(const char* host, const char* authHeader, const char* calendarPath,
                    const char hrefs[][TT_CAL_HREF_LEN], int hrefCount,
                    time_t rangeStart, time_t rangeEnd);

    char _accountHost[TT_CAL_HOST_MAX] = {};
    char _accountUser[TT_CAL_USER_MAX] = {};
    char _cals[TT_CAL_CALS_MAX][TT_CAL_PATH_MAX];
    TTCalEvent _events[TT_CAL_EVENT_MAX];
    uint8_t _calCount = 0;
    uint8_t _count = 0;
    int32_t _rangeStart = 0;
    int32_t _rangeEnd = 0;
    bool _discovered = false;
};
