#include "TTUITask.h"
#include "../Pages/TTHomePage.h"
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

    lv_display_t* disp = TTInstanceOf<TTLvglEpdDriver>().getDisplay();
    ERR_CHECK_FAIL(_keypad.begin(disp));
    _nav.setKeypadInput(&_keypad);

    _nav.setRoot(std::unique_ptr<TTScreenPage>(new TTHomePage()));

    LOG_I("UI task started.");
}

void TTUITask::requestFullRefreshAsync() {
    auto* f = new std::function<void()>([]() {
        TTInstanceOf<TTLvglEpdDriver>().requestRefresh(true);
    });
    enqueue(f);
}

void TTUITask::loop() {
    _keypad.tick();
    lv_timer_handler();
    vTaskDelay(pdMS_TO_TICKS(TT_UI_LOOP_DELAY_MS));
}
