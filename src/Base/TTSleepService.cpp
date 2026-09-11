#include "TTSleepService.h"
#include "Logger.h"
#include "TTInstance.h"
#include "TTLvglEpdDriver.h"
#include "TTNotificationCenter.h"
#include "TTPopupLayer.h"
#include "TTRtc.h"
#include "../Tasks/TTWiFiTask.h"
#include <WiFi.h>
#include <cstring>
#include <esp_sleep.h>

static const char* tt_sleep_reason_text(TTSleepWakeReason reason) {
    switch (reason) {
        case TT_SLEEP_WAKE_TIME:
            return "time";
        case TT_SLEEP_WAKE_FETCH:
            return "fetch";
        case TT_SLEEP_WAKE_INPUT:
            return "input";
    }
    return "?";
}

static void tt_sleep_format_wall(time_t unixTime, char* out, size_t outMax) {
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

void TTSleepService::requestLightSleep(void* owner) {
    _owner = owner;
    _requested = true;
    _loggedWait = false;
    if (_nextFetchUnix <= 0) {
        refreshFetchDeadline();
    }
    LOG_I("Sleep: requested owner=%p", owner);
}

void TTSleepService::cancelLightSleep(void* owner) {
    if (!_requested) {
        return;
    }
    if (_owner != nullptr && owner != nullptr && _owner != owner) {
        return;
    }
    LOG_I("Sleep: cancelled owner=%p", owner);
    consumeRequest();
}

void TTSleepService::consumeRequest() {
    _requested = false;
    _owner = nullptr;
    _loggedWait = false;
}

void TTSleepService::tryEnter() {
    if (_holdoffAfterInput) {
        _holdoffAfterInput = false;
        return;
    }
    if (!_requested) {
        return;
    }
    auto& wifi = TTInstanceOf<TTWiFiTask>();
    if (isFetchPeriodDue() && !wifi.isRadioActive()) {
        LOG_I("Sleep: fetch period due");
        consumeRequest();
        publishWake(TT_SLEEP_WAKE_FETCH);
        return;
    }
    if (!canEnter()) {
        if (!_loggedWait) {
            LOG_I("Sleep: wait, not idle");
            _loggedWait = true;
        }
        return;
    }

    const time_t deadline = TTInstanceOf<TTRtc>().nextMinuteTick();
    const uint64_t sleepUs = sleepUsUntil(deadline);
    if (sleepUs < TT_SLEEP_MIN_US) {
        LOG_W("Sleep: skip, window %.1f s", sleepUs / 1000000.0);
        return;
    }

    char nowText[TT_SLEEP_WALL_TEXT_MAX];
    char untilText[TT_SLEEP_WALL_TEXT_MAX];
    tt_sleep_format_wall(time(nullptr), nowText, sizeof(nowText));
    tt_sleep_format_wall(deadline, untilText, sizeof(untilText));
    LOG_I("Sleep: enter at %s for %u s until %s",
          nowText[0] != '\0' ? nowText : "?",
          (unsigned)(sleepUs / 1000000ULL),
          untilText[0] != '\0' ? untilText : "?");
    if (!enterSleep(sleepUs)) {
        _loggedWait = false;
        return;
    }
    afterWake();
}

bool TTSleepService::canEnter() const {
    if (TTInstanceOf<TTPopupLayer>().isBusy()) {
        return false;
    }
    if (TTInstanceOf<TTWiFiTask>().isRadioActive() || isFetchPeriodDue()) {
        return false;
    }
    if (WiFi.getMode() != WIFI_OFF) {
        return false;
    }
    return true;
}

uint64_t TTSleepService::sleepUsUntil(time_t deadline) const {
    time_t now = time(nullptr);
    if (now < 0) {
        now = 0;
    }
    if (deadline <= now) {
        return 0;
    }
    return (uint64_t)(deadline - now) * 1000000ULL;
}

bool TTSleepService::enterSleep(uint64_t sleepUs) {
    TTInstanceOf<TTLvglEpdDriver>().hibernate();
    if (WiFi.getMode() != WIFI_OFF) {
        WiFi.mode(WIFI_OFF);
    }

    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    const esp_err_t timerErr = esp_sleep_enable_timer_wakeup(sleepUs);
    if (timerErr != ESP_OK) {
        LOG_E("Sleep: timer wakeup err=%d", (int)timerErr);
        return false;
    }
    const esp_err_t gpioErr = esp_sleep_enable_ext1_wakeup(
        TT_SLEEP_GPIO_WAKE_MASK, ESP_EXT1_WAKEUP_ANY_HIGH);
    if (gpioErr != ESP_OK) {
        LOG_E("Sleep: gpio wakeup err=%d", (int)gpioErr);
        return false;
    }

    Serial.flush();
    const esp_err_t err = esp_light_sleep_start();
    if (err != ESP_OK) {
        LOG_E("Sleep: start err=%d", (int)err);
        return false;
    }
    return true;
}

void TTSleepService::afterWake() {
    consumeRequest();

    if (TTInstanceOf<TTRtc>().hasHardwareRtc()) {
        TTInstanceOf<TTRtc>().loadFromHardware();
    }

    const TTSleepWakeReason reason = classifyWake();
    char nowText[TT_SLEEP_WALL_TEXT_MAX];
    tt_sleep_format_wall(time(nullptr), nowText, sizeof(nowText));
    LOG_I("Sleep: wake %s at %s",
          tt_sleep_reason_text(reason),
          nowText[0] != '\0' ? nowText : "?");
    if (reason == TT_SLEEP_WAKE_INPUT) {
        _holdoffAfterInput = true;
    }
    publishWake(reason);
}

TTSleepWakeReason TTSleepService::classifyWake() const {
    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT1) {
        return TT_SLEEP_WAKE_INPUT;
    }
    if (isFetchPeriodDue()) {
        return TT_SLEEP_WAKE_FETCH;
    }
    return TT_SLEEP_WAKE_TIME;
}

bool TTSleepService::isFetchPeriodDue() const {
    if (_nextFetchUnix <= 0 || !TTInstanceOf<TTRtc>().isTimeValid()) {
        return false;
    }
    const time_t now = time(nullptr);
    return now > 0 && now >= _nextFetchUnix;
}

void TTSleepService::refreshFetchDeadline() {
    if (!TTInstanceOf<TTRtc>().isTimeValid()) {
        _nextFetchUnix = 0;
        return;
    }
    const time_t now = time(nullptr);
    if (now <= 0) {
        _nextFetchUnix = 0;
        return;
    }
    _nextFetchUnix = now + (time_t)(TT_SLEEP_FETCH_PERIOD_MS / 1000u);
    LOG_I("Sleep: next fetch unix=%ld", (long)_nextFetchUnix);
}

void TTSleepService::publishWake(TTSleepWakeReason reason) {
    if (reason == TT_SLEEP_WAKE_FETCH) {
        refreshFetchDeadline();
    }
    TTSleepWakePayload payload;
    payload.reason = reason;
    TTInstanceOf<TTNotificationCenter>().sendNotification(TT_NOTIFICATION_SLEEP_WAKE, payload);
}
