#include "TTSleepService.h"
#include "TTScheduleService.h"
#include "../Base/Logger.h"
#include "../Base/TTInstance.h"
#include "../Base/TTLvglEpdDriver.h"
#include "../Models/TTNotificationPayloads.h"
#include "../Base/TTNotificationCenter.h"
#include "../Base/TTPopupLayer.h"
#include "../Base/TTRtc.h"
#include "../Tasks/TTWiFiTask.h"
#include "../Tasks/TTSensorTask.h"
#include "../Tasks/TTUITask.h"
#include <Arduino.h>
#include <cstring>
#include <driver/gpio.h>
#include <esp_sleep.h>

static const char* tt_sleep_reason_text(TTSleepWakeReason reason) {
    switch (reason) {
        case TT_SLEEP_WAKE_TIME:
            return "time";
        case TT_SLEEP_WAKE_FETCH:
            return "fetch";
        case TT_SLEEP_WAKE_INPUT:
            return "input";
        case TT_SLEEP_WAKE_POWER:
            return "power";
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

void TTSleepService::begin() {
    TTInstanceOf<TTNotificationCenter>().subscribe<TTRtcLoadPayload>(
        TT_NOTIFICATION_RTC_LOAD,
        this,
        [this](const TTRtcLoadPayload& rtc) {
            finishWake(rtc);
        });
    TTInstanceOf<TTNotificationCenter>().subscribe<TTSensorDataPayload>(
        TT_NOTIFICATION_SENSOR_DATA_UPDATE,
        this,
        [this](const TTSensorDataPayload& data) {
            onSensor(data);
        });
}

void TTSleepService::requestLightSleep(void* owner) {
    _owner = owner;
    _requested = true;
    _loggedWait = false;
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

bool TTSleepService::takeTimerWake() {
    const bool wake = _timerWake;
    _timerWake = false;
    return wake;
}

void TTSleepService::tryEnter() {
    if (_holdoffAfterInput) {
        _holdoffAfterInput = false;
        return;
    }
    if (!_requested || _rtcPending) {
        return;
    }
    if (!canEnter()) {
        if (!_loggedWait) {
            LOG_I("Sleep: wait, %s", _externalPower ? "external power" : "not idle");
            _loggedWait = true;
        }
        return;
    }

    time_t deadline = TTInstanceOf<TTScheduleService>().nextDeadline();
    if (deadline <= 0) {
        deadline = TTInstanceOf<TTRtc>().nextMinuteTick();
    }
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
    TTKeypadInput& keypad = TTInstanceOf<TTUITask>().keypadInput();
    keypad.suspend();
    const bool slept = enterSleep(sleepUs);
    keypad.resume();
    if (!slept) {
        _loggedWait = false;
        return;
    }
    afterWake();
}

void TTSleepService::onSensor(const TTSensorDataPayload& data) {
    const bool powered = data.charging || data.usbPlugged;
    if (_powerKnown && powered == _externalPower) {
        return;
    }
    _powerKnown = true;
    _externalPower = powered;
    _loggedWait = false;
    LOG_I("Sleep: external power=%d charging=%d usb=%d",
          powered ? 1 : 0, data.charging ? 1 : 0, data.usbPlugged ? 1 : 0);
}

bool TTSleepService::canEnter() const {
    if (_externalPower) {
        return false;
    }
    if (TTInstanceOf<TTPopupLayer>().isBusy()) {
        return false;
    }
    if (TTInstanceOf<TTWiFiTask>().isRadioActive()) {
        return false;
    }
    if (!TTInstanceOf<TTWiFiTask>().isRadioParked()) {
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

static void tt_sleep_hold_uart_pins() {
    const gpio_num_t pins[] = {
        (gpio_num_t)TT_SLEEP_UART_TX_GPIO,
        (gpio_num_t)TT_SLEEP_UART_RX_GPIO,
    };
    for (size_t i = 0; i < sizeof(pins) / sizeof(pins[0]); i++) {
        const gpio_num_t pin = pins[i];
        esp_err_t err = gpio_sleep_set_direction(pin, GPIO_MODE_INPUT);
        if (err != ESP_OK) {
            LOG_W("Sleep: UART GPIO%d sleep dir err=%d", (int)pin, (int)err);
            continue;
        }
        err = gpio_sleep_set_pull_mode(pin, GPIO_FLOATING);
        if (err != ESP_OK) {
            LOG_W("Sleep: UART GPIO%d sleep pull err=%d", (int)pin, (int)err);
        }
        err = gpio_sleep_sel_en(pin);
        if (err != ESP_OK) {
            LOG_W("Sleep: UART GPIO%d sleep sel err=%d", (int)pin, (int)err);
        }
    }
}

bool TTSleepService::enterSleep(uint64_t sleepUs) {
    publishSleepState(TT_SLEEP_STATE_SLEEPING);
    TTInstanceOf<TTLvglEpdDriver>().requestOverlayRefresh();
    TTInstanceOf<TTLvglEpdDriver>().hibernate();
    if (!TTInstanceOf<TTWiFiTask>().isRadioParked()) {
        TTInstanceOf<TTWiFiTask>().parkRadio();
    }

    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    const esp_err_t timerErr = esp_sleep_enable_timer_wakeup(sleepUs);
    if (timerErr != ESP_OK) {
        LOG_E("Sleep: timer wakeup err=%d", (int)timerErr);
        publishSleepState(TT_SLEEP_STATE_AWAKE);
        TTInstanceOf<TTLvglEpdDriver>().requestOverlayRefresh();
        return false;
    }
    const esp_err_t gpioErr = esp_sleep_enable_ext1_wakeup(
        TT_SLEEP_GPIO_WAKE_MASK, ESP_EXT1_WAKEUP_ANY_HIGH);
    if (gpioErr != ESP_OK) {
        LOG_E("Sleep: gpio wakeup err=%d", (int)gpioErr);
        publishSleepState(TT_SLEEP_STATE_AWAKE);
        TTInstanceOf<TTLvglEpdDriver>().requestOverlayRefresh();
        return false;
    }
    const int chargeLevel = digitalRead(TT_BATTERY_CHARGE_PIN) == HIGH ? 1 : 0;
    const int ext0Level = chargeLevel ? 0 : 1;
    const esp_err_t ext0Err = esp_sleep_enable_ext0_wakeup(
        (gpio_num_t)TT_BATTERY_CHARGE_PIN, ext0Level);
    if (ext0Err != ESP_OK) {
        LOG_E("Sleep: ext0 wakeup err=%d", (int)ext0Err);
        publishSleepState(TT_SLEEP_STATE_AWAKE);
        TTInstanceOf<TTLvglEpdDriver>().requestOverlayRefresh();
        return false;
    }
    LOG_I("Sleep: ext0 GPIO%d wake on %d", TT_BATTERY_CHARGE_PIN, ext0Level);

    Serial.flush();
    fflush(stdout);
    tt_sleep_hold_uart_pins();
    const esp_err_t err = esp_light_sleep_start();
    if (err != ESP_OK) {
        LOG_E("Sleep: start err=%d", (int)err);
        publishSleepState(TT_SLEEP_STATE_AWAKE);
        TTInstanceOf<TTLvglEpdDriver>().requestOverlayRefresh();
        return false;
    }
    return true;
}

void TTSleepService::afterWake() {
    consumeRequest();
    _rtcPending = true;
    LOG_I("Sleep: wake, wait rtc load");
    TTInstanceOf<TTSensorTask>().requestRtcLoadAsync();

    publishSleepState(TT_SLEEP_STATE_AWAKE);
    TTInstanceOf<TTLvglEpdDriver>().requestOverlayRefresh();
}

void TTSleepService::finishWake(const TTRtcLoadPayload& rtc) {
    if (!_rtcPending) {
        return;
    }
    _rtcPending = false;
    const TTSleepWakeReason reason = classifyWake();
    char nowText[TT_SLEEP_WALL_TEXT_MAX];
    tt_sleep_format_wall(time(nullptr), nowText, sizeof(nowText));
    LOG_I("Sleep: wake %s at %s rtc=%d",
          tt_sleep_reason_text(reason),
          nowText[0] != '\0' ? nowText : "?",
          rtc.loaded ? 1 : 0);
    if (reason == TT_SLEEP_WAKE_INPUT) {
        _holdoffAfterInput = true;
        publishWake(reason);
        return;
    }
    if (reason == TT_SLEEP_WAKE_POWER) {
        TTInstanceOf<TTSensorTask>().requestSensorUpdateAsync();
        publishWake(reason);
        return;
    }
    _timerWake = true;
    LOG_I("Sleep: timer wake, schedule resumes");
}

TTSleepWakeReason TTSleepService::classifyWake() const {
    const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    if (cause == ESP_SLEEP_WAKEUP_EXT1) {
        return TT_SLEEP_WAKE_INPUT;
    }
    if (cause == ESP_SLEEP_WAKEUP_EXT0) {
        return TT_SLEEP_WAKE_POWER;
    }
    return TT_SLEEP_WAKE_TIME;
}

void TTSleepService::publishWake(TTSleepWakeReason reason) {
    TTSleepWakePayload payload;
    payload.reason = reason;
    TTInstanceOf<TTNotificationCenter>().sendNotification(TT_NOTIFICATION_SLEEP_WAKE, payload);
}

void TTSleepService::publishSleepState(TTSleepState state) {
    TTSleepStatePayload payload;
    payload.state = state;
    LOG_I("Sleep: state=%s", state == TT_SLEEP_STATE_SLEEPING ? "sleep" : "awake");
    TTInstanceOf<TTNotificationCenter>().sendNotification(TT_NOTIFICATION_SLEEP_STATE, payload);
}
