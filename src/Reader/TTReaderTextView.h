#pragma once

#include <lvgl.h>
#include "TTReaderLayout.h"

#define TT_READER_GLYPH_BUF_SIZE  512

lv_obj_t* tt_reader_text_view_create(lv_obj_t* parent);
void tt_reader_text_view_set_page(lv_obj_t* obj, TTReaderLayout* layout, const TTReaderPageLayout* page);
