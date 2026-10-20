#include "TTReaderText.h"

static bool tt_utf8_is_cont(uint8_t b) {
    return (b & 0xC0) == 0x80;
}

size_t tt_utf8_decode(const uint8_t* p, size_t avail, uint32_t* cp) {
    if (avail == 0) {
        *cp = 0;
        return 0;
    }
    const uint8_t b0 = p[0];
    if (b0 < 0x80) {
        *cp = b0;
        return 1;
    }
    size_t len = 0;
    uint32_t value = 0;
    uint32_t min = 0;
    if (b0 >= 0xC2 && b0 <= 0xDF) {
        len = 2;
        value = b0 & 0x1F;
        min = 0x80;
    } else if ((b0 & 0xF0) == 0xE0) {
        len = 3;
        value = b0 & 0x0F;
        min = 0x800;
    } else if (b0 >= 0xF0 && b0 <= 0xF4) {
        len = 4;
        value = b0 & 0x07;
        min = 0x10000;
    } else {
        *cp = TT_UTF8_REPLACEMENT;
        return 1;
    }
    if (avail < len) {
        *cp = TT_UTF8_REPLACEMENT;
        return 1;
    }
    for (size_t i = 1; i < len; i++) {
        if (!tt_utf8_is_cont(p[i])) {
            *cp = TT_UTF8_REPLACEMENT;
            return 1;
        }
        value = (value << 6) | (p[i] & 0x3F);
    }
    if (value < min || value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF)) {
        *cp = TT_UTF8_REPLACEMENT;
        return 1;
    }
    *cp = value;
    return len;
}

size_t tt_utf8_encode(uint32_t cp, char* out) {
    if (cp < 0x80) {
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

size_t tt_utf8_count_invalid(const uint8_t* p, size_t len) {
    size_t invalid = 0;
    size_t pos = 0;
    while (pos < len) {
        const size_t remain = len - pos;
        uint32_t cp = 0;
        const size_t n = tt_utf8_decode(p + pos, remain, &cp);
        const bool truncatedTail = cp == TT_UTF8_REPLACEMENT && n == 1
            && remain < TT_UTF8_MAX_BYTES && p[pos] >= 0xC2;
        if (truncatedTail) {
            break;
        }
        if (cp == TT_UTF8_REPLACEMENT && n == 1) {
            invalid++;
        }
        pos += n;
    }
    return invalid;
}

uint32_t tt_reader_hash(const void* data, size_t len, uint32_t seed) {
    const uint8_t* p = (const uint8_t*)data;
    uint32_t h = seed;
    for (size_t i = 0; i < len; i++) {
        h ^= p[i];
        h *= TT_READER_HASH_PRIME;
    }
    return h;
}

bool tt_reader_is_space(uint32_t cp) {
    return cp == ' ' || cp == '\t';
}

bool tt_reader_is_para_space(uint32_t cp) {
    return tt_reader_is_space(cp) || cp == '\r' || cp == '\n' || cp == 0x00A0 || cp == 0x3000;
}

bool tt_reader_is_ignorable(uint32_t cp) {
    if (cp < 0x20) {
        return cp != '\t' && cp != '\r' && cp != '\n';
    }
    if (cp >= 0x7F && cp <= 0x9F) {
        return true;
    }
    return cp == 0xFEFF || (cp >= 0x200B && cp <= 0x200F);
}

bool tt_reader_is_ideograph(uint32_t cp) {
    if (cp >= 0x3400 && cp <= 0x4DBF) {
        return true;
    }
    if (cp >= 0x4E00 && cp <= 0x9FFF) {
        return true;
    }
    if (cp >= 0xF900 && cp <= 0xFAFF) {
        return true;
    }
    return cp >= 0x20000 && cp <= 0x2FFFF;
}

bool tt_reader_is_cjk_punct(uint32_t cp) {
    if (cp >= 0x3001 && cp <= 0x303F) {
        return true;
    }
    if ((cp >= 0xFF01 && cp <= 0xFF0F) || (cp >= 0xFF1A && cp <= 0xFF20)
        || (cp >= 0xFF3B && cp <= 0xFF40) || (cp >= 0xFF5B && cp <= 0xFF65)) {
        return true;
    }
    switch (cp) {
        case 0x00B7:
        case 0x2014:
        case 0x2015:
        case 0x2018:
        case 0x2019:
        case 0x201C:
        case 0x201D:
        case 0x2026:
        case 0x2030:
        case 0x30FB:
            return true;
        default:
            return false;
    }
}

bool tt_reader_is_emphasis_char(uint32_t cp) {
    return tt_reader_is_ideograph(cp) || tt_reader_is_cjk_punct(cp);
}

bool tt_reader_is_wide(uint32_t cp) {
    if (cp >= 0x2E80 && cp <= 0x9FFF) {
        return true;
    }
    if (cp >= 0xAC00 && cp <= 0xD7AF) {
        return true;
    }
    if (cp >= 0xF900 && cp <= 0xFAFF) {
        return true;
    }
    if (cp >= 0xFE30 && cp <= 0xFE4F) {
        return true;
    }
    if (cp >= 0xFF00 && cp <= 0xFFEF) {
        return true;
    }
    if (cp >= 0x20000 && cp <= 0x2FFFF) {
        return true;
    }
    switch (cp) {
        case 0x00B7:
        case 0x2014:
        case 0x2015:
        case 0x2026:
            return true;
        default:
            return false;
    }
}

bool tt_reader_no_line_start(uint32_t cp) {
    switch (cp) {
        case ',':
        case '.':
        case ';':
        case ':':
        case '?':
        case '!':
        case ')':
        case ']':
        case '}':
        case '%':
        case 0x00B7:
        case 0x2014:
        case 0x2019:
        case 0x201D:
        case 0x2026:
        case 0x2030:
        case 0x3001:
        case 0x3002:
        case 0x3009:
        case 0x300B:
        case 0x300D:
        case 0x300F:
        case 0x3011:
        case 0x3015:
        case 0x3017:
        case 0x3019:
        case 0x301B:
        case 0x30FB:
        case 0xFF01:
        case 0xFF05:
        case 0xFF09:
        case 0xFF0C:
        case 0xFF0E:
        case 0xFF1A:
        case 0xFF1B:
        case 0xFF1F:
        case 0xFF3D:
        case 0xFF5D:
        case 0xFF5E:
            return true;
        default:
            return false;
    }
}

bool tt_reader_no_line_end(uint32_t cp) {
    switch (cp) {
        case '(':
        case '[':
        case '{':
        case 0x2018:
        case 0x201C:
        case 0x3008:
        case 0x300A:
        case 0x300C:
        case 0x300E:
        case 0x3010:
        case 0x3014:
        case 0x3016:
        case 0x3018:
        case 0x301A:
        case 0xFF08:
        case 0xFF3B:
        case 0xFF5B:
            return true;
        default:
            return false;
    }
}

bool tt_reader_can_break(uint32_t prev, uint32_t cur) {
    if (tt_reader_is_space(cur)) {
        return false;
    }
    if (tt_reader_is_space(prev)) {
        return true;
    }
    if (tt_reader_no_line_start(cur) || tt_reader_no_line_end(prev)) {
        return false;
    }
    if (tt_reader_is_wide(prev) || tt_reader_is_wide(cur)) {
        return true;
    }
    return prev == '-' && cur < 0x80 && cur > ' ';
}
