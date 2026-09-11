#pragma once

#include <stdint.h>

#include "TTWeatherTypes.h"

#define TT_NOTIFICATION_SENSOR_DATA_UPDATE "TTNotify.SensorDataUpdate"
#define TT_NOTIFICATION_WIFI_STATUS        "TTNotify.WiFiStatus"
#define TT_NOTIFICATION_TIME_SYNC          "TTNotify.TimeSync"
#define TT_NOTIFICATION_WEATHER            "TTNotify.Weather"
#define TT_NOTIFICATION_SLEEP_WAKE         "TTNotify.SleepWake"
#define TT_NOTIFICATION_TIME_TICK          "TTNotify.TimeTick"

#define TT_WIFI_SSID_MAX   32
#define TT_WIFI_PASS_MAX   16
#define TT_WIFI_IP_MAX     16
#define TT_WIFI_URL_MAX    32
#define TT_TZ_TEXT_MAX     32
#define TT_TIME_TEXT_MAX   20
#define TT_STATUS_MSG_MAX  48

enum TTWiFiLinkState {
    TT_WIFI_LINK_IDLE = 0,
    TT_WIFI_LINK_CONNECTING,
    TT_WIFI_LINK_CONNECTED,
    TT_WIFI_LINK_PROVISIONING,
};

enum TTTimeSyncState {
    TT_TIME_SYNC_IDLE = 0,
    TT_TIME_SYNC_NEED_WIFI,
    TT_TIME_SYNC_SYNCING,
    TT_TIME_SYNC_OK,
    TT_TIME_SYNC_FAILED,
};

enum TTSleepWakeReason {
    TT_SLEEP_WAKE_TIME = 0,
    TT_SLEEP_WAKE_FETCH,
    TT_SLEEP_WAKE_INPUT,
};

struct TTSleepWakePayload {
    TTSleepWakeReason reason;
};

struct TTTimeTickPayload {
};

struct TTSensorDataPayload {
    float temperature;
    float humidity;
    float pressure;
    int16_t voltageMv;
    uint8_t percent;
    bool charging;
    bool usbPlugged;
};

struct TTWiFiStatusPayload {
    TTWiFiLinkState state;
    char ssid[TT_WIFI_SSID_MAX + 1];
    char ip[TT_WIFI_IP_MAX + 1];
    char apSsid[TT_WIFI_SSID_MAX + 1];
    char apPassword[TT_WIFI_PASS_MAX + 1];
    char portalUrl[TT_WIFI_URL_MAX + 1];
};

struct TTTimeSyncPayload {
    TTTimeSyncState state;
    char timezone[TT_TZ_TEXT_MAX + 1];
    char apSsid[TT_WIFI_SSID_MAX + 1];
    char portalUrl[TT_WIFI_URL_MAX + 1];
    char timeText[TT_TIME_TEXT_MAX + 1];
    char message[TT_STATUS_MSG_MAX + 1];
};
