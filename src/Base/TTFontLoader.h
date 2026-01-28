#pragma once

#include <Arduino.h>
#include <LittleFS.h>
#include <Adafruit_GFX.h>
#include <lvgl.h>

// Maximum glyph bitmap size for A8 format (48x48 = 2304 bytes)
#define TT_FONT_GLYPH_BUF_SIZE 2304

class TTFontLoader {
public:
    TTFontLoader() {}
    ~TTFontLoader() { end(); }

    // Load font file(s)
    // path: main font file (e.g. Chinese font)
    // asciiPath: optional ASCII font file for better English rendering (0x20-0x7E)
    bool begin(const char* path, const char* asciiPath = nullptr);
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
        bool fromAsciiFont;   // True if this glyph is from ASCII font
    };

    // Public methods for LVGL callbacks
    bool getGlyphInfo(uint32_t unicode, GlyphInfo& info);
    bool getGlyphBitmap(const GlyphInfo& info, uint8_t* buf, size_t bufSize);
    int32_t getLineHeight() const { return _head.ascent - _head.descent; }
    int32_t getBaseLine() const { return -_head.descent; }
    
private:
    // Font file data structure (used for both main and ASCII fonts)
    struct FontData {
        File file;
        
        // Head table info
        struct {
            uint16_t ascent;
            int16_t descent;
            uint16_t def_adv_w;
            uint8_t bpp;
            uint8_t bits_x_y;
            uint8_t bits_w_h;
            uint8_t bits_adv;
            uint8_t adv_format;
            uint8_t loc_format;
            uint8_t compression;
        } head;
        
        // Table offsets
        uint32_t cmapOffset = 0;
        uint32_t locaOffset = 0;
        uint32_t glyfOffset = 0;
        
        // CMAP Cache
        struct CMAPSubtable {
            uint32_t dataOffset;
            uint32_t startUnicode;
            uint16_t length;
            uint16_t glyphIdOffset;
            uint16_t entriesCount;
            uint8_t type;
        };
        CMAPSubtable* cmaps = nullptr;
        uint16_t cmapCount = 0;
        
        // Bit-stream reader state
        uint8_t bitBuf = 0;
        uint8_t bitCount = 0;
        
        bool isLoaded() const { return (bool)file; }
    };
    
    FontData _main;     // Main font (Chinese)
    FontData _ascii;    // ASCII font (English)
    uint16_t _color = 0;

    // Shared LVGL font structure and glyph buffer
    lv_font_t _lvFont;
    uint8_t _glyphBuf[TT_FONT_GLYPH_BUF_SIZE];
    lv_draw_buf_t _drawBuf;

    // For compatibility with _head access
    decltype(_main.head)& _head = _main.head;

    // Font loading helper
    bool _loadFontData(FontData& fd, const char* path);
    void _freeFontData(FontData& fd);
    
    // Font data access helpers
    bool _seekToTable(FontData& fd, const char* tag);
    uint32_t _getGlyphID(FontData& fd, uint32_t unicode);
    uint32_t _getGlyphOffset(FontData& fd, uint32_t glyphId);
    uint32_t _readBits(FontData& fd, uint8_t bits);
    int32_t _readSignedBits(FontData& fd, uint8_t bits);
    void _resetBitReader(FontData& fd) { fd.bitCount = 0; fd.bitBuf = 0; }
    
    // Internal glyph info getter
    bool _getGlyphInfoFromFont(FontData& fd, uint32_t unicode, GlyphInfo& info);
    
    uint32_t _decodeUTF8(const char** s);

    // LVGL static callbacks
    static bool lvglGetGlyphDsc(const lv_font_t* font, lv_font_glyph_dsc_t* dsc, 
                                 uint32_t letter, uint32_t letter_next);
    static const void* lvglGetGlyphBitmap(lv_font_glyph_dsc_t* dsc, lv_draw_buf_t* buf);
    static void lvglReleaseGlyph(const lv_font_t* font, lv_font_glyph_dsc_t* dsc);
};
