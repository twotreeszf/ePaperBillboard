#pragma once

#include "TTHomePage.h"
#include <array>

#define TT_SETTINGS_MENU_ROW_W  (4 * TT_HOME_ITEM_W + 3 * TT_HOME_ITEMS_GAP)

class TTSettingsPage : public TTScreenPage {
public:
    TTSettingsPage() : TTScreenPage("设置") {}

protected:
    void buildContent(lv_obj_t* screen) override;

private:
    void onRebootClicked();

    std::array<MenuItem, 5> _items;
};
