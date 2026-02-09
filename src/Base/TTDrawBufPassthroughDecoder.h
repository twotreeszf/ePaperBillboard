#pragma once

#include <lvgl.h>
#include "draw/lv_draw_buf.h"

#define TT_DRAW_BUF_PASSTHROUGH_FLAG  LV_IMAGE_FLAGS_USER1

void TTDrawBufPassthroughDecoder_init(void);
void TTDrawBufPassthroughDecoder_deinit(void);

bool TTDrawBufPassthroughDecoder_is_passthrough_buf(const void* src);

/** No-op handlers for passthrough draw buffers (no free); required by LVGL decoder assert. */
const lv_draw_buf_handlers_t* TTDrawBufPassthroughDecoder_get_handlers(void);
