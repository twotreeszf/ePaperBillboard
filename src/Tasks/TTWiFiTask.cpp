#include "TTWiFiTask.h"
#include "TTUITask.h"
#include "TTSensorTask.h"
#include "../Base/Logger.h"
#include "../Base/TTInstance.h"
#include "../Models/TTNotificationPayloads.h"
#include "../Base/TTRtc.h"
#include "../Base/TTPreference.h"
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
    bool debug = false;
    TTInstanceOf<TTPreference>().get(PREF_DEV_DEBUG, debug, false);
    _debugWanted = debug;
    LOG_I("Debug: option %s", debug ? "on" : "off");
    _wifiManager.seedDefaultNetwork();
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
    processBookTransfer();
    processLanPortal();
    processDebugServer();
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
        if (_wifiManager.takeNtpAfterConnect()) {
            LOG_I("WiFi: portal connected, start NTP");
            postWorkJob("ntp", [this]() { startNtpSync(); }, {});
        }
        return;
    }
    if (connectFailed || linkLost) {
        LOG_I("WiFi: %s", connectFailed ? "connect failed" : "link lost");
        if (_bookTransferWanted) {
            stopBookTransfer(TT_BOOK_TRANSFER_FAILED, connectFailed
                ? TT_BOOK_TRANSFER_ERROR_CONNECT_FAILED : TT_BOOK_TRANSFER_ERROR_LINK_LOST);
        }
        if (_lanPortalWanted) {
            _lanPortalWanted = false;
            stopLanListen();
        }
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
        _keepRadio = true;
        LOG_I("WiFi: provisioning holds radio");
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

void TTWiFiTask::requestRadioOffAsync() {
    auto* f = new std::function<void()>([this]() {
        const bool provisioning = _wifiManager.isProvisioning();
        LOG_I("WiFi: page leave provisioning=%d state=%d",
              provisioning ? 1 : 0, (int)_wifiManager.linkState());
        _keepRadio = false;
        if (provisioning) {
            _wifiManager.shutdownRadio();
        }
        if (_bookTransferWanted || _lanPortalWanted || _debugWanted || !_jobs.empty()) {
            LOG_I("WiFi: page leave, radio kept transfer=%d lan=%d debug=%d jobs=%u",
                  _bookTransferWanted ? 1 : 0, _lanPortalWanted ? 1 : 0, _debugWanted ? 1 : 0,
                  (unsigned)_jobs.size());
            publishStatus();
            return;
        }
        if (!provisioning) {
            _wifiManager.shutdownRadio();
        }
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

void TTWiFiTask::requestKeepRadio(bool keep) {
    auto* f = new std::function<void()>([this, keep]() {
        _keepRadio = keep;
        LOG_I("WiFi: keep radio=%d", keep ? 1 : 0);
        if (keep) {
            if (_wifiManager.isConnected() || _wifiManager.isConnecting()
                || _wifiManager.isProvisioning()) {
                publishStatus();
                return;
            }
            beginWake();
            return;
        }
        if (!_jobs.empty() || _wifiManager.isProvisioning()) {
            publishStatus();
            return;
        }
        if (_wifiManager.isConnected() || _wifiManager.isConnecting()) {
            LOG_I("WiFi: keep released, radio off");
            endWake();
            return;
        }
        publishStatus();
    });
    enqueue(f);
}

void TTWiFiTask::requestReleaseDriverWhenIdle(bool release) {
    auto* f = new std::function<void()>([this, release]() {
        if (release && _debugWanted) {
            _wifiManager.setReleaseDriverOnSleep(false);
            LOG_I("WiFi: keep driver, debug server is on");
            return;
        }
        _wifiManager.setReleaseDriverOnSleep(release);
        const bool active = isRadioActive();
        LOG_I("WiFi: release driver when idle=%d active=%d", release ? 1 : 0, active ? 1 : 0);
        if (release && !active) {
            _wifiManager.sleepRadio();
        }
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

void TTWiFiTask::requestStartBookTransferAsync() {
    auto* f = new std::function<void()>([this]() {
        startBookTransfer();
    });
    enqueue(f);
}

void TTWiFiTask::requestStopBookTransferAsync() {
    _bookTransfer.requestAbort();
    auto* f = new std::function<void()>([this]() {
        if (!_bookTransferWanted && !_bookTransfer.isRunning()) {
            return;
        }
        stopBookTransfer(TT_BOOK_TRANSFER_STOPPED, TT_BOOK_TRANSFER_ERROR_NONE);
    });
    enqueue(f);
}

void TTWiFiTask::startBookTransfer() {
    if (_bookTransferWanted) {
        LOG_I("Transfer: already active running=%d", _bookTransfer.isRunning() ? 1 : 0);
        publishBookTransfer(_bookTransfer.isRunning() ? TT_BOOK_TRANSFER_READY : TT_BOOK_TRANSFER_CONNECTING,
                            TT_BOOK_TRANSFER_ERROR_NONE);
        return;
    }
    if (_wifiManager.isProvisioning() || _lanPortalWanted) {
        LOG_W("Transfer: refused, settings portal provisioning=%d lan=%d",
              _wifiManager.isProvisioning() ? 1 : 0, _lanPortalWanted ? 1 : 0);
        publishBookTransfer(TT_BOOK_TRANSFER_FAILED, TT_BOOK_TRANSFER_ERROR_BUSY);
        return;
    }
    if (!_wifiManager.refreshSavedNetwork()) {
        LOG_W("Transfer: refused, no saved network");
        publishBookTransfer(TT_BOOK_TRANSFER_FAILED, TT_BOOK_TRANSFER_ERROR_NO_NETWORK);
        return;
    }
    _bookTransferWanted = true;
    LOG_I("Transfer: start, link state=%d", (int)_wifiManager.linkState());
    if (!_wifiManager.isConnected() && !_wifiManager.isConnecting()) {
        beginWake();
        if (_wakeFailed) {
            stopBookTransfer(TT_BOOK_TRANSFER_FAILED, TT_BOOK_TRANSFER_ERROR_CONNECT_FAILED);
            return;
        }
    }
    publishBookTransfer(TT_BOOK_TRANSFER_CONNECTING, TT_BOOK_TRANSFER_ERROR_NONE);
}

void TTWiFiTask::processBookTransfer() {
    if (!_bookTransferWanted) {
        return;
    }
    if (_bookTransfer.isRunning()) {
        _bookTransfer.handleClient();
        return;
    }
    if (!_wifiManager.isConnected()) {
        return;
    }
    const TTBookTransferError error = _bookTransfer.begin(
        [this](TTBookTransferState state, const char* name, uint32_t size, const char* message) {
            publishBookTransfer(state, TT_BOOK_TRANSFER_ERROR_NONE, name, size, message);
        });
    if (error != TT_BOOK_TRANSFER_ERROR_NONE) {
        stopBookTransfer(TT_BOOK_TRANSFER_FAILED, error);
        return;
    }
    WiFi.setSleep(false);
    publishBookTransfer(TT_BOOK_TRANSFER_READY, TT_BOOK_TRANSFER_ERROR_NONE);
}

void TTWiFiTask::stopBookTransfer(TTBookTransferState state, TTBookTransferError error) {
    const bool wasRunning = _bookTransfer.isRunning();
    _bookTransfer.stop();
    if (wasRunning && !_debug.isRunning() && !_wifiManager.lanPortalRunning()) {
        WiFi.setSleep(true);
    }
    _bookTransferWanted = false;
    LOG_I("Transfer: stop state=%d error=%d running=%d", (int)state, (int)error, wasRunning ? 1 : 0);
    publishBookTransfer(state, error);
}

void TTWiFiTask::requestDebugServer(bool enabled) {
    auto* f = new std::function<void()>([this, enabled]() {
        LOG_I("Debug: option %s", enabled ? "on" : "off");
        _debugWanted = enabled;
        if (!enabled) {
            stopDebugListen();
            return;
        }
        _wakeFailed = false;
        _debugRetryAt = 0;
    });
    enqueue(f);
}

void TTWiFiTask::stopDebugListen() {
    if (!_debug.isRunning()) {
        return;
    }
    _debug.stop();
    if (!_bookTransfer.isRunning() && !_wifiManager.lanPortalRunning()) {
        WiFi.setSleep(true);
    }
}

void TTWiFiTask::requestStartLanPortalAsync() {
    auto* f = new std::function<void()>([this]() {
        if (_bookTransferWanted || _wifiManager.isProvisioning()) {
            LOG_W("WiFi: lan portal refused transfer=%d provisioning=%d",
                  _bookTransferWanted ? 1 : 0, _wifiManager.isProvisioning() ? 1 : 0);
            _lanPortalWanted = false;
            publishStatus();
            return;
        }
        _lanPortalWanted = true;
        _wakeFailed = false;
        LOG_I("WiFi: lan portal requested state=%d", (int)_wifiManager.linkState());
        if (!_wifiManager.isConnected() && !_wifiManager.isConnecting()) {
            beginWake();
        }
        publishStatus();
    });
    enqueue(f);
}

void TTWiFiTask::requestStopLanPortalAsync() {
    auto* f = new std::function<void()>([this]() {
        LOG_I("WiFi: lan portal stop requested");
        _lanPortalWanted = false;
        stopLanListen();
        publishStatus();
    });
    enqueue(f);
}

void TTWiFiTask::stopLanListen() {
    const bool wasRunning = _wifiManager.lanPortalRunning();
    _wifiManager.stopLanPortal();
    if (wasRunning && !_bookTransfer.isRunning() && !_debug.isRunning()) {
        WiFi.setSleep(true);
    }
}

void TTWiFiTask::processLanPortal() {
    if (!_lanPortalWanted) {
        if (_wifiManager.lanPortalRunning()) {
            stopLanListen();
        }
        return;
    }
    if (_wifiManager.isProvisioning() || _bookTransferWanted) {
        LOG_W("WiFi: lan portal stopped, network busy");
        _lanPortalWanted = false;
        stopLanListen();
        publishStatus();
        return;
    }
    if (!_wifiManager.isConnected()) {
        return;
    }
    if (_wifiManager.lanPortalRunning()) {
        return;
    }
    if (!_wifiManager.startLanPortal()) {
        LOG_E("WiFi: lan portal start failed");
        _lanPortalWanted = false;
        publishStatus();
        return;
    }
    WiFi.setSleep(false);
    publishStatus();
}

void TTWiFiTask::processDebugServer() {
    if (!_debugWanted) {
        stopDebugListen();
        return;
    }
    const bool up = _wifiManager.isProvisioning() || _wifiManager.isConnected();
    if (!up) {
        stopDebugListen();
        if (_wifiManager.isConnecting()) {
            return;
        }
        if (_wakeFailed && (int32_t)(millis() - _debugRetryAt) < 0) {
            return;
        }
        beginWake();
        if (_wakeFailed) {
            _debugRetryAt = millis() + TT_DEBUG_RETRY_MS;
            LOG_W("Debug: connect failed, retry in %u ms", (unsigned)TT_DEBUG_RETRY_MS);
        }
        return;
    }
    if (!_debug.isRunning()) {
        _debug.begin();
        WiFi.setSleep(false);
        const String ip = _wifiManager.isProvisioning()
            ? WiFi.softAPIP().toString()
            : WiFi.localIP().toString();
        LOG_I("Debug: listening http://%s:%u", ip.c_str(), (unsigned)TT_DEBUG_HTTP_PORT);
    }
    _debug.handleClient();
}

void TTWiFiTask::publishBookTransfer(TTBookTransferState state, TTBookTransferError error,
                                     const char* name, uint32_t size, const char* message) {
    TTBookTransferPayload payload;
    memset(&payload, 0, sizeof(payload));
    payload.state = state;
    payload.error = error;
    payload.fileSize = size;
    payload.receivedCount = _bookTransfer.receivedCount();
    TTWiFiStatusPayload link;
    _wifiManager.fillStatus(link);
    strncpy(payload.ssid, link.ssid, TT_WIFI_SSID_MAX);
    if (_bookTransfer.isRunning() && link.ip[0] != '\0') {
        snprintf(payload.url, sizeof(payload.url), "http://%s/", link.ip);
    }
    if (name != nullptr) {
        strncpy(payload.fileName, name, TT_BOOK_TRANSFER_NAME_MAX);
    }
    if (message != nullptr) {
        strncpy(payload.message, message, TT_STATUS_MSG_MAX);
    }
    LOG_I("Transfer: publish state=%d error=%d ssid=%s url=%s file=%s size=%u count=%u msg=%s",
          (int)state, (int)error, payload.ssid, payload.url, payload.fileName,
          (unsigned)size, (unsigned)payload.receivedCount, payload.message);
    TTInstanceOf<TTUITask>().postNotification(TT_NOTIFICATION_BOOK_TRANSFER, payload);
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

    if (connected && !ranWork && _jobs.empty() && !connecting && !_keepRadio
        && !_bookTransferWanted && !_lanPortalWanted && !_debugWanted) {
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
    return _keepRadio
        || _bookTransferWanted
        || _lanPortalWanted
        || _debugWanted
        || !_jobs.empty()
        || _wifiManager.isProvisioning()
        || _wifiManager.isConnecting()
        || _wifiManager.isConnected();
}

bool TTWiFiTask::isRadioParked() const {
    return !isRadioActive() && _wifiManager.isRadioParked();
}

void TTWiFiTask::parkRadio() {
    _wifiManager.sleepRadio();
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
