#pragma once

#include <EPDConfig.h>
#include <lvgl.h>
#include "TTRefreshLevel.h"

class TTScreenshotDisplay;

class TTLvglEpdDriver {
public:

    bool begin(EPaperDisplay& display);
    void setCapture(TTScreenshotDisplay* capture) { _capture = capture; }
    void requestRefresh(TTRefreshLevel level = TT_REFRESH_PARTIAL);
    void requestOverlayRefresh();
    void setAutoDeepRefresh(bool enabled);
    bool isAutoDeepRefreshEnabled() const { return _autoDeepRefresh; }
    void hibernate();
    lv_display_t* getDisplay() { return _lvDisplay; }

private:
    static void _flushCallback(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map);
    void addDirty(int32_t x1, int32_t y1, int32_t x2, int32_t y2);
    void presentDirty();

    EPaperDisplay* _epd = nullptr;
    lv_area_t _dirty = {};
    bool _hasDirty = false;
    lv_display_t* _lvDisplay = nullptr;
    uint8_t _partialCount = 0;
    bool _needDeepRefresh = true;
    bool _autoDeepRefresh = true;
    bool _deepRefreshPending = false;
    bool _flushingOverlay = false;
    bool _navTouched = false;
    TTScreenshotDisplay* _capture = nullptr;
};
