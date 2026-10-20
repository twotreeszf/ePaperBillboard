#include "TTReaderPage.h"
#include "../Base/Logger.h"
#include "../Base/TTFontManager.h"
#include "../Base/TTInstance.h"
#include "../Base/TTLvglEpdDriver.h"
#include "../Base/TTNavigationBar.h"
#include "../Base/TTPopupLayer.h"
#include "../Base/TTStreamImage.h"
#include "../Reader/TTReaderTextView.h"
#include "../Service/TTSdCardService.h"
#include "../Service/TTSleepService.h"
#include "../Tasks/TTUITask.h"
#include <EPDConfig.h>
#include <esp_heap_caps.h>
#include <cstdio>
#include <cstring>
#include <new>

#define TT_READER_TEXT_W  (EPD_WIDTH - 2 * TT_READER_MARGIN_X)

static_assert(TT_READER_IMAGE_PAGE_W == EPD_WIDTH, "image page width");
static_assert(TT_READER_IMAGE_PAGE_H == EPD_HEIGHT - TT_NAV_PAGE_INSET, "image page height");

static void readerTextFrame(const TTFontLoader* font, int32_t& top, int32_t& height) {
    const int32_t lineH = font != nullptr ? font->getLineHeight() : TT_READER_FONT_SIZE;
    top = TT_READER_MARGIN_TOP;
    height = TT_READER_PAGE_LINES * lineH + (TT_READER_PAGE_LINES - 1) * TT_READER_LINE_GAP;
    const int32_t bottom = (EPD_HEIGHT - TT_NAV_PAGE_INSET) - top - height;
    LOG_I("Reader frame: lines=%d lineH=%d gap=%d top=%d bottom=%d",
          TT_READER_PAGE_LINES, (int)lineH, TT_READER_LINE_GAP, (int)top, (int)bottom);
}

TTReaderPage::TTReaderPage(const TTBookEntry& book) : TTScreenPage(_title), _book(book) {
    const String name = TTInstanceOf<TTBookStore>().displayName(book.name);
    const char* src = name.c_str();
    size_t len = strlen(src);
    if (len >= sizeof(_title)) {
        len = sizeof(_title) - 1;
        while (len > 0 && ((uint8_t)src[len] & 0xC0) == 0x80) {
            len--;
        }
    }
    memcpy(_title, src, len);
    _title[len] = '\0';
}

void TTReaderPage::buildContent(lv_obj_t* screen) {
    _content = screen;
    TTFontManager& fonts = TTFontManager::instance();
    lv_font_t* footerFont = fonts.getFont(TT_NAV_STATUS_FONT);
    if (footerFont == nullptr) {
        footerFont = fonts.getFont(TT_READER_FONT_SIZE);
    }

    _textView = tt_reader_text_view_create(screen);
    lv_obj_set_pos(_textView, TT_READER_MARGIN_X, 0);
    lv_obj_set_size(_textView, TT_READER_TEXT_W, EPD_HEIGHT - TT_NAV_PAGE_INSET);

    _image = tt_stream_image_create(screen);
    tt_stream_image_set_storage(_image, &TTInstanceOf<TTSdCardService>().fs());
    lv_obj_set_pos(_image, 0, 0);
    lv_obj_set_hidden(_image, true);

    _imageNote = lv_label_create(screen);
    lv_obj_set_style_text_font(_imageNote, fonts.getFont(TT_READER_FONT_SIZE), 0);
    lv_obj_set_style_text_color(_imageNote, lv_color_black(), 0);
    lv_obj_set_style_text_align(_imageNote, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(_imageNote, TT_READER_IMAGE_PAGE_W);
    lv_label_set_text(_imageNote, TT_READER_TEXT_IMAGE);
    lv_obj_align(_imageNote, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_hidden(_imageNote, true);

    _footer = lv_label_create(screen);
    lv_obj_set_style_text_font(_footer, footerFont, 0);
    lv_obj_set_style_text_color(_footer, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(_footer, LV_OPA_TRANSP, 0);
    lv_label_set_text(_footer, "");
    lv_obj_set_hidden(_footer, true);

    _percent = lv_label_create(screen);
    lv_obj_set_style_text_font(_percent, footerFont, 0);
    lv_obj_set_style_text_color(_percent, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(_percent, LV_OPA_TRANSP, 0);
    lv_label_set_text(_percent, "");
    lv_obj_set_hidden(_percent, true);

    createGroup();
}

void TTReaderPage::setup() {
    TTScreenPage::setup();
    _error = openBook();
    if (_error != nullptr) {
        LOG_E("Reader: open %s failed: %s", _book.name.c_str(), _error);
        closeBook();
        return;
    }
    subscribe<TTSleepWakePayload>(TT_NOTIFICATION_SLEEP_WAKE, [this](const TTSleepWakePayload& wake) {
        onSleepWake(wake);
    });
    subscribe<TTTimeTickPayload>(TT_NOTIFICATION_TIME_TICK, [this](const TTTimeTickPayload&) {
        onTimeTick();
    });
}

void TTReaderPage::willAppear() {
    TTScreenPage::willAppear();
    _visible = true;
    if (_error != nullptr) {
        const char* msg = _error;
        runOnce(0, [this, msg]() { leaveWithMessage(msg); });
        return;
    }
    attachStatus();
    TTInstanceOf<TTUITask>().keypadInput().setDoubleClick(true);
    TTInstanceOf<TTLvglEpdDriver>().setAutoDeepRefresh(false);
    _deepRefreshPaused = true;
    startIndexing();
    _pollHandle = runRepeat(TT_SD_DETECT_POLL_MS, [this]() { pollCard(); }, false);
    armIdleSleep();
}

void TTReaderPage::willDisappear() {
    detachStatus();
    _visible = false;
    _sleepAfterTimeTick = false;
    cancelIdleSleep();
    stopIndexing();
    if (_pollHandle != 0) {
        cancelRepeat(_pollHandle);
        _pollHandle = 0;
    }
    if (_saveHandle != 0) {
        cancelRepeat(_saveHandle);
        _saveHandle = 0;
    }
    if (_leaving && TTInstanceOf<TTPopupLayer>().isDialogShown()) {
        TTInstanceOf<TTPopupLayer>().dismissDialog();
    }
    TTInstanceOf<TTUITask>().keypadInput().setDoubleClick(false);
    saveProgress();
    closeBook();
    if (_deepRefreshPaused) {
        TTInstanceOf<TTLvglEpdDriver>().setAutoDeepRefresh(true);
        _deepRefreshPaused = false;
    }
    TTScreenPage::willDisappear();
}

void TTReaderPage::willDestroy() {
    detachStatus();
    if (_footer != nullptr) {
        lv_obj_delete(_footer);
        _footer = nullptr;
    }
    if (_percent != nullptr) {
        lv_obj_delete(_percent);
        _percent = nullptr;
    }
    closeBook();
    if (_sdAcquired) {
        TTInstanceOf<TTSdCardService>().release();
        _sdAcquired = false;
    }
    TTScreenPage::willDestroy();
}

bool TTReaderPage::handleKeyAction(TTKeyId key, TTKeyGesture gesture) {
    cancelLightSleep();
    _sleepAfterTimeTick = false;
    cancelIdleSleep();
    bool handled = false;
    if ((gesture == TT_KEY_CLICK || gesture == TT_KEY_DOUBLE_CLICK)
        && (key == TT_KEY_LEFT || key == TT_KEY_RIGHT)) {
        if (TTInstanceOf<TTPopupLayer>().isDialogShown()) {
            return false;
        }
        if (_error == nullptr && !_leaving && !_cardLost && _page) {
            if (gesture == TT_KEY_DOUBLE_CLICK) {
                if (key == TT_KEY_RIGHT) {
                    skipForward();
                } else {
                    skipBack();
                }
            } else if (key == TT_KEY_RIGHT) {
                nextPage();
            } else {
                prevPage();
            }
        }
        handled = true;
    } else {
        handled = TTScreenPage::handleKeyAction(key, gesture);
    }
    armIdleSleep();
    return handled;
}

const char* TTReaderPage::openBook() {
    const uint32_t startMs = millis();
    TTSdCardService& sd = TTInstanceOf<TTSdCardService>();
    if (sd.acquire() != TT_SD_MOUNT_OK) {
        return TT_READER_TEXT_SD_FAILED;
    }
    _sdAcquired = true;

    _page.reset(new (std::nothrow) TTReaderPageLayout());
    _scratch.reset(new (std::nothrow) TTReaderPageLayout());
    if (!_page || !_scratch) {
        LOG_E("Reader: page buffers alloc failed, free=%u largest=%u",
              (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
              (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
        _page.reset();
        _scratch.reset();
        return TT_READER_TEXT_NO_MEMORY;
    }

    TTBookStore& store = TTInstanceOf<TTBookStore>();
    if (!_source.open(sd.fs(), store.bookPath(_book).c_str())) {
        return TT_READER_TEXT_OPEN_FAILED;
    }
    if (_source.size() == 0) {
        return TT_READER_TEXT_EMPTY;
    }
    if (!checkEncoding()) {
        return TT_READER_TEXT_ENCODING;
    }
    TTFontLoader* textFont = TTFontManager::instance().getLoader(TT_READER_FONT_SIZE);
    int32_t textTop = 0;
    int32_t textH = 0;
    readerTextFrame(textFont, textTop, textH);
    lv_obj_set_pos(_textView, TT_READER_MARGIN_X, textTop);
    lv_obj_set_size(_textView, TT_READER_TEXT_W, textH);
    const bool markdown = TTBookStore::isMarkdownName(_book.name.c_str());
    TTFontLoader* codeFont = markdown ? TTFontManager::instance().getLoader(TT_READER_CODE_FONT_SIZE) : nullptr;
    if (!_layout.begin(textFont, codeFont, TT_READER_TEXT_W, (int16_t)textH, markdown)) {
        return TT_READER_TEXT_OPEN_FAILED;
    }

    const uint32_t hash = _layout.configHash();
    if (!store.ensureBookCache(_book)
        || !_index.open(sd.fs(), store.indexPath(_book, hash).c_str(), hash, _source.size())) {
        LOG_W("Reader: index unavailable, back paging limited to history");
    }

    uint32_t offset = 0;
    store.loadProgress(_book, offset);
    if (_index.isOpen() && _index.covers(offset)) {
        const int32_t page = _index.findPage(offset);
        uint32_t start = 0;
        if (page >= 0 && _index.pageStart((uint32_t)page, start) && start != offset) {
            LOG_I("Reader: snap progress %u -> page %d start %u", (unsigned)offset, (int)page, (unsigned)start);
            offset = start;
        }
    }

    if (!_layout.layoutPage(_source, offset, *_page)) {
        return TT_READER_TEXT_READ_FAILED;
    }
    showLaidPage();
    updateFooter();
    store.touchRecent(_book.name);
    LOG_I("Reader: opened %s at %u/%u in %u ms, free=%u largest=%u",
          _book.name.c_str(), (unsigned)offset, (unsigned)_source.size(), (unsigned)(millis() - startMs),
          (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
          (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    return nullptr;
}

bool TTReaderPage::checkEncoding() {
    uint8_t b0 = 0;
    uint8_t b1 = 0;
    if (_source.size() >= 2 && _source.byteAt(0, b0) && _source.byteAt(1, b1)
        && ((b0 == TT_READER_UTF16_BOM_LE0 && b1 == TT_READER_UTF16_BOM_LE1)
            || (b0 == TT_READER_UTF16_BOM_LE1 && b1 == TT_READER_UTF16_BOM_LE0))) {
        LOG_W("Reader: UTF-16 BOM detected, unsupported");
        return false;
    }
    size_t probed = 0;
    const size_t invalid = _source.probeInvalidUtf8(probed);
    LOG_I("Reader: encoding probe %u bytes, %u invalid UTF-8", (unsigned)probed, (unsigned)invalid);
    return invalid <= TT_READER_ENCODING_MAX_INVALID;
}

void TTReaderPage::showLaidPage() {
    tt_reader_text_view_set_page(_textView, &_layout, _page.get());
    if (_page->image[0] == '\0') {
        tt_stream_image_set_src(_image, "");
        lv_obj_set_hidden(_image, true);
        lv_obj_set_hidden(_imageNote, true);
        lv_obj_set_hidden(_textView, false);
        return;
    }
    const String path = TTInstanceOf<TTBookStore>().cacheDir(_book) + "/" + _page->image;
    if (TTInstanceOf<TTSdCardService>().fs().exists(path)) {
        LOG_I("Reader: show image %s", path.c_str());
        tt_stream_image_set_src(_image, path.c_str());
        lv_obj_set_hidden(_textView, true);
        lv_obj_set_hidden(_imageNote, true);
        lv_obj_set_hidden(_image, false);
        return;
    }
    LOG_W("Reader: missing image %s", path.c_str());
    tt_stream_image_set_src(_image, "");
    lv_obj_set_hidden(_image, true);
    lv_obj_set_hidden(_textView, true);
    lv_obj_set_hidden(_imageNote, false);
}

void TTReaderPage::closeBook() {
    _index.close();
    _source.close();
}

bool TTReaderPage::turnTo(uint32_t offset) {
    const uint32_t startMs = millis();
    if (!_layout.layoutPage(_source, offset, *_page)) {
        LOG_E("Reader: layout at %u failed", (unsigned)offset);
        leaveWithMessage(TT_READER_TEXT_READ_FAILED);
        return false;
    }
    if (_page->missingGlyphs > 0) {
        LOG_W("Reader: page at %u has %u missing glyphs", (unsigned)offset, (unsigned)_page->missingGlyphs);
    }
    showLaidPage();
    updateFooter();
    lv_obj_invalidate(_content);

    requestRefresh(TT_REFRESH_PARTIAL);
    scheduleProgressSave();
    LOG_I("Reader: page %u..%u lines=%u index=%d layout %u ms refresh=%d",
          (unsigned)_page->start, (unsigned)_page->end, (unsigned)_page->lineCount, (int)_pageIndex,
          (unsigned)(millis() - startMs), (int)TT_REFRESH_PARTIAL);
    return true;
}

void TTReaderPage::nextPage() {
    if (_page->atEnd) {
        TTInstanceOf<TTPopupLayer>().showToast(TT_READER_TEXT_LAST_PAGE);
        return;
    }
    const uint32_t current = _page->start;
    if (turnTo(_page->end)) {
        pushHistory(current);
    }
}

void TTReaderPage::prevPage() {
    if (_page->start == 0) {
        TTInstanceOf<TTPopupLayer>().showToast(TT_READER_TEXT_FIRST_PAGE);
        return;
    }
    uint32_t start = 0;
    if (!previousStart(_page->start, start)) {
        LOG_I("Reader: previous of %u unknown, index parsed=%u",
              (unsigned)_page->start, (unsigned)_index.parsedBytes());
        TTInstanceOf<TTPopupLayer>().showToast(TT_READER_TEXT_INDEXING);
        return;
    }
    turnTo(start);
}

bool TTReaderPage::indexedSkip(int delta, uint32_t* departed, int& departedCount, uint32_t& landed) {
    departedCount = 0;
    const int32_t page = currentPageIndex();
    if (page < 0 || !_index.isOpen() || delta == 0) {
        return false;
    }
    int32_t target = page + delta;
    if (target < 0) {
        target = 0;
    }
    if ((uint32_t)target >= _index.pageCount()) {
        if (!_index.isComplete() || _index.pageCount() == 0) {
            return false;
        }
        target = (int32_t)_index.pageCount() - 1;
    }
    if (target == page) {
        return false;
    }
    if (delta > 0) {
        const int count = target - page;
        if (count > TT_READER_SKIP_PAGES) {
            return false;
        }
        for (int i = 0; i < count; i++) {
            if (!_index.pageStart((uint32_t)(page + i), departed[i])) {
                return false;
            }
        }
        departedCount = count;
    }
    return _index.pageStart((uint32_t)target, landed);
}

void TTReaderPage::skipForward() {
    if (_page->atEnd) {
        TTInstanceOf<TTPopupLayer>().showToast(TT_READER_TEXT_LAST_PAGE);
        return;
    }
    uint32_t departed[TT_READER_SKIP_PAGES];
    int departedCount = 0;
    uint32_t landed = 0;
    if (!indexedSkip(TT_READER_SKIP_PAGES, departed, departedCount, landed)) {
        uint32_t cursor = _page->start;
        uint32_t next = _page->end;
        departedCount = 0;
        while (departedCount < TT_READER_SKIP_PAGES && next < _source.size()) {
            if (!_layout.layoutPage(_source, next, *_scratch)) {
                leaveWithMessage(TT_READER_TEXT_READ_FAILED);
                return;
            }
            if (_scratch->start == cursor) {
                break;
            }
            departed[departedCount] = cursor;
            cursor = _scratch->start;
            next = _scratch->atEnd ? _source.size() : _scratch->end;
            departedCount++;
            if (_scratch->atEnd) {
                break;
            }
        }
        landed = cursor;
    }
    if (departedCount == 0 || landed == _page->start) {
        TTInstanceOf<TTPopupLayer>().showToast(TT_READER_TEXT_LAST_PAGE);
        return;
    }
    LOG_I("Reader: skip +%d %u -> %u", departedCount, (unsigned)_page->start, (unsigned)landed);
    for (int i = 0; i < departedCount; i++) {
        pushHistory(departed[i]);
    }
    turnTo(landed);
}

void TTReaderPage::skipBack() {
    if (_page->start == 0) {
        TTInstanceOf<TTPopupLayer>().showToast(TT_READER_TEXT_FIRST_PAGE);
        return;
    }
    uint32_t unused[TT_READER_SKIP_PAGES];
    int unusedCount = 0;
    uint32_t landed = 0;
    int moved = 0;
    if (indexedSkip(-TT_READER_SKIP_PAGES, unused, unusedCount, landed)) {
        const int32_t from = currentPageIndex();
        const int32_t to = _index.findPage(landed);
        moved = (from >= 0 && to >= 0 && from > to) ? (from - to) : 1;
    } else {
        uint32_t cursor = _page->start;
        while (moved < TT_READER_SKIP_PAGES && cursor > 0) {
            uint32_t prev = 0;
            if (!previousStart(cursor, prev)) {
                break;
            }
            landed = prev;
            cursor = prev;
            moved++;
        }
    }
    if (moved == 0) {
        LOG_I("Reader: skip back of %u unknown, index parsed=%u",
              (unsigned)_page->start, (unsigned)_index.parsedBytes());
        TTInstanceOf<TTPopupLayer>().showToast(TT_READER_TEXT_INDEXING);
        return;
    }
    while (_historyCount > 0 && _history[_historyCount - 1] >= landed) {
        _historyCount--;
    }
    LOG_I("Reader: skip -%d %u -> %u", moved, (unsigned)_page->start, (unsigned)landed);
    turnTo(landed);
}

bool TTReaderPage::previousStart(uint32_t current, uint32_t& start) {
    if (_index.isOpen() && _index.covers(current - 1)) {
        const int32_t page = _index.findPage(current - 1);
        if (page >= 0 && _index.pageStart((uint32_t)page, start)) {
            if (_historyCount > 0 && _history[_historyCount - 1] == start) {
                _historyCount--;
            }
            return true;
        }
    }
    if (_historyCount > 0) {
        start = _history[--_historyCount];
        return true;
    }
    return false;
}

void TTReaderPage::pushHistory(uint32_t start) {
    if (_historyCount >= TT_READER_HISTORY_MAX) {
        memmove(_history, _history + 1, (TT_READER_HISTORY_MAX - 1) * sizeof(_history[0]));
        _historyCount = TT_READER_HISTORY_MAX - 1;
    }
    _history[_historyCount++] = start;
}

int32_t TTReaderPage::currentPageIndex() {
    if (!_index.isOpen() || !_index.covers(_page->start)) {
        return -1;
    }
    const int32_t page = _index.findPage(_page->start);
    uint32_t start = 0;
    if (page < 0 || !_index.pageStart((uint32_t)page, start) || start != _page->start) {
        return -1;
    }
    return page;
}

void TTReaderPage::updateFooter() {
    if (_footer == nullptr || !_page) {
        return;
    }
    const uint32_t size = _source.size();
    const uint32_t permille = (_page->atEnd || size == 0)
        ? 1000 : (uint32_t)((uint64_t)_page->start * 1000 / size);
    char percent[16];
    snprintf(percent, sizeof(percent), "%u.%u%%", (unsigned)(permille / 10), (unsigned)(permille % 10));

    _pageIndex = currentPageIndex();
    const bool paging = _index.isOpen() && !_index.isComplete();
    char pages[TT_READER_FOOTER_TEXT_MAX];
    pages[0] = '\0';
    if (_pageIndex >= 0 && _index.isComplete()) {
        snprintf(pages, sizeof(pages), "%d / %u",
                 (int)(_pageIndex + 1), (unsigned)_index.pageCount());
    } else if (_pageIndex >= 0 && paging) {
        char dots[TT_READER_PAGING_DOTS_MAX + 1];
        uint8_t count = _pagingDots;
        if (count < 1 || count > TT_READER_PAGING_DOTS_MAX) {
            count = 1;
        }
        for (uint8_t i = 0; i < count; i++) {
            dots[i] = '.';
        }
        dots[count] = '\0';
        snprintf(pages, sizeof(pages), "%d / %u%s",
                 (int)(_pageIndex + 1), (unsigned)_index.estimatedPages(), dots);
    } else if (_pageIndex >= 0) {
        snprintf(pages, sizeof(pages), "%d / %u",
                 (int)(_pageIndex + 1), (unsigned)_index.estimatedPages());
    }
    if (_footer != nullptr) {
        lv_label_set_text(_footer, pages);
    }
    if (_percent != nullptr) {
        lv_label_set_text(_percent, percent);
    }
    ITTNavigationController* nav = getNavigationController();
    TTNavigationBar* bar = nav != nullptr ? nav->getNavBar() : nullptr;
    if (bar != nullptr && bar->trailingContainer() == _percent) {
        bar->setCenterContainer(pages[0] == '\0' ? nullptr : _footer);
        bar->setTrailingContainer(_percent);
    }
}

void TTReaderPage::attachStatus() {
    ITTNavigationController* nav = getNavigationController();
    TTNavigationBar* bar = nav != nullptr ? nav->getNavBar() : nullptr;
    if (bar == nullptr || _footer == nullptr || _percent == nullptr) {
        return;
    }
    bar->setSensorsVisible(false);
    bar->setTrailingContainer(_percent);
    bar->setCenterContainer(_footer);
    updateFooter();
    LOG_I("Reader: progress attached to status bar");
}

void TTReaderPage::detachStatus() {
    ITTNavigationController* nav = getNavigationController();
    TTNavigationBar* bar = nav != nullptr ? nav->getNavBar() : nullptr;
    if (bar == nullptr) {
        return;
    }
    if (bar->centerContainer() == _footer) {
        bar->setCenterContainer(nullptr);
    }
    if (bar->trailingContainer() == _percent) {
        bar->setTrailingContainer(nullptr);
    }
    bar->setSensorsVisible(true);
}

void TTReaderPage::startIndexing() {
    if (_indexHandle != 0 || !_index.isOpen() || _index.isComplete()) {
        return;
    }
    _indexStartMs = millis();
    _pagingDots = 1;
    _pagingDotMs = _indexStartMs;
    LOG_I("Reader: index start pages=%u parsed=%u/%u",
          (unsigned)_index.pageCount(), (unsigned)_index.parsedBytes(), (unsigned)_source.size());
    _indexHandle = runRepeat(TT_READER_INDEX_TICK_MS, [this]() { indexStep(); }, false);
}

void TTReaderPage::stopIndexing() {
    if (_indexHandle == 0) {
        return;
    }
    cancelRepeat(_indexHandle);
    _indexHandle = 0;
    _index.sync();
    const bool complete = _index.isComplete();
    LOG_I("Reader: index stop pages=%u parsed=%u/%u complete=%d",
          (unsigned)_index.pageCount(), (unsigned)_index.parsedBytes(), (unsigned)_source.size(),
          complete ? 1 : 0);
    if (!complete && _visible && !_leaving) {
        updateFooter();
        requestRefresh(TT_REFRESH_PARTIAL);
    }
}

void TTReaderPage::indexStep() {
    if (!_index.isOpen() || _index.isComplete() || _cardLost || _leaving) {
        stopIndexing();
        return;
    }
    const uint32_t startMs = millis();
    const bool wasKnown = _pageIndex >= 0;
    uint8_t pages = 0;
    while (pages < TT_READER_INDEX_BATCH_PAGES && millis() - startMs < TT_READER_INDEX_BUDGET_MS) {
        const uint32_t start = _index.parsedBytes();
        if (!_layout.layoutPage(_source, start, *_scratch)) {
            LOG_E("Reader: index layout at %u failed", (unsigned)start);
            stopIndexing();
            return;
        }
        if (_scratch->end <= start && !_scratch->atEnd) {
            LOG_E("Reader: index stuck at %u", (unsigned)start);
            stopIndexing();
            return;
        }
        if (!_index.append(start, _scratch->end, _scratch->atEnd)) {
            stopIndexing();
            return;
        }
        pages++;
        if (_index.pageCount() % TT_READER_INDEX_LOG_EVERY == 0) {
            LOG_I("Reader: index %u pages, %u/%u bytes, %u ms",
                  (unsigned)_index.pageCount(), (unsigned)_index.parsedBytes(), (unsigned)_source.size(),
                  (unsigned)(millis() - _indexStartMs));
        }
        if (_scratch->atEnd) {
            break;
        }
    }
    _index.sync();

    const bool complete = _index.isComplete();
    if (complete) {
        LOG_I("Reader: index complete %u pages in %u ms",
              (unsigned)_index.pageCount(), (unsigned)(millis() - _indexStartMs));
        stopIndexing();
    }
    const bool pageKnown = !wasKnown && _index.covers(_page->start);
    if (pageKnown && !complete) {
        _pagingDots = 1;
        _pagingDotMs = millis();
    }
    bool refreshFooter = complete || pageKnown;
    if (!complete && wasKnown && millis() - _pagingDotMs >= TT_READER_PAGING_DOT_MS) {
        _pagingDots = (uint8_t)(_pagingDots % TT_READER_PAGING_DOTS_MAX) + 1;
        _pagingDotMs = millis();
        refreshFooter = true;
    }
    if (refreshFooter) {
        updateFooter();
        if (_visible) {
            requestRefresh(TT_REFRESH_PARTIAL);
        }
    }
}

void TTReaderPage::scheduleProgressSave() {
    _progressDirty = true;
    if (_saveHandle != 0) {
        cancelRepeat(_saveHandle);
    }
    _saveHandle = runOnce(TT_READER_PROGRESS_SAVE_MS, [this]() {
        _saveHandle = 0;
        saveProgress();
    });
}

void TTReaderPage::saveProgress() {
    if (!_progressDirty || _cardLost || !_page) {
        return;
    }
    if (TTInstanceOf<TTBookStore>().saveProgress(_book, _page->start)) {
        _progressDirty = false;
    }
}

void TTReaderPage::pollCard() {
    if (_cardLost || TTInstanceOf<TTSdCardService>().isCardInserted()) {
        return;
    }
    LOG_W("Reader: card removed while reading %s", _book.name.c_str());
    _cardLost = true;
    stopIndexing();
    closeBook();
    TTInstanceOf<TTSdCardService>().handleCardRemoved();
    leaveWithMessage(TT_READER_TEXT_CARD_REMOVED);
}

void TTReaderPage::leaveWithMessage(const char* msg) {
    if (_leaving) {
        return;
    }
    _leaving = true;
    LOG_W("Reader: leave with message: %s", msg);
    cancelLightSleep();
    cancelIdleSleep();
    stopIndexing();
    if (_pollHandle != 0) {
        cancelRepeat(_pollHandle);
        _pollHandle = 0;
    }
    if (_saveHandle != 0) {
        cancelRepeat(_saveHandle);
        _saveHandle = 0;
    }
    saveProgress();
    TTInstanceOf<TTPopupLayer>().showConfirm(msg, [this]() {
        runOnce(0, [this]() {
            ITTNavigationController* nav = getNavigationController();
            if (nav != nullptr && nav->canPop()) {
                nav->pop();
            }
        });
    });
}

void TTReaderPage::armIdleSleep() {
    cancelIdleSleep();
    if (!_visible || _leaving || _cardLost || _error != nullptr) {
        return;
    }
    _idleSleepHandle = runOnce(TT_SLEEP_INPUT_IDLE_MS, [this]() {
        _idleSleepHandle = 0;
        enterIdleSleep();
    });
    LOG_I("Reader: sleep in %d s if idle", TT_SLEEP_INPUT_IDLE_MS / 1000);
}

void TTReaderPage::cancelIdleSleep() {
    if (_idleSleepHandle == 0) {
        return;
    }
    cancelRepeat(_idleSleepHandle);
    _idleSleepHandle = 0;
}

void TTReaderPage::enterIdleSleep() {
    if (!_visible || _leaving || _cardLost || _error != nullptr) {
        return;
    }
    if (_indexHandle != 0) {
        LOG_I("Reader: indexing, delay sleep");
        armIdleSleep();
        return;
    }
    if (_saveHandle != 0) {
        cancelRepeat(_saveHandle);
        _saveHandle = 0;
    }
    saveProgress();
    LOG_I("Reader: request light sleep");
    requestLightSleep();
}

void TTReaderPage::onSleepWake(const TTSleepWakePayload& wake) {
    if (!_visible || _leaving) {
        return;
    }
    LOG_I("Reader: sleep wake reason=%d", (int)wake.reason);
    switch (wake.reason) {
        case TT_SLEEP_WAKE_FETCH:
            cancelIdleSleep();
            enterIdleSleep();
            break;
        case TT_SLEEP_WAKE_TIME:
            _sleepAfterTimeTick = true;
            armIdleSleep();
            break;
        case TT_SLEEP_WAKE_INPUT:
        case TT_SLEEP_WAKE_POWER:
            _sleepAfterTimeTick = false;
            armIdleSleep();
            break;
    }
}

void TTReaderPage::onTimeTick() {
    if (!_visible || !_sleepAfterTimeTick) {
        return;
    }
    _sleepAfterTimeTick = false;
    LOG_I("Reader: time tick after wake, sleep again");
    cancelIdleSleep();
    enterIdleSleep();
}
