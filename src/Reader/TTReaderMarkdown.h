#pragma once

#include <Arduino.h>
#include "TTReaderSource.h"

#define TT_READER_STYLE_BOLD        0x01
#define TT_READER_STYLE_ITALIC      0x02
#define TT_READER_STYLE_UNDERLINE   0x04
#define TT_READER_STYLE_STRIKE      0x08
#define TT_READER_STYLE_MASK        0x0F
#define TT_READER_STYLE_MARK        0x01
#define TT_READER_STYLE_MARK_FLAG   0x80
#define TT_READER_STYLE_MARK_BYTES  2

#define TT_MD_LAYOUT_VERSION        3
#define TT_MD_HEADING_MAX           6
#define TT_MD_QUOTE_MAX             2
#define TT_MD_LIST_LEVEL_MAX        3
#define TT_MD_LIST_LEVEL_COLUMNS    2
#define TT_MD_TAB_COLUMNS           4
#define TT_MD_WIDE_SPACE_COLUMNS    2
#define TT_MD_COLUMNS_MAX           32
#define TT_MD_ORDERED_DIGITS_MAX    9
#define TT_MD_FENCE_LEN             3
#define TT_MD_FENCE_INDENT_MAX      3
#define TT_MD_RULE_MIN              3
#define TT_MD_RULE_SCAN_MAX         128
#define TT_MD_LINK_SCAN_MAX         512
#define TT_MD_EMPHASIS_RUN_MAX      3
#define TT_MD_CONTEXT_MAX           (TT_READER_SOURCE_BUF_SIZE - 16)

enum TTMdBlockKind : uint8_t {
    TT_MD_PARAGRAPH = 0,
    TT_MD_HEADING,
    TT_MD_QUOTE,
    TT_MD_BULLET,
    TT_MD_ORDERED,
    TT_MD_RULE,
    TT_MD_CODE,
};

struct TTMdBlock {
    TTMdBlockKind kind = TT_MD_PARAGRAPH;
    uint8_t level = 0;
};

struct TTMdInline {
    uint8_t toggles = 0;
    uint8_t literal = 0;
    uint8_t literalChar = 0;
    bool code = false;
    bool link = false;
    bool url = false;
};

struct TTMdCursor {
    TTMdBlock block;
    TTMdInline inl;
    bool fence = false;
};

uint32_t tt_md_skip_indent(TTReaderSource& src, uint32_t offset, uint8_t& columns);
uint32_t tt_md_skip_blank(TTReaderSource& src, uint32_t offset, uint8_t& columns);
uint32_t tt_md_skip_line(TTReaderSource& src, uint32_t offset);
bool tt_md_is_fence(TTReaderSource& src, uint32_t offset);
void tt_md_line_context(TTReaderSource& src, uint32_t offset, uint32_t& lineStart, bool& fence);
TTMdBlock tt_md_parse_block(TTReaderSource& src, uint32_t& offset, uint8_t columns);
uint32_t tt_md_next(TTReaderSource& src, uint32_t& offset, TTMdInline& state);
uint8_t tt_md_style(const TTMdCursor& cursor);
