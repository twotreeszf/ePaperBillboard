#pragma once

#include "../Base/TTNavigationBar.h"
#include "../Base/TTScreenPage.h"
#include "../Base/TTTextButton.h"
#include "../Models/TTNotificationPayloads.h"
#include "../Reader/TTReaderLayout.h"
#include "../Reader/TTReaderSource.h"
#include "../Service/TTOtaService.h"
#include <EPDConfig.h>
#include <memory>

#define TT_UPDATE_PAD            16
#define TT_UPDATE_SECTION_GAP    16
#define TT_UPDATE_TITLE_GAP      4
#define TT_UPDATE_BODY_W         (EPD_WIDTH - (TT_UPDATE_PAD * 2))
#define TT_UPDATE_STATUS_MAX_W   (EPD_WIDTH - 64)
#define TT_UPDATE_STATUS_PAD     12
#define TT_UPDATE_STATUS_BORDER  1
#define TT_UPDATE_NOTES_FAIL     "无法打开更新说明"
#define TT_UPDATE_NOTES_PAGES_MAX 32
#define TT_UPDATE_NOTES_FIRST    "已经是第一页"
#define TT_UPDATE_NOTES_LAST     "已经是最后一页"
#define TT_UPDATE_CONTENT_H      (EPD_HEIGHT - TT_NAV_PAGE_INSET)
#define TT_UPDATE_BUTTON_BOTTOM  8
#define TT_UPDATE_NOTES_GAP      8

class TTUpdatePage : public TTScreenPage {
public:
    TTUpdatePage() : TTScreenPage("系统更新") {}

    void setup() override;
    void willAppear() override;
    void willDisappear() override;
    void willDestroy() override;
    bool handleKeyAction(TTKeyId key, TTKeyGesture gesture) override;
    bool allowPop() const override { return !_locked; }
    void requestUpgradeFromNotes();
    void notesUnavailable();

protected:
    void buildContent(lv_obj_t* screen) override;

private:
    void applyOta(const TTOtaPayload& payload);
    void showInstalled();
    bool showInstalledNotes(const char* notes);
    int32_t notesMaxHeight() const;
    void resetNotesPages();
    void showNotesPage(bool refresh);
    void nextNotesPage();
    void prevNotesPage();
    void syncNotesChrome();
    void attachNotesFooter();
    void detachNotesFooter();
    void updateNotesFooter();
    void rememberTarget(const TTOtaPayload& payload);
    void showTarget();
    void setStatus(const char* text);
    void showCheckButton(bool show);
    void setLocked(bool locked);
    void setDeepRefreshBlocked(bool blocked);
    void startCheck();
    void startUpgrade();
    void showRebootDialog(const char* message);
    void showFinishedDialog(const char* message);
    static void onCheckEvent(lv_event_t* e);

    lv_obj_t* _versionTitle = nullptr;
    lv_obj_t* _versionValue = nullptr;
    lv_obj_t* _notesValue = nullptr;
    lv_obj_t* _notesView = nullptr;
    lv_obj_t* _notesFooter = nullptr;
    TTReaderSource _notesSource;
    TTReaderLayout _notesLayout;
    std::unique_ptr<TTReaderPageLayout> _notesPage;
    uint32_t _notesStarts[TT_UPDATE_NOTES_PAGES_MAX] = {};
    uint16_t _notesPageCount = 0;
    uint16_t _notesPageIndex = 0;
    lv_obj_t* _statusBox = nullptr;
    lv_obj_t* _statusLabel = nullptr;
    lv_obj_t* _checkBtn = nullptr;
    bool _visible = false;
    bool _loading = false;
    bool _locked = false;
    bool _updateStarted = false;
    bool _deepBlocked = false;
    bool _upgradeFromNotes = false;
    bool _notesFailed = false;
    char _targetVersion[TT_OTA_VER_MAX] = {};
};
