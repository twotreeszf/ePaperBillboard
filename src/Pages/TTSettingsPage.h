#pragma once

#include "TTHomePage.h"
#include <array>

#define TT_SETTINGS_ITEM_COUNT  6

class TTSettingsPage : public TTScreenPage {
public:
    TTSettingsPage() : TTScreenPage("设置") {}

protected:
    void buildContent(lv_obj_t* screen) override;

private:
    void onRebootClicked();

    MenuRow _row;
    std::array<MenuItem, TT_SETTINGS_ITEM_COUNT> _items;
};
