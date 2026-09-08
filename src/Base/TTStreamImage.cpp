#include "TTStreamImage.h"
#include "TTDrawBufPassthroughDecoder.h"
#include "Logger.h"
#include <LittleFS.h>
#include <cstring>
#include <esp_heap_caps.h>

#include "core/lv_obj_private.h"
#include "core/lv_obj_class_private.h"
#include "misc/lv_area_private.h"
#include <spng.h>

#define MY_CLASS (&tt_stream_image_class)

struct tt_stream_image_cache_entry_t {
    char path[TT_STREAM_IMAGE_PATH_MAX];
    uint8_t* i1_data;
    int32_t img_w;
    int32_t img_h;
    uint32_t i1_stride;
    size_t i1_bytes;
    uint32_t refs;
    tt_stream_image_cache_entry_t* lru_prev;
    tt_stream_image_cache_entry_t* lru_next;
};

struct tt_stream_image_t {
    lv_obj_t obj;
    char path[TT_STREAM_IMAGE_PATH_MAX];
    int32_t img_w;
    int32_t img_h;
    tt_stream_image_cache_entry_t* entry;
};

static const uint8_t PNG_SIG[] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};

static tt_stream_image_cache_entry_t* s_lru_head = nullptr;
static tt_stream_image_cache_entry_t* s_lru_tail = nullptr;
static size_t s_cache_bytes = 0;

static void constructor(const lv_obj_class_t* class_p, lv_obj_t* obj);
static void destructor(const lv_obj_class_t* class_p, lv_obj_t* obj);
static void event_cb(const lv_obj_class_t* class_p, lv_event_t* e);
static void draw_main(lv_event_t* e);

static bool read_png_header(File& f, int32_t* out_w, int32_t* out_h);
static void rgba_to_i1_row(uint8_t* out, const uint8_t* rgba, int32_t w);
static int spng_read_cb(spng_ctx* ctx, void* user, void* dest, size_t length);

static void cache_lru_unlink(tt_stream_image_cache_entry_t* entry);
static void cache_lru_touch(tt_stream_image_cache_entry_t* entry);
static void cache_destroy_entry(tt_stream_image_cache_entry_t* entry);
static void cache_evict_until(size_t need_bytes);
static tt_stream_image_cache_entry_t* cache_find(const char* path);
static bool cache_decode_png(tt_stream_image_cache_entry_t* entry);
static tt_stream_image_cache_entry_t* cache_acquire(const char* path, int32_t w, int32_t h);
static void cache_release(tt_stream_image_cache_entry_t* entry);
static void image_clear_src(tt_stream_image_t* img);

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
    image_clear_src(img);
    if (!path || !path[0]) {
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
        lv_obj_invalidate(obj);
        return;
    }
    int32_t w = 0, h = 0;
    if (!read_png_header(f, &w, &h)) {
        LOG_E("TTStreamImage: invalid PNG header %s", path);
        f.close();
        lv_obj_invalidate(obj);
        return;
    }
    f.close();
    if (w <= 0 || w > TT_STREAM_IMAGE_MAX_W || h <= 0 || h > TT_STREAM_IMAGE_MAX_H) {
        LOG_E("TTStreamImage: size %" LV_PRId32 "x%" LV_PRId32 " out of range", w, h);
        lv_obj_invalidate(obj);
        return;
    }

    img->entry = cache_acquire(path, w, h);
    if (img->entry == nullptr) {
        LOG_E("TTStreamImage: cache acquire failed %s", path);
        img->path[0] = '\0';
        lv_obj_invalidate(obj);
        return;
    }
    img->img_w = img->entry->img_w;
    img->img_h = img->entry->img_h;
    lv_obj_set_size(obj, img->img_w, img->img_h);
    lv_obj_invalidate(obj);
}

static void constructor(const lv_obj_class_t* class_p, lv_obj_t* obj) {
    LV_UNUSED(class_p);
    tt_stream_image_t* img = (tt_stream_image_t*)obj;
    img->path[0] = '\0';
    img->img_w = 0;
    img->img_h = 0;
    img->entry = nullptr;
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
}

static void destructor(const lv_obj_class_t* class_p, lv_obj_t* obj) {
    LV_UNUSED(class_p);
    image_clear_src((tt_stream_image_t*)obj);
}

static void image_clear_src(tt_stream_image_t* img) {
    if (img->entry != nullptr) {
        cache_release(img->entry);
        img->entry = nullptr;
    }
    img->path[0] = '\0';
    img->img_w = 0;
    img->img_h = 0;
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

static void cache_lru_unlink(tt_stream_image_cache_entry_t* entry) {
    if (entry->lru_prev != nullptr) {
        entry->lru_prev->lru_next = entry->lru_next;
    } else {
        s_lru_head = entry->lru_next;
    }
    if (entry->lru_next != nullptr) {
        entry->lru_next->lru_prev = entry->lru_prev;
    } else {
        s_lru_tail = entry->lru_prev;
    }
    entry->lru_prev = nullptr;
    entry->lru_next = nullptr;
}

static bool cache_lru_is_linked(const tt_stream_image_cache_entry_t* entry) {
    return entry == s_lru_head || entry == s_lru_tail ||
           entry->lru_prev != nullptr || entry->lru_next != nullptr;
}

static void cache_lru_touch(tt_stream_image_cache_entry_t* entry) {
    if (s_lru_head == entry) {
        return;
    }
    if (cache_lru_is_linked(entry)) {
        cache_lru_unlink(entry);
    }
    entry->lru_next = s_lru_head;
    entry->lru_prev = nullptr;
    if (s_lru_head != nullptr) {
        s_lru_head->lru_prev = entry;
    } else {
        s_lru_tail = entry;
    }
    s_lru_head = entry;
}

static void cache_destroy_entry(tt_stream_image_cache_entry_t* entry) {
    cache_lru_unlink(entry);
    if (s_cache_bytes >= entry->i1_bytes) {
        s_cache_bytes -= entry->i1_bytes;
    } else {
        s_cache_bytes = 0;
    }
    if (entry->i1_data != nullptr) {
        heap_caps_free(entry->i1_data);
    }
    heap_caps_free(entry);
}

static void cache_evict_until(size_t need_bytes) {
    tt_stream_image_cache_entry_t* cur = s_lru_tail;
    while (cur != nullptr && s_cache_bytes + need_bytes > TT_STREAM_IMAGE_CACHE_MAX_BYTES) {
        tt_stream_image_cache_entry_t* prev = cur->lru_prev;
        if (cur->refs == 0) {
            LOG_I("TTStreamImage: LRU evict %s %u bytes, total %u -> %u",
                  cur->path, (unsigned)cur->i1_bytes,
                  (unsigned)s_cache_bytes,
                  (unsigned)(s_cache_bytes - cur->i1_bytes));
            cache_destroy_entry(cur);
        }
        cur = prev;
    }
}

static tt_stream_image_cache_entry_t* cache_find(const char* path) {
    for (tt_stream_image_cache_entry_t* cur = s_lru_head; cur != nullptr; cur = cur->lru_next) {
        if (strcmp(cur->path, path) == 0) {
            return cur;
        }
    }
    return nullptr;
}

static bool cache_decode_png(tt_stream_image_cache_entry_t* entry) {
    File f = LittleFS.open(entry->path, "r");
    if (!f) {
        LOG_E("TTStreamImage: cache open failed %s", entry->path);
        return false;
    }

    static uint8_t row_buf[TT_STREAM_IMAGE_ROW_BYTES];
    static uint8_t file_buf[TT_STREAM_IMAGE_FILE_BUF_SZ];
    size_t file_size = (size_t)f.size();
    bool use_buffer = (file_size > 0 && file_size <= sizeof(file_buf));
    if (use_buffer) {
        if (f.read(file_buf, file_size) != file_size) {
            f.close();
            LOG_E("TTStreamImage: cache read short path=%s", entry->path);
            return false;
        }
        f.close();
    }

    entry->i1_data = (uint8_t*)heap_caps_malloc(entry->i1_bytes, MALLOC_CAP_8BIT);
    if (entry->i1_data == nullptr) {
        if (!use_buffer) f.close();
        LOG_E("TTStreamImage: cache alloc failed %u bytes", (unsigned)entry->i1_bytes);
        return false;
    }
    memset(entry->i1_data, 0xFF, entry->i1_bytes);

    spng_ctx* ctx = spng_ctx_new(0);
    if (!ctx) {
        if (!use_buffer) f.close();
        heap_caps_free(entry->i1_data);
        entry->i1_data = nullptr;
        LOG_E("TTStreamImage: cache spng_ctx_new failed");
        return false;
    }
    int err;
    if (use_buffer)
        err = spng_set_png_buffer(ctx, file_buf, file_size);
    else
        err = spng_set_png_stream(ctx, (spng_rw_fn*)spng_read_cb, &f);
    if (err) {
        spng_ctx_free(ctx);
        if (!use_buffer) f.close();
        heap_caps_free(entry->i1_data);
        entry->i1_data = nullptr;
        LOG_E("TTStreamImage: cache spng_set_* %s path=%s", spng_strerror(err), entry->path);
        return false;
    }
    err = spng_set_image_limits(ctx, (size_t)TT_STREAM_IMAGE_MAX_W, (size_t)TT_STREAM_IMAGE_MAX_H);
    if (err) {
        spng_ctx_free(ctx);
        if (!use_buffer) f.close();
        heap_caps_free(entry->i1_data);
        entry->i1_data = nullptr;
        return false;
    }
    err = spng_decode_image(ctx, NULL, 0, SPNG_FMT_RGBA8, SPNG_DECODE_PROGRESSIVE);
    if (err) {
        LOG_E("TTStreamImage: cache decode %s path=%s", spng_strerror(err), entry->path);
        spng_ctx_free(ctx);
        if (!use_buffer) f.close();
        heap_caps_free(entry->i1_data);
        entry->i1_data = nullptr;
        return false;
    }
    size_t row_size = (size_t)entry->img_w * 4u;
    if (row_size > sizeof(row_buf)) {
        spng_ctx_free(ctx);
        if (!use_buffer) f.close();
        heap_caps_free(entry->i1_data);
        entry->i1_data = nullptr;
        return false;
    }
    for (uint32_t row = 0; row < (uint32_t)entry->img_h; row++) {
        err = spng_decode_row(ctx, row_buf, row_size);
        rgba_to_i1_row(entry->i1_data + (size_t)row * entry->i1_stride, row_buf, entry->img_w);
        if (err == SPNG_EOI) break;
        if (err) {
            LOG_E("TTStreamImage: cache row %s path=%s row=%u", spng_strerror(err), entry->path, (unsigned)row);
            break;
        }
    }
    spng_ctx_free(ctx);
    if (!use_buffer) f.close();
    return entry->i1_data != nullptr;
}

static tt_stream_image_cache_entry_t* cache_acquire(const char* path, int32_t w, int32_t h) {
    tt_stream_image_cache_entry_t* existing = cache_find(path);
    if (existing != nullptr) {
        existing->refs++;
        cache_lru_touch(existing);
        LOG_I("TTStreamImage: cache hit %s refs=%u total=%u",
              path, (unsigned)existing->refs, (unsigned)s_cache_bytes);
        return existing;
    }

    const size_t need_bytes = (size_t)((w + 7) / 8) * (size_t)h;
    if (need_bytes == 0 || need_bytes > TT_STREAM_IMAGE_CACHE_MAX_BYTES) {
        LOG_E("TTStreamImage: cache entry %u bytes exceeds cap %u",
              (unsigned)need_bytes, (unsigned)TT_STREAM_IMAGE_CACHE_MAX_BYTES);
        return nullptr;
    }

    cache_evict_until(need_bytes);
    if (s_cache_bytes + need_bytes > TT_STREAM_IMAGE_CACHE_MAX_BYTES) {
        LOG_E("TTStreamImage: cache full, cannot load %s need=%u in_use=%u",
              path, (unsigned)need_bytes, (unsigned)s_cache_bytes);
        return nullptr;
    }

    tt_stream_image_cache_entry_t* entry =
        (tt_stream_image_cache_entry_t*)heap_caps_malloc(sizeof(tt_stream_image_cache_entry_t), MALLOC_CAP_8BIT);
    if (entry == nullptr) {
        LOG_E("TTStreamImage: cache entry alloc failed");
        return nullptr;
    }
    memset(entry, 0, sizeof(*entry));
    strncpy(entry->path, path, sizeof(entry->path) - 1);
    entry->img_w = w;
    entry->img_h = h;
    entry->i1_stride = (uint32_t)((w + 7) / 8);
    entry->i1_bytes = need_bytes;
    if (!cache_decode_png(entry)) {
        heap_caps_free(entry);
        return nullptr;
    }

    s_cache_bytes += entry->i1_bytes;
    entry->refs = 1;
    cache_lru_touch(entry);
    LOG_I("TTStreamImage: cache insert %s %dx%d %u bytes refs=1 total=%u",
          path, (int)w, (int)h, (unsigned)entry->i1_bytes, (unsigned)s_cache_bytes);
    return entry;
}

static void cache_release(tt_stream_image_cache_entry_t* entry) {
    if (entry == nullptr || entry->refs == 0) {
        return;
    }
    entry->refs--;
    LOG_I("TTStreamImage: cache release %s refs=%u total=%u",
          entry->path, (unsigned)entry->refs, (unsigned)s_cache_bytes);
}

static void draw_main(lv_event_t* e) {
    lv_obj_t* obj = (lv_obj_t*)lv_event_get_current_target(e);
    tt_stream_image_t* img = (tt_stream_image_t*)obj;
    tt_stream_image_cache_entry_t* entry = img->entry;
    if (entry == nullptr || entry->i1_data == nullptr || img->img_w <= 0 || img->img_h <= 0) {
        return;
    }

    lv_layer_t* layer = lv_event_get_layer(e);
    lv_area_t obj_coords;
    lv_obj_get_coords(obj, &obj_coords);
    lv_area_t clip;
    if (!lv_area_intersect(&clip, &obj_coords, &layer->_clip_area)) {
        LOG_W("TTStreamImage: skip draw, clip miss path=%s", img->path);
        return;
    }

    cache_lru_touch(entry);

    lv_draw_buf_t draw_buf;
    memset(&draw_buf, 0, sizeof(draw_buf));
    draw_buf.header.magic = LV_IMAGE_HEADER_MAGIC;
    draw_buf.header.cf = LV_COLOR_FORMAT_I1;
    draw_buf.header.flags = TT_DRAW_BUF_PASSTHROUGH_FLAG;
    draw_buf.header.w = (uint32_t)entry->img_w;
    draw_buf.header.h = (uint32_t)entry->img_h;
    draw_buf.header.stride = entry->i1_stride;
    draw_buf.data_size = entry->i1_bytes;
    draw_buf.data = entry->i1_data;
    draw_buf.unaligned_data = entry->i1_data;
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
    draw_dsc.image_area = obj_coords;
    lv_draw_image(layer, &draw_dsc, &obj_coords);
}
