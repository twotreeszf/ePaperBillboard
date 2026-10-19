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
    lv_obj_refresh_ext_draw_size(obj);
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
    const lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_REFR_EXT_DRAW_SIZE) {
        lv_event_set_ext_draw_size(e, TT_READER_VIEW_EXT_DRAW);
    } else if (code == LV_EVENT_DRAW_MAIN) {
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

struct TTReaderCanvas {
    const lv_layer_t* layer;
    lv_area_t clip;
    uint8_t* pixels;
    uint32_t stride;
};

static int32_t italic_shift(int32_t height) {
    if (height >= 0) {
        return (height * TT_READER_ITALIC_SLANT_X1000 + TT_READER_ITALIC_SCALE / 2) / TT_READER_ITALIC_SCALE;
    }
    return -((-height * TT_READER_ITALIC_SLANT_X1000 + TT_READER_ITALIC_SCALE / 2) / TT_READER_ITALIC_SCALE);
}

static void put_pixel(const TTReaderCanvas& canvas, int32_t x, int32_t y) {
    if (x < canvas.clip.x1 || x > canvas.clip.x2 || y < canvas.clip.y1 || y > canvas.clip.y2) {
        return;
    }
    uint8_t* row = canvas.pixels + (uint32_t)(y - canvas.layer->buf_area.y1) * canvas.stride;
    const int32_t bx = x - canvas.layer->buf_area.x1;
    row[bx >> 3] &= (uint8_t)~(0x80 >> (bx & 7));
}

static void draw_vline(const TTReaderCanvas& canvas, int32_t x, int32_t y1, int32_t y2) {
    if (x < canvas.clip.x1 || x > canvas.clip.x2) {
        return;
    }
    const int32_t from = y1 > canvas.clip.y1 ? y1 : canvas.clip.y1;
    const int32_t to = y2 < canvas.clip.y2 ? y2 : canvas.clip.y2;
    for (int32_t y = from; y <= to; y++) {
        put_pixel(canvas, x, y);
    }
}

static void draw_line(const TTReaderCanvas& canvas, int32_t x1, int32_t y1, int32_t x2, int32_t y2) {
    const int32_t dx = x2 >= x1 ? x2 - x1 : x1 - x2;
    const int32_t dy = y2 >= y1 ? y2 - y1 : y1 - y2;
    const int32_t sx = x1 < x2 ? 1 : -1;
    const int32_t sy = y1 < y2 ? 1 : -1;
    int32_t err = dx - dy;
    while (true) {
        put_pixel(canvas, x1, y1);
        if (x1 == x2 && y1 == y2) {
            break;
        }
        const int32_t e2 = err * 2;
        if (e2 > -dy) {
            err -= dy;
            x1 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y1 += sy;
        }
    }
}

static void draw_hline(const TTReaderCanvas& canvas, int32_t x1, int32_t x2, int32_t y) {
    if (y < canvas.clip.y1 || y > canvas.clip.y2) {
        return;
    }
    for (int32_t x = x1; x <= x2; x++) {
        put_pixel(canvas, x, y);
    }
}

static void draw_task_box(const TTReaderCanvas& canvas, int32_t x, int32_t baseline, bool done) {
    const int32_t y1 = baseline - TT_READER_TASK_BOX + 1;
    const int32_t x2 = x + TT_READER_TASK_BOX - 1;
    const int32_t y2 = baseline;
    draw_hline(canvas, x, x2, y1);
    draw_hline(canvas, x, x2, y2);
    draw_vline(canvas, x, y1, y2);
    draw_vline(canvas, x2, y1, y2);
    if (!done) {
        return;
    }
    draw_line(canvas, x + 2, y1 + TT_READER_TASK_BOX / 2, x + TT_READER_TASK_BOX / 3, y2 - 2);
    draw_line(canvas, x + TT_READER_TASK_BOX / 3, y2 - 2, x2 - 2, y1 + 2);
}

static bool dash_on(int32_t step) {
    return step % (TT_READER_CODE_DASH_ON + TT_READER_CODE_DASH_OFF) < TT_READER_CODE_DASH_ON;
}

static void draw_dashed_hline(const TTReaderCanvas& canvas, int32_t x1, int32_t x2, int32_t y) {
    if (y < canvas.clip.y1 || y > canvas.clip.y2) {
        return;
    }
    for (int32_t x = x1; x <= x2; x++) {
        if (dash_on(x - x1)) {
            put_pixel(canvas, x, y);
        }
    }
}

static void draw_dashed_vline(const TTReaderCanvas& canvas, int32_t x, int32_t y1, int32_t y2) {
    const int32_t from = y1 > canvas.clip.y1 ? y1 : canvas.clip.y1;
    const int32_t to = y2 < canvas.clip.y2 ? y2 : canvas.clip.y2;
    for (int32_t y = from; y <= to; y++) {
        if (dash_on(y - y1)) {
            put_pixel(canvas, x, y);
        }
    }
}

static void draw_code_boxes(const TTReaderCanvas& canvas, const TTReaderPageLayout& page,
                            const lv_area_t& coords, int16_t width) {
    uint8_t i = 0;
    while (i < page.lineCount) {
        const TTReaderLine& first = page.lines[i];
        if (first.kind != TT_READER_LINE_CODE) {
            i++;
            continue;
        }
        uint8_t last = i;
        while (!(page.lines[last].flags & TT_READER_LINE_FLAG_BOX_BOTTOM) && last + 1 < page.lineCount
               && page.lines[last + 1].kind == TT_READER_LINE_CODE
               && !(page.lines[last + 1].flags & TT_READER_LINE_FLAG_BOX_TOP)) {
            last++;
        }
        const bool top = (first.flags & TT_READER_LINE_FLAG_BOX_TOP) != 0;
        const bool bottom = (page.lines[last].flags & TT_READER_LINE_FLAG_BOX_BOTTOM) != 0;
        const int32_t y1 = coords.y1 + first.y - (top ? TT_READER_CODE_INSET_Y : 0);
        const int32_t y2 = coords.y1 + page.lines[last].y + TT_READER_CODE_PITCH + TT_READER_CODE_PAD_Y;
        const int32_t x1 = coords.x1;
        const int32_t x2 = coords.x1 + width - 1;
        if (y1 <= canvas.clip.y2 && y2 >= canvas.clip.y1) {
            if (top) {
                draw_dashed_hline(canvas, x1, x2, y1);
            }
            if (bottom) {
                draw_dashed_hline(canvas, x1, x2, y2);
            }
            draw_dashed_vline(canvas, x1, y1, y2);
            draw_dashed_vline(canvas, x2, y1, y2);
        }
        i = (uint8_t)(last + 1);
    }
}

static void blit_glyph(const TTReaderCanvas& canvas, const uint8_t* bits, const TTFontLoader::GlyphInfo& info,
                       int32_t gx, int32_t gy, bool italic) {
    const int32_t w = info.box_w;
    const int32_t h = info.box_h;
    for (int32_t r = 0; r < h; r++) {
        const int32_t y = gy + r;
        if (y < canvas.clip.y1 || y > canvas.clip.y2) {
            continue;
        }
        const int32_t shift = italic ? italic_shift(info.ofs_y + h - 1 - r) : 0;
        for (int32_t c = 0; c < w; c++) {
            const int32_t idx = r * w + c;
            if ((bits[idx >> 3] & (0x80 >> (idx & 7))) != 0) {
                put_pixel(canvas, gx + c + shift, y);
            }
        }
    }
}

struct TTReaderDecoration {
    uint8_t flag;
    int32_t y;
    bool active;
    int32_t x1;
    int32_t x2;
};

static void decoration_flush(const TTReaderCanvas& canvas, TTReaderDecoration& deco) {
    if (deco.active) {
        draw_hline(canvas, deco.x1, deco.x2 - 1, deco.y);
        deco.active = false;
    }
}

static void decoration_extend(TTReaderDecoration& deco, uint8_t style, uint32_t cp, int32_t x, int32_t adv) {
    if (!(style & deco.flag) || cp == ' ') {
        return;
    }
    if (!deco.active) {
        deco.active = true;
        deco.x1 = x;
    }
    deco.x2 = x + adv;
}

static bool draw_glyph(const TTReaderCanvas& canvas, TTFontLoader* font, uint8_t* bits, uint32_t cp,
                       int32_t x, int32_t baseline, uint8_t style, TTFontLoader::GlyphInfo& info) {
    info = {};
    if (!font->getGlyphInfo(cp, info)) {
        return false;
    }
    const int32_t gx = x + info.ofs_x;
    const int32_t gy = baseline - (info.ofs_y + info.box_h);
    const bool visible = info.box_w > 0 && info.box_h > 0
        && gx - TT_READER_VIEW_EXT_DRAW <= canvas.clip.x2
        && gx + info.box_w + TT_READER_VIEW_EXT_DRAW > canvas.clip.x1
        && gy <= canvas.clip.y2 && gy + info.box_h > canvas.clip.y1;
    if (visible && font->getGlyphBitmap(info, bits, TT_READER_GLYPH_BUF_SIZE)) {
        const bool italic = (style & TT_READER_STYLE_ITALIC) != 0;
        blit_glyph(canvas, bits, info, gx, gy, italic);
        if ((style & TT_READER_STYLE_BOLD) && cp != ' ') {
            blit_glyph(canvas, bits, info, gx + TT_READER_BOLD_EXTRA, gy, italic);
        }
    }
    return true;
}

static void draw_main(lv_event_t* e) {
    lv_obj_t* obj = (lv_obj_t*)lv_event_get_current_target(e);
    tt_reader_text_view_t* view = (tt_reader_text_view_t*)obj;
    if (view->layout == nullptr || view->page == nullptr || view->layout->font() == nullptr
        || view->layout->codeFont() == nullptr) {
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
    lv_area_t area = coords;
    area.x1 -= TT_READER_VIEW_EXT_DRAW;
    area.x2 += TT_READER_VIEW_EXT_DRAW;
    TTReaderCanvas canvas;
    if (!lv_area_intersect(&canvas.clip, &area, &layer->_clip_area)) {
        return;
    }
    if (!lv_area_intersect(&canvas.clip, &canvas.clip, &layer->buf_area)) {
        return;
    }
    const uint32_t palette = LV_COLOR_INDEXED_PALETTE_SIZE(LV_COLOR_FORMAT_I1) * sizeof(lv_color32_t);
    canvas.layer = layer;
    canvas.pixels = layer->draw_buf->data + palette;
    canvas.stride = layer->draw_buf->header.stride;

    const uint32_t startMs = millis();
    const TTReaderPageLayout& page = *view->page;
    TTFontLoader* textFont = view->layout->font();
    TTFontLoader* codeFont = view->layout->codeFont();
    const int16_t ascent = view->layout->ascent();
    const int16_t lineHeight = view->layout->lineHeight();
    const int16_t codeBaseline = view->layout->codeBaseline();
    uint8_t bits[TT_READER_GLYPH_BUF_SIZE];
    uint16_t drawn = 0;

    draw_code_boxes(canvas, page, coords, view->layout->width());

    for (uint8_t i = 0; i < page.lineCount; i++) {
        const TTReaderLine& line = page.lines[i];
        if (line.kind == TT_READER_LINE_VRULE) {
            const int32_t y1 = coords.y1 + line.y;
            const int32_t y2 = coords.y1 + line.markX;
            if (y1 <= canvas.clip.y2 && y2 >= canvas.clip.y1) {
                draw_vline(canvas, coords.x1 + line.x, y1, y2);
            }
            continue;
        }
        const int32_t top = coords.y1 + line.y;
        if (top > canvas.clip.y2 || top + lineHeight + TT_READER_LINE_GAP < canvas.clip.y1) {
            continue;
        }
        if (line.kind == TT_READER_LINE_RULE) {
            if ((line.flags & TT_READER_LINE_FLAG_EXACT) != 0) {
                draw_hline(canvas, coords.x1 + line.x, coords.x1 + line.markX, top);
            } else {
                draw_hline(canvas, coords.x1, coords.x1 + view->layout->width() - 1, top + lineHeight / 2);
            }
            continue;
        }
        const bool small = line.kind == TT_READER_LINE_CODE || line.kind == TT_READER_LINE_TABLE;
        const int32_t baseline = top + (small ? codeBaseline : ascent);
        TTFontLoader* font = small ? codeFont : textFont;
        TTFontLoader::GlyphInfo info = {};
        if (line.kind == TT_READER_LINE_BULLET) {
            draw_glyph(canvas, font, bits, TT_READER_BULLET_GLYPH, coords.x1 + line.markX, baseline, 0, info);
        } else if (line.kind == TT_READER_LINE_TASK) {
            draw_task_box(canvas, coords.x1 + line.markX, baseline,
                          (line.flags & TT_READER_LINE_FLAG_TASK_DONE) != 0);
        }
        TTReaderDecoration underline = { TT_READER_STYLE_UNDERLINE, baseline + TT_READER_UNDERLINE_DY, false, 0, 0 };
        TTReaderDecoration strike = { TT_READER_STYLE_STRIKE, baseline - TT_READER_STRIKE_DY, false, 0, 0 };
        int32_t x = coords.x1 + line.x;
        uint8_t style = line.style;
        uint16_t count = 0;
        uint16_t gap = 0;
        size_t pos = line.textStart;
        const size_t end = (size_t)line.textStart + line.textLen;
        while (pos < end) {
            if ((uint8_t)page.text[pos] == TT_READER_STYLE_MARK && pos + 1 < end) {
                const uint8_t next = (uint8_t)page.text[pos + 1] & TT_READER_STYLE_MASK;
                if (count > 0 && (style & TT_READER_STYLE_ITALIC) && !(next & TT_READER_STYLE_ITALIC)) {
                    x += TT_READER_ITALIC_TAIL;
                }
                if (!(next & TT_READER_STYLE_UNDERLINE)) {
                    decoration_flush(canvas, underline);
                }
                if (!(next & TT_READER_STYLE_STRIKE)) {
                    decoration_flush(canvas, strike);
                }
                style = next;
                pos += TT_READER_STYLE_MARK_BYTES;
                continue;
            }
            uint32_t cp = 0;
            const size_t n = tt_utf8_decode((const uint8_t*)page.text + pos, end - pos, &cp);
            pos += n > 0 ? n : 1;
            if (!draw_glyph(canvas, font, bits, cp, x, baseline, style, info)) {
                continue;
            }
            drawn++;
            const bool bold = (style & TT_READER_STYLE_BOLD) && cp != ' ';
            const int32_t adv = info.adv_w + (bold ? TT_READER_BOLD_EXTRA : 0);
            decoration_extend(underline, style, cp, x, adv);
            decoration_extend(strike, style, cp, x, adv);
            x += adv;
            count++;
            if (pos < end && (line.spaceGaps ? cp == ' ' : true)) {
                x += justify_extra(line, gap++);
            }
        }
        decoration_flush(canvas, underline);
        decoration_flush(canvas, strike);
    }
    LOG_D("Reader view: draw clip y=%d..%d glyphs=%u in %ums",
          (int)canvas.clip.y1, (int)canvas.clip.y2, (unsigned)drawn, (unsigned)(millis() - startMs));
}
