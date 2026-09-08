#pragma once

#include "../Base/TTVTask.h"
#include "../Base/TTWiFiManager.h"

#define TT_WIFI_TASK_STACK  12288
#define TT_WIFI_LOOP_DELAY_MS  10

class TTWiFiTask : public TTVTask {
public:
    TTWiFiTask() : TTVTask("WiFiTask", TT_WIFI_TASK_STACK) {}

    void requestStartProvisioningAsync();
    void requestStopProvisioningAsync();
    void requestStatusAsync();
    void requestNtpSyncAsync();

protected:
    void setup() override;
    void loop() override;

private:
    void publishStatus();
    void publishTimeSync(TTTimeSyncState state, const char* message = nullptr);
    void startNtpSync();
    void syncNtp();

    TTWiFiManager _wifiManager;
    TTWiFiLinkState _publishedState = TT_WIFI_LINK_IDLE;
};
