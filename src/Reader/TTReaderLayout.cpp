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
    info = {};
    glyph(_codeFont, TT_READER_INDENT_REF_CHAR, shown, info);
    _codeUnit = info.adv_w;
    _codeBaseline = (int16_t)(_codeFont->getLineHeight() - _codeFont->getBaseLine()
                              + (TT_READER_CODE_PITCH - TT_READER_CODE_FONT_SIZE) / 2);
    _quoteLineHeight = (int16_t)_codeFont->getLineHeight();
    _quoteAscent = (int16_t)(_quoteLineHeight - _codeFont->getBaseLine());
    if (info.box_h > 0) {
        _quoteInkTop = (int16_t)(_quoteAscent - info.ofs_y - (int16_t)info.box_h);
        _quoteInkBottom = (int16_t)(_quoteAscent - info.ofs_y - 1);
    } else {
        _quoteInkTop = 0;
        _quoteInkBottom = (int16_t)(_quoteLineHeight - 1);
    }

    const int32_t config[] = {
        TT_READER_LAYOUT_VERSION, _width, _height, _lineHeight, _ascent,
        TT_READER_LINE_GAP, TT_READER_PARA_GAP, TT_READER_JUSTIFY_MAX_GAP,
        _indent, _spaceAdv, TT_READER_PAGE_LINES, TT_READER_PAGE_TEXT_MAX,
    };
    _configHash = tt_reader_hash(config, sizeof(config));
    if (_markdown) {
        const int32_t mdConfig[] = {
            TT_MD_LAYOUT_VERSION, _unit, _bulletX, TT_READER_BOLD_EXTRA, TT_READER_ITALIC_TAIL,
            TT_READER_QUOTE_BAR_X, TT_READER_QUOTE_BAR_W, TT_READER_QUOTE_LINE_GAP, _quoteLineHeight,
            _quoteInkTop, _quoteInkBottom,
            TT_MD_LIST_LEVEL_COLUMNS, TT_MD_CONTEXT_MAX,
            (int32_t)_codeFont->getLineHeight(), (int32_t)_codeFont->getBaseLine(), _codeSpaceAdv,
            TT_READER_CODE_PITCH, TT_READER_CODE_PAD_X, TT_READER_CODE_PAD_Y, TT_READER_CODE_BORDER,
            TT_READER_MAX_LINES, TT_READER_TABLE_MIN_CHARS, TT_READER_TABLE_PAD_X, TT_READER_TABLE_PAD_Y,
            TT_READER_TABLE_BORDER, _codeUnit, TT_READER_TASK_BOX,
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
    const bool quote = md != nullptr && md->block.kind == TT_MD_QUOTE;
    const bool inlineMarkdown = md != nullptr && !code;
    TTFontLoader* font = (code || quote) ? _codeFont : _font;
    const uint16_t spaceAdv = (code || quote) ? _codeSpaceAdv : _spaceAdv;
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
    uint32_t glyphCp = 0;
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
        if (cp == '\r' || cp == '\n') {
            if (cp == '\r') {
                uint8_t b = 0;
                if (src.byteAt(offset, b) && b == '\n') {
                    offset++;
                }
            }
            uint32_t content = offset;
            if (!inlineMarkdown || !tt_md_soft_join(src, offset, md->block, content)) {
                reason = TT_LINE_END_PARAGRAPH;
                break;
            }
            while (page.textLen > line.textStart && page.text[page.textLen - 1] == ' ') {
                page.textLen--;
                x -= spaceAdv;
                if (count > 0) {
                    count--;
                }
                if (spaces > 0) {
                    spaces--;
                }
            }
            if (count > 0) {
                hasBreak = true;
                breakOffset = content;
                breakText = page.textLen;
                breakX = x;
                breakCount = count;
                breakSpaces = spaces;
                breakInline = before;
            }
            const uint32_t nextCp = tt_md_first_glyph(src, content, before);
            const bool tight = tt_reader_is_wide(glyphCp) && tt_reader_is_wide(nextCp);
            if (count > 0 && !tight) {
                if (x + spaceAdv > right || !appendText(page, ' ')) {
                    offset = content;
                    md->inl = before;
                    reason = x + spaceAdv > right ? TT_LINE_END_OVERFLOW : TT_LINE_END_BUFFER;
                    break;
                }
                x += spaceAdv;
                count++;
                spaces++;
                prev = ' ';
            }
            offset = content;
            continue;
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
        const int32_t tail = mark && count > 0 && italicEnds && !tt_reader_is_emphasis_char(prev)
            ? TT_READER_ITALIC_TAIL : 0;
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
        glyphCp = cp;
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
    page.image[0] = '\0';
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
    bool joined = false;
    if (md != nullptr && !beginMarkdown(src, offset, paragraph, joined, cursor, contX)) {
        return false;
    }
    if (joined) {
        paragraph = false;
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
        if (mdBlockStart && !cursor.fence && tt_md_is_image(src, offset, page.image, sizeof(page.image))) {
            if (page.lineCount > 0) {
                page.image[0] = '\0';
                page.end = lineStart;
                break;
            }
            offset = tt_md_skip_line(src, offset);
            if (src.failed()) {
                return false;
            }
            page.end = offset;
            page.atEnd = offset >= src.size();
            LOG_I("Reader markdown: image %s at %u", page.image, (unsigned)page.start);
            break;
        }
        if (mdBlockStart && !cursor.fence) {
            int16_t tableY = y;
            uint32_t tableEnd = offset;
            const TableLay lay = layoutTable(src, offset, page, tableY, tableEnd);
            if (lay == TT_TABLE_FAIL) {
                return false;
            }
            if (lay == TT_TABLE_BREAK) {
                page.end = tableEnd;
                break;
            }
            if (lay == TT_TABLE_DONE) {
                offset = tableEnd;
                y = tableY;
                paragraph = true;
                continue;
            }
        }
        const bool codeLine = md != nullptr && cursor.fence;
        const bool quoteFit = (md != nullptr && cursor.block.kind == TT_MD_QUOTE)
            || (mdBlockStart && !cursor.fence);
        const int16_t lineY = (int16_t)(y + ((paragraph && page.lineCount > 0) ? TT_READER_PARA_GAP : 0));
        const int32_t lineBottom = codeLine ? lineY + TT_READER_CODE_PITCH + TT_READER_CODE_INSET_Y
                                            : quoteFit ? lineY + _quoteLineHeight
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
        const bool quoteLine = md != nullptr && cursor.block.kind == TT_MD_QUOTE;
        if (!codeLine && !quoteLine && lineY + _lineHeight > _height) {
            page.end = lineStart;
            break;
        }
        line.y = lineY;
        const LineEnd reason = fillLine(src, offset, x0, page, line, md);
        if (src.failed()) {
            return false;
        }
        if (mdBlockStart && cursor.block.kind == TT_MD_HEADING && cursor.block.level == 1
            && line.textLen > 0
            && (reason == TT_LINE_END_PARAGRAPH || reason == TT_LINE_END_EOF)) {
            const int32_t w = textWidth(page.text + line.textStart, line.textLen, line.style);
            if (w > 0 && w <= _width) {
                line.x = (int16_t)((_width - w) / 2);
                LOG_D("Reader markdown: center h1 width=%d x=%d", (int)w, (int)line.x);
            }
        }
        if (bullet) {
            if (cursor.block.task != TT_MD_TASK_NONE) {
                const int16_t slot = _unit > TT_READER_TASK_BOX ? (int16_t)((_unit - TT_READER_TASK_BOX) / 2) : 0;
                line.kind = TT_READER_LINE_TASK;
                line.markX = (int16_t)(x0 - _unit + slot);
                if (cursor.block.task == TT_MD_TASK_DONE) {
                    line.flags |= TT_READER_LINE_FLAG_TASK_DONE;
                }
            } else {
                line.kind = TT_READER_LINE_BULLET;
                line.markX = (int16_t)(x0 - _unit + _bulletX);
            }
        } else if (codeLine) {
            line.kind = TT_READER_LINE_CODE;
        }
        if (offset <= lineStart) {
            LOG_E("Reader layout: no progress at %u, stop", (unsigned)lineStart);
            page.end = src.size();
            page.atEnd = true;
            break;
        }
        if (md != nullptr && cursor.block.kind == TT_MD_QUOTE && cursor.block.level > 0) {
            line.flags |= TT_READER_LINE_FLAG_QUOTE;
            line.markX = cursor.block.level;
        }
        if (line.textLen > 0 || line.kind == TT_READER_LINE_TASK) {
            if (codeLine && boxTopPending) {
                line.flags |= TT_READER_LINE_FLAG_BOX_TOP;
                boxTopPending = false;
            }
            page.lineCount++;
            const bool placedQuote = md != nullptr && cursor.block.kind == TT_MD_QUOTE;
            const int16_t quoteStep = (int16_t)(_quoteLineHeight + TT_READER_QUOTE_LINE_GAP);
            y = (int16_t)(lineY + (codeLine ? TT_READER_CODE_PITCH
                                            : placedQuote ? quoteStep
                                                          : _lineHeight + TT_READER_LINE_GAP));
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
    if (md != nullptr) {
        appendQuoteBars(page);
    }
    return !src.failed();
}

void TTReaderLayout::appendQuoteBars(TTReaderPageLayout& page) {
    const uint8_t count = page.lineCount;
    const int16_t step = (int16_t)(_quoteLineHeight + TT_READER_QUOTE_LINE_GAP);
    uint8_t i = 0;
    while (i < count) {
        const TTReaderLine& first = page.lines[i];
        if ((first.flags & TT_READER_LINE_FLAG_QUOTE) == 0 || first.markX <= 0) {
            i++;
            continue;
        }
        uint8_t j = (uint8_t)(i + 1);
        while (j < count) {
            const TTReaderLine& line = page.lines[j];
            if ((line.flags & TT_READER_LINE_FLAG_QUOTE) == 0 || line.markX != first.markX) {
                break;
            }
            if (line.y > page.lines[j - 1].y + step) {
                break;
            }
            j++;
        }
        const int16_t top = (int16_t)(first.y + _quoteInkTop);
        const int16_t bottom = (int16_t)(page.lines[j - 1].y + _quoteInkBottom);
        const uint8_t level = first.markX > TT_MD_QUOTE_MAX ? TT_MD_QUOTE_MAX : (uint8_t)first.markX;
        for (uint8_t n = 1; n <= level; n++) {
            const int16_t x = (int16_t)((n - 1) * _unit + TT_READER_QUOTE_BAR_X);
            if (!appendRule(page, TT_READER_LINE_VRULE, x, top, bottom)) {
                LOG_W("Reader markdown: quote bar dropped");
                return;
            }
            page.lines[page.lineCount - 1].flags |= TT_READER_LINE_FLAG_QUOTE;
        }
        i = j;
    }
}

bool TTReaderLayout::beginMarkdown(TTReaderSource& src, uint32_t& offset, bool paragraph, bool& joined,
                                   TTMdCursor& cursor, int16_t& contX) {
    joined = false;
    uint32_t lineStart = offset;
    bool fence = false;
    tt_md_line_context(src, offset, lineStart, fence);
    cursor.fence = fence;
    contX = 0;
    if (src.failed()) {
        return false;
    }
    uint8_t columns = 0;
    if (fence) {
        if (!paragraph) {
            cursor.block.kind = TT_MD_CODE;
            tt_md_skip_indent(src, lineStart, columns);
            contX = codeX(columns);
        }
        return !src.failed();
    }
    TTMdBlock owner;
    uint32_t origin = lineStart;
    uint32_t content = lineStart;
    if (tt_md_continued(src, lineStart, owner, origin, content)) {
        joined = true;
        cursor.block = owner;
        contX = blockX(owner, false);
        if (offset <= lineStart) {
            offset = content;
        }
        tt_md_replay(src, origin, offset, owner, cursor.inl);
        LOG_D("Reader markdown: continued kind=%u task=%u from %u at %u",
              (unsigned)owner.kind, (unsigned)owner.task, (unsigned)origin, (unsigned)offset);
        return !src.failed();
    }
    if (paragraph) {
        return true;
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

int32_t TTReaderLayout::textWidth(const char* text, uint16_t len, uint8_t style) {
    int32_t x = 0;
    uint16_t count = 0;
    uint32_t lastCp = 0;
    size_t pos = 0;
    while (pos < len) {
        if ((uint8_t)text[pos] == TT_READER_STYLE_MARK && pos + 1 < len) {
            const uint8_t next = (uint8_t)text[pos + 1] & TT_READER_STYLE_MASK;
            if (count > 0 && (style & TT_READER_STYLE_ITALIC) && !(next & TT_READER_STYLE_ITALIC)
                && !tt_reader_is_emphasis_char(lastCp)) {
                x += TT_READER_ITALIC_TAIL;
            }
            style = next;
            pos += TT_READER_STYLE_MARK_BYTES;
            continue;
        }
        uint32_t cp = 0;
        const size_t n = tt_utf8_decode((const uint8_t*)text + pos, len - pos, &cp);
        pos += n > 0 ? n : 1;
        uint32_t shown = 0;
        TTFontLoader::GlyphInfo info = {};
        if (!glyph(_font, cp, shown, info) && shown == 0) {
            continue;
        }
        x += info.adv_w;
        if ((style & TT_READER_STYLE_BOLD) && cp != ' ') {
            x += TT_READER_BOLD_EXTRA;
        }
        lastCp = cp;
        count++;
    }
    return x;
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

uint8_t TTReaderLayout::cellStyle(const TTMdInline& inl, bool header) const {
    uint8_t style = inl.toggles & TT_READER_STYLE_MASK;
    if (inl.link) {
        style |= TT_READER_STYLE_UNDERLINE;
    }
    if (header) {
        style |= TT_READER_STYLE_BOLD;
    }
    return style;
}

int32_t TTReaderLayout::cellWidth(TTReaderSource& src, uint32_t start, uint32_t end, bool header) {
    TTMdInline inl;
    uint32_t off = start;
    int32_t width = 0;
    uint8_t style = cellStyle(inl, header);
    uint16_t count = 0;
    uint32_t prev = 0;
    while (off < end) {
        const uint32_t cp0 = tt_md_next(src, off, inl, end);
        if (cp0 == 0 || src.failed()) {
            break;
        }
        uint32_t cp = cp0;
        if (tt_reader_is_ignorable(cp)) {
            continue;
        }
        if (cp == '\t') {
            cp = ' ';
        }
        const uint8_t nextStyle = cellStyle(inl, header);
        const bool mark = nextStyle != style;
        const bool italicEnds = (style & TT_READER_STYLE_ITALIC) && !(nextStyle & TT_READER_STYLE_ITALIC);
        const int32_t tail = mark && count > 0 && italicEnds && !tt_reader_is_emphasis_char(prev)
            ? TT_READER_ITALIC_TAIL : 0;
        uint32_t shown = 0;
        TTFontLoader::GlyphInfo info = {};
        if (!glyph(_codeFont, cp, shown, info) && shown == 0) {
            continue;
        }
        width += info.adv_w + tail;
        if ((nextStyle & TT_READER_STYLE_BOLD) && cp != ' ') {
            width += TT_READER_BOLD_EXTRA;
        }
        prev = cp;
        style = nextStyle;
        count++;
    }
    return width;
}

bool TTReaderLayout::appendRule(TTReaderPageLayout& page, TTReaderLineKind kind, int16_t x, int16_t y, int16_t mark) {
    if (page.lineCount >= TT_READER_MAX_LINES) {
        return false;
    }
    TTReaderLine& line = page.lines[page.lineCount];
    line = TTReaderLine();
    line.textStart = page.textLen;
    line.kind = kind;
    line.flags = TT_READER_LINE_FLAG_EXACT;
    line.x = x;
    line.y = y;
    line.markX = mark;
    page.lineCount++;
    return true;
}

bool TTReaderLayout::appendTableCell(TTReaderSource& src, const TTMdCell& cell, int16_t x, int16_t colW,
                                     bool header, uint8_t bandCap, TTReaderPageLayout& page, uint8_t& bands,
                                     bool& clipped) {
    bands = 0;
    clipped = false;
    if (cell.start >= cell.end || bandCap == 0) {
        return true;
    }
    uint32_t off = cell.start;
    TTMdInline inl;
    uint8_t band = 0;
    while (off < cell.end && band < bandCap) {
        if (page.lineCount >= TT_READER_MAX_LINES) {
            return false;
        }
        const uint32_t bandStart = off;
        if (band > 0) {
            while (off < cell.end) {
                const uint32_t at = off;
                TTMdInline before = inl;
                const uint32_t cp = tt_md_next(src, off, inl, cell.end);
                if (cp == 0 || src.failed()) {
                    break;
                }
                if (tt_reader_is_ignorable(cp) || cp == ' ' || cp == '\t') {
                    continue;
                }
                off = at;
                inl = before;
                break;
            }
        }
        TTReaderLine& line = page.lines[page.lineCount];
        line = TTReaderLine();
        line.textStart = page.textLen;
        line.kind = TT_READER_LINE_TABLE;
        line.x = x;
        line.style = cellStyle(inl, header);
        uint8_t style = line.style;
        int32_t pen = 0;
        uint16_t count = 0;
        bool hasBreak = false;
        uint32_t breakOff = off;
        uint16_t breakText = page.textLen;
        uint16_t breakCount = 0;
        TTMdInline breakInl = inl;
        uint8_t breakStyle = style;
        uint32_t prev = 0;
        bool wrapped = false;
        while (off < cell.end) {
            const uint32_t charOff = off;
            TTMdInline before = inl;
            uint32_t cp = tt_md_next(src, off, inl, cell.end);
            if (src.failed()) {
                return false;
            }
            if (cp == 0) {
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
                breakOff = charOff;
                breakText = page.textLen;
                breakCount = count;
                breakInl = before;
                breakStyle = style;
            }
            uint32_t shown = 0;
            TTFontLoader::GlyphInfo info = {};
            if (!glyph(_codeFont, cp, shown, info)) {
                if (shown == 0) {
                    continue;
                }
                page.missingGlyphs++;
            }
            const uint8_t nextStyle = cellStyle(inl, header);
            const bool mark = nextStyle != style;
            const bool italicEnds = (style & TT_READER_STYLE_ITALIC) && !(nextStyle & TT_READER_STYLE_ITALIC);
            const int32_t tail = mark && count > 0 && italicEnds && !tt_reader_is_emphasis_char(prev)
                ? TT_READER_ITALIC_TAIL : 0;
            const bool bold = (nextStyle & TT_READER_STYLE_BOLD) && cp != ' ';
            const int32_t glyphW = info.adv_w + tail + (bold ? TT_READER_BOLD_EXTRA : 0);
            if (count > 0 && pen + glyphW > colW) {
                if (hasBreak) {
                    page.textLen = breakText;
                    count = breakCount;
                    off = breakOff;
                    inl = breakInl;
                    style = breakStyle;
                } else {
                    off = charOff;
                    inl = before;
                }
                wrapped = true;
                break;
            }
            if (!appendText(page, shown, mark, nextStyle)) {
                return false;
            }
            style = nextStyle;
            pen += glyphW;
            count++;
            prev = cp;
        }
        while (page.textLen > line.textStart && page.text[page.textLen - 1] == ' ') {
            page.textLen--;
        }
        line.textLen = (uint16_t)(page.textLen - line.textStart);
        if (line.textLen == 0) {
            if (!wrapped || off <= bandStart) {
                break;
            }
            continue;
        }
        line.extra = band;
        page.lineCount++;
        band++;
        if (!wrapped) {
            break;
        }
    }
    bands = band;
    clipped = off < cell.end;
    return !src.failed();
}

static bool tt_table_line_has_pipe(TTReaderSource& src, uint32_t offset) {
    const uint32_t line = tt_md_line_start(src, offset);
    for (uint16_t i = 0; i < TT_MD_TABLE_LINE_MAX; i++) {
        uint8_t b = 0;
        if (!src.byteAt(line + i, b) || b == '\n' || b == '\r') {
            return false;
        }
        if (b == '|') {
            return true;
        }
    }
    return false;
}

TTReaderLayout::TableLay TTReaderLayout::layoutTable(TTReaderSource& src, uint32_t offset,
                                                     TTReaderPageLayout& page, int16_t& y, uint32_t& end) {
    if (_codeUnit == 0 || !tt_table_line_has_pipe(src, offset)) {
        return src.failed() ? TT_TABLE_FAIL : TT_TABLE_NONE;
    }
    TTMdTable table;
    if (!tt_md_find_table(src, offset, table)) {
        return src.failed() ? TT_TABLE_FAIL : TT_TABLE_NONE;
    }
    const uint8_t cols = table.cols;
    int32_t natural[TT_MD_TABLE_COLS_MAX] = {};
    TTMdCell headerCells[TT_MD_TABLE_COLS_MAX];
    uint8_t headerCount = 0;
    if (tt_md_split_cells(src, table.header, headerCells, TT_MD_TABLE_COLS_MAX, headerCount)) {
        const uint8_t n = headerCount < cols ? headerCount : cols;
        for (uint8_t c = 0; c < n; c++) {
            natural[c] = cellWidth(src, headerCells[c].start, headerCells[c].end, true);
        }
    }
    for (uint32_t row = table.body; row < table.end && !src.failed(); ) {
        TTMdCell cells[TT_MD_TABLE_COLS_MAX];
        uint8_t count = 0;
        if (tt_md_split_cells(src, row, cells, TT_MD_TABLE_COLS_MAX, count)) {
            const uint8_t n = count < cols ? count : cols;
            for (uint8_t c = 0; c < n; c++) {
                const int32_t w = cellWidth(src, cells[c].start, cells[c].end, false);
                if (w > natural[c]) {
                    natural[c] = w;
                }
            }
        }
        const uint32_t next = tt_md_skip_line(src, row);
        if (next <= row) {
            break;
        }
        row = next;
    }
    if (src.failed()) {
        return TT_TABLE_FAIL;
    }

    const int32_t minW = (int32_t)_codeUnit * TT_READER_TABLE_MIN_CHARS;
    const int32_t chrome = (int32_t)cols * TT_READER_TABLE_PAD_X * 2
        + (int32_t)(cols + 1) * TT_READER_TABLE_BORDER;
    if (minW <= 0 || chrome + (int32_t)cols * minW > _width) {
        LOG_D("Reader table: plain text cols=%u need=%d width=%d",
              (unsigned)cols, (int)(chrome + (int32_t)cols * minW), (int)_width);
        return TT_TABLE_NONE;
    }
    const int32_t budget = _width - chrome;
    const int32_t flex = budget - (int32_t)cols * minW;
    int64_t weight = 0;
    for (uint8_t c = 0; c < cols; c++) {
        weight += natural[c];
    }
    int32_t colW[TT_MD_TABLE_COLS_MAX];
    int16_t textX[TT_MD_TABLE_COLS_MAX];
    int16_t gridX[TT_MD_TABLE_COLS_MAX + 1];
    int32_t used = 0;
    uint8_t widest = 0;
    for (uint8_t c = 0; c < cols; c++) {
        int32_t extra = 0;
        if (flex > 0) {
            extra = weight > 0 ? (int32_t)((int64_t)flex * natural[c] / weight) : flex / cols;
        }
        colW[c] = minW + extra;
        used += colW[c];
        if (natural[c] >= natural[widest]) {
            widest = c;
        }
    }
    if (used < budget) {
        colW[widest] += budget - used;
    }
    int32_t x = 0;
    for (uint8_t c = 0; c < cols; c++) {
        gridX[c] = (int16_t)x;
        x += TT_READER_TABLE_BORDER + TT_READER_TABLE_PAD_X;
        textX[c] = (int16_t)x;
        x += colW[c] + TT_READER_TABLE_PAD_X;
    }
    gridX[cols] = (int16_t)x;

    uint32_t row = tt_md_line_start(src, offset);
    if (row == table.separator || (row != table.header && row < table.body)) {
        row = table.body;
    }
    if (row < table.header || row >= table.end) {
        return TT_TABLE_NONE;
    }

    const bool pageHadLines = page.lineCount > 0;
    bool placed = false;
    int16_t topY = y;
    int16_t bottomY = y;
    bool reported = false;
    while (row < table.end && row < src.size()) {
        const bool header = row == table.header;
        const int32_t topBorder = placed ? 0 : TT_READER_TABLE_BORDER;
        const uint8_t reserved = (uint8_t)(cols + 1 + (placed ? 1 : 2));
        int32_t bandsFit = 0;
        if (page.lineCount + reserved < TT_READER_MAX_LINES) {
            const int32_t roomY = (int32_t)_height - y - topBorder - TT_READER_TABLE_PAD_Y * 2
                - TT_READER_TABLE_BORDER;
            if (roomY > 0) {
                bandsFit = roomY / TT_READER_CODE_PITCH;
            }
            const uint8_t slots = (uint8_t)(TT_READER_MAX_LINES - reserved - page.lineCount);
            const uint8_t slotBands = (uint8_t)(slots / cols);
            if (bandsFit > slotBands) {
                bandsFit = slotBands;
            }
        }
        if (bandsFit > TT_READER_TABLE_BANDS_MAX) {
            bandsFit = TT_READER_TABLE_BANDS_MAX;
        }
        if (bandsFit < 1) {
            if (placed || pageHadLines) {
                end = row;
                break;
            }
            bandsFit = 1;
        }

        const uint16_t savedLines = page.lineCount;
        const uint16_t savedText = page.textLen;
        const uint16_t savedMissing = page.missingGlyphs;
        TTMdCell cells[TT_MD_TABLE_COLS_MAX];
        uint8_t count = 0;
        tt_md_split_cells(src, row, cells, TT_MD_TABLE_COLS_MAX, count);
        uint8_t maxBand = 0;
        bool rowClipped = false;
        bool rowFailed = false;
        for (uint8_t c = 0; c < cols; c++) {
            TTMdCell cell;
            if (c < count) {
                cell = cells[c];
            }
            uint8_t cellBands = 0;
            bool cellClipped = false;
            if (!appendTableCell(src, cell, textX[c], (int16_t)colW[c], header, (uint8_t)bandsFit, page,
                                 cellBands, cellClipped)) {
                rowFailed = true;
                break;
            }
            if (cellBands > maxBand) {
                maxBand = cellBands;
            }
            rowClipped = rowClipped || cellClipped;
        }
        if (rowFailed || src.failed()) {
            page.lineCount = savedLines;
            page.textLen = savedText;
            page.missingGlyphs = savedMissing;
            if (src.failed()) {
                return TT_TABLE_FAIL;
            }
            if (placed || pageHadLines) {
                end = row;
                break;
            }
            end = tt_md_skip_line(src, row);
            LOG_W("Reader table: drop row at %u, line buffer full", (unsigned)row);
            if (end <= offset) {
                return TT_TABLE_FAIL;
            }
            break;
        }
        if (maxBand == 0) {
            maxBand = 1;
        }
        const int16_t textY = (int16_t)(y + topBorder + TT_READER_TABLE_PAD_Y);
        for (uint16_t i = savedLines; i < page.lineCount; i++) {
            TTReaderLine& line = page.lines[i];
            line.y = (int16_t)(textY + line.extra * TT_READER_CODE_PITCH);
            line.extra = 0;
        }
        if (!placed && !appendRule(page, TT_READER_LINE_RULE, 0, y, gridX[cols])) {
            page.lineCount = savedLines;
            page.textLen = savedText;
            page.missingGlyphs = savedMissing;
            end = row;
            break;
        }
        if (!placed) {
            topY = y;
        }
        const int16_t ruleY = (int16_t)(textY + maxBand * TT_READER_CODE_PITCH + TT_READER_TABLE_PAD_Y);
        if (!appendRule(page, TT_READER_LINE_RULE, 0, ruleY, gridX[cols])) {
            page.lineCount = savedLines;
            page.textLen = savedText;
            page.missingGlyphs = savedMissing;
            end = row;
            break;
        }
        bottomY = ruleY;
        y = (int16_t)(ruleY + TT_READER_TABLE_BORDER);
        placed = true;
        if (rowClipped && !reported) {
            reported = true;
            LOG_W("Reader table: row at %u clipped to %d bands", (unsigned)row, (int)bandsFit);
        }
        const uint32_t next = tt_md_skip_line(src, row);
        if (next <= row) {
            row = table.end;
            break;
        }
        row = next == table.separator ? table.body : next;
    }

    if (!placed) {
        if (end < offset) {
            end = offset;
        }
        if (page.lineCount > 0 || end > offset) {
            return TT_TABLE_BREAK;
        }
        return TT_TABLE_NONE;
    }
    for (uint8_t c = 0; c <= cols; c++) {
        if (!appendRule(page, TT_READER_LINE_VRULE, gridX[c], topY, bottomY)) {
            LOG_W("Reader table: vertical rule dropped");
            break;
        }
    }
    y = (int16_t)(y + TT_READER_LINE_GAP);
    end = row >= table.end ? table.end : row;
    LOG_D("Reader table: cols=%u end=%u y=%d", (unsigned)cols, (unsigned)end, (int)y);
    return end >= table.end ? TT_TABLE_DONE : TT_TABLE_BREAK;
}
