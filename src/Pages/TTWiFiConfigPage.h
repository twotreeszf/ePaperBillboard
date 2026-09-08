#pragma once

#include <lvgl.h>
#include "../Base/TTScreenPage.h"
#include "../Base/TTNotificationPayloads.h"

#define TT_WIFI_STEPS_W_WITH_QR  210
#define TT_WIFI_QR_RIGHT_PAD     8
#define TT_WIFI_QR_HINT_GAP      4

class TTWiFiConfigPage : public TTScreenPage {
public:
    TTWiFiConfigPage() : TTScreenPage("Web 设置") {}

    void setup() override;
    void willAppear() override;
    void willDisappear() override;

protected:
    void buildContent(lv_obj_t* screen) override;

private:
    void applyStatus(const TTWiFiStatusPayload& status);
    void onActionClicked();
    void startProvisioningWithLoading();
    void dismissStartLoading();
    void showProvisionQr(const char* apSsid);
    void hideProvisionQr();
    static void onActionEvent(lv_event_t* e);

    lv_obj_t* _statusLabel = nullptr;
    lv_obj_t* _stepsLabel = nullptr;
    lv_obj_t* _qr = nullptr;
    lv_obj_t* _qrHint = nullptr;
    lv_obj_t* _actionBtn = nullptr;
    TTWiFiLinkState _state = TT_WIFI_LINK_IDLE;
    bool _visible = false;
    bool _startLoading = false;
};
