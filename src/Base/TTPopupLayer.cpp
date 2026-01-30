#include "TTPopupLayer.h"
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

    TTInstanceOf<TTLvglEpdDriver>().requestRefresh(false);

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
        TTInstanceOf<TTLvglEpdDriver>().requestRefresh(false);
    }
}

void TTPopupLayer::toastTimerCallback(lv_timer_t* timer) {
    TTPopupLayer* self = (TTPopupLayer*)lv_timer_get_user_data(timer);
    if (self != nullptr) {
        self->_toastTimer = nullptr;
        if (self->_toastPanel != nullptr) {
            lv_obj_delete(self->_toastPanel);
            self->_toastPanel = nullptr;
            TTInstanceOf<TTLvglEpdDriver>().requestRefresh(false);
        }
    }
}
