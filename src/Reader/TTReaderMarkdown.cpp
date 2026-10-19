#include "TTReaderMarkdown.h"
#include "TTReaderText.h"

static int mdByte(TTReaderSource& src, uint32_t offset) {
    uint8_t b = 0;
    return src.byteAt(offset, b) ? (int)b : -1;
}

static bool mdIsLineEnd(int b) {
    return b < 0 || b == '\n' || b == '\r';
}

static bool mdIsBlankByte(int b) {
    return mdIsLineEnd(b) || b == ' ' || b == '\t';
}

static bool mdIsWordByte(int b) {
    return (b >= '0' && b <= '9') || (b >= 'A' && b <= 'Z') || (b >= 'a' && b <= 'z');
}

static bool mdIsPunct(int b) {
    return b > ' ' && b < 0x7F && !mdIsWordByte(b);
}

static uint32_t mdSkipInlineSpaces(TTReaderSource& src, uint32_t offset) {
    int b = mdByte(src, offset);
    while (b == ' ' || b == '\t') {
        b = mdByte(src, ++offset);
    }
    return offset;
}

uint32_t tt_md_skip_indent(TTReaderSource& src, uint32_t offset, uint8_t& columns) {
    while (offset < src.size()) {
        uint32_t next = offset;
        const uint32_t cp = src.next(next);
        if (src.failed()) {
            return src.size();
        }
        uint16_t add = 0;
        if (cp == ' ' || cp == 0x00A0) {
            add = 1;
        } else if (cp == '\t') {
            add = TT_MD_TAB_COLUMNS;
        } else if (cp == 0x3000) {
            add = TT_MD_WIDE_SPACE_COLUMNS;
        } else if (!tt_reader_is_ignorable(cp)) {
            return offset;
        }
        const uint16_t total = (uint16_t)(columns + add);
        columns = (uint8_t)(total > TT_MD_COLUMNS_MAX ? TT_MD_COLUMNS_MAX : total);
        offset = next;
    }
    return offset;
}

static bool mdIsFenceLine(TTReaderSource& src, uint32_t offset) {
    uint8_t columns = 0;
    const uint32_t content = tt_md_skip_indent(src, offset, columns);
    return content < src.size() && columns <= TT_MD_FENCE_INDENT_MAX && tt_md_is_fence(src, content);
}

static bool mdIsRule(TTReaderSource& src, uint32_t offset) {
    const int mark = mdByte(src, offset);
    if (mark != '-' && mark != '*' && mark != '_') {
        return false;
    }
    uint16_t count = 0;
    for (uint32_t i = 0; i < TT_MD_RULE_SCAN_MAX; i++) {
        const int b = mdByte(src, offset + i);
        if (mdIsLineEnd(b)) {
            return count >= TT_MD_RULE_MIN;
        }
        if (b == mark) {
            count++;
        } else if (b != ' ' && b != '\t') {
            return false;
        }
    }
    return false;
}

static bool mdHasLinkEnd(TTReaderSource& src, uint32_t offset) {
    for (uint32_t i = 0; i < TT_MD_LINK_SCAN_MAX; i++) {
        const int b = mdByte(src, offset + i);
        if (mdIsLineEnd(b)) {
            return false;
        }
        if (b == ']' && mdByte(src, offset + i + 1) == '(') {
            return true;
        }
    }
    return false;
}

static bool mdMatch(TTReaderSource& src, uint32_t offset, const char* text) {
    for (uint32_t i = 0; text[i] != '\0'; i++) {
        if (mdByte(src, offset + i) != (uint8_t)text[i]) {
            return false;
        }
    }
    return true;
}

static uint8_t mdLevel(uint8_t value, uint8_t max) {
    return value > max ? max : value;
}

uint32_t tt_md_skip_blank(TTReaderSource& src, uint32_t offset, uint8_t& columns) {
    columns = 0;
    while (true) {
        offset = tt_md_skip_indent(src, offset, columns);
        if (offset >= src.size()) {
            return src.size();
        }
        const int b = mdByte(src, offset);
        if (b != '\n' && b != '\r') {
            return offset;
        }
        columns = 0;
        offset++;
    }
}

uint32_t tt_md_skip_line(TTReaderSource& src, uint32_t offset) {
    while (offset < src.size()) {
        const int b = mdByte(src, offset);
        if (b < 0) {
            return src.size();
        }
        offset++;
        if (b == '\n') {
            return offset;
        }
        if (b == '\r') {
            if (mdByte(src, offset) == '\n') {
                offset++;
            }
            return offset;
        }
    }
    return src.size();
}

bool tt_md_is_fence(TTReaderSource& src, uint32_t offset) {
    const int mark = mdByte(src, offset);
    if (mark != '`' && mark != '~') {
        return false;
    }
    for (uint32_t i = 1; i < TT_MD_FENCE_LEN; i++) {
        if (mdByte(src, offset + i) != mark) {
            return false;
        }
    }
    return true;
}

void tt_md_line_context(TTReaderSource& src, uint32_t offset, uint32_t& lineStart, bool& fence) {
    const uint32_t from = offset > TT_MD_CONTEXT_MAX ? offset - TT_MD_CONTEXT_MAX : 0;
    lineStart = from;
    fence = false;
    bool open = false;
    bool atLine = from == 0;
    for (uint32_t p = from; p <= offset && p < src.size(); p++) {
        if (atLine) {
            lineStart = p;
            fence = open;
            if (p < offset && mdIsFenceLine(src, p)) {
                open = !open;
            }
        }
        if (p == offset) {
            break;
        }
        const int b = mdByte(src, p);
        if (b < 0) {
            return;
        }
        atLine = b == '\n' || b == '\r';
    }
}

TTMdBlock tt_md_parse_block(TTReaderSource& src, uint32_t& offset, uint8_t columns) {
    TTMdBlock block;
    const int b = mdByte(src, offset);
    if (b == '#') {
        uint32_t end = offset;
        uint8_t count = 0;
        while (count <= TT_MD_HEADING_MAX && mdByte(src, end) == '#') {
            end++;
            count++;
        }
        if (count <= TT_MD_HEADING_MAX && mdIsBlankByte(mdByte(src, end))) {
            block.kind = TT_MD_HEADING;
            block.level = count;
            offset = mdSkipInlineSpaces(src, end);
        }
        return block;
    }
    if (b == '>') {
        uint32_t end = offset;
        uint8_t depth = 0;
        while (mdByte(src, end) == '>') {
            depth++;
            end++;
            if (mdByte(src, end) == ' ') {
                end++;
            }
        }
        block.kind = TT_MD_QUOTE;
        block.level = mdLevel(depth, TT_MD_QUOTE_MAX);
        offset = mdSkipInlineSpaces(src, end);
        return block;
    }
    if (mdIsRule(src, offset)) {
        block.kind = TT_MD_RULE;
        return block;
    }
    const uint8_t level = mdLevel((uint8_t)(columns / TT_MD_LIST_LEVEL_COLUMNS), TT_MD_LIST_LEVEL_MAX);
    if ((b == '-' || b == '*' || b == '+') && (mdByte(src, offset + 1) == ' ' || mdByte(src, offset + 1) == '\t')) {
        block.kind = TT_MD_BULLET;
        block.level = level;
        offset = mdSkipInlineSpaces(src, offset + 1);
        return block;
    }
    if (b >= '0' && b <= '9') {
        uint32_t end = offset;
        uint8_t digits = 0;
        int c = mdByte(src, end);
        while (digits < TT_MD_ORDERED_DIGITS_MAX && c >= '0' && c <= '9') {
            digits++;
            c = mdByte(src, ++end);
        }
        const int after = mdByte(src, end + 1);
        if ((c == '.' || c == ')') && (after == ' ' || after == '\t')) {
            block.kind = TT_MD_ORDERED;
            block.level = level;
        }
    }
    return block;
}

uint32_t tt_md_next(TTReaderSource& src, uint32_t& offset, TTMdInline& state) {
    const uint32_t size = src.size();
    while (offset < size) {
        if (state.literal > 0) {
            state.literal--;
            offset++;
            return state.literalChar;
        }
        uint32_t next = offset;
        const uint32_t cp = src.next(next);
        if (src.failed()) {
            offset = size;
            return 0;
        }
        if (cp == '\n' || cp == '\r') {
            offset = next;
            return cp;
        }
        if (state.url) {
            offset = next;
            if (cp == ')') {
                state.url = false;
            }
            continue;
        }
        if (state.code) {
            offset = next;
            if (cp == '`') {
                state.code = false;
                continue;
            }
            return cp;
        }
        const int after = mdByte(src, next);
        switch (cp) {
            case '\\':
                if (mdIsPunct(after)) {
                    offset = next + 1;
                    return (uint32_t)after;
                }
                if (mdIsLineEnd(after)) {
                    offset = next;
                    continue;
                }
                break;
            case '`':
                offset = next;
                state.code = true;
                continue;
            case '*':
            case '_': {
                uint8_t run = 1;
                uint32_t end = next;
                while (run < TT_MD_EMPHASIS_RUN_MAX && mdByte(src, end) == (int)cp) {
                    run++;
                    end++;
                }
                const int prev = offset > 0 ? mdByte(src, offset - 1) : ' ';
                const int follow = mdByte(src, end);
                const bool loose = mdIsBlankByte(prev) && mdIsBlankByte(follow);
                const bool intraword = cp == '_' && mdIsWordByte(prev) && mdIsWordByte(follow);
                if (loose || intraword) {
                    state.literal = (uint8_t)(run - 1);
                    state.literalChar = (uint8_t)cp;
                    offset = next;
                    return cp;
                }
                offset = end;
                if (run >= 2) {
                    state.toggles ^= TT_READER_STYLE_BOLD;
                }
                if (run != 2) {
                    state.toggles ^= TT_READER_STYLE_ITALIC;
                }
                continue;
            }
            case '~':
                if (after == '~') {
                    offset = next + 1;
                    state.toggles ^= TT_READER_STYLE_STRIKE;
                    continue;
                }
                break;
            case '!':
                if (after == '[' && !state.link && mdHasLinkEnd(src, next + 1)) {
                    offset = next + 1;
                    state.link = true;
                    continue;
                }
                break;
            case '[':
                if (!state.link && mdHasLinkEnd(src, next)) {
                    offset = next;
                    state.link = true;
                    continue;
                }
                break;
            case ']':
                if (state.link) {
                    state.link = false;
                    offset = next;
                    if (after == '(') {
                        offset = next + 1;
                        state.url = true;
                    }
                    continue;
                }
                break;
            case '<':
                if (mdMatch(src, next, "u>")) {
                    offset = next + 2;
                    state.toggles |= TT_READER_STYLE_UNDERLINE;
                    continue;
                }
                if (mdMatch(src, next, "/u>")) {
                    offset = next + 3;
                    state.toggles &= (uint8_t)~TT_READER_STYLE_UNDERLINE;
                    continue;
                }
                break;
            default:
                break;
        }
        offset = next;
        return cp;
    }
    return 0;
}

uint8_t tt_md_style(const TTMdCursor& cursor) {
    if (cursor.block.kind == TT_MD_CODE) {
        return 0;
    }
    uint8_t style = cursor.inl.toggles & TT_READER_STYLE_MASK;
    if (cursor.inl.link) {
        style |= TT_READER_STYLE_UNDERLINE;
    }
    if (cursor.block.kind == TT_MD_HEADING) {
        style |= TT_READER_STYLE_BOLD;
    } else if (cursor.block.kind == TT_MD_QUOTE) {
        style |= TT_READER_STYLE_ITALIC;
    }
    return style;
}
