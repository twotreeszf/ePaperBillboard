#pragma once

#include <lvgl.h>

/*
 * TTI1 image widget. Files larger than TT_STREAM_IMAGE_FILE_RENDER_MIN are
 * blitted from the file into the refresh buffer on draw. Smaller files stay
 * in a path-keyed I1 cache (referenced entries only, 20KB cap).
 * Set source with tt_stream_image_set_src(obj, "/path/on/littlefs.i1").
 * Image size must be within TT_STREAM_IMAGE_MAX_W x TT_STREAM_IMAGE_MAX_H.
 * Requires TTDrawBufPassthroughDecoder_init() before use (called from TTLvglEpdDriver::begin).
 */

#define TT_STREAM_IMAGE_PATH_MAX  64
#define TT_STREAM_IMAGE_MAX_W     400
#define TT_STREAM_IMAGE_MAX_H     300
#define TT_STREAM_IMAGE_ROW_BYTES (((TT_STREAM_IMAGE_MAX_W) + 7) / 8)
#define TT_STREAM_IMAGE_CACHE_MAX_BYTES  (20 * 1024)
#define TT_STREAM_IMAGE_FILE_RENDER_MIN  (4 * 1024)
#define TT_I1_HEADER_SIZE         8
#define TT_I1_MAGIC0              'T'
#define TT_I1_MAGIC1              'T'
#define TT_I1_MAGIC2              'I'
#define TT_I1_MAGIC3              '1'

typedef struct tt_stream_image_t tt_stream_image_t;

lv_obj_t* tt_stream_image_create(lv_obj_t* parent);
void tt_stream_image_set_src(lv_obj_t* obj, const char* path);
void tt_stream_image_set_invert(lv_obj_t* obj, bool invert);

extern const lv_obj_class_t tt_stream_image_class;
