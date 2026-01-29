#include "TTUITask.h"
#include "../Pages/TTClockScreenPage.h"
#include <SPI.h>
#include <LittleFS.h>
#include <memory>
#include <functional>
#include "../LVGLDriver/TTLVGLDriver.h"
#include "../Base/TTInstance.h"
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
    ERR_CHECK_FAIL(TTInstanceOf<TTLVGLDriver>().begin(_display));

    _nav.setRoot(std::unique_ptr<TTClockScreenPage>(new TTClockScreenPage()));

    LOG_I("UI task started.");
}

void TTUITask::loop() {
    lv_timer_handler();
    _nav.tick();
    vTaskDelay(pdMS_TO_TICKS(1000));
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
