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
        cancelIdleCheck();
        LOG_I("WiFi: provisioning, duty cycle paused");
        return;
    }
    if (becameConnected) {
        LOG_I("WiFi: connected, refs=%u", (unsigned)_useCount);
        refreshPeriodDeadline();
        cancelIdleCheck();
        _idleCheckReady = false;
        armIdleCheck();
        if (!_ntpAutoDone) {
            _ntpAutoDone = true;
            LOG_I("NTP: first link after boot");
            startNtpSync();
        } else if (_ntpOnConnect) {
            _ntpOnConnect = false;
            startNtpSync();
        }
        _ntpOnConnect = false;
        return;
    }
    if (connectFailed || linkLost) {
        LOG_I("WiFi: %s, wait next period", connectFailed ? "connect failed" : "link lost");
        _ntpOnConnect = false;
        endWake();
        refreshPeriodDeadline();
        return;
    }
    if (now == TT_WIFI_LINK_CONNECTING) {
        refreshPeriodDeadline();
        armIdleCheck();
    }
}

void TTWiFiTask::requestStartProvisioningAsync() {
    auto* f = new std::function<void()>([this]() {
        cancelIdleCheck();
        _ntpOnConnect = false;
        _wifiManager.startProvisioning();
        publishStatus();
    });
    enqueue(f);
}

void TTWiFiTask::requestStopProvisioningAsync() {
    auto* f = new std::function<void()>([this]() {
        cancelIdleCheck();
        _wifiManager.stopProvisioning();
        publishStatus();
        if (_wifiManager.isConnected()) {
            armIdleCheck();
            refreshPeriodDeadline();
            return;
        }
        if (_wifiManager.isConnecting()) {
            armIdleCheck();
            refreshPeriodDeadline();
            return;
        }
        if (_wifiManager.hasConfiguredNetwork()) {
            refreshPeriodDeadline();
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

void TTWiFiTask::requestAcquireAsync(const char* tag) {
    auto* f = new std::function<void()>([this, tag]() {
        acquireRadio(tag);
    });
    enqueue(f);
}

void TTWiFiTask::requestReleaseAsync(const char* tag) {
    auto* f = new std::function<void()>([this, tag]() {
        releaseRadio(tag);
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
    acquireRadio("ntp");
    publishTimeSync(TT_TIME_SYNC_SYNCING, "正在校时...");
    syncNtp();
    releaseRadio("ntp");
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
        refreshPeriodDeadline();
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
        refreshPeriodDeadline();
        armIdleCheck();
        if (_ntpOnConnect) {
            _ntpOnConnect = false;
            startNtpSync();
        }
        trySleepIfIdle();
        return;
    }
    if (_wifiManager.isConnecting()) {
        LOG_I("WiFi: already connecting");
        publishStatus();
        refreshPeriodDeadline();
        armIdleCheck();
        return;
    }
    LOG_I("WiFi: wake connect ssid=%s", _wifiManager.hasConfiguredNetwork() ? "saved" : "?");
    refreshPeriodDeadline();
    if (_wifiManager.tryConnectSaved()) {
        publishStatus();
        armIdleCheck();
        return;
    }
    LOG_W("WiFi: wake connect failed to start");
    publishStatus();
    refreshPeriodDeadline();
}

void TTWiFiTask::endWake() {
    cancelIdleCheck();
    _idleCheckReady = false;
    if (_wifiManager.isProvisioning()) {
        return;
    }
    _wifiManager.sleepRadio();
    publishStatus();
}

void TTWiFiTask::acquireRadio(const char* tag) {
    if (_useCount < 0xFFFFFFFFu) {
        _useCount++;
    }
    LOG_I("WiFi: acquire tag=%s count=%u", tag != nullptr ? tag : "-", (unsigned)_useCount);
    if (_wifiManager.isProvisioning()) {
        return;
    }
    if (_wifiManager.isConnected() || _wifiManager.isConnecting()) {
        return;
    }
    beginWake();
}

void TTWiFiTask::releaseRadio(const char* tag) {
    if (_useCount == 0) {
        LOG_W("WiFi: release underflow tag=%s", tag != nullptr ? tag : "-");
        return;
    }
    _useCount--;
    LOG_I("WiFi: release tag=%s count=%u", tag != nullptr ? tag : "-", (unsigned)_useCount);
    trySleepIfIdle();
}

void TTWiFiTask::armIdleCheck() {
    if (_wifiManager.isProvisioning()) {
        return;
    }
    if (_idleCheckReady) {
        trySleepIfIdle();
        return;
    }
    if (_idleCheckHandle != 0) {
        return;
    }
    LOG_I("WiFi: idle check in %u ms count=%u",
          (unsigned)TT_WIFI_IDLE_CHECK_MS, (unsigned)_useCount);
    _idleCheckHandle = runOnce(TT_WIFI_IDLE_CHECK_MS, [this]() {
        _idleCheckHandle = 0;
        _idleCheckReady = true;
        LOG_I("WiFi: idle check on count=%u", (unsigned)_useCount);
        trySleepIfIdle();
    });
}

void TTWiFiTask::cancelIdleCheck() {
    if (_idleCheckHandle == 0) {
        return;
    }
    LOG_I("WiFi: cancel idle check");
    cancelRepeat(_idleCheckHandle);
    _idleCheckHandle = 0;
}

void TTWiFiTask::trySleepIfIdle() {
    if (_wifiManager.isProvisioning()) {
        return;
    }
    if (!_idleCheckReady) {
        return;
    }
    if (_useCount > 0) {
        LOG_I("WiFi: idle stay count=%u", (unsigned)_useCount);
        return;
    }
    if (_wifiManager.isConnecting()) {
        LOG_I("WiFi: idle wait, connecting");
        return;
    }
    if (!_wifiManager.isConnected()) {
        LOG_I("WiFi: idle already off count=0");
        cancelIdleCheck();
        _idleCheckReady = false;
        return;
    }
    LOG_I("WiFi: idle count=0, sleep");
    endWake();
    refreshPeriodDeadline();
}

void TTWiFiTask::refreshPeriodDeadline() {
    if (!_wifiManager.hasConfiguredNetwork() || !TTInstanceOf<TTRtc>().isTimeValid()) {
        _nextPeriodUnix = 0;
        return;
    }
    const time_t now = time(nullptr);
    if (now <= 0) {
        _nextPeriodUnix = 0;
        return;
    }
    if (_nextPeriodUnix <= now) {
        _nextPeriodUnix = now + (time_t)(TT_WIFI_WAKE_PERIOD_MS / 1000u);
        LOG_I("WiFi: period deadline unix=%ld", (long)_nextPeriodUnix);
    }
}

bool TTWiFiTask::isPeriodDue() const {
    if (_nextPeriodUnix <= 0 || !TTInstanceOf<TTRtc>().isTimeValid()) {
        return false;
    }
    const time_t now = time(nullptr);
    return now > 0 && now >= _nextPeriodUnix;
}

bool TTWiFiTask::isRadioActive() const {
    return _useCount > 0
        || _wifiManager.isProvisioning()
        || _wifiManager.isConnecting()
        || _wifiManager.isConnected();
}

void TTWiFiTask::cancelPeriod() {
    if (_nextPeriodUnix == 0) {
        return;
    }
    LOG_I("WiFi: cancel period");
    _nextPeriodUnix = 0;
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
