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
    lv_obj_align(_bar, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_bg_color(_bar, TT_NAV_BAR_BG_COLOR, 0);
    lv_obj_set_style_bg_opa(_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_bar, 0, 0);
    lv_obj_set_style_pad_all(_bar, 0, 0);
    lv_obj_set_style_radius(_bar, 0, 0);
    lv_obj_remove_flag(_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(_bar, LV_OBJ_FLAG_HIDDEN);

    _backBtn = lv_btn_create(_bar);
    lv_obj_set_size(_backBtn, TT_NAV_ARROW_W, TT_NAV_BAR_HEIGHT - TT_NAV_DIVIDER_H);
    lv_obj_align(_backBtn, LV_ALIGN_LEFT_MID, TT_NAV_BAR_PAD, TT_NAV_BAR_CONTENT_Y);
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

    _titleBox = lv_obj_create(_bar);
    lv_obj_set_size(_titleBox, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(_titleBox, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(_titleBox, 0, 0);
    lv_obj_set_style_pad_all(_titleBox, 0, 0);
    lv_obj_set_style_radius(_titleBox, 0, 0);
    lv_obj_set_layout(_titleBox, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(_titleBox, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(_titleBox, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(_titleBox, TT_NAV_TITLE_BRACKET_GAP, 0);
    lv_obj_remove_flag(_titleBox, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* leftBracket = lv_label_create(_titleBox);
    lv_label_set_text(leftBracket, "[");
    lv_obj_set_style_text_color(leftBracket, TT_NAV_BAR_FG_COLOR, 0);
    lv_obj_set_style_bg_opa(leftBracket, LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_font(leftBracket, font, 0);

    _title = lv_label_create(_titleBox);
    lv_label_set_text(_title, "");
    lv_obj_set_style_text_color(_title, TT_NAV_BAR_FG_COLOR, 0);
    lv_obj_set_style_bg_opa(_title, LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_font(_title, font, 0);
    lv_obj_set_style_text_align(_title, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_long_mode(_title, LV_LABEL_LONG_DOT);

    lv_obj_t* rightBracket = lv_label_create(_titleBox);
    lv_label_set_text(rightBracket, "]");
    lv_obj_set_style_text_color(rightBracket, TT_NAV_BAR_FG_COLOR, 0);
    lv_obj_set_style_bg_opa(rightBracket, LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_font(rightBracket, font, 0);

    beginStatus(TTFontManager::instance().getFont(TT_NAV_STATUS_FONT));

    lv_obj_t* divider = lv_obj_create(_bar);
    lv_obj_set_size(divider, EPD_WIDTH, TT_NAV_DIVIDER_H);
    lv_obj_align(divider, LV_ALIGN_TOP_LEFT, 0, TT_NAV_DIVIDER_Y);
    lv_obj_set_style_bg_color(divider, TT_NAV_BAR_FG_COLOR, 0);
    lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(divider, 0, 0);
    lv_obj_set_style_pad_all(divider, 0, 0);
    lv_obj_set_style_radius(divider, 0, 0);

    updateTime();
    subscribeStatus();
    TTInstanceOf<TTWiFiTask>().requestStatusAsync();
    TTInstanceOf<TTSensorTask>().requestSensorUpdateAsync();
    LOG_I("NavBar: created");
}

void TTNavigationBar::beginStatus(lv_font_t* font) {
    _statusRow = lv_obj_create(_bar);
    lv_obj_set_size(_statusRow, LV_SIZE_CONTENT, TT_NAV_BAR_HEIGHT - TT_NAV_DIVIDER_H);
    lv_obj_align(_statusRow, LV_ALIGN_RIGHT_MID, -TT_NAV_BAR_PAD, TT_NAV_BAR_CONTENT_Y);
    lv_obj_set_style_bg_opa(_statusRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(_statusRow, 0, 0);
    lv_obj_set_style_pad_all(_statusRow, 0, 0);
    lv_obj_set_style_radius(_statusRow, 0, 0);
    lv_obj_set_layout(_statusRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(_statusRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(_statusRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(_statusRow, TT_NAV_STATUS_GAP, 0);
    lv_obj_remove_flag(_statusRow, LV_OBJ_FLAG_SCROLLABLE);

    createWifiStatus(_statusRow);
    _tempLabel = createSensorItem(_statusRow, font, TT_NAV_ICON_TEMP, TT_NAV_TEMP_ICON_W, "--.-℃",
                                  TT_NAV_TEMP_PREFIX);
    _humLabel = createSensorItem(_statusRow, font, TT_NAV_ICON_HUM, TT_NAV_HUM_ICON_W, "--%");
    _pressLabel = createSensorItem(_statusRow, font, TT_NAV_ICON_PRESS, TT_NAV_PRESS_ICON_W, "----p");
    _timeLabel = createValue(_statusRow, font, "--:--");
    createBatteryStatus(_statusRow, font);
}

void TTNavigationBar::createWifiStatus(lv_obj_t* parent) {
    _wifiIcon = createIcon(parent, TT_NAV_ICON_WIFI_OFF, TT_NAV_WIFI_ICON_W, TT_NAV_WIFI_ICON_H);
    lv_obj_set_style_pad_all(_wifiIcon, 0, 0);
    lv_obj_set_style_translate_y(_wifiIcon, TT_NAV_WIFI_ICON_Y, 0);
}

lv_obj_t* TTNavigationBar::createSensorItem(lv_obj_t* parent, lv_font_t* font, const char* iconPath,
                                            int32_t iconW, const char* placeholder, const char* prefix) {
    lv_obj_t* group = lv_obj_create(parent);
    lv_obj_set_size(group, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(group, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(group, 0, 0);
    lv_obj_set_style_pad_all(group, 0, 0);
    lv_obj_set_style_radius(group, 0, 0);
    lv_obj_set_layout(group, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(group, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(group, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(group, TT_NAV_SENSOR_ICON_GAP, 0);
    lv_obj_remove_flag(group, LV_OBJ_FLAG_SCROLLABLE);

    if (prefix != nullptr && prefix[0] != '\0') {
        lv_font_t* prefixFont = TTFontManager::instance().getFont(TT_NAV_TEMP_PREFIX_FONT);
        lv_obj_t* prefixLabel = createValue(group, prefixFont, prefix);
        lv_obj_set_style_pad_all(prefixLabel, 0, 0);
        const int32_t valueH = lv_font_get_line_height(font);
        const int32_t prefixH = lv_font_get_line_height(prefixFont);
        lv_obj_set_height(prefixLabel, valueH);
        if (valueH > prefixH) {
            lv_obj_set_style_pad_top(prefixLabel, (valueH - prefixH) / 2, 0);
        }
        lv_obj_set_style_translate_y(prefixLabel, TT_NAV_TEMP_PREFIX_Y, 0);
    }

    lv_obj_t* icon = createIcon(group, iconPath, iconW, TT_NAV_SENSOR_ICON_H);
    lv_obj_set_style_pad_all(icon, 0, 0);
    lv_obj_set_style_translate_y(icon, TT_NAV_SENSOR_ICON_Y, 0);

    lv_obj_t* label = createValue(group, font, placeholder);
    lv_obj_set_style_pad_all(label, 0, 0);
    lv_obj_set_height(label, lv_font_get_line_height(font));
    return label;
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
    lv_obj_set_style_translate_y(_batteryIcon, TT_NAV_SENSOR_ICON_Y, 0);

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
    lv_obj_set_style_text_color(label, TT_NAV_BAR_FG_COLOR, 0);
    lv_obj_set_style_bg_opa(label, LV_OPA_TRANSP, 0);
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
    nc.subscribe<TTTimeTickPayload>(
        TT_NOTIFICATION_TIME_TICK,
        this,
        [this](const TTTimeTickPayload&) {
            if (updateTime()) {
                requestRedraw();
            }
        });
}

void TTNavigationBar::show(const char* title, bool showBack) {
    if (_bar == nullptr) return;
    lv_label_set_text(_title, title != nullptr ? title : "");
    if (showBack) {
        lv_obj_remove_flag(_backBtn, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(_backBtn, LV_OBJ_FLAG_HIDDEN);
    }
    layoutTitle(showBack);
    lv_obj_remove_flag(_bar, LV_OBJ_FLAG_HIDDEN);
    _visible = true;
    updateTime();
    LOG_I("NavBar: show title=%s back=%d", title != nullptr ? title : "", showBack ? 1 : 0);
}

void TTNavigationBar::layoutTitle(bool showBack) {
    if (_titleBox == nullptr || _title == nullptr) return;

    if (showBack) {
        lv_obj_align_to(_titleBox, _backBtn, LV_ALIGN_OUT_RIGHT_MID, TT_NAV_BAR_PAD, 0);
    } else {
        lv_obj_align(_titleBox, LV_ALIGN_LEFT_MID, TT_NAV_BAR_PAD, TT_NAV_BAR_CONTENT_Y);
    }

    if (_statusRow != nullptr) {
        lv_obj_update_layout(_statusRow);
        lv_obj_update_layout(_titleBox);
        const int32_t left = showBack
            ? (TT_NAV_BAR_PAD + TT_NAV_ARROW_W + TT_NAV_BAR_PAD)
            : TT_NAV_BAR_PAD;
        const int32_t statusW = lv_obj_get_width(_statusRow);
        lv_obj_t* leftBracket = lv_obj_get_child(_titleBox, 0);
        lv_obj_t* rightBracket = lv_obj_get_child(_titleBox, 2);
        const int32_t bracketW = lv_obj_get_width(leftBracket) + lv_obj_get_width(rightBracket)
            + TT_NAV_TITLE_BRACKET_GAP * 2;
        int32_t maxTitle = EPD_WIDTH - left - statusW - TT_NAV_BAR_PAD * 2 - bracketW;
        if (maxTitle < 24) {
            maxTitle = 24;
        }
        lv_obj_set_width(_title, LV_SIZE_CONTENT);
        lv_obj_update_layout(_title);
        if (lv_obj_get_width(_title) > maxTitle) {
            lv_obj_set_width(_title, maxTitle);
        }
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

void TTNavigationBar::applyWiFi(const TTWiFiStatusPayload& status) {
    if (_wifiIcon == nullptr) return;
    if (_wifiState == status.state) {
        return;
    }
    _wifiState = status.state;
    tt_stream_image_set_src(_wifiIcon, wifiIconPath(status.state));
    LOG_I("NavBar: wifi state=%d", (int)status.state);
    TTInstanceOf<TTUITask>().runOnce(0, [this]() {
        requestRedraw();
    });
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
    snprintf(text, sizeof(text), "%.1f℃", _temperature);
    lv_label_set_text(_tempLabel, text);
    snprintf(text, sizeof(text), "%.0f%%", _humidity);
    lv_label_set_text(_humLabel, text);
    snprintf(text, sizeof(text), "%.0fp", _pressure);
    lv_label_set_text(_pressLabel, text);
    const char* batteryIcon = batteryIconPath(data);
    tt_stream_image_set_src(_batteryIcon, batteryIcon);
    if (batteryIcon == TT_NAV_ICON_BATTERY_USB) {
        lv_obj_set_size(_batteryIcon, TT_NAV_PLUG_ICON_W, TT_NAV_PLUG_ICON_H);
    } else {
        lv_obj_set_size(_batteryIcon, TT_NAV_BATTERY_ICON_W, TT_NAV_BATTERY_ICON_H);
    }
    if (_batteryCharging || _batteryUsb) {
        lv_label_set_text(_batteryLabel, "");
        lv_obj_add_flag(_batteryLabel, LV_OBJ_FLAG_HIDDEN);
    } else {
        snprintf(text, sizeof(text), "%u%%", (unsigned)_batteryPercent);
        lv_label_set_text(_batteryLabel, text);
        lv_obj_remove_flag(_batteryLabel, LV_OBJ_FLAG_HIDDEN);
    }
    LOG_I("NavBar: sensor T=%.1f H=%.1f P=%.0f bat=%dmV %u%% usb=%d charging=%d",
          _temperature, _humidity, _pressure,
          _batteryMv, (unsigned)_batteryPercent, _batteryUsb ? 1 : 0, _batteryCharging ? 1 : 0);
    if (_visible) {
        layoutTitle(!lv_obj_has_flag(_backBtn, LV_OBJ_FLAG_HIDDEN));
    }
    requestRedraw();
}

const char* TTNavigationBar::wifiIconPath(TTWiFiLinkState state) const {
    if (state == TT_WIFI_LINK_CONNECTED) {
        return TT_NAV_ICON_WIFI_ON;
    }
    if (state == TT_WIFI_LINK_CONNECTING) {
        return TT_NAV_ICON_WIFI_WAIT;
    }
    if (state == TT_WIFI_LINK_PROVISIONING) {
        return TT_NAV_ICON_WIFI_AP;
    }
    return TT_NAV_ICON_WIFI_OFF;
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

bool TTNavigationBar::updateTime() {
    if (_timeLabel == nullptr) return false;

    struct tm t;
    if (!TTInstanceOf<TTRtc>().getLocalTime(t)) {
        if (_lastMinute != -2) {
            _lastMinute = -2;
            lv_label_set_text(_timeLabel, "--:--");
            LOG_I("NavBar: time invalid");
            return true;
        }
        return false;
    }
    const int minuteKey = t.tm_yday * 24 * 60 + t.tm_hour * 60 + t.tm_min;
    if (minuteKey == _lastMinute) {
        return false;
    }
    _lastMinute = minuteKey;

    char text[20];
    snprintf(text, sizeof(text), "%02d:%02d", t.tm_hour, t.tm_min);
    lv_label_set_text(_timeLabel, text);
    LOG_I("NavBar: time %s", text);
    return true;
}

void TTNavigationBar::requestRedraw() {
    if (!_visible) return;
    TTInstanceOf<TTLvglEpdDriver>().requestOverlayRefresh();
}
