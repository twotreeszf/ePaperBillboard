#pragma once

#include <lvgl.h>
#include <functional>

#define TT_POPUP_DIALOG_BTN_UNDERLINE_H  1
#define TT_POPUP_DIALOG_BTN_UNDERLINE_W_CANCEL  42
#define TT_POPUP_DIALOG_BTN_UNDERLINE_W_OK  20

class TTKeypadInput;

/**
 * Popup layer that stays on top of all screen pages (LVGL display top layer).
 * Use for toast messages, dialogs, or any overlay that should appear above the nav stack.
 * Dialog takes keypad focus (own group); on show the current page group is saved and restored on dismiss.
 */
class TTPopupLayer {
public:
    using DialogCallback = std::function<void()>;

    void begin(lv_display_t* display);
    lv_obj_t* getLayer() { return _topLayer; }

    void setKeypadInput(TTKeypadInput* keypad) { _keypad = keypad; }

    void showToast(const char* text, uint32_t durationMs = 2000);
    void dismissToast();

    void showLoading();
    void dismissLoading();

    void showDialog(const char* msg, DialogCallback onOk, DialogCallback onCancel);
    void dismissDialog();

private:
    static void toastTimerCallback(lv_timer_t* timer);
    static void dialogBtnClicked(lv_event_t* e);
    static void dialogBtnFocusChanged(lv_event_t* e);

    lv_display_t* _display = nullptr;
    lv_obj_t* _topLayer = nullptr;
    lv_obj_t* _toastPanel = nullptr;
    lv_timer_t* _toastTimer = nullptr;
    lv_obj_t* _loadingPanel = nullptr;

    TTKeypadInput* _keypad = nullptr;
    lv_obj_t* _dialogPanel = nullptr;
    lv_group_t* _dialogGroup = nullptr;
    lv_group_t* _savedPageGroup = nullptr;
    DialogCallback _onDialogOk;
    DialogCallback _onDialogCancel;
};
