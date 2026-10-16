#include "TTReaderPage.h"
#include "../Base/Logger.h"
#include "../Base/TTFontManager.h"
#include "../Base/TTInstance.h"
#include "../Base/TTLvglEpdDriver.h"
#include "../Base/TTNavigationBar.h"
#include "../Base/TTPopupLayer.h"
#include "../Reader/TTReaderTextView.h"
#include "../Service/TTSdCardService.h"
#include <EPDConfig.h>
#include <esp_heap_caps.h>
#include <cstdio>
#include <cstring>
#include <new>

#define TT_READER_TEXT_W  (EPD_WIDTH - 2 * TT_READER_MARGIN_X)
#define TT_READER_TEXT_H  (EPD_HEIGHT - TT_NAV_PAGE_INSET - TT_READER_MARGIN_TOP - TT_READER_FOOTER_H)

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
    lv_font_t* footerFont = fonts.getFont(TT_READER_FOOTER_FONT_SIZE);
    if (footerFont == nullptr) {
        footerFont = fonts.getFont(TT_READER_FONT_SIZE);
    }

    _textView = tt_reader_text_view_create(screen);
    lv_obj_set_pos(_textView, TT_READER_MARGIN_X, TT_READER_MARGIN_TOP);
    lv_obj_set_size(_textView, TT_READER_TEXT_W, TT_READER_TEXT_H);

    _footer = lv_label_create(screen);
    lv_obj_set_style_text_font(_footer, footerFont, 0);
    lv_obj_set_style_text_color(_footer, lv_color_black(), 0);
    lv_label_set_text(_footer, "");
    lv_obj_align(_footer, LV_ALIGN_BOTTOM_RIGHT, -TT_READER_MARGIN_X, -TT_READER_FOOTER_BOTTOM);

    createGroup();
}

void TTReaderPage::setup() {
    TTScreenPage::setup();
    _error = openBook();
    if (_error != nullptr) {
        LOG_E("Reader: open %s failed: %s", _book.name.c_str(), _error);
        closeBook();
    }
}

void TTReaderPage::willAppear() {
    TTScreenPage::willAppear();
    _visible = true;
    if (_error != nullptr) {
        const char* msg = _error;
        runOnce(0, [this, msg]() { leaveWithMessage(msg); });
        return;
    }
    TTInstanceOf<TTLvglEpdDriver>().setAutoDeepRefresh(false);
    _deepRefreshPaused = true;
    startIndexing();
    _pollHandle = runRepeat(TT_SD_DETECT_POLL_MS, [this]() { pollCard(); }, false);
}

void TTReaderPage::willDisappear() {
    _visible = false;
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
    saveProgress();
    closeBook();
    if (_deepRefreshPaused) {
        TTInstanceOf<TTLvglEpdDriver>().setAutoDeepRefresh(true);
        _deepRefreshPaused = false;
    }
    TTScreenPage::willDisappear();
}

void TTReaderPage::willDestroy() {
    closeBook();
    if (_sdAcquired) {
        TTInstanceOf<TTSdCardService>().release();
        _sdAcquired = false;
    }
    TTScreenPage::willDestroy();
}

bool TTReaderPage::handleKeyAction(TTKeyId key, TTKeyGesture gesture) {
    if (gesture == TT_KEY_CLICK && (key == TT_KEY_LEFT || key == TT_KEY_RIGHT)) {
        if (TTInstanceOf<TTPopupLayer>().isDialogShown()) {
            return false;
        }
        if (_error != nullptr || _leaving || _cardLost || !_page) {
            return true;
        }
        if (key == TT_KEY_RIGHT) {
            nextPage();
        } else {
            prevPage();
        }
        return true;
    }
    return TTScreenPage::handleKeyAction(key, gesture);
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
    if (!_layout.begin(TTFontManager::instance().getLoader(TT_READER_FONT_SIZE), TT_READER_TEXT_W, TT_READER_TEXT_H)) {
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
    tt_reader_text_view_set_page(_textView, &_layout, _page.get());
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
    tt_reader_text_view_set_page(_textView, &_layout, _page.get());
    updateFooter();
    lv_obj_invalidate(_content);

    TTRefreshLevel level = TT_REFRESH_PARTIAL;
    if (++_turns >= TT_READER_DEEP_REFRESH_TURNS) {
        _turns = 0;
        level = TT_REFRESH_DEEP;
    }
    requestRefresh(level);
    scheduleProgressSave();
    LOG_I("Reader: page %u..%u lines=%u index=%d layout %u ms refresh=%d",
          (unsigned)_page->start, (unsigned)_page->end, (unsigned)_page->lineCount, (int)_pageIndex,
          (unsigned)(millis() - startMs), (int)level);
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
    if (!previousStart(start)) {
        LOG_I("Reader: previous of %u unknown, index parsed=%u",
              (unsigned)_page->start, (unsigned)_index.parsedBytes());
        TTInstanceOf<TTPopupLayer>().showToast(TT_READER_TEXT_INDEXING);
        return;
    }
    turnTo(start);
}

bool TTReaderPage::previousStart(uint32_t& start) {
    const uint32_t current = _page->start;
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
    char text[TT_READER_FOOTER_TEXT_MAX];
    if (_pageIndex >= 0 && _index.isComplete()) {
        snprintf(text, sizeof(text), "%d / %u   %s",
                 (int)(_pageIndex + 1), (unsigned)_index.pageCount(), percent);
    } else if (_pageIndex >= 0) {
        snprintf(text, sizeof(text), "%d / ~%u   %s",
                 (int)(_pageIndex + 1), (unsigned)_index.estimatedPages(), percent);
    } else {
        snprintf(text, sizeof(text), "%s", percent);
    }
    lv_label_set_text(_footer, text);
}

void TTReaderPage::startIndexing() {
    if (_indexHandle != 0 || !_index.isOpen() || _index.isComplete()) {
        return;
    }
    _indexStartMs = millis();
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
    LOG_I("Reader: index stop pages=%u parsed=%u/%u complete=%d",
          (unsigned)_index.pageCount(), (unsigned)_index.parsedBytes(), (unsigned)_source.size(),
          _index.isComplete() ? 1 : 0);
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
    if (complete || (!wasKnown && _index.covers(_page->start))) {
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
