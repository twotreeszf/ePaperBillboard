#pragma once

#include "../Base/TTScreenPage.h"
#include <array>
#include <functional>

#define TT_HOME_ICON_SIZE  48
#define TT_HOME_ICON_PAD   4
#define TT_HOME_ITEM_PAD_X  8
#define TT_HOME_ITEM_PAD_Y  6
#define TT_HOME_ITEM_W    84
#define TT_HOME_ITEM_H    86
#define TT_HOME_ITEMS_GAP 8
#define TT_HOME_ITEM_RADIUS 8
#define TT_HOME_INDICATOR_W 16
#define TT_HOME_INDICATOR_H 6
#define TT_HOME_INDICATOR_GAP  4
#define TT_HOME_INDICATOR_RADIUS  3
#define TT_HOME_ROW_VISIBLE  4
#define TT_HOME_ROW_W  (TT_HOME_ROW_VISIBLE * TT_HOME_ITEM_W + (TT_HOME_ROW_VISIBLE - 1) * TT_HOME_ITEMS_GAP)
#define TT_HOME_ROW_MARK_SIZE  24
#define TT_HOME_ROW_MARK_PAD   0

struct MenuItem {
    lv_obj_t* btn = nullptr;
    lv_obj_t* underline = nullptr;
    std::function<void()> onClick;

    static void create(MenuItem* item, lv_obj_t* parent, TTScreenPage* page, const char* iconPath, const char* labelText, lv_font_t* font, std::function<void()> onClick);
    static void onEntryClicked(lv_event_t* e);
    static void onFocusChanged(lv_event_t* e);
};

struct MenuRow {
    lv_obj_t* container = nullptr;
    lv_obj_t* prevMark = nullptr;
    lv_obj_t* nextMark = nullptr;
    MenuItem* items = nullptr;
    int count = 0;
    int first = 0;

    void create(lv_obj_t* screen);
    void bind(MenuItem* menuItems, int itemCount);
    void showItem(int index);
    static void onItemFocused(lv_event_t* e);
};

class TTHomePage : public TTScreenPage {
public:
    TTHomePage() : TTScreenPage("首页") {}

protected:
    void buildContent(lv_obj_t* screen) override;

private:
    MenuRow _row;
    std::array<MenuItem, 4> _items;
};
