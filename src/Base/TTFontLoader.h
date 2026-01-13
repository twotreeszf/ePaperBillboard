#pragma once

#include <Arduino.h>
#include <LittleFS.h>
#include <Adafruit_GFX.h>

class TTFontLoader {
public:
    TTFontLoader() {}
    ~TTFontLoader() { end(); }

    bool begin(const char* path);
    void end();

    void setTextColor(uint16_t color) { _color = color; }
    void drawUTF8(Adafruit_GFX& gfx, int16_t x, int16_t y, const char* text);

private:
    File _file;
    uint16_t _color = 0;

    // Head table info
    struct {
        uint16_t ascent;
        int16_t descent;
        uint8_t bpp;
        uint8_t bits_x_y;
        uint8_t bits_w_h;
        uint8_t bits_adv;
        uint8_t adv_format; // 0: uint, 1: FP12.4
        uint8_t loc_format; // 0: 16bit, 1: 32bit
        uint8_t compression;
    } _head;

    // Table offsets
    uint32_t _cmapOffset = 0;
    uint32_t _locaOffset = 0;
    uint32_t _glyfOffset = 0;

    // CMAP Cache
    struct CMAPSubtable {
        uint32_t dataOffset;
        uint32_t startUnicode;
        uint16_t length;
        uint16_t glyphIdOffset;
        uint16_t entriesCount;
        uint8_t type;
    };
    CMAPSubtable* _cmaps = nullptr;
    uint16_t _cmapCount = 0;

    // Bit-stream reader state
    uint8_t _bitBuf = 0;
    uint8_t _bitCount = 0;

    bool _seekToTable(const char* tag);
    uint32_t _getGlyphID(uint32_t unicode);
    uint32_t _readBits(uint8_t bits);
    int32_t _readSignedBits(uint8_t bits);
    void _resetBitReader() { _bitCount = 0; _bitBuf = 0; }
    
    uint32_t _decodeUTF8(const char** s);
};
