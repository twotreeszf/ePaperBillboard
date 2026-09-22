#pragma once

#include <stdint.h>

#define PREF_CALDAV_USER   "caldav_user"
#define PREF_CALDAV_PASS   "caldav_pass"
#define PREF_CALDAV_HOST   "caldav_host"

#define TT_CAL_HOST_MAX        48
#define TT_CAL_USER_MAX        32
#define TT_CAL_PASS_MAX        32
#define TT_CAL_PATH_MAX        64
#define TT_CAL_CALS_MAX        8
#define TT_CAL_HREF_MAX        24
#define TT_CAL_HREF_LEN        256
#define TT_CAL_EVENT_MAX       40
#define TT_CAL_TITLE_MAX       60
#define TT_CAL_FETCH_DAYS      7
#define TT_CAL_BODY_MAX        32768
#define TT_CAL_MULTIGET_BATCH  2
#define TT_CAL_MSG_MAX         32
#define TT_CAL_TMP             "/caldav_resp"

enum TTCalendarState {
    TT_CAL_IDLE = 0,
    TT_CAL_NEED_WIFI,
    TT_CAL_FETCHING,
    TT_CAL_OK,
    TT_CAL_FAILED,
};

struct TTCalEvent {
    int32_t startUnix;
    int32_t endUnix;
    uint8_t allDay;
    char title[TT_CAL_TITLE_MAX];
};

struct TTCalendarPayload {
    TTCalendarState state;
    char message[TT_CAL_MSG_MAX];
    int32_t rangeStart;
    int32_t rangeEnd;
    uint8_t count;
    uint8_t extended;
};
