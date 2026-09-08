#pragma once

#include "../Base/TTScreenPage.h"

class TTNTPDemoPage : public TTScreenPage {
public:
    TTNTPDemoPage() : TTScreenPage("对时") {}

protected:
    void buildContent(lv_obj_t* screen) override;
};
