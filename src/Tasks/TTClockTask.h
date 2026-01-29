#pragma once

#include <Arduino.h>
#include <GxEPD2_BW.h>
#include <lvgl.h>
#include "../Base/TTVTask.h"
#include "../Base/TTFontLoader.h"

#define TT_CLOCK_EPD_MOSI  4
#define TT_CLOCK_EPD_SCK   16
#define TT_CLOCK_EPD_CS    17
#define TT_CLOCK_EPD_DC    5
#define TT_CLOCK_EPD_RST   18
#define TT_CLOCK_EPD_BUSY  19
#define TT_CLOCK_FULL_REFRESH_INTERVAL  10

class TTClockTask : public TTVTask {
public:
    using EPaperDisplay = GxEPD2_BW<GxEPD2_290, GxEPD2_290::HEIGHT>;

    TTClockTask() : TTVTask("TTClockTask", 8192),
        _display(GxEPD2_290(TT_CLOCK_EPD_CS, TT_CLOCK_EPD_DC, TT_CLOCK_EPD_RST, TT_CLOCK_EPD_BUSY)) {}

    void submitSensorData(float temperature, float humidity, float pressure,
        bool ahtAvailable, bool bmp280Available);

protected:
    void setup() override;
    void loop() override;

private:
    void updateTime();
    void createClockUI();
    void updateSensorDisplay();
    void updateClockDisplay();

    EPaperDisplay _display;
    TTFontLoader _font_16;
    TTFontLoader _font_10;
    TTFontLoader _font_12;
    TTFontLoader _font_48;
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
