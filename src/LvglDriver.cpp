#include "LvglDriver.h"
#include "Base/Logger.h"

// Global instance
LvglDriver lvglDriver;

// Static draw buffer - must be aligned for LVGL 9.x
static uint8_t _drawBuf[EPD_BUF_SIZE] __attribute__((aligned(4)));

static uint32_t lvglTickCallback() {
    return millis();
}

bool LvglDriver::begin(EPaperDisplay& display) {
    _epd = &display;
    
    // Initialize LVGL
    lv_init();
    
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

void LvglDriver::flushCallback(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
    LvglDriver* driver = (LvglDriver*)lv_display_get_user_data(disp);
    if (!driver || !driver->_epd) {
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
    
    LOG_I("Flush area: (%d,%d) - (%d,%d), size: %dx%d", x1, y1, x2, y2, w, h);
    
    // Set rotation: 3 = landscape 180° (upside down)
    driver->_epd->setRotation(3);

    bool doFullRefresh = driver->_needFullRefresh || 
                         (driver->_partialCount >= EPD_FULL_REFRESH_INTERVAL);
    
    if (doFullRefresh) {
        LOG_I("E-Paper full refresh");
        driver->_epd->setFullWindow();
        driver->_partialCount = 0;
        driver->_needFullRefresh = false;
    } else {
        LOG_I("E-Paper partial refresh at (%d,%d) %dx%d", x1, y1, w, h);
        driver->_epd->setPartialWindow(x1, y1, w, h);
        driver->_partialCount++;
    }
    
    // For PARTIAL render mode, buffer contains only the flush area
    // Stride is based on the area width, rounded up to bytes
    int32_t buf_stride = (w + 7) / 8;
    
    // Draw pixels using GxEPD2's paged drawing
    driver->_epd->firstPage();
    do {
        for (int32_t y = y1; y <= y2; y++) {
            for (int32_t x = x1; x <= x2; x++) {
                // Convert screen coordinates to buffer-relative coordinates
                int32_t rel_x = x - x1;
                int32_t rel_y = y - y1;
                
                // Calculate byte and bit index in the buffer
                int32_t byte_idx = rel_y * buf_stride + (rel_x / 8);
                int32_t bit_idx = 7 - (rel_x % 8);  // MSB first
                
                // Get pixel value from LVGL buffer
                bool isSet = (px_map[byte_idx] >> bit_idx) & 0x01;
                
                // LVGL I1: 1 = foreground, 0 = background
                // For E-Paper: invert - 1 should be white (paper), 0 should be black (ink)
                uint16_t color = isSet ? GxEPD_WHITE : GxEPD_BLACK;
                driver->_epd->drawPixel(x, y, color);
            }
        }
    } while (driver->_epd->nextPage());
    
    LOG_I("E-Paper flush complete");
    
    // Notify LVGL that flush is complete
    lv_display_flush_ready(disp);
}

void LvglDriver::requestRefresh(bool fullRefresh) {
    if (fullRefresh) {
        _needFullRefresh = true;
    }
    
    LOG_I("requestRefresh called, fullRefresh=%d", fullRefresh);
    
    // Invalidate the entire screen to mark it as dirty
    lv_obj_invalidate(lv_scr_act());
    
    // For LVGL 9.x with deleted refresh timer, use lv_refr_now() 
    // But we need to ensure the display is properly set
    lv_refr_now(_lvDisplay);
    
    LOG_I("requestRefresh done");
}
