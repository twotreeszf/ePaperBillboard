#pragma once

#include <GxEPD2_BW.h>
#include <lvgl.h>

// Display dimensions (landscape mode)
#define EPD_WIDTH   296
#define EPD_HEIGHT  128

// Buffer size for 1bpp: (width * height / 8) + 8 bytes for palette
#define EPD_BUF_SIZE ((EPD_WIDTH * EPD_HEIGHT / 8) + 8)

// Full refresh interval to prevent ghosting
#define EPD_FULL_REFRESH_INTERVAL 10

class TTLVGLDriver {
public:
    using EPaperDisplay = GxEPD2_BW<GxEPD2_290, GxEPD2_290::HEIGHT>;
    
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

extern TTLVGLDriver lvglDriver;
