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
    _wifiManager.refreshSavedNetwork();
    if (!_wifiManager.hasConfiguredNetwork()) {
        LOG_I("WiFi: no SSID, radio stays off");
        _wifiManager.sleepRadio();
        publishStatus();
        return;
    }
    beginWake();
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
    if (now == TT_WIFI_LINK_PROVISIONING) {
        cancelHold();
        LOG_I("WiFi: provisioning, duty cycle paused");
        return;
    }
    if (becameConnected) {
        LOG_I("WiFi: connected, hold %u ms", (unsigned)TT_WIFI_WAKE_HOLD_MS);
        _ntpOnConnect = false;
        scheduleHold();
        schedulePeriod();
        LOG_I("NTP: auto start after Wi-Fi connected");
        startNtpSync();
        return;
    }
    if (connectFailed || linkLost) {
        LOG_I("WiFi: %s, wait next period", connectFailed ? "connect failed" : "link lost");
        _ntpOnConnect = false;
        endWake();
        schedulePeriod();
        return;
    }
    if (now == TT_WIFI_LINK_CONNECTING) {
        schedulePeriod();
    }
}

void TTWiFiTask::requestStartProvisioningAsync() {
    auto* f = new std::function<void()>([this]() {
        cancelHold();
        _ntpOnConnect = false;
        _wifiManager.startProvisioning();
        publishStatus();
    });
    enqueue(f);
}

void TTWiFiTask::requestStopProvisioningAsync() {
    auto* f = new std::function<void()>([this]() {
        cancelHold();
        _wifiManager.stopProvisioning();
        publishStatus();
        if (_wifiManager.isConnected()) {
            scheduleHold();
            schedulePeriod();
            return;
        }
        if (_wifiManager.isConnecting()) {
            schedulePeriod();
            return;
        }
        if (_wifiManager.hasConfiguredNetwork()) {
            schedulePeriod();
            return;
        }
        _wifiManager.sleepRadio();
        publishStatus();
        cancelPeriod();
    });
    enqueue(f);
}

void TTWiFiTask::requestStatusAsync() {
    auto* f = new std::function<void()>([this]() {
        publishStatus();
    });
    enqueue(f);
}

void TTWiFiTask::requestConnectAsync() {
    auto* f = new std::function<void()>([this]() {
        LOG_I("WiFi: page requested connect");
        cancelPeriod();
        beginWake();
    });
    enqueue(f);
}

void TTWiFiTask::requestNtpSyncAsync() {
    auto* f = new std::function<void()>([this]() {
        if (_wifiManager.isConnected()) {
            startNtpSync();
            return;
        }
        if (!_wifiManager.hasConfiguredNetwork() && !_wifiManager.refreshSavedNetwork()) {
            LOG_I("NTP: Wi-Fi not configured");
            publishStatus();
            publishTimeSync(TT_TIME_SYNC_NEED_WIFI, "未连接 Wi-Fi");
            return;
        }
        LOG_I("NTP: wake Wi-Fi then sync");
        _ntpOnConnect = true;
        cancelPeriod();
        beginWake();
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

void TTWiFiTask::beginWake() {
    if (_wifiManager.isProvisioning()) {
        LOG_I("WiFi: wake skipped, provisioning");
        publishStatus();
        return;
    }
    if (!_wifiManager.refreshSavedNetwork()) {
        LOG_I("WiFi: wake skipped, no SSID");
        _wifiManager.sleepRadio();
        publishStatus();
        cancelPeriod();
        return;
    }
    if (_wifiManager.isConnected()) {
        LOG_I("WiFi: already connected");
        publishStatus();
        scheduleHold();
        schedulePeriod();
        if (_ntpOnConnect) {
            _ntpOnConnect = false;
            startNtpSync();
        }
        return;
    }
    if (_wifiManager.isConnecting()) {
        LOG_I("WiFi: already connecting");
        publishStatus();
        schedulePeriod();
        return;
    }
    LOG_I("WiFi: wake connect ssid=%s", _wifiManager.hasConfiguredNetwork() ? "saved" : "?");
    schedulePeriod();
    if (_wifiManager.tryConnectSaved()) {
        publishStatus();
        return;
    }
    LOG_W("WiFi: wake connect failed to start");
    publishStatus();
    schedulePeriod();
}

void TTWiFiTask::endWake() {
    cancelHold();
    if (_wifiManager.isProvisioning()) {
        return;
    }
    _wifiManager.sleepRadio();
    publishStatus();
}

void TTWiFiTask::scheduleHold() {
    cancelHold();
    LOG_I("WiFi: hold radio %u ms", (unsigned)TT_WIFI_WAKE_HOLD_MS);
    _wakeHoldHandle = runOnce(TT_WIFI_WAKE_HOLD_MS, [this]() {
        _wakeHoldHandle = 0;
        if (_wifiManager.isProvisioning()) {
            LOG_I("WiFi: hold skipped, provisioning");
            return;
        }
        LOG_I("WiFi: hold done, sleep");
        endWake();
    });
}

void TTWiFiTask::cancelHold() {
    if (_wakeHoldHandle == 0) {
        return;
    }
    LOG_I("WiFi: cancel hold");
    cancelRepeat(_wakeHoldHandle);
    _wakeHoldHandle = 0;
}

void TTWiFiTask::schedulePeriod() {
    if (_wakePeriodHandle != 0) {
        return;
    }
    if (!_wifiManager.hasConfiguredNetwork()) {
        return;
    }
    LOG_I("WiFi: next wake in %u ms", (unsigned)TT_WIFI_WAKE_PERIOD_MS);
    _wakePeriodHandle = runOnce(TT_WIFI_WAKE_PERIOD_MS, [this]() {
        _wakePeriodHandle = 0;
        LOG_I("WiFi: period wake");
        beginWake();
    });
}

void TTWiFiTask::cancelPeriod() {
    if (_wakePeriodHandle == 0) {
        return;
    }
    LOG_I("WiFi: cancel period");
    cancelRepeat(_wakePeriodHandle);
    _wakePeriodHandle = 0;
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
