#include "TTClockScreenPage.h"
#include "../Base/Logger.h"
#include "../Base/TTFontManager.h"

void TTClockScreenPage::buildContent() {
    TTFontManager& fm = TTFontManager::instance();
    lv_font_t* font_16 = fm.getFont(16);
    lv_font_t* font_10 = fm.getFont(10);
    lv_font_t* font_12 = fm.getFont(12);
    lv_font_t* font_48 = fm.getFont(48);

    lv_obj_t* scr = getScreen();
    lv_obj_set_style_bg_color(scr, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    _titleLabel = lv_label_create(scr);
    lv_label_set_text(_titleLabel, "电子墨水屏时钟 E-Paper Clock");
    lv_obj_set_style_text_color(_titleLabel, lv_color_black(), 0);
    lv_obj_set_style_text_font(_titleLabel, font_16, 0);
    lv_obj_align(_titleLabel, LV_ALIGN_TOP_MID, 0, 4);

    lv_obj_t* testLabel = lv_label_create(scr);
    lv_label_set_text(testLabel, "Claude Code、Cursor 和 Lovable 等 AI 辅助编程助手让用户几乎无需手动编码就能将其意图转化为可工作的应用。");
    lv_obj_set_style_text_color(testLabel, lv_color_black(), 0);
    lv_obj_set_style_text_font(testLabel, font_10, 0);
    lv_obj_set_width(testLabel, lv_pct(100));
    lv_obj_set_style_pad_left(testLabel, 4, 0);
    lv_obj_set_style_pad_right(testLabel, 4, 0);
    lv_obj_set_style_text_line_space(testLabel, 4, 0);
    lv_obj_align(testLabel, LV_ALIGN_TOP_MID, 0, 24);

    _timeLabel = lv_label_create(scr);
    lv_label_set_text(_timeLabel, "00:00");
    lv_obj_set_style_text_color(_timeLabel, lv_color_black(), 0);
    lv_obj_set_style_text_font(_timeLabel, font_48, 0);
    lv_obj_align(_timeLabel, LV_ALIGN_CENTER, 0, 18);

    _statusLabel = lv_label_create(scr);
    lv_label_set_text(_statusLabel, "正在读取传感器... / Reading sensor...");
    lv_obj_set_style_text_color(_statusLabel, lv_color_black(), 0);
    lv_obj_set_style_text_font(_statusLabel, font_12, 0);
    lv_obj_align(_statusLabel, LV_ALIGN_BOTTOM_MID, 0, -4);

    LOG_I("ClockScreenPage UI created");
}

void TTClockScreenPage::didAppear() {
    _lastUpdateMs = millis();
    updateClockDisplay();
}

void TTClockScreenPage::updateTime() {
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

void TTClockScreenPage::loop() {
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
}

void TTClockScreenPage::onSensorData(float temperature, float humidity, float pressure,
    bool ahtAvailable, bool bmp280Available) {
    _temperature = temperature;
    _humidity = humidity;
    _pressure = pressure;
    _aht20Available = ahtAvailable;
    _bmp280Available = bmp280Available;
    updateSensorDisplay();
    requestRefresh(false);
}

void TTClockScreenPage::updateSensorDisplay() {
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

void TTClockScreenPage::updateClockDisplay() {
    char timeStr[8];
    snprintf(timeStr, sizeof(timeStr), "%02d:%02d", _hours, _minutes);
    lv_label_set_text(_timeLabel, timeStr);
    requestRefresh(false);
}
