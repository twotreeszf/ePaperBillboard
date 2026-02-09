#pragma once

#include <lvgl.h>

/*
 * Streaming image widget: no cache, decode on draw, direct to screen.
 * Set source with tt_stream_image_set_src(obj, "/path/on/littlefs.png").
 * PNG size must be within TT_STREAM_IMAGE_MAX_W x TT_STREAM_IMAGE_MAX_H.
 * Requires TTDrawBufPassthroughDecoder_init() before use (called from TTLvglEpdDriver::begin).
 */

#define TT_STREAM_IMAGE_PATH_MAX  64
#define TT_STREAM_IMAGE_MAX_W     296
#define TT_STREAM_IMAGE_MAX_H     128
#define TT_STREAM_IMAGE_ROW_BYTES    ((TT_STREAM_IMAGE_MAX_W * 4))
#define TT_STREAM_IMAGE_FILE_BUF_KB 12
#define TT_STREAM_IMAGE_FILE_BUF_SZ (TT_STREAM_IMAGE_FILE_BUF_KB * 1024)

typedef struct tt_stream_image_t tt_stream_image_t;

lv_obj_t* tt_stream_image_create(lv_obj_t* parent);
void tt_stream_image_set_src(lv_obj_t* obj, const char* path);

extern const lv_obj_class_t tt_stream_image_class;
