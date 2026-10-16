#include "TTFontLoader.h"
#include "TTFile.h"
#include "Base/Logger.h"
#include <new>

bool TTFontLoader::_seekToTable(FontData& fd, const char* tag) {
    fd.file.seek(0);
    while (fd.file.available() >= 8) {
        uint32_t length;
        char name[4];
        uint32_t startPos = fd.file.position();
        if (fd.file.read((uint8_t*)&length, 4) != 4) break;
        if (fd.file.read((uint8_t*)name, 4) != 4) break;
        if (memcmp(name, tag, 4) == 0) return true;
        fd.file.seek(startPos + length);
    }
    return false;
}

bool TTFontLoader::_loadFontData(FontData& fd, const char* path) {
    fd.file = tt_file_open(path, "r");
    if (!fd.file) return false;

    if (!_seekToTable(fd, "head")) return false;
    uint32_t headStart = fd.file.position() - 8;
    fd.file.seek(headStart + 8 + 8);
    fd.file.read((uint8_t*)&fd.head.ascent, 2);
    fd.file.read((uint8_t*)&fd.head.descent, 2);
    fd.file.seek(headStart + 8 + 22);
    fd.file.read((uint8_t*)&fd.head.def_adv_w, 2);
    fd.file.seek(headStart + 8 + 26);
    fd.file.read(&fd.head.loc_format, 1);
    fd.file.seek(headStart + 8 + 28);
    fd.file.read(&fd.head.adv_format, 1);
    fd.file.read(&fd.head.bpp, 1);
    fd.file.read(&fd.head.bits_x_y, 1);
    fd.file.read(&fd.head.bits_w_h, 1);
    fd.file.read(&fd.head.bits_adv, 1);
    fd.file.read(&fd.head.compression, 1);

    // 2. Parse CMAP
    if (!_seekToTable(fd, "cmap")) return false;
    fd.cmapOffset = fd.file.position() - 8;
    uint32_t subtablesCount;
    fd.file.read((uint8_t*)&subtablesCount, 4);
    fd.cmapCount = (uint16_t)subtablesCount;
    _loadCmapRanges(fd);

    // 3. Locate tables
    if (_seekToTable(fd, "loca")) fd.locaOffset = fd.file.position() - 8;
    if (_seekToTable(fd, "glyf")) fd.glyfOffset = fd.file.position() - 8;

    return true;
}

void TTFontLoader::_freeFontData(FontData& fd) {
    fd.cmapCount = 0;
    fd.cmapStarts.reset();
    fd.cmapLengths.reset();
    fd.file.close();
}

void TTFontLoader::_loadCmapRanges(FontData& fd) {
    fd.cmapStarts.reset();
    fd.cmapLengths.reset();
    if (fd.cmapCount == 0) {
        return;
    }
    std::unique_ptr<uint32_t[]> starts(new (std::nothrow) uint32_t[fd.cmapCount]);
    std::unique_ptr<uint16_t[]> lengths(new (std::nothrow) uint16_t[fd.cmapCount]);
    if (!starts || !lengths) {
        LOG_W("Font cmap ranges alloc failed: %u subtables, linear lookup", (unsigned)fd.cmapCount);
        return;
    }
    if (!fd.file.seek(fd.cmapOffset + 12)) {
        LOG_W("Font cmap ranges seek failed, linear lookup");
        return;
    }
    uint8_t rec[TT_FONT_CMAP_RECORD_SIZE];
    for (uint16_t i = 0; i < fd.cmapCount; i++) {
        if (fd.file.read(rec, sizeof(rec)) != sizeof(rec)) {
            LOG_W("Font cmap ranges read failed at %u, linear lookup", (unsigned)i);
            return;
        }
        memcpy(&starts[i], rec + 4, 4);
        memcpy(&lengths[i], rec + 8, 2);
        if (i > 0 && starts[i] < starts[i - 1] + lengths[i - 1]) {
            LOG_W("Font cmap subtable %u not sorted, linear lookup", (unsigned)i);
            return;
        }
    }
    fd.cmapStarts = std::move(starts);
    fd.cmapLengths = std::move(lengths);
    LOG_I("Font cmap ranges cached: %u subtables, %u bytes",
          (unsigned)fd.cmapCount, (unsigned)(fd.cmapCount * (sizeof(uint32_t) + sizeof(uint16_t))));
}

int32_t TTFontLoader::_findCmapSubtable(FontData& fd, uint32_t unicode) {
    if (fd.cmapStarts && fd.cmapLengths) {
        int32_t lo = 0;
        int32_t hi = (int32_t)fd.cmapCount - 1;
        int32_t found = -1;
        while (lo <= hi) {
            const int32_t mid = (lo + hi) / 2;
            if (fd.cmapStarts[mid] <= unicode) {
                found = mid;
                lo = mid + 1;
            } else {
                hi = mid - 1;
            }
        }
        if (found >= 0 && unicode < fd.cmapStarts[found] + fd.cmapLengths[found]) {
            return found;
        }
        return -1;
    }
    FontData::CMAPSubtable cmap;
    for (uint16_t i = 0; i < fd.cmapCount; i++) {
        if (!_readCmapSubtable(fd, i, cmap)) {
            return -1;
        }
        if (unicode >= cmap.startUnicode && unicode < cmap.startUnicode + cmap.length) {
            return i;
        }
    }
    return -1;
}

bool TTFontLoader::_readCmapSubtable(FontData& fd, uint16_t index, FontData::CMAPSubtable& out) {
    if (index >= fd.cmapCount) {
        return false;
    }
    const uint32_t recOff = fd.cmapOffset + 12 + (uint32_t)index * TT_FONT_CMAP_RECORD_SIZE;
    if (!fd.file.seek(recOff)) {
        return false;
    }
    if (fd.file.read((uint8_t*)&out.dataOffset, 4) != 4) return false;
    if (fd.file.read((uint8_t*)&out.startUnicode, 4) != 4) return false;
    if (fd.file.read((uint8_t*)&out.length, 2) != 2) return false;
    if (fd.file.read((uint8_t*)&out.glyphIdOffset, 2) != 2) return false;
    if (fd.file.read((uint8_t*)&out.entriesCount, 2) != 2) return false;
    if (fd.file.read(&out.type, 1) != 1) return false;
    return true;
}

void TTFontLoader::_glyphCacheClear() {
    _glyphCacheCount = 0;
    _glyphCacheNext = 0;
}

bool TTFontLoader::_glyphCacheGet(uint32_t unicode, GlyphInfo& info) {
    for (uint16_t i = 0; i < _glyphCacheCount; i++) {
        if (_glyphCache[i].unicode == unicode) {
            info = _glyphCache[i].info;
            return true;
        }
    }
    return false;
}

void TTFontLoader::_glyphCachePut(uint32_t unicode, const GlyphInfo& info) {
    if (_glyphCacheCapacity == 0) {
        return;
    }
    uint16_t slot;
    if (_glyphCacheCount < _glyphCacheCapacity) {
        slot = _glyphCacheCount++;
    } else {
        slot = _glyphCacheNext;
        _glyphCacheNext = (uint16_t)((_glyphCacheNext + 1) % _glyphCacheCapacity);
    }
    _glyphCache[slot].unicode = unicode;
    _glyphCache[slot].info = info;
}

bool TTFontLoader::begin(const char* path, const char* asciiPath, uint16_t glyphCacheCapacity) {
    _glyphCacheClear();
    if (_glyphCacheCapacity != glyphCacheCapacity) {
        _glyphCache.reset(glyphCacheCapacity > 0 ? new (std::nothrow) GlyphCacheEntry[glyphCacheCapacity] : nullptr);
        _glyphCacheCapacity = _glyphCache ? glyphCacheCapacity : 0;
        if (glyphCacheCapacity > 0 && !_glyphCache) {
            LOG_W("Font glyph cache alloc failed: %u entries", (unsigned)glyphCacheCapacity);
        }
    }

    // Load main font
    if (!_loadFontData(_main, path)) {
        return false;
    }

    // Load ASCII font if specified
    if (asciiPath) {
        if (_loadFontData(_ascii, asciiPath)) {
            LOG_I("ASCII font loaded: %s", asciiPath);
        } else {
            LOG_W("Failed to load ASCII font: %s", asciiPath);
        }
    }

    // Initialize LVGL font structure (use main font metrics)
    memset(&_lvFont, 0, sizeof(_lvFont));
    _lvFont.get_glyph_dsc = lvglGetGlyphDsc;
    _lvFont.get_glyph_bitmap = lvglGetGlyphBitmap;
    _lvFont.release_glyph = lvglReleaseGlyph;
    _lvFont.line_height = getLineHeight();
    _lvFont.base_line = getBaseLine();
    _lvFont.subpx = LV_FONT_SUBPX_NONE;
    _lvFont.underline_position = -2;
    _lvFont.underline_thickness = 1;
    _lvFont.dsc = this;

    LOG_I("Font Ready: Asc=%d, Des=%d, LineH=%d, CMAPs=%d, BPP=%d, Cache=%u (%u bytes)",
        _main.head.ascent, _main.head.descent, getLineHeight(), 
        _main.cmapCount, _main.head.bpp,
        (unsigned)_glyphCacheCapacity, (unsigned)(_glyphCacheCapacity * sizeof(GlyphCacheEntry)));
    return true;
}

void TTFontLoader::end() {
    _glyphCacheClear();
    _glyphCache.reset();
    _glyphCacheCapacity = 0;
    _freeFontData(_ascii);
    _freeFontData(_main);
}

uint32_t TTFontLoader::_getGlyphID(FontData& fd, uint32_t unicode) {
    const int32_t index = _findCmapSubtable(fd, unicode);
    if (index < 0) {
        return 0;
    }
    FontData::CMAPSubtable cmap;
    if (!_readCmapSubtable(fd, (uint16_t)index, cmap)) {
        return 0;
    }
    return _glyphIdInSubtable(fd, cmap, unicode);
}

uint32_t TTFontLoader::_glyphIdInSubtable(FontData& fd, const FontData::CMAPSubtable& cmap, uint32_t unicode) {
    const uint32_t offset = unicode - cmap.startUnicode;
    const uint32_t dataBase = fd.cmapOffset + cmap.dataOffset;

    if (cmap.type == 2) {
        return cmap.glyphIdOffset + offset;
    }
    if (cmap.type == 0) {
        fd.file.seek(dataBase + offset);
        return cmap.glyphIdOffset + fd.file.read();
    }
    if (cmap.type != 1 && cmap.type != 3) {
        return 0;
    }
    int32_t lo = 0;
    int32_t hi = (int32_t)cmap.entriesCount - 1;
    while (lo <= hi) {
        const int32_t mid = (lo + hi) / 2;
        fd.file.seek(dataBase + (uint32_t)mid * 2);
        uint16_t diff = 0;
        if (fd.file.read((uint8_t*)&diff, 2) != 2) {
            return 0;
        }
        if (diff < offset) {
            lo = mid + 1;
            continue;
        }
        if (diff > offset) {
            hi = mid - 1;
            continue;
        }
        if (cmap.type == 3) {
            return cmap.glyphIdOffset + (uint32_t)mid;
        }
        fd.file.seek(dataBase + (uint32_t)cmap.entriesCount * 2 + (uint32_t)mid * 2);
        uint16_t gid = 0;
        fd.file.read((uint8_t*)&gid, 2);
        return gid;
    }
    return 0;
}

uint32_t TTFontLoader::_getGlyphOffset(FontData& fd, uint32_t glyphId) {
    uint32_t locaDataStart = fd.locaOffset + 12;
    uint32_t gOffset;
    
    if (fd.head.loc_format == 0) {
        fd.file.seek(locaDataStart + glyphId * 2);
        uint16_t off16; fd.file.read((uint8_t*)&off16, 2);
        gOffset = (uint32_t)off16;
    } else {
        fd.file.seek(locaDataStart + glyphId * 4);
        fd.file.read((uint8_t*)&gOffset, 4);
    }
    return gOffset;
}

uint32_t TTFontLoader::_readBits(FontData& fd, uint8_t bits) {
    if (bits == 0) return 0;
    uint32_t res = 0;
    for (int i = 0; i < bits; i++) {
        if (fd.bitCount == 0) {
            fd.bitBuf = fd.file.read();
            fd.bitCount = 8;
        }
        res <<= 1;
        if (fd.bitBuf & 0x80) res |= 1;
        fd.bitBuf <<= 1;
        fd.bitCount--;
    }
    return res;
}

int32_t TTFontLoader::_readSignedBits(FontData& fd, uint8_t bits) {
    if (bits == 0) return 0;
    uint32_t val = _readBits(fd, bits);
    if (val & (1 << (bits - 1))) return (int32_t)val - (1 << bits);
    return val;
}

bool TTFontLoader::_getGlyphInfoFromFont(FontData& fd, uint32_t unicode, GlyphInfo& info) {
    if (!fd.file) return false;
        
    uint32_t gid = _getGlyphID(fd, unicode);
    if (gid == 0 && unicode != 0) return false;
    
    uint32_t gOffset = _getGlyphOffset(fd, gid);
    
    fd.file.seek(fd.glyfOffset + gOffset);
    _resetBitReader(fd);
    
    uint32_t adv_w;
    if (fd.head.bits_adv > 0) {
        adv_w = _readBits(fd, fd.head.bits_adv);
        if (fd.head.adv_format == 1) {
            adv_w = (adv_w + 8) >> 4;
        }
    } else {
        adv_w = fd.head.def_adv_w;
    }
    
    int32_t box_x = _readSignedBits(fd, fd.head.bits_x_y);
    int32_t box_y = _readSignedBits(fd, fd.head.bits_x_y);
    uint32_t box_w = _readBits(fd, fd.head.bits_w_h);
    uint32_t box_h = _readBits(fd, fd.head.bits_w_h);
    
    uint8_t headerBits = fd.head.bits_adv + fd.head.bits_x_y * 2 + fd.head.bits_w_h * 2;
    
    info.adv_w = adv_w;
    info.box_w = box_w;
    info.box_h = box_h;
    info.ofs_x = box_x;
    info.ofs_y = box_y;
    info.glyfOffset = fd.glyfOffset + gOffset;
    info.bitmapBits = headerBits;
    
    return true;
}

bool TTFontLoader::getGlyphInfo(uint32_t unicode, GlyphInfo& info) {
    if (_glyphCacheGet(unicode, info)) {
        return true;
    }

    if (_ascii.isLoaded()) {
        if (_getGlyphInfoFromFont(_ascii, unicode, info)) {
            info.fromAsciiFont = true;
            _glyphCachePut(unicode, info);
            return true;
        }
    }

    if (_getGlyphInfoFromFont(_main, unicode, info)) {
        info.fromAsciiFont = false;
        _glyphCachePut(unicode, info);
        return true;
    }

    return false;
}

bool TTFontLoader::getGlyphBitmap(const GlyphInfo& info, uint8_t* buf, size_t bufSize) {
    FontData& fd = info.fromAsciiFont ? _ascii : _main;
    if (!fd.file || !buf) return false;
    
    uint32_t bitmapBits = info.box_w * info.box_h * fd.head.bpp;
    uint32_t bitmapBytes = (bitmapBits + 7) / 8;
    
    if (bitmapBytes > bufSize) {
        LOG_E("Glyph buffer too small: need %u, have %u", bitmapBytes, bufSize);
        return false;
    }
    
    fd.file.seek(info.glyfOffset);
    _resetBitReader(fd);
    _readBits(fd, info.bitmapBits);
    
    memset(buf, 0, bitmapBytes);
    for (uint32_t i = 0; i < bitmapBits; i++) {
        uint8_t bit = _readBits(fd, fd.head.bpp);
        uint32_t byteIdx = i / 8;
        uint32_t bitIdx = 7 - (i % 8);
        if (bit) {
            buf[byteIdx] |= (1 << bitIdx);
        }
    }
    
    return true;
}

// LVGL callback: get glyph descriptor
bool TTFontLoader::lvglGetGlyphDsc(const lv_font_t* font, lv_font_glyph_dsc_t* dsc, 
                                    uint32_t letter, uint32_t letter_next) {
    LV_UNUSED(letter_next);
    
    TTFontLoader* loader = (TTFontLoader*)font->dsc;
    if (!loader) return false;
    
    GlyphInfo info;
    if (!loader->getGlyphInfo(letter, info)) {
        return false;
    }
    
    dsc->box_w = info.box_w;
    dsc->box_h = info.box_h;
    dsc->ofs_y = info.ofs_y;
    dsc->adv_w = info.adv_w;
    dsc->ofs_x = info.ofs_x;
    
    dsc->format = LV_FONT_GLYPH_FORMAT_A8;
    dsc->is_placeholder = 0;
    dsc->resolved_font = font;
    // Store fromAsciiFont flag in gid.index high bit
    dsc->gid.index = letter | (info.fromAsciiFont ? 0x80000000 : 0);
    
    return true;
}

// LVGL callback: get glyph bitmap
const void* TTFontLoader::lvglGetGlyphBitmap(lv_font_glyph_dsc_t* dsc, lv_draw_buf_t* draw_buf) {
    if (!dsc || !dsc->resolved_font) return nullptr;
    if (!draw_buf || !draw_buf->data) {
        LOG_E("Glyph draw buffer missing");
        return nullptr;
    }
    
    TTFontLoader* loader = (TTFontLoader*)dsc->resolved_font->dsc;
    if (!loader) return nullptr;
    
    uint32_t letter = dsc->gid.index & 0x7FFFFFFF;
    GlyphInfo info;
    if (!loader->getGlyphInfo(letter, info)) {
        return nullptr;
    }
    
    FontData& fd = info.fromAsciiFont ? loader->_ascii : loader->_main;
    if (!fd.file) return nullptr;
    
    const uint32_t stride = draw_buf->header.stride;
    if (draw_buf->header.w < info.box_w || draw_buf->header.h < info.box_h
        || stride < info.box_w || stride * info.box_h > draw_buf->data_size) {
        LOG_E("Glyph %ux%u exceeds draw buffer %ux%u stride=%u size=%u",
              info.box_w, info.box_h, draw_buf->header.w, draw_buf->header.h,
              (unsigned)stride, (unsigned)draw_buf->data_size);
        return nullptr;
    }

    fd.file.seek(info.glyfOffset);
    loader->_resetBitReader(fd);
    loader->_readBits(fd, info.bitmapBits);

    for (uint32_t y = 0; y < info.box_h; y++) {
        uint8_t* dst = draw_buf->data + y * stride;
        for (uint32_t x = 0; x < info.box_w; x++) {
            dst[x] = loader->_readBits(fd, fd.head.bpp) ? 0xFF : 0x00;
        }
    }

    return draw_buf;
}

// LVGL callback: release glyph
void TTFontLoader::lvglReleaseGlyph(const lv_font_t* font, lv_font_glyph_dsc_t* dsc) {
    LV_UNUSED(font);
    LV_UNUSED(dsc);
}

void TTFontLoader::drawUTF8(Adafruit_GFX& gfx, int16_t x, int16_t y, const char* text) {
    if (!_main.file) return;
    const char* p = text;
    int16_t curX = x;
    while (*p) {
        uint32_t unicode = _decodeUTF8(&p);
        
        GlyphInfo info;
        if (!getGlyphInfo(unicode, info)) continue;
        
        FontData& fd = info.fromAsciiFont ? _ascii : _main;

        int16_t drawX = curX + info.ofs_x;
        int16_t baseline = y + _main.head.ascent;
        int16_t drawY = baseline - (info.ofs_y + info.box_h);

        fd.file.seek(info.glyfOffset);
        _resetBitReader(fd);
        _readBits(fd, info.bitmapBits);
        
        for (uint32_t h = 0; h < info.box_h; h++) {
            for (uint32_t w = 0; w < info.box_w; w++) {
                if (_readBits(fd, fd.head.bpp)) {
                    gfx.drawPixel(drawX + w, drawY + h, _color);
                }
            }
        }
        
        curX += info.adv_w;
    }
}

uint32_t TTFontLoader::_decodeUTF8(const char** s) {
    uint32_t c = (unsigned char)**s;
    if (!c) return 0;
    (*s)++;
    if (c < 0x80) return c;
    if ((c & 0xE0) == 0xC0) c = ((c & 0x1F) << 6) | ((unsigned char)(*(*s)++) & 0x3F);
    else if ((c & 0xF0) == 0xE0) {
        c = ((c & 0x0F) << 12);
        c |= ((unsigned char)(*(*s)++) & 0x3F) << 6;
        c |= ((unsigned char)(*(*s)++) & 0x3F);
    } else if ((c & 0xF8) == 0xF0) {
        c = ((c & 0x07) << 18);
        c |= ((unsigned char)(*(*s)++) & 0x3F) << 12;
        c |= ((unsigned char)(*(*s)++) & 0x3F) << 6;
        c |= ((unsigned char)(*(*s)++) & 0x3F);
    }
    return c;
}
