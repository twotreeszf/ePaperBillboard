#pragma once

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <vector>
#include "TTNotificationPayloads.h"

#define PREF_WIFI_NETWORKS  "wifi_networks"
#define TT_WIFI_AP_SSID_PREFIX  "Billboard"
#define TT_WIFI_CONNECT_TIMEOUT_MS  15000
#define TT_WIFI_APPLY_DELAY_MS      800
#define TT_WIFI_DNS_PORT  53

class TTWiFiManager {
public:
    TTWiFiManager() : _server(80) {}

    bool tryConnectSaved();
    bool refreshSavedNetwork();
    bool sleepRadio();
    bool startProvisioning();
    bool stopProvisioning();
    void process();
    void fillStatus(TTWiFiStatusPayload& out) const;

    bool isConnected() const { return _state == TT_WIFI_LINK_CONNECTED; }
    bool isConnecting() const { return _state == TT_WIFI_LINK_CONNECTING; }
    bool isProvisioning() const { return _state == TT_WIFI_LINK_PROVISIONING; }
    bool hasConfiguredNetwork() const { return _hasNetworks; }
    TTWiFiLinkState linkState() const { return _state; }

private:
    bool _startConnect(const String& ssid, const String& password);
    void _pollConnect();
    bool _scanNearby(std::vector<String>& out);
    bool _scanWiFi();
    bool _chooseSavedFromScan(const std::vector<String>& nearby,
                              String& ssid, String& password);
    void _rememberSsid(const String& ssid);
    bool _startAP();
    bool _startWebServer();
    void _stopAP();
    void _buildApSsid();
    void _handleRoot();
    void _handleSave();
    void _handleStatus();
    void _handleScanWiFi();
    void _handleNotFound();
    void _sendSaveResult(int code, const char* title, const char* msg);
    String _getHTMLContent();
    String _getWiFiListJSON();

    WebServer _server;
    DNSServer _dnsServer;
    TTWiFiLinkState _state = TT_WIFI_LINK_IDLE;
    std::vector<String> _ssidList;
    char _apSsid[TT_WIFI_SSID_MAX + 1] = {0};
    char _savedSsid[TT_WIFI_SSID_MAX + 1] = {0};
    bool _hasNetworks = false;
    bool _serverStarted = false;
    bool _applyPending = false;
    uint32_t _applyAt = 0;
    uint32_t _connectStartedAt = 0;
};
