#pragma once

#include "../Base/TTScreenPage.h"
#include "../Reader/TTBookStore.h"
#include <vector>

#define TT_SHELF_ROWS               5
#define TT_SHELF_TITLE_H            28
#define TT_SHELF_TITLE_FONT_SIZE    16
#define TT_SHELF_COUNT_FONT_SIZE    12
#define TT_SHELF_ROW_H              46
#define TT_SHELF_TOP                TT_SHELF_TITLE_H
#define TT_SHELF_PAD_X              10
#define TT_SHELF_ROW_PAD_X          8
#define TT_SHELF_MARK_W             12
#define TT_SHELF_MARK_ICON_GAP      4
#define TT_SHELF_FONT_SIZE          16
#define TT_SHELF_SIZE_FONT_SIZE     12
#define TT_SHELF_NAME_SIZE_GAP      6
#define TT_SHELF_ICON_H             (TT_SHELF_FONT_SIZE + TT_SHELF_NAME_SIZE_GAP + TT_SHELF_SIZE_FONT_SIZE)
#define TT_SHELF_CONTENT_Y          ((TT_SHELF_ROW_H - TT_SHELF_ICON_H) / 2)
#define TT_SHELF_ICON_W             24
#define TT_SHELF_ICON_FOLD          7
#define TT_SHELF_ICON_X             (TT_SHELF_MARK_W + TT_SHELF_MARK_ICON_GAP)
#define TT_SHELF_ICON_Y             TT_SHELF_CONTENT_Y
#define TT_SHELF_NAME_Y             TT_SHELF_CONTENT_Y
#define TT_SHELF_ICON_LINE_INSET    6
#define TT_SHELF_ICON_LINE_COUNT    2
#define TT_SHELF_ICON_TYPE_H        12
#define TT_SHELF_ICON_LINE_TOP      (TT_SHELF_ICON_FOLD + 2)
#define TT_SHELF_ICON_LINE_BOTTOM   (TT_SHELF_ICON_H - TT_SHELF_ICON_TYPE_H - 1)
#define TT_SHELF_TYPE_BOTTOM        2
#define TT_SHELF_TEXT_GAP           7
#define TT_SHELF_TYPE_FONT_SIZE     10
#define TT_SHELF_TRANSFER_FONT_SIZE 12
#define TT_SHELF_TRANSFER_PAD_X     8
#define TT_SHELF_TRANSFER_PAD_Y     1
#define TT_SHELF_TRANSFER_TEXT_DY   1
#define TT_SHELF_TRANSFER_PAD_TOP   (TT_SHELF_TRANSFER_PAD_Y + TT_SHELF_TRANSFER_TEXT_DY)
#define TT_SHELF_TRANSFER_PAD_BOTTOM (TT_SHELF_TRANSFER_PAD_Y - TT_SHELF_TRANSFER_TEXT_DY)
#define TT_SHELF_TRANSFER_BORDER    1
#define TT_SHELF_TRANSFER_TITLE_DY  (-2)
#define TT_SHELF_TRANSFER_EMPTY_FONT_SIZE 16
#define TT_SHELF_TRANSFER_EMPTY_PAD_X     20
#define TT_SHELF_TRANSFER_EMPTY_PAD_Y     10
#define TT_SHELF_TRANSFER_EMPTY_TEXT_DY   0
#define TT_SHELF_TRANSFER_EMPTY_PAD_TOP   (TT_SHELF_TRANSFER_EMPTY_PAD_Y + TT_SHELF_TRANSFER_EMPTY_TEXT_DY)
#define TT_SHELF_TRANSFER_EMPTY_PAD_BOTTOM (TT_SHELF_TRANSFER_EMPTY_PAD_Y - TT_SHELF_TRANSFER_EMPTY_TEXT_DY)
#define TT_SHELF_PROGRESS_W         48
#define TT_SHELF_PROGRESS_GAP       8
#define TT_SHELF_PROGRESS_FONT_SIZE 16
#define TT_SHELF_PROGRESS_Y         (TT_SHELF_CONTENT_Y + (TT_SHELF_ICON_H - TT_SHELF_PROGRESS_FONT_SIZE) / 2)
#define TT_SHELF_SIZE_TEXT_MAX      16
#define TT_SHELF_TYPE_TEXT_MAX      8
#define TT_SHELF_PROGRESS_TEXT_MAX  8
#define TT_SHELF_COUNT_TEXT_MAX     32
#define TT_SHELF_PROGRESS_UNKNOWN   -1
#define TT_SHELF_SIZE_KB            1024u
#define TT_SHELF_SIZE_MB            (1024u * 1024u)

#define TT_SHELF_TEXT_NO_CARD       "阅读功能需要 SD 卡\n请插入 SD 卡后再试"
#define TT_SHELF_TEXT_MOUNT_FAILED  "无法读取 SD 卡\n请使用 FAT32 格式的卡"
#define TT_SHELF_TEXT_CARD_REMOVED  "SD 卡已拔出"
#define TT_SHELF_TEXT_TITLE         "书架"
#define TT_SHELF_TEXT_TRANSFER      "WiFi传书"
#define TT_SHELF_TEXT_DELETE_ASK    "是否删除《%s》？"
#define TT_SHELF_TEXT_DELETE_YES    "是"
#define TT_SHELF_TEXT_DELETE_NO     "否"
#define TT_SHELF_DELETE_TEXT_MAX    160
#define TT_SHELF_DELETE_NAME_MAX    48

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
    void formatSize(size_t index, char* out, size_t outSize);
    void formatType(const String& name, char* out, size_t outSize);
    void formatProgress(size_t index, char* out, size_t outSize);
    void showCount(size_t count);
    void openSelected();
    void openTransfer();
    void confirmDelete();
    void deleteBook(const String& name);
    void placeTransfer(bool empty);
    void applyTransferStyle();
    void pollCard();
    void leaveWithMessage(const char* msg);
    void popDeferred();

    std::vector<TTBookEntry> _books;
    std::vector<int16_t> _progress;
    lv_obj_t* _rows[TT_SHELF_ROWS] = {};
    lv_obj_t* _marks[TT_SHELF_ROWS] = {};
    lv_obj_t* _icons[TT_SHELF_ROWS] = {};
    lv_obj_t* _types[TT_SHELF_ROWS] = {};
    lv_obj_t* _names[TT_SHELF_ROWS] = {};
    lv_obj_t* _sizes[TT_SHELF_ROWS] = {};
    lv_obj_t* _percents[TT_SHELF_ROWS] = {};
    lv_obj_t* _count = nullptr;
    lv_obj_t* _transfer = nullptr;
    lv_obj_t* _hit = nullptr;

    const char* _error = nullptr;
    size_t _selected = 0;
    bool _transferFocused = false;
    uint32_t _pollHandle = 0;
    bool _sdAcquired = false;
    bool _appeared = false;
    bool _leaving = false;
};
