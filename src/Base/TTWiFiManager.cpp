#include "TTWiFiManager.h"
#include "Logger.h"
#include "ErrorCheck.h"
#include "TTInstance.h"
#include "TTPreference.h"
#include <ArduinoJson.h>
#include <map>
#include "Util.h"

#define PREF_WIFI_SSID "wifi_ssid"
#define PREF_WIFI_PASSWORD "wifi_password"

bool TTWiFiManager::tryConfigWiFi()
{
    // Try to connect to saved WiFi
    String ssid, password;
    auto &pref = TTInstanceOf<TTPreference>();
    ERR_CHECK_RET(pref.get(PREF_WIFI_SSID, ssid, String("")));
    ERR_CHECK_RET(pref.get(PREF_WIFI_PASSWORD, password, String("")));
    if (!ssid.isEmpty() && !password.isEmpty())
    {
        if (_connectToWiFi(ssid, password)) {
            return true;
        }
        else
        {
            LOG_W("Failed to connect to saved WiFi, removing credentials, restarting...");

            pref.remove(PREF_WIFI_SSID);
            pref.remove(PREF_WIFI_PASSWORD);
            pref.sync();
            
            delay(1000);
            ESP.restart();
        }
    }

    // If connection failed or no saved credentials, start AP mode
    LOG_I("Starting AP mode for configuration");

    // First scan available networks
    ERR_CHECK_RET(_scanWiFi());
    ERR_CHECK_RET(_startAP());
    ERR_CHECK_RET(_startWebServer());

    return true;
}

bool TTWiFiManager::_connectToWiFi(const String &ssid, const String &password)
{
    LOG_I("Connecting to WiFi: %s", ssid.c_str());
    _status = WIFI_STA_CONNECTING;

    ERR_CHECK_RET(WiFi.mode(WIFI_STA));
    ERR_CHECK_RET(WiFi.disconnect());
    delay(100);
    ERR_CHECK_RET(WiFi.setTxPower(WIFI_POWER_19dBm));
    WiFi.begin(ssid.c_str(), password.c_str());

    // Wait for connection with timeout
    uint32_t startTime = millis();
    while (WiFi.status() != WL_CONNECTED)
    {
        if (millis() - startTime > WIFI_CONNECT_TIMEOUT)
        {
            LOG_E("WiFi connection timeout");
            _status = WIFI_DISCONNECTED;
            return false;
        }
        delay(500);
    }

    LOG_I("WiFi connected, IP: %s", WiFi.localIP().toString().c_str());
    _status = WIFI_STA_CONNECTED;
    return true;
}

void TTWiFiManager::process()
{
    if (isAPMode())
    {
        _dnsServer.processNextRequest();
        _server.handleClient();
    }
    else if (_status == WIFI_STA_CONNECTED && WiFi.status() != WL_CONNECTED)
    {
        // Restart device to reconfigure WiFi
        LOG_W("WiFi connection lost");
        delay(5000);
        ESP.restart();
    }
}

bool TTWiFiManager::_startAP()
{
    // Start AP with STA mode to support scanning
    ERR_CHECK_RET(WiFi.mode(WIFI_AP));
    delay(100);
    ERR_CHECK_RET(WiFi.setTxPower(WIFI_POWER_19dBm));
    ERR_CHECK_RET(WiFi.softAP(DEFAULT_AP_SSID, DEFAULT_AP_PASSWORD));

    // Configure captive portal
    ERR_CHECK_RET(_dnsServer.start(53, "*", WiFi.softAPIP()));

    LOG_I("AP started. SSID: %s, Password: %s, IP: %s",
          DEFAULT_AP_SSID,
          DEFAULT_AP_PASSWORD,
          WiFi.softAPIP().toString().c_str());

    _status = WIFI_AP_MODE;
    return true;
}

bool TTWiFiManager::_scanWiFi()
{
    // Scan for WiFi networks
    _status = WIFI_STA_SCANNING;
    LOG_I("Scanning for WiFi networks...");

    // Set WiFi to station mode for scanning
    ERR_CHECK_RET(WiFi.mode(WIFI_STA));
    delay(100);
    ERR_CHECK_RET(WiFi.setTxPower(WIFI_POWER_19dBm));

    // Start scanning
    int n = WiFi.scanNetworks();
    if (n == 0)
    {
        LOG_W("No WiFi networks found");
    }
    else
    {
        LOG_I("Found %d networks", n);

        // Clear previous list
        _ssidList.clear();

        // Store found networks
        for (int i = 0; i < n; ++i)
        {
            // Only add networks with SSID
            if (WiFi.SSID(i).length() > 0)
            {
                _ssidList.push_back(WiFi.SSID(i));
            }
            delay(10); // Small delay to prevent watchdog reset
        }

        // Sort networks by SSID
        std::sort(_ssidList.begin(), _ssidList.end());

        // Remove duplicates
        _ssidList.erase(std::unique(_ssidList.begin(), _ssidList.end()), _ssidList.end());
    }

    // Delete scan result to free memory
    WiFi.scanDelete();
    WiFi.mode(WIFI_OFF);
    return true;
}

bool TTWiFiManager::_startWebServer()
{
    _server.on("/", HTTP_GET, [this]()
               { _handleRoot(); });
    _server.on("/save", HTTP_POST, [this]()
               { _handleSave(); });
    _server.on("/scan", HTTP_GET, [this]()
               { _handleScanWiFi(); });
    _server.onNotFound([this]()
                       { _handleNotFound(); });
    _server.begin();
    return true;
}

void TTWiFiManager::_handleRoot()
{
    _server.send(200, "text/html", _getHTMLContent());
}

void TTWiFiManager::_handleSave()
{
    String ssid = _server.arg("ssid");
    String password = _server.arg("password");

    if (ssid.isEmpty())
    {
        _server.send(400, "text/plain", "SSID cannot be empty");
        return;
    }

    // Save credentials
    LOG_I("Saving WiFi credentials: SSID: %s, Password: %s", ssid.c_str(), password.c_str());
    auto &pref = TTInstanceOf<TTPreference>();
    pref.set(PREF_WIFI_SSID, ssid);
    pref.set(PREF_WIFI_PASSWORD, password);
    pref.sync();

    String response = "WiFi credentials saved. Device will restart in 3 seconds...";
    _server.send(200, "text/plain", response);
    _server.client().flush(); // Ensure response is sent

    // Restart device to connect to new WiFi
    LOG_I("Restarting device...");
    delay(200);
    ESP.restart();
}

void TTWiFiManager::_handleScanWiFi()
{
    String json = _getWiFiListJSON();
    _server.send(200, "application/json", json);
}

String TTWiFiManager::_getWiFiListJSON()
{
    DynamicJsonDocument doc(4096);
    JsonArray array = doc.createNestedArray("networks");

    for (const String &ssid : _ssidList)
    {
        JsonObject network = array.createNestedObject();
        network["ssid"] = ssid;
    }

    String result;
    serializeJson(doc, result);
    return result;
}

void TTWiFiManager::_handleNotFound()
{
    if (isAPMode())
    {
        // Redirect all requests to configuration page in AP mode
        _server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString(), true);
        _server.send(302, "text/plain", "");
    }
    else
    {
        _server.send(404, "text/plain", "Not found");
    }
}

String TTWiFiManager::_getHTMLContent()
{
    String html = R"html(
<!DOCTYPE html>
<html>
<head>
    <title>VerseCam WiFi Setup</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body {
            font-family: Arial, sans-serif;
            margin: 0;
            padding: 20px;
            background: #f5f5f5;
        }
        .container {
            max-width: 400px;
            margin: 0 auto;
            background: white;
            padding: 20px;
            border-radius: 12px;
            box-shadow: 0 2px 4px rgba(0,0,0,0.1);
        }
        h2 {
            color: #2c3e50;
            margin-top: 0;
            margin-bottom: 25px;
            text-align: center;
        }
        .form-group {
            margin-bottom: 20px;
        }
        label {
            display: block;
            margin-bottom: 8px;
            color: #34495e;
        }
        select, input[type="text"], input[type="password"] {
            width: 100%;
            padding: 8px;
            border: 1px solid #bdc3c7;
            border-radius: 4px;
            box-sizing: border-box;
            font-size: 14px;
        }
        select {
            background-color: white;
        }
        button {
            width: 100%;
            padding: 10px;
            background: #3498db;
            color: white;
            border: none;
            border-radius: 4px;
            font-size: 16px;
            cursor: pointer;
            transition: background 0.3s;
        }
        button:hover {
            background: #2980b9;
        }
        button:disabled {
            background: #95a5a6;
            cursor: not-allowed;
        }
        .loading {
            display: none;
            text-align: center;
            margin: 15px 0;
            color: #7f8c8d;
        }
        @media (max-width: 480px) {
            body {
                padding: 15px;
            }
            .container {
                padding: 15px;
            }
        }
    </style>
</head>
<body>
    <div class="container">
        <h2>VerseCam WiFi Setup</h2>
        <form id="wifi-form" onsubmit="return saveConfig()">
            <div class="form-group">
                <label>WiFi Network:</label>
                <select id="ssid-select" onchange="updateSSID(this.value)">
                    <option value="">Select a network...</option>
                </select>
                <input type="text" id="ssid" name="ssid" placeholder="Or type SSID manually">
            </div>
            <div class="form-group">
                <label>Password:</label>
                <input type="password" id="password" name="password" placeholder="Enter WiFi password">
            </div>
            <div class="loading" id="loading">
                <div>Saving configuration...</div>
                <div style="font-size: 0.9em; margin-top: 5px;">The device will restart automatically</div>
            </div>
            <button type="submit">Save Configuration</button>
        </form>
    </div>
    <script>
        function updateSSID(value) {
            if (value) {
                document.getElementById('ssid').value = value;
            }
        }
        
        function saveConfig() {
            const form = document.getElementById('wifi-form');
            const loading = document.getElementById('loading');
            const submitButton = form.querySelector('button[type="submit"]');
            const data = new FormData(form);
            
            if (!data.get('ssid')) {
                alert('Please select or enter a WiFi network');
                return false;
            }
            
            loading.style.display = 'block';
            submitButton.disabled = true;
            
            fetch('/save', {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/x-www-form-urlencoded'
                },
                body: new URLSearchParams(data)
            })
            .then(response => {
                if (!response.ok) {
                    throw new Error('Network response was not ok');
                }
                return response.text();
            })
            .then(message => {
                alert(message);
            })
            .catch(error => {
                alert('Error saving configuration: ' + error.message);
                loading.style.display = 'none';
                submitButton.disabled = false;
                console.error('Error:', error);
            });
            
            return false;
        }
        
        // Auto scan when page loads
        function scanWiFi() {
            const select = document.getElementById('ssid-select');
            select.innerHTML = '<option value="">Scanning...</option>';
            
            fetch('/scan')
                .then(response => response.json())
                .then(data => {
                    select.innerHTML = '<option value="">Select a network...</option>';
                    data.networks.forEach(network => {
                        const option = document.createElement('option');
                        option.value = network.ssid;
                        option.textContent = network.ssid;
                        select.appendChild(option);
                    });
                })
                .catch(error => {
                    select.innerHTML = '<option value="">Scan failed</option>';
                    console.error('Error:', error);
                });
        }
        
        window.onload = scanWiFi;
    </script>
</body>
</html>
)html";

    return html;
}