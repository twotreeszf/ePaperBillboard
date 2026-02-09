#include "TTLvglEpdDriver.h"
#include "TTDrawBufPassthroughDecoder.h"
#include <EPDConfig.h>
#include "Logger.h"

// Static draw buffer - must be aligned for LVGL 9.x
static uint8_t _drawBuf[EPD_BUF_SIZE] __attribute__((aligned(4)));

static uint32_t lvglTickCallback() {
    return millis();
}

bool TTLvglEpdDriver::begin(EPaperDisplay& display) {
    _epd = &display;

    // Initialize LVGL
    lv_init();

    TTDrawBufPassthroughDecoder_init();

    // Set tick callback for LVGL timing
    lv_tick_set_cb(lvglTickCallback);
    
    // Create display with landscape dimensions
    _lvDisplay = lv_display_create(EPD_WIDTH, EPD_HEIGHT);
    if (!_lvDisplay) {
        LOG_E("Failed to create LVGL display");
        return false;
    }
    
    // Set color format to 1-bit (monochrome)
    lv_display_set_color_format(_lvDisplay, LV_COLOR_FORMAT_I1);
    
    // Set draw buffer - single buffer mode is sufficient for E-Paper
    lv_display_set_buffers(_lvDisplay, _drawBuf, nullptr, sizeof(_drawBuf), LV_DISPLAY_RENDER_MODE_PARTIAL);
    
    // Set flush callback
    lv_display_set_flush_cb(_lvDisplay, flushCallback);
    
    // Store 'this' pointer in user data for callback access
    lv_display_set_user_data(_lvDisplay, this);
    
    // Note: Keep the refresh timer active, but we'll only update when content changes
    // The timer is needed for lv_refr_now() to work properly
    
    LOG_I("LVGL display initialized: %dx%d, 1bpp", EPD_WIDTH, EPD_HEIGHT);
    return true;
}

void TTLvglEpdDriver::flushCallback(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
    TTLvglEpdDriver* pThis = (TTLvglEpdDriver*)lv_display_get_user_data(disp);
    if (!pThis || !pThis->_epd) {
        LOG_E("Flush callback: invalid driver or display");
        lv_display_flush_ready(disp);
        return;
    }
    
    // Skip the 8-byte palette header for monochrome format
    px_map += 8;
    
    int32_t x1 = area->x1;
    int32_t y1 = area->y1;
    int32_t x2 = area->x2;
    int32_t y2 = area->y2;
    int32_t w = x2 - x1 + 1;
    int32_t h = y2 - y1 + 1;

    LOG_I("Flush area: (%d,%d)-(%d,%d), size %dx%d", x1, y1, x2, y2, w, h);

    pThis->_epd->setRotation(EPD_ROTATION);

    bool doFullRefresh = pThis->_needFullRefresh ||
                         (pThis->_partialCount >= EPD_FULL_REFRESH_INTERVAL);

    if (doFullRefresh) {
        LOG_I("E-Paper full refresh");
        pThis->_epd->setFullWindow();
        pThis->_partialCount = 0;
        pThis->_needFullRefresh = false;
    } else {
        LOG_I("E-Paper partial refresh at (%d,%d) %dx%d", x1, y1, w, h);
        pThis->_epd->setPartialWindow(x1, y1, (uint16_t)w, (uint16_t)h);
        pThis->_partialCount++;
    }

    int32_t buf_stride = (w + 7) / 8;

    pThis->_epd->firstPage();
    do {
        for (int32_t y = y1; y <= y2; y++) {
            for (int32_t x = x1; x <= x2; x++) {
                int32_t rel_x = x - x1;
                int32_t rel_y = y - y1;
                int32_t byte_idx = rel_y * buf_stride + (rel_x / 8);
                int32_t bit_idx = 7 - (rel_x % 8);
                bool isSet = (px_map[byte_idx] >> bit_idx) & 0x01;
                uint16_t color = isSet ? GxEPD_WHITE : GxEPD_BLACK;
                pThis->_epd->drawPixel(x, y, color);
            }
        }
    } while (pThis->_epd->nextPage());

    LOG_I("E-Paper flush complete");

    lv_display_flush_ready(disp);
}

void TTLvglEpdDriver::requestRefresh(bool fullRefresh) {
    if (fullRefresh) {
        _needFullRefresh = true;
    }
    lv_obj_invalidate(lv_scr_act());
    lv_refr_now(_lvDisplay);
}
