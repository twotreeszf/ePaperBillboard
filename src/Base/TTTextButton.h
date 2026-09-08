#pragma once

#include <lvgl.h>

#define TT_TEXT_BUTTON_W         140
#define TT_TEXT_BUTTON_H         32
#define TT_TEXT_BUTTON_DOT_SIZE  6
#define TT_TEXT_BUTTON_DOT_GAP   6

class TTTextButton {
public:
    static lv_obj_t* create(lv_obj_t* parent, const char* text, lv_font_t* font,
                            int32_t w = TT_TEXT_BUTTON_W, int32_t h = TT_TEXT_BUTTON_H);
    static void setText(lv_obj_t* btn, const char* text);

private:
    static lv_obj_t* dotOf(lv_obj_t* btn);
    static lv_obj_t* labelOf(lv_obj_t* btn);
    static void setFocusDot(lv_obj_t* btn, bool focused);
    static void onEvent(lv_event_t* e);
};
