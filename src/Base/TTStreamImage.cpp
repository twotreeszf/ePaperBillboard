#include "TTStreamImage.h"
#include "TTDrawBufPassthroughDecoder.h"
#include "Logger.h"
#include <LittleFS.h>
#include <cstring>

#include "core/lv_obj_private.h"
#include "core/lv_obj_class_private.h"
#include "misc/lv_area_private.h"
#include <spng.h>

#define MY_CLASS (&tt_stream_image_class)

struct tt_stream_image_t {
    lv_obj_t obj;
    char path[TT_STREAM_IMAGE_PATH_MAX];
    int32_t img_w;
    int32_t img_h;
};

static const uint8_t PNG_SIG[] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};

static void constructor(const lv_obj_class_t* class_p, lv_obj_t* obj);
static void destructor(const lv_obj_class_t* class_p, lv_obj_t* obj);
static void event_cb(const lv_obj_class_t* class_p, lv_event_t* e);
static void draw_main(lv_event_t* e);

static bool read_png_header(File& f, int32_t* out_w, int32_t* out_h);
static void rgba_to_i1_row(uint8_t* out, const uint8_t* rgba, int32_t w);
static int spng_read_cb(spng_ctx* ctx, void* user, void* dest, size_t length);

const lv_obj_class_t tt_stream_image_class = {
    .base_class = &lv_obj_class,
    .constructor_cb = constructor,
    .destructor_cb = destructor,
    .event_cb = event_cb,
    .user_data = nullptr,
    .name = "tt_stream_image",
    .width_def = LV_SIZE_CONTENT,
    .height_def = LV_SIZE_CONTENT,
    .editable = LV_OBJ_CLASS_EDITABLE_INHERIT,
    .group_def = LV_OBJ_CLASS_GROUP_DEF_INHERIT,
    .instance_size = sizeof(tt_stream_image_t),
    .theme_inheritable = LV_OBJ_CLASS_THEME_INHERITABLE_TRUE,
};

lv_obj_t* tt_stream_image_create(lv_obj_t* parent) {
    lv_obj_t* obj = lv_obj_class_create_obj(MY_CLASS, parent);
    if (!obj) return nullptr;
    lv_obj_class_init_obj(obj);
    return obj;
}

void tt_stream_image_set_src(lv_obj_t* obj, const char* path) {
    tt_stream_image_t* img = (tt_stream_image_t*)obj;
    if (!path || !path[0]) {
        img->path[0] = '\0';
        img->img_w = 0;
        img->img_h = 0;
        lv_obj_invalidate(obj);
        return;
    }
    size_t len = strlen(path);
    if (len >= sizeof(img->path)) {
        LOG_E("TTStreamImage: path too long");
        return;
    }
    memcpy(img->path, path, len + 1);
    
    File f = LittleFS.open(path, "r");
    if (!f) {
        LOG_E("TTStreamImage: open failed %s", path);
        img->img_w = 0;
        img->img_h = 0;
        lv_obj_invalidate(obj);
        return;
    }
    int32_t w = 0, h = 0;
    if (!read_png_header(f, &w, &h)) {
        LOG_E("TTStreamImage: invalid PNG header %s", path);
        f.close();
        img->img_w = 0;
        img->img_h = 0;
        lv_obj_invalidate(obj);
        return;
    }
    
    f.close();
    if (w <= 0 || w > TT_STREAM_IMAGE_MAX_W || h <= 0 || h > TT_STREAM_IMAGE_MAX_H) {
        LOG_E("TTStreamImage: size %" LV_PRId32 "x%" LV_PRId32 " out of range", w, h);
        img->img_w = 0;
        img->img_h = 0;
        lv_obj_invalidate(obj);
        return;
    }
    img->img_w = w;
    img->img_h = h;
    lv_obj_set_size(obj, w, h);
    lv_obj_invalidate(obj);
}

static void constructor(const lv_obj_class_t* class_p, lv_obj_t* obj) {
    LV_UNUSED(class_p);
    tt_stream_image_t* img = (tt_stream_image_t*)obj;
    img->path[0] = '\0';
    img->img_w = 0;
    img->img_h = 0;
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
}

static void destructor(const lv_obj_class_t* class_p, lv_obj_t* obj) {
    LV_UNUSED(class_p);
    tt_stream_image_t* img = (tt_stream_image_t*)obj;
    LV_UNUSED(img);
}

static void event_cb(const lv_obj_class_t* class_p, lv_event_t* e) {
    lv_result_t res = lv_obj_event_base(class_p, e);
    if (res != LV_RESULT_OK) return;
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_DRAW_MAIN) {
        draw_main(e);
    }
}

static bool read_png_header(File& f, int32_t* out_w, int32_t* out_h) {
    uint8_t buf[24];
    if (f.read(buf, sizeof(buf)) != sizeof(buf)) return false;
    if (memcmp(buf, PNG_SIG, sizeof(PNG_SIG)) != 0) return false;
    uint32_t w = (uint32_t)buf[16] << 24 | (uint32_t)buf[17] << 16 |
                 (uint32_t)buf[18] << 8 | buf[19];
    uint32_t h = (uint32_t)buf[20] << 24 | (uint32_t)buf[21] << 16 |
                 (uint32_t)buf[22] << 8 | buf[23];
    *out_w = (int32_t)w;
    *out_h = (int32_t)h;
    return true;
}

static void rgba_to_i1_row(uint8_t* out, const uint8_t* rgba, int32_t w) {
    int32_t stride = (w + 7) / 8;
    memset(out, 0xFF, (size_t)stride);
    for (int32_t col = 0; col < w; col++) {
        int white;
        if (rgba[3] < 128)
            white = 1;
        else {
            uint8_t lum = (uint8_t)((rgba[0] * 77 + rgba[1] * 150 + rgba[2] * 29) >> 8);
            white = (lum > 128) ? 1 : 0;
        }
        int32_t byteIdx = col / 8;
        int bitIdx = 7 - (col % 8);
        if (white)
            out[byteIdx] |= (1 << bitIdx);
        else
            out[byteIdx] &= ~(1 << bitIdx);
        rgba += 4;
    }
}

static int spng_read_cb(spng_ctx* ctx, void* user, void* dest, size_t length) {
    (void)ctx;
    File* f = (File*)user;
    if (!f || !dest) return SPNG_IO_ERROR;
    size_t total = 0;
    while (total < length) {
        size_t to_read = length - total;
        size_t n = f->read((uint8_t*)dest + total, to_read);
        if (n == 0) return SPNG_IO_EOF;
        total += n;
    }
    return 0;
}

static void draw_main(lv_event_t* e) {
    lv_obj_t* obj = (lv_obj_t*)lv_event_get_current_target(e);
    tt_stream_image_t* img = (tt_stream_image_t*)obj;
    if (img->img_w <= 0 || img->img_h <= 0 || img->path[0] == '\0') return;

    lv_layer_t* layer = lv_event_get_layer(e);
    lv_area_t obj_coords;
    lv_obj_get_coords(obj, &obj_coords);
    lv_area_t clip;
    if (!lv_area_intersect(&clip, &obj_coords, &layer->_clip_area)) return;

    int32_t x1 = clip.x1 - obj_coords.x1;
    int32_t y1 = clip.y1 - obj_coords.y1;
    int32_t x2 = clip.x2 - obj_coords.x1;
    int32_t y2 = clip.y2 - obj_coords.y1;
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 >= img->img_w) x2 = img->img_w - 1;
    if (y2 >= img->img_h) y2 = img->img_h - 1;
    if (x1 > x2 || y1 > y2) return;

    int32_t chunk_w = x2 - x1 + 1;
    int32_t chunk_h = y2 - y1 + 1;
    int32_t stride = (chunk_w + 7) / 8;
    size_t chunk_buf_size = (size_t)stride * (size_t)chunk_h;
    if (chunk_buf_size > TT_STREAM_IMAGE_ROW_BYTES * (size_t)TT_STREAM_IMAGE_MAX_H)
        return;

    static uint8_t row_buf[TT_STREAM_IMAGE_ROW_BYTES];
    static uint8_t chunk_buf[TT_STREAM_IMAGE_MAX_H * ((TT_STREAM_IMAGE_MAX_W + 7) / 8)];
    static uint8_t file_buf[TT_STREAM_IMAGE_FILE_BUF_SZ];
    if (chunk_buf_size > sizeof(chunk_buf)) return;
    memset(chunk_buf, 0xFF, chunk_buf_size);

    File f = LittleFS.open(img->path, "r");
    if (!f) {
        LOG_E("TTStreamImage: draw open failed %s", img->path);
        return;
    }
    size_t file_size = (size_t)f.size();
    bool use_buffer = (file_size > 0 && file_size <= sizeof(file_buf));
    if (use_buffer) {
        if (f.read(file_buf, file_size) != file_size) {
            f.close();
            LOG_E("TTStreamImage: read short path=%s", img->path);
            return;
        }
        f.close();
    }

    spng_ctx* ctx = spng_ctx_new(0);
    if (!ctx) {
        if (!use_buffer) f.close();
        LOG_E("TTStreamImage: spng_ctx_new failed");
        return;
    }
    int err;
    if (use_buffer)
        err = spng_set_png_buffer(ctx, file_buf, file_size);
    else
        err = spng_set_png_stream(ctx, (spng_rw_fn*)spng_read_cb, &f);
    if (err) {
        spng_ctx_free(ctx);
        if (!use_buffer) f.close();
        LOG_E("TTStreamImage: spng_set_* %s path=%s", spng_strerror(err), img->path);
        return;
    }
    err = spng_set_image_limits(ctx, (size_t)TT_STREAM_IMAGE_MAX_W, (size_t)TT_STREAM_IMAGE_MAX_H);
    if (err) {
        spng_ctx_free(ctx);
        if (!use_buffer) f.close();
        LOG_E("TTStreamImage: spng_set_image_limits %s", spng_strerror(err));
        return;
    }
    err = spng_decode_image(ctx, NULL, 0, SPNG_FMT_RGBA8, SPNG_DECODE_PROGRESSIVE);
    if (err) {
        if (err == SPNG_EFILTER && use_buffer && file_size >= 8) {
            const uint8_t* p = file_buf;
            uint32_t sum = 0;
            for (size_t i = 0; i < file_size; i++) sum += p[i];
            LOG_E("TTStreamImage: %s path=%s size=%u sum=%lu (compare with: python3 tools/analyze_png.py --checksum <file>)",
                  spng_strerror(err), img->path, (unsigned)file_size, (unsigned long)sum);
        } else {
            LOG_E("TTStreamImage: spng_decode_image %s path=%s", spng_strerror(err), img->path);
        }
        spng_ctx_free(ctx);
        if (!use_buffer) f.close();
        return;
    }
    size_t row_size = (size_t)img->img_w * 4u;
    if (row_size > sizeof(row_buf)) {
        spng_ctx_free(ctx);
        if (!use_buffer) f.close();
        return;
    }
    for (uint32_t row = 0; row < (uint32_t)img->img_h; row++) {
        err = spng_decode_row(ctx, row_buf, row_size);
        if (row >= (uint32_t)y1 && row <= (uint32_t)y2) {
            int32_t out_row = (int32_t)row - y1;
            uint8_t* chunk_row = chunk_buf + (size_t)out_row * (size_t)stride;
            const uint8_t* rgba = row_buf + (size_t)x1 * 4;
            rgba_to_i1_row(chunk_row, rgba, chunk_w);
        }
        if (err == SPNG_EOI) break;
        if (err) {
            LOG_E("TTStreamImage: spng_decode_row %s path=%s row=%u", spng_strerror(err), img->path, (unsigned)row);
            break;
        }
    }
    spng_ctx_free(ctx);
    if (!use_buffer) f.close();

    lv_draw_buf_t draw_buf;
    memset(&draw_buf, 0, sizeof(draw_buf));
    draw_buf.header.magic = LV_IMAGE_HEADER_MAGIC;
    draw_buf.header.cf = LV_COLOR_FORMAT_I1;
    draw_buf.header.flags = TT_DRAW_BUF_PASSTHROUGH_FLAG;
    draw_buf.header.w = chunk_w;
    draw_buf.header.h = chunk_h;
    draw_buf.header.stride = (uint32_t)stride;
    draw_buf.data_size = chunk_buf_size;
    draw_buf.data = chunk_buf;
    draw_buf.unaligned_data = chunk_buf;
    draw_buf.handlers = TTDrawBufPassthroughDecoder_get_handlers();

    lv_draw_image_dsc_t draw_dsc;
    lv_draw_image_dsc_init(&draw_dsc);
    draw_dsc.base.layer = layer;
    lv_obj_init_draw_image_dsc(obj, LV_PART_MAIN, &draw_dsc);
    draw_dsc.src = &draw_buf;
    draw_dsc.opa = LV_OPA_COVER;
    draw_dsc.rotation = 0;
    draw_dsc.scale_x = LV_SCALE_NONE;
    draw_dsc.scale_y = LV_SCALE_NONE;

    lv_area_t coords;
    coords.x1 = obj_coords.x1 + x1;
    coords.y1 = obj_coords.y1 + y1;
    coords.x2 = coords.x1 + chunk_w - 1;
    coords.y2 = coords.y1 + chunk_h - 1;
    draw_dsc.image_area = coords;
    lv_draw_image(layer, &draw_dsc, &coords);
}
