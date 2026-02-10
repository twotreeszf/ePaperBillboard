#pragma once

#include "../Base/TTScreenPage.h"
#include <array>
#include <functional>

#define TT_HOME_ICON_SIZE  32
#define TT_HOME_ICON_PAD   4
#define TT_HOME_ITEM_W    48
#define TT_HOME_ITEM_H    58
#define TT_HOME_ITEMS_GAP 6
#define TT_HOME_ITEM_RADIUS 8
#define TT_HOME_INDICATOR_W 16
#define TT_HOME_INDICATOR_H 1

class TTHomePage;

struct HomeItem {
    lv_obj_t* btn = nullptr;
    lv_obj_t* underline = nullptr;
    std::function<void()> onClick;

    static void create(HomeItem* item, lv_obj_t* parent, TTHomePage* page, const char* iconPath, const char* labelText, lv_font_t* font, std::function<void()> onClick);
    static void onEntryClicked(lv_event_t* e);
    static void onFocusChanged(lv_event_t* e);
};

class TTHomePage : public TTScreenPage {
public:
    TTHomePage() : TTScreenPage("Home") {}

protected:
    void buildContent(lv_obj_t* screen) override;
    void setup() override;

private:
    std::array<HomeItem, 3> _items;
};
