#include "TTBookshelfPage.h"
#include "TTBookTransferPage.h"
#include "TTReaderPage.h"
#include "../Base/Logger.h"
#include "../Base/TTFontManager.h"
#include "../Base/TTInstance.h"
#include "../Base/TTNavigationBar.h"
#include "../Base/TTTextFit.h"
#include "../Base/TTPopupLayer.h"
#include "../Service/TTSdCardService.h"
#include "../Tasks/TTWiFiTask.h"
#include <EPDConfig.h>
#include <cstdio>
#include <cstring>
#include <memory>

#define TT_SHELF_ROW_W      EPD_WIDTH
#define TT_SHELF_TEXT_X     (TT_SHELF_ICON_X + TT_SHELF_ICON_W + TT_SHELF_TEXT_GAP)
#define TT_SHELF_PROGRESS_X (TT_SHELF_ROW_W - TT_SHELF_PAD_X - TT_SHELF_ROW_PAD_X - TT_SHELF_PROGRESS_W)
#define TT_SHELF_TEXT_W     (TT_SHELF_PROGRESS_X - TT_SHELF_TEXT_X - TT_SHELF_PROGRESS_GAP)
#define TT_SHELF_SIZE_Y     (TT_SHELF_NAME_Y + TT_SHELF_FONT_SIZE + TT_SHELF_NAME_SIZE_GAP)

static void shelfDrawSpan(lv_layer_t* layer, const lv_draw_rect_dsc_t* ink,
                          int x1, int y1, int x2, int y2) {
    lv_area_t area;
    area.x1 = x1;
    area.x2 = x2;
    area.y1 = y1;
    area.y2 = y2;
    lv_draw_rect(layer, ink, &area);
}

static void shelfDrawMark(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_DRAW_MAIN) {
        return;
    }
    lv_obj_t* mark = (lv_obj_t*)lv_event_get_target(e);
    lv_layer_t* layer = lv_event_get_layer(e);
    lv_area_t coords;
    lv_obj_get_coords(mark, &coords);
    const int x = coords.x1;
    const int top = coords.y1;
    const int w = (int)lv_obj_get_width(mark);
    const int h = (int)lv_obj_get_height(mark);
    if (w <= 0 || h <= 0) {
        return;
    }
    int chamfer = TT_SHELF_MARK_CHAMFER;
    if (chamfer > w) {
        chamfer = w;
    }
    if (chamfer * 2 > h) {
        chamfer = h / 2;
    }

    lv_draw_rect_dsc_t ink;
    lv_draw_rect_dsc_init(&ink);
    ink.bg_color = lv_color_black();
    ink.bg_opa = LV_OPA_COVER;
    ink.border_width = 0;
    ink.radius = 0;

    if (h > chamfer * 2) {
        shelfDrawSpan(layer, &ink, x, top + chamfer, x + w - 1, top + h - chamfer - 1);
    }
    const int flat = w - chamfer;
    for (int i = 0; i < chamfer; i++) {
        const int right = x + flat + i - 1;
        if (right < x) {
            continue;
        }
        shelfDrawSpan(layer, &ink, x, top + i, right, top + i);
        shelfDrawSpan(layer, &ink, x, top + h - 1 - i, right, top + h - 1 - i);
    }
}

static void shelfDrawFileIcon(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_DRAW_MAIN) {
        return;
    }
    lv_obj_t* icon = (lv_obj_t*)lv_event_get_target(e);
    lv_layer_t* layer = lv_event_get_layer(e);
    lv_area_t coords;
    lv_obj_get_coords(icon, &coords);
    const int x = coords.x1;
    const int y = coords.y1;
    const int fold = TT_SHELF_ICON_FOLD;
    const int right = x + TT_SHELF_ICON_W - 1;
    const int bottom = y + TT_SHELF_ICON_H - 1;

    lv_draw_rect_dsc_t ink;
    lv_draw_rect_dsc_init(&ink);
    ink.bg_color = lv_color_black();
    ink.bg_opa = LV_OPA_COVER;
    ink.border_width = 0;
    ink.radius = 0;

    shelfDrawSpan(layer, &ink, x, y, right - fold, y);
    shelfDrawSpan(layer, &ink, x, y, x, bottom);
    shelfDrawSpan(layer, &ink, x, bottom, right, bottom);
    shelfDrawSpan(layer, &ink, right, y + fold, right, bottom);
    shelfDrawSpan(layer, &ink, right - fold, y, right - fold, y + fold);
    shelfDrawSpan(layer, &ink, right - fold, y + fold, right, y + fold);
    for (int i = 0; i <= fold; i++) {
        shelfDrawSpan(layer, &ink, right - fold + i, y + i, right - fold + i, y + i);
    }

    const int lineLeft = x + TT_SHELF_ICON_LINE_INSET;
    const int lineSpan = TT_SHELF_ICON_LINE_BOTTOM - TT_SHELF_ICON_LINE_TOP;
    const int lineGaps = TT_SHELF_ICON_LINE_COUNT + 1;
    for (int i = 0; i < TT_SHELF_ICON_LINE_COUNT; i++) {
        const int lineY = y + TT_SHELF_ICON_LINE_TOP + (lineSpan * (i + 1)) / lineGaps;
        int lineRight = right - TT_SHELF_ICON_LINE_INSET;
        const int localY = lineY - y;
        if (localY < fold) {
            const int diagonalX = right - fold + localY - 2;
            if (lineRight > diagonalX) {
                lineRight = diagonalX;
            }
        }
        if (lineRight > lineLeft) {
            shelfDrawSpan(layer, &ink, lineLeft, lineY, lineRight, lineY);
        }
    }
}

void TTBookshelfPage::buildContent(lv_obj_t* screen) {
    TTFontManager& fonts = TTFontManager::instance();
    lv_font_t* font = fonts.getFont(TT_SHELF_FONT_SIZE);
    lv_font_t* sizeFont = fonts.getFont(TT_SHELF_SIZE_FONT_SIZE);
    lv_font_t* typeFont = fonts.getFont(TT_SHELF_TYPE_FONT_SIZE);
    lv_font_t* titleFont = fonts.getFont(TT_SHELF_TITLE_FONT_SIZE);
    lv_font_t* countFont = fonts.getFont(TT_SHELF_COUNT_FONT_SIZE);
    lv_font_t* transferFont = fonts.getFont(TT_SHELF_TRANSFER_FONT_SIZE);
    lv_font_t* progressFont = fonts.getFont(TT_SHELF_PROGRESS_FONT_SIZE);
    if (sizeFont == nullptr) {
        sizeFont = font;
    }
    if (typeFont == nullptr) {
        typeFont = sizeFont;
    }
    if (titleFont == nullptr) {
        titleFont = font;
    }
    if (countFont == nullptr) {
        countFont = sizeFont;
    }
    if (transferFont == nullptr) {
        transferFont = countFont;
    }
    if (progressFont == nullptr) {
        progressFont = font;
    }

    lv_obj_t* header = lv_obj_create(screen);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_size(header, EPD_WIDTH, TT_SHELF_TITLE_H);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(header, lv_color_black(), 0);
    lv_obj_set_style_border_side(header, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(header, 1, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_set_scrollable(header, false);
    lv_obj_set_clickable(header, false);

    lv_obj_t* title = lv_label_create(header);
    lv_obj_set_style_text_font(title, titleFont, 0);
    lv_obj_set_style_text_color(title, lv_color_black(), 0);
    lv_label_set_text(title, TT_SHELF_TEXT_TITLE);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, TT_SHELF_PAD_X, 0);

    _count = lv_label_create(header);
    lv_obj_set_style_text_font(_count, countFont, 0);
    lv_obj_set_style_text_color(_count, lv_color_black(), 0);
    lv_label_set_text(_count, "");
    lv_obj_align(_count, LV_ALIGN_RIGHT_MID, -TT_SHELF_PAD_X, 0);

    for (int i = 0; i < TT_SHELF_ROWS; i++) {
        _rows[i] = lv_obj_create(screen);
        lv_obj_set_pos(_rows[i], 0, TT_SHELF_TOP + i * TT_SHELF_ROW_H);
        lv_obj_set_size(_rows[i], TT_SHELF_ROW_W, TT_SHELF_ROW_H);
        lv_obj_set_style_bg_color(_rows[i], lv_color_white(), 0);
        lv_obj_set_style_bg_opa(_rows[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(_rows[i], 0, 0);
        lv_obj_set_style_radius(_rows[i], 0, 0);
        lv_obj_set_style_pad_all(_rows[i], 0, 0);
        lv_obj_set_scrollable(_rows[i], false);
        lv_obj_set_clickable(_rows[i], false);
        lv_obj_set_hidden(_rows[i], true);

        _marks[i] = lv_obj_create(_rows[i]);
        lv_obj_set_pos(_marks[i], 0, TT_SHELF_CONTENT_Y);
        lv_obj_set_size(_marks[i], TT_SHELF_MARK_W, TT_SHELF_ICON_H);
        lv_obj_set_style_bg_opa(_marks[i], LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(_marks[i], 0, 0);
        lv_obj_set_style_radius(_marks[i], 0, 0);
        lv_obj_set_style_pad_all(_marks[i], 0, 0);
        lv_obj_set_scrollable(_marks[i], false);
        lv_obj_set_clickable(_marks[i], false);
        lv_obj_set_hidden(_marks[i], true);
        lv_obj_add_event_cb(_marks[i], shelfDrawMark, LV_EVENT_DRAW_MAIN, nullptr);

        _icons[i] = lv_obj_create(_rows[i]);
        lv_obj_set_pos(_icons[i], TT_SHELF_ICON_X, TT_SHELF_ICON_Y);
        lv_obj_set_size(_icons[i], TT_SHELF_ICON_W, TT_SHELF_ICON_H);
        lv_obj_set_style_bg_opa(_icons[i], LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(_icons[i], 0, 0);
        lv_obj_set_style_pad_all(_icons[i], 0, 0);
        lv_obj_set_scrollable(_icons[i], false);
        lv_obj_set_clickable(_icons[i], false);
        lv_obj_add_event_cb(_icons[i], shelfDrawFileIcon, LV_EVENT_DRAW_MAIN, nullptr);

        _types[i] = lv_label_create(_icons[i]);
        lv_obj_set_style_text_font(_types[i], typeFont, 0);
        lv_obj_set_style_text_color(_types[i], lv_color_black(), 0);
        lv_obj_set_style_text_align(_types[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_pad_all(_types[i], 0, 0);
        lv_obj_set_width(_types[i], TT_SHELF_ICON_W);
        lv_obj_align(_types[i], LV_ALIGN_BOTTOM_MID, 0, -TT_SHELF_TYPE_BOTTOM);
        lv_label_set_long_mode(_types[i], LV_LABEL_LONG_CLIP);
        lv_label_set_text(_types[i], "");

        _names[i] = lv_label_create(_rows[i]);
        lv_obj_set_style_text_font(_names[i], font, 0);
        lv_obj_set_style_text_color(_names[i], lv_color_black(), 0);
        lv_obj_set_pos(_names[i], TT_SHELF_TEXT_X, TT_SHELF_NAME_Y);
        lv_obj_set_width(_names[i], TT_SHELF_TEXT_W);
        lv_label_set_long_mode(_names[i], LV_LABEL_LONG_CLIP);
        lv_label_set_text(_names[i], "");

        _sizes[i] = lv_label_create(_rows[i]);
        lv_obj_set_style_text_font(_sizes[i], sizeFont, 0);
        lv_obj_set_style_text_color(_sizes[i], lv_color_black(), 0);
        lv_obj_set_pos(_sizes[i], TT_SHELF_TEXT_X, TT_SHELF_SIZE_Y);
        lv_obj_set_width(_sizes[i], TT_SHELF_TEXT_W);
        lv_label_set_long_mode(_sizes[i], LV_LABEL_LONG_CLIP);
        lv_label_set_text(_sizes[i], "");

        _percents[i] = lv_label_create(_rows[i]);
        lv_obj_set_style_text_font(_percents[i], progressFont, 0);
        lv_obj_set_style_text_color(_percents[i], lv_color_black(), 0);
        lv_obj_set_style_text_align(_percents[i], LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_pos(_percents[i], TT_SHELF_PROGRESS_X, TT_SHELF_PROGRESS_Y);
        lv_obj_set_width(_percents[i], TT_SHELF_PROGRESS_W);
        lv_label_set_long_mode(_percents[i], LV_LABEL_LONG_CLIP);
        lv_label_set_text(_percents[i], "");
    }

    _transfer = lv_label_create(screen);
    lv_obj_set_style_text_font(_transfer, transferFont, 0);
    lv_obj_set_style_text_align(_transfer, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_radius(_transfer, 0, 0);
    lv_obj_set_style_pad_hor(_transfer, TT_SHELF_TRANSFER_PAD_X, 0);
    lv_obj_set_style_pad_top(_transfer, TT_SHELF_TRANSFER_PAD_TOP, 0);
    lv_obj_set_style_pad_bottom(_transfer, TT_SHELF_TRANSFER_PAD_BOTTOM, 0);
    lv_obj_set_style_border_color(_transfer, lv_color_black(), 0);
    lv_obj_set_style_border_width(_transfer, TT_SHELF_TRANSFER_BORDER, 0);
    lv_obj_set_style_border_opa(_transfer, LV_OPA_COVER, 0);
    lv_label_set_text(_transfer, TT_SHELF_TEXT_TRANSFER);

    _hit = lv_btn_create(screen);
    lv_obj_remove_style_all(_hit);
    lv_obj_set_pos(_hit, 0, 0);
    lv_obj_set_size(_hit, EPD_WIDTH, EPD_HEIGHT - TT_NAV_PAGE_INSET);
    lv_obj_add_event_cb(_hit, [](lv_event_t* e) {
        TTBookshelfPage* self = (TTBookshelfPage*)lv_event_get_user_data(e);
        if (self == nullptr) {
            return;
        }
        if (self->_transferFocused || self->_books.empty()) {
            self->openTransfer();
        } else {
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
}

void TTBookshelfPage::willDisappear() {
    if (_pollHandle != 0) {
        cancelRepeat(_pollHandle);
        _pollHandle = 0;
    }
    if (TTInstanceOf<TTPopupLayer>().isDialogShown()) {
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
        if (key == TT_KEY_RIGHT) {
            if (_transferFocused) {
                return true;
            }
            if (_selected == 0) {
                _transferFocused = true;
                showRows();
                requestRefresh(TT_REFRESH_PARTIAL);
                LOG_I("Bookshelf: focus transfer");
                return true;
            }
            _selected = (_selected + count - 1) % count;
        } else if (_transferFocused) {
            _transferFocused = false;
            showRows();
            requestRefresh(TT_REFRESH_PARTIAL);
            LOG_I("Bookshelf: focus list");
            return true;
        } else {
            _selected = (_selected + 1) % count;
        }
        showRows();
        requestRefresh(TT_REFRESH_PARTIAL);
        return true;
    }
    if (gesture == TT_KEY_LONG_PRESS && key == TT_KEY_CENTER) {
        if (TTInstanceOf<TTPopupLayer>().isDialogShown() || _leaving
            || _transferFocused || _books.empty() || _selected >= _books.size()) {
            return true;
        }
        confirmDelete();
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
    _transferFocused = false;
}

void TTBookshelfPage::formatSize(size_t index, char* out, size_t outSize) {
    const uint32_t size = _books[index].size;
    if (size >= TT_SHELF_SIZE_MB) {
        const uint32_t tenths = (uint32_t)((uint64_t)size * 10 / TT_SHELF_SIZE_MB);
        snprintf(out, outSize, "%u.%uM", (unsigned)(tenths / 10), (unsigned)(tenths % 10));
    } else {
        snprintf(out, outSize, "%uK", (unsigned)((size + TT_SHELF_SIZE_KB - 1) / TT_SHELF_SIZE_KB));
    }
}

void TTBookshelfPage::formatType(const String& name, char* out, size_t outSize) {
    const int dot = name.lastIndexOf('.');
    size_t n = 0;
    if (dot >= 0) {
        for (unsigned i = (unsigned)dot + 1; i < name.length() && n + 1 < outSize; i++) {
            char c = name[i];
            if (c >= 'a' && c <= 'z') {
                c = (char)(c - 'a' + 'A');
            }
            out[n++] = c;
        }
    }
    out[n] = '\0';
}

void TTBookshelfPage::formatProgress(size_t index, char* out, size_t outSize) {
    int16_t& progress = _progress[index];
    if (progress == TT_SHELF_PROGRESS_UNKNOWN) {
        uint32_t offset = 0;
        const TTBookEntry& book = _books[index];
        if (TTInstanceOf<TTBookStore>().loadProgress(book, offset) && book.size > 0) {
            progress = (int16_t)((uint64_t)offset * 100 / book.size);
            if (progress > 100) {
                progress = 100;
            }
        } else {
            progress = 0;
        }
    }
    snprintf(out, outSize, "%d%%", (int)progress);
}

void TTBookshelfPage::showCount(size_t count) {
    char text[TT_SHELF_COUNT_TEXT_MAX];
    const unsigned current = count == 0 ? 0 : (unsigned)(_selected + 1);
    snprintf(text, sizeof(text), "%u/%u", current, (unsigned)count);
    lv_label_set_text(_count, text);
    placeTransfer(count == 0);
}

void TTBookshelfPage::placeTransfer(bool empty) {
    if (_transfer == nullptr) {
        return;
    }
    TTFontManager& fonts = TTFontManager::instance();
    const int fontSize = empty ? TT_SHELF_TRANSFER_EMPTY_FONT_SIZE : TT_SHELF_TRANSFER_FONT_SIZE;
    lv_font_t* font = fonts.getFont(fontSize);
    if (font == nullptr) {
        font = fonts.getFont(TT_SHELF_TRANSFER_FONT_SIZE);
    }
    if (font != nullptr) {
        lv_obj_set_style_text_font(_transfer, font, 0);
    }
    if (empty) {
        lv_obj_set_style_pad_hor(_transfer, TT_SHELF_TRANSFER_EMPTY_PAD_X, 0);
        lv_obj_set_style_pad_top(_transfer, TT_SHELF_TRANSFER_EMPTY_PAD_TOP, 0);
        lv_obj_set_style_pad_bottom(_transfer, TT_SHELF_TRANSFER_EMPTY_PAD_BOTTOM, 0);
    } else {
        lv_obj_set_style_pad_hor(_transfer, TT_SHELF_TRANSFER_PAD_X, 0);
        lv_obj_set_style_pad_top(_transfer, TT_SHELF_TRANSFER_PAD_TOP, 0);
        lv_obj_set_style_pad_bottom(_transfer, TT_SHELF_TRANSFER_PAD_BOTTOM, 0);
    }
    applyTransferStyle();
    if (empty) {
        lv_obj_align(_transfer, LV_ALIGN_CENTER, 0, 0);
    } else {
        lv_obj_update_layout(_transfer);
        const int32_t height = lv_obj_get_height(_transfer);
        int32_t y = (TT_SHELF_TITLE_H - height) / 2;
        if (y < 0) {
            y = 0;
        }
        lv_obj_align(_transfer, LV_ALIGN_TOP_MID, 0, y + TT_SHELF_TRANSFER_TITLE_DY);
    }
}

void TTBookshelfPage::applyTransferStyle() {
    if (_transfer == nullptr) {
        return;
    }
    const bool filled = _transferFocused;
    lv_obj_set_style_bg_color(_transfer, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(_transfer, filled ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_color(_transfer, filled ? lv_color_white() : lv_color_black(), 0);
}

void TTBookshelfPage::showRows() {
    const size_t count = _books.size();
    if (count == 0) {
        for (int row = 0; row < TT_SHELF_ROWS; row++) {
            lv_obj_set_hidden(_rows[row], true);
        }
        _transferFocused = true;
        showCount(0);
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
        char sizeText[TT_SHELF_SIZE_TEXT_MAX];
        char typeText[TT_SHELF_TYPE_TEXT_MAX];
        char progressText[TT_SHELF_PROGRESS_TEXT_MAX];
        formatSize(index, sizeText, sizeof(sizeText));
        formatType(_books[index].name, typeText, sizeof(typeText));
        formatProgress(index, progressText, sizeof(progressText));
        const String shown = store.displayName(_books[index].name);
        char fitted[TT_SHELF_NAME_FIT_MAX];
        const lv_font_t* nameFont = lv_obj_get_style_text_font(_names[row], LV_PART_MAIN);
        const int32_t letterSpace = lv_obj_get_style_text_letter_space(_names[row], LV_PART_MAIN);
        ttTextFit(fitted, sizeof(fitted), shown.c_str(), nameFont, TT_SHELF_TEXT_W, letterSpace);
        lv_label_set_text(_names[row], fitted);
        lv_label_set_text(_sizes[row], sizeText);
        lv_label_set_text(_types[row], typeText);
        lv_label_set_text(_percents[row], progressText);
        lv_obj_set_hidden(_marks[row], _transferFocused || index != _selected);
        lv_obj_set_hidden(_rows[row], false);
    }
    showCount(count);
    LOG_I("Bookshelf: select %u/%u window=%u", (unsigned)(_selected + 1), (unsigned)count, (unsigned)window);
}

void TTBookshelfPage::openSelected() {
    if (_leaving) {
        return;
    }
    if (_books.empty() || _transferFocused) {
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

void TTBookshelfPage::confirmDelete() {
    const String name = _books[_selected].name;
    const String shown = TTInstanceOf<TTBookStore>().displayName(name);
    const char* src = shown.c_str();
    size_t len = strlen(src);
    char clipped[TT_SHELF_DELETE_NAME_MAX];
    if (len >= sizeof(clipped)) {
        len = sizeof(clipped) - 1;
        while (len > 0 && ((uint8_t)src[len] & 0xC0) == 0x80) {
            len--;
        }
    }
    memcpy(clipped, src, len);
    clipped[len] = '\0';
    char text[TT_SHELF_DELETE_TEXT_MAX];
    snprintf(text, sizeof(text), TT_SHELF_TEXT_DELETE_ASK, clipped);
    LOG_I("Bookshelf: confirm delete %s", name.c_str());
    TTInstanceOf<TTPopupLayer>().showDialog(
        text,
        [this, name]() { deleteBook(name); },
        []() {},
        TT_SHELF_TEXT_DELETE_YES,
        TT_SHELF_TEXT_DELETE_NO);
}

void TTBookshelfPage::deleteBook(const String& name) {
    if (_leaving) {
        return;
    }
    const size_t keep = _selected;
    if (!TTInstanceOf<TTBookStore>().removeBook(name)) {
        LOG_W("Bookshelf: delete failed %s", name.c_str());
        return;
    }
    LOG_I("Bookshelf: deleted %s", name.c_str());
    loadBooks();
    if (!_books.empty()) {
        _selected = keep < _books.size() ? keep : _books.size() - 1;
    }
    showRows();
    requestRefresh(TT_REFRESH_PARTIAL);
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
