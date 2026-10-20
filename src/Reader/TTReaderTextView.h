#pragma once

#include <lvgl.h>
#include "TTReaderLayout.h"

#define TT_READER_GLYPH_BUF_SIZE        512
#define TT_READER_ITALIC_SLANT_X1000    200
#define TT_READER_ITALIC_SCALE          1000
#define TT_READER_VIEW_EXT_DRAW         4
#define TT_READER_UNDERLINE_DY          3
#define TT_READER_STRIKE_DY             5
#define TT_READER_EMPHASIS_DY           3
#define TT_READER_EMPHASIS_THICK        2
#define TT_READER_CODE_DASH_ON          4
#define TT_READER_CODE_DASH_OFF         2

lv_obj_t* tt_reader_text_view_create(lv_obj_t* parent);
void tt_reader_text_view_set_page(lv_obj_t* obj, TTReaderLayout* layout, const TTReaderPageLayout* page);
