#include "TTPopupLayer.h"
#include "TTKeypadInput.h"
#include "Logger.h"
#include "TTInstance.h"
#include "TTFontManager.h"
#include "TTLvglEpdDriver.h"

void TTPopupLayer::begin(lv_display_t* display) {
    _display = display;
    if (_display == nullptr) return;
    _topLayer = lv_display_get_layer_top(_display);
    if (_topLayer == nullptr) {
        LOG_E("PopupLayer: failed to get top layer");
        return;
    }
    lv_obj_set_size(_topLayer, EPD_WIDTH, EPD_HEIGHT);
    lv_obj_set_style_bg_opa(_topLayer, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(_topLayer, LV_OBJ_FLAG_SCROLLABLE);
    LOG_I("PopupLayer: top layer ready");
}

void TTPopupLayer::showToast(const char* text, uint32_t durationMs) {
    if (_topLayer == nullptr) return;
    dismissToast();

    _toastPanel = lv_obj_create(_topLayer);
    lv_obj_add_flag(_toastPanel, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_size(_toastPanel, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(_toastPanel, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(_toastPanel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(_toastPanel, lv_color_black(), 0);
    lv_obj_set_style_border_width(_toastPanel, 1, 0);
    lv_obj_set_style_radius(_toastPanel, 1, 0);
    lv_obj_set_style_pad_all(_toastPanel, 6, 0);
    lv_obj_remove_flag(_toastPanel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* label = lv_label_create(_toastPanel);
    lv_label_set_text(label, text != nullptr ? text : "");
    lv_obj_set_style_text_color(label, lv_color_black(), 0);
    lv_font_t* font = TTFontManager::instance().getFont(12);
    if (font != nullptr) {
        lv_obj_set_style_text_font(label, font, 0);
    }

    lv_obj_update_layout(_toastPanel);
    lv_obj_align(_toastPanel, LV_ALIGN_CENTER, 0, 0);

    TTInstanceOf<TTLvglEpdDriver>().requestRefresh(TT_REFRESH_FULL);

    if (durationMs > 0 && _toastTimer == nullptr) {
        _toastTimer = lv_timer_create(toastTimerCallback, durationMs, this);
        lv_timer_set_repeat_count(_toastTimer, 1);
    }
}

void TTPopupLayer::dismissToast() {
    if (_toastTimer != nullptr) {
        lv_timer_del(_toastTimer);
        _toastTimer = nullptr;
    }
    if (_toastPanel != nullptr) {
        lv_obj_delete(_toastPanel);
        _toastPanel = nullptr;
        TTInstanceOf<TTLvglEpdDriver>().requestRefresh(TT_REFRESH_FULL);
    }
}

void TTPopupLayer::toastTimerCallback(lv_timer_t* timer) {
    TTPopupLayer* self = (TTPopupLayer*)lv_timer_get_user_data(timer);
    if (self != nullptr) {
        self->_toastTimer = nullptr;
        if (self->_toastPanel != nullptr) {
            lv_obj_delete(self->_toastPanel);
            self->_toastPanel = nullptr;
            TTInstanceOf<TTLvglEpdDriver>().requestRefresh(TT_REFRESH_FULL);
        }
    }
}

void TTPopupLayer::showLoading() {
    if (_topLayer == nullptr) return;
    dismissLoading();

    _loadingPanel = lv_obj_create(_topLayer);
    lv_obj_add_flag(_loadingPanel, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_size(_loadingPanel, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(_loadingPanel, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(_loadingPanel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(_loadingPanel, lv_color_black(), 0);
    lv_obj_set_style_border_width(_loadingPanel, 1, 0);
    lv_obj_set_style_radius(_loadingPanel, 1, 0);
    lv_obj_set_style_pad_all(_loadingPanel, 12, 0);
    lv_obj_remove_flag(_loadingPanel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* label = lv_label_create(_loadingPanel);
    lv_label_set_text(label, "Loading...");
    lv_obj_set_style_text_color(label, lv_color_black(), 0);
    lv_font_t* font = TTFontManager::instance().getFont(12);
    if (font != nullptr) {
        lv_obj_set_style_text_font(label, font, 0);
    }

    lv_obj_update_layout(_loadingPanel);
    lv_obj_align(_loadingPanel, LV_ALIGN_CENTER, 0, 0);

    TTInstanceOf<TTLvglEpdDriver>().requestRefresh(TT_REFRESH_FULL);
}

void TTPopupLayer::dismissLoading() {
    if (_loadingPanel != nullptr) {
        lv_obj_delete(_loadingPanel);
        _loadingPanel = nullptr;
        TTInstanceOf<TTLvglEpdDriver>().requestRefresh(TT_REFRESH_FULL);
    }
}

void TTPopupLayer::dialogBtnFocusChanged(lv_event_t* e) {
    lv_obj_t* underline = (lv_obj_t*)lv_event_get_user_data(e);
    if (underline == nullptr) return;
    uint32_t code = lv_event_get_code(e);
    if (code == LV_EVENT_FOCUSED) {
        lv_obj_clear_flag(underline, LV_OBJ_FLAG_HIDDEN);
    } else if (code == LV_EVENT_DEFOCUSED) {
        lv_obj_add_flag(underline, LV_OBJ_FLAG_HIDDEN);
    }
}

void TTPopupLayer::dialogBtnClicked(lv_event_t* e) {
    lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
    lv_obj_t* btnRow = (lv_obj_t*)lv_obj_get_parent(btn);
    lv_obj_t* panel = (lv_obj_t*)lv_obj_get_parent(btnRow);
    TTPopupLayer* self = (TTPopupLayer*)lv_obj_get_user_data(panel);
    if (self == nullptr) return;
    intptr_t isOk = (intptr_t)lv_event_get_user_data(e);
    self->dismissDialog();

    if (isOk && self->_onDialogOk) self->_onDialogOk();
    if (!isOk && self->_onDialogCancel) self->_onDialogCancel();
    self->_onDialogOk = nullptr;
    self->_onDialogCancel = nullptr;
}

void TTPopupLayer::showDialog(const char* msg, DialogCallback onOk, DialogCallback onCancel) {
    if (_topLayer == nullptr) return;
    dismissDialog();

    _onDialogOk = std::move(onOk);
    _onDialogCancel = std::move(onCancel);

    if (_keypad != nullptr)
        _savedPageGroup = lv_indev_get_group(_keypad->getIndev());

    _dialogPanel = lv_obj_create(_topLayer);
    lv_obj_set_user_data(_dialogPanel, this);
    lv_obj_add_flag(_dialogPanel, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_size(_dialogPanel, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(_dialogPanel, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(_dialogPanel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(_dialogPanel, lv_color_black(), 0);
    lv_obj_set_style_border_width(_dialogPanel, 1, 0);
    lv_obj_set_style_radius(_dialogPanel, 1, 0);
    lv_obj_set_style_pad_all(_dialogPanel, 8, 0);
    lv_obj_remove_flag(_dialogPanel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(_dialogPanel, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(_dialogPanel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(_dialogPanel, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(_dialogPanel, 8, 0);

    lv_font_t* font = TTFontManager::instance().getFont(12);
    if (font == nullptr) font = (lv_font_t*)LV_FONT_DEFAULT;

    lv_obj_t* label = lv_label_create(_dialogPanel);
    lv_label_set_text(label, msg != nullptr ? msg : "");
    lv_obj_set_style_text_color(label, lv_color_black(), 0);
    lv_obj_set_style_text_font(label, font, 0);

    lv_obj_t* btnRow = lv_obj_create(_dialogPanel);
    lv_obj_set_size(btnRow, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(btnRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btnRow, 0, 0);
    lv_obj_set_style_pad_all(btnRow, 0, 0);
    lv_obj_set_layout(btnRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(btnRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btnRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(btnRow, 8, 0);

    _dialogGroup = lv_group_create();

    lv_obj_t* cancelBtn = lv_btn_create(btnRow);
    lv_obj_set_size(cancelBtn, 56, 24);
    lv_obj_set_style_radius(cancelBtn, 1, 0);
    lv_obj_set_style_bg_color(cancelBtn, lv_color_white(), 0);
    lv_obj_set_style_border_color(cancelBtn, lv_color_black(), 0);
    lv_obj_set_style_border_width(cancelBtn, 1, 0);
    lv_obj_set_style_border_width(cancelBtn, 1, LV_STATE_FOCUSED);
    lv_obj_t* cancelLabel = lv_label_create(cancelBtn);
    lv_label_set_text(cancelLabel, "Cancel");
    lv_obj_set_style_text_color(cancelLabel, lv_color_black(), 0);
    lv_obj_set_style_text_font(cancelLabel, font, 0);
    lv_obj_center(cancelLabel);
    lv_obj_update_layout(cancelBtn);
    lv_obj_t* cancelUnderline = lv_obj_create(cancelBtn);
    lv_obj_set_size(cancelUnderline, TT_POPUP_DIALOG_BTN_UNDERLINE_W_CANCEL, TT_POPUP_DIALOG_BTN_UNDERLINE_H);
    lv_obj_set_style_bg_color(cancelUnderline, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(cancelUnderline, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(cancelUnderline, 0, 0);
    lv_obj_set_style_pad_all(cancelUnderline, 0, 0);
    lv_obj_set_style_radius(cancelUnderline, 0, 0);
    lv_obj_align_to(cancelUnderline, cancelLabel, LV_ALIGN_OUT_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(cancelUnderline, LV_OBJ_FLAG_HIDDEN);
    lv_group_add_obj(_dialogGroup, cancelBtn);
    lv_obj_add_event_cb(cancelBtn, dialogBtnClicked, LV_EVENT_CLICKED, (void*)0);
    lv_obj_add_event_cb(cancelBtn, dialogBtnFocusChanged, LV_EVENT_FOCUSED, cancelUnderline);
    lv_obj_add_event_cb(cancelBtn, dialogBtnFocusChanged, LV_EVENT_DEFOCUSED, cancelUnderline);

    lv_obj_t* okBtn = lv_btn_create(btnRow);
    lv_obj_set_size(okBtn, 48, 24);
    lv_obj_set_style_radius(okBtn, 1, 0);
    lv_obj_set_style_bg_color(okBtn, lv_color_white(), 0);
    lv_obj_set_style_border_color(okBtn, lv_color_black(), 0);
    lv_obj_set_style_border_width(okBtn, 1, 0);
    lv_obj_set_style_border_width(okBtn, 1, LV_STATE_FOCUSED);
    lv_obj_t* okLabel = lv_label_create(okBtn);
    lv_label_set_text(okLabel, "OK");
    lv_obj_set_style_text_color(okLabel, lv_color_black(), 0);
    lv_obj_set_style_text_font(okLabel, font, 0);
    lv_obj_center(okLabel);
    lv_obj_update_layout(okBtn);
    lv_obj_t* okUnderline = lv_obj_create(okBtn);
    lv_obj_set_size(okUnderline, TT_POPUP_DIALOG_BTN_UNDERLINE_W_OK, TT_POPUP_DIALOG_BTN_UNDERLINE_H);
    lv_obj_set_style_bg_color(okUnderline, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(okUnderline, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(okUnderline, 0, 0);
    lv_obj_set_style_pad_all(okUnderline, 0, 0);
    lv_obj_set_style_radius(okUnderline, 0, 0);
    lv_obj_align_to(okUnderline, okLabel, LV_ALIGN_OUT_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(okUnderline, LV_OBJ_FLAG_HIDDEN);
    lv_group_add_obj(_dialogGroup, okBtn);
    lv_obj_add_event_cb(okBtn, dialogBtnClicked, LV_EVENT_CLICKED, (void*)1);
    lv_obj_add_event_cb(okBtn, dialogBtnFocusChanged, LV_EVENT_FOCUSED, okUnderline);
    lv_obj_add_event_cb(okBtn, dialogBtnFocusChanged, LV_EVENT_DEFOCUSED, okUnderline);

    if (_keypad != nullptr) {
        lv_indev_set_group(_keypad->getIndev(), _dialogGroup);
        lv_group_focus_obj(okBtn);
    }

    lv_obj_update_layout(_dialogPanel);
    lv_obj_align(_dialogPanel, LV_ALIGN_CENTER, 0, 0);

    TTInstanceOf<TTLvglEpdDriver>().requestRefresh(TT_REFRESH_FULL);
}

void TTPopupLayer::dismissDialog() {
    if (_keypad != nullptr && _savedPageGroup != nullptr)
        lv_indev_set_group(_keypad->getIndev(), _savedPageGroup);
    _savedPageGroup = nullptr;

    if (_dialogGroup != nullptr) {
        lv_group_delete(_dialogGroup);
        _dialogGroup = nullptr;
    }
    if (_dialogPanel != nullptr) {
        lv_obj_delete(_dialogPanel);
        _dialogPanel = nullptr;
        TTInstanceOf<TTLvglEpdDriver>().requestRefresh(TT_REFRESH_FULL);
    }
}
