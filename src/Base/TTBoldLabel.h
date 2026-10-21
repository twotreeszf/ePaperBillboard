#pragma once

#include <lvgl.h>

#define TT_BOLD_LABEL_EXTRA 1

class TTBoldLabel {
public:
    static lv_obj_t* create(lv_obj_t* parent, const char* text, lv_font_t* font);

private:
    static void onDraw(lv_event_t* e);
};
