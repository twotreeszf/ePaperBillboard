#pragma once

#include <lvgl.h>
#include "../Base/TTScreenPage.h"

class TTClockScreenPage : public TTScreenPage {
public:
    TTClockScreenPage() = default;

    void loop() override;

protected:
    void buildContent() override;
    void didAppear() override;
    void didDisappear() override;

private:
    void updateTime();
    void updateSensorDisplay();
    void updateClockDisplay();

    lv_obj_t* _titleLabel = nullptr;
    lv_obj_t* _timeLabel = nullptr;
    lv_obj_t* _statusLabel = nullptr;
    uint8_t _hours = 0;
    uint8_t _minutes = 0;
    uint8_t _seconds = 0;
    unsigned long _lastUpdateMs = 0;
    float _temperature = 0.0f;
    float _humidity = 0.0f;
    float _pressure = 0.0f;
};
