#include "TTBookshelfPage.h"
#include "TTBookTransferPage.h"
#include "TTReaderPage.h"
#include "../Base/Logger.h"
#include "../Base/TTFontManager.h"
#include "../Base/TTInstance.h"
#include "../Base/TTNavigationBar.h"
#include "../Base/TTPopupLayer.h"
#include "../Service/TTSdCardService.h"
#include "../Tasks/TTWiFiTask.h"
#include <EPDConfig.h>
#include <cstdio>
#include <memory>

#define TT_SHELF_ROW_W   (EPD_WIDTH - 2 * TT_SHELF_PAD_X)
#define TT_SHELF_NAME_W  (TT_SHELF_ROW_W - 2 * TT_SHELF_ROW_PAD_X - TT_SHELF_INFO_W - TT_SHELF_INFO_GAP)

void TTBookshelfPage::buildContent(lv_obj_t* screen) {
    TTFontManager& fonts = TTFontManager::instance();
    lv_font_t* font = fonts.getFont(TT_SHELF_FONT_SIZE);
    lv_font_t* infoFont = fonts.getFont(TT_SHELF_INFO_FONT_SIZE);
    lv_font_t* ordinalFont = fonts.getFont(TT_SHELF_ORDINAL_FONT_SIZE);
    if (infoFont == nullptr) {
        infoFont = font;
    }
    if (ordinalFont == nullptr) {
        ordinalFont = infoFont;
    }

    for (int i = 0; i < TT_SHELF_ROWS; i++) {
        _rows[i] = lv_obj_create(screen);
        lv_obj_set_pos(_rows[i], TT_SHELF_PAD_X, TT_SHELF_TOP + i * TT_SHELF_ROW_H);
        lv_obj_set_size(_rows[i], TT_SHELF_ROW_W, TT_SHELF_ROW_H);
        lv_obj_set_style_bg_color(_rows[i], lv_color_white(), 0);
        lv_obj_set_style_bg_opa(_rows[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(_rows[i], 0, 0);
        lv_obj_set_style_radius(_rows[i], 0, 0);
        lv_obj_set_style_pad_all(_rows[i], 0, 0);
        lv_obj_set_scrollable(_rows[i], false);
        lv_obj_set_clickable(_rows[i], false);
        lv_obj_set_hidden(_rows[i], true);

        _names[i] = lv_label_create(_rows[i]);
        lv_obj_set_style_text_font(_names[i], font, 0);
        lv_obj_set_style_text_color(_names[i], lv_color_black(), 0);
        lv_obj_set_width(_names[i], TT_SHELF_NAME_W);
        lv_label_set_long_mode(_names[i], LV_LABEL_LONG_DOT);
        lv_label_set_text(_names[i], "");
        lv_obj_align(_names[i], LV_ALIGN_LEFT_MID, TT_SHELF_ROW_PAD_X, 0);

        _infos[i] = lv_label_create(_rows[i]);
        lv_obj_set_style_text_font(_infos[i], infoFont, 0);
        lv_obj_set_style_text_color(_infos[i], lv_color_black(), 0);
        lv_obj_set_style_text_align(_infos[i], LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_width(_infos[i], TT_SHELF_INFO_W);
        lv_label_set_long_mode(_infos[i], LV_LABEL_LONG_CLIP);
        lv_label_set_text(_infos[i], "");
        lv_obj_align(_infos[i], LV_ALIGN_RIGHT_MID, -TT_SHELF_ROW_PAD_X, 0);
    }

    _ordinal = lv_label_create(screen);
    lv_obj_set_style_text_font(_ordinal, ordinalFont, 0);
    lv_obj_set_style_text_color(_ordinal, lv_color_black(), 0);
    lv_label_set_text(_ordinal, "");
    lv_obj_align(_ordinal, LV_ALIGN_BOTTOM_RIGHT, -TT_SHELF_PAD_X, -TT_SHELF_ORDINAL_BOTTOM);

    lv_obj_t* transferHint = lv_label_create(screen);
    lv_obj_set_style_text_font(transferHint, ordinalFont, 0);
    lv_obj_set_style_text_color(transferHint, lv_color_black(), 0);
    lv_label_set_text(transferHint, TT_SHELF_TEXT_TRANSFER_HINT);
    lv_obj_align(transferHint, LV_ALIGN_BOTTOM_LEFT, TT_SHELF_PAD_X, -TT_SHELF_ORDINAL_BOTTOM);

    _hit = lv_btn_create(screen);
    lv_obj_remove_style_all(_hit);
    lv_obj_set_pos(_hit, 0, 0);
    lv_obj_set_size(_hit, EPD_WIDTH, EPD_HEIGHT - TT_NAV_PAGE_INSET);
    lv_obj_add_event_cb(_hit, [](lv_event_t* e) {
        TTBookshelfPage* self = (TTBookshelfPage*)lv_event_get_user_data(e);
        if (self != nullptr) {
            self->openSelected();
        }
    }, LV_EVENT_CLICKED, this);
    addToFocusGroup(_hit);
}

void TTBookshelfPage::setup() {
    TTScreenPage::setup();
    TTInstanceOf<TTWiFiTask>().requestReleaseDriverWhenIdle(true);
    const TTSdMountResult result = TTInstanceOf<TTSdCardService>().acquire();
    if (result == TT_SD_MOUNT_NO_CARD) {
        _error = TT_SHELF_TEXT_NO_CARD;
    } else if (result != TT_SD_MOUNT_OK) {
        _error = TT_SHELF_TEXT_MOUNT_FAILED;
    } else {
        _sdAcquired = true;
        if (!TTInstanceOf<TTBookStore>().ensureDirs()) {
            LOG_W("Bookshelf: books dir unavailable");
        }
        loadBooks();
    }
    if (_error != nullptr) {
        LOG_W("Bookshelf: %s", _error);
    }
    showRows();
}

void TTBookshelfPage::willAppear() {
    TTScreenPage::willAppear();
    if (_error != nullptr) {
        const char* msg = _error;
        runOnce(0, [this, msg]() { leaveWithMessage(msg); });
        return;
    }
    const bool firstAppear = !_appeared;
    if (_appeared) {
        TTSdCardService& sd = TTInstanceOf<TTSdCardService>();
        if (!sd.isMounted() || !sd.isCardInserted()) {
            LOG_W("Bookshelf: card gone on return, leave");
            _leaving = true;
            popDeferred();
            return;
        }
        loadBooks();
        showRows();
    }
    _appeared = true;
    if (_hit != nullptr) {
        lv_group_focus_obj(_hit);
    }
    _pollHandle = runRepeat(TT_SD_DETECT_POLL_MS, [this]() { pollCard(); }, false);
    if (firstAppear && _books.empty()) {
        runOnce(0, [this]() { showEmptyHint(); });
    }
}

void TTBookshelfPage::willDisappear() {
    if (_pollHandle != 0) {
        cancelRepeat(_pollHandle);
        _pollHandle = 0;
    }
    if (_leaving && TTInstanceOf<TTPopupLayer>().isDialogShown()) {
        TTInstanceOf<TTPopupLayer>().dismissDialog();
    }
    TTScreenPage::willDisappear();
}

void TTBookshelfPage::willDestroy() {
    if (_sdAcquired) {
        TTInstanceOf<TTSdCardService>().release();
        _sdAcquired = false;
    }
    TTInstanceOf<TTWiFiTask>().requestReleaseDriverWhenIdle(false);
    TTScreenPage::willDestroy();
}

bool TTBookshelfPage::handleKeyAction(TTKeyId key, TTKeyGesture gesture) {
    if (gesture == TT_KEY_CLICK && (key == TT_KEY_LEFT || key == TT_KEY_RIGHT)) {
        if (TTInstanceOf<TTPopupLayer>().isDialogShown()) {
            return false;
        }
        if (_leaving || _books.empty()) {
            return true;
        }
        const size_t count = _books.size();
        if (key == TT_KEY_LEFT) {
            _selected = (_selected + 1) % count;
        } else {
            _selected = (_selected + count - 1) % count;
        }
        showRows();
        requestRefresh(TT_REFRESH_PARTIAL);
        return true;
    }
    if (gesture == TT_KEY_LONG_PRESS && key == TT_KEY_CENTER) {
        if (TTInstanceOf<TTPopupLayer>().isDialogShown()) {
            return false;
        }
        openTransfer();
        return true;
    }
    return TTScreenPage::handleKeyAction(key, gesture);
}

void TTBookshelfPage::loadBooks() {
    if (!TTInstanceOf<TTBookStore>().listBooks(_books)) {
        _books.clear();
    }
    _progress.assign(_books.size(), TT_SHELF_PROGRESS_UNKNOWN);
    _selected = 0;
}

void TTBookshelfPage::formatInfo(size_t index, char* out, size_t outSize) {
    const TTBookEntry& book = _books[index];
    int16_t& progress = _progress[index];
    if (progress == TT_SHELF_PROGRESS_UNKNOWN) {
        uint32_t offset = 0;
        progress = TTInstanceOf<TTBookStore>().loadProgress(book, offset) && book.size > 0
            ? (int16_t)((uint64_t)offset * 100 / book.size) : TT_SHELF_PROGRESS_NONE;
    }
    if (progress >= 0) {
        snprintf(out, outSize, "%d%%", (int)progress);
    } else if (book.size >= TT_SHELF_SIZE_MB) {
        const uint32_t tenths = (uint32_t)((uint64_t)book.size * 10 / TT_SHELF_SIZE_MB);
        snprintf(out, outSize, "%u.%uM", (unsigned)(tenths / 10), (unsigned)(tenths % 10));
    } else {
        snprintf(out, outSize, "%uK", (unsigned)((book.size + TT_SHELF_SIZE_KB - 1) / TT_SHELF_SIZE_KB));
    }
}

void TTBookshelfPage::showRows() {
    const size_t count = _books.size();
    if (count == 0) {
        for (int row = 0; row < TT_SHELF_ROWS; row++) {
            lv_obj_set_hidden(_rows[row], true);
        }
        lv_label_set_text(_ordinal, "");
        return;
    }
    if (_selected >= count) {
        _selected = 0;
    }
    size_t window = 0;
    if (count > TT_SHELF_ROWS) {
        const int centered = (int)_selected - TT_SHELF_ROWS / 2;
        window = centered < 0 ? 0 : (size_t)centered;
        if (window + TT_SHELF_ROWS > count) {
            window = count - TT_SHELF_ROWS;
        }
    }
    TTBookStore& store = TTInstanceOf<TTBookStore>();
    for (int row = 0; row < TT_SHELF_ROWS; row++) {
        const size_t index = window + row;
        if (index >= count) {
            lv_obj_set_hidden(_rows[row], true);
            continue;
        }
        char info[TT_SHELF_INFO_TEXT_MAX];
        formatInfo(index, info, sizeof(info));
        const bool selected = index == _selected;
        const lv_color_t ink = selected ? lv_color_white() : lv_color_black();
        const lv_color_t paper = selected ? lv_color_black() : lv_color_white();
        lv_label_set_text(_names[row], store.displayName(_books[index].name).c_str());
        lv_label_set_text(_infos[row], info);
        lv_obj_set_style_bg_color(_rows[row], paper, 0);
        lv_obj_set_style_text_color(_names[row], ink, 0);
        lv_obj_set_style_text_color(_infos[row], ink, 0);
        lv_obj_set_hidden(_rows[row], false);
    }
    char ordinal[TT_SHELF_ORDINAL_TEXT_MAX];
    snprintf(ordinal, sizeof(ordinal), "%u/%u", (unsigned)(_selected + 1), (unsigned)count);
    lv_label_set_text(_ordinal, ordinal);
    LOG_I("Bookshelf: select %u/%u window=%u", (unsigned)(_selected + 1), (unsigned)count, (unsigned)window);
}

void TTBookshelfPage::openSelected() {
    if (_leaving) {
        return;
    }
    if (_books.empty()) {
        showEmptyHint();
        return;
    }
    if (_selected >= _books.size()) {
        return;
    }
    if (!TTInstanceOf<TTSdCardService>().isCardInserted()) {
        pollCard();
        return;
    }
    LOG_I("Bookshelf: open %s (%u bytes)", _books[_selected].name.c_str(), (unsigned)_books[_selected].size);
    getNavigationController()->pushPage(std::unique_ptr<TTScreenPage>(new TTReaderPage(_books[_selected])));
}

void TTBookshelfPage::openTransfer() {
    if (_leaving) {
        return;
    }
    if (!TTInstanceOf<TTSdCardService>().isCardInserted()) {
        pollCard();
        return;
    }
    LOG_I("Bookshelf: open book transfer");
    getNavigationController()->pushPage(std::unique_ptr<TTScreenPage>(new TTBookTransferPage()));
}

void TTBookshelfPage::showEmptyHint() {
    if (_leaving || !_books.empty() || TTInstanceOf<TTPopupLayer>().isDialogShown()) {
        return;
    }
    LOG_I("Bookshelf: show empty hint");
    TTInstanceOf<TTPopupLayer>().showConfirm(TT_SHELF_TEXT_EMPTY, nullptr);
}

void TTBookshelfPage::pollCard() {
    if (_leaving || TTInstanceOf<TTSdCardService>().isCardInserted()) {
        return;
    }
    LOG_W("Bookshelf: card removed");
    TTInstanceOf<TTSdCardService>().handleCardRemoved();
    leaveWithMessage(TT_SHELF_TEXT_CARD_REMOVED);
}

void TTBookshelfPage::leaveWithMessage(const char* msg) {
    if (_leaving) {
        return;
    }
    _leaving = true;
    LOG_W("Bookshelf: leave with message: %s", msg);
    if (_pollHandle != 0) {
        cancelRepeat(_pollHandle);
        _pollHandle = 0;
    }
    TTInstanceOf<TTPopupLayer>().showConfirm(msg, [this]() { popDeferred(); });
}

void TTBookshelfPage::popDeferred() {
    runOnce(0, [this]() {
        ITTNavigationController* nav = getNavigationController();
        if (nav != nullptr && nav->canPop()) {
            nav->pop();
        }
    });
}
