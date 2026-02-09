#include "TTDrawBufPassthroughDecoder.h"
#include "Logger.h"
#include <cstring>

#include "draw/lv_image_decoder_private.h"
#include "draw/lv_draw_buf.h"
#include "draw/lv_draw_buf_private.h"

#define DECODER_NAME "TTDrawBufPassthrough"

static lv_result_t decoder_info(lv_image_decoder_t* decoder, lv_image_decoder_dsc_t* dsc,
                                lv_image_header_t* header) {
    LV_UNUSED(decoder);
    if (!TTDrawBufPassthroughDecoder_is_passthrough_buf(dsc->src))
        return LV_RESULT_INVALID;
    const lv_draw_buf_t* buf = (const lv_draw_buf_t*)dsc->src;
    *header = buf->header;
    return LV_RESULT_OK;
}

static lv_result_t decoder_open(lv_image_decoder_t* decoder, lv_image_decoder_dsc_t* dsc) {
    LV_UNUSED(decoder);
    if (!TTDrawBufPassthroughDecoder_is_passthrough_buf(dsc->src))
        return LV_RESULT_INVALID;
    dsc->decoded = (const lv_draw_buf_t*)dsc->src;
    dsc->header = ((const lv_draw_buf_t*)dsc->src)->header;
    return LV_RESULT_OK;
}

static void decoder_close(lv_image_decoder_t* decoder, lv_image_decoder_dsc_t* dsc) {
    LV_UNUSED(decoder);
    LV_UNUSED(dsc);
}

bool TTDrawBufPassthroughDecoder_is_passthrough_buf(const void* src) {
    if (!src) return false;
    const lv_draw_buf_t* buf = (const lv_draw_buf_t*)src;
    return buf->header.magic == LV_IMAGE_HEADER_MAGIC &&
           (buf->header.flags & TT_DRAW_BUF_PASSTHROUGH_FLAG) != 0;
}

static lv_image_decoder_t* s_decoder = nullptr;
static lv_draw_buf_handlers_t s_passthrough_handlers;
static bool s_handlers_inited = false;

const lv_draw_buf_handlers_t* TTDrawBufPassthroughDecoder_get_handlers(void) {
    if (!s_handlers_inited) {
        lv_draw_buf_handlers_init(&s_passthrough_handlers, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
        s_handlers_inited = true;
    }
    return &s_passthrough_handlers;
}

void TTDrawBufPassthroughDecoder_init(void) {
    (void)TTDrawBufPassthroughDecoder_get_handlers();
    if (s_decoder) return;
    s_decoder = lv_image_decoder_create();
    if (!s_decoder) {
        LOG_E("TTDrawBufPassthrough: failed to create decoder");
        return;
    }
    lv_image_decoder_set_info_cb(s_decoder, decoder_info);
    lv_image_decoder_set_open_cb(s_decoder, decoder_open);
    lv_image_decoder_set_close_cb(s_decoder, decoder_close);
    s_decoder->name = DECODER_NAME;
    LOG_I("TTDrawBufPassthrough decoder registered");
}

void TTDrawBufPassthroughDecoder_deinit(void) {
    if (!s_decoder) return;
    lv_image_decoder_delete(s_decoder);
    s_decoder = nullptr;
    LOG_I("TTDrawBufPassthrough decoder unregistered");
}
