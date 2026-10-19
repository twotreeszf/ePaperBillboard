#include "TTReaderLayout.h"
#include "TTReaderText.h"
#include "../Base/Logger.h"

bool TTReaderLayout::begin(TTFontLoader* font, TTFontLoader* codeFont, int16_t width, int16_t height,
                           bool markdown) {
    _font = font;
    if (_font == nullptr || width <= 0 || height <= 0) {
        LOG_E("Reader layout: invalid font or size %dx%d", (int)width, (int)height);
        return false;
    }
    if (codeFont == nullptr) {
        if (markdown) {
            LOG_W("Reader layout: code font missing, use text font");
        }
        codeFont = font;
    }
    _codeFont = codeFont;
    _width = width;
    _height = height;
    _markdown = markdown;
    _lineHeight = (int16_t)_font->getLineHeight();
    _ascent = (int16_t)(_font->getLineHeight() - _font->getBaseLine());

    uint32_t shown = 0;
    TTFontLoader::GlyphInfo info = {};
    glyph(_font, TT_READER_INDENT_REF_CHAR, shown, info);
    _unit = (int16_t)info.adv_w;
    _indent = (int16_t)(TT_READER_INDENT_CHARS * info.adv_w);
    info = {};
    glyph(_font, ' ', shown, info);
    _spaceAdv = info.adv_w;
    info = {};
    glyph(_font, TT_READER_BULLET_GLYPH, shown, info);
    _bulletX = info.adv_w < _unit ? (int16_t)((_unit - info.adv_w) / 2) : 0;
    info = {};
    glyph(_codeFont, ' ', shown, info);
    _codeSpaceAdv = info.adv_w;
    _codeBaseline = (int16_t)(_codeFont->getLineHeight() - _codeFont->getBaseLine()
                              + (TT_READER_CODE_PITCH - TT_READER_CODE_FONT_SIZE) / 2);

    const int32_t config[] = {
        TT_READER_LAYOUT_VERSION, _width, _height, _lineHeight, _ascent,
        TT_READER_LINE_GAP, TT_READER_PARA_GAP, TT_READER_JUSTIFY_MAX_GAP,
        _indent, _spaceAdv, TT_READER_PAGE_LINES, TT_READER_PAGE_TEXT_MAX,
    };
    _configHash = tt_reader_hash(config, sizeof(config));
    if (_markdown) {
        const int32_t mdConfig[] = {
            TT_MD_LAYOUT_VERSION, _unit, _bulletX, TT_READER_BOLD_EXTRA, TT_READER_ITALIC_TAIL,
            TT_MD_LIST_LEVEL_COLUMNS, TT_MD_CONTEXT_MAX,
            (int32_t)_codeFont->getLineHeight(), (int32_t)_codeFont->getBaseLine(), _codeSpaceAdv,
            TT_READER_CODE_PITCH, TT_READER_CODE_PAD_X, TT_READER_CODE_PAD_Y, TT_READER_CODE_BORDER,
            TT_READER_MAX_LINES,
        };
        _configHash = tt_reader_hash(mdConfig, sizeof(mdConfig), _configHash);
    }
    LOG_I("Reader layout: %dx%d lineH=%d gap=%d ascent=%d indent=%d space=%u md=%d codePitch=%d codeBase=%d hash=%08x",
          (int)_width, (int)_height, (int)_lineHeight, TT_READER_LINE_GAP, (int)_ascent, (int)_indent,
          (unsigned)_spaceAdv, _markdown ? 1 : 0, TT_READER_CODE_PITCH, (int)_codeBaseline,
          (unsigned)_configHash);
    return true;
}

bool TTReaderLayout::glyph(TTFontLoader* font, uint32_t cp, uint32_t& shown, TTFontLoader::GlyphInfo& info) {
    if (font->getGlyphInfo(cp, info)) {
        shown = cp;
        return true;
    }
    if (font->getGlyphInfo(TT_READER_MISSING_GLYPH, info)) {
        shown = TT_READER_MISSING_GLYPH;
        return false;
    }
    if (font->getGlyphInfo(TT_READER_FALLBACK_GLYPH, info)) {
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

bool TTReaderLayout::appendText(TTReaderPageLayout& page, uint32_t cp, bool mark, uint8_t style) {
    char bytes[TT_READER_STYLE_MARK_BYTES + TT_UTF8_MAX_BYTES];
    size_t n = 0;
    if (mark) {
        bytes[n++] = (char)TT_READER_STYLE_MARK;
        bytes[n++] = (char)(TT_READER_STYLE_MARK_FLAG | style);
    }
    n += tt_utf8_encode(cp, bytes + n);
    if (page.textLen + n > TT_READER_PAGE_TEXT_MAX) {
        return false;
    }
    memcpy(page.text + page.textLen, bytes, n);
    page.textLen = (uint16_t)(page.textLen + n);
    return true;
}

TTReaderLayout::LineEnd TTReaderLayout::fillLine(TTReaderSource& src, uint32_t& offset, int16_t x0,
                                                 TTReaderPageLayout& page, TTReaderLine& line, TTMdCursor* md) {
    line.textStart = page.textLen;
    line.textLen = 0;
    line.x = x0;
    line.extra = 0;
    line.gaps = 0;
    line.spaceGaps = false;
    line.style = md != nullptr ? tt_md_style(*md) : 0;
    line.kind = TT_READER_LINE_TEXT;
    line.flags = 0;
    line.markX = 0;

    const bool code = md != nullptr && md->block.kind == TT_MD_CODE;
    const bool inlineMarkdown = md != nullptr && !code;
    TTFontLoader* font = code ? _codeFont : _font;
    const uint16_t spaceAdv = code ? _codeSpaceAdv : _spaceAdv;
    const int32_t right = code ? _width - TT_READER_CODE_INSET_X : _width;
    uint8_t style = line.style;
    TTMdInline breakInline;
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
        TTMdInline before;
        if (inlineMarkdown) {
            before = md->inl;
        }
        uint32_t cp = inlineMarkdown ? tt_md_next(src, offset, md->inl) : src.next(offset);
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
            breakInline = before;
        }
        uint32_t shown = 0;
        TTFontLoader::GlyphInfo info = {};
        if (!glyph(font, cp, shown, info)) {
            if (shown == 0) {
                continue;
            }
            page.missingGlyphs++;
        }
        const int32_t adv = info.adv_w;
        if (cp == ' ') {
            if (x + adv > right) {
                reason = TT_LINE_END_OVERFLOW;
                break;
            }
            if (!appendText(page, ' ')) {
                offset = charOffset;
                if (inlineMarkdown) {
                    md->inl = before;
                }
                reason = TT_LINE_END_BUFFER;
                break;
            }
            x += adv;
            count++;
            spaces++;
            prev = cp;
            continue;
        }
        const uint8_t charStyle = md != nullptr ? tt_md_style(*md) : 0;
        const bool mark = charStyle != style;
        const bool italicEnds = (style & TT_READER_STYLE_ITALIC) && !(charStyle & TT_READER_STYLE_ITALIC);
        const int32_t tail = mark && count > 0 && italicEnds ? TT_READER_ITALIC_TAIL : 0;
        const int32_t width = adv + tail + ((charStyle & TT_READER_STYLE_BOLD) ? TT_READER_BOLD_EXTRA : 0);
        const bool tooLong = page.textLen - line.textStart >= TT_READER_LINE_BYTES_MAX;
        if (count > 0 && (x + width > right || tooLong)) {
            if (hasBreak) {
                page.textLen = breakText;
                x = breakX;
                count = breakCount;
                spaces = breakSpaces;
                offset = breakOffset;
                if (inlineMarkdown) {
                    md->inl = breakInline;
                }
            } else {
                offset = charOffset;
                if (inlineMarkdown) {
                    md->inl = before;
                }
            }
            reason = TT_LINE_END_OVERFLOW;
            break;
        }
        if (!appendText(page, shown, mark, charStyle)) {
            offset = charOffset;
            if (inlineMarkdown) {
                md->inl = before;
            }
            reason = TT_LINE_END_BUFFER;
            break;
        }
        style = charStyle;
        x += width;
        count++;
        prev = cp;
    }

    while (page.textLen > line.textStart && page.text[page.textLen - 1] == ' ') {
        page.textLen--;
        x -= spaceAdv;
        count--;
        spaces--;
    }
    line.textLen = (uint16_t)(page.textLen - line.textStart);

    if (!code && reason == TT_LINE_END_OVERFLOW && count > 1 && x < _width) {
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

    TTMdCursor cursor;
    TTMdCursor* md = _markdown ? &cursor : nullptr;
    int16_t contX = 0;
    if (md != nullptr && !beginMarkdown(src, offset, paragraph, cursor, contX)) {
        return false;
    }

    const uint8_t maxLines = md != nullptr ? TT_READER_MAX_LINES : TT_READER_PAGE_LINES;
    bool boxTopPending = false;
    int16_t boxTopY = 0;
    int16_t y = 0;
    while (true) {
        const uint32_t lineStart = offset;
        uint8_t columns = 0;
        const bool mdBlockStart = md != nullptr && paragraph;
        if (mdBlockStart && cursor.fence) {
            offset = tt_md_skip_indent(src, offset, columns);
        } else if (mdBlockStart) {
            offset = tt_md_skip_blank(src, offset, columns);
        } else {
            offset = skipSpaces(src, offset, paragraph);
        }
        if (src.failed()) {
            return false;
        }
        if (offset >= src.size()) {
            page.end = src.size();
            page.atEnd = true;
            break;
        }
        if (mdBlockStart && columns <= TT_MD_FENCE_INDENT_MAX && tt_md_is_fence(src, offset)) {
            if (!cursor.fence) {
                const int32_t need = y + TT_READER_CODE_INSET_Y * 2 + TT_READER_CODE_PITCH;
                if (page.lineCount > 0 && (page.lineCount >= maxLines || need > _height)) {
                    page.end = lineStart;
                    break;
                }
                boxTopPending = true;
                boxTopY = y;
                y = (int16_t)(y + TT_READER_CODE_INSET_Y);
            } else if (boxTopPending) {
                boxTopPending = false;
                y = boxTopY;
            } else if (page.lineCount > 0 && page.lines[page.lineCount - 1].kind == TT_READER_LINE_CODE) {
                TTReaderLine& last = page.lines[page.lineCount - 1];
                last.flags |= TT_READER_LINE_FLAG_BOX_BOTTOM;
                y = (int16_t)(last.y + TT_READER_CODE_PITCH + TT_READER_CODE_INSET_Y + TT_READER_LINE_GAP);
            }
            cursor.fence = !cursor.fence;
            offset = tt_md_skip_line(src, offset);
            if (src.failed()) {
                return false;
            }
            continue;
        }
        const bool codeLine = md != nullptr && cursor.fence;
        const int16_t lineY = (int16_t)(y + ((paragraph && page.lineCount > 0) ? TT_READER_PARA_GAP : 0));
        const int32_t lineBottom = codeLine ? lineY + TT_READER_CODE_PITCH + TT_READER_CODE_INSET_Y
                                            : lineY + _lineHeight;
        if (page.lineCount >= maxLines || lineBottom > _height) {
            page.end = lineStart;
            break;
        }
        TTReaderLine& line = page.lines[page.lineCount];
        int16_t x0 = paragraph ? _indent : 0;
        bool bullet = false;
        if (mdBlockStart) {
            if (cursor.fence) {
                cursor.block = TTMdBlock();
                cursor.block.kind = TT_MD_CODE;
            } else {
                cursor.block = tt_md_parse_block(src, offset, columns);
            }
            cursor.inl = TTMdInline();
            if (cursor.block.kind == TT_MD_RULE) {
                line = TTReaderLine();
                line.textStart = page.textLen;
                line.y = lineY;
                line.kind = TT_READER_LINE_RULE;
                page.lineCount++;
                y = (int16_t)(lineY + _lineHeight + TT_READER_LINE_GAP);
                offset = tt_md_skip_line(src, offset);
                if (src.failed()) {
                    return false;
                }
                continue;
            }
            if (cursor.block.kind == TT_MD_CODE) {
                uint8_t b = 0;
                if (src.byteAt(offset, b) && (b == '\n' || b == '\r')) {
                    line = TTReaderLine();
                    line.textStart = page.textLen;
                    line.y = lineY;
                    line.kind = TT_READER_LINE_CODE;
                    if (boxTopPending) {
                        line.flags |= TT_READER_LINE_FLAG_BOX_TOP;
                        boxTopPending = false;
                    }
                    page.lineCount++;
                    y = (int16_t)(lineY + TT_READER_CODE_PITCH);
                    offset = tt_md_skip_line(src, offset);
                    if (src.failed()) {
                        return false;
                    }
                    continue;
                }
                x0 = codeX(columns);
                contX = x0;
            } else {
                x0 = blockX(cursor.block, true);
                contX = blockX(cursor.block, false);
            }
            bullet = cursor.block.kind == TT_MD_BULLET;
        } else if (md != nullptr) {
            x0 = contX;
        }
        line.y = lineY;
        const LineEnd reason = fillLine(src, offset, x0, page, line, md);
        if (src.failed()) {
            return false;
        }
        if (bullet) {
            line.kind = TT_READER_LINE_BULLET;
            line.markX = (int16_t)(x0 - _unit + _bulletX);
        } else if (codeLine) {
            line.kind = TT_READER_LINE_CODE;
        }
        if (offset <= lineStart) {
            LOG_E("Reader layout: no progress at %u, stop", (unsigned)lineStart);
            page.end = src.size();
            page.atEnd = true;
            break;
        }
        if (line.textLen > 0) {
            if (codeLine && boxTopPending) {
                line.flags |= TT_READER_LINE_FLAG_BOX_TOP;
                boxTopPending = false;
            }
            page.lineCount++;
            y = (int16_t)(lineY + (codeLine ? TT_READER_CODE_PITCH : _lineHeight + TT_READER_LINE_GAP));
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
    if (!page.atEnd) {
        page.atEnd = md != nullptr ? markdownTailEmpty(src, page.end)
                                   : skipSpaces(src, page.end, true) >= src.size();
    }
    if (page.atEnd && md != nullptr && page.lineCount > 0
        && page.lines[page.lineCount - 1].kind == TT_READER_LINE_CODE) {
        page.lines[page.lineCount - 1].flags |= TT_READER_LINE_FLAG_BOX_BOTTOM;
    }
    return !src.failed();
}

bool TTReaderLayout::beginMarkdown(TTReaderSource& src, uint32_t offset, bool paragraph,
                                   TTMdCursor& cursor, int16_t& contX) {
    uint32_t lineStart = offset;
    bool fence = false;
    tt_md_line_context(src, offset, lineStart, fence);
    cursor.fence = fence;
    contX = 0;
    if (paragraph || src.failed()) {
        return !src.failed();
    }
    uint8_t columns = 0;
    if (fence) {
        cursor.block.kind = TT_MD_CODE;
        tt_md_skip_indent(src, lineStart, columns);
        contX = codeX(columns);
        return !src.failed();
    }
    uint32_t pos = tt_md_skip_blank(src, lineStart, columns);
    if (pos >= offset) {
        return !src.failed();
    }
    cursor.block = tt_md_parse_block(src, pos, columns);
    if (cursor.block.kind == TT_MD_RULE) {
        cursor.block = TTMdBlock();
    }
    contX = blockX(cursor.block, false);
    while (pos < offset && !src.failed()) {
        tt_md_next(src, pos, cursor.inl);
    }
    return !src.failed();
}

bool TTReaderLayout::markdownTailEmpty(TTReaderSource& src, uint32_t offset) {
    while (true) {
        uint8_t columns = 0;
        offset = tt_md_skip_blank(src, offset, columns);
        if (src.failed()) {
            return false;
        }
        if (offset >= src.size()) {
            return true;
        }
        if (columns > TT_MD_FENCE_INDENT_MAX || !tt_md_is_fence(src, offset)) {
            return false;
        }
        offset = tt_md_skip_line(src, offset);
    }
}

int16_t TTReaderLayout::blockX(const TTMdBlock& block, bool first) const {
    switch (block.kind) {
        case TT_MD_PARAGRAPH:
            return first ? _indent : 0;
        case TT_MD_QUOTE:
            return (int16_t)(block.level * _unit);
        case TT_MD_BULLET:
            return (int16_t)((block.level + 1) * _unit);
        case TT_MD_ORDERED:
            return (int16_t)((block.level + (first ? 0 : 1)) * _unit);
        default:
            return 0;
    }
}

int16_t TTReaderLayout::codeX(uint8_t columns) const {
    return (int16_t)(TT_READER_CODE_INSET_X + columns * _codeSpaceAdv);
}
