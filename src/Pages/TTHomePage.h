#pragma once

#include "../Base/TTScreenPage.h"
#include <array>
#include <functional>

#define TT_HOME_ICON_SIZE  48
#define TT_HOME_ICON_PAD   4
#define TT_HOME_ITEM_W    84
#define TT_HOME_ITEM_H    86
#define TT_HOME_ITEMS_GAP 8
#define TT_HOME_ITEM_RADIUS 8
#define TT_HOME_INDICATOR_W 16
#define TT_HOME_INDICATOR_H 6
#define TT_HOME_INDICATOR_RADIUS  3

struct MenuItem {
    lv_obj_t* btn = nullptr;
    lv_obj_t* underline = nullptr;
    std::function<void()> onClick;

    static void create(MenuItem* item, lv_obj_t* parent, TTScreenPage* page, const char* iconPath, const char* labelText, lv_font_t* font, std::function<void()> onClick);
    static void onEntryClicked(lv_event_t* e);
    static void onFocusChanged(lv_event_t* e);
};

class TTHomePage : public TTScreenPage {
public:
    TTHomePage() : TTScreenPage("首页") {}

protected:
    void buildContent(lv_obj_t* screen) override;

private:
    std::array<MenuItem, 2> _items;
};
