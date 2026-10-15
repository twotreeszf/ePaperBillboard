#include "TTWiFiManager.h"
#include "Logger.h"
#include "ErrorCheck.h"
#include "TTInstance.h"
#include "TTPreference.h"
#include "Util.h"
#include "../Tasks/TTUITask.h"
#include <algorithm>
#include <cstring>
#include <esp_random.h>
#include <esp_wifi.h>

namespace {

bool networkKeys(std::vector<String>& keys) {
    auto& pref = TTInstanceOf<TTPreference>();
    ERR_CHECK_RET(pref.kvKeys(PREF_WIFI_NETWORKS, keys));
    return true;
}

}

TTWiFiManager::TTWiFiManager() {
    _portal.bind(this);
}

void TTWiFiManager::noteSavedNetwork(const char* ssid) {
    _hasNetworks = true;
    _rememberSsid(ssid != nullptr ? ssid : "");
}

void TTWiFiManager::_rememberSsid(const String& ssid) {
    strncpy(_savedSsid, ssid.c_str(), TT_WIFI_SSID_MAX);
    _savedSsid[TT_WIFI_SSID_MAX] = '\0';
}

bool TTWiFiManager::refreshSavedNetwork() {
    std::vector<String> keys;
    ERR_CHECK_RET(networkKeys(keys));
    _hasNetworks = !keys.empty();
    if (!_hasNetworks) {
        _savedSsid[0] = '\0';
        LOG_I("WiFi: no saved networks");
        return false;
    }
    auto& pref = TTInstanceOf<TTPreference>();
    String last;
    pref.get(PREF_WIFI_LAST_SSID, last, String(""));
    if (!last.isEmpty() && pref.hasKv(PREF_WIFI_NETWORKS, last.c_str())) {
        _rememberSsid(last);
    } else if (_savedSsid[0] != '\0' && !pref.hasKv(PREF_WIFI_NETWORKS, _savedSsid)) {
        _savedSsid[0] = '\0';
    }
    LOG_I("WiFi: saved networks=%u last=%s", (unsigned)keys.size(),
          _savedSsid[0] != '\0' ? _savedSsid : "-");
    return true;
}

bool TTWiFiManager::resumeRadio() {
    if (!_driverHeld) {
        return true;
    }
    const esp_err_t err = esp_wifi_start();
    if (err != ESP_OK) {
        LOG_E("WiFi: start err=%d", (int)err);
        return false;
    }
    _driverHeld = false;
    LOG_I("WiFi: radio resume heap=%u largest=%u",
          (unsigned)Util::heapFree(), (unsigned)Util::heapLargest());
    return true;
}

bool TTWiFiManager::isRadioParked() const {
    return _driverHeld || WiFi.getMode() == WIFI_OFF;
}

bool TTWiFiManager::sleepRadio() {
    if (_state == TT_WIFI_LINK_PROVISIONING) {
        LOG_W("WiFi: sleep ignored, provisioning");
        return false;
    }
    _state = TT_WIFI_LINK_IDLE;
    _connectStartedAt = 0;
    _fallbackScan = false;
    _ntpAfterConnect = false;
    if (_driverHeld || WiFi.getMode() == WIFI_OFF) {
        return true;
    }
    LOG_I("WiFi: radio stop");
    WiFi.disconnect(false, false);
    const esp_err_t err = esp_wifi_stop();
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_STARTED && err != ESP_ERR_WIFI_NOT_INIT) {
        LOG_W("WiFi: stop err=%d", (int)err);
        return false;
    }
    if (err == ESP_OK || err == ESP_ERR_WIFI_NOT_STARTED) {
        _driverHeld = true;
    }
    LOG_I("WiFi: radio stopped held=%d heap=%u largest=%u",
          _driverHeld ? 1 : 0,
          (unsigned)Util::heapFree(), (unsigned)Util::heapLargest());
    return true;
}

bool TTWiFiManager::tryConnectSaved() {
    std::vector<String> keys;
    ERR_CHECK_RET(networkKeys(keys));
    _hasNetworks = !keys.empty();
    if (!_hasNetworks) {
        LOG_I("WiFi: no saved SSID");
        _savedSsid[0] = '\0';
        if (_state != TT_WIFI_LINK_PROVISIONING) {
            sleepRadio();
        } else {
            _state = TT_WIFI_LINK_IDLE;
        }
        return false;
    }

    if (_startPreferredConnect()) {
        _fallbackScan = true;
        return true;
    }
    LOG_I("WiFi: no last SSID, scan nearby");
    if (_connectFromScan(nullptr)) {
        _fallbackScan = false;
        return true;
    }
    sleepRadio();
    return false;
}

bool TTWiFiManager::startProvisioning() {
    if (_state == TT_WIFI_LINK_PROVISIONING) {
        LOG_I("WiFi: provisioning already running");
        return true;
    }

    LOG_I("WiFi: start phone provisioning");
    if (_state == TT_WIFI_LINK_CONNECTED || _state == TT_WIFI_LINK_CONNECTING) {
        WiFi.disconnect(false, false);
        delay(100);
    }

    if (!_scanWiFi()) {
        LOG_W("WiFi: scan failed, continue with empty list");
    } else {
        LOG_I("WiFi: cached %u SSIDs before AP", (unsigned)_ssidList.size());
    }
    _portal.setScanList(_ssidList);
    ERR_CHECK_RET(_startAP());
    _portal.begin();
    return true;
}

bool TTWiFiManager::takeNtpAfterConnect() {
    if (!_ntpAfterConnect) {
        return false;
    }
    _ntpAfterConnect = false;
    return true;
}

bool TTWiFiManager::stopProvisioning() {
    _portal.cancelApply();
    _ntpAfterConnect = false;
    if (_state != TT_WIFI_LINK_PROVISIONING) {
        return true;
    }
    LOG_I("WiFi: stop portal state=%d", (int)_state);
    _stopAP();
    _state = TT_WIFI_LINK_IDLE;
    tryConnectSaved();
    return true;
}

void TTWiFiManager::process() {
    if (_state == TT_WIFI_LINK_PROVISIONING) {
        _dnsServer.processNextRequest();
        _portal.handleClient();
        bool restart = false;
        if (_portal.takeApply(restart)) {
            LOG_I("WiFi: apply portal result state=%d restart=%d", (int)_state, restart ? 1 : 0);
            _stopAP();
            _state = TT_WIFI_LINK_IDLE;
            if (restart) {
                TTInstanceOf<TTUITask>().requestRestartAsync();
                return;
            }
            if (tryConnectSaved()) {
                _ntpAfterConnect = true;
                LOG_I("WiFi: NTP armed after portal connect");
            }
        }
        return;
    }
    if (_state == TT_WIFI_LINK_CONNECTING) {
        _pollConnect();
        return;
    }
    if (_state == TT_WIFI_LINK_CONNECTED && WiFi.status() != WL_CONNECTED) {
        LOG_W("WiFi: STA lost");
        sleepRadio();
    }
}

void TTWiFiManager::fillStatus(TTWiFiStatusPayload& out) const {
    memset(&out, 0, sizeof(out));
    out.state = _state;
    strncpy(out.apSsid, _apSsid, TT_WIFI_SSID_MAX);
    out.apSsid[TT_WIFI_SSID_MAX] = '\0';
    out.apPassword[0] = '\0';

    if (_state == TT_WIFI_LINK_PROVISIONING) {
        String ip = WiFi.softAPIP().toString();
        snprintf(out.portalUrl, sizeof(out.portalUrl), "http://%s", ip.c_str());
    } else if (_state == TT_WIFI_LINK_CONNECTED) {
        strncpy(out.ssid, WiFi.SSID().c_str(), TT_WIFI_SSID_MAX);
        out.ssid[TT_WIFI_SSID_MAX] = '\0';
        strncpy(out.ip, WiFi.localIP().toString().c_str(), TT_WIFI_IP_MAX);
        out.ip[TT_WIFI_IP_MAX] = '\0';
    } else {
        strncpy(out.ssid, _savedSsid, TT_WIFI_SSID_MAX);
        out.ssid[TT_WIFI_SSID_MAX] = '\0';
    }
}

bool TTWiFiManager::_startConnect(const String& ssid, const String& password) {
    LOG_I("WiFi: connecting ssid=%s", ssid.c_str());
    _state = TT_WIFI_LINK_CONNECTING;
    _connectStartedAt = millis();

    if (!resumeRadio() || !WiFi.mode(WIFI_STA)) {
        LOG_E("WiFi: sta start failed ssid=%s", ssid.c_str());
        _state = TT_WIFI_LINK_IDLE;
        _connectStartedAt = 0;
        return false;
    }
    WiFi.disconnect();
    delay(100);
    _applyTxPower();
    if (password.isEmpty()) {
        WiFi.begin(ssid.c_str());
    } else {
        WiFi.begin(ssid.c_str(), password.c_str());
    }
    return true;
}

void TTWiFiManager::_pollConnect() {
    if (WiFi.status() == WL_CONNECTED) {
        const String ssid = WiFi.SSID();
        LOG_I("WiFi: connected ssid=%s ip=%s", ssid.c_str(), WiFi.localIP().toString().c_str());
        _state = TT_WIFI_LINK_CONNECTED;
        _connectStartedAt = 0;
        _fallbackScan = false;
        _persistLastSsid(ssid);
        return;
    }
    if ((int32_t)(millis() - _connectStartedAt) >= (int32_t)TT_WIFI_CONNECT_TIMEOUT_MS) {
        LOG_E("WiFi: connect timeout ssid=%s", _savedSsid);
        if (_fallbackScan) {
            _fallbackScan = false;
            const String skip = _savedSsid;
            LOG_W("WiFi: last SSID failed, scan nearby skip=%s", skip.c_str());
            if (_connectFromScan(skip.c_str())) {
                return;
            }
        }
        sleepRadio();
    }
}

bool TTWiFiManager::_startPreferredConnect() {
    auto& pref = TTInstanceOf<TTPreference>();
    if (_savedSsid[0] == '\0' || !pref.hasKv(PREF_WIFI_NETWORKS, _savedSsid)) {
        String last;
        pref.get(PREF_WIFI_LAST_SSID, last, String(""));
        if (last.isEmpty() || !pref.hasKv(PREF_WIFI_NETWORKS, last.c_str())) {
            return false;
        }
        _rememberSsid(last);
    }
    String password;
    if (!pref.getKv(PREF_WIFI_NETWORKS, _savedSsid, password, String(""))) {
        return false;
    }
    LOG_I("WiFi: try last ssid=%s", _savedSsid);
    return _startConnect(String(_savedSsid), password);
}

bool TTWiFiManager::_connectFromScan(const char* skipSsid) {
    std::vector<String> nearby;
    if (!_scanNearby(nearby)) {
        LOG_W("WiFi: scan failed, cannot pick saved SSID");
        return false;
    }
    String ssid;
    String password;
    if (!_chooseSavedFromScan(nearby, ssid, password, skipSsid)) {
        LOG_W("WiFi: no saved SSID in scan nearby=%u skip=%s",
              (unsigned)nearby.size(),
              (skipSsid != nullptr && skipSsid[0] != '\0') ? skipSsid : "-");
        return false;
    }
    _rememberSsid(ssid);
    if (!_startConnect(ssid, password)) {
        LOG_W("WiFi: scan connect start failed ssid=%s", ssid.c_str());
        return false;
    }
    return true;
}

void TTWiFiManager::_persistLastSsid(const String& ssid) {
    if (ssid.isEmpty()) {
        return;
    }
    _rememberSsid(ssid);
    auto& pref = TTInstanceOf<TTPreference>();
    String last;
    pref.get(PREF_WIFI_LAST_SSID, last, String(""));
    if (last == ssid) {
        return;
    }
    if (!pref.set(PREF_WIFI_LAST_SSID, ssid) || !pref.sync()) {
        LOG_W("WiFi: persist last ssid failed ssid=%s", ssid.c_str());
        return;
    }
    LOG_I("WiFi: persist last ssid=%s", ssid.c_str());
}

void TTWiFiManager::_applyTxPower() {
    if (!WiFi.setTxPower(TT_WIFI_TX_POWER)) {
        LOG_W("WiFi: set tx power failed");
        return;
    }
    LOG_I("WiFi: tx power=%d", (int)WiFi.getTxPower());
}

void TTWiFiManager::_buildApSsid() {
    uint32_t suffix = (uint32_t)(ESP.getEfuseMac() & 0xFFFF);
    snprintf(_apSsid, sizeof(_apSsid), "%s-%04X", TT_WIFI_AP_SSID_PREFIX, (unsigned)suffix);
}

bool TTWiFiManager::_startAP() {
    _buildApSsid();
    ERR_CHECK_RET(resumeRadio());
    ERR_CHECK_RET(WiFi.mode(WIFI_AP));
    delay(100);
    _applyTxPower();
    ERR_CHECK_RET(WiFi.softAP(_apSsid));
    ERR_CHECK_RET(_dnsServer.start(TT_WIFI_DNS_PORT, "*", WiFi.softAPIP()));
    LOG_I("WiFi: AP ssid=%s ip=%s open", _apSsid, WiFi.softAPIP().toString().c_str());
    _state = TT_WIFI_LINK_PROVISIONING;
    return true;
}

void TTWiFiManager::_stopAP() {
    LOG_I("WiFi: stop AP");
    _dnsServer.stop();
    _portal.stop();
    if (!WiFi.softAPdisconnect(false)) {
        LOG_W("WiFi: softAP disconnect failed");
    }
    const wifi_mode_t mode = WiFi.getMode();
    if (mode != WIFI_OFF && mode != WIFI_STA) {
        if (!WiFi.mode(WIFI_STA)) {
            LOG_W("WiFi: leave AP mode failed mode=%d", (int)mode);
        }
    }
    _state = TT_WIFI_LINK_IDLE;
    sleepRadio();
}

bool TTWiFiManager::_scanNearby(std::vector<String>& out) {
    out.clear();
    WiFi.scanDelete();

    ERR_CHECK_RET(resumeRadio());
    ERR_CHECK_RET(WiFi.mode(WIFI_STA));
    WiFi.disconnect(false, false);
    delay(200);
    _applyTxPower();

    int n = WiFi.scanNetworks(false, true);
    if (n <= 0) {
        LOG_W("WiFi: first scan result=%d, retry", n);
        WiFi.scanDelete();
        delay(200);
        n = WiFi.scanNetworks(false, true);
    }
    if (n < 0) {
        LOG_W("WiFi: scan failed result=%d", n);
        WiFi.scanDelete();
        return false;
    }
    if (n == 0) {
        LOG_W("WiFi: no networks found");
        WiFi.scanDelete();
        return true;
    }

    LOG_I("WiFi: found %d networks", n);
    for (int i = 0; i < n; ++i) {
        String ssid = WiFi.SSID(i);
        if (ssid.length() == 0) {
            continue;
        }
        LOG_I("WiFi: ssid[%d]=%s rssi=%d", i, ssid.c_str(), WiFi.RSSI(i));
        out.push_back(ssid);
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    WiFi.scanDelete();
    return true;
}

bool TTWiFiManager::_chooseSavedFromScan(const std::vector<String>& nearby,
                                         String& ssid, String& password,
                                         const char* skipSsid) {
    std::vector<String> keys;
    if (!networkKeys(keys)) {
        return false;
    }
    std::vector<String> matches;
    for (const String& saved : keys) {
        if (skipSsid != nullptr && skipSsid[0] != '\0' && saved == skipSsid) {
            continue;
        }
        if (std::find(nearby.begin(), nearby.end(), saved) == nearby.end()) {
            continue;
        }
        matches.push_back(saved);
    }
    if (matches.empty()) {
        return false;
    }
    const size_t idx = (size_t)(esp_random() % (uint32_t)matches.size());
    ssid = matches[idx];
    auto& pref = TTInstanceOf<TTPreference>();
    if (!pref.getKv(PREF_WIFI_NETWORKS, ssid.c_str(), password, String(""))) {
        return false;
    }
    LOG_I("WiFi: pick ssid=%s matches=%u nearby=%u",
          ssid.c_str(), (unsigned)matches.size(), (unsigned)nearby.size());
    return true;
}

bool TTWiFiManager::_scanWiFi() {
    LOG_I("WiFi: scanning before AP");
    return _scanNearby(_ssidList);
}

