#pragma once

#include <lvgl.h>

/*
 * Streaming image widget. Uncompressed TTI1 files are read into a path-keyed
 * I1 cache (referenced entries only, 8KB cap) and blitted on draw.
 * Set source with tt_stream_image_set_src(obj, "/path/on/littlefs.i1").
 * Image size must be within TT_STREAM_IMAGE_MAX_W x TT_STREAM_IMAGE_MAX_H.
 * Requires TTDrawBufPassthroughDecoder_init() before use (called from TTLvglEpdDriver::begin).
 */

#define TT_STREAM_IMAGE_PATH_MAX  64
#define TT_STREAM_IMAGE_MAX_W     296
#define TT_STREAM_IMAGE_MAX_H     128
#define TT_STREAM_IMAGE_CACHE_MAX_BYTES  (8 * 1024)
#define TT_I1_HEADER_SIZE         8
#define TT_I1_MAGIC0              'T'
#define TT_I1_MAGIC1              'T'
#define TT_I1_MAGIC2              'I'
#define TT_I1_MAGIC3              '1'

typedef struct tt_stream_image_t tt_stream_image_t;

lv_obj_t* tt_stream_image_create(lv_obj_t* parent);
void tt_stream_image_set_src(lv_obj_t* obj, const char* path);

extern const lv_obj_class_t tt_stream_image_class;
