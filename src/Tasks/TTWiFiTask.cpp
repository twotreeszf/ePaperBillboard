#include "TTWiFiTask.h"
#include "TTUITask.h"
#include "TTSensorTask.h"
#include "../Base/Logger.h"
#include "../Base/TTInstance.h"
#include "../Base/TTNotificationPayloads.h"
#include "../Base/TTRtc.h"
#include <ctime>
#include <cstring>

void TTWiFiTask::setup() {
    LOG_I("WiFi task starting");
    _wifiManager.tryConnectSaved();
    publishStatus();
    if (!_wifiManager.isConnecting() && !_wifiManager.isConnected()) {
        scheduleReconnect();
    }
}

void TTWiFiTask::loop() {
    _wifiManager.process();
    const TTWiFiLinkState now = _wifiManager.linkState();
    if (now == _publishedState) {
        return;
    }
    const bool becameConnected = (now == TT_WIFI_LINK_CONNECTED
        && _publishedState != TT_WIFI_LINK_CONNECTED);
    const bool connectFailed = (_publishedState == TT_WIFI_LINK_CONNECTING && now == TT_WIFI_LINK_IDLE);
    const bool linkLost = (_publishedState == TT_WIFI_LINK_CONNECTED && now == TT_WIFI_LINK_IDLE);
    publishStatus();
    if (becameConnected) {
        cancelReconnect();
        LOG_I("NTP: auto start after Wi-Fi connected");
        startNtpSync();
        return;
    }
    if (now == TT_WIFI_LINK_CONNECTED || now == TT_WIFI_LINK_PROVISIONING
        || now == TT_WIFI_LINK_CONNECTING) {
        cancelReconnect();
        return;
    }
    if (connectFailed || linkLost) {
        scheduleReconnect();
    }
}

void TTWiFiTask::requestStartProvisioningAsync() {
    auto* f = new std::function<void()>([this]() {
        cancelReconnect();
        _wifiManager.startProvisioning();
        publishStatus();
    });
    enqueue(f);
}

void TTWiFiTask::requestStopProvisioningAsync() {
    auto* f = new std::function<void()>([this]() {
        cancelReconnect();
        _wifiManager.stopProvisioning();
        publishStatus();
        if (!_wifiManager.isConnected() && !_wifiManager.isConnecting()) {
            scheduleReconnect();
        }
    });
    enqueue(f);
}

void TTWiFiTask::requestStatusAsync() {
    auto* f = new std::function<void()>([this]() {
        publishStatus();
    });
    enqueue(f);
}

void TTWiFiTask::requestReconnectAsync() {
    auto* f = new std::function<void()>([this]() {
        LOG_I("WiFi: reconnect saved network");
        cancelReconnect();
        _wifiManager.tryConnectSaved();
        publishStatus();
        if (!_wifiManager.isConnected() && !_wifiManager.isConnecting()) {
            scheduleReconnect();
        }
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
        publishTimeSync(TT_TIME_SYNC_NEED_WIFI, "未连接 Wi-Fi");
        return;
    }
    publishTimeSync(TT_TIME_SYNC_SYNCING, "正在校时...");
    syncNtp();
}

void TTWiFiTask::syncNtp() {
    if (!_wifiManager.isConnected()) {
        LOG_E("NTP: STA not connected");
        publishTimeSync(TT_TIME_SYNC_FAILED, "校时失败");
        publishStatus();
        return;
    }
    LOG_I("NTP: start");
    const bool ok = TTInstanceOf<TTRtc>().syncFromNtp(
        [](void* ctx, const char* text) {
            static_cast<TTWiFiTask*>(ctx)->publishTimeSync(TT_TIME_SYNC_SYNCING, text);
        },
        this);
    if (ok) {
        publishTimeSync(TT_TIME_SYNC_SYNCING, "正在保存时间...");
        time_t now = 0;
        time(&now);
        TTInstanceOf<TTSensorTask>().requestRtcWriteAsync(now);
    }
    publishTimeSync(ok ? TT_TIME_SYNC_OK : TT_TIME_SYNC_FAILED, ok ? "校时完成" : "校时失败");
    publishStatus();
}

void TTWiFiTask::scheduleReconnect() {
    if (_reconnectHandle != 0) {
        return;
    }
    if (!_wifiManager.hasConfiguredNetwork() || _wifiManager.isProvisioning()
        || _wifiManager.isConnected() || _wifiManager.isConnecting()) {
        return;
    }
    LOG_I("WiFi: reconnect in %u ms", (unsigned)TT_WIFI_RECONNECT_MS);
    _reconnectHandle = runOnce(TT_WIFI_RECONNECT_MS, [this]() {
        _reconnectHandle = 0;
        if (_wifiManager.isProvisioning() || _wifiManager.isConnected()
            || _wifiManager.isConnecting()) {
            LOG_I("WiFi: skip auto reconnect state=%d", (int)_wifiManager.linkState());
            return;
        }
        if (!_wifiManager.hasConfiguredNetwork()) {
            LOG_I("WiFi: skip auto reconnect, no saved network");
            return;
        }
        LOG_I("WiFi: auto reconnect start");
        _wifiManager.tryConnectSaved();
        publishStatus();
        if (!_wifiManager.isConnected() && !_wifiManager.isConnecting()) {
            scheduleReconnect();
        }
    });
}

void TTWiFiTask::cancelReconnect() {
    if (_reconnectHandle == 0) {
        return;
    }
    LOG_I("WiFi: cancel scheduled reconnect");
    cancelRepeat(_reconnectHandle);
    _reconnectHandle = 0;
}

void TTWiFiTask::publishStatus() {
    TTWiFiStatusPayload payload;
    _wifiManager.fillStatus(payload);
    _publishedState = payload.state;
    LOG_I("WiFi: publish state=%d ssid=%s ap=%s url=%s",
          (int)payload.state, payload.ssid, payload.apSsid, payload.portalUrl);
    TTInstanceOf<TTUITask>().postNotification(TT_NOTIFICATION_WIFI_STATUS, payload);
}

void TTWiFiTask::publishTimeSync(TTTimeSyncState state, const char* message) {
    TTTimeSyncPayload payload;
    memset(&payload, 0, sizeof(payload));
    payload.state = state;
    if (!TTRtc::loadTimezoneLabel(payload.timezone, sizeof(payload.timezone))) {
        TTRtc::loadTimezone(payload.timezone, sizeof(payload.timezone));
    }
    TTInstanceOf<TTRtc>().formatLocal(payload.timeText, sizeof(payload.timeText));
    if (message != nullptr) {
        strncpy(payload.message, message, TT_STATUS_MSG_MAX);
        payload.message[TT_STATUS_MSG_MAX] = '\0';
    }

    LOG_I("TimeSync: publish state=%d tz=%s time=%s msg=%s",
          (int)state, payload.timezone, payload.timeText, payload.message);
    TTInstanceOf<TTUITask>().postNotification(TT_NOTIFICATION_TIME_SYNC, payload);
}
