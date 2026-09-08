#include "TTWiFiTask.h"
#include "TTUITask.h"
#include "TTSensorTask.h"
#include "../Base/Logger.h"
#include "../Base/TTInstance.h"
#include "../Base/TTPreference.h"
#include "../Base/TTNotificationPayloads.h"
#include "../Base/TTRtc.h"
#include <ctime>
#include <cstring>

void TTWiFiTask::setup() {
    LOG_I("WiFi task starting");
    if (!TTInstanceOf<TTPreference>().begin()) {
        LOG_E("WiFi: preference begin failed");
    }
    _wifiManager.tryConnectSaved();
    publishStatus();
}

void TTWiFiTask::loop() {
    _wifiManager.process();
    if (_wifiManager.isConnected() != (_publishedState == TT_WIFI_LINK_CONNECTED)
        || _wifiManager.isProvisioning() != (_publishedState == TT_WIFI_LINK_PROVISIONING)) {
        publishStatus();
    }
}

void TTWiFiTask::requestStartProvisioningAsync() {
    auto* f = new std::function<void()>([this]() {
        _wifiManager.startProvisioning();
        publishStatus();
    });
    enqueue(f);
}

void TTWiFiTask::requestStopProvisioningAsync() {
    auto* f = new std::function<void()>([this]() {
        _wifiManager.stopProvisioning();
        publishStatus();
    });
    enqueue(f);
}

void TTWiFiTask::requestStatusAsync() {
    auto* f = new std::function<void()>([this]() {
        publishStatus();
    });
    enqueue(f);
}

void TTWiFiTask::requestNtpSyncAsync() {
    auto* f = new std::function<void()>([this]() {
        startNtpSync();
    });
    enqueue(f);
}

void TTWiFiTask::startNtpSync() {
    if (!_wifiManager.isConnected()) {
        LOG_I("NTP: Wi-Fi not connected");
        publishStatus();
        publishTimeSync(TT_TIME_SYNC_NEED_WIFI);
        return;
    }
    publishTimeSync(TT_TIME_SYNC_SYNCING);
    syncNtp();
}

void TTWiFiTask::syncNtp() {
    if (!_wifiManager.isConnected()) {
        LOG_E("NTP: STA not connected");
        publishTimeSync(TT_TIME_SYNC_FAILED);
        publishStatus();
        return;
    }
    LOG_I("NTP: start");
    const bool ok = TTInstanceOf<TTRtc>().syncFromNtp();
    if (ok) {
        time_t now = 0;
        time(&now);
        TTInstanceOf<TTSensorTask>().requestRtcWriteAsync(now);
    }
    publishTimeSync(ok ? TT_TIME_SYNC_OK : TT_TIME_SYNC_FAILED);
    publishStatus();
}

void TTWiFiTask::publishStatus() {
    TTWiFiStatusPayload payload;
    _wifiManager.fillStatus(payload);
    _publishedState = payload.state;
    LOG_I("WiFi: publish state=%d ssid=%s ap=%s url=%s",
          (int)payload.state, payload.ssid, payload.apSsid, payload.portalUrl);
    TTInstanceOf<TTUITask>().postNotification(TT_NOTIFICATION_WIFI_STATUS, payload);
}

void TTWiFiTask::publishTimeSync(TTTimeSyncState state) {
    TTTimeSyncPayload payload;
    memset(&payload, 0, sizeof(payload));
    payload.state = state;
    if (!TTRtc::loadTimezoneLabel(payload.timezone, sizeof(payload.timezone))) {
        TTRtc::loadTimezone(payload.timezone, sizeof(payload.timezone));
    }
    TTInstanceOf<TTRtc>().formatLocal(payload.timeText, sizeof(payload.timeText));

    LOG_I("TimeSync: publish state=%d tz=%s time=%s ap=%s",
          (int)state, payload.timezone, payload.timeText, payload.apSsid);
    TTInstanceOf<TTUITask>().postNotification(TT_NOTIFICATION_TIME_SYNC, payload);
}
