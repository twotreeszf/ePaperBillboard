#include "TTLvglEpdDriver.h"
#include "TTScreenshotDisplay.h"
#include "TTDrawBufPassthroughDecoder.h"
#include "TTInstance.h"
#include "Tasks/TTUITask.h"
#include "TTNavigationBar.h"
#include <EPDConfig.h>
#include "Logger.h"
#include "Util.h"
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
        if (!lv_obj_is_hidden(child)) {
            lv_obj_invalidate(child);
        }
    }
}

bool TTLvglEpdDriver::begin(EPaperDisplay& display) {
    _epd = &display;
    _epd->setRotation(EPD_ROTATION);
    _epd->setFullWindow();
    _epd->fillScreen(GxEPD_WHITE);

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
              (unsigned)EPD_BUF_SIZE, (unsigned)Util::heapFree(), (unsigned)Util::heapLargest());
        _drawBuf = (uint8_t*)heap_caps_aligned_alloc(4, EPD_BUF_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (_drawBuf == nullptr) {
        LOG_E("Failed to allocate LVGL draw buffer (%u bytes), heap=%u largest=%u",
              (unsigned)EPD_BUF_SIZE, (unsigned)Util::heapFree(), (unsigned)Util::heapLargest());
        return false;
    }

    lv_display_set_buffers(_lvDisplay, _drawBuf, nullptr, EPD_BUF_SIZE, LV_DISPLAY_RENDER_MODE_PARTIAL);
    
    // Set flush callback
    lv_display_set_flush_cb(_lvDisplay, _flushCallback);
    
    // Store 'this' pointer in user data for callback access
    lv_display_set_user_data(_lvDisplay, this);
    
    // Note: Keep the refresh timer active, but we'll only update when content changes
    // The timer is needed for lv_refr_now() to work properly
    
    LOG_I("LVGL display initialized: %dx%d, 1bpp, draw buf %u bytes (%d rows)",
          EPD_WIDTH, EPD_HEIGHT, (unsigned)EPD_BUF_SIZE, EPD_DRAW_BUF_ROWS);
    return true;
}

void TTLvglEpdDriver::_flushCallback(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
    TTLvglEpdDriver* pThis = (TTLvglEpdDriver*)lv_display_get_user_data(disp);
    if (!pThis || !pThis->_epd) {
        LOG_E("Flush callback: invalid driver or display");
        lv_display_flush_ready(disp);
        return;
    }

    if (pThis->_capture != nullptr) {
        pThis->_capture->blit(area, px_map + EPD_I1_PALETTE_SIZE);
        lv_display_flush_ready(disp);
        return;
    }

    px_map += EPD_I1_PALETTE_SIZE;
    const bool isLast = lv_display_flush_is_last(disp);

    const int32_t src_x1 = area->x1;
    const int32_t src_y1 = area->y1;
    int32_t x1 = area->x1;
    int32_t y1 = area->y1;
    int32_t x2 = area->x2;
    int32_t y2 = area->y2;

    const int32_t navTop = EPD_HEIGHT - TT_NAV_PAGE_INSET;
    bool paint = true;
    if (!pThis->_flushingOverlay && y1 >= navTop) {
        if (!pThis->_needDeepRefresh) {
            pThis->_navTouched = true;
        } else {
            LOG_I("Flush: defer nav until page deep refresh");
        }
        LOG_I("Flush: skip page paint in nav bar region");
        paint = false;
    }
    if (paint && !pThis->_needDeepRefresh && !pThis->_flushingOverlay && y2 >= navTop) {
        y2 = navTop - 1;
        LOG_I("Flush: clip page above nav bar y<%d", navTop);
    }
    if (x2 < x1 || y2 < y1) {
        paint = false;
    }

    if (paint) {
        LOG_D("Flush chunk: (%d,%d)-(%d,%d) last=%d", x1, y1, x2, y2, isLast ? 1 : 0);
        const int32_t buf_stride = ((area->x2 - area->x1 + 1) + 7) / 8;
        for (int32_t y = y1; y <= y2; y++) {
            const uint8_t* row = px_map + (y - src_y1) * buf_stride;
            for (int32_t x = x1; x <= x2; x++) {
                const int32_t rel_x = x - src_x1;
                const bool isSet = (row[rel_x / 8] >> (7 - (rel_x % 8))) & 0x01;
                pThis->_epd->drawPixel(x, y, isSet ? GxEPD_WHITE : GxEPD_BLACK);
            }
        }
        pThis->addDirty(x1, y1, x2, y2);
    }

    if (isLast) {
        pThis->presentDirty();
    }
    lv_display_flush_ready(disp);
}

void TTLvglEpdDriver::addDirty(int32_t x1, int32_t y1, int32_t x2, int32_t y2) {
    if (!_hasDirty) {
        _dirty.x1 = x1;
        _dirty.y1 = y1;
        _dirty.x2 = x2;
        _dirty.y2 = y2;
        _hasDirty = true;
        return;
    }
    if (x1 < _dirty.x1) _dirty.x1 = x1;
    if (y1 < _dirty.y1) _dirty.y1 = y1;
    if (x2 > _dirty.x2) _dirty.x2 = x2;
    if (y2 > _dirty.y2) _dirty.y2 = y2;
}

void TTLvglEpdDriver::presentDirty() {
    if (!_hasDirty) {
        return;
    }
    _hasDirty = false;
    int32_t x1 = _dirty.x1;
    int32_t y1 = _dirty.y1;
    int32_t x2 = _dirty.x2;
    int32_t y2 = _dirty.y2;

    const int32_t pageBottom = EPD_HEIGHT - TT_NAV_PAGE_INSET - 1;
    const bool coversPage = x1 <= 0 && y1 <= 0 && x2 >= EPD_WIDTH - 1 && y2 >= pageBottom;
    const bool doDeepFull = _needDeepRefresh && !_flushingOverlay && coversPage;
    if (_needDeepRefresh && !_flushingOverlay && !coversPage) {
        LOG_I("Flush: hold deep refresh for the page");
    }

    const uint32_t flushStart = millis();
    if (doDeepFull) {
        _needDeepRefresh = false;
        _partialCount = 0;
        _deepRefreshPending = false;
        LOG_I("E-Paper deep full refresh (with nav bar)");
        _epd->display(false);
    } else {
        if (EPD_ROTATION % 2 == 1) {
            y1 &= ~0x7;
            y2 |= 0x7;
            if (y2 > EPD_HEIGHT - 1) y2 = EPD_HEIGHT - 1;
        }
        const int32_t w = x2 - x1 + 1;
        const int32_t h = y2 - y1 + 1;
        if (!_needDeepRefresh) {
            _partialCount++;
        }
        LOG_I("E-Paper partial refresh at (%d,%d) %dx%d, partial count: %d", x1, y1, w, h, _partialCount);
        _epd->displayWindow((uint16_t)x1, (uint16_t)y1, (uint16_t)w, (uint16_t)h);
    }
    LOG_I("E-Paper flush complete in %u ms", (unsigned)(millis() - flushStart));

    if (_autoDeepRefresh
        && _partialCount >= EPD_FULL_REFRESH_INTERVAL
        && !_deepRefreshPending) {
        _deepRefreshPending = true;
        TTInstanceOf<TTUITask>().requestDeepRefreshAsync();
    }
}

void TTLvglEpdDriver::setAutoDeepRefresh(bool enabled) {
    _autoDeepRefresh = enabled;
    if (enabled) {
        _partialCount = 0;
    } else {
        _needDeepRefresh = false;
        _deepRefreshPending = false;
    }
    LOG_I("E-Paper auto deep refresh=%d pending=%d",
          enabled ? 1 : 0, _deepRefreshPending ? 1 : 0);
}

void TTLvglEpdDriver::hibernate() {
    if (_epd == nullptr) {
        return;
    }
    _epd->hibernate();
    LOG_I("E-Paper hibernate");
}

void TTLvglEpdDriver::requestOverlayRefresh() {
    if (_lvDisplay == nullptr) {
        return;
    }
    LOG_I("Flush: overlay refresh");
    _flushingOverlay = true;
    invalidateTopLayerWidgets(_lvDisplay);
    lv_refr_now(_lvDisplay);
    _flushingOverlay = false;
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
