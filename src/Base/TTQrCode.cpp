#include "TTQrCode.h"
#include "Logger.h"
#include <cstring>
#include <cstdio>
#include <qrcode.h>
#include <esp_heap_caps.h>

#define TT_QR_WIFI_PREFIX  "WIFI:T:nopass;S:"
#define TT_QR_WIFI_SUFFIX  ";P:;;"

struct TTQrCodeData {
    QRCode qr;
    uint8_t* modules;
};

static void appendEscaped(char* out, size_t outMax, size_t* pos, const char* s) {
    for (; s != nullptr && *s != '\0' && *pos + 2 < outMax; s++) {
        const char c = *s;
        if (c == '\\' || c == ';' || c == ',' || c == ':' || c == '"') {
            out[(*pos)++] = '\\';
        }
        out[(*pos)++] = c;
    }
    if (*pos < outMax) {
        out[*pos] = '\0';
    }
}

int32_t TTQrCode::pixelSize() {
    const int32_t modules = 4 * TT_QR_VERSION + 17 + TT_QR_QUIET * 2;
    return modules * TT_QR_MODULE_PX;
}

lv_obj_t* TTQrCode::create(lv_obj_t* parent) {
    lv_obj_t* qr = lv_obj_create(parent);
    const int32_t px = pixelSize();
    lv_obj_set_size(qr, px, px);
    lv_obj_set_style_bg_color(qr, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(qr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(qr, 0, 0);
    lv_obj_set_style_pad_all(qr, 0, 0);
    lv_obj_set_style_radius(qr, 0, 0);
    lv_obj_remove_flag(qr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(qr, LV_OBJ_FLAG_HIDDEN);

    TTQrCodeData* data = (TTQrCodeData*)heap_caps_malloc(sizeof(TTQrCodeData), MALLOC_CAP_8BIT);
    if (data == nullptr) {
        LOG_E("QR: data alloc failed");
        return qr;
    }
    memset(data, 0, sizeof(TTQrCodeData));
    lv_obj_set_user_data(qr, data);
    lv_obj_add_event_cb(qr, onDraw, LV_EVENT_DRAW_MAIN, nullptr);
    lv_obj_add_event_cb(qr, onDelete, LV_EVENT_DELETE, nullptr);
    return qr;
}

bool TTQrCode::setText(lv_obj_t* qr, const char* text) {
    if (qr == nullptr || text == nullptr || text[0] == '\0') {
        return false;
    }
    TTQrCodeData* data = (TTQrCodeData*)lv_obj_get_user_data(qr);
    if (data == nullptr) {
        return false;
    }

    const uint16_t bufSize = qrcode_getBufferSize(TT_QR_VERSION);
    uint8_t* modules = (uint8_t*)heap_caps_malloc(bufSize, MALLOC_CAP_8BIT);
    if (modules == nullptr) {
        LOG_E("QR: module buffer alloc failed %u", (unsigned)bufSize);
        return false;
    }

    QRCode encoded;
    int err = qrcode_initText(&encoded, modules, TT_QR_VERSION, ECC_MEDIUM, text);
    if (err != 0) {
        LOG_E("QR: encode failed err=%d len=%u", err, (unsigned)strlen(text));
        heap_caps_free(modules);
        return false;
    }

    if (data->modules != nullptr) {
        heap_caps_free(data->modules);
    }
    data->modules = modules;
    data->qr = encoded;
    LOG_I("QR: encoded version=%d size=%d payload_len=%u",
          TT_QR_VERSION, encoded.size, (unsigned)strlen(text));
    lv_obj_invalidate(qr);
    return true;
}

bool TTQrCode::setWifiOpen(lv_obj_t* qr, const char* ssid) {
    if (ssid == nullptr || ssid[0] == '\0') {
        LOG_E("QR: empty AP SSID");
        return false;
    }
    char payload[TT_QR_PAYLOAD_MAX];
    size_t pos = 0;
    const char* prefix = TT_QR_WIFI_PREFIX;
    while (*prefix != '\0' && pos + 1 < sizeof(payload)) {
        payload[pos++] = *prefix++;
    }
    appendEscaped(payload, sizeof(payload), &pos, ssid);
    const char* suffix = TT_QR_WIFI_SUFFIX;
    while (*suffix != '\0' && pos + 1 < sizeof(payload)) {
        payload[pos++] = *suffix++;
    }
    payload[pos] = '\0';
    LOG_I("QR: wifi payload=%s", payload);
    return setText(qr, payload);
}

void TTQrCode::onDraw(lv_event_t* e) {
    lv_obj_t* qr = (lv_obj_t*)lv_event_get_current_target(e);
    TTQrCodeData* data = (TTQrCodeData*)lv_obj_get_user_data(qr);
    if (data == nullptr || data->modules == nullptr || data->qr.size <= 0) {
        return;
    }

    lv_layer_t* layer = lv_event_get_layer(e);
    lv_area_t coords;
    lv_obj_get_coords(qr, &coords);

    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = lv_color_black();
    dsc.bg_opa = LV_OPA_COVER;
    dsc.border_width = 0;
    dsc.radius = 0;

    const int32_t originX = coords.x1 + TT_QR_QUIET * TT_QR_MODULE_PX;
    const int32_t originY = coords.y1 + TT_QR_QUIET * TT_QR_MODULE_PX;
    for (uint8_t y = 0; y < data->qr.size; y++) {
        for (uint8_t x = 0; x < data->qr.size; x++) {
            if (!qrcode_getModule(&data->qr, x, y)) {
                continue;
            }
            lv_area_t cell;
            cell.x1 = originX + (int32_t)x * TT_QR_MODULE_PX;
            cell.y1 = originY + (int32_t)y * TT_QR_MODULE_PX;
            cell.x2 = cell.x1 + TT_QR_MODULE_PX - 1;
            cell.y2 = cell.y1 + TT_QR_MODULE_PX - 1;
            lv_draw_rect(layer, &dsc, &cell);
        }
    }
}

void TTQrCode::onDelete(lv_event_t* e) {
    lv_obj_t* qr = (lv_obj_t*)lv_event_get_current_target(e);
    TTQrCodeData* data = (TTQrCodeData*)lv_obj_get_user_data(qr);
    if (data == nullptr) {
        return;
    }
    if (data->modules != nullptr) {
        heap_caps_free(data->modules);
    }
    heap_caps_free(data);
    lv_obj_set_user_data(qr, nullptr);
}
