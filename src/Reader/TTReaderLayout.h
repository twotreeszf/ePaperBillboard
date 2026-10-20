#pragma once

#include <Arduino.h>
#include "TTReaderSource.h"
#include "TTReaderMarkdown.h"
#include "../Base/TTFontLoader.h"

#define TT_READER_LAYOUT_VERSION    1
#define TT_READER_PAGE_LINES        10
#define TT_READER_LINE_GAP          10
#define TT_READER_PARA_GAP          0
#define TT_READER_JUSTIFY_MAX_GAP   12
#define TT_READER_INDENT_CHARS      2
#define TT_READER_INDENT_REF_CHAR   0x4E2D
#define TT_READER_MAX_LINES         64
#define TT_READER_PAGE_TEXT_MAX     2048
#define TT_READER_LINE_BYTES_MAX    512
#define TT_READER_MISSING_GLYPH     0x25A1
#define TT_READER_FALLBACK_GLYPH    '?'
#define TT_READER_BOM_LEN           3
#define TT_READER_BOLD_EXTRA        1
#define TT_READER_ITALIC_TAIL       2
#define TT_READER_BULLET_GLYPH      0x2022
#define TT_READER_CODE_FONT_SIZE    12
#define TT_READER_CODE_PITCH_X10    12
#define TT_READER_CODE_PITCH        ((TT_READER_CODE_FONT_SIZE * TT_READER_CODE_PITCH_X10 + 5) / 10)
#define TT_READER_CODE_PAD_X        4
#define TT_READER_CODE_PAD_Y        6
#define TT_READER_CODE_BORDER       1
#define TT_READER_CODE_INSET_X      (TT_READER_CODE_BORDER + TT_READER_CODE_PAD_X)
#define TT_READER_CODE_INSET_Y      (TT_READER_CODE_BORDER + TT_READER_CODE_PAD_Y)
#define TT_READER_LINE_FLAG_BOX_TOP     0x01
#define TT_READER_LINE_FLAG_BOX_BOTTOM  0x02
#define TT_READER_LINE_FLAG_EXACT       0x04
#define TT_READER_LINE_FLAG_TASK_DONE   0x08
#define TT_READER_TASK_BOX              10
#define TT_READER_TABLE_MIN_CHARS       2
#define TT_READER_TABLE_PAD_X           3
#define TT_READER_TABLE_PAD_Y           4
#define TT_READER_TABLE_BORDER          1
#define TT_READER_TABLE_BANDS_MAX       32

enum TTReaderLineKind : uint8_t {
    TT_READER_LINE_TEXT = 0,
    TT_READER_LINE_BULLET,
    TT_READER_LINE_RULE,
    TT_READER_LINE_CODE,
    TT_READER_LINE_TABLE,
    TT_READER_LINE_VRULE,
    TT_READER_LINE_TASK,
};

struct TTReaderLine {
    uint16_t textStart;
    uint16_t textLen;
    int16_t x;
    int16_t y;
    uint16_t extra;
    uint16_t gaps;
    bool spaceGaps;
    uint8_t style;
    TTReaderLineKind kind;
    uint8_t flags;
    int16_t markX;
};

struct TTReaderPageLayout {
    uint32_t start = 0;
    uint32_t end = 0;
    bool atEnd = false;
    char image[TT_MD_IMAGE_NAME_MAX] = {};
    uint8_t lineCount = 0;
    uint16_t textLen = 0;
    uint16_t missingGlyphs = 0;
    TTReaderLine lines[TT_READER_MAX_LINES];
    char text[TT_READER_PAGE_TEXT_MAX];
};

class TTReaderLayout {
public:
    bool begin(TTFontLoader* font, TTFontLoader* codeFont, int16_t width, int16_t height, bool markdown);
    bool layoutPage(TTReaderSource& src, uint32_t start, TTReaderPageLayout& page);

    TTFontLoader* font() const { return _font; }
    TTFontLoader* codeFont() const { return _codeFont; }
    uint32_t configHash() const { return _configHash; }
    int16_t ascent() const { return _ascent; }
    int16_t lineHeight() const { return _lineHeight; }
    int16_t codeBaseline() const { return _codeBaseline; }
    int16_t width() const { return _width; }
    int16_t height() const { return _height; }
    bool markdown() const { return _markdown; }

private:
    enum LineEnd {
        TT_LINE_END_OVERFLOW = 0,
        TT_LINE_END_PARAGRAPH,
        TT_LINE_END_EOF,
        TT_LINE_END_BUFFER,
    };

    enum TableLay : uint8_t {
        TT_TABLE_NONE = 0,
        TT_TABLE_DONE,
        TT_TABLE_BREAK,
        TT_TABLE_FAIL,
    };

    bool glyph(TTFontLoader* font, uint32_t cp, uint32_t& shown, TTFontLoader::GlyphInfo& info);
    bool isParagraphStart(TTReaderSource& src, uint32_t offset);
    uint32_t skipSpaces(TTReaderSource& src, uint32_t offset, bool paragraph);
    LineEnd fillLine(TTReaderSource& src, uint32_t& offset, int16_t x0, TTReaderPageLayout& page,
                     TTReaderLine& line, TTMdCursor* md);
    bool appendText(TTReaderPageLayout& page, uint32_t cp, bool mark = false, uint8_t style = 0);
    bool beginMarkdown(TTReaderSource& src, uint32_t& offset, bool paragraph, bool& joined,
                       TTMdCursor& cursor, int16_t& contX);
    bool markdownTailEmpty(TTReaderSource& src, uint32_t offset);
    int16_t blockX(const TTMdBlock& block, bool first) const;
    int32_t textWidth(const char* text, uint16_t len, uint8_t style);
    int16_t codeX(uint8_t columns) const;
    uint8_t cellStyle(const TTMdInline& inl, bool header) const;
    int32_t cellWidth(TTReaderSource& src, uint32_t start, uint32_t end, bool header);
    bool appendRule(TTReaderPageLayout& page, TTReaderLineKind kind, int16_t x, int16_t y, int16_t mark);
    bool appendTableCell(TTReaderSource& src, const TTMdCell& cell, int16_t x, int16_t colW, bool header,
                         uint8_t bandCap, TTReaderPageLayout& page, uint8_t& bands, bool& clipped);
    TableLay layoutTable(TTReaderSource& src, uint32_t offset, TTReaderPageLayout& page, int16_t& y, uint32_t& end);

    TTFontLoader* _font = nullptr;
    TTFontLoader* _codeFont = nullptr;
    int16_t _width = 0;
    int16_t _height = 0;
    int16_t _ascent = 0;
    int16_t _lineHeight = 0;
    int16_t _indent = 0;
    int16_t _unit = 0;
    int16_t _bulletX = 0;
    uint16_t _spaceAdv = 0;
    uint16_t _codeSpaceAdv = 0;
    uint16_t _codeUnit = 0;
    int16_t _codeBaseline = 0;
    uint32_t _configHash = 0;
    bool _markdown = false;
};
