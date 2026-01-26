#include "TTFontLoader.h"
#include "Base/Logger.h"

bool TTFontLoader::_seekToTable(const char* tag) {
    _file.seek(0);
    while (_file.available() >= 8) {
        uint32_t length;
        char name[4];
        uint32_t startPos = _file.position();
        if (_file.read((uint8_t*)&length, 4) != 4) break;
        if (_file.read((uint8_t*)name, 4) != 4) break;
        if (memcmp(name, tag, 4) == 0) return true;
        _file.seek(startPos + length);
    }
    return false;
}

bool TTFontLoader::begin(const char* path) {
    _file = LittleFS.open(path, "r");
    if (!_file) return false;

    // 1. Parse HEAD
    if (!_seekToTable("head")) return false;
    uint32_t headStart = _file.position() - 8;
    _file.seek(headStart + 16); 
    _file.read((uint8_t*)&_head.ascent, 2);
    _file.read((uint8_t*)&_head.descent, 2);
    _file.seek(headStart + 34);
    _file.read(&_head.loc_format, 1);
    _file.seek(headStart + 36);
    _file.read(&_head.adv_format, 1);
    _file.read(&_head.bpp, 1);
    _file.read(&_head.bits_x_y, 1);
    _file.read(&_head.bits_w_h, 1);
    _file.read(&_head.bits_adv, 1);
    _file.read(&_head.compression, 1);

    // 2. Parse CMAP
    if (!_seekToTable("cmap")) return false;
    _cmapOffset = _file.position() - 8;
    uint32_t subtablesCount;
    _file.read((uint8_t*)&subtablesCount, 4);
    _cmapCount = (uint16_t)subtablesCount;
    _cmaps = new CMAPSubtable[_cmapCount];
    for (int i = 0; i < _cmapCount; i++) {
        _file.read((uint8_t*)&_cmaps[i].dataOffset, 4);
        _file.read((uint8_t*)&_cmaps[i].startUnicode, 4);
        _file.read((uint8_t*)&_cmaps[i].length, 2);
        _file.read((uint8_t*)&_cmaps[i].glyphIdOffset, 2);
        _file.read((uint8_t*)&_cmaps[i].entriesCount, 2);
        _file.read(&_cmaps[i].type, 1);
        _file.seek(_file.position() + 1);
    }

    // 3. Locate tables
    if (_seekToTable("loca")) _locaOffset = _file.position() - 8;
    if (_seekToTable("glyf")) _glyfOffset = _file.position() - 8;

    // 4. Initialize LVGL font structure
    memset(&_lvFont, 0, sizeof(_lvFont));
    _lvFont.get_glyph_dsc = lvglGetGlyphDsc;
    _lvFont.get_glyph_bitmap = lvglGetGlyphBitmap;
    _lvFont.release_glyph = lvglReleaseGlyph;
    _lvFont.line_height = getLineHeight();
    _lvFont.base_line = getBaseLine();
    _lvFont.subpx = LV_FONT_SUBPX_NONE;
    _lvFont.underline_position = -2;
    _lvFont.underline_thickness = 1;
    _lvFont.dsc = this;  // Store 'this' pointer for callbacks

    LOG_I("Font Ready: Asc=%d, Des=%d, LineH=%d, CMAPs=%d, BPP=%d", 
        _head.ascent, _head.descent, getLineHeight(), _cmapCount, _head.bpp);
    return true;
}

uint32_t TTFontLoader::_getGlyphID(uint32_t unicode) {
    for (int i = 0; i < _cmapCount; i++) {
        if (unicode >= _cmaps[i].startUnicode && unicode < _cmaps[i].startUnicode + _cmaps[i].length) {
            uint32_t offset = unicode - _cmaps[i].startUnicode;
            uint32_t dataBase = _cmapOffset + _cmaps[i].dataOffset;
            
            if (_cmaps[i].type == 0 || _cmaps[i].type == 2) {
                if (_cmaps[i].type == 2) return _cmaps[i].glyphIdOffset + offset;
                _file.seek(dataBase + offset);
                return _cmaps[i].glyphIdOffset + _file.read();
            } else if (_cmaps[i].type == 1 || _cmaps[i].type == 3) {
                // Sparse search
                for (uint16_t j = 0; j < _cmaps[i].entriesCount; j++) {
                    _file.seek(dataBase + j * 2);
                    uint16_t diff; _file.read((uint8_t*)&diff, 2);
                    if (diff == offset) {
                        if (_cmaps[i].type == 3) return _cmaps[i].glyphIdOffset + j;
                        _file.seek(dataBase + _cmaps[i].entriesCount * 2 + j * 2);
                        uint16_t gid; _file.read((uint8_t*)&gid, 2);
                        return gid;
                    }
                }
            }
        }
    }
    return 0;
}

uint32_t TTFontLoader::_getGlyphOffset(uint32_t glyphId) {
    uint32_t locaDataStart = _locaOffset + 12;
    uint32_t gOffset;
    
    if (_head.loc_format == 0) {
        _file.seek(locaDataStart + glyphId * 2);
        uint16_t off16; _file.read((uint8_t*)&off16, 2);
        gOffset = (uint32_t)off16;
    } else {
        _file.seek(locaDataStart + glyphId * 4);
        _file.read((uint8_t*)&gOffset, 4);
    }
    return gOffset;
}

bool TTFontLoader::getGlyphInfo(uint32_t unicode, GlyphInfo& info) {
    if (!_file) return false;
        
    uint32_t gid = _getGlyphID(unicode);
    if (gid == 0 && unicode != 0) return false;
    
    uint32_t gOffset = _getGlyphOffset(gid);
    
    _file.seek(_glyfOffset + gOffset);
    _resetBitReader();
    
    uint32_t adv_w = _readBits(_head.bits_adv);
    int32_t box_x = _readSignedBits(_head.bits_x_y);
    int32_t box_y = _readSignedBits(_head.bits_x_y);
    uint32_t box_w = _readBits(_head.bits_w_h);
    uint32_t box_h = _readBits(_head.bits_w_h);
    
    // Calculate header bits consumed
    uint8_t headerBits = _head.bits_adv + _head.bits_x_y * 2 + _head.bits_w_h * 2;
    
    // Convert advance width if FP12.4 format
    if (_head.adv_format == 1) {
        adv_w = (adv_w + 8) >> 4;
    }
    
    info.adv_w = adv_w;
    info.box_w = box_w;
    info.box_h = box_h;
    info.ofs_x = box_x;
    // LVGL ofs_y should equal box_y (bearingY) from the font file
    // LVGL formula: glyph_top_y = baseline - ofs_y - box_h
    // Original formula: drawY = baseline - box_y - box_h
    // So ofs_y = box_y
    info.ofs_y = box_y;
    info.glyfOffset = _glyfOffset + gOffset;
    info.bitmapBits = headerBits;
    
    return true;
}

bool TTFontLoader::getGlyphBitmap(const GlyphInfo& info, uint8_t* buf, size_t bufSize) {
    if (!_file || !buf) return false;
    
    // Calculate bitmap size in bytes
    uint32_t bitmapBits = info.box_w * info.box_h * _head.bpp;
    uint32_t bitmapBytes = (bitmapBits + 7) / 8;
    
    if (bitmapBytes > bufSize) {
        LOG_E("Glyph buffer too small: need %u, have %u", bitmapBytes, bufSize);
        return false;
    }
    
    // Seek to glyph data start
    _file.seek(info.glyfOffset);
    _resetBitReader();
    
    // Skip header bits
    _readBits(info.bitmapBits);
    
    // Read bitmap data bit by bit, pack into bytes (MSB first)
    memset(buf, 0, bitmapBytes);
    for (uint32_t i = 0; i < bitmapBits; i++) {
        uint8_t bit = _readBits(_head.bpp);
        // Pack bits MSB first
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
    
    // Apply extra spacing for ASCII characters (unicode < 0x80)
    if (letter < 0x80) {
        dsc->adv_w = info.adv_w + 2;
        dsc->ofs_x = info.ofs_x;
    } else {
        dsc->adv_w = info.adv_w;
        dsc->ofs_x = info.ofs_x;
    }   
    
    dsc->format = LV_FONT_GLYPH_FORMAT_A8;
    dsc->is_placeholder = 0;
    dsc->resolved_font = font;
    dsc->gid.index = letter;
    
    return true;
}

// LVGL callback: get glyph bitmap
const void* TTFontLoader::lvglGetGlyphBitmap(lv_font_glyph_dsc_t* dsc, lv_draw_buf_t* draw_buf) {
    LV_UNUSED(draw_buf);
    
    if (!dsc || !dsc->resolved_font) return nullptr;
    
    TTFontLoader* loader = (TTFontLoader*)dsc->resolved_font->dsc;
    if (!loader) return nullptr;
    
    // Get glyph info again (we stored unicode in gid.index)
    GlyphInfo info;
    if (!loader->getGlyphInfo(dsc->gid.index, info)) {
        return nullptr;
    }
    
    // Check buffer size (A8 format needs box_w * box_h bytes)
    uint32_t a8Size = info.box_w * info.box_h;
    if (a8Size > sizeof(loader->_glyphBuf)) {
        LOG_E("Glyph too large: %ux%u = %u bytes", info.box_w, info.box_h, a8Size);
        return nullptr;
    }
    
    // Read bitmap directly from file and convert A1 to A8
    loader->_file.seek(info.glyfOffset);
    loader->_resetBitReader();
    loader->_readBits(info.bitmapBits);  // Skip header
    
    // Convert A1 to A8 (0 -> 0x00, 1 -> 0xFF)
    uint8_t* dst = loader->_glyphBuf;
    for (uint32_t i = 0; i < a8Size; i++) {
        uint8_t bit = loader->_readBits(loader->_head.bpp);
        *dst++ = bit ? 0xFF : 0x00;
    }
    
    // Setup draw buffer structure for LVGL
    uint32_t stride = info.box_w;  // A8 format: 1 byte per pixel
    
    memset(&loader->_drawBuf, 0, sizeof(loader->_drawBuf));
    loader->_drawBuf.header.magic = LV_IMAGE_HEADER_MAGIC;
    loader->_drawBuf.header.cf = LV_COLOR_FORMAT_A8;
    loader->_drawBuf.header.w = info.box_w;
    loader->_drawBuf.header.h = info.box_h;
    loader->_drawBuf.header.stride = stride;
    loader->_drawBuf.data_size = a8Size;
    loader->_drawBuf.data = loader->_glyphBuf;
    
    return &loader->_drawBuf;
}

// LVGL callback: release glyph (nothing to do for our implementation)
void TTFontLoader::lvglReleaseGlyph(const lv_font_t* font, lv_font_glyph_dsc_t* dsc) {
    LV_UNUSED(font);
    LV_UNUSED(dsc);
    // No dynamic allocation to free
}

void TTFontLoader::drawUTF8(Adafruit_GFX& gfx, int16_t x, int16_t y, const char* text) {
    if (!_file) return;
    const char* p = text;
    int16_t curX = x;
    while (*p) {
        uint32_t unicode = _decodeUTF8(&p);
        
        GlyphInfo info;
        if (!getGlyphInfo(unicode, info)) continue;

        int16_t drawX = curX + info.ofs_x;
        int16_t baseline = y + _head.ascent;
        int16_t drawY = baseline - (info.ofs_y + info.box_h);

        // Read and draw bitmap
        _file.seek(info.glyfOffset);
        _resetBitReader();
        _readBits(info.bitmapBits);  // Skip header
        
        for (uint32_t h = 0; h < info.box_h; h++) {
            for (uint32_t w = 0; w < info.box_w; w++) {
                if (_readBits(_head.bpp)) {
                    gfx.drawPixel(drawX + w, drawY + h, _color);
                }
            }
        }
        
        curX += info.adv_w;
    }
}

uint32_t TTFontLoader::_readBits(uint8_t bits) {
    if (bits == 0) return 0;
    uint32_t res = 0;
    for (int i = 0; i < bits; i++) {
        if (_bitCount == 0) {
            _bitBuf = _file.read();
            _bitCount = 8;
        }
        res <<= 1;
        if (_bitBuf & 0x80) res |= 1;
        _bitBuf <<= 1;
        _bitCount--;
    }
    return res;
}

int32_t TTFontLoader::_readSignedBits(uint8_t bits) {
    if (bits == 0) return 0;
    uint32_t val = _readBits(bits);
    if (val & (1 << (bits - 1))) return (int32_t)val - (1 << bits);
    return val;
}

void TTFontLoader::end() {
    if (_cmaps) delete[] _cmaps;
    _cmaps = nullptr;
    _file.close();
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
