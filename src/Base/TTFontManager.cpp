#include "TTFontManager.h"
#include "Logger.h"
#include "TTFile.h"
#include <cstring>

static const struct {
    int size;
    const char* path;
    const char* asciiPath;
} TT_FONT_ENTRIES[] = {
    { 10, TT_FS_FONT_DIR "/all_10.bin", nullptr },
    { 12, TT_FS_FONT_DIR "/all_12.bin", nullptr },
    { 16, TT_FS_FONT_DIR "/all_16.bin", nullptr },
    { 32, TT_FS_RES_DIR "/fonts/en_32.bin", nullptr },
    { 40, TT_FS_RES_DIR "/fonts/en_40.bin", nullptr },
    { 48, TT_FS_RES_DIR "/fonts/en_48.bin", nullptr },
    { 120, TT_FS_RES_DIR "/fonts/en_120.bin", nullptr },
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

void TTFontManager::releaseResFonts() {
    const char* prefix = TT_FS_RES_DIR "/";
    const size_t prefixLen = strlen(prefix);
    for (size_t i = 0; i < TT_FONT_ENTRIES_COUNT; i++) {
        const char* path = TT_FONT_ENTRIES[i].path;
        if (strncmp(path, prefix, prefixLen) != 0) {
            continue;
        }
        auto it = _fonts.find(TT_FONT_ENTRIES[i].size);
        if (it == _fonts.end()) {
            continue;
        }
        LOG_I("Font: release %d %s", TT_FONT_ENTRIES[i].size, path);
        _fonts.erase(it);
    }
}

lv_font_t* TTFontManager::getFont(int size) {
    auto it = _fonts.find(size);
    return (it != _fonts.end()) ? it->second->getLvglFont() : nullptr;
}
