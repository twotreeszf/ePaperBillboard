#pragma once

#include "TTHomePage.h"
#include <array>

class TTSettingsPage : public TTScreenPage {
public:
    TTSettingsPage() : TTScreenPage("设置") {}

protected:
    void buildContent(lv_obj_t* screen) override;

private:
    void onRebootClicked();

    MenuRow _row;
    std::array<MenuItem, 5> _items;
};
