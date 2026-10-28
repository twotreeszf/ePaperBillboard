#pragma once

#include "../Base/TTVTask.h"
#include "../Base/TTWiFiManager.h"
#include "../Service/TTBookTransferServer.h"
#include "../Service/TTDebugServer.h"
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
    void requestRadioOffAsync();
    void requestStatusAsync();
    void requestKeepRadio(bool keep);
    void requestReleaseDriverWhenIdle(bool release);
    void requestConnectAsync();
    void requestNtpSyncAsync();
    void requestStartBookTransferAsync();
    void requestStopBookTransferAsync();
    void requestStartLanPortalAsync();
    void requestStopLanPortalAsync();
    void requestDebugServer(bool enabled);
    bool isBookTransferActive() const { return _bookTransferWanted; }
    void runWithRadio(const char* tag, std::function<void()> work,
                      std::function<void()> onFailed = {});
    bool isRadioActive() const;
    bool isRadioParked() const;
    void parkRadio();

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
    void startBookTransfer();
    void processBookTransfer();
    void stopBookTransfer(TTBookTransferState state, TTBookTransferError error);
    void processDebugServer();
    void stopDebugListen();
    void processLanPortal();
    void stopLanListen();
    void publishBookTransfer(TTBookTransferState state, TTBookTransferError error,
                             const char* name = nullptr, uint32_t size = 0, const char* message = nullptr);

    TTWiFiManager _wifiManager;
    TTBookTransferServer _bookTransfer;
    TTDebugServer _debug;
    bool _bookTransferWanted = false;
    bool _lanPortalWanted = false;
    bool _debugWanted = false;
    uint32_t _debugRetryAt = 0;
    TTWiFiLinkState _publishedState = TT_WIFI_LINK_IDLE;
    bool _wakeFailed = false;
    bool _keepRadio = false;
    std::vector<TTWifiJob> _jobs;
};
