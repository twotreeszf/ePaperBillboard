#pragma once

#include <lvgl.h>
#include "TTNotificationPayloads.h"

#define TT_NAV_BAR_HEIGHT      20
#define TT_NAV_BAR_FONT        12
#define TT_NAV_STATUS_FONT     12
#define TT_NAV_BAR_PAD         4
#define TT_NAV_TITLE_MAX       32
#define TT_NAV_ARROW_W         14
#define TT_NAV_DIVIDER_H       1
#define TT_NAV_BACK_ICON       "/icons/back.png"
#define TT_NAV_STATUS_ICON     12
#define TT_NAV_BATTERY_ICON_W  18
#define TT_NAV_BATTERY_ICON_H  12
#define TT_NAV_STATUS_GAP      4
#define TT_NAV_WIFI_ICON_GAP   1
#define TT_NAV_WIFI_ICON_Y     -1
#define TT_NAV_STATUS_CLOCK_MS 1000
#define TT_NAV_WIFI_TEXT       "WiFi"
#define TT_NAV_ICON_CHECK            "/icons/check_sm.png"
#define TT_NAV_ICON_X                "/icons/x_sm.png"
#define TT_NAV_ICON_BATTERY_EMPTY    "/icons/battery_sm.png"
#define TT_NAV_ICON_BATTERY_LOW      "/icons/battery_low_sm.png"
#define TT_NAV_ICON_BATTERY_MEDIUM   "/icons/battery_medium_sm.png"
#define TT_NAV_ICON_BATTERY_FULL     "/icons/battery_full_sm.png"
#define TT_NAV_ICON_BATTERY_CHARGE   "/icons/battery_charging_sm.png"
#define TT_NAV_ICON_BATTERY_USB      "/icons/plug_sm.png"
#define TT_NAV_BATTERY_PERCENT_DEADBAND  2

class ITTNavigationController;

class TTNavigationBar {
public:
    void begin(lv_obj_t* parent, ITTNavigationController* nav);
    void show(const char* title, bool showBack);
    void hide();
    bool isVisible() const { return _visible; }
    lv_obj_t* getObject() const { return _bar; }
    lv_obj_t* getBackButton() const { return _backBtn; }

private:
    static void onBackClicked(lv_event_t* e);

    void beginStatus(lv_font_t* font);
    void subscribeStatus();
    void onClockTick();
    void applyWiFi(const TTWiFiStatusPayload& status);
    void applySensor(const TTSensorDataPayload& data);
    void updateTime(bool refreshIfChanged);
    void requestRedraw();
    void layoutTitle(bool showBack);
    void createWifiStatus(lv_obj_t* parent, lv_font_t* font);
    void createBatteryStatus(lv_obj_t* parent, lv_font_t* font);
    const char* batteryIconPath(const TTSensorDataPayload& data) const;
    lv_obj_t* createIcon(lv_obj_t* parent, const char* path, int32_t width = TT_NAV_STATUS_ICON, int32_t height = -1);
    lv_obj_t* createValue(lv_obj_t* parent, lv_font_t* font, const char* text);

    ITTNavigationController* _nav = nullptr;
    lv_obj_t* _bar = nullptr;
    lv_obj_t* _title = nullptr;
    lv_obj_t* _backBtn = nullptr;
    lv_obj_t* _statusRow = nullptr;
    lv_obj_t* _wifiLabel = nullptr;
    lv_obj_t* _wifiIcon = nullptr;
    lv_obj_t* _timeLabel = nullptr;
    lv_obj_t* _tempLabel = nullptr;
    lv_obj_t* _humLabel = nullptr;
    lv_obj_t* _pressLabel = nullptr;
    lv_obj_t* _batteryIcon = nullptr;
    lv_obj_t* _batteryLabel = nullptr;
    TTWiFiLinkState _wifiState = TT_WIFI_LINK_IDLE;
    int _lastMinute = -1;
    bool _hasSensor = false;
    float _temperature = 0.0f;
    float _humidity = 0.0f;
    float _pressure = 0.0f;
    int16_t _batteryMv = 0;
    uint8_t _batteryPercent = 0;
    bool _batteryCharging = false;
    bool _batteryUsb = false;
    bool _visible = false;
};
