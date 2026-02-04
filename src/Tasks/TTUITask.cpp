#include "TTUITask.h"
#include "TTKeypadTask.h"
#include "../Pages/TTClockScreenPage.h"
#include <SPI.h>
#include <LittleFS.h>
#include <memory>
#include "../Base/TTLvglEpdDriver.h"
#include "../Base/TTInstance.h"
#include "../Base/TTPopupLayer.h"
#include "../Base/Logger.h"
#include "../Base/ErrorCheck.h"
#include "../Base/TTFontManager.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

void TTUITask::setup() {
    LOG_I("Initializing SPI (MOSI=%d, SCK=%d)...", TT_UI_EPD_MOSI, TT_UI_EPD_SCK);
    SPI.begin(TT_UI_EPD_SCK, -1, TT_UI_EPD_MOSI, TT_UI_EPD_CS);

    LOG_I("Initializing E-Paper display...");
    _display.init(115200, true, 2, false, SPI, SPISettings(4000000, MSBFIRST, SPI_MODE0));

    ERR_CHECK_FAIL(LittleFS.begin());
    LOG_I("LittleFS initialized");

    ERR_CHECK_FAIL(TTFontManager::instance().begin());

    LOG_I("Initializing LVGL...");
    ERR_CHECK_FAIL(TTInstanceOf<TTLvglEpdDriver>().begin(_display));
    TTInstanceOf<TTPopupLayer>().begin(TTInstanceOf<TTLvglEpdDriver>().getDisplay());

    _nav.setRoot(std::unique_ptr<TTClockScreenPage>(new TTClockScreenPage()));

    lv_display_t* disp = TTInstanceOf<TTLvglEpdDriver>().getDisplay();
    ERR_CHECK_FAIL(_keypad.begin(disp));
    _nav.setKeypadInput(&_keypad);

    TTInstanceOf<TTKeypadTask>().setKeypad(&_keypad);
    TTInstanceOf<TTKeypadTask>().start(0);
    LOG_I("UI task started.");
}

void TTUITask::loop() {
    lv_timer_handler();
    _nav.tick();
    vTaskDelay(pdMS_TO_TICKS(1000));
}
