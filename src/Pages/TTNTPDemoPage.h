#pragma once

#include "../Base/TTScreenPage.h"

class TTNTPDemoPage : public TTScreenPage {
public:
    TTNTPDemoPage() = default;

protected:
    void buildContent(lv_obj_t* screen) override;

private:
    static void onBackClicked(lv_event_t* e);
};
