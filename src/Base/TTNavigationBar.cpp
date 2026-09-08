#include "TTNavigationBar.h"
#include "ITTNavigationController.h"
#include "TTFontManager.h"
#include "TTStreamImage.h"
#include "TTInstance.h"
#include "TTLvglEpdDriver.h"
#include "TTRtc.h"
#include "TTNotificationCenter.h"
#include "Logger.h"
#include "../Tasks/TTUITask.h"
#include "../Tasks/TTWiFiTask.h"
#include "../Tasks/TTSensorTask.h"
#include <EPDConfig.h>
#include <cstdio>
#include <cmath>

void TTNavigationBar::begin(lv_obj_t* parent, ITTNavigationController* nav) {
    if (_bar != nullptr) return;
    if (parent == nullptr) {
        LOG_E("NavBar: begin failed, parent is null");
        return;
    }

    _nav = nav;

    lv_font_t* font = TTFontManager::instance().getFont(TT_NAV_BAR_FONT);

    _bar = lv_obj_create(parent);
    lv_obj_add_flag(_bar, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_add_flag(_bar, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_size(_bar, EPD_WIDTH, TT_NAV_BAR_HEIGHT);
    lv_obj_align(_bar, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(_bar, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_bar, 0, 0);
    lv_obj_set_style_pad_all(_bar, 0, 0);
    lv_obj_set_style_radius(_bar, 0, 0);
    lv_obj_remove_flag(_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(_bar, LV_OBJ_FLAG_HIDDEN);

    _backBtn = lv_btn_create(_bar);
    lv_obj_set_size(_backBtn, TT_NAV_ARROW_W, TT_NAV_BAR_HEIGHT - TT_NAV_DIVIDER_H);
    lv_obj_align(_backBtn, LV_ALIGN_LEFT_MID, TT_NAV_BAR_PAD, -(TT_NAV_DIVIDER_H / 2));
    lv_obj_set_style_bg_opa(_backBtn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(_backBtn, LV_OPA_TRANSP, LV_STATE_FOCUS_KEY);
    lv_obj_set_style_border_width(_backBtn, 0, 0);
    lv_obj_set_style_outline_width(_backBtn, 0, 0);
    lv_obj_set_style_shadow_width(_backBtn, 0, 0);
    lv_obj_set_style_pad_all(_backBtn, 0, 0);
    lv_obj_set_style_radius(_backBtn, 0, 0);
    lv_obj_add_event_cb(_backBtn, onBackClicked, LV_EVENT_CLICKED, this);

    lv_obj_t* arrow = tt_stream_image_create(_backBtn);
    tt_stream_image_set_src(arrow, TT_NAV_BACK_ICON);
    lv_obj_center(arrow);

    _title = lv_label_create(_bar);
    lv_label_set_text(_title, "");
    lv_obj_set_style_text_color(_title, lv_color_black(), 0);
    lv_obj_set_style_text_font(_title, font, 0);
    lv_obj_set_style_text_align(_title, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_long_mode(_title, LV_LABEL_LONG_DOT);

    beginStatus(TTFontManager::instance().getFont(TT_NAV_STATUS_FONT));

    lv_obj_t* divider = lv_obj_create(_bar);
    lv_obj_set_size(divider, EPD_WIDTH, TT_NAV_DIVIDER_H);
    lv_obj_align(divider, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_bg_color(divider, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(divider, 0, 0);
    lv_obj_set_style_pad_all(divider, 0, 0);
    lv_obj_set_style_radius(divider, 0, 0);

    updateTime(false);
    subscribeStatus();
    TTInstanceOf<TTUITask>().runRepeat(TT_NAV_STATUS_CLOCK_MS, [this]() { onClockTick(); }, false);
    TTInstanceOf<TTWiFiTask>().requestStatusAsync();
    TTInstanceOf<TTSensorTask>().requestSensorUpdateAsync();
    LOG_I("NavBar: created");
}

void TTNavigationBar::beginStatus(lv_font_t* font) {
    _statusRow = lv_obj_create(_bar);
    lv_obj_set_size(_statusRow, LV_SIZE_CONTENT, TT_NAV_BAR_HEIGHT - TT_NAV_DIVIDER_H);
    lv_obj_align(_statusRow, LV_ALIGN_RIGHT_MID, -TT_NAV_BAR_PAD, -(TT_NAV_DIVIDER_H / 2));
    lv_obj_set_style_bg_opa(_statusRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(_statusRow, 0, 0);
    lv_obj_set_style_pad_all(_statusRow, 0, 0);
    lv_obj_set_style_radius(_statusRow, 0, 0);
    lv_obj_set_layout(_statusRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(_statusRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(_statusRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(_statusRow, TT_NAV_STATUS_GAP, 0);
    lv_obj_remove_flag(_statusRow, LV_OBJ_FLAG_SCROLLABLE);

    _timeLabel = createValue(_statusRow, font, "--/-- --:--");
    createWifiStatus(_statusRow, font);
    _tempLabel = createValue(_statusRow, font, "温--.-℃");
    _humLabel = createValue(_statusRow, font, "湿--%");
    _pressLabel = createValue(_statusRow, font, "压----p");
    createBatteryStatus(_statusRow, font);
}

void TTNavigationBar::createWifiStatus(lv_obj_t* parent, lv_font_t* font) {
    lv_obj_t* group = lv_obj_create(parent);
    lv_obj_set_size(group, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(group, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(group, 0, 0);
    lv_obj_set_style_pad_all(group, 0, 0);
    lv_obj_set_style_radius(group, 0, 0);
    lv_obj_set_layout(group, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(group, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(group, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(group, TT_NAV_WIFI_ICON_GAP, 0);
    lv_obj_remove_flag(group, LV_OBJ_FLAG_SCROLLABLE);

    _wifiLabel = createValue(group, font, TT_NAV_WIFI_TEXT);
    lv_obj_set_style_pad_all(_wifiLabel, 0, 0);
    lv_obj_set_height(_wifiLabel, lv_font_get_line_height(font));

    _wifiIcon = createIcon(group, TT_NAV_ICON_X);
    lv_obj_set_style_pad_all(_wifiIcon, 0, 0);
    lv_obj_set_style_translate_y(_wifiIcon, TT_NAV_WIFI_ICON_Y, 0);
}

void TTNavigationBar::createBatteryStatus(lv_obj_t* parent, lv_font_t* font) {
    lv_obj_t* group = lv_obj_create(parent);
    lv_obj_set_size(group, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(group, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(group, 0, 0);
    lv_obj_set_style_pad_all(group, 0, 0);
    lv_obj_set_style_radius(group, 0, 0);
    lv_obj_set_layout(group, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(group, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(group, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(group, TT_NAV_WIFI_ICON_GAP, 0);
    lv_obj_remove_flag(group, LV_OBJ_FLAG_SCROLLABLE);

    _batteryIcon = createIcon(group, TT_NAV_ICON_BATTERY_EMPTY, TT_NAV_BATTERY_ICON_W, TT_NAV_BATTERY_ICON_H);
    lv_obj_set_style_pad_all(_batteryIcon, 0, 0);
    lv_obj_set_style_translate_y(_batteryIcon, TT_NAV_WIFI_ICON_Y, 0);

    _batteryLabel = createValue(group, font, "--%");
    lv_obj_set_style_pad_all(_batteryLabel, 0, 0);
    lv_obj_set_height(_batteryLabel, lv_font_get_line_height(font));
}

lv_obj_t* TTNavigationBar::createIcon(lv_obj_t* parent, const char* path, int32_t width, int32_t height) {
    lv_obj_t* icon = tt_stream_image_create(parent);
    tt_stream_image_set_src(icon, path);
    lv_obj_set_size(icon, width, height < 0 ? width : height);
    return icon;
}

lv_obj_t* TTNavigationBar::createValue(lv_obj_t* parent, lv_font_t* font, const char* text) {
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_black(), 0);
    lv_obj_set_style_text_font(label, font, 0);
    return label;
}

void TTNavigationBar::subscribeStatus() {
    TTNotificationCenter& nc = TTInstanceOf<TTNotificationCenter>();
    nc.subscribe<TTWiFiStatusPayload>(
        TT_NOTIFICATION_WIFI_STATUS,
        this,
        [this](const TTWiFiStatusPayload& status) {
            applyWiFi(status);
        });
    nc.subscribe<TTSensorDataPayload>(
        TT_NOTIFICATION_SENSOR_DATA_UPDATE,
        this,
        [this](const TTSensorDataPayload& data) {
            applySensor(data);
        });
}

void TTNavigationBar::show(const char* title, bool showBack) {
    if (_bar == nullptr) return;
    char titled[TT_NAV_TITLE_MAX];
    snprintf(titled, sizeof(titled), "[%s]", title != nullptr ? title : "");
    lv_label_set_text(_title, titled);
    if (showBack) {
        lv_obj_remove_flag(_backBtn, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(_backBtn, LV_OBJ_FLAG_HIDDEN);
    }
    layoutTitle(showBack);
    lv_obj_remove_flag(_bar, LV_OBJ_FLAG_HIDDEN);
    _visible = true;
    LOG_I("NavBar: show title=%s back=%d", title != nullptr ? title : "", showBack ? 1 : 0);
}

void TTNavigationBar::layoutTitle(bool showBack) {
    if (_title == nullptr) return;

    if (showBack) {
        lv_obj_align_to(_title, _backBtn, LV_ALIGN_OUT_RIGHT_MID, TT_NAV_BAR_PAD, 0);
    } else {
        lv_obj_align(_title, LV_ALIGN_LEFT_MID, TT_NAV_BAR_PAD, -(TT_NAV_DIVIDER_H / 2));
    }

    if (_statusRow != nullptr) {
        lv_obj_update_layout(_statusRow);
        const int32_t left = showBack
            ? (TT_NAV_BAR_PAD + TT_NAV_ARROW_W + TT_NAV_BAR_PAD)
            : TT_NAV_BAR_PAD;
        const int32_t statusW = lv_obj_get_width(_statusRow);
        int32_t maxTitle = EPD_WIDTH - left - statusW - TT_NAV_BAR_PAD * 2;
        if (maxTitle < 24) {
            maxTitle = 24;
        }
        lv_obj_set_width(_title, maxTitle);
    }
}

void TTNavigationBar::hide() {
    if (_bar == nullptr) return;
    lv_obj_add_flag(_bar, LV_OBJ_FLAG_HIDDEN);
    _visible = false;
    LOG_I("NavBar: hide");
}

void TTNavigationBar::onBackClicked(lv_event_t* e) {
    TTNavigationBar* self = (TTNavigationBar*)lv_event_get_user_data(e);
    if (self == nullptr || self->_nav == nullptr) return;
    LOG_I("NavBar: back clicked");
    self->_nav->pop();
}

void TTNavigationBar::onClockTick() {
    updateTime(true);
}

void TTNavigationBar::applyWiFi(const TTWiFiStatusPayload& status) {
    if (_wifiIcon == nullptr) return;
    if (_wifiState == status.state) {
        return;
    }
    _wifiState = status.state;
    tt_stream_image_set_src(_wifiIcon,
                            status.state == TT_WIFI_LINK_CONNECTED ? TT_NAV_ICON_CHECK : TT_NAV_ICON_X);
    LOG_I("NavBar: wifi state=%d", (int)status.state);
    requestRedraw();
}

void TTNavigationBar::applySensor(const TTSensorDataPayload& data) {
    if (_tempLabel == nullptr || _humLabel == nullptr || _pressLabel == nullptr
        || _batteryIcon == nullptr || _batteryLabel == nullptr) {
        return;
    }
    TTSensorDataPayload prev = {};
    prev.voltageMv = _batteryMv;
    prev.percent = _batteryPercent;
    prev.charging = _batteryCharging;
    prev.usbPlugged = _batteryUsb;
    const int percentDelta = (int)data.percent - (int)_batteryPercent;
    const int percentAbs = percentDelta < 0 ? -percentDelta : percentDelta;
    if (_hasSensor
        && fabsf(data.temperature - _temperature) < 0.05f
        && fabsf(data.humidity - _humidity) < 0.05f
        && fabsf(data.pressure - _pressure) < 0.5f
        && data.charging == _batteryCharging
        && data.usbPlugged == _batteryUsb
        && percentAbs < TT_NAV_BATTERY_PERCENT_DEADBAND
        && batteryIconPath(data) == batteryIconPath(prev)) {
        return;
    }
    _hasSensor = true;
    _temperature = data.temperature;
    _humidity = data.humidity;
    _pressure = data.pressure;
    _batteryMv = data.voltageMv;
    _batteryPercent = data.percent;
    _batteryCharging = data.charging;
    _batteryUsb = data.usbPlugged;

    char text[24];
    snprintf(text, sizeof(text), "温%.1f℃", _temperature);
    lv_label_set_text(_tempLabel, text);
    snprintf(text, sizeof(text), "湿%.0f%%", _humidity);
    lv_label_set_text(_humLabel, text);
    snprintf(text, sizeof(text), "压%.0fp", _pressure);
    lv_label_set_text(_pressLabel, text);
    tt_stream_image_set_src(_batteryIcon, batteryIconPath(data));
    snprintf(text, sizeof(text), "%u%%", (unsigned)_batteryPercent);
    lv_label_set_text(_batteryLabel, text);
    LOG_I("NavBar: sensor T=%.1f H=%.1f P=%.0f bat=%dmV %u%% usb=%d charging=%d",
          _temperature, _humidity, _pressure,
          _batteryMv, (unsigned)_batteryPercent, _batteryUsb ? 1 : 0, _batteryCharging ? 1 : 0);
    if (_visible) {
        layoutTitle(!lv_obj_has_flag(_backBtn, LV_OBJ_FLAG_HIDDEN));
    }
    requestRedraw();
}

const char* TTNavigationBar::batteryIconPath(const TTSensorDataPayload& data) const {
    if (data.charging) {
        return TT_NAV_ICON_BATTERY_CHARGE;
    }
    if (data.usbPlugged) {
        return TT_NAV_ICON_BATTERY_USB;
    }
    if (data.voltageMv < TT_BATTERY_EMPTY_MV) {
        return TT_NAV_ICON_BATTERY_EMPTY;
    }
    if (data.voltageMv < TT_BATTERY_LOW_MV) {
        return TT_NAV_ICON_BATTERY_LOW;
    }
    if (data.voltageMv < TT_BATTERY_MEDIUM_MV) {
        return TT_NAV_ICON_BATTERY_MEDIUM;
    }
    return TT_NAV_ICON_BATTERY_FULL;
}

void TTNavigationBar::updateTime(bool refreshIfChanged) {
    if (_timeLabel == nullptr) return;

    struct tm t;
    if (!TTInstanceOf<TTRtc>().getLocalTime(t)) {
        if (_lastMinute != -2) {
            _lastMinute = -2;
            lv_label_set_text(_timeLabel, "--/-- --:--");
            LOG_I("NavBar: time invalid");
            if (refreshIfChanged) {
                requestRedraw();
            }
        }
        return;
    }
    const int minuteKey = t.tm_yday * 24 * 60 + t.tm_hour * 60 + t.tm_min;
    if (minuteKey == _lastMinute) {
        return;
    }
    _lastMinute = minuteKey;

    char text[20];
    snprintf(text, sizeof(text), "%02d/%02d %02d:%02d",
             t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min);
    lv_label_set_text(_timeLabel, text);
    LOG_I("NavBar: time %s", text);
    if (refreshIfChanged) {
        requestRedraw();
    }
}

void TTNavigationBar::requestRedraw() {
    if (!_visible) return;
    TTInstanceOf<TTLvglEpdDriver>().requestRefresh(TT_REFRESH_PARTIAL);
}
