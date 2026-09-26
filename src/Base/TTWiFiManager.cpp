#include "TTWiFiManager.h"
#include "Logger.h"
#include "ErrorCheck.h"
#include "TTInstance.h"
#include "TTCalendarTypes.h"
#include "TTPreference.h"
#include "TTRtc.h"
#include <ArduinoJson.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <esp_random.h>
#include <esp_wifi.h>

namespace {

bool networkKeys(std::vector<String>& keys) {
    auto& pref = TTInstanceOf<TTPreference>();
    ERR_CHECK_RET(pref.kvKeys(PREF_WIFI_NETWORKS, keys));
    return true;
}

bool parseCoord(const String& text, float& out, float minV, float maxV) {
    if (text.isEmpty()) {
        return false;
    }
    char* end = nullptr;
    const float value = strtof(text.c_str(), &end);
    if (end == text.c_str() || (end != nullptr && *end != '\0')) {
        return false;
    }
    if (!isfinite(value) || value < minV || value > maxV) {
        return false;
    }
    out = value;
    return true;
}

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
          (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
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
          (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
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
    ERR_CHECK_RET(_startAP());
    ERR_CHECK_RET(_startWebServer());
    return true;
}

bool TTWiFiManager::stopProvisioning() {
    _applyPending = false;
    _applyAt = 0;
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
        _server.handleClient();
        if (_applyPending && (int32_t)(millis() - _applyAt) >= 0) {
            _applyPending = false;
            _applyAt = 0;
            LOG_I("WiFi: apply portal result state=%d", (int)_state);
            _stopAP();
            _state = TT_WIFI_LINK_IDLE;
            tryConnectSaved();
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
    _dnsServer.stop();
    _server.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    _driverHeld = false;
    delay(100);
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

bool TTWiFiManager::_startWebServer() {
    if (!_serverStarted) {
        _server.on("/", HTTP_GET, [this]() { _handleRoot(); });
        _server.on("/save", HTTP_POST, [this]() { _handleSave(); });
        _server.on("/status", HTTP_GET, [this]() { _handleStatus(); });
        _server.on("/scan", HTTP_GET, [this]() { _handleScanWiFi(); });
        _server.on("/generate_204", HTTP_GET, [this]() { _handleRoot(); });
        _server.on("/hotspot-detect.html", HTTP_GET, [this]() { _handleRoot(); });
        _server.on("/canonical.html", HTTP_GET, [this]() { _handleRoot(); });
        _server.onNotFound([this]() { _handleNotFound(); });
        _serverStarted = true;
    }
    _server.begin();
    LOG_I("WiFi: portal listening on :80");
    return true;
}

void TTWiFiManager::_handleRoot() {
    _server.send(200, "text/html; charset=utf-8", _getHTMLContent());
}

void TTWiFiManager::_sendSaveResult(int code, const char* title, const char* msg) {
    String html = F("<!DOCTYPE html><html><head><meta charset='utf-8'>");
    html += F("<meta name='viewport' content='width=device-width,initial-scale=1'>");
    html += F("<title>");
    html += title;
    html += F("</title><style>body{font-family:-apple-system,sans-serif;padding:24px;text-align:center}</style>");
    html += F("</head><body><h2>");
    html += title;
    html += F("</h2><p>");
    html += msg;
    html += F("</p></body></html>");
    _server.send(code, "text/html; charset=utf-8", html);
    _server.client().flush();
}

void TTWiFiManager::_handleSave() {
    String ssid = _server.arg("ssid");
    String password = _server.arg("password");
    String timezone = _server.arg("timezone");
    String label = _server.arg("timezone_label");
    String weatherCity = _server.arg("weather_city");
    String weatherLat = _server.arg("weather_lat");
    String weatherLon = _server.arg("weather_lon");
    String weatherRegion = _server.arg("weather_region");
    String calHost = _server.arg("caldav_host");
    String calUser = _server.arg("caldav_user");
    String calPass = _server.arg("caldav_pass");
    LOG_I("WiFi: portal save args=%d ssid_len=%u tz_len=%u label_len=%u city_len=%u cal_host_len=%u",
          _server.args(), (unsigned)ssid.length(), (unsigned)timezone.length(),
          (unsigned)label.length(), (unsigned)weatherCity.length(), (unsigned)calHost.length());
    ssid.trim();
    timezone.trim();
    label.trim();
    weatherCity.trim();
    weatherLat.trim();
    weatherLon.trim();
    weatherRegion.trim();
    if (weatherRegion != TT_WEATHER_REGION_OVERSEAS) {
        weatherRegion = TT_WEATHER_REGION_CN;
    }
    calHost.trim();
    calUser.trim();
    calPass.trim();
    if (ssid.isEmpty()) {
        _sendSaveResult(400, "保存失败", "Wi-Fi 名称不能为空");
        return;
    }
    if (timezone.isEmpty()) {
        _sendSaveResult(400, "保存失败", "请选择时区");
        return;
    }

    float weatherLatVal = 0;
    float weatherLonVal = 0;
    const bool weatherHasCoord = !weatherLat.isEmpty() || !weatherLon.isEmpty();
    if (weatherHasCoord
        && (!parseCoord(weatherLat, weatherLatVal, -90.0f, 90.0f)
            || !parseCoord(weatherLon, weatherLonVal, -180.0f, 180.0f))) {
        _sendSaveResult(400, "保存失败", "请填写有效的纬度和经度");
        return;
    }

    const bool calAny = !calHost.isEmpty() || !calUser.isEmpty() || !calPass.isEmpty();
    if (calAny && (calHost.isEmpty() || calUser.isEmpty())) {
        _sendSaveResult(400, "保存失败", "请填写日历服务器和账号");
        return;
    }
    if (calHost.length() >= TT_CAL_HOST_MAX
        || calUser.length() >= TT_CAL_USER_MAX
        || calPass.length() >= TT_CAL_PASS_MAX) {
        _sendSaveResult(400, "保存失败", "日历配置过长");
        return;
    }

    auto& pref = TTInstanceOf<TTPreference>();
    if (calAny && calPass.isEmpty()) {
        pref.get(PREF_CALDAV_PASS, calPass, String(""));
    }
    if (calAny && calPass.isEmpty()) {
        _sendSaveResult(400, "保存失败", "请填写日历密码");
        return;
    }

    if (password.isEmpty()) {
        pref.getKv(PREF_WIFI_NETWORKS, ssid.c_str(), password, String(""));
        if (!password.isEmpty()) {
            LOG_I("WiFi: keep existing password ssid=%s", ssid.c_str());
        }
    }
    if (!pref.setKv(PREF_WIFI_NETWORKS, ssid.c_str(), password)
        || !pref.sync()) {
        _sendSaveResult(500, "保存失败", "Wi-Fi 保存失败");
        return;
    }
    _hasNetworks = true;
    _rememberSsid(ssid);
    LOG_I("WiFi: save ssid=%s password_len=%u networks=%u tz=%s label=%s",
          ssid.c_str(), (unsigned)password.length(),
          (unsigned)pref.kvSize(PREF_WIFI_NETWORKS),
          timezone.c_str(), label.c_str());

    if (!TTRtc::saveTimezone(timezone.c_str(), label.c_str())) {
        _sendSaveResult(500, "保存失败", "时区保存失败");
        return;
    }

    pref.set(PREF_WEATHER_CITY, weatherCity);
    pref.set(PREF_WEATHER_REGION, weatherRegion);
    LOG_I("Weather: save region=%s", weatherRegion.c_str());
    if (!weatherHasCoord) {
        pref.remove(PREF_WEATHER_LAT);
        pref.remove(PREF_WEATHER_LON);
        LOG_I("Weather: cleared lat/lon city=%s", weatherCity.c_str());
    } else {
        pref.set(PREF_WEATHER_LAT, weatherLatVal);
        pref.set(PREF_WEATHER_LON, weatherLonVal);
        LOG_I("Weather: save city=%s lat=%.4f lon=%.4f", weatherCity.c_str(), weatherLatVal, weatherLonVal);
    }

    if (!calAny) {
        pref.remove(PREF_CALDAV_HOST);
        pref.remove(PREF_CALDAV_USER);
        pref.remove(PREF_CALDAV_PASS);
        LOG_I("CalDAV: cleared account");
    } else {
        pref.set(PREF_CALDAV_HOST, calHost);
        pref.set(PREF_CALDAV_USER, calUser);
        pref.set(PREF_CALDAV_PASS, calPass);
        LOG_I("CalDAV: save host=%s user=%s pass_len=%u",
              calHost.c_str(), calUser.c_str(), (unsigned)calPass.length());
    }
    pref.sync();

    _sendSaveResult(200, "配置已保存", "热点即将关闭，设备正在连接 Wi-Fi。可以关闭此页面。");
    _applyAt = millis() + TT_WIFI_APPLY_DELAY_MS;
    _applyPending = true;
}

void TTWiFiManager::_handleStatus() {
    JsonDocument doc;
    auto& pref = TTInstanceOf<TTPreference>();
    std::vector<String> keys;
    networkKeys(keys);
    String ssid = _savedSsid;
    if (ssid.isEmpty() || !pref.hasKv(PREF_WIFI_NETWORKS, ssid.c_str())) {
        ssid = keys.empty() ? String("") : keys[0];
    }
    String password;
    pref.getKv(PREF_WIFI_NETWORKS, ssid.c_str(), password, String(""));
    doc["ssid"] = ssid;
    doc["password"] = password;

    char tz[TT_TZ_MAX + 1] = {0};
    char label[TT_TZ_MAX + 1] = {0};
    if (TTRtc::loadTimezone(tz, sizeof(tz))) {
        doc["timezone"] = tz;
    }
    if (TTRtc::loadTimezoneLabel(label, sizeof(label))) {
        doc["label"] = label;
    }

    String weatherCity;
    float weatherLat = NAN;
    float weatherLon = NAN;
    pref.get(PREF_WEATHER_CITY, weatherCity, String(""));
    pref.get(PREF_WEATHER_LAT, weatherLat, NAN);
    pref.get(PREF_WEATHER_LON, weatherLon, NAN);
    doc["weather_city"] = weatherCity;
    String weatherRegion;
    pref.get(PREF_WEATHER_REGION, weatherRegion, String(TT_WEATHER_REGION_CN));
    if (weatherRegion != TT_WEATHER_REGION_OVERSEAS) {
        weatherRegion = TT_WEATHER_REGION_CN;
    }
    doc["weather_region"] = weatherRegion;
    if (isfinite(weatherLat)) {
        doc["weather_lat"] = weatherLat;
    }
    if (isfinite(weatherLon)) {
        doc["weather_lon"] = weatherLon;
    }

    String calHost;
    String calUser;
    String calPass;
    pref.get(PREF_CALDAV_HOST, calHost, String(""));
    pref.get(PREF_CALDAV_USER, calUser, String(""));
    pref.get(PREF_CALDAV_PASS, calPass, String(""));
    doc["caldav_host"] = calHost;
    doc["caldav_user"] = calUser;
    doc["caldav_pass"] = calPass;
    LOG_I("WiFi: status fill ssid=%s password_len=%u tz=%s label=%s city=%s cal_host=%s cal_user=%s",
          ssid.c_str(), (unsigned)password.length(), tz, label, weatherCity.c_str(),
          calHost.c_str(), calUser.c_str());
    String result;
    serializeJson(doc, result);
    _server.send(200, "application/json", result);
}

void TTWiFiManager::_handleScanWiFi() {
    _server.send(200, "application/json", _getWiFiListJSON());
}

String TTWiFiManager::_getWiFiListJSON() {
    JsonDocument doc;
    JsonArray array = doc["networks"].to<JsonArray>();
    for (const String& ssid : _ssidList) {
        JsonObject network = array.add<JsonObject>();
        network["ssid"] = ssid;
    }
    String result;
    serializeJson(doc, result);
    return result;
}

void TTWiFiManager::_handleNotFound() {
    if (_state == TT_WIFI_LINK_PROVISIONING) {
        _server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString(), true);
        _server.send(302, "text/plain", "");
        return;
    }
    _server.send(404, "text/plain", "Not found");
}

String TTWiFiManager::_getHTMLContent() {
    return String(R"html(
<!DOCTYPE html>
<html>
<head>
    <meta charset="utf-8">
    <title>设备设置</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { font-family: -apple-system, sans-serif; margin: 0; padding: 16px; background: #f4f4f4; }
        .box { max-width: 400px; margin: 0 auto; background: #fff; padding: 20px; border-radius: 8px; }
        h2 { margin: 0 0 8px; text-align: center; }
        label { display: block; margin: 12px 0 6px; }
        select, input { width: 100%; padding: 10px; font-size: 16px; box-sizing: border-box; }
        .tabs { display: flex; gap: 6px; margin: 12px 0 4px; }
        .tab { flex: 1; margin: 0; padding: 8px 4px; font-size: 14px; background: #eee; color: #111; border: 0; border-radius: 6px; }
        .tab.on { background: #111; color: #fff; }
        .panel { display: none; }
        .panel.on { display: block; }
        button.save { width: 100%; margin-top: 20px; padding: 12px; font-size: 16px; background: #111; color: #fff; border: 0; border-radius: 6px; }
        button.save:disabled { background: #999; }
        .tip { color: #666; font-size: 13px; margin: 8px 0; }
    </style>
</head>
<body>
    <div class="box">
        <h2>设备设置</h2>
        <p class="tip">按分类填写。完成配置后设备将关闭热点并以 STA 模式连接。</p>
        <form method="post" action="/save" onsubmit="return onSubmit()">
            <div class="tabs">
                <button type="button" class="tab on" data-tab="wifi">Wi-Fi</button>
                <button type="button" class="tab" data-tab="tz">时区</button>
                <button type="button" class="tab" data-tab="weather">天气</button>
                <button type="button" class="tab" data-tab="cal">日历</button>
            </div>
            <div class="panel on" id="panel-wifi">
                <label>名称</label>
                <select id="ssid-select" onchange="document.getElementById('ssid').value=this.value">
                    <option value="">正在扫描...</option>
                </select>
                <input type="text" id="ssid" name="ssid" placeholder="或手动输入名称">
                <label>密码</label>
                <input type="text" id="password" name="password" placeholder="Wi-Fi 密码" autocomplete="off">
            </div>
            <div class="panel" id="panel-tz">
                <label>地区</label>
                <select id="region" onchange="fillCities()"></select>
                <label>城市</label>
                <select id="city" onchange="applyCity()"></select>
                <input type="hidden" id="timezone" name="timezone" value="CST-8">
                <input type="hidden" id="timezone_label" name="timezone_label" value="上海">
            </div>
            <div class="panel" id="panel-weather">
                <p class="tip">大陆使用彩云天气，海外使用现有预报。城市名仅用于展示。纬度为负表示南纬，经度为负表示西经。</p>
                <label>数据源</label>
                <select id="weather_region" name="weather_region">
                    <option value="cn" selected>大陆</option>
                    <option value="overseas">海外</option>
                </select>
                <label>城市</label>
                <input type="text" id="weather_city" name="weather_city" placeholder="例如 上海" maxlength="32">
                <label>纬度</label>
                <input type="text" id="weather_lat" name="weather_lat" placeholder="例如 31.2304" inputmode="decimal">
                <label>经度</label>
                <input type="text" id="weather_lon" name="weather_lon" placeholder="例如 121.4737" inputmode="decimal">
            </div>
            <div class="panel" id="panel-cal">
                <p class="tip">CalDAV 账号。密码留空则保留已保存的密码。三项都留空则清除日历配置。</p>
                <label>服务器</label>
                <input type="text" id="caldav_host" name="caldav_host" placeholder="例如 caldav.feishu.cn" maxlength="47" autocomplete="off">
                <label>账号</label>
                <input type="text" id="caldav_user" name="caldav_user" placeholder="CalDAV 用户名" maxlength="31" autocomplete="off">
                <label>密码</label>
                <input type="text" id="caldav_pass" name="caldav_pass" placeholder="CalDAV 密码" maxlength="31" autocomplete="off">
            </div>
            <button class="save" type="submit">完成配置</button>
        </form>
    </div>
    <script>
        const CITIES = {
            "亚洲":[["上海","CST-8"],["香港","HKT-8"],["台北","CST-8"],["新加坡","SGT-8"],["东京","JST-9"],["首尔","KST-9"],["曼谷","ICT-7"],["迪拜","GST-4"]],
            "欧洲":[["伦敦","GMT0"],["巴黎","CET-1"],["柏林","CET-1"],["莫斯科","MSK-3"]],
            "美洲":[["纽约","EST5"],["芝加哥","CST6"],["洛杉矶","PST8"],["丹佛","MST7"],["圣保罗","BRT3"]],
            "大洋洲":[["悉尼","AEST-10"],["奥克兰","NZST-12"]],
            "UTC":[["UTC","GMT0"]]
        };
        function fillRegions() {
            const r = document.getElementById('region');
            r.innerHTML = '';
            Object.keys(CITIES).forEach(name => {
                const o = document.createElement('option');
                o.value = name; o.textContent = name; r.appendChild(o);
            });
        }
        function fillCities() {
            const list = CITIES[document.getElementById('region').value] || [];
            const c = document.getElementById('city');
            c.innerHTML = '';
            list.forEach(item => {
                const o = document.createElement('option');
                o.value = item[1]; o.textContent = item[0]; o.dataset.label = item[0];
                c.appendChild(o);
            });
            applyCity();
        }
        function applyCity() {
            const c = document.getElementById('city');
            const opt = c.options[c.selectedIndex];
            document.getElementById('timezone').value = opt ? opt.value : '';
            document.getElementById('timezone_label').value = opt ? opt.dataset.label : '';
        }
        function selectCity(tz, label) {
            let found = null;
            Object.keys(CITIES).forEach(region => {
                CITIES[region].forEach(item => {
                    if (label && item[0] === label) found = { region, item };
                });
            });
            if (!found && tz) {
                Object.keys(CITIES).some(region =>
                    CITIES[region].some(item => {
                        if (item[1] === tz) { found = { region, item }; return true; }
                        return false;
                    })
                );
            }
            if (!found) return;
            document.getElementById('region').value = found.region;
            fillCities();
            const c = document.getElementById('city');
            for (let i = 0; i < c.options.length; i++) {
                if (c.options[i].dataset.label === found.item[0] && c.options[i].value === found.item[1]) {
                    c.selectedIndex = i;
                }
            }
            applyCity();
        }
        function fillSaved(status, scan) {
            const s = document.getElementById('ssid-select');
            s.innerHTML = '<option value="">选择网络...</option>';
            const networks = scan.networks || [];
            networks.forEach(n => {
                const o = document.createElement('option');
                o.value = n.ssid; o.textContent = n.ssid; s.appendChild(o);
            });
            if (status.ssid && networks.every(n => n.ssid !== status.ssid)) {
                const o = document.createElement('option');
                o.value = status.ssid;
                o.textContent = status.ssid + '（已保存）';
                s.appendChild(o);
            }
            if (status.ssid) {
                document.getElementById('ssid').value = status.ssid;
                s.value = status.ssid;
            }
            if (status.password) {
                document.getElementById('password').value = status.password;
            }
            if (status.timezone || status.label) {
                selectCity(status.timezone || '', status.label || '');
            }
            if (status.weather_region) {
                document.getElementById('weather_region').value = status.weather_region;
            }
            if (status.weather_city) {
                document.getElementById('weather_city').value = status.weather_city;
            }
            if (status.weather_lat !== undefined && status.weather_lat !== null) {
                document.getElementById('weather_lat').value = status.weather_lat;
            }
            if (status.weather_lon !== undefined && status.weather_lon !== null) {
                document.getElementById('weather_lon').value = status.weather_lon;
            }
            if (status.caldav_host) {
                document.getElementById('caldav_host').value = status.caldav_host;
            }
            if (status.caldav_user) {
                document.getElementById('caldav_user').value = status.caldav_user;
            }
            if (status.caldav_pass) {
                document.getElementById('caldav_pass').value = status.caldav_pass;
            }
        }
        function showTab(name) {
            document.querySelectorAll('.tab').forEach(el => {
                el.classList.toggle('on', el.dataset.tab === name);
            });
            document.querySelectorAll('.panel').forEach(el => {
                el.classList.toggle('on', el.id === 'panel-' + name);
            });
        }
        document.querySelectorAll('.tab').forEach(el => {
            el.addEventListener('click', () => showTab(el.dataset.tab));
        });
        function onSubmit() {
            applyCity();
            const ssid = document.getElementById('ssid').value.replace(/^\s+|\s+$/g, '');
            document.getElementById('ssid').value = ssid;
            if (!ssid) {
                showTab('wifi');
                alert('请选择或输入 Wi-Fi 名称');
                return false;
            }
            if (!document.getElementById('timezone').value) {
                showTab('tz');
                alert('请选择时区');
                return false;
            }
            const btn = document.querySelector('button.save');
            btn.disabled = true;
            btn.textContent = '保存中...';
            return true;
        }
        fillRegions();
        fillCities();
        Promise.all([
            fetch('/status').then(r => r.json()).catch(() => ({})),
            fetch('/scan').then(r => r.json()).catch(() => ({}))
        ]).then(([status, scan]) => fillSaved(status, scan));
    </script>
</body>
</html>
)html");
}
