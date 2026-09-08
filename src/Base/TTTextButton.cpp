#include "TTTextButton.h"

static void applyPlainBg(lv_obj_t* btn, lv_style_selector_t sel) {
    lv_obj_set_style_bg_color(btn, lv_color_white(), sel);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, sel);
}

lv_obj_t* TTTextButton::create(lv_obj_t* parent, const char* text, lv_font_t* font, int32_t w, int32_t h) {
    lv_obj_t* btn = lv_btn_create(parent);
    lv_obj_set_size(btn, w, h);
    applyPlainBg(btn, 0);
    applyPlainBg(btn, LV_STATE_FOCUSED);
    applyPlainBg(btn, LV_STATE_FOCUS_KEY);
    applyPlainBg(btn, LV_STATE_FOCUSED | LV_STATE_FOCUS_KEY);
    lv_obj_set_style_border_color(btn, lv_color_black(), 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_radius(btn, 0, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_outline_width(btn, 0, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(btn, TT_TEXT_BUTTON_DOT_GAP, 0);
    lv_obj_add_event_cb(btn, onEvent, LV_EVENT_FOCUSED, nullptr);
    lv_obj_add_event_cb(btn, onEvent, LV_EVENT_DEFOCUSED, nullptr);

    lv_obj_t* dot = lv_obj_create(btn);
    lv_obj_set_size(dot, TT_TEXT_BUTTON_DOT_SIZE, TT_TEXT_BUTTON_DOT_SIZE);
    lv_obj_set_style_bg_color(dot, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    lv_obj_set_style_pad_all(dot, 0, 0);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* label = lv_label_create(btn);
    lv_label_set_text(label, text != nullptr ? text : "");
    lv_obj_set_style_text_color(label, lv_color_black(), 0);
    if (font != nullptr) {
        lv_obj_set_style_text_font(label, font, 0);
    }
    return btn;
}

void TTTextButton::setText(lv_obj_t* btn, const char* text) {
    lv_obj_t* label = labelOf(btn);
    if (label != nullptr) {
        lv_label_set_text(label, text != nullptr ? text : "");
    }
}

lv_obj_t* TTTextButton::dotOf(lv_obj_t* btn) {
    return btn != nullptr ? lv_obj_get_child(btn, 0) : nullptr;
}

lv_obj_t* TTTextButton::labelOf(lv_obj_t* btn) {
    return btn != nullptr ? lv_obj_get_child(btn, 1) : nullptr;
}

void TTTextButton::setFocusDot(lv_obj_t* btn, bool focused) {
    lv_obj_t* dot = dotOf(btn);
    if (dot != nullptr) {
        lv_obj_set_style_bg_opa(dot, focused ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    }
}

void TTTextButton::onEvent(lv_event_t* e) {
    lv_obj_t* btn = (lv_obj_t*)lv_event_get_current_target(e);
    uint32_t code = lv_event_get_code(e);
    if (code == LV_EVENT_FOCUSED) {
        setFocusDot(btn, true);
        return;
    }
    if (code == LV_EVENT_DEFOCUSED) {
        setFocusDot(btn, false);
    }
}
