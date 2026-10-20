#include "TTOtaNotesPage.h"
#include "TTReaderPage.h"
#include "TTUpdatePage.h"
#include "../Base/Logger.h"
#include "../Base/TTFontManager.h"
#include "../Base/TTInstance.h"
#include "../Base/TTLvglEpdDriver.h"
#include "../Base/TTNavigationBar.h"
#include "../Base/TTPopupLayer.h"
#include "../Reader/TTReaderTextView.h"
#include "../Service/TTOtaService.h"
#include <EPDConfig.h>
#include <LittleFS.h>
#include <cstdio>
#include <new>

#define TT_OTA_NOTES_TEXT_W  (EPD_WIDTH - 2 * TT_READER_MARGIN_X)

TTOtaNotesPage::TTOtaNotesPage(TTUpdatePage* host) : TTScreenPage("更新说明"), _host(host) {}

void TTOtaNotesPage::buildContent(lv_obj_t* screen) {
    TTFontManager& fonts = TTFontManager::instance();
    lv_font_t* footerFont = fonts.getFont(TT_NAV_STATUS_FONT);
    if (footerFont == nullptr) {
        footerFont = fonts.getFont(TT_READER_FONT_SIZE);
    }

    _textView = tt_reader_text_view_create(screen);
    lv_obj_set_pos(_textView, TT_READER_MARGIN_X, TT_READER_MARGIN_TOP);
    lv_obj_set_size(_textView, TT_OTA_NOTES_TEXT_W, EPD_HEIGHT - TT_NAV_PAGE_INSET - TT_READER_MARGIN_TOP);

    _footer = lv_label_create(screen);
    lv_obj_set_style_text_font(_footer, footerFont, 0);
    lv_obj_set_style_text_color(_footer, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(_footer, LV_OPA_TRANSP, 0);
    lv_label_set_text(_footer, "");
    lv_obj_set_hidden(_footer, true);
    createGroup();
}

bool TTOtaNotesPage::openNotes() {
    _page.reset(new (std::nothrow) TTReaderPageLayout());
    if (!_page) {
        LOG_E("OTA notes: page buffer alloc failed");
        return false;
    }
    if (!_source.open(LittleFS, TT_OTA_NOTES_PATH) || _source.size() == 0) {
        LOG_E("OTA notes: open %s failed", TT_OTA_NOTES_PATH);
        return false;
    }
    TTFontLoader* textFont = TTFontManager::instance().getLoader(TT_READER_FONT_SIZE);
    TTFontLoader* codeFont = TTFontManager::instance().getLoader(TT_READER_CODE_FONT_SIZE);
    if (textFont == nullptr) {
        LOG_E("OTA notes: text font missing");
        return false;
    }
    const int32_t lineH = textFont->getLineHeight();
    const int32_t height = TT_READER_PAGE_LINES * lineH + (TT_READER_PAGE_LINES - 1) * TT_READER_LINE_GAP;
    lv_obj_set_pos(_textView, TT_READER_MARGIN_X, TT_READER_MARGIN_TOP);
    lv_obj_set_size(_textView, TT_OTA_NOTES_TEXT_W, height);
    if (!_layout.begin(textFont, codeFont, TT_OTA_NOTES_TEXT_W, (int16_t)height, true)) {
        LOG_E("OTA notes: layout begin failed");
        return false;
    }

    uint32_t offset = 0;
    while (_pageCount < TT_OTA_NOTES_PAGES_MAX) {
        if (!_layout.layoutPage(_source, offset, *_page)) {
            LOG_E("OTA notes: layout at %u failed", (unsigned)offset);
            return false;
        }
        _starts[_pageCount++] = _page->start;
        if (_page->atEnd || _page->end <= offset) {
            break;
        }
        offset = _page->end;
    }
    if (!_page->atEnd) {
        LOG_W("OTA notes: stopped at page cap %u", (unsigned)TT_OTA_NOTES_PAGES_MAX);
    }
    _pageIndex = 0;
    showPage(false);
    LOG_I("OTA notes: opened %u bytes, %u pages", (unsigned)_source.size(), (unsigned)_pageCount);
    return _pageCount > 0;
}

void TTOtaNotesPage::setup() {
    TTScreenPage::setup();
    _openFailed = !openNotes();
}

void TTOtaNotesPage::willAppear() {
    TTScreenPage::willAppear();
    if (_openFailed) {
        if (_host != nullptr) {
            _host->notesUnavailable();
        }
        ITTNavigationController* nav = getNavigationController();
        runOnce(0, [nav]() {
            if (nav != nullptr && nav->canPop()) {
                nav->pop();
            }
        });
        return;
    }
    attachFooter();
    TTInstanceOf<TTLvglEpdDriver>().setAutoDeepRefresh(false);
    _deepPaused = true;
}

void TTOtaNotesPage::willDisappear() {
    detachFooter();
    if (_deepPaused) {
        TTInstanceOf<TTLvglEpdDriver>().setAutoDeepRefresh(true);
        _deepPaused = false;
    }
    TTScreenPage::willDisappear();
}

void TTOtaNotesPage::willDestroy() {
    detachFooter();
    if (_footer != nullptr) {
        lv_obj_delete(_footer);
        _footer = nullptr;
    }
    TTScreenPage::willDestroy();
    _source.close();
    _page.reset();
}

void TTOtaNotesPage::showPage(bool refresh) {
    if (_textView == nullptr || !_page || _pageCount == 0) {
        return;
    }
    if (!_layout.layoutPage(_source, _starts[_pageIndex], *_page)) {
        LOG_E("OTA notes: show page %u failed", (unsigned)_pageIndex);
        return;
    }
    tt_reader_text_view_set_page(_textView, &_layout, _page.get());
    updateFooter();
    if (refresh) {
        requestRefresh(TT_REFRESH_PARTIAL);
    }
}

void TTOtaNotesPage::nextPage() {
    if (_pageIndex + 1 >= _pageCount) {
        TTInstanceOf<TTPopupLayer>().showToast(TT_OTA_NOTES_LAST);
        return;
    }
    _pageIndex++;
    LOG_I("OTA notes: page %u/%u", (unsigned)(_pageIndex + 1), (unsigned)_pageCount);
    showPage(true);
}

void TTOtaNotesPage::prevPage() {
    if (_pageIndex == 0) {
        TTInstanceOf<TTPopupLayer>().showToast(TT_OTA_NOTES_FIRST);
        return;
    }
    _pageIndex--;
    LOG_I("OTA notes: page %u/%u", (unsigned)(_pageIndex + 1), (unsigned)_pageCount);
    showPage(true);
}

void TTOtaNotesPage::confirmUpdate() {
    LOG_I("OTA notes: center confirm");
    TTUpdatePage* host = _host;
    ITTNavigationController* nav = getNavigationController();
    if (host != nullptr) {
        host->requestUpgradeFromNotes();
    }
    if (nav != nullptr) {
        nav->pop();
    }
}

void TTOtaNotesPage::attachFooter() {
    ITTNavigationController* nav = getNavigationController();
    TTNavigationBar* bar = nav != nullptr ? nav->getNavBar() : nullptr;
    if (bar == nullptr || _footer == nullptr) {
        return;
    }
    bar->setSensorsVisible(false);
    bar->setCenterContainer(_footer);
    updateFooter();
}

void TTOtaNotesPage::detachFooter() {
    ITTNavigationController* nav = getNavigationController();
    TTNavigationBar* bar = nav != nullptr ? nav->getNavBar() : nullptr;
    if (bar == nullptr) {
        return;
    }
    if (bar->centerContainer() == _footer) {
        bar->setCenterContainer(nullptr);
    }
    bar->setSensorsVisible(true);
}

void TTOtaNotesPage::updateFooter() {
    if (_footer == nullptr || _pageCount == 0) {
        return;
    }
    char text[16];
    snprintf(text, sizeof(text), "%u / %u", (unsigned)(_pageIndex + 1), (unsigned)_pageCount);
    lv_label_set_text(_footer, text);
    ITTNavigationController* nav = getNavigationController();
    TTNavigationBar* bar = nav != nullptr ? nav->getNavBar() : nullptr;
    if (bar != nullptr && bar->centerContainer() == _footer) {
        bar->setCenterContainer(_footer);
    }
}

bool TTOtaNotesPage::handleKeyAction(TTKeyId key, TTKeyGesture gesture) {
    if (gesture != TT_KEY_CLICK || _openFailed) {
        return TTScreenPage::handleKeyAction(key, gesture);
    }
    if (key == TT_KEY_RIGHT) {
        nextPage();
        return true;
    }
    if (key == TT_KEY_LEFT) {
        prevPage();
        return true;
    }
    if (key == TT_KEY_CENTER) {
        confirmUpdate();
        return true;
    }
    return TTScreenPage::handleKeyAction(key, gesture);
}
