#pragma once

#include <ctime>
#include "../Models/TTNotificationPayloads.h"

#define TT_SCHEDULE_FETCH_QUIET_START_HOUR  22
#define TT_SCHEDULE_FETCH_QUIET_END_HOUR    6
#define TT_SCHEDULE_WALL_TEXT_MAX           24

class TTScheduleService {
public:
    void begin();
    void tick();
    time_t nextDeadline() const;

private:
    void ensureDeadlines();
    void refreshFetchDeadline();
    void dispatchDue(bool resumeSleep, bool timerWake);
    void publish(TTScheduleEvent event, bool resumeSleep);

    time_t _nextTimeUnix = 0;
    time_t _nextFetchUnix = 0;
};
