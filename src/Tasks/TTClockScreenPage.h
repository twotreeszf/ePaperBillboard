#pragma once

#include <lvgl.h>
#include "../Base/TTScreenPage.h"
#include "../Base/TTFontLoader.h"

#define TT_CLOCK_FULL_REFRESH_INTERVAL  10

class TTClockScreenPage : public TTScreenPage {
public:
    TTClockScreenPage(TTFontLoader* font_16, TTFontLoader* font_10,
        TTFontLoader* font_12, TTFontLoader* font_48);

    void loop() override;
    void onSensorData(float temperature, float humidity, float pressure,
        bool ahtAvailable, bool bmp280Available) override;

protected:
    void buildContent() override;
    void didAppear() override;

private:
    void updateTime();
    void updateSensorDisplay();
    void updateClockDisplay();

    TTFontLoader* _font_16;
    TTFontLoader* _font_10;
    TTFontLoader* _font_12;
    TTFontLoader* _font_48;
    lv_obj_t* _titleLabel = nullptr;
    lv_obj_t* _timeLabel = nullptr;
    lv_obj_t* _statusLabel = nullptr;
    uint8_t _hours = 0;
    uint8_t _minutes = 0;
    uint8_t _seconds = 0;
    unsigned long _lastUpdateMs = 0;
    uint8_t _partialRefreshCount = 0;
    float _temperature = 0.0f;
    float _humidity = 0.0f;
    float _pressure = 0.0f;
    bool _aht20Available = false;
    bool _bmp280Available = false;
};
