#pragma once

#include <GxEPD2_BW.h>
#include <GxEPD2_420_SSD1619.h>
#include <lvgl.h>

// Display dimensions (E042A13-A0 4.2" 400x300, SSD1619 with partial refresh)
#define EPD_WIDTH   400
#define EPD_HEIGHT  300

// Buffer size for 1bpp: (width * height / 8) + 8 bytes for palette
#define EPD_BUF_SIZE ((EPD_WIDTH * EPD_HEIGHT / 8) + 8)

// Full refresh interval to prevent ghosting
#define EPD_FULL_REFRESH_INTERVAL 10

class TTLvglEpdDriver {
public:
    using EPaperDisplay = GxEPD2_BW<GxEPD2_420_SSD1619, GxEPD2_420_SSD1619::HEIGHT>;

    bool begin(EPaperDisplay& display);
    void requestRefresh(bool fullRefresh = false);
    lv_display_t* getDisplay() { return _lvDisplay; }

private:
    static void flushCallback(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map);

    EPaperDisplay* _epd = nullptr;
    lv_display_t* _lvDisplay = nullptr;
    uint8_t _partialCount = 0;
    bool _needFullRefresh = true;
};
