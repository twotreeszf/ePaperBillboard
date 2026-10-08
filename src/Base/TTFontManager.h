#pragma once

#include <lvgl.h>
#include <map>
#include <memory>
#include "TTFontLoader.h"
#include "TTInstance.h"

#define TT_FONT_GLYPH_CACHE_CJK    64
#define TT_FONT_GLYPH_CACHE_LATIN  24

class TTFontManager {
public:
    static TTFontManager& instance() { return TTInstanceOf<TTFontManager>(); }

    bool begin();
    void releaseResFonts();

    lv_font_t* getFont(int size);

private:
    std::map<int, std::unique_ptr<TTFontLoader>> _fonts;
};
