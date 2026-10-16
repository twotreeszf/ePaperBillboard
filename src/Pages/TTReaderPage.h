#pragma once

#include "../Base/TTScreenPage.h"
#include "../Models/TTNotificationPayloads.h"
#include "../Reader/TTBookStore.h"
#include "../Reader/TTReaderIndex.h"
#include "../Reader/TTReaderLayout.h"
#include "../Reader/TTReaderSource.h"
#include <memory>

#define TT_READER_FONT_SIZE              16
#define TT_READER_FOOTER_FONT_SIZE       10
#define TT_READER_MARGIN_X               14
#define TT_READER_MARGIN_TOP             10
#define TT_READER_FOOTER_H               16
#define TT_READER_FOOTER_BOTTOM          2
#define TT_READER_TITLE_MAX              64
#define TT_READER_FOOTER_TEXT_MAX        48
#define TT_READER_HISTORY_MAX            32
#define TT_READER_DEEP_REFRESH_TURNS     20
#define TT_READER_INDEX_TICK_MS          30
#define TT_READER_INDEX_BATCH_PAGES      5
#define TT_READER_INDEX_BUDGET_MS        40
#define TT_READER_INDEX_LOG_EVERY        100
#define TT_READER_PROGRESS_SAVE_MS       3000
#define TT_READER_ENCODING_MAX_INVALID   4
#define TT_READER_UTF16_BOM_LE0          0xFF
#define TT_READER_UTF16_BOM_LE1          0xFE

#define TT_READER_TEXT_FIRST_PAGE        "已经是第一页"
#define TT_READER_TEXT_LAST_PAGE         "已经是最后一页"
#define TT_READER_TEXT_INDEXING          "正在建立分页索引，请稍后再试"
#define TT_READER_TEXT_SD_FAILED         "无法读取 SD 卡"
#define TT_READER_TEXT_NO_MEMORY         "内存不足，无法打开这本书"
#define TT_READER_TEXT_OPEN_FAILED       "无法打开这本书"
#define TT_READER_TEXT_EMPTY             "这本书没有内容"
#define TT_READER_TEXT_ENCODING          "暂不支持该编码\n请转换为 UTF-8 格式"
#define TT_READER_TEXT_READ_FAILED       "读取书籍失败"
#define TT_READER_TEXT_CARD_REMOVED      "SD 卡已拔出"

class TTReaderPage : public TTScreenPage {
public:
    explicit TTReaderPage(const TTBookEntry& book);

    void setup() override;
    void willAppear() override;
    void willDisappear() override;
    void willDestroy() override;
    bool handleKeyAction(TTKeyId key, TTKeyGesture gesture) override;

protected:
    void buildContent(lv_obj_t* screen) override;

private:
    const char* openBook();
    bool checkEncoding();
    void closeBook();
    bool turnTo(uint32_t offset);
    void nextPage();
    void prevPage();
    bool previousStart(uint32_t& start);
    void pushHistory(uint32_t start);
    int32_t currentPageIndex();
    void updateFooter();
    void startIndexing();
    void stopIndexing();
    void indexStep();
    void scheduleProgressSave();
    void saveProgress();
    void pollCard();
    void leaveWithMessage(const char* msg);
    void armIdleSleep();
    void cancelIdleSleep();
    void enterIdleSleep();
    void onSleepWake(const TTSleepWakePayload& wake);
    void onTimeTick();

    TTBookEntry _book;
    char _title[TT_READER_TITLE_MAX] = {};
    TTReaderSource _source;
    TTReaderLayout _layout;
    TTReaderIndex _index;
    std::unique_ptr<TTReaderPageLayout> _page;
    std::unique_ptr<TTReaderPageLayout> _scratch;
    uint32_t _history[TT_READER_HISTORY_MAX] = {};
    uint8_t _historyCount = 0;

    lv_obj_t* _content = nullptr;
    lv_obj_t* _textView = nullptr;
    lv_obj_t* _footer = nullptr;

    const char* _error = nullptr;
    uint32_t _indexHandle = 0;
    uint32_t _pollHandle = 0;
    uint32_t _saveHandle = 0;
    uint32_t _idleSleepHandle = 0;
    uint32_t _indexStartMs = 0;
    uint16_t _turns = 0;
    int32_t _pageIndex = -1;
    bool _sdAcquired = false;
    bool _visible = false;
    bool _deepRefreshPaused = false;
    bool _progressDirty = false;
    bool _cardLost = false;
    bool _leaving = false;
    bool _sleepAfterTimeTick = false;
};
