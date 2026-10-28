#pragma once

#include <lvgl.h>
#include "../Base/TTScreenPage.h"
#include "../Models/TTNotificationPayloads.h"

#define TT_WIFI_STEPS_W_WITH_QR  210
#define TT_WIFI_QR_RIGHT_PAD     8
#define TT_WIFI_QR_HINT_GAP      4
#define TT_WIFI_INFO_TOP         56
#define TT_WIFI_INFO_SIDE        32
#define TT_WIFI_INFO_SECTION_GAP 22
#define TT_WIFI_INFO_LINE_GAP    6
#define TT_WIFI_CONNECTED_ICON_GAP   6

enum TTPortalPageKind {
    TT_PORTAL_PAGE_NETWORK = 0,
    TT_PORTAL_PAGE_WEB,
};

class TTWiFiConfigPage : public TTScreenPage {
public:
    explicit TTWiFiConfigPage(TTPortalPageKind kind = TT_PORTAL_PAGE_NETWORK)
        : TTScreenPage(kind == TT_PORTAL_PAGE_WEB ? "Web 设置" : "联网设置"), _kind(kind) {}

    void setup() override;
    void willAppear() override;
    void willDisappear() override;

protected:
    void buildContent(lv_obj_t* screen) override;

private:
    void applyStatus(const TTWiFiStatusPayload& status);
    void applyLanStatus(const TTWiFiStatusPayload& status);
    void onActionClicked();
    void startProvisioningWithLoading();
    void startLanPortalWithLoading();
    void dismissStartLoading();
    void showProvisionQr(const char* url);
    void hideProvisionQr();
    void createInfoSection(lv_obj_t* parent, lv_font_t* captionFont, lv_font_t* valueFont,
                           const char* caption, lv_obj_t** valueOut);
    static void onActionEvent(lv_event_t* e);

    lv_obj_t* _statusLabel = nullptr;
    lv_obj_t* _titleRow = nullptr;
    lv_obj_t* _stepsLabel = nullptr;
    lv_obj_t* _infoBox = nullptr;
    lv_obj_t* _ssidValue = nullptr;
    lv_obj_t* _ipValue = nullptr;
    lv_obj_t* _tzValue = nullptr;
    lv_obj_t* _qr = nullptr;
    lv_obj_t* _qrHint = nullptr;
    lv_obj_t* _actionBtn = nullptr;
    TTWiFiLinkState _state = TT_WIFI_LINK_IDLE;
    TTPortalPageKind _kind = TT_PORTAL_PAGE_NETWORK;
    bool _visible = false;
    bool _startLoading = false;
};
