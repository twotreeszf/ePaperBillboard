#pragma once

#include <Arduino.h>
#include "TTReaderSource.h"
#include "../Base/TTFontLoader.h"

#define TT_READER_LAYOUT_VERSION    1
#define TT_READER_PAGE_LINES        11
#define TT_READER_LINE_GAP          7
#define TT_READER_PARA_GAP          0
#define TT_READER_JUSTIFY_MAX_GAP   12
#define TT_READER_INDENT_CHARS      2
#define TT_READER_INDENT_REF_CHAR   0x4E2D
#define TT_READER_MAX_LINES         24
#define TT_READER_PAGE_TEXT_MAX     2048
#define TT_READER_LINE_BYTES_MAX    512
#define TT_READER_MISSING_GLYPH     0x25A1
#define TT_READER_FALLBACK_GLYPH    '?'
#define TT_READER_BOM_LEN           3

struct TTReaderLine {
    uint16_t textStart;
    uint16_t textLen;
    int16_t x;
    int16_t y;
    uint16_t extra;
    uint16_t gaps;
    bool spaceGaps;
};

struct TTReaderPageLayout {
    uint32_t start = 0;
    uint32_t end = 0;
    bool atEnd = false;
    uint8_t lineCount = 0;
    uint16_t textLen = 0;
    uint16_t missingGlyphs = 0;
    TTReaderLine lines[TT_READER_MAX_LINES];
    char text[TT_READER_PAGE_TEXT_MAX];
};

class TTReaderLayout {
public:
    bool begin(TTFontLoader* font, int16_t width, int16_t height);
    bool layoutPage(TTReaderSource& src, uint32_t start, TTReaderPageLayout& page);
    bool glyph(uint32_t cp, uint32_t& shown, TTFontLoader::GlyphInfo& info);

    TTFontLoader* font() const { return _font; }
    uint32_t configHash() const { return _configHash; }
    int16_t ascent() const { return _ascent; }
    int16_t lineHeight() const { return _lineHeight; }
    int16_t width() const { return _width; }
    int16_t height() const { return _height; }

private:
    enum LineEnd {
        TT_LINE_END_OVERFLOW = 0,
        TT_LINE_END_PARAGRAPH,
        TT_LINE_END_EOF,
        TT_LINE_END_BUFFER,
    };

    bool isParagraphStart(TTReaderSource& src, uint32_t offset);
    uint32_t skipSpaces(TTReaderSource& src, uint32_t offset, bool paragraph);
    LineEnd fillLine(TTReaderSource& src, uint32_t& offset, int16_t x0, TTReaderPageLayout& page, TTReaderLine& line);
    bool appendText(TTReaderPageLayout& page, uint32_t cp);

    TTFontLoader* _font = nullptr;
    int16_t _width = 0;
    int16_t _height = 0;
    int16_t _ascent = 0;
    int16_t _lineHeight = 0;
    int16_t _indent = 0;
    uint16_t _spaceAdv = 0;
    uint32_t _configHash = 0;
};
