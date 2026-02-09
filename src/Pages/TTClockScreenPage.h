#pragma once

#include <lvgl.h>
#include "../Base/TTScreenPage.h"

#define TT_CLOCK_TIMER_MS  1000

class TTClockScreenPage : public TTScreenPage {
public:
    TTClockScreenPage() = default;

    void setup() override;
    void willDestroy() override;

protected:
    void buildContent(lv_obj_t* screen) override;

private:
    static void onBackClicked(lv_event_t* e);
    static void timerCb(lv_timer_t* t);
    void onTimerTick();
    void updateTime();
    void updateSensorDisplay(float temperature, float humidity, float pressure);
    void updateClockDisplay();

    lv_timer_t* _timer = nullptr;
    lv_obj_t* _btnBack = nullptr;
    lv_obj_t* _titleLabel = nullptr;
    lv_obj_t* _timeLabel = nullptr;
    lv_obj_t* _statusLabel = nullptr;
    uint8_t _hours = 0;
    uint8_t _minutes = 0;
    uint8_t _seconds = 0;
    unsigned long _lastUpdateMs = 0;
};
