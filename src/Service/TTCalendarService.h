#pragma once

#include "../Base/TTCalendarTypes.h"
#include <ctime>

class TTCalendarService {
public:
    void requestFetch(bool extend);
    uint8_t copyEvents(TTCalEvent* out, uint8_t maxCount) const;

private:
    void publish(TTCalendarState state, const char* message, bool extended);
    void fetch(bool extend);
    bool loadAccount(char* host, size_t hostMax, char* user, size_t userMax, char* pass, size_t passMax);
    bool discover(const char* host, const char* authHeader);
    int queryHrefs(const char* host, const char* authHeader, const char* calendarPath,
                   time_t rangeStart, time_t rangeEnd);
    bool pullEvents(const char* host, const char* authHeader, const char* calendarPath,
                    int hrefCount, time_t rangeStart, time_t rangeEnd);

    char _accountHost[TT_CAL_HOST_MAX] = {};
    char _accountUser[TT_CAL_USER_MAX] = {};
    char _cals[TT_CAL_CALS_MAX][TT_CAL_PATH_MAX];
    char _hrefs[TT_CAL_HREF_MAX][TT_CAL_HREF_LEN];
    TTCalEvent _events[TT_CAL_EVENT_MAX];
    uint8_t _calCount = 0;
    uint8_t _count = 0;
    int32_t _rangeStart = 0;
    int32_t _rangeEnd = 0;
    bool _discovered = false;
};
