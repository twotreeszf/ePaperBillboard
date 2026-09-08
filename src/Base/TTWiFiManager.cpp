#include "TTWiFiManager.h"
#include "Logger.h"
#include "ErrorCheck.h"
#include "TTInstance.h"
#include "TTPreference.h"
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
        WiFi.disconnect(true, false);
        delay(100);
    }

    if (!_scanWiFi()) {
        LOG_W("WiFi: scan failed, continue with empty list");
    }
    ERR_CHECK_RET(_startAP());
    ERR_CHECK_RET(_startWebServer());
    return true;
}

bool TTWiFiManager::stopProvisioning() {
    _applyPending = false;
    if (_state != TT_WIFI_LINK_PROVISIONING) {
        return true;
    }
    LOG_I("WiFi: stop provisioning");
    _stopAP();
    _state = TT_WIFI_LINK_IDLE;
    tryConnectSaved();
    return true;
}

void TTWiFiManager::process() {
    if (_state == TT_WIFI_LINK_PROVISIONING) {
        _dnsServer.processNextRequest();
        _server.handleClient();
        if (_applyPending) {
            _applyPending = false;
            LOG_I("WiFi: apply saved credentials without restart");
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
    LOG_I("WiFi: scanning");
    ERR_CHECK_RET(WiFi.mode(WIFI_STA));
    delay(100);
    WiFi.setTxPower(WIFI_POWER_19dBm);

    int n = WiFi.scanNetworks();
    _ssidList.clear();
    if (n <= 0) {
        LOG_W("WiFi: no networks found");
        WiFi.scanDelete();
        return true;
    }

    LOG_I("WiFi: found %d networks", n);
    for (int i = 0; i < n; ++i) {
        String ssid = WiFi.SSID(i);
        if (ssid.length() > 0) {
            _ssidList.push_back(ssid);
        }
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

void TTWiFiManager::_handleSave() {
    String ssid = _server.arg("ssid");
    String password = _server.arg("password");
    if (ssid.isEmpty()) {
        _server.send(400, "text/plain; charset=utf-8", "名称不能为空");
        return;
    }

    LOG_I("WiFi: save ssid=%s password_len=%u", ssid.c_str(), (unsigned)password.length());
    auto& pref = TTInstanceOf<TTPreference>();
    pref.set(PREF_WIFI_SSID, ssid);
    pref.set(PREF_WIFI_PASSWORD, password);
    pref.sync();
    strncpy(_savedSsid, ssid.c_str(), TT_WIFI_SSID_MAX);
    _savedSsid[TT_WIFI_SSID_MAX] = '\0';

    _server.send(200, "text/plain; charset=utf-8", "已保存，正在连接...");
    _server.client().flush();
    delay(200);
    _applyPending = true;
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
    <title>电子墨水屏配网</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { font-family: -apple-system, sans-serif; margin: 0; padding: 16px; background: #f4f4f4; }
        .box { max-width: 400px; margin: 0 auto; background: #fff; padding: 20px; border-radius: 8px; }
        h2 { margin: 0 0 16px; text-align: center; }
        label { display: block; margin: 12px 0 6px; }
        select, input { width: 100%; padding: 10px; font-size: 16px; box-sizing: border-box; }
        button { width: 100%; margin-top: 16px; padding: 12px; font-size: 16px; background: #111; color: #fff; border: 0; border-radius: 6px; }
        button:disabled { background: #999; }
        .tip { color: #666; font-size: 13px; margin-top: 8px; }
    </style>
</head>
<body>
    <div class="box">
        <h2>电子墨水屏配网</h2>
        <p class="tip">选择房间的 Wi-Fi 并保存，设备会自动连接。</p>
        <form onsubmit="return saveConfig()">
            <label>Wi-Fi 名称</label>
            <select id="ssid-select" onchange="document.getElementById('ssid').value=this.value">
                <option value="">正在扫描...</option>
            </select>
            <input type="text" id="ssid" name="ssid" placeholder="或手动输入名称">
            <label>密码（开放网络可留空）</label>
            <input type="password" id="password" name="password" placeholder="Wi-Fi 密码">
            <button type="submit">保存并连接</button>
        </form>
    </div>
    <script>
        function saveConfig() {
            const ssid = document.getElementById('ssid').value;
            if (!ssid) { alert('请选择或输入 Wi-Fi 名称'); return false; }
            const btn = document.querySelector('button');
            btn.disabled = true;
            btn.textContent = '保存中...';
            fetch('/save', {
                method: 'POST',
                headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
                body: new URLSearchParams(new FormData(document.querySelector('form')))
            }).then(r => r.text()).then(t => alert(t))
              .catch(e => { alert(e); btn.disabled = false; btn.textContent = '保存并连接'; });
            return false;
        }
        fetch('/scan').then(r => r.json()).then(data => {
            const s = document.getElementById('ssid-select');
            s.innerHTML = '<option value="">选择网络...</option>';
            (data.networks || []).forEach(n => {
                const o = document.createElement('option');
                o.value = n.ssid; o.textContent = n.ssid; s.appendChild(o);
            });
        }).catch(() => {
            document.getElementById('ssid-select').innerHTML = '<option value="">扫描失败，请手动输入</option>';
        });
    </script>
</body>
</html>
)html");
}
