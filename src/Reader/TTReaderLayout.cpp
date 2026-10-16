#include "TTReaderLayout.h"
#include "TTReaderText.h"
#include "../Base/Logger.h"

bool TTReaderLayout::begin(TTFontLoader* font, int16_t width, int16_t height) {
    _font = font;
    if (_font == nullptr || width <= 0 || height <= 0) {
        LOG_E("Reader layout: invalid font or size %dx%d", (int)width, (int)height);
        return false;
    }
    _width = width;
    _height = height;
    _lineHeight = (int16_t)_font->getLineHeight();
    _ascent = (int16_t)(_font->getLineHeight() - _font->getBaseLine());

    uint32_t shown = 0;
    TTFontLoader::GlyphInfo info = {};
    glyph(TT_READER_INDENT_REF_CHAR, shown, info);
    _indent = (int16_t)(TT_READER_INDENT_CHARS * info.adv_w);
    info = {};
    glyph(' ', shown, info);
    _spaceAdv = info.adv_w;

    const int32_t config[] = {
        TT_READER_LAYOUT_VERSION, _width, _height, _lineHeight, _ascent,
        TT_READER_LINE_GAP, TT_READER_PARA_GAP, TT_READER_JUSTIFY_MAX_GAP,
        _indent, _spaceAdv, TT_READER_MAX_LINES, TT_READER_PAGE_TEXT_MAX,
    };
    _configHash = tt_reader_hash(config, sizeof(config));
    LOG_I("Reader layout: %dx%d lineH=%d ascent=%d indent=%d space=%u hash=%08x",
          (int)_width, (int)_height, (int)_lineHeight, (int)_ascent, (int)_indent,
          (unsigned)_spaceAdv, (unsigned)_configHash);
    return true;
}

bool TTReaderLayout::glyph(uint32_t cp, uint32_t& shown, TTFontLoader::GlyphInfo& info) {
    if (_font->getGlyphInfo(cp, info)) {
        shown = cp;
        return true;
    }
    if (_font->getGlyphInfo(TT_READER_MISSING_GLYPH, info)) {
        shown = TT_READER_MISSING_GLYPH;
        return false;
    }
    if (_font->getGlyphInfo(TT_READER_FALLBACK_GLYPH, info)) {
        shown = TT_READER_FALLBACK_GLYPH;
        return false;
    }
    shown = 0;
    return false;
}

bool TTReaderLayout::isParagraphStart(TTReaderSource& src, uint32_t offset) {
    if (offset == 0) {
        return true;
    }
    uint8_t prev = 0;
    if (!src.byteAt(offset - 1, prev)) {
        return true;
    }
    return prev == '\n' || prev == '\r';
}

uint32_t TTReaderLayout::skipSpaces(TTReaderSource& src, uint32_t offset, bool paragraph) {
    while (offset < src.size()) {
        uint32_t next = offset;
        const uint32_t cp = src.next(next);
        if (src.failed()) {
            return src.size();
        }
        const bool skip = tt_reader_is_ignorable(cp)
            || (paragraph ? tt_reader_is_para_space(cp) : tt_reader_is_space(cp));
        if (!skip) {
            return offset;
        }
        offset = next;
    }
    return src.size();
}

bool TTReaderLayout::appendText(TTReaderPageLayout& page, uint32_t cp) {
    char bytes[TT_UTF8_MAX_BYTES];
    const size_t n = tt_utf8_encode(cp, bytes);
    if (page.textLen + n > TT_READER_PAGE_TEXT_MAX) {
        return false;
    }
    memcpy(page.text + page.textLen, bytes, n);
    page.textLen = (uint16_t)(page.textLen + n);
    return true;
}

TTReaderLayout::LineEnd TTReaderLayout::fillLine(TTReaderSource& src, uint32_t& offset, int16_t x0,
                                                 TTReaderPageLayout& page, TTReaderLine& line) {
    line.textStart = page.textLen;
    line.textLen = 0;
    line.x = x0;
    line.extra = 0;
    line.gaps = 0;
    line.spaceGaps = false;

    int32_t x = x0;
    uint16_t count = 0;
    uint16_t spaces = 0;
    bool hasBreak = false;
    uint32_t breakOffset = 0;
    uint16_t breakText = 0;
    int32_t breakX = 0;
    uint16_t breakCount = 0;
    uint16_t breakSpaces = 0;
    uint32_t prev = 0;
    LineEnd reason = TT_LINE_END_EOF;

    while (offset < src.size()) {
        const uint32_t charOffset = offset;
        uint32_t cp = src.next(offset);
        if (src.failed()) {
            reason = TT_LINE_END_EOF;
            break;
        }
        if (cp == '\r') {
            uint8_t b = 0;
            if (src.byteAt(offset, b) && b == '\n') {
                offset++;
            }
            reason = TT_LINE_END_PARAGRAPH;
            break;
        }
        if (cp == '\n') {
            reason = TT_LINE_END_PARAGRAPH;
            break;
        }
        if (tt_reader_is_ignorable(cp)) {
            continue;
        }
        if (cp == '\t') {
            cp = ' ';
        }
        if (count > 0 && tt_reader_can_break(prev, cp)) {
            hasBreak = true;
            breakOffset = charOffset;
            breakText = page.textLen;
            breakX = x;
            breakCount = count;
            breakSpaces = spaces;
        }
        uint32_t shown = 0;
        TTFontLoader::GlyphInfo info = {};
        if (!glyph(cp, shown, info)) {
            if (shown == 0) {
                continue;
            }
            page.missingGlyphs++;
        }
        const int32_t adv = info.adv_w;
        if (cp == ' ') {
            if (x + adv > _width) {
                reason = TT_LINE_END_OVERFLOW;
                break;
            }
            if (!appendText(page, ' ')) {
                offset = charOffset;
                reason = TT_LINE_END_BUFFER;
                break;
            }
            x += adv;
            count++;
            spaces++;
            prev = cp;
            continue;
        }
        const bool tooLong = page.textLen - line.textStart >= TT_READER_LINE_BYTES_MAX;
        if (count > 0 && (x + adv > _width || tooLong)) {
            if (hasBreak) {
                page.textLen = breakText;
                x = breakX;
                count = breakCount;
                spaces = breakSpaces;
                offset = breakOffset;
            } else {
                offset = charOffset;
            }
            reason = TT_LINE_END_OVERFLOW;
            break;
        }
        if (!appendText(page, shown)) {
            offset = charOffset;
            reason = TT_LINE_END_BUFFER;
            break;
        }
        x += adv;
        count++;
        prev = cp;
    }

    while (page.textLen > line.textStart && page.text[page.textLen - 1] == ' ') {
        page.textLen--;
        x -= _spaceAdv;
        count--;
        spaces--;
    }
    line.textLen = (uint16_t)(page.textLen - line.textStart);

    if (reason == TT_LINE_END_OVERFLOW && count > 1 && x < _width) {
        const uint16_t gaps = spaces > 0 ? spaces : (uint16_t)(count - 1);
        const int32_t extra = _width - x;
        if (extra <= (int32_t)gaps * TT_READER_JUSTIFY_MAX_GAP) {
            line.spaceGaps = spaces > 0;
            line.gaps = gaps;
            line.extra = (uint16_t)extra;
        }
    }
    return reason;
}

bool TTReaderLayout::layoutPage(TTReaderSource& src, uint32_t start, TTReaderPageLayout& page) {
    page.start = start;
    page.end = start;
    page.atEnd = false;
    page.lineCount = 0;
    page.textLen = 0;
    page.missingGlyphs = 0;
    if (_font == nullptr || !src.isOpen()) {
        return false;
    }
    if (start >= src.size()) {
        page.end = src.size();
        page.atEnd = true;
        return true;
    }

    uint32_t offset = start;
    bool paragraph = isParagraphStart(src, offset);
    if (offset == 0) {
        uint8_t b0 = 0;
        uint8_t b1 = 0;
        uint8_t b2 = 0;
        if (src.byteAt(0, b0) && src.byteAt(1, b1) && src.byteAt(2, b2)
            && b0 == 0xEF && b1 == 0xBB && b2 == 0xBF) {
            offset = TT_READER_BOM_LEN;
        }
    }

    int16_t y = 0;
    while (true) {
        const uint32_t lineStart = offset;
        offset = skipSpaces(src, offset, paragraph);
        if (src.failed()) {
            return false;
        }
        if (offset >= src.size()) {
            page.end = src.size();
            page.atEnd = true;
            break;
        }
        const int16_t lineY = (int16_t)(y + ((paragraph && page.lineCount > 0) ? TT_READER_PARA_GAP : 0));
        if (page.lineCount >= TT_READER_MAX_LINES || lineY + _lineHeight > _height) {
            page.end = lineStart;
            break;
        }
        TTReaderLine& line = page.lines[page.lineCount];
        line.y = lineY;
        const LineEnd reason = fillLine(src, offset, paragraph ? _indent : 0, page, line);
        if (src.failed()) {
            return false;
        }
        if (offset <= lineStart) {
            LOG_E("Reader layout: no progress at %u, stop", (unsigned)lineStart);
            page.end = src.size();
            page.atEnd = true;
            break;
        }
        if (line.textLen > 0) {
            page.lineCount++;
            y = (int16_t)(lineY + _lineHeight + TT_READER_LINE_GAP);
        }
        paragraph = reason == TT_LINE_END_PARAGRAPH;
        if (reason == TT_LINE_END_BUFFER) {
            page.end = offset;
            break;
        }
        if (reason == TT_LINE_END_EOF) {
            page.end = src.size();
            page.atEnd = true;
            break;
        }
    }
    if (!page.atEnd && skipSpaces(src, page.end, true) >= src.size()) {
        page.atEnd = true;
    }
    return !src.failed();
}
