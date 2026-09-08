#pragma once

#define TT_NOTIFICATION_SENSOR_DATA_UPDATE "TTNotify.SensorDataUpdate"
#define TT_NOTIFICATION_WIFI_STATUS        "TTNotify.WiFiStatus"

#define TT_WIFI_SSID_MAX   32
#define TT_WIFI_PASS_MAX   16
#define TT_WIFI_IP_MAX     16
#define TT_WIFI_URL_MAX    32

enum TTWiFiLinkState {
    TT_WIFI_LINK_IDLE = 0,
    TT_WIFI_LINK_CONNECTING,
    TT_WIFI_LINK_CONNECTED,
    TT_WIFI_LINK_PROVISIONING,
};

struct TTSensorDataPayload {
    float temperature;
    float humidity;
    float pressure;
};

struct TTWiFiStatusPayload {
    TTWiFiLinkState state;
    char ssid[TT_WIFI_SSID_MAX + 1];
    char ip[TT_WIFI_IP_MAX + 1];
    char apSsid[TT_WIFI_SSID_MAX + 1];
    char apPassword[TT_WIFI_PASS_MAX + 1];
    char portalUrl[TT_WIFI_URL_MAX + 1];
};
