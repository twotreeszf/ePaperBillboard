# Font Generation Guide

This document describes how to generate custom binary fonts for the TTFontLoader.

## Prerequisites

Install `lv_font_conv` globally:

```bash
npm install -g lv_font_conv
```

## Generating Font Files

### Basic Command

```bash
lv_font_conv \
  --font fonts/SourceHanSansCN-Regular.ttf \
  --size 14 \
  --bpp 1 \
  --format bin \
  --no-compress \
  --range 0x20-0x7E \
  --symbols "$(cat gb2312.txt)" \
  -o data/font14.bin
```

### Parameters Explained

| Parameter | Description |
|-----------|-------------|
| `--font` | Path to the source TTF font file |
| `--size` | Font size in pixels (e.g., 14, 16, 20) |
| `--bpp` | Bits per pixel (1 = monochrome, 2 = 4 grayscale levels) |
| `--format bin` | Output format (must be `bin` for TTFontLoader) |
| `--no-compress` | Disable RLE compression (required for current loader) |
| `--range` | Unicode range for ASCII characters |
| `--symbols` | Additional characters to include |
| `-o` | Output file path |

### Character Ranges

**ASCII (Basic Latin):**
```
--range 0x20-0x7E
```

**Chinese GB2312:**
Create a file `gb2312.txt` containing all GB2312 characters, or use ranges:
```
--range 0x4E00-0x9FFF
```

### Example: Full GB2312 + ASCII Font

```bash
# Generate 14px font with GB2312 and ASCII
lv_font_conv \
  --font fonts/SourceHanSansCN-Regular.ttf \
  --size 14 \
  --bpp 1 \
  --format bin \
  --no-compress \
  --range 0x20-0x7E \
  --range 0x4E00-0x9FFF \
  -o data/font14.bin
```

## Font File Format (LVF1)

The generated `.bin` file follows the LVGL Font Format (LVF1):

### Table Structure

| Table | Description |
|-------|-------------|
| `head` | Font metadata (size, ascent, descent, glyph metrics format) |
| `cmap` | Character to Glyph ID mapping |
| `loca` | Glyph offset table |
| `glyf` | Glyph bitmap data |
| `kern` | Kerning data (optional) |

### Key Fields in `head` Table

| Offset | Size | Field |
|--------|------|-------|
| 16 | 2 | Ascent |
| 18 | 2 | Descent |
| 34 | 1 | indexToLocFormat (0=16bit, 1=32bit) |
| 36 | 1 | advanceWidthFormat (0=int, 1=FP12.4) |
| 37 | 1 | bitsPerPixel |
| 38 | 1 | xy_bits (bits for bearingX/Y) |
| 39 | 1 | wh_bits (bits for boxW/H) |
| 40 | 1 | advance_width_bits |
| 41 | 1 | compression (0=none) |

### Glyph Data Format

Each glyph is stored as a bit-stream:
```
[advance_width: N bits]
[bearingX: M bits, signed]
[bearingY: M bits, signed]
[boxW: K bits]
[boxH: K bits]
[bitmap: boxW * boxH * bpp bits]
```

## Uploading to ESP8266

1. Place the font file in the `data/` directory
2. Upload using PlatformIO:

```bash
pio run --target uploadfs
```

## Usage in Code

```cpp
#include "Base/TTFontLoader.h"

TTFontLoader font;

void setup() {
    LittleFS.begin();
    font.begin("/font14.bin");
    font.setTextColor(GxEPD_BLACK);
}

void loop() {
    font.drawUTF8(display, 10, 30, "Hello 你好世界");
}
```

## File Size Estimates

| Content | Approximate Size |
|---------|------------------|
| ASCII only (95 chars) | ~5 KB |
| ASCII + GB2312 (~7000 chars) | ~500-600 KB |
| Full CJK (~20000 chars) | ~1.5-2 MB |

## Troubleshooting

### Characters not displaying
- Ensure the character is included in `--range` or `--symbols`
- Check that `--no-compress` is used (compression not yet supported)

### Font too large
- Reduce `--bpp` to 1
- Use font subsetting to include only needed characters
- Reduce `--size`

### Garbled text
- Verify font file uploaded correctly to LittleFS
- Check available LittleFS space with `LittleFS.info()`

## References

- [lv_font_conv GitHub](https://github.com/lvgl/lv_font_conv)
- [LVF1 Font Specification](https://github.com/lvgl/lv_font_conv/blob/master/doc/font_spec.md)
