#include "TTLvglEpdDriver.h"
#include "TTDrawBufPassthroughDecoder.h"
#include "TTInstance.h"
#include "Tasks/TTUITask.h"
#include "TTNavigationBar.h"
#include <EPDConfig.h>
#include "Logger.h"
#include <esp_heap_caps.h>

static uint8_t* _drawBuf = nullptr;

static uint32_t lvglTickCallback() {
    return millis();
}

static void invalidatePageContent() {
    lv_obj_t* scr = lv_scr_act();
    if (scr == nullptr) {
        return;
    }
    lv_area_t area;
    area.x1 = 0;
    area.y1 = 0;
    area.x2 = EPD_WIDTH - 1;
    area.y2 = EPD_HEIGHT - TT_NAV_PAGE_INSET - 1;
    lv_obj_invalidate_area(scr, &area);
}

static void invalidateTopLayerWidgets(lv_display_t* disp) {
    lv_obj_t* top = lv_display_get_layer_top(disp);
    if (top == nullptr) {
        return;
    }
    for (uint32_t i = 0; ; i++) {
        lv_obj_t* child = lv_obj_get_child(top, (int32_t)i);
        if (child == nullptr) {
            break;
        }
        if (!lv_obj_has_flag(child, LV_OBJ_FLAG_HIDDEN)) {
            lv_obj_invalidate(child);
        }
    }
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
    
    lv_display_set_color_format(_lvDisplay, LV_COLOR_FORMAT_I1);

    if (_drawBuf == nullptr) {
        LOG_I("LVGL alloc %u bytes, heap=%u largest=%u",
              (unsigned)EPD_BUF_SIZE, (unsigned)ESP.getFreeHeap(),
              (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        _drawBuf = (uint8_t*)heap_caps_aligned_alloc(4, EPD_BUF_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (_drawBuf == nullptr) {
        LOG_E("Failed to allocate LVGL draw buffer (%u bytes), heap=%u largest=%u",
              (unsigned)EPD_BUF_SIZE, (unsigned)ESP.getFreeHeap(),
              (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        return false;
    }

    lv_display_set_buffers(_lvDisplay, _drawBuf, nullptr, EPD_BUF_SIZE, LV_DISPLAY_RENDER_MODE_PARTIAL);
    
    // Set flush callback
    lv_display_set_flush_cb(_lvDisplay, _flushCallback);
    
    // Store 'this' pointer in user data for callback access
    lv_display_set_user_data(_lvDisplay, this);
    
    // Note: Keep the refresh timer active, but we'll only update when content changes
    // The timer is needed for lv_refr_now() to work properly
    
    LOG_I("LVGL display initialized: %dx%d, 1bpp", EPD_WIDTH, EPD_HEIGHT);
    return true;
}

void TTLvglEpdDriver::_flushCallback(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
    TTLvglEpdDriver* pThis = (TTLvglEpdDriver*)lv_display_get_user_data(disp);
    if (!pThis || !pThis->_epd) {
        LOG_E("Flush callback: invalid driver or display");
        lv_display_flush_ready(disp);
        return;
    }
    
    // Skip the 8-byte palette header for monochrome format
    px_map += 8;
    
    const int32_t src_x1 = area->x1;
    const int32_t src_y1 = area->y1;
    int32_t x1 = area->x1;
    int32_t y1 = area->y1;
    int32_t x2 = area->x2;
    int32_t y2 = area->y2;

    const int32_t navTop = EPD_HEIGHT - TT_NAV_PAGE_INSET;
    if (!pThis->_needDeepRefresh && !pThis->_flushingOverlay && y2 >= navTop) {
        pThis->_navTouched = true;
        if (y1 >= navTop) {
            LOG_I("Flush: skip page paint in nav bar region");
            lv_display_flush_ready(disp);
            return;
        }
        y2 = navTop - 1;
        LOG_I("Flush: clip page above nav bar y<%d", navTop);
    }

    int32_t w = x2 - x1 + 1;
    int32_t h = y2 - y1 + 1;
    if (w <= 0 || h <= 0) {
        lv_display_flush_ready(disp);
        return;
    }

    LOG_I("Flush area: (%d,%d)-(%d,%d), size %dx%d", x1, y1, x2, y2, w, h);
    uint32_t flushStart = millis();

    pThis->_epd->setRotation(EPD_ROTATION);
    const bool doDeepFull = pThis->_needDeepRefresh && !pThis->_flushingOverlay;
    if (doDeepFull) {
        pThis->_epd->setFullWindow();
        pThis->_needDeepRefresh = false;
        pThis->_partialCount = 0;
        pThis->_deepRefreshPending = false;
        LOG_I("E-Paper deep full refresh (with nav bar)");
    } else {
        pThis->_epd->setPartialWindow(x1, y1, (uint16_t)w, (uint16_t)h);
        if (!pThis->_needDeepRefresh) {
            pThis->_partialCount++;
        }
        LOG_I("E-Paper partial refresh at (%d,%d) %dx%d, partial count: %d", x1, y1, w, h, pThis->_partialCount);
    }

    const int32_t buf_stride = ((area->x2 - area->x1 + 1) + 7) / 8;

    pThis->_epd->firstPage();
    do {
        for (int32_t y = y1; y <= y2; y++) {
            for (int32_t x = x1; x <= x2; x++) {
                int32_t rel_x = x - src_x1;
                int32_t rel_y = y - src_y1;
                int32_t byte_idx = rel_y * buf_stride + (rel_x / 8);
                int32_t bit_idx = 7 - (rel_x % 8);
                bool isSet = (px_map[byte_idx] >> bit_idx) & 0x01;
                uint16_t color = isSet ? GxEPD_WHITE : GxEPD_BLACK;
                pThis->_epd->drawPixel(x, y, color);
            }
        }
    } while (pThis->_epd->nextPage());

    LOG_I("E-Paper flush complete in %u ms", (unsigned)(millis() - flushStart));

    lv_display_flush_ready(disp);

    if (pThis->_partialCount >= EPD_FULL_REFRESH_INTERVAL && !pThis->_deepRefreshPending) {
        pThis->_deepRefreshPending = true;
        TTInstanceOf<TTUITask>().requestDeepRefreshAsync();
    }
}

void TTLvglEpdDriver::requestRefresh(TTRefreshLevel level) {
    switch (level) {
        case TT_REFRESH_PARTIAL:
            _navTouched = false;
            lv_refr_now(_lvDisplay);
            if (_navTouched) {
                LOG_I("Flush: page overlapped nav, redraw overlay");
                _flushingOverlay = true;
                invalidateTopLayerWidgets(_lvDisplay);
                lv_refr_now(_lvDisplay);
                _flushingOverlay = false;
            }
            break;

        case TT_REFRESH_FULL:
            invalidatePageContent();
            lv_refr_now(_lvDisplay);
            _flushingOverlay = true;
            invalidateTopLayerWidgets(_lvDisplay);
            lv_refr_now(_lvDisplay);
            _flushingOverlay = false;
            break;

        case TT_REFRESH_DEEP:
            _needDeepRefresh = true;
            _partialCount = 0;
            invalidatePageContent();
            lv_refr_now(_lvDisplay);
            _flushingOverlay = true;
            invalidateTopLayerWidgets(_lvDisplay);
            lv_refr_now(_lvDisplay);
            _flushingOverlay = false;
            _needDeepRefresh = false;
            break;
    }
}
