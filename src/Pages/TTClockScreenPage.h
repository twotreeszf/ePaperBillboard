#pragma once

#include <lvgl.h>
#include "../Base/TTScreenPage.h"

#define TT_CLOCK_TIMER_MS  1000

class TTClockScreenPage : public TTScreenPage {
public:
    TTClockScreenPage() : TTScreenPage("时钟") {}

    void setup() override;
    void willAppear() override;

protected:
    void buildContent(lv_obj_t* screen) override;

private:
    void onTimerTick();
    void updateClockDisplay();

    lv_obj_t* _titleLabel = nullptr;
    lv_obj_t* _timeLabel = nullptr;
    int _lastMinute = -1;
};
