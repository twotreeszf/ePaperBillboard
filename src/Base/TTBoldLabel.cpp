#include "TTBoldLabel.h"

void TTBoldLabel::onDraw(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_DRAW_MAIN) {
        return;
    }
    lv_obj_t* obj = static_cast<lv_obj_t*>(lv_event_get_target(e));
    const char* text = lv_label_get_text(obj);
    if (text == nullptr || text[0] == '\0') {
        return;
    }
    lv_area_t coords;
    lv_obj_get_content_coords(obj, &coords);
    coords.x1 += TT_BOLD_LABEL_EXTRA;
    coords.x2 += TT_BOLD_LABEL_EXTRA;

    lv_draw_label_dsc_t dsc;
    lv_draw_label_dsc_init(&dsc);
    lv_obj_init_draw_label_dsc(obj, LV_PART_MAIN, &dsc);
    dsc.text = text;
    lv_draw_label(lv_event_get_layer(e), &dsc, &coords);
}

void TTBoldLabel::setEnabled(lv_obj_t* label, bool enabled) {
    if (label == nullptr) {
        return;
    }
    lv_obj_remove_event_cb(label, onDraw);
    if (enabled) {
        lv_obj_add_event_cb(label, onDraw, LV_EVENT_DRAW_MAIN, nullptr);
    }
}

lv_obj_t* TTBoldLabel::create(lv_obj_t* parent, const char* text, lv_font_t* font) {
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, text != nullptr ? text : "");
    lv_obj_set_style_text_color(label, lv_color_black(), 0);
    if (font != nullptr) {
        lv_obj_set_style_text_font(label, font, 0);
    }
    lv_obj_add_event_cb(label, onDraw, LV_EVENT_DRAW_MAIN, nullptr);
    return label;
}
