#pragma once

#include <lvgl.h>
#include <map>
#include <memory>
#include "TTFontLoader.h"
#include "TTInstance.h"

class TTFontManager {
public:
    static TTFontManager& instance() { return TTInstanceOf<TTFontManager>(); }

    bool begin();

    lv_font_t* getFont(int size);

private:
    std::map<int, std::unique_ptr<TTFontLoader>> _fonts;
};
