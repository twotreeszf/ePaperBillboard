#pragma once

#ifdef ESP32
#include <WiFi.h>
#include <WebServer.h>
#elif defined(ESP8266)
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
using WebServer = ESP8266WebServer;
#endif
#include <DNSServer.h>
#include <ArduinoJson.h>
#include "TTPreference.h"
#include "TTInstance.h"
#include "PrefKeys.h"

/**
 * @brief WiFi Manager for handling WiFi connection and configuration
 * Provides AP mode for initial setup and handles WiFi credentials storage
 */
#define DEFAULT_AP_SSID "VerseCam"
#define DEFAULT_AP_PASSWORD "1234567890"
#define WIFI_CONNECT_TIMEOUT 10 * 1000 // 10 seconds

enum TTWiFiStatus {
    WIFI_DISCONNECTED,
    WIFI_STA_CONNECTING,
    WIFI_STA_CONNECTED,
    WIFI_STA_SCANNING,
    WIFI_AP_MODE,
};

class TTWiFiManager {
public:
    TTWiFiManager() : _server(80), _status(WIFI_DISCONNECTED) {}
    bool tryConfigWiFi();
    void process();
    bool isConnected() { return _status == WIFI_STA_CONNECTED; }
    bool isAPMode() { return _status == WIFI_AP_MODE; }
    TTWiFiStatus getStatus() { return _status; }
    
private:
    WebServer _server;
    DNSServer _dnsServer;
    TTWiFiStatus _status;
    std::vector<String> _ssidList;
    
    bool _startAP();
    bool _scanWiFi(); 
    bool _startWebServer();
    bool _connectToWiFi(const String& ssid, const String& password);
    void _handleRoot();
    void _handleSave();
    void _handleNotFound();
    void _handleScanWiFi();
    String _getHTMLContent();
    String _getWiFiListJSON();
};
