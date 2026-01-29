#include "TTUITask.h"
#include "TTClockScreenPage.h"
#include <SPI.h>
#include <LittleFS.h>
#include <memory>
#include <functional>
#include "../LvglDriver.h"
#include "../Base/Logger.h"
#include "../Base/ErrorCheck.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

void TTUITask::setup() {
    LOG_I("Initializing SPI (MOSI=%d, SCK=%d)...", TT_UI_EPD_MOSI, TT_UI_EPD_SCK);
    SPI.begin(TT_UI_EPD_SCK, -1, TT_UI_EPD_MOSI, TT_UI_EPD_CS);

    LOG_I("Initializing E-Paper display...");
    _display.init(115200, true, 2, false, SPI, SPISettings(4000000, MSBFIRST, SPI_MODE0));

    ERR_CHECK_FAIL(LittleFS.begin());
    LOG_I("LittleFS initialized");

    if (_font_10.begin("/fonts/all_10.bin")) {
        LOG_I("12px font loaded (CHS + EN)");
    } else {
        LOG_W("Failed to load 12px font");
    }
    if (_font_12.begin("/fonts/all_12.bin")) {
        LOG_I("14px font loaded (CHS + EN)");
    } else {
        LOG_W("Failed to load 14px font");
    }
    if (_font_16.begin("/fonts/all_16.bin")) {
        LOG_I("16px font loaded (CHS + EN)");
    } else {
        LOG_W("Failed to load 16px font");
    }
    if (_font_48.begin("/fonts/en_48.bin")) {
        LOG_I("48px clock font loaded");
    } else {
        LOG_W("Failed to load 48px clock font");
    }

    LOG_I("Initializing LVGL...");
    ERR_CHECK_FAIL(lvglDriver.begin(_display));

    _nav.setRoot(std::unique_ptr<TTClockScreenPage>(
        new TTClockScreenPage(&_font_16, &_font_10, &_font_12, &_font_48)));

    LOG_I("UI task started.");
}

void TTUITask::loop() {
    lv_timer_handler();
    _nav.tick();
    vTaskDelay(pdMS_TO_TICKS(100));
}

void TTUITask::submitSensorData(float temperature, float humidity, float pressure,
    bool ahtAvailable, bool bmp280Available) {
    auto* f = new std::function<void()>([this, temperature, humidity, pressure, ahtAvailable, bmp280Available]() {
        TTScreenPage* p = _nav.getCurrentPage();
        if (p != nullptr) {
            p->onSensorData(temperature, humidity, pressure, ahtAvailable, bmp280Available);
        }
    });
    enqueue(f);
}
