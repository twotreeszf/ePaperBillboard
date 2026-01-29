#pragma once

#include <Arduino.h>
#include <GxEPD2_BW.h>
#include <lvgl.h>
#include "../Base/TTVTask.h"
#include "../Base/TTNavigationController.h"

#define TT_UI_EPD_MOSI  4
#define TT_UI_EPD_SCK   16
#define TT_UI_EPD_CS    17
#define TT_UI_EPD_DC    5
#define TT_UI_EPD_RST   18
#define TT_UI_EPD_BUSY  19

class TTUITask : public TTVTask {
public:
    using EPaperDisplay = GxEPD2_BW<GxEPD2_290, GxEPD2_290::HEIGHT>;

    TTUITask() : TTVTask("TTUITask", 8192),
        _display(GxEPD2_290(TT_UI_EPD_CS, TT_UI_EPD_DC, TT_UI_EPD_RST, TT_UI_EPD_BUSY)) {}

    void submitSensorData(float temperature, float humidity, float pressure,
        bool ahtAvailable, bool bmp280Available);

protected:
    void setup() override;
    void loop() override;

private:
    EPaperDisplay _display;
    TTNavigationController _nav;
};
