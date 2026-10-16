#pragma once

#include <stddef.h>
#include <stdint.h>

#define TT_UTF8_REPLACEMENT      0xFFFD
#define TT_UTF8_MAX_BYTES        4
#define TT_READER_HASH_SEED      2166136261u
#define TT_READER_HASH_PRIME     16777619u

size_t tt_utf8_decode(const uint8_t* p, size_t avail, uint32_t* cp);
size_t tt_utf8_encode(uint32_t cp, char* out);
size_t tt_utf8_count_invalid(const uint8_t* p, size_t len);
uint32_t tt_reader_hash(const void* data, size_t len, uint32_t seed = TT_READER_HASH_SEED);

bool tt_reader_is_space(uint32_t cp);
bool tt_reader_is_para_space(uint32_t cp);
bool tt_reader_is_ignorable(uint32_t cp);
bool tt_reader_is_wide(uint32_t cp);
bool tt_reader_no_line_start(uint32_t cp);
bool tt_reader_no_line_end(uint32_t cp);
bool tt_reader_can_break(uint32_t prev, uint32_t cur);
