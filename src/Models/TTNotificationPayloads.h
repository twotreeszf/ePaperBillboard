#pragma once

#include <stdint.h>

#include "TTWeatherTypes.h"
#include "TTCalendarTypes.h"

#define TT_NOTIFICATION_SENSOR_DATA_UPDATE "TTNotify.SensorDataUpdate"
#define TT_NOTIFICATION_WIFI_STATUS        "TTNotify.WiFiStatus"
#define TT_NOTIFICATION_TIME_SYNC          "TTNotify.TimeSync"
#define TT_NOTIFICATION_WEATHER            "TTNotify.Weather"
#define TT_NOTIFICATION_SLEEP_WAKE         "TTNotify.SleepWake"
#define TT_NOTIFICATION_SLEEP_STATE        "TTNotify.SleepState"
#define TT_NOTIFICATION_TIME_TICK          "TTNotify.TimeTick"
#define TT_NOTIFICATION_CALENDAR           "TTNotify.Calendar"
#define TT_NOTIFICATION_PICTORIAL          "TTNotify.Pictorial"
#define TT_NOTIFICATION_OTA                "TTNotify.Ota"
#define TT_NOTIFICATION_RTC_LOAD           "TTNotify.RtcLoad"
#define TT_NOTIFICATION_BOOK_TRANSFER      "TTNotify.BookTransfer"

#define TT_OTA_MSG_MAX     192

#define TT_WIFI_SSID_MAX   32
#define TT_WIFI_PASS_MAX   16
#define TT_WIFI_IP_MAX     16
#define TT_WIFI_URL_MAX    32
#define TT_TZ_TEXT_MAX     32
#define TT_TIME_TEXT_MAX   20
#define TT_STATUS_MSG_MAX  48
#define TT_BOOK_TRANSFER_NAME_MAX  128
#define TT_TRANSFER_MSG_IMAGE      "image"

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
    TT_SLEEP_WAKE_POWER,
};

struct TTSleepWakePayload {
    TTSleepWakeReason reason;
};

enum TTSleepState {
    TT_SLEEP_STATE_AWAKE = 0,
    TT_SLEEP_STATE_SLEEPING,
};

struct TTSleepStatePayload {
    TTSleepState state;
};

struct TTTimeTickPayload {
};

struct TTRtcLoadPayload {
    bool loaded;
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

enum TTOtaPhase {
    TT_OTA_PHASE_UP_TO_DATE = 0,
    TT_OTA_PHASE_AVAILABLE,
    TT_OTA_PHASE_PROGRESS,
    TT_OTA_PHASE_FAILED,
    TT_OTA_PHASE_REBOOT,
};

struct TTOtaPayload {
    TTOtaPhase phase;
    char version[16];
    char message[TT_OTA_MSG_MAX];
};

enum TTBookTransferState {
    TT_BOOK_TRANSFER_CONNECTING = 0,
    TT_BOOK_TRANSFER_READY,
    TT_BOOK_TRANSFER_RECEIVING,
    TT_BOOK_TRANSFER_RECEIVED,
    TT_BOOK_TRANSFER_UPLOAD_FAILED,
    TT_BOOK_TRANSFER_DELETED,
    TT_BOOK_TRANSFER_FAILED,
    TT_BOOK_TRANSFER_STOPPED,
};

enum TTBookTransferError {
    TT_BOOK_TRANSFER_ERROR_NONE = 0,
    TT_BOOK_TRANSFER_ERROR_NO_NETWORK,
    TT_BOOK_TRANSFER_ERROR_BUSY,
    TT_BOOK_TRANSFER_ERROR_CONNECT_FAILED,
    TT_BOOK_TRANSFER_ERROR_LINK_LOST,
    TT_BOOK_TRANSFER_ERROR_SD_FAILED,
};

struct TTBookTransferPayload {
    TTBookTransferState state;
    TTBookTransferError error;
    uint32_t fileSize;
    uint16_t receivedCount;
    char ssid[TT_WIFI_SSID_MAX + 1];
    char url[TT_WIFI_URL_MAX + 1];
    char fileName[TT_BOOK_TRANSFER_NAME_MAX + 1];
    char message[TT_STATUS_MSG_MAX + 1];
};

struct TTTimeSyncPayload {
    TTTimeSyncState state;
    char timezone[TT_TZ_TEXT_MAX + 1];
    char apSsid[TT_WIFI_SSID_MAX + 1];
    char portalUrl[TT_WIFI_URL_MAX + 1];
    char timeText[TT_TIME_TEXT_MAX + 1];
    char message[TT_STATUS_MSG_MAX + 1];
};
