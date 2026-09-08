#include "TTWiFiTask.h"
#include "TTUITask.h"
#include "../Base/Logger.h"
#include "../Base/TTInstance.h"
#include "../Base/TTPreference.h"
#include "../Base/TTNotificationPayloads.h"

void TTWiFiTask::setup() {
    LOG_I("WiFi task starting");
    if (!TTInstanceOf<TTPreference>().begin()) {
        LOG_E("WiFi: preference begin failed");
    }
    _wifiManager.tryConnectSaved();
    publishStatus();
}

void TTWiFiTask::loop() {
    _wifiManager.process();
    if (_wifiManager.isConnected() != (_publishedState == TT_WIFI_LINK_CONNECTED)
        || _wifiManager.isProvisioning() != (_publishedState == TT_WIFI_LINK_PROVISIONING)) {
        publishStatus();
    }
}

void TTWiFiTask::requestStartProvisioningAsync() {
    auto* f = new std::function<void()>([this]() {
        _wifiManager.startProvisioning();
        publishStatus();
    });
    enqueue(f);
}

void TTWiFiTask::requestStopProvisioningAsync() {
    auto* f = new std::function<void()>([this]() {
        _wifiManager.stopProvisioning();
        publishStatus();
    });
    enqueue(f);
}

void TTWiFiTask::requestStatusAsync() {
    auto* f = new std::function<void()>([this]() {
        publishStatus();
    });
    enqueue(f);
}

void TTWiFiTask::publishStatus() {
    TTWiFiStatusPayload payload;
    _wifiManager.fillStatus(payload);
    _publishedState = payload.state;
    LOG_I("WiFi: publish state=%d ssid=%s ap=%s url=%s",
          (int)payload.state, payload.ssid, payload.apSsid, payload.portalUrl);
    TTInstanceOf<TTUITask>().postNotification(TT_NOTIFICATION_WIFI_STATUS, payload);
}
