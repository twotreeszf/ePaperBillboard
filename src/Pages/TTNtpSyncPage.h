#pragma once

#include <lvgl.h>
#include "../Base/TTScreenPage.h"
#include "../Base/TTNotificationPayloads.h"

#define TT_NTP_BTN_W         120
#define TT_NTP_BTN_GAP       12
#define TT_NTP_STATUS_TOP    16
#define TT_NTP_STATUS_LEFT   16
#define TT_NTP_TITLE_W       40
#define TT_NTP_ROW_GAP       8

class TTNtpSyncPage : public TTScreenPage {
public:
    TTNtpSyncPage() : TTScreenPage("NTP 对时") {}

    void setup() override;
    void willAppear() override;
    void willDisappear() override;

protected:
    void buildContent(lv_obj_t* screen) override;

private:
    void applyStatus(const TTTimeSyncPayload& status);
    void applyTimeRows(const TTTimeSyncPayload& status);
    lv_obj_t* createStatusRow(lv_obj_t* parent, lv_font_t* font, const char* title, lv_obj_t** valueOut);
    void showActionOnly();
    void showDoneButtons();
    void hideButtons();
    void startSync();
    void goWifiSettings();
    void goBack();
    void showSyncLoading(const char* text);
    void dismissSyncLoading();
    static void onRetryEvent(lv_event_t* e);
    static void onBackEvent(lv_event_t* e);

    lv_obj_t* _timeValue = nullptr;
    lv_obj_t* _tzValue = nullptr;
    lv_obj_t* _statusLabel = nullptr;
    lv_obj_t* _btnRow = nullptr;
    lv_obj_t* _retryBtn = nullptr;
    lv_obj_t* _backBtn = nullptr;
    TTTimeSyncState _state = TT_TIME_SYNC_IDLE;
    bool _visible = false;
    bool _loading = false;
};
