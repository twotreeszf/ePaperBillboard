#include "TTConfigServer.h"
#include "TTHttpFile.h"
#include "TTWiFiManager.h"
#include "TTScreenshotDisplay.h"
#include "TTFile.h"
#include "Logger.h"
#include "ErrorCheck.h"
#include "TTInstance.h"
#include "../Models/TTCalendarTypes.h"
#include "../Models/TTPictorialTypes.h"
#include "TTHttpsClient.h"
#include "TTPreference.h"
#include "TTRtc.h"
#include "../Tasks/TTSensorTask.h"
#include "../Tasks/TTWiFiTask.h"
#include "../Service/TTDebugServer.h"
#include <EPDConfig.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <cmath>
#include <cstdlib>

namespace {

bool networkKeys(std::vector<String>& keys) {
    auto& pref = TTInstanceOf<TTPreference>();
    ERR_CHECK_RET(pref.kvKeys(PREF_WIFI_NETWORKS, keys));
    return true;
}

bool textHasCtrl(const String& text) {
    for (unsigned i = 0; i < text.length(); i++) {
        const char c = text[i];
        if (c <= ' ' || c == 0x7f) {
            return true;
        }
    }
    return false;
}

bool customPicUrlOk(const String& url) {
    if (url.isEmpty() || url.length() >= TT_PIC_CUSTOM_URL_MAX || !url.startsWith("https://")) {
        return false;
    }
    if (textHasCtrl(url)) {
        return false;
    }
    const int hostStart = 8;
    int hostEnd = url.indexOf('/', hostStart);
    if (hostEnd < 0) {
        hostEnd = url.length();
    }
    int hostLen = hostEnd - hostStart;
    const int colon = url.indexOf(':', hostStart);
    if (colon >= hostStart && colon < hostEnd) {
        hostLen = colon - hostStart;
    }
    if (hostLen <= 0 || hostLen >= TT_HTTPS_HOST_MAX) {
        return false;
    }
    return url.length() - (unsigned)hostEnd < TT_HTTPS_PATH_MAX;
}

bool customPicTokenOk(const String& token) {
    if (token.isEmpty() || token.length() >= TT_PIC_CUSTOM_TOKEN_MAX) {
        return false;
    }
    for (unsigned i = 0; i < token.length(); i++) {
        const char c = token[i];
        if (c == '\r' || c == '\n') {
            return false;
        }
    }
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

String normalizeEpdPanel(const String& panel) {
    if (panel == TT_EPD_PANEL_A0_1 || panel == TT_EPD_PANEL_A0_2 || panel == TT_EPD_PANEL_A0_3) {
        return panel;
    }
    return String(TT_EPD_PANEL_DEFAULT);
}

void appendUrlText(String& out, const char* text) {
    const char* hex = "0123456789ABCDEF";
    if (text == nullptr) {
        return;
    }
    for (const char* p = text; *p != '\0'; p++) {
        const unsigned char c = (unsigned char)*p;
        const bool plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
            || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~';
        if (plain) {
            out += (char)c;
            continue;
        }
        out += '%';
        out += hex[c >> 4];
        out += hex[c & 0x0F];
    }
}

struct TTShotHttpBody {
    WiFiClient client;
    bool ok;
};

bool shotHttpWrite(void* ctx, const uint8_t* data, size_t len) {
    TTShotHttpBody* body = static_cast<TTShotHttpBody*>(ctx);
    size_t sent = 0;
    int spins = 0;
    while (sent < len) {
        const int n = body->client.write(data + sent, len - sent);
        if (n > 0) {
            sent += (size_t)n;
            spins = 0;
            continue;
        }
        if (!body->client.connected() || ++spins > 40) {
            body->ok = false;
            LOG_E("Shot: http write stalled");
            return false;
        }
        delay(10);
    }
    return true;
}

}

TTConfigServer::TTConfigServer() : _server(80) {}

void TTConfigServer::bind(TTWiFiManager* wifi) {
    _wifi = wifi;
}

void TTConfigServer::setScanList(const std::vector<String>& ssids) {
    _ssidList = ssids;
}

void TTConfigServer::handleClient() {
    _server.handleClient();
}

void TTConfigServer::stop() {
    _server.stop();
}

bool TTConfigServer::takeApply(bool& restart) {
    if (!_applyPending || (int32_t)(millis() - _applyAt) < 0) {
        return false;
    }
    _applyPending = false;
    _applyAt = 0;
    restart = _restartOnApply;
    _restartOnApply = false;
    return true;
}

void TTConfigServer::cancelApply() {
    _applyPending = false;
    _restartOnApply = false;
    _applyAt = 0;
}

void TTConfigServer::begin() {
    if (!_serverStarted) {
        _server.on("/", HTTP_GET, [this]() { _handleRoot(); });
        _server.on("/save", HTTP_POST, [this]() { _handleSave(); });
        _server.on("/status", HTTP_GET, [this]() { _handleStatus(); });
        _server.on("/scan", HTTP_GET, [this]() { _handleScanWiFi(); });
        _server.on("/dev/open", HTTP_POST, [this]() { _handleDevOpen(); });
        _server.on("/dev/save", HTTP_POST, [this]() { _handleDevSave(); });
        _server.on("/shots", HTTP_GET, [this]() { _handleShots(); });
        _server.on("/shots/list", HTTP_GET, [this]() { _handleShotList(); });
        _server.on("/shots/file", HTTP_GET, [this]() { _handleShotFile(); });
        _server.on("/shots.zip", HTTP_GET, [this]() { _handleShotsZip(); });
        _server.on("/shots/delete", HTTP_POST, [this]() { _handleShotDelete(); });
        _server.on("/shots/clear", HTTP_POST, [this]() { _handleShotsClear(); });
        _server.on("/generate_204", HTTP_GET, [this]() { _handleRoot(); });
        _server.on("/hotspot-detect.html", HTTP_GET, [this]() { _handleRoot(); });
        _server.on("/canonical.html", HTTP_GET, [this]() { _handleRoot(); });
        _server.onNotFound([this]() { _handleNotFound(); });
        _serverStarted = true;
    }
    _server.begin();
    LOG_I("WiFi: portal listening on :80 kind=%s", _kind == TT_PORTAL_LAN ? "lan" : "network");
}

void TTConfigServer::_sendPage(const char* path) {
    if (ttHttpSendFile(_server, path, "text/html; charset=utf-8")) {
        return;
    }
    LOG_E("Portal: page missing %s", path);
    _server.send(503, "text/plain; charset=utf-8", "页面文件缺失");
}

void TTConfigServer::_handleRoot() {
    _sendPage(_kind == TT_PORTAL_LAN ? TT_WWW_SETUP_PAGE : TT_WWW_NETWORK_PAGE);
}

void TTConfigServer::_sendSaveResult(int code, const char* title, const char* msg) {
    String loc = TT_WWW_SETUP_RESULT;
    loc += F("?title=");
    appendUrlText(loc, title);
    loc += F("&msg=");
    appendUrlText(loc, msg);
    if (code != 200) {
        loc += F("&err=1");
    }
    LOG_I("Portal: result %d %s", code, title != nullptr ? title : "");
    _server.sendHeader("Location", loc, true);
    _server.send(303, "text/plain", "");
    _server.client().flush();
}

void TTConfigServer::_handleSave() {
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
    String epdPanel = _server.arg("epd_panel");
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
    epdPanel.trim();
    epdPanel = normalizeEpdPanel(epdPanel);
    if (_kind == TT_PORTAL_LAN) {
        _handleLanSave(weatherCity, weatherLat, weatherLon, weatherRegion, calHost, calUser, calPass, epdPanel);
        return;
    }
    if (ssid.isEmpty()) {
        _sendSaveResult(400, "保存失败", "Wi-Fi 名称不能为空");
        return;
    }
    if (timezone.isEmpty()) {
        _sendSaveResult(400, "保存失败", "请选择时区");
        return;
    }

    auto& pref = TTInstanceOf<TTPreference>();
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
    _wifi->noteSavedNetwork(ssid.c_str());
    LOG_I("WiFi: save ssid=%s password_len=%u networks=%u tz=%s label=%s",
          ssid.c_str(), (unsigned)password.length(),
          (unsigned)pref.kvSize(PREF_WIFI_NETWORKS),
          timezone.c_str(), label.c_str());

    if (!TTRtc::saveTimezone(timezone.c_str(), label.c_str())) {
        _sendSaveResult(500, "保存失败", "时区保存失败");
        return;
    }
    LOG_I("WiFi: network portal saved, closing hotspot");
    _restartOnApply = false;
    _sendSaveResult(200, "配置已保存", "热点即将关闭，设备正在连接 Wi-Fi。可以关闭此页面。");
    _applyAt = millis() + TT_WIFI_APPLY_DELAY_MS;
    _applyPending = true;
}

void TTConfigServer::_handleLanSave(const String& weatherCity, const String& weatherLat, const String& weatherLon,
                                     const String& weatherRegion, const String& calHost, const String& calUser,
                                     String calPass, const String& epdPanel) {
    const bool devOpen = _server.arg("dev_open") == "1";
    if (devOpen && _server.arg("dev_password") != TT_DEV_PASSWORD) {
        _sendSaveResult(400, "保存失败", "开发者密码不正确");
        return;
    }
    const bool picOn = _server.arg("pic_custom") == "1";
    String picUrl = _server.arg("pic_url");
    String picToken = _server.arg("pic_token");
    picUrl.trim();
    picToken.trim();
    if (picOn && !customPicUrlOk(picUrl)) {
        _sendSaveResult(400, "保存失败", "请填写 https 开头的画报地址");
        return;
    }
    if (picOn && !customPicTokenOk(picToken)) {
        _sendSaveResult(400, "保存失败", "请填写画报 Token");
        return;
    }
    if (!picOn && !picUrl.isEmpty() && !customPicUrlOk(picUrl)) {
        _sendSaveResult(400, "保存失败", "画报地址无效");
        return;
    }
    if (!picToken.isEmpty() && !customPicTokenOk(picToken)) {
        _sendSaveResult(400, "保存失败", "画报 Token 无效");
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

    String oldEpdPanel;
    pref.get(PREF_EPD_PANEL, oldEpdPanel, String(TT_EPD_PANEL_DEFAULT));
    oldEpdPanel = normalizeEpdPanel(oldEpdPanel);
    pref.set(PREF_PIC_CUSTOM, picOn);
    pref.set(PREF_PIC_CUSTOM_URL, picUrl);
    pref.set(PREF_PIC_CUSTOM_TOKEN, picToken);
    LOG_I("Pictorial: save custom=%d url_len=%u token_len=%u",
          picOn ? 1 : 0, (unsigned)picUrl.length(), (unsigned)picToken.length());

    pref.set(PREF_EPD_PANEL, epdPanel);
    _restartOnApply = epdPanel != oldEpdPanel;
    LOG_I("Hardware: save epd_panel=%s old=%s restart=%d",
          epdPanel.c_str(), oldEpdPanel.c_str(), _restartOnApply ? 1 : 0);

    bool debugOn = false;
    if (devOpen) {
        const bool shot = _server.arg("shot") == "1";
        const bool baroOff = _server.arg("baro_off") == "1";
        debugOn = _server.arg("debug") == "1";
        if (!pref.set(PREF_DEV_SHOT, shot) || !pref.set(PREF_DEV_BARO_OFF, baroOff)
            || !pref.set(PREF_DEV_DEBUG, debugOn)) {
            _sendSaveResult(500, "保存失败", "无法保存开发者选项");
            return;
        }
        LOG_I("Dev: screenshot %s, barometer %s, debug %s",
              shot ? "on" : "off", baroOff ? "off" : "on", debugOn ? "on" : "off");
    }
    if (!pref.sync()) {
        _sendSaveResult(500, "保存失败", "无法保存设置");
        return;
    }
    if (devOpen) {
        TTInstanceOf<TTSensorTask>().requestSensorUpdateAsync();
        TTInstanceOf<TTWiFiTask>().requestDebugServer(debugOn);
    }
    if (_restartOnApply) {
        _sendSaveResult(200, "配置已保存", "屏幕型号已更改，设备即将重启。可以关闭此页面。");
        _applyAt = millis() + TT_WIFI_APPLY_DELAY_MS;
        _applyPending = true;
        return;
    }
    _sendSaveResult(200, "配置已保存", "已保存。设备保持当前网络连接。可以关闭此页面。");
}

void TTConfigServer::_handleStatus() {
    JsonDocument doc;
    auto& pref = TTInstanceOf<TTPreference>();
    std::vector<String> keys;
    networkKeys(keys);
    String ssid = _wifi->savedSsid();
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
    pref.get(PREF_WEATHER_CITY, weatherCity, String(TT_WEATHER_DEFAULT_CITY));
    if (weatherCity.isEmpty()) {
        weatherCity = TT_WEATHER_DEFAULT_CITY;
    }
    pref.get(PREF_WEATHER_LAT, weatherLat, TT_WEATHER_DEFAULT_LAT);
    pref.get(PREF_WEATHER_LON, weatherLon, TT_WEATHER_DEFAULT_LON);
    if (!isfinite(weatherLat)) {
        weatherLat = TT_WEATHER_DEFAULT_LAT;
    }
    if (!isfinite(weatherLon)) {
        weatherLon = TT_WEATHER_DEFAULT_LON;
    }
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

    bool picOn = false;
    String picUrl;
    String picToken;
    pref.get(PREF_PIC_CUSTOM, picOn, false);
    pref.get(PREF_PIC_CUSTOM_URL, picUrl, String(""));
    pref.get(PREF_PIC_CUSTOM_TOKEN, picToken, String(""));
    doc["pic_custom"] = picOn;
    doc["pic_url"] = picUrl;
    doc["pic_token"] = picToken;
    LOG_I("Pictorial: status custom=%d url_len=%u token_len=%u",
          picOn ? 1 : 0, (unsigned)picUrl.length(), (unsigned)picToken.length());

    String epdPanel;
    pref.get(PREF_EPD_PANEL, epdPanel, String(TT_EPD_PANEL_DEFAULT));
    epdPanel = normalizeEpdPanel(epdPanel);
    doc["epd_panel"] = epdPanel;
    doc["portal"] = _kind == TT_PORTAL_LAN ? "device" : "network";
    bool baroOff = false;
    bool debugOn = false;
    pref.get(PREF_DEV_BARO_OFF, baroOff, false);
    pref.get(PREF_DEV_DEBUG, debugOn, false);
    const bool shotOn = TTInstanceOf<TTScreenshotDisplay>().shotsEnabled();
    doc["dev_shot"] = shotOn;
    doc["dev_baro_off"] = baroOff;
    doc["dev_debug"] = debugOn;
    LOG_I("Portal: status kind=%s dev_shot=%d dev_baro_off=%d dev_debug=%d",
          _kind == TT_PORTAL_LAN ? "device" : "network",
          shotOn ? 1 : 0, baroOff ? 1 : 0, debugOn ? 1 : 0);
    LOG_I("WiFi: status fill ssid=%s password_len=%u tz=%s label=%s city=%s cal_host=%s cal_user=%s epd_panel=%s",
          ssid.c_str(), (unsigned)password.length(), tz, label, weatherCity.c_str(),
          calHost.c_str(), calUser.c_str(), epdPanel.c_str());
    String result;
    serializeJson(doc, result);
    _server.send(200, "application/json", result);
}

void TTConfigServer::_handleScanWiFi() {
    _server.send(200, "application/json", _getWiFiListJSON());
}

String TTConfigServer::_getWiFiListJSON() {
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

bool TTConfigServer::_devPasswordOk() {
    return _server.arg("password") == TT_DEV_PASSWORD;
}

void TTConfigServer::_handleDevOpen() {
    if (_rejectUnlessLan()) {
        return;
    }
    if (!_devPasswordOk()) {
        LOG_W("Dev: open rejected");
        _server.send(200, "application/json", "{\"ok\":false}");
        return;
    }
    LOG_I("Dev: options opened");
    _server.send(200, "application/json", "{\"ok\":true}");
}

void TTConfigServer::_handleDevSave() {
    if (_rejectUnlessLan()) {
        return;
    }
    if (!_devPasswordOk()) {
        LOG_W("Dev: save rejected");
        _sendSaveResult(400, "保存失败", "开发者密码不正确");
        return;
    }
    const bool enabled = _server.arg("shot") == "1";
    const bool baroOff = _server.arg("baro_off") == "1";
    const bool debugOn = _server.arg("debug") == "1";
    auto& pref = TTInstanceOf<TTPreference>();
    if (!pref.set(PREF_DEV_SHOT, enabled) || !pref.set(PREF_DEV_BARO_OFF, baroOff)
        || !pref.set(PREF_DEV_DEBUG, debugOn) || !pref.sync()) {
        LOG_E("Dev: save failed");
        _sendSaveResult(500, "保存失败", "无法保存开发者选项");
        return;
    }
    LOG_I("Dev: screenshot %s, barometer %s, debug %s",
          enabled ? "on" : "off", baroOff ? "off" : "on", debugOn ? "on" : "off");
    TTInstanceOf<TTSensorTask>().requestSensorUpdateAsync();
    TTInstanceOf<TTWiFiTask>().requestDebugServer(debugOn);
    _sendSaveResult(200, "配置已保存", "开发者选项已保存。");
}

void TTConfigServer::_handleShots() {
    if (_rejectUnlessLan()) {
        return;
    }
    if (!TTInstanceOf<TTScreenshotDisplay>().shotsEnabled()) {
        _server.send(404, "text/plain", "Not found");
        return;
    }
    String loc = TT_WWW_SETUP_SHOTS;
    if (_server.hasArg("dev")) {
        loc += F("?dev=");
        appendUrlText(loc, _server.arg("dev").c_str());
    }
    LOG_I("Shot: redirect page");
    _server.sendHeader("Location", loc, true);
    _server.send(302, "text/plain", "");
}

void TTConfigServer::_handleShotList() {
    if (_rejectUnlessLan()) {
        return;
    }
    if (!TTInstanceOf<TTScreenshotDisplay>().shotsEnabled()) {
        _server.send(404, "text/plain", "Not found");
        return;
    }
    auto& shots = TTInstanceOf<TTScreenshotDisplay>();
    JsonDocument doc;
    const bool busy = shots.isBusy();
    doc["busy"] = busy;
    JsonArray names = doc["shots"].to<JsonArray>();
    if (!busy) {
        char stored[TT_SHOT_MAX][TT_SHOT_NAME_MAX];
        const int count = shots.list(stored, TT_SHOT_MAX);
        for (int i = 0; i < count; i++) {
            names.add(stored[i]);
        }
        LOG_I("Shot: list %d", count);
    } else {
        LOG_I("Shot: list busy");
    }
    String result;
    serializeJson(doc, result);
    _server.send(200, "application/json", result);
}

void TTConfigServer::_handleShotFile() {
    if (_rejectUnlessLan()) {
        return;
    }
    if (!TTInstanceOf<TTScreenshotDisplay>().shotsEnabled()) {
        _server.send(404, "text/plain", "Not found");
        return;
    }
    auto& shots = TTInstanceOf<TTScreenshotDisplay>();
    if (shots.isBusy()) {
        _server.send(503, "text/plain; charset=utf-8", "busy");
        return;
    }
    char path[TT_SHOT_PATH_MAX];
    const String name = _server.arg("name");
    if (!shots.resolve(name.c_str(), path, sizeof(path))) {
        LOG_W("Shot: download missing %s", name.c_str());
        _server.send(404, "text/plain; charset=utf-8", "not found");
        return;
    }
    LOG_I("Shot: download %s", path);
    String header = F("attachment; filename=\"");
    header += name;
    header += '"';
    _server.sendHeader("Content-Disposition", header);
    if (!ttHttpSendFile(_server, path, "image/bmp")) {
        LOG_E("Shot: download open failed %s", path);
        _server.send(404, "text/plain; charset=utf-8", "not found");
    }
}

void TTConfigServer::_handleShotsZip() {
    if (_rejectUnlessLan()) {
        return;
    }
    if (!TTInstanceOf<TTScreenshotDisplay>().shotsEnabled()) {
        _server.send(404, "text/plain", "Not found");
        return;
    }
    auto& shots = TTInstanceOf<TTScreenshotDisplay>();
    if (shots.isBusy()) {
        _server.send(503, "text/plain; charset=utf-8", "busy");
        return;
    }
    size_t bytes = 0;
    if (!shots.zipBytes(&bytes) || bytes == 0) {
        _server.send(404, "text/plain; charset=utf-8", "no shots");
        return;
    }
    LOG_I("Shot: zip download %u bytes", (unsigned)bytes);
    _server.sendHeader("Content-Disposition", "attachment; filename=shots.zip");
    _server.setContentLength(bytes);
    _server.send(200, "application/zip", "");
    TTShotHttpBody body;
    body.client = _server.client();
    body.ok = true;
    if (!shots.writeZip(shotHttpWrite, &body) || !body.ok) {
        LOG_E("Shot: zip download failed");
    }
}

void TTConfigServer::_handleShotDelete() {
    if (_rejectUnlessLan()) {
        return;
    }
    if (!TTInstanceOf<TTScreenshotDisplay>().shotsEnabled()) {
        _server.send(404, "text/plain", "Not found");
        return;
    }
    if (!_devPasswordOk()) {
        LOG_W("Shot: delete rejected");
        _server.sendHeader("Location", TT_WWW_SETUP_SHOTS "?dev=denied", true);
        _server.send(303, "text/plain", "");
        return;
    }
    auto& shots = TTInstanceOf<TTScreenshotDisplay>();
    const String name = _server.arg("name");
    if (!shots.isBusy()) {
        shots.removeOne(name.c_str());
    } else {
        LOG_W("Shot: delete skipped, capture busy");
    }
    _server.sendHeader("Location", TT_WWW_SETUP_SHOTS, true);
    _server.send(303, "text/plain", "");
}

void TTConfigServer::_handleShotsClear() {
    if (_rejectUnlessLan()) {
        return;
    }
    if (!TTInstanceOf<TTScreenshotDisplay>().shotsEnabled()) {
        _server.send(404, "text/plain", "Not found");
        return;
    }
    if (!_devPasswordOk()) {
        LOG_W("Shot: clear rejected");
        _server.sendHeader("Location", TT_WWW_SETUP_SHOTS "?dev=denied", true);
        _server.send(303, "text/plain", "");
        return;
    }
    auto& shots = TTInstanceOf<TTScreenshotDisplay>();
    if (!shots.isBusy()) {
        shots.removeAll();
    } else {
        LOG_W("Shot: clear skipped, capture busy");
    }
    _server.sendHeader("Location", TT_WWW_SETUP_SHOTS, true);
    _server.send(303, "text/plain", "");
}

bool TTConfigServer::_rejectUnlessLan() {
    if (_kind == TT_PORTAL_LAN) {
        return false;
    }
    _server.send(404, "text/plain", "Not found");
    return true;
}

void TTConfigServer::_handleNotFound() {
    const String uri = _server.uri();
    if (ttHttpWwwPath(uri.c_str())) {
        if (!tt_file_exists(uri.c_str()) || !ttHttpSendFile(_server, uri.c_str(), nullptr)) {
            LOG_W("Portal: missing %s", uri.c_str());
            _server.send(404, "text/plain; charset=utf-8", "not found");
        }
        return;
    }
    if (_kind == TT_PORTAL_LAN) {
        _server.send(404, "text/plain", "Not found");
        return;
    }
    _server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString(), true);
    _server.send(302, "text/plain", "");
}

