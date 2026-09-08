#include "TTWiFiManager.h"
#include "Logger.h"
#include "ErrorCheck.h"
#include "TTInstance.h"
#include "TTPreference.h"
#include "TTRtc.h"
#include <ArduinoJson.h>
#include <algorithm>
#include <cstring>

bool TTWiFiManager::tryConnectSaved() {
    String ssid;
    String password;
    auto& pref = TTInstanceOf<TTPreference>();
    ERR_CHECK_RET(pref.get(PREF_WIFI_SSID, ssid, String("")));
    ERR_CHECK_RET(pref.get(PREF_WIFI_PASSWORD, password, String("")));
    if (ssid.isEmpty()) {
        LOG_I("WiFi: no saved SSID");
        _savedSsid[0] = '\0';
        _state = TT_WIFI_LINK_IDLE;
        return false;
    }
    strncpy(_savedSsid, ssid.c_str(), TT_WIFI_SSID_MAX);
    _savedSsid[TT_WIFI_SSID_MAX] = '\0';
    if (!_connectToWiFi(ssid, password)) {
        LOG_W("WiFi: saved network connect failed ssid=%s", ssid.c_str());
        _state = TT_WIFI_LINK_IDLE;
        return false;
    }
    return true;
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
    if (_state == TT_WIFI_LINK_CONNECTED && WiFi.status() != WL_CONNECTED) {
        LOG_W("WiFi: STA lost");
        _state = TT_WIFI_LINK_IDLE;
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

bool TTWiFiManager::_connectToWiFi(const String& ssid, const String& password) {
    LOG_I("WiFi: connecting ssid=%s", ssid.c_str());
    _state = TT_WIFI_LINK_CONNECTING;

    ERR_CHECK_RET(WiFi.mode(WIFI_STA));
    WiFi.disconnect();
    delay(100);
    WiFi.setTxPower(WIFI_POWER_19dBm);
    if (password.isEmpty()) {
        WiFi.begin(ssid.c_str());
    } else {
        WiFi.begin(ssid.c_str(), password.c_str());
    }

    uint32_t startTime = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - startTime > TT_WIFI_CONNECT_TIMEOUT_MS) {
            LOG_E("WiFi: connect timeout");
            _state = TT_WIFI_LINK_IDLE;
            return false;
        }
        delay(400);
    }

    LOG_I("WiFi: connected ip=%s", WiFi.localIP().toString().c_str());
    _state = TT_WIFI_LINK_CONNECTED;
    return true;
}

void TTWiFiManager::_buildApSsid() {
    uint32_t suffix = (uint32_t)(ESP.getEfuseMac() & 0xFFFF);
    snprintf(_apSsid, sizeof(_apSsid), "%s-%04X", TT_WIFI_AP_SSID_PREFIX, (unsigned)suffix);
}

bool TTWiFiManager::_startAP() {
    _buildApSsid();
    ERR_CHECK_RET(WiFi.mode(WIFI_AP));
    delay(100);
    WiFi.setTxPower(WIFI_POWER_19dBm);
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
    delay(100);
}

bool TTWiFiManager::_scanWiFi() {
    LOG_I("WiFi: scanning before AP");
    WiFi.scanDelete();
    _ssidList.clear();

    ERR_CHECK_RET(WiFi.mode(WIFI_STA));
    WiFi.disconnect(false, false);
    delay(200);
    WiFi.setTxPower(WIFI_POWER_19dBm);

    int n = WiFi.scanNetworks(false, true);
    if (n <= 0) {
        LOG_W("WiFi: first scan result=%d, retry", n);
        WiFi.scanDelete();
        delay(200);
        n = WiFi.scanNetworks(false, true);
    }
    if (n <= 0) {
        LOG_W("WiFi: no networks found result=%d", n);
        WiFi.scanDelete();
        return n == 0;
    }

    LOG_I("WiFi: found %d networks", n);
    for (int i = 0; i < n; ++i) {
        String ssid = WiFi.SSID(i);
        if (ssid.length() == 0) {
            continue;
        }
        LOG_I("WiFi: ssid[%d]=%s rssi=%d", i, ssid.c_str(), WiFi.RSSI(i));
        _ssidList.push_back(ssid);
    }
    std::sort(_ssidList.begin(), _ssidList.end());
    _ssidList.erase(std::unique(_ssidList.begin(), _ssidList.end()), _ssidList.end());
    WiFi.scanDelete();
    return true;
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
    LOG_I("WiFi: portal save args=%d ssid_len=%u tz_len=%u label_len=%u",
          _server.args(), (unsigned)ssid.length(), (unsigned)timezone.length(), (unsigned)label.length());
    ssid.trim();
    timezone.trim();
    label.trim();
    if (ssid.isEmpty()) {
        _sendSaveResult(400, "保存失败", "Wi-Fi 名称不能为空");
        return;
    }
    if (timezone.isEmpty()) {
        _sendSaveResult(400, "保存失败", "请选择时区");
        return;
    }

    auto& pref = TTInstanceOf<TTPreference>();
    if (password.isEmpty() && ssid == String(_savedSsid)) {
        pref.get(PREF_WIFI_PASSWORD, password, String(""));
        LOG_I("WiFi: keep existing password ssid=%s", ssid.c_str());
    }

    LOG_I("WiFi: save ssid=%s password_len=%u tz=%s label=%s",
          ssid.c_str(), (unsigned)password.length(), timezone.c_str(), label.c_str());
    pref.set(PREF_WIFI_SSID, ssid);
    pref.set(PREF_WIFI_PASSWORD, password);
    pref.sync();
    strncpy(_savedSsid, ssid.c_str(), TT_WIFI_SSID_MAX);
    _savedSsid[TT_WIFI_SSID_MAX] = '\0';

    if (!TTRtc::saveTimezone(timezone.c_str(), label.c_str())) {
        _sendSaveResult(500, "保存失败", "时区保存失败");
        return;
    }

    _sendSaveResult(200, "配置已保存", "热点即将关闭，设备正在连接 Wi-Fi。可以关闭此页面。");
    _applyAt = millis() + TT_WIFI_APPLY_DELAY_MS;
    _applyPending = true;
}

void TTWiFiManager::_handleStatus() {
    JsonDocument doc;
    auto& pref = TTInstanceOf<TTPreference>();
    String ssid;
    String password;
    pref.get(PREF_WIFI_SSID, ssid, String(""));
    pref.get(PREF_WIFI_PASSWORD, password, String(""));
    if (ssid.isEmpty() && _savedSsid[0] != '\0') {
        ssid = _savedSsid;
    }
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
    LOG_I("WiFi: status fill ssid=%s password_len=%u tz=%s label=%s",
          ssid.c_str(), (unsigned)password.length(), tz, label);
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
        h3 { margin: 20px 0 8px; font-size: 16px; }
        label { display: block; margin: 12px 0 6px; }
        select, input { width: 100%; padding: 10px; font-size: 16px; box-sizing: border-box; }
        button { width: 100%; margin-top: 20px; padding: 12px; font-size: 16px; background: #111; color: #fff; border: 0; border-radius: 6px; }
        button:disabled { background: #999; }
        .tip { color: #666; font-size: 13px; margin: 0 0 8px; }
    </style>
</head>
<body>
    <div class="box">
        <h2>设备设置</h2>
        <p class="tip">设置 Wi-Fi 和时区，点击完成配置后设备将关闭热点并以 STA 模式连接。</p>
        <form method="post" action="/save" onsubmit="return onSubmit()">
            <h3>Wi-Fi</h3>
            <label>名称</label>
            <select id="ssid-select" onchange="document.getElementById('ssid').value=this.value">
                <option value="">正在扫描...</option>
            </select>
            <input type="text" id="ssid" name="ssid" placeholder="或手动输入名称" required>
            <label>密码</label>
            <input type="text" id="password" name="password" placeholder="Wi-Fi 密码" autocomplete="off">
            <h3>时区</h3>
            <label>地区</label>
            <select id="region" onchange="fillCities()"></select>
            <label>城市</label>
            <select id="city" onchange="applyCity()"></select>
            <input type="hidden" id="timezone" name="timezone" value="CST-8">
            <input type="hidden" id="timezone_label" name="timezone_label" value="上海">
            <button type="submit">完成配置</button>
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
        }
        function onSubmit() {
            applyCity();
            const ssid = document.getElementById('ssid').value.replace(/^\s+|\s+$/g, '');
            document.getElementById('ssid').value = ssid;
            if (!ssid) { alert('请选择或输入 Wi-Fi 名称'); return false; }
            if (!document.getElementById('timezone').value) { alert('请选择时区'); return false; }
            const btn = document.querySelector('button');
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
