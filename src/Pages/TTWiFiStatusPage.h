#pragma once

#include <lvgl.h>
#include "../Base/TTScreenPage.h"
#include "../Base/TTNotificationPayloads.h"

#define TT_WIFI_STATUS_BTN_W      90
#define TT_WIFI_STATUS_BTN_GAP    8
#define TT_WIFI_STATUS_TOP        16
#define TT_WIFI_STATUS_LEFT       16
#define TT_WIFI_STATUS_TITLE_W    40
#define TT_WIFI_STATUS_ROW_GAP    8

class TTWiFiStatusPage : public TTScreenPage {
public:
    TTWiFiStatusPage() : TTScreenPage("Wi-Fi") {}

    void setup() override;
    void willAppear() override;
    void willDisappear() override;

protected:
    void buildContent(lv_obj_t* screen) override;

private:
    void applyStatus(const TTWiFiStatusPayload& status);
    lv_obj_t* createStatusRow(lv_obj_t* parent, lv_font_t* font, const char* title, lv_obj_t** valueOut);
    void goWebSettings();
    void goBack();
    void reconnect();
    void showReadLoading(const char* text);
    void dismissReadLoading();
    static void onActionEvent(lv_event_t* e);
    static void onReconnectEvent(lv_event_t* e);
    static void onBackEvent(lv_event_t* e);

    lv_obj_t* _stateValue = nullptr;
    lv_obj_t* _ssidValue = nullptr;
    lv_obj_t* _ipValue = nullptr;
    lv_obj_t* _hintLabel = nullptr;
    lv_obj_t* _btnRow = nullptr;
    lv_obj_t* _reconnectBtn = nullptr;
    lv_obj_t* _actionBtn = nullptr;
    lv_obj_t* _backBtn = nullptr;
    bool _visible = false;
    bool _loading = false;
};
