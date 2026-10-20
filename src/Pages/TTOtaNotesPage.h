#pragma once

#include "../Base/TTNavigationBar.h"
#include "../Base/TTScreenPage.h"
#include "../Base/TTTextButton.h"
#include "../Reader/TTReaderLayout.h"
#include "../Reader/TTReaderSource.h"
#include <memory>

#define TT_OTA_NOTES_PAGES_MAX        32
#define TT_OTA_NOTES_FIRST            "已经是第一页"
#define TT_OTA_NOTES_LAST             "已经是最后一页"
#define TT_OTA_NOTES_UPDATE_TEXT      "开始更新"
#define TT_OTA_NOTES_BUTTON_GAP       8
#define TT_OTA_NOTES_BUTTON_TEXT_GAP  8
#define TT_OTA_NOTES_CONTENT_H        (EPD_HEIGHT - TT_NAV_PAGE_INSET)
#define TT_OTA_NOTES_BUTTON_BOTTOM    TT_OTA_NOTES_BUTTON_GAP

class TTUpdatePage;

class TTOtaNotesPage : public TTScreenPage {
public:
    explicit TTOtaNotesPage(TTUpdatePage* host);

    void setup() override;
    void willAppear() override;
    void willDisappear() override;
    void willDestroy() override;
    bool handleKeyAction(TTKeyId key, TTKeyGesture gesture) override;

protected:
    void buildContent(lv_obj_t* screen) override;

private:
    bool openNotes();
    void showPage(bool refresh);
    void nextPage();
    void prevPage();
    void confirmUpdate();
    static void onUpdateClicked(lv_event_t* e);
    void attachFooter();
    void detachFooter();
    void updateFooter();

    TTUpdatePage* _host = nullptr;
    TTReaderSource _source;
    TTReaderLayout _layout;
    std::unique_ptr<TTReaderPageLayout> _page;
    lv_obj_t* _textView = nullptr;
    lv_obj_t* _footer = nullptr;
    lv_obj_t* _updateBtn = nullptr;
    uint32_t _starts[TT_OTA_NOTES_PAGES_MAX] = {};
    uint16_t _pageCount = 0;
    uint16_t _pageIndex = 0;
    bool _deepPaused = false;
    bool _openFailed = false;
};
