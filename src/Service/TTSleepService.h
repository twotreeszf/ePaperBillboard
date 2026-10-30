#pragma once

#include <ctime>
#include "../Base/TTKeypadConfig.h"
#include "../Models/TTNotificationPayloads.h"

#define TT_SLEEP_MIN_US             200000ULL
#define TT_SLEEP_AFTER_SYNC_MS      0
#define TT_SLEEP_AFTER_REFRESH_MS   5000
#define TT_SLEEP_INPUT_IDLE_MS      TT_SLEEP_AFTER_REFRESH_MS
#define TT_SLEEP_WALL_TEXT_MAX      24
#define TT_SLEEP_GPIO_WAKE_MASK \
    ((1ULL << PIN_BUTTONL) | (1ULL << PIN_BUTTONR) | (1ULL << PIN_BUTTONC))
#define TT_SLEEP_UART_TX_GPIO       1
#define TT_SLEEP_UART_RX_GPIO       3

class TTSleepService {
public:
    void begin();
    void requestLightSleep(void* owner);
    void cancelLightSleep(void* owner);
    bool isLightSleepRequested() const { return _requested; }
    bool isRtcPending() const { return _rtcPending; }
    bool takeTimerWake();
    void tryEnter();

private:
    bool canEnter() const;
    uint64_t sleepUsUntil(time_t deadline) const;
    bool enterSleep(uint64_t sleepUs);
    void consumeRequest();
    void afterWake();
    void finishWake(const TTRtcLoadPayload& rtc);
    TTSleepWakeReason classifyWake() const;
    void publishWake(TTSleepWakeReason reason);
    void publishSleepState(TTSleepState state);

    void* _owner = nullptr;
    bool _requested = false;
    bool _holdoffAfterInput = false;
    bool _loggedWait = false;
    bool _rtcPending = false;
    bool _timerWake = false;
};
