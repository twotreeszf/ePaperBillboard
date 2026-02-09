#pragma once

#include "../Base/TTScreenPage.h"

#define TT_HOME_ICON_SIZE  32
#define TT_HOME_ICON_PAD   4
#define TT_HOME_ITEM_W    48
#define TT_HOME_ITEM_H    64
#define TT_HOME_ITEMS_GAP 8
#define TT_HOME_ITEM_RADIUS 8

class TTHomePage : public TTScreenPage {
public:
    TTHomePage() = default;

protected:
    void buildContent(lv_obj_t* screen) override;

private:
    static void onEntryClicked(lv_event_t* e);

    lv_obj_t* _btnWiFi = nullptr;
    lv_obj_t* _btnNTP = nullptr;
    lv_obj_t* _btnClock = nullptr;
};
