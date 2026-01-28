#include "TTFontLoader.h"
#include "Base/Logger.h"

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
    fd.file = LittleFS.open(path, "r");
    if (!fd.file) return false;

    // 1. Parse HEAD
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
    fd.cmaps = new FontData::CMAPSubtable[fd.cmapCount];
    for (int i = 0; i < fd.cmapCount; i++) {
        fd.file.read((uint8_t*)&fd.cmaps[i].dataOffset, 4);
        fd.file.read((uint8_t*)&fd.cmaps[i].startUnicode, 4);
        fd.file.read((uint8_t*)&fd.cmaps[i].length, 2);
        fd.file.read((uint8_t*)&fd.cmaps[i].glyphIdOffset, 2);
        fd.file.read((uint8_t*)&fd.cmaps[i].entriesCount, 2);
        fd.file.read(&fd.cmaps[i].type, 1);
        fd.file.seek(fd.file.position() + 1);
    }

    // 3. Locate tables
    if (_seekToTable(fd, "loca")) fd.locaOffset = fd.file.position() - 8;
    if (_seekToTable(fd, "glyf")) fd.glyfOffset = fd.file.position() - 8;

    return true;
}

void TTFontLoader::_freeFontData(FontData& fd) {
    if (fd.cmaps) {
        delete[] fd.cmaps;
        fd.cmaps = nullptr;
    }
    fd.cmapCount = 0;
    fd.file.close();
}

bool TTFontLoader::begin(const char* path, const char* asciiPath) {
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

    LOG_I("Font Ready: Asc=%d, Des=%d, LineH=%d, CMAPs=%d, BPP=%d",
        _main.head.ascent, _main.head.descent, getLineHeight(), 
        _main.cmapCount, _main.head.bpp);
    return true;
}

void TTFontLoader::end() {
    _freeFontData(_ascii);
    _freeFontData(_main);
}

uint32_t TTFontLoader::_getGlyphID(FontData& fd, uint32_t unicode) {
    for (int i = 0; i < fd.cmapCount; i++) {
        if (unicode >= fd.cmaps[i].startUnicode && 
            unicode < fd.cmaps[i].startUnicode + fd.cmaps[i].length) {
            uint32_t offset = unicode - fd.cmaps[i].startUnicode;
            uint32_t dataBase = fd.cmapOffset + fd.cmaps[i].dataOffset;
            
            if (fd.cmaps[i].type == 0 || fd.cmaps[i].type == 2) {
                if (fd.cmaps[i].type == 2) return fd.cmaps[i].glyphIdOffset + offset;
                fd.file.seek(dataBase + offset);
                return fd.cmaps[i].glyphIdOffset + fd.file.read();
            } else if (fd.cmaps[i].type == 1 || fd.cmaps[i].type == 3) {
                for (uint16_t j = 0; j < fd.cmaps[i].entriesCount; j++) {
                    fd.file.seek(dataBase + j * 2);
                    uint16_t diff; fd.file.read((uint8_t*)&diff, 2);
                    if (diff == offset) {
                        if (fd.cmaps[i].type == 3) return fd.cmaps[i].glyphIdOffset + j;
                        fd.file.seek(dataBase + fd.cmaps[i].entriesCount * 2 + j * 2);
                        uint16_t gid; fd.file.read((uint8_t*)&gid, 2);
                        return gid;
                    }
                }
            }
        }
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
    // For ASCII characters (0x20-0x7E), try ASCII font first
    if (unicode >= 0x20 && unicode <= 0x7E && _ascii.isLoaded()) {
        if (_getGlyphInfoFromFont(_ascii, unicode, info)) {
            info.fromAsciiFont = true;
            return true;
        }
    }
    
    // Fall back to main font
    if (_getGlyphInfoFromFont(_main, unicode, info)) {
        info.fromAsciiFont = false;
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
    LV_UNUSED(draw_buf);
    
    if (!dsc || !dsc->resolved_font) return nullptr;
    
    TTFontLoader* loader = (TTFontLoader*)dsc->resolved_font->dsc;
    if (!loader) return nullptr;
    
    // Extract unicode and fromAsciiFont flag
    uint32_t letter = dsc->gid.index & 0x7FFFFFFF;
    bool fromAsciiFont = (dsc->gid.index & 0x80000000) != 0;
    
    // Select the correct font data
    FontData& fd = fromAsciiFont ? loader->_ascii : loader->_main;
    if (!fd.file) return nullptr;
    
    // Get glyph info from the correct font
    GlyphInfo info;
    if (!loader->_getGlyphInfoFromFont(fd, letter, info)) {
        return nullptr;
    }
    
    uint32_t a8Size = info.box_w * info.box_h;
    if (a8Size > sizeof(loader->_glyphBuf)) {
        LOG_E("Glyph too large: %ux%u = %u bytes", info.box_w, info.box_h, a8Size);
        return nullptr;
    }
    
    // Read bitmap from the correct font file
    fd.file.seek(info.glyfOffset);
    loader->_resetBitReader(fd);
    loader->_readBits(fd, info.bitmapBits);
    
    // Convert A1 to A8
    uint8_t* dst = loader->_glyphBuf;
    for (uint32_t i = 0; i < a8Size; i++) {
        uint8_t bit = loader->_readBits(fd, fd.head.bpp);
        *dst++ = bit ? 0xFF : 0x00;
    }
    
    // Setup draw buffer
    memset(&loader->_drawBuf, 0, sizeof(loader->_drawBuf));
    loader->_drawBuf.header.magic = LV_IMAGE_HEADER_MAGIC;
    loader->_drawBuf.header.cf = LV_COLOR_FORMAT_A8;
    loader->_drawBuf.header.w = info.box_w;
    loader->_drawBuf.header.h = info.box_h;
    loader->_drawBuf.header.stride = info.box_w;
    loader->_drawBuf.data_size = a8Size;
    loader->_drawBuf.data = loader->_glyphBuf;
    
    return &loader->_drawBuf;
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
