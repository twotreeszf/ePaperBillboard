#pragma once

#include <lvgl.h>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#define TT_TEXT_FIT_ELLIPSIS "..."

inline uint32_t ttTextFitRead(const char* text, size_t len, size_t& index) {
    if (index >= len) {
        return 0;
    }
    const uint8_t lead = (uint8_t)text[index++];
    if (lead < 0x80) {
        return lead;
    }
    if ((lead & 0xE0) == 0xC0 && index < len) {
        const uint8_t b1 = (uint8_t)text[index++];
        return ((uint32_t)(lead & 0x1F) << 6) | (b1 & 0x3F);
    }
    if ((lead & 0xF0) == 0xE0 && index + 1 < len) {
        const uint8_t b1 = (uint8_t)text[index++];
        const uint8_t b2 = (uint8_t)text[index++];
        return ((uint32_t)(lead & 0x0F) << 12) | ((uint32_t)(b1 & 0x3F) << 6) | (b2 & 0x3F);
    }
    if ((lead & 0xF8) == 0xF0 && index + 2 < len) {
        const uint8_t b1 = (uint8_t)text[index++];
        const uint8_t b2 = (uint8_t)text[index++];
        const uint8_t b3 = (uint8_t)text[index++];
        return ((uint32_t)(lead & 0x07) << 18) | ((uint32_t)(b1 & 0x3F) << 12)
            | ((uint32_t)(b2 & 0x3F) << 6) | (b3 & 0x3F);
    }
    return lead;
}

inline int32_t ttTextFitWidth(const lv_font_t* font, const char* text, int32_t letterSpace) {
    if (font == nullptr || text == nullptr || text[0] == '\0') {
        return 0;
    }
    int32_t width = 0;
    size_t index = 0;
    const size_t len = strlen(text);
    bool any = false;
    while (index < len) {
        const uint32_t cp = ttTextFitRead(text, len, index);
        if (cp == 0) {
            break;
        }
        if (any && letterSpace > 0) {
            width += letterSpace;
        }
        width += (int32_t)lv_font_get_glyph_width(font, cp, 0);
        any = true;
    }
    return width;
}

inline void ttTextFit(char* out, size_t outSize, const char* text, const lv_font_t* font,
                      int32_t maxWidth, int32_t letterSpace) {
    if (outSize == 0) {
        return;
    }
    out[0] = '\0';
    if (text == nullptr || font == nullptr || maxWidth <= 0) {
        return;
    }
    if (ttTextFitWidth(font, text, letterSpace) <= maxWidth) {
        snprintf(out, outSize, "%s", text);
        return;
    }
    const int32_t ellipsisWidth = ttTextFitWidth(font, TT_TEXT_FIT_ELLIPSIS, letterSpace);
    const int32_t limit = maxWidth > ellipsisWidth ? maxWidth - ellipsisWidth : 0;
    size_t index = 0;
    size_t keep = 0;
    int32_t width = 0;
    const size_t len = strlen(text);
    while (index < len) {
        const size_t at = index;
        const uint32_t cp = ttTextFitRead(text, len, index);
        if (cp == 0) {
            break;
        }
        const int32_t advance = (int32_t)lv_font_get_glyph_width(font, cp, 0);
        const int32_t next = width == 0 ? advance : width + letterSpace + advance;
        if (next > limit) {
            index = at;
            break;
        }
        width = next;
        keep = index;
    }
    if (keep >= outSize) {
        keep = outSize - 1;
        while (keep > 0 && ((uint8_t)text[keep] & 0xC0) == 0x80) {
            keep--;
        }
    }
    memcpy(out, text, keep);
    out[keep] = '\0';
    if (keep + strlen(TT_TEXT_FIT_ELLIPSIS) < outSize) {
        memcpy(out + keep, TT_TEXT_FIT_ELLIPSIS, strlen(TT_TEXT_FIT_ELLIPSIS) + 1);
    }
}
