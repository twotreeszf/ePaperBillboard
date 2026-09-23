#pragma once

#include "../Base/TTScreenPage.h"
#include "../Base/TTNotificationPayloads.h"

#define TT_UPDATE_STATUS_TOP   16
#define TT_UPDATE_STATUS_LEFT  16
#define TT_UPDATE_TITLE_W      48
#define TT_UPDATE_ROW_GAP      8

class TTUpdatePage : public TTScreenPage {
public:
    TTUpdatePage() : TTScreenPage("系统更新") {}

    void setup() override;
    void willAppear() override;
    void willDisappear() override;

protected:
    void buildContent(lv_obj_t* screen) override;

private:
    void applyOta(const TTOtaPayload& payload);
    void setStatus(const char* text);
    void showCheckButton(bool show);
    void startCheck();
    void startUpgrade();
    static void onCheckEvent(lv_event_t* e);

    lv_obj_t* _versionValue = nullptr;
    lv_obj_t* _statusLabel = nullptr;
    lv_obj_t* _checkBtn = nullptr;
    bool _visible = false;
    bool _loading = false;
};
