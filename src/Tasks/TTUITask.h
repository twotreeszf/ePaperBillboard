#pragma once

#include <Arduino.h>
#include <EPDConfig.h>
#include <lvgl.h>
#include "../Base/TTVTask.h"
#include "../Base/TTNavigationController.h"
#include "../Base/TTKeypadInput.h"

#define TT_UI_EPD_MOSI  4
#define TT_UI_EPD_SCK   16
#define TT_UI_EPD_CS    17
#define TT_UI_EPD_DC    5
#define TT_UI_EPD_RST   18
#define TT_UI_EPD_BUSY  19

class TTUITask : public TTVTask {
public:
    TTUITask() : TTVTask("TTUITask", 8192),
        _display(EPD_DRIVER_CLASS(TT_UI_EPD_CS, TT_UI_EPD_DC, TT_UI_EPD_RST, TT_UI_EPD_BUSY)) {}

protected:
    void setup() override;
    void loop() override;

private:
    EPaperDisplay _display;
    TTNavigationController _nav;
    TTKeypadInput _keypad;
};
