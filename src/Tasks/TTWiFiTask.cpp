#include "TTWiFiTask.h"
#include "TTUITask.h"
#include "TTSensorTask.h"
#include "../Base/Logger.h"
#include "../Base/TTInstance.h"
#include "../Base/TTNotificationPayloads.h"
#include "../Base/TTRtc.h"
#include <ctime>
#include <cstring>
#include <string>

static void tt_wifi_copy_tag(char* out, size_t outMax, const char* tag) {
    if (out == nullptr || outMax == 0) {
        return;
    }
    if (tag == nullptr || tag[0] == '\0') {
        strncpy(out, "-", outMax - 1);
    } else {
        strncpy(out, tag, outMax - 1);
    }
    out[outMax - 1] = '\0';
}

void TTWiFiTask::setup() {
    LOG_I("WiFi task starting");
    _wifiManager.refreshSavedNetwork();
    if (!_wifiManager.hasConfiguredNetwork()) {
        LOG_I("WiFi: no SSID, radio stays off");
        _wifiManager.sleepRadio();
        publishStatus();
        return;
    }
    runWithRadio("ntp", [this]() {
        startNtpSync();
    });
}

void TTWiFiTask::loop() {
    _wifiManager.process();
    handleLinkChange();
    processRadioJobs();
}

void TTWiFiTask::handleLinkChange() {
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
        LOG_I("WiFi: provisioning, duty cycle paused");
        return;
    }
    if (becameConnected) {
        LOG_I("WiFi: connected jobs=%u", (unsigned)_jobs.size());
        _wakeFailed = false;
        return;
    }
    if (connectFailed || linkLost) {
        LOG_I("WiFi: %s", connectFailed ? "connect failed" : "link lost");
        _wakeFailed = true;
        endWake();
        return;
    }
    if (now == TT_WIFI_LINK_CONNECTING) {
        _wakeFailed = false;
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
        if (_wifiManager.isConnected() || _wifiManager.isConnecting()) {
            return;
        }
        if (_wifiManager.hasConfiguredNetwork()) {
            return;
        }
        _wifiManager.sleepRadio();
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

void TTWiFiTask::requestConnectAsync() {
    LOG_I("WiFi: page requested connect");
    runWithRadio("status", [this]() {
        publishStatus();
    });
}

void TTWiFiTask::runWithRadio(const char* tag, std::function<void()> work,
                              std::function<void()> onFailed) {
    char tagBuf[TT_WIFI_RADIO_TAG_MAX];
    tt_wifi_copy_tag(tagBuf, sizeof(tagBuf), tag);
    const std::string tagStr(tagBuf);
    LOG_I("WiFi: runWithRadio tag=%s", tagStr.c_str());
    auto* f = new std::function<void()>([this, tagStr, work, onFailed]() {
        postWorkJob(tagStr.c_str(), work, onFailed);
    });
    enqueue(f);
}

void TTWiFiTask::requestNtpSyncAsync() {
    runWithRadio("ntp", [this]() {
        startNtpSync();
    });
}

void TTWiFiTask::postWorkJob(const char* tag, std::function<void()> work,
                             std::function<void()> onFailed) {
    _wakeFailed = false;
    TTWifiJob job;
    tt_wifi_copy_tag(job.tag, sizeof(job.tag), tag);
    job.work = work;
    job.onFailed = onFailed;
    _jobs.push_back(job);
    LOG_I("WiFi: queue work tag=%s pending=%u", job.tag, (unsigned)_jobs.size());
}

void TTWiFiTask::dropFailedJobs() {
    if (_jobs.empty()) {
        return;
    }
    LOG_W("WiFi: wake failed, drop %u job(s)", (unsigned)_jobs.size());
    const std::vector<TTWifiJob> failed = std::move(_jobs);
    _jobs.clear();
    for (const TTWifiJob& job : failed) {
        if (!job.onFailed) {
            continue;
        }
        LOG_W("WiFi: fail callback tag=%s", job.tag);
        job.onFailed();
    }
}

void TTWiFiTask::runAllWorkJobs() {
    while (!_jobs.empty()) {
        TTWifiJob job = _jobs.front();
        _jobs.erase(_jobs.begin());
        LOG_I("WiFi: run work tag=%s", job.tag);
        if (job.work) {
            job.work();
        }
    }
}

void TTWiFiTask::processRadioJobs() {
    if (_wifiManager.isProvisioning()) {
        return;
    }

    const bool connected = _wifiManager.isConnected();
    const bool connecting = _wifiManager.isConnecting();
    bool ranWork = false;

    if (!_jobs.empty() && !connected && !connecting) {
        if (_wakeFailed) {
            dropFailedJobs();
        } else {
            beginWake();
        }
    } else if (connected && !_jobs.empty()) {
        runAllWorkJobs();
        ranWork = true;
    }

    if (connected && !ranWork && _jobs.empty() && !connecting) {
        LOG_I("WiFi: idle, radio off");
        endWake();
    }
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
        _wakeFailed = true;
        _wifiManager.sleepRadio();
        publishStatus();
        return;
    }
    if (_wifiManager.isConnected()) {
        LOG_I("WiFi: already connected");
        _wakeFailed = false;
        publishStatus();
        return;
    }
    if (_wifiManager.isConnecting()) {
        LOG_I("WiFi: already connecting");
        publishStatus();
        return;
    }
    LOG_I("WiFi: wake connect ssid=%s", _wifiManager.hasConfiguredNetwork() ? "saved" : "?");
    if (_wifiManager.tryConnectSaved()) {
        _wakeFailed = false;
        publishStatus();
        return;
    }
    LOG_W("WiFi: wake connect failed to start");
    _wakeFailed = true;
    publishStatus();
}

void TTWiFiTask::endWake() {
    if (_wifiManager.isProvisioning()) {
        return;
    }
    _wifiManager.sleepRadio();
    publishStatus();
}

bool TTWiFiTask::isRadioActive() const {
    return !_jobs.empty()
        || _wifiManager.isProvisioning()
        || _wifiManager.isConnecting()
        || _wifiManager.isConnected();
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
