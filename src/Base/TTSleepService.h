#pragma once

#include <ctime>
#include "TTKeypadInput.h"
#include "TTNotificationPayloads.h"

#define TT_SLEEP_MIN_US             200000ULL
#define TT_SLEEP_INPUT_IDLE_MS      10000
#define TT_SLEEP_FETCH_PERIOD_MS    (5 * 60 * 1000)
#define TT_SLEEP_WALL_TEXT_MAX      24
#define TT_SLEEP_GPIO_WAKE_MASK \
    ((1ULL << PIN_BUTTONL) | (1ULL << PIN_BUTTONR) | (1ULL << PIN_BUTTONC))

class TTSleepService {
public:
    void requestLightSleep(void* owner);
    void cancelLightSleep(void* owner);
    void tryEnter();

private:
    bool canEnter() const;
    bool isFetchPeriodDue() const;
    void refreshFetchDeadline();
    uint64_t sleepUsUntil(time_t deadline) const;
    bool enterSleep(uint64_t sleepUs);
    void consumeRequest();
    void afterWake();
    TTSleepWakeReason classifyWake() const;
    void publishWake(TTSleepWakeReason reason);

    void* _owner = nullptr;
    time_t _nextFetchUnix = 0;
    bool _requested = false;
    bool _holdoffAfterInput = false;
    bool _loggedWait = false;
};
