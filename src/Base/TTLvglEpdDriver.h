#pragma once

#include <EPDConfig.h>
#include <lvgl.h>

class TTLvglEpdDriver {
public:

    bool begin(EPaperDisplay& display);
    void requestRefresh(bool fullRefresh = false);
    lv_display_t* getDisplay() { return _lvDisplay; }

private:
    static void flushCallback(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map);

    EPaperDisplay* _epd = nullptr;
    lv_display_t* _lvDisplay = nullptr;
    uint8_t _partialCount = 0;
    bool _needFullRefresh = true;
    bool _fullRefreshPending = false;
};
