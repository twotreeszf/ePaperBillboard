#pragma once

#include <Arduino.h>
#include <LittleFS.h>
#include <Adafruit_GFX.h>
#include <lvgl.h>

// Maximum glyph bitmap size for A8 format (24x24 = 576 bytes)
#define TT_FONT_GLYPH_BUF_SIZE 576

class TTFontLoader {
public:
    TTFontLoader() {}
    ~TTFontLoader() { end(); }

    bool begin(const char* path);
    void end();

    // GFX direct drawing (legacy)
    void setTextColor(uint16_t color) { _color = color; }
    void drawUTF8(Adafruit_GFX& gfx, int16_t x, int16_t y, const char* text);

    // LVGL font interface
    lv_font_t* getLvglFont() { return &_lvFont; }

    // Glyph info for LVGL
    struct GlyphInfo {
        uint16_t adv_w;
        uint16_t box_w;
        uint16_t box_h;
        int16_t ofs_x;
        int16_t ofs_y;
        uint32_t glyfOffset;  // File offset to bitmap data
        uint8_t bitmapBits;   // Bits consumed by header (for bitmap start)
    };

    // Public methods for LVGL callbacks
    bool getGlyphInfo(uint32_t unicode, GlyphInfo& info);
    bool getGlyphBitmap(const GlyphInfo& info, uint8_t* buf, size_t bufSize);
    int32_t getLineHeight() const { return _head.ascent - _head.descent; }
    int32_t getBaseLine() const { return -_head.descent; }

private:
    File _file;
    uint16_t _color = 0;

    // LVGL font structure
    lv_font_t _lvFont;
    uint8_t _glyphBuf[TT_FONT_GLYPH_BUF_SIZE];
    lv_draw_buf_t _drawBuf;  // LVGL draw buffer for glyph bitmap

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
    uint32_t _getGlyphOffset(uint32_t glyphId);

    // LVGL static callbacks
    static bool lvglGetGlyphDsc(const lv_font_t* font, lv_font_glyph_dsc_t* dsc, 
                                 uint32_t letter, uint32_t letter_next);
    static const void* lvglGetGlyphBitmap(lv_font_glyph_dsc_t* dsc, lv_draw_buf_t* buf);
    static void lvglReleaseGlyph(const lv_font_t* font, lv_font_glyph_dsc_t* dsc);
};
