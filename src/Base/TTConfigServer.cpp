#include "TTConfigServer.h"
#include "TTWiFiManager.h"
#include "TTScreenshotDisplay.h"
#include "TTFile.h"
#include "Logger.h"
#include "ErrorCheck.h"
#include "TTInstance.h"
#include "../Models/TTCalendarTypes.h"
#include "TTPreference.h"
#include "TTRtc.h"
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

void appendHtmlText(String& out, const char* text) {
    if (text == nullptr) {
        return;
    }
    for (const char* p = text; *p != '\0'; p++) {
        const unsigned char c = (unsigned char)*p;
        if (c == '&') {
            out += F("&amp;");
        } else if (c == '<') {
            out += F("&lt;");
        } else if (c == '>') {
            out += F("&gt;");
        } else if (c == '"') {
            out += F("&quot;");
        } else {
            out += (char)c;
        }
    }
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
    LOG_I("WiFi: portal listening on :80");
}

void TTConfigServer::_handleRoot() {
    const bool denied = _server.hasArg("dev") && _server.arg("dev") == "denied";
    _server.send(200, "text/html; charset=utf-8", _getHTMLContent(false, denied));
}

void TTConfigServer::_sendSaveResult(int code, const char* title, const char* msg) {
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
    _wifi->noteSavedNetwork(ssid.c_str());
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

    String oldEpdPanel;
    pref.get(PREF_EPD_PANEL, oldEpdPanel, String(TT_EPD_PANEL_DEFAULT));
    oldEpdPanel = normalizeEpdPanel(oldEpdPanel);
    pref.set(PREF_EPD_PANEL, epdPanel);
    _restartOnApply = epdPanel != oldEpdPanel;
    LOG_I("Hardware: save epd_panel=%s old=%s restart=%d",
          epdPanel.c_str(), oldEpdPanel.c_str(), _restartOnApply ? 1 : 0);
    pref.sync();

    if (_restartOnApply) {
        _sendSaveResult(200, "配置已保存", "屏幕型号已更改，设备即将重启并连接 Wi-Fi。可以关闭此页面。");
    } else {
        _sendSaveResult(200, "配置已保存", "热点即将关闭，设备正在连接 Wi-Fi。可以关闭此页面。");
    }
    _applyAt = millis() + TT_WIFI_APPLY_DELAY_MS;
    _applyPending = true;
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

    String epdPanel;
    pref.get(PREF_EPD_PANEL, epdPanel, String(TT_EPD_PANEL_DEFAULT));
    epdPanel = normalizeEpdPanel(epdPanel);
    doc["epd_panel"] = epdPanel;
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
    if (!_devPasswordOk()) {
        LOG_W("Dev: open rejected");
        _server.send(200, "text/html; charset=utf-8", _getHTMLContent(false, true));
        return;
    }
    LOG_I("Dev: options opened");
    _server.send(200, "text/html; charset=utf-8", _getHTMLContent(true, false, _server.arg("password")));
}

void TTConfigServer::_handleDevSave() {
    if (!_devPasswordOk()) {
        LOG_W("Dev: save rejected");
        _server.send(200, "text/html; charset=utf-8", _getHTMLContent(false, true));
        return;
    }
    const bool enabled = _server.arg("shot") == "1";
    auto& pref = TTInstanceOf<TTPreference>();
    if (!pref.set(PREF_DEV_SHOT, enabled) || !pref.sync()) {
        LOG_E("Dev: save failed");
        _sendSaveResult(500, "保存失败", "无法保存开发者选项");
        return;
    }
    LOG_I("Dev: screenshot %s", enabled ? "on" : "off");
    _server.send(200, "text/html; charset=utf-8", _getHTMLContent(true, false, _server.arg("password")));
}

String TTConfigServer::_devPanelHtml(bool devOpen, bool devDenied, const String& devPassword) {
    String html;
    html += F("<div class='panel' id='panel-dev'>");
    if (!devOpen) {
        html += F("<p class='tip'>输入密码后才能查看开发者选项。</p>");
        if (devDenied) {
            html += F("<p class='err'>密码不正确</p>");
        }
        html += F("<form method='post' action='/dev/open'>");
        html += F("<label>密码</label>");
        html += F("<input type='password' id='dev-password' name='password' autocomplete='off'>");
        html += F("<button class='devbtn' type='submit'>解锁</button></form></div>");
        return html;
    }
    const bool enabled = TTInstanceOf<TTScreenshotDisplay>().shotsEnabled();
    html += F("<form method='post' action='/dev/save'>");
    html += F("<input type='hidden' name='password' value='");
    appendHtmlText(html, devPassword.c_str());
    html += F("'>");
    html += F("<label class='row'><input type='checkbox' name='shot' value='1'");
    if (enabled) {
        html += F(" checked");
    }
    html += F(">长按上键截图</label>");
    html += F("<button class='devbtn' type='submit'>保存</button></form>");
    if (enabled) {
        html += F("<a class='shots' href='/shots'>截图下载与删除</a>");
    }
    html += F("</div>");
    return html;
}

void TTConfigServer::_handleShots() {
    if (!TTInstanceOf<TTScreenshotDisplay>().shotsEnabled()) {
        _server.send(404, "text/plain", "Not found");
        return;
    }
    auto& shots = TTInstanceOf<TTScreenshotDisplay>();
    char names[TT_SHOT_MAX][TT_SHOT_NAME_MAX];
    const int count = shots.list(names, TT_SHOT_MAX);
    String html;
    html.reserve(1024 + (size_t)count * 220);
    html += F("<!DOCTYPE html><html><head><meta charset='utf-8'>");
    html += F("<meta name='viewport' content='width=device-width,initial-scale=1'>");
    html += F("<title>截图</title><style>");
    html += F("body{font-family:-apple-system,sans-serif;margin:0;padding:16px;background:#f4f4f4}");
    html += F(".box{max-width:400px;margin:0 auto;background:#fff;padding:20px;border-radius:8px}");
    html += F("a{color:#111} li{margin:10px 0} button{margin-left:8px}");
    html += F("input{padding:8px;font-size:16px;box-sizing:border-box}");
    html += F(".err{color:#b00020}");
    html += F("</style></head><body><div class='box'><h2>截图</h2>");
    html += F("<p>在设备上长按上键保存当前画面。空间不足时不会保存。删除时必须填写密码。</p>");
    if (_server.hasArg("dev") && _server.arg("dev") == "denied") {
        html += F("<p class='err'>密码不正确，未删除</p>");
    }
    if (shots.isBusy()) {
        html += F("<p>正在保存，请稍后刷新。</p>");
    } else if (count <= 0) {
        html += F("<p>还没有截图。</p>");
    } else {
        html += F("<p><a href='/shots.zip'>下载全部</a></p><ul>");
        for (int i = 0; i < count; i++) {
            html += F("<li><a href='/shots/file?name=");
            appendUrlText(html, names[i]);
            html += F("'>");
            appendHtmlText(html, names[i]);
            html += F("</a><form style='display:inline' method='post' action='/shots/delete'><input type='hidden' name='name' value='");
            appendHtmlText(html, names[i]);
            html += F("'><input type='password' name='password' placeholder='密码' autocomplete='off'><button type='submit'>删除</button></form></li>");
        }
        html += F("</ul><form method='post' action='/shots/clear'><input type='password' name='password' placeholder='密码' autocomplete='off'><button type='submit'>删除全部</button></form>");
    }
    html += F("<p><a href='/'>返回设置</a></p></div></body></html>");
    _server.send(200, "text/html; charset=utf-8", html);
    LOG_I("Shot: list page %d", count);
}

void TTConfigServer::_handleShotFile() {
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
    File file = tt_file_open(path, "r");
    if (!file) {
        LOG_E("Shot: download open failed %s", path);
        _server.send(404, "text/plain; charset=utf-8", "not found");
        return;
    }
    LOG_I("Shot: download %s", path);
    String header = F("attachment; filename=\"");
    header += name;
    header += '"';
    _server.sendHeader("Content-Disposition", header);
    _server.streamFile(file, "image/bmp");
    file.close();
}

void TTConfigServer::_handleShotsZip() {
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
    if (!TTInstanceOf<TTScreenshotDisplay>().shotsEnabled()) {
        _server.send(404, "text/plain", "Not found");
        return;
    }
    if (!_devPasswordOk()) {
        LOG_W("Shot: delete rejected");
        _server.sendHeader("Location", "/shots?dev=denied", true);
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
    _server.sendHeader("Location", "/shots", true);
    _server.send(303, "text/plain", "");
}

void TTConfigServer::_handleShotsClear() {
    if (!TTInstanceOf<TTScreenshotDisplay>().shotsEnabled()) {
        _server.send(404, "text/plain", "Not found");
        return;
    }
    if (!_devPasswordOk()) {
        LOG_W("Shot: clear rejected");
        _server.sendHeader("Location", "/shots?dev=denied", true);
        _server.send(303, "text/plain", "");
        return;
    }
    auto& shots = TTInstanceOf<TTScreenshotDisplay>();
    if (!shots.isBusy()) {
        shots.removeAll();
    } else {
        LOG_W("Shot: clear skipped, capture busy");
    }
    _server.sendHeader("Location", "/shots", true);
    _server.send(303, "text/plain", "");
}

void TTConfigServer::_handleNotFound() {
    _server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString(), true);
    _server.send(302, "text/plain", "");
}

String TTConfigServer::_getHTMLContent(bool devOpen, bool devDenied, const String& devPassword) {
    String html = String(R"html(
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
        .tabs { display: flex; flex-wrap: nowrap; gap: 6px; margin: 12px 0 4px; overflow-x: auto; -webkit-overflow-scrolling: touch; }
        .tab { flex: 0 0 auto; margin: 0; padding: 8px 12px; font-size: 14px; white-space: nowrap; background: #eee; color: #111; border: 0; border-radius: 6px; }
        .tab.on { background: #111; color: #fff; }
        .panel { display: none; }
        .panel.on { display: block; }
        button.save { width: 100%; margin-top: 20px; padding: 12px; font-size: 16px; background: #111; color: #fff; border: 0; border-radius: 6px; }
        a.shots { display: block; margin-top: 12px; text-align: center; color: #111; }
        #panel-dev input[type=checkbox] { width: auto; margin-right: 8px; }
        #panel-dev label.row { display: flex; align-items: center; }
        button.devbtn { width: 100%; margin-top: 8px; padding: 10px; font-size: 15px; background: #eee; color: #111; border: 0; border-radius: 6px; }
        .err { color: #b00020; }
        button.save:disabled { background: #999; }
        .tip { color: #666; font-size: 13px; margin: 8px 0; }
    </style>
</head>
<body>
    <div class="box">
        <h2>设备设置</h2>
        <p class="tip">按分类填写。完成配置后设备将关闭热点并以 STA 模式连接。</p>
        <div class="tabs">
            <button type="button" class="tab on" data-tab="wifi">Wi-Fi</button>
            <button type="button" class="tab" data-tab="tz">时区</button>
            <button type="button" class="tab" data-tab="weather">天气</button>
            <button type="button" class="tab" data-tab="cal">日历</button>
            <button type="button" class="tab" data-tab="hw">硬件</button>
            <button type="button" class="tab" data-tab="dev">开发者</button>
        </div>
        <form method="post" action="/save" onsubmit="return onSubmit()">
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
                <input type="text" id="weather_city" name="weather_city" placeholder="北京.望京" maxlength="32">
                <label>纬度</label>
                <input type="text" id="weather_lat" name="weather_lat" placeholder="39.9972" inputmode="decimal">
                <label>经度</label>
                <input type="text" id="weather_lon" name="weather_lon" placeholder="116.4569" inputmode="decimal">
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
            <div class="panel" id="panel-hw">
                <p class="tip">同一排线有三种波形。A0-2 是当前默认。A0-3 是最初的 A0 驱动。A0-1 是另一款膜。选错会出现残影或噪点。更改后设备会自动重启。</p>
                <label>屏幕型号</label>
                <select id="epd_panel" name="epd_panel">
                    <option value="a0-1">HINK-E042A13 A0-1</option>
                    <option value="a0-2" selected>HINK-E042A13 A0-2</option>
                    <option value="a0-3">HINK-E042A13 A0-3</option>
                </select>
            </div>
            <button class="save" type="submit">完成配置</button>
        </form>
        <!--DEV-->
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
            if (status.epd_panel) {
                document.getElementById('epd_panel').value = status.epd_panel;
            }
        }
        function showTab(name) {
            document.querySelectorAll('.tab').forEach(el => {
                el.classList.toggle('on', el.dataset.tab === name);
            });
            document.querySelectorAll('.panel').forEach(el => {
                el.classList.toggle('on', el.id === 'panel-' + name);
            });
            const save = document.querySelector('button.save');
            if (save) save.style.display = name === 'dev' ? 'none' : '';
            const tab = document.querySelector('.tab[data-tab="' + name + '"]');
            if (tab) tab.scrollIntoView({ inline: 'nearest', block: 'nearest' });
            if (name === 'dev') {
                const pw = document.getElementById('dev-password');
                if (pw) pw.focus();
            }
        }
        document.querySelectorAll('.tab').forEach(el => {
            el.addEventListener('click', () => showTab(el.dataset.tab));
        });
        const devState = "<!--DEV_STATE-->";
        if (devState === "open" || devState === "denied") showTab("dev");
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
    html.replace("<!--DEV_STATE-->", devOpen ? "open" : (devDenied ? "denied" : "idle"));
    html.replace("<!--DEV-->", _devPanelHtml(devOpen, devDenied, devPassword));
    return html;
}
