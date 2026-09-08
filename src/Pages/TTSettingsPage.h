#pragma once

#include "TTHomePage.h"
#include <array>

class TTSettingsPage : public TTScreenPage {
public:
    TTSettingsPage() : TTScreenPage("Settings") {}

protected:
    void buildContent(lv_obj_t* screen) override;

private:
    std::array<MenuItem, 2> _items;
};
