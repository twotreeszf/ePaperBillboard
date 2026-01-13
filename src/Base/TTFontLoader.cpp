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

    // 3. Locate tables (offsets are relative to table start, not data start)
    if (_seekToTable("loca")) _locaOffset = _file.position() - 8;
    if (_seekToTable("glyf")) _glyfOffset = _file.position() - 8;

    LOG_I("Font Ready: Asc=%d, CMAPs=%d, LocFmt=%d, AdvBits=%d", 
        _head.ascent, _cmapCount, _head.loc_format, _head.bits_adv);
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

void TTFontLoader::drawUTF8(Adafruit_GFX& gfx, int16_t x, int16_t y, const char* text) {
    if (!_file) return;
    const char* p = text;
    int16_t curX = x;
    while (*p) {
        uint32_t unicode = _decodeUTF8(&p);
        uint32_t gid = _getGlyphID(unicode);
        
        uint32_t gOffset;
        // loca data starts after 8-byte table header + 4-byte entries count
        uint32_t locaDataStart = _locaOffset + 12;
        
        if (_head.loc_format == 0) {
            _file.seek(locaDataStart + gid * 2);
            uint16_t off16; _file.read((uint8_t*)&off16, 2);
            gOffset = (uint32_t)off16;
        } else {
            _file.seek(locaDataStart + gid * 4);
            _file.read((uint8_t*)&gOffset, 4);
        }

        _file.seek(_glyfOffset + gOffset);
        _resetBitReader();
        uint32_t adv_w = _readBits(_head.bits_adv);
        int32_t box_x = _readSignedBits(_head.bits_x_y);
        int32_t box_y = _readSignedBits(_head.bits_x_y);
        uint32_t box_w = _readBits(_head.bits_w_h);
        uint32_t box_h = _readBits(_head.bits_w_h);

        int16_t drawX = curX + box_x;
        // y is the top of the text line
        // LVGL formula: glyph_top_y = baseline - bearingY - box_h
        int16_t baseline = y + _head.ascent;
        int16_t drawY = baseline - box_y - box_h; 

        for (uint32_t h = 0; h < box_h; h++) {
            for (uint32_t w = 0; w < box_w; w++) {
                if (_readBits(1)) {
                    gfx.drawPixel(drawX + w, drawY + h, _color);
                }
            }
        }
        
        if (_head.adv_format == 1) curX += (adv_w + 8) >> 4;
        else curX += adv_w;
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
