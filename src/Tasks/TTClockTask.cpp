#include "TTClockTask.h"
#include <SPI.h>
#include <LittleFS.h>
#include <functional>
#include "../LvglDriver.h"
#include "../Base/Logger.h"
#include "../Base/ErrorCheck.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

void TTClockTask::updateTime() {
    unsigned long currentMs = millis();
    unsigned long elapsedMs = currentMs - _lastUpdateMs;

    if (elapsedMs >= 1000) {
        uint32_t elapsedSeconds = elapsedMs / 1000;
        _lastUpdateMs = currentMs - (elapsedMs % 1000);

        _seconds += elapsedSeconds;
        while (_seconds >= 60) {
            _seconds -= 60;
            _minutes++;
        }
        while (_minutes >= 60) {
            _minutes -= 60;
            _hours++;
        }
        while (_hours >= 24) {
            _hours -= 24;
        }
    }
}

void TTClockTask::createClockUI() {
    lv_obj_t* scr = lv_scr_act();

    lv_obj_set_style_bg_color(scr, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    _titleLabel = lv_label_create(scr);
    lv_label_set_text(_titleLabel, "电子墨水屏时钟 E-Paper Clock");
    lv_obj_set_style_text_color(_titleLabel, lv_color_black(), 0);
    lv_obj_set_style_text_font(_titleLabel, _font_16.getLvglFont(), 0);
    lv_obj_align(_titleLabel, LV_ALIGN_TOP_MID, 0, 4);

    lv_obj_t* testLabel = lv_label_create(scr);
    lv_label_set_text(testLabel, "Claude Code、Cursor 和 Lovable 等 AI 辅助编程助手让用户几乎无需手动编码就能将其意图转化为可工作的应用。");
    lv_obj_set_style_text_color(testLabel, lv_color_black(), 0);
    lv_obj_set_style_text_font(testLabel, _font_10.getLvglFont(), 0);
    lv_obj_set_width(testLabel, lv_pct(100));
    lv_obj_set_style_pad_left(testLabel, 4, 0);
    lv_obj_set_style_pad_right(testLabel, 4, 0);
    lv_obj_set_style_text_line_space(testLabel, 4, 0);
    lv_obj_align(testLabel, LV_ALIGN_TOP_MID, 0, 24);

    _timeLabel = lv_label_create(scr);
    lv_label_set_text(_timeLabel, "00:00");
    lv_obj_set_style_text_color(_timeLabel, lv_color_black(), 0);
    lv_obj_set_style_text_font(_timeLabel, _font_48.getLvglFont(), 0);
    lv_obj_align(_timeLabel, LV_ALIGN_CENTER, 0, 18);

    _statusLabel = lv_label_create(scr);
    lv_label_set_text(_statusLabel, "正在读取传感器... / Reading sensor...");
    lv_obj_set_style_text_color(_statusLabel, lv_color_black(), 0);
    lv_obj_set_style_text_font(_statusLabel, _font_12.getLvglFont(), 0);
    lv_obj_align(_statusLabel, LV_ALIGN_BOTTOM_MID, 0, -4);

    LOG_I("LVGL UI created with dual fonts");
}

void TTClockTask::submitSensorData(float temperature, float humidity, float pressure,
    bool ahtAvailable, bool bmp280Available) {
    auto* f = new std::function<void()>([this, temperature, humidity, pressure, ahtAvailable, bmp280Available]() {
        _temperature = temperature;
        _humidity = humidity;
        _pressure = pressure;
        _aht20Available = ahtAvailable;
        _bmp280Available = bmp280Available;
        updateSensorDisplay();
        lvglDriver.requestRefresh(false);
    });
    enqueue(f);
}

void TTClockTask::updateSensorDisplay() {
    if (!_aht20Available && !_bmp280Available) {
        lv_label_set_text(_statusLabel, "传感器未连接 / Sensor not found");
        return;
    }

    char sensorStr[96];
    if (_aht20Available && _bmp280Available) {
        snprintf(sensorStr, sizeof(sensorStr),
                 "温度:%.1f℃ | 湿度:%.1f%% | 气压:%.0f hPag",
                 _temperature, _humidity, _pressure);
    }
    lv_label_set_text(_statusLabel, sensorStr);
}

void TTClockTask::updateClockDisplay() {
    char timeStr[8];
    snprintf(timeStr, sizeof(timeStr), "%02d:%02d", _hours, _minutes);

    lv_label_set_text(_timeLabel, timeStr);

    bool needFullRefresh = (_partialRefreshCount >= TT_CLOCK_FULL_REFRESH_INTERVAL);

    if (needFullRefresh) {
        LOG_I("Full screen refresh...");
        _partialRefreshCount = 0;
    } else {
        LOG_I("Partial refresh...");
        _partialRefreshCount++;
    }

    lvglDriver.requestRefresh(needFullRefresh);
}

void TTClockTask::setup() {
    LOG_I("Initializing SPI (MOSI=%d, SCK=%d)...", TT_CLOCK_EPD_MOSI, TT_CLOCK_EPD_SCK);
    SPI.begin(TT_CLOCK_EPD_SCK, -1, TT_CLOCK_EPD_MOSI, TT_CLOCK_EPD_CS);

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

    createClockUI();

    _lastUpdateMs = millis();
    updateClockDisplay();

    LOG_I("Clock started.");
    LOG_I("Partial refresh enabled, full refresh every %d minutes", TT_CLOCK_FULL_REFRESH_INTERVAL);
}

void TTClockTask::loop() {
    updateTime();

    static uint8_t lastMinute = 255;
    bool needRefresh = false;

    if (_minutes != lastMinute) {
        lastMinute = _minutes;
        LOG_I("Time: %02d:%02d:%02d", _hours, _minutes, _seconds);
        needRefresh = true;
    }

    if (needRefresh) {
        updateClockDisplay();
        LOG_I("Display refreshed.");
    }

    lv_timer_handler();

    vTaskDelay(pdMS_TO_TICKS(100));
}
