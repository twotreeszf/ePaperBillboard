#pragma once

#include <lvgl.h>

/**
 * Popup layer that stays on top of all screen pages (LVGL display top layer).
 * Use for toast messages, dialogs, or any overlay that should appear above the nav stack.
 */
class TTPopupLayer {
public:
    void begin(lv_display_t* display);
    lv_obj_t* getLayer() { return _topLayer; }

    void showToast(const char* text, uint32_t durationMs = 2000);
    void dismissToast();

private:
    static void toastTimerCallback(lv_timer_t* timer);

    lv_display_t* _display = nullptr;
    lv_obj_t* _topLayer = nullptr;
    lv_obj_t* _toastPanel = nullptr;
    lv_timer_t* _toastTimer = nullptr;
};
