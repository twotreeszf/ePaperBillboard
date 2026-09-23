#pragma once

#include "../Base/TTScreenPage.h"
#include "../Base/TTNotificationPayloads.h"
#include <EPDConfig.h>

#define TT_UPDATE_PAD            16
#define TT_UPDATE_SECTION_GAP    16
#define TT_UPDATE_TITLE_GAP      4
#define TT_UPDATE_BODY_W         (EPD_WIDTH - (TT_UPDATE_PAD * 2))
#define TT_UPDATE_STATUS_MAX_W   (EPD_WIDTH - 64)
#define TT_UPDATE_STATUS_PAD     12
#define TT_UPDATE_STATUS_BORDER  1

class TTUpdatePage : public TTScreenPage {
public:
    TTUpdatePage() : TTScreenPage("系统更新") {}

    void setup() override;
    void willAppear() override;
    void willDisappear() override;
    bool allowPop() const override { return !_locked; }

protected:
    void buildContent(lv_obj_t* screen) override;

private:
    void applyOta(const TTOtaPayload& payload);
    void showInstalled();
    void setStatus(const char* text);
    void showCheckButton(bool show);
    void setLocked(bool locked);
    void startCheck();
    void startUpgrade();
    static void onCheckEvent(lv_event_t* e);

    lv_obj_t* _versionValue = nullptr;
    lv_obj_t* _notesValue = nullptr;
    lv_obj_t* _statusBox = nullptr;
    lv_obj_t* _statusLabel = nullptr;
    lv_obj_t* _checkBtn = nullptr;
    bool _visible = false;
    bool _loading = false;
    bool _locked = false;
};
