#pragma once

#include "../Base/TTScreenPage.h"

class TTWiFiDemoPage : public TTScreenPage {
public:
    TTWiFiDemoPage() : TTScreenPage("WiFi") {}

protected:
    void buildContent(lv_obj_t* screen) override;
};
