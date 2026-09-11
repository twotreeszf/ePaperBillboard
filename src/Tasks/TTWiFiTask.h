#pragma once

#include "../Base/TTVTask.h"
#include "../Base/TTWiFiManager.h"
#include <ctime>

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
    void requestAcquireAsync(const char* tag);
    void requestReleaseAsync(const char* tag);
    bool isPeriodDue() const;
    bool isRadioActive() const;

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
    void acquireRadio(const char* tag);
    void releaseRadio(const char* tag);
    void armIdleCheck();
    void cancelIdleCheck();
    void trySleepIfIdle();
    void refreshPeriodDeadline();
    void cancelPeriod();

    TTWiFiManager _wifiManager;
    TTWiFiLinkState _publishedState = TT_WIFI_LINK_IDLE;
    uint32_t _useCount = 0;
    uint32_t _idleCheckHandle = 0;
    time_t _nextPeriodUnix = 0;
    bool _idleCheckReady = false;
    bool _ntpOnConnect = false;
    bool _ntpAutoDone = false;
};
