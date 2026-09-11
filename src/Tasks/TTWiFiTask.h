#pragma once

#include "../Base/TTVTask.h"
#include "../Base/TTWiFiManager.h"
#include <functional>
#include <vector>

#define TT_WIFI_TASK_STACK  20480
#define TT_WIFI_LOOP_DELAY_MS  10
#define TT_WIFI_TASK_CORE      1
#define TT_WIFI_RADIO_TAG_MAX  16

struct TTWifiJob {
    char tag[TT_WIFI_RADIO_TAG_MAX];
    std::function<void()> work;
    std::function<void()> onFailed;
};

class TTWiFiTask : public TTVTask {
public:
    TTWiFiTask() : TTVTask("WiFiTask", TT_WIFI_TASK_STACK) {}

    void requestStartProvisioningAsync();
    void requestStopProvisioningAsync();
    void requestStatusAsync();
    void requestConnectAsync();
    void requestNtpSyncAsync();
    void runWithRadio(const char* tag, std::function<void()> work,
                      std::function<void()> onFailed = {});
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
    void handleLinkChange();
    void processRadioJobs();
    void postWorkJob(const char* tag, std::function<void()> work, std::function<void()> onFailed);
    void dropFailedJobs();
    void runAllWorkJobs();

    TTWiFiManager _wifiManager;
    TTWiFiLinkState _publishedState = TT_WIFI_LINK_IDLE;
    bool _wakeFailed = false;
    std::vector<TTWifiJob> _jobs;
};
