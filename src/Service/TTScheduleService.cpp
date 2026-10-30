#include "TTScheduleService.h"
#include "TTSleepService.h"
#include "../Base/Logger.h"
#include "../Base/TTInstance.h"
#include "../Base/TTNotificationCenter.h"
#include "../Base/TTRtc.h"
#include <cstring>

static void tt_schedule_format_wall(time_t unixTime, char* out, size_t outMax) {
    if (out == nullptr || outMax == 0) {
        return;
    }
    if (unixTime <= 0) {
        out[0] = '\0';
        return;
    }
    struct tm t;
    memset(&t, 0, sizeof(t));
    if (localtime_r(&unixTime, &t) == nullptr) {
        out[0] = '\0';
        return;
    }
    snprintf(out, outMax, "%04d-%02d-%02d %02d:%02d:%02d",
             t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
             t.tm_hour, t.tm_min, t.tm_sec);
}

void TTScheduleService::begin() {
    ensureDeadlines();
    char timeText[TT_SCHEDULE_WALL_TEXT_MAX];
    char fetchText[TT_SCHEDULE_WALL_TEXT_MAX];
    tt_schedule_format_wall(_nextTimeUnix, timeText, sizeof(timeText));
    tt_schedule_format_wall(_nextFetchUnix, fetchText, sizeof(fetchText));
    LOG_I("Schedule: start time=%s fetch=%s",
          timeText[0] != '\0' ? timeText : "-",
          fetchText[0] != '\0' ? fetchText : "-");
}

void TTScheduleService::tick() {
    if (TTInstanceOf<TTSleepService>().isRtcPending()) {
        return;
    }
    const bool timerWake = TTInstanceOf<TTSleepService>().takeTimerWake();
    const bool resumeSleep = timerWake || TTInstanceOf<TTSleepService>().isLightSleepRequested();
    dispatchDue(resumeSleep, timerWake);
}

time_t TTScheduleService::nextDeadline() const {
    time_t deadline = 0;
    if (_nextTimeUnix > 0) {
        deadline = _nextTimeUnix;
    }
    if (_nextFetchUnix > 0 && (deadline <= 0 || _nextFetchUnix < deadline)) {
        deadline = _nextFetchUnix;
    }
    return deadline;
}

void TTScheduleService::ensureDeadlines() {
    if (!TTInstanceOf<TTRtc>().isTimeValid()) {
        _nextTimeUnix = 0;
        _nextFetchUnix = 0;
        return;
    }
    if (_nextTimeUnix <= 0) {
        _nextTimeUnix = TTInstanceOf<TTRtc>().nextMinuteTick();
    }
    if (_nextFetchUnix <= 0) {
        refreshFetchDeadline();
    }
}

void TTScheduleService::setFetchIntervalMin(uint16_t minutes) {
    if (minutes == 0) {
        restoreFetchInterval();
        return;
    }
    if (_fetchIntervalMin == minutes) {
        return;
    }
    _fetchIntervalMin = minutes;
    LOG_I("Schedule: fetch interval %u min", (unsigned)minutes);
    refreshFetchDeadline();
}

void TTScheduleService::restoreFetchInterval() {
    if (_fetchIntervalMin == 0) {
        return;
    }
    _fetchIntervalMin = 0;
    LOG_I("Schedule: fetch interval hourly");
    refreshFetchDeadline();
}

void TTScheduleService::refreshFetchDeadline() {
    if (!TTInstanceOf<TTRtc>().isTimeValid()) {
        _nextFetchUnix = 0;
        return;
    }
    const time_t now = time(nullptr);
    if (now <= 0) {
        _nextFetchUnix = 0;
        return;
    }
    if (_fetchIntervalMin > 0) {
        struct tm slot;
        memset(&slot, 0, sizeof(slot));
        localtime_r(&now, &slot);
        slot.tm_min = ((slot.tm_min / _fetchIntervalMin) + 1) * _fetchIntervalMin;
        slot.tm_sec = TT_RTC_MINUTE_TICK_SEC;
        time_t next = mktime(&slot);
        if (next <= now) {
            next += (time_t)_fetchIntervalMin * 60;
        }
        _nextFetchUnix = next;
        char untilText[TT_SCHEDULE_WALL_TEXT_MAX];
        tt_schedule_format_wall(next, untilText, sizeof(untilText));
        LOG_I("Schedule: next fetch at %s every %u min",
              untilText[0] != '\0' ? untilText : "?", (unsigned)_fetchIntervalMin);
        return;
    }
    struct tm t;
    memset(&t, 0, sizeof(t));
    localtime_r(&now, &t);
    t.tm_min = 0;
    t.tm_sec = TT_RTC_MINUTE_TICK_SEC;
    t.tm_hour += 1;
    time_t next = mktime(&t);
    if (next <= now) {
        next += 3600;
    }
    for (int step = 0; step < 24; step++) {
        struct tm slot;
        memset(&slot, 0, sizeof(slot));
        if (localtime_r(&next, &slot) == nullptr) {
            break;
        }
        const int hour = slot.tm_hour;
        const bool quiet = hour >= TT_SCHEDULE_FETCH_QUIET_START_HOUR
            || hour < TT_SCHEDULE_FETCH_QUIET_END_HOUR;
        if (!quiet) {
            break;
        }
        next += 3600;
    }
    _nextFetchUnix = next;
    char untilText[TT_SCHEDULE_WALL_TEXT_MAX];
    tt_schedule_format_wall(next, untilText, sizeof(untilText));
    LOG_I("Schedule: next fetch at %s", untilText[0] != '\0' ? untilText : "?");
}

void TTScheduleService::dispatchDue(bool resumeSleep, bool timerWake) {
    ensureDeadlines();
    const time_t now = time(nullptr);
    if (now <= 0 || !TTInstanceOf<TTRtc>().isTimeValid()) {
        return;
    }
    if (_nextFetchUnix > 0 && now >= _nextFetchUnix) {
        refreshFetchDeadline();
        publish(TT_SCHEDULE_FETCH, resumeSleep);
        return;
    }
    if (timerWake || (_nextTimeUnix > 0 && now >= _nextTimeUnix)) {
        _nextTimeUnix = TTInstanceOf<TTRtc>().nextMinuteTick();
        publish(TT_SCHEDULE_TIME, resumeSleep);
    }
}

void TTScheduleService::publish(TTScheduleEvent event, bool resumeSleep) {
    LOG_I("Schedule: %s resume=%d", event == TT_SCHEDULE_FETCH ? "fetch" : "time", resumeSleep ? 1 : 0);
    TTSchedulePayload payload;
    payload.event = event;
    payload.resumeSleep = resumeSleep;
    TTInstanceOf<TTNotificationCenter>().sendNotification(TT_NOTIFICATION_SCHEDULE, payload);
}
