#include "TTFontManager.h"
#include "Logger.h"

static const struct {
    int size;
    const char* path;
    const char* asciiPath;
} TT_FONT_ENTRIES[] = {
    { 10, "/fonts/all_10.bin", nullptr },
    { 12, "/fonts/all_12.bin", nullptr },
    { 16, "/fonts/all_16.bin", nullptr },
    { 48, "/fonts/en_48.bin", nullptr },
};
#define TT_FONT_ENTRIES_COUNT  (sizeof(TT_FONT_ENTRIES) / sizeof(TT_FONT_ENTRIES[0]))

bool TTFontManager::begin() {
    bool ok = true;
    for (size_t i = 0; i < TT_FONT_ENTRIES_COUNT; i++) {
        int size = TT_FONT_ENTRIES[i].size;
        const char* path = TT_FONT_ENTRIES[i].path;
        const char* asciiPath = TT_FONT_ENTRIES[i].asciiPath;
        std::unique_ptr<TTFontLoader> loader(new TTFontLoader());
        if (loader->begin(path, asciiPath)) {
            LOG_I("Font %d loaded%s", size, asciiPath ? " (dual)" : "");
            _fonts[size] = std::move(loader);
        } else {
            LOG_W("Failed to load font %d", size);
            ok = false;
        }
    }
    return ok;
}

lv_font_t* TTFontManager::getFont(int size) {
    auto it = _fonts.find(size);
    return (it != _fonts.end()) ? it->second->getLvglFont() : nullptr;
}
