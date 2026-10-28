#pragma once

#include <WebServer.h>
#include <vector>

#define TT_DEV_PASSWORD         "bagemima"
#define TT_WIFI_APPLY_DELAY_MS  800

enum TTPortalKind {
    TT_PORTAL_NETWORK = 0,
    TT_PORTAL_LAN,
};

class TTWiFiManager;

class TTConfigServer {
public:
    TTConfigServer();

    void bind(TTWiFiManager* wifi);
    void setKind(TTPortalKind kind) { _kind = kind; }
    TTPortalKind kind() const { return _kind; }
    void setScanList(const std::vector<String>& ssids);
    void begin();
    void handleClient();
    void stop();
    bool takeApply(bool& restart);
    void cancelApply();

private:
    void _handleRoot();
    void _handleSave();
    void _handleLanSave(const String& weatherCity, const String& weatherLat, const String& weatherLon,
                        const String& weatherRegion, const String& calHost, const String& calUser,
                        String calPass, const String& epdPanel);
    bool _rejectUnlessLan();
    void _handleStatus();
    void _handleScanWiFi();
    void _handleDevOpen();
    void _handleDevSave();
    void _handleShots();
    void _handleShotFile();
    void _handleShotsZip();
    void _handleShotDelete();
    void _handleShotsClear();
    void _handleNotFound();
    void _sendSaveResult(int code, const char* title, const char* msg);
    String _getHTMLContent(bool devOpen = false, bool devDenied = false, const String& devPassword = "");
    String _devPanelHtml(bool devOpen, bool devDenied, const String& devPassword);
    bool _devPasswordOk();
    String _getWiFiListJSON();

    WebServer _server;
    TTWiFiManager* _wifi = nullptr;
    TTPortalKind _kind = TT_PORTAL_NETWORK;
    std::vector<String> _ssidList;
    bool _serverStarted = false;
    bool _applyPending = false;
    bool _restartOnApply = false;
    uint32_t _applyAt = 0;
};
