#pragma once

#include "../Base/TTVTask.h"
#include "../Base/TTWiFiManager.h"

#define TT_WIFI_TASK_STACK  12288
#define TT_WIFI_LOOP_DELAY_MS  10
#define TT_WIFI_TASK_CORE      1

class TTWiFiTask : public TTVTask {
public:
    TTWiFiTask() : TTVTask("WiFiTask", TT_WIFI_TASK_STACK) {}

    void requestStartProvisioningAsync();
    void requestStopProvisioningAsync();
    void requestStatusAsync();
    void requestConnectAsync();
    void requestNtpSyncAsync();

protected:
    void setup() override;
    void loop() override;

private:
    void publishStatus();
    void publishTimeSync(TTTimeSyncState state, const char* message = nullptr);
    void startNtpSync();
    void syncNtp();
    void beginWake();
    void endWake();
    void scheduleHold();
    void cancelHold();
    void schedulePeriod();
    void cancelPeriod();

    TTWiFiManager _wifiManager;
    TTWiFiLinkState _publishedState = TT_WIFI_LINK_IDLE;
    uint32_t _wakeHoldHandle = 0;
    uint32_t _wakePeriodHandle = 0;
    bool _ntpOnConnect = false;
};
