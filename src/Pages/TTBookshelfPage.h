#pragma once

#include "../Base/TTScreenPage.h"
#include "../Reader/TTBookStore.h"
#include <vector>

#define TT_SHELF_ROWS               8
#define TT_SHELF_ROW_H              30
#define TT_SHELF_TOP                6
#define TT_SHELF_PAD_X              10
#define TT_SHELF_ROW_PAD_X          8
#define TT_SHELF_INFO_W             64
#define TT_SHELF_INFO_GAP           8
#define TT_SHELF_FONT_SIZE          16
#define TT_SHELF_INFO_FONT_SIZE     12
#define TT_SHELF_ORDINAL_FONT_SIZE  10
#define TT_SHELF_ORDINAL_BOTTOM     4
#define TT_SHELF_INFO_TEXT_MAX      16
#define TT_SHELF_ORDINAL_TEXT_MAX   16
#define TT_SHELF_SIZE_KB            1024u
#define TT_SHELF_SIZE_MB            (1024u * 1024u)
#define TT_SHELF_PROGRESS_UNKNOWN   -2
#define TT_SHELF_PROGRESS_NONE      -1

#define TT_SHELF_TEXT_NO_CARD       "阅读功能需要 SD 卡\n请插入 SD 卡后再试"
#define TT_SHELF_TEXT_MOUNT_FAILED  "无法读取 SD 卡\n请使用 FAT32 格式的卡"
#define TT_SHELF_TEXT_CARD_REMOVED  "SD 卡已拔出"
#define TT_SHELF_TEXT_EMPTY         "书架是空的\n长按中键用手机或电脑传书"
#define TT_SHELF_TEXT_TRANSFER_HINT "长按中键：用手机或电脑传书"

class TTBookshelfPage : public TTScreenPage {
public:
    TTBookshelfPage() : TTScreenPage("阅读") {}

    void setup() override;
    void willAppear() override;
    void willDisappear() override;
    void willDestroy() override;
    bool handleKeyAction(TTKeyId key, TTKeyGesture gesture) override;

protected:
    void buildContent(lv_obj_t* screen) override;

private:
    void loadBooks();
    void showRows();
    void formatInfo(size_t index, char* out, size_t outSize);
    void openSelected();
    void openTransfer();
    void showEmptyHint();
    void pollCard();
    void leaveWithMessage(const char* msg);
    void popDeferred();

    std::vector<TTBookEntry> _books;
    std::vector<int16_t> _progress;
    lv_obj_t* _rows[TT_SHELF_ROWS] = {};
    lv_obj_t* _names[TT_SHELF_ROWS] = {};
    lv_obj_t* _infos[TT_SHELF_ROWS] = {};
    lv_obj_t* _ordinal = nullptr;
    lv_obj_t* _hit = nullptr;

    const char* _error = nullptr;
    size_t _selected = 0;
    uint32_t _pollHandle = 0;
    bool _sdAcquired = false;
    bool _appeared = false;
    bool _leaving = false;
};
