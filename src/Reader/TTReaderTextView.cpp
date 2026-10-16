#include "TTReaderTextView.h"
#include "TTReaderText.h"
#include "../Base/Logger.h"

#include "core/lv_obj_private.h"
#include "core/lv_obj_class_private.h"
#include "misc/lv_area_private.h"

#define MY_CLASS (&tt_reader_text_view_class)

struct tt_reader_text_view_t {
    lv_obj_t obj;
    TTReaderLayout* layout;
    const TTReaderPageLayout* page;
};

static void constructor(const lv_obj_class_t* class_p, lv_obj_t* obj);
static void event_cb(const lv_obj_class_t* class_p, lv_event_t* e);
static void draw_main(lv_event_t* e);

const lv_obj_class_t tt_reader_text_view_class = {
    .base_class = &lv_obj_class,
    .constructor_cb = constructor,
    .destructor_cb = nullptr,
    .event_cb = event_cb,
    .user_data = nullptr,
    .name = "tt_reader_text_view",
    .width_def = LV_PCT(100),
    .height_def = LV_PCT(100),
    .editable = LV_OBJ_CLASS_EDITABLE_INHERIT,
    .group_def = LV_OBJ_CLASS_GROUP_DEF_INHERIT,
    .instance_size = sizeof(tt_reader_text_view_t),
    .theme_inheritable = LV_OBJ_CLASS_THEME_INHERITABLE_TRUE,
};

lv_obj_t* tt_reader_text_view_create(lv_obj_t* parent) {
    lv_obj_t* obj = lv_obj_class_create_obj(MY_CLASS, parent);
    if (!obj) return nullptr;
    lv_obj_class_init_obj(obj);
    return obj;
}

void tt_reader_text_view_set_page(lv_obj_t* obj, TTReaderLayout* layout, const TTReaderPageLayout* page) {
    tt_reader_text_view_t* view = (tt_reader_text_view_t*)obj;
    view->layout = layout;
    view->page = page;
    lv_obj_invalidate(obj);
}

static void constructor(const lv_obj_class_t* class_p, lv_obj_t* obj) {
    LV_UNUSED(class_p);
    tt_reader_text_view_t* view = (tt_reader_text_view_t*)obj;
    view->layout = nullptr;
    view->page = nullptr;
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_clickable(obj, false);
    lv_obj_set_scrollable(obj, false);
}

static void event_cb(const lv_obj_class_t* class_p, lv_event_t* e) {
    lv_result_t res = lv_obj_event_base(class_p, e);
    if (res != LV_RESULT_OK) return;
    if (lv_event_get_code(e) == LV_EVENT_DRAW_MAIN) {
        draw_main(e);
    }
}

static uint16_t justify_extra(const TTReaderLine& line, uint16_t gap) {
    if (line.gaps == 0) {
        return 0;
    }
    const uint32_t extra = line.extra;
    return (uint16_t)(extra * (gap + 1) / line.gaps - extra * gap / line.gaps);
}

static void blit_glyph(const lv_layer_t* layer, const lv_area_t& clip, uint8_t* pixels, uint32_t stride,
                       const uint8_t* bits, int32_t gx, int32_t gy, int32_t w, int32_t h) {
    for (int32_t r = 0; r < h; r++) {
        const int32_t y = gy + r;
        if (y < clip.y1 || y > clip.y2) {
            continue;
        }
        uint8_t* row = pixels + (uint32_t)(y - layer->buf_area.y1) * stride;
        for (int32_t c = 0; c < w; c++) {
            const int32_t x = gx + c;
            if (x < clip.x1 || x > clip.x2) {
                continue;
            }
            const int32_t idx = r * w + c;
            if ((bits[idx >> 3] & (0x80 >> (idx & 7))) == 0) {
                continue;
            }
            const int32_t bx = x - layer->buf_area.x1;
            row[bx >> 3] &= (uint8_t)~(0x80 >> (bx & 7));
        }
    }
}

static void draw_main(lv_event_t* e) {
    lv_obj_t* obj = (lv_obj_t*)lv_event_get_current_target(e);
    tt_reader_text_view_t* view = (tt_reader_text_view_t*)obj;
    if (view->layout == nullptr || view->page == nullptr || view->layout->font() == nullptr) {
        return;
    }
    lv_layer_t* layer = lv_event_get_layer(e);
    if (layer == nullptr || layer->draw_buf == nullptr || layer->draw_buf->data == nullptr) {
        return;
    }
    if (layer->color_format != LV_COLOR_FORMAT_I1) {
        LOG_E("Reader view: needs I1 layer, cf=%d", (int)layer->color_format);
        return;
    }

    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);
    lv_area_t clip;
    if (!lv_area_intersect(&clip, &coords, &layer->_clip_area)) {
        return;
    }
    if (!lv_area_intersect(&clip, &clip, &layer->buf_area)) {
        return;
    }

    const uint32_t startMs = millis();
    const TTReaderPageLayout& page = *view->page;
    TTFontLoader* font = view->layout->font();
    const int16_t ascent = view->layout->ascent();
    const int16_t lineHeight = view->layout->lineHeight();
    const uint32_t palette = LV_COLOR_INDEXED_PALETTE_SIZE(LV_COLOR_FORMAT_I1) * sizeof(lv_color32_t);
    uint8_t* pixels = layer->draw_buf->data + palette;
    const uint32_t stride = layer->draw_buf->header.stride;
    uint8_t bits[TT_READER_GLYPH_BUF_SIZE];
    uint16_t drawn = 0;

    for (uint8_t i = 0; i < page.lineCount; i++) {
        const TTReaderLine& line = page.lines[i];
        const int32_t top = coords.y1 + line.y;
        if (top > clip.y2 || top + lineHeight + TT_READER_LINE_GAP < clip.y1) {
            continue;
        }
        const int32_t baseline = top + ascent;
        int32_t x = coords.x1 + line.x;
        uint16_t gap = 0;
        size_t pos = line.textStart;
        const size_t end = (size_t)line.textStart + line.textLen;
        while (pos < end) {
            uint32_t cp = 0;
            const size_t n = tt_utf8_decode((const uint8_t*)page.text + pos, end - pos, &cp);
            pos += n > 0 ? n : 1;
            TTFontLoader::GlyphInfo info = {};
            if (!font->getGlyphInfo(cp, info)) {
                continue;
            }
            const int32_t gx = x + info.ofs_x;
            const int32_t gy = baseline - (info.ofs_y + info.box_h);
            const bool visible = info.box_w > 0 && info.box_h > 0
                && gx <= clip.x2 && gx + info.box_w > clip.x1
                && gy <= clip.y2 && gy + info.box_h > clip.y1;
            if (visible && font->getGlyphBitmap(info, bits, sizeof(bits))) {
                blit_glyph(layer, clip, pixels, stride, bits, gx, gy, info.box_w, info.box_h);
                drawn++;
            }
            x += info.adv_w;
            if (pos < end && (line.spaceGaps ? cp == ' ' : true)) {
                x += justify_extra(line, gap++);
            }
        }
    }
    LOG_D("Reader view: draw clip y=%d..%d glyphs=%u in %ums",
          (int)clip.y1, (int)clip.y2, (unsigned)drawn, (unsigned)(millis() - startMs));
}
