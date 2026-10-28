#pragma once

#include <lvgl.h>

#define TT_QR_VERSION      4
#define TT_QR_MODULE_PX    4
#define TT_QR_QUIET        3

class TTQrCode {
public:
    static lv_obj_t* create(lv_obj_t* parent, uint8_t version = TT_QR_VERSION);
    static bool setText(lv_obj_t* qr, const char* text);
    static int32_t pixelSize(uint8_t version = TT_QR_VERSION);

private:
    static void onDraw(lv_event_t* e);
    static void onDelete(lv_event_t* e);
};
