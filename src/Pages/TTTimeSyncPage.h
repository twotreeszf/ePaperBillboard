#pragma once

#include "TTHomePage.h"
#include <array>

class TTTimeSyncPage : public TTScreenPage {
public:
    TTTimeSyncPage() : TTScreenPage("对时") {}

protected:
    void buildContent(lv_obj_t* screen) override;

private:
    std::array<MenuItem, 2> _items;
};
