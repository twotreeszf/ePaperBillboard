# ESP32 E-Paper Billboard

ESP32-WROOM-32E + 2.9" E-Paper Display (E029A01) project with custom Chinese font support.

## Hardware

### Components

- **MCU**: ESP32-WROOM-32E (8MB Flash)
- **Display**: E029A01 2.9" E-Paper (128x296, IL3820 driver)

### Wiring

| E-Paper | ESP32 GPIO | Description |
|---------|------------|-------------|
| VCC | 3.3V | Power |
| GND | GND | Ground |
| DIN | GPIO4 | SPI MOSI |
| CLK | GPIO16 | SPI SCK |
| CS | GPIO17 | Chip Select |
| DC | GPIO5 | Data/Command |
| RST | GPIO18 | Reset |
| BUSY | GPIO19 | Busy Signal |

## Build & Flash

### Prerequisites

- [PlatformIO](https://platformio.org/) installed
- USB cable connected to ESP32

### Initial Setup (First Time)

For fresh ESP32 with 8MB flash, run these commands in order:

```bash
# 1. Erase entire flash (important for partition table change)
pio run --target erase

# 2. Build and upload firmware
pio run --target upload

# 3. Upload filesystem (fonts and data)
pio run --target uploadfs

# 4. Monitor serial output
pio device monitor
```

### Regular Development

```bash
# Build and upload firmware only
pio run --target upload

# Monitor serial output
pio device monitor

# Upload filesystem (after changing files in data/)
pio run --target uploadfs
```

### Flash Configuration

This project uses a custom 8MB partition table (`partitions_8MB.csv`):

| Partition | Size | Description |
|-----------|------|-------------|
| app0 | 2 MB | Main application (OTA slot 0) |
| app1 | 2 MB | OTA slot 1 |
| spiffs | 3.875 MB | LittleFS filesystem |
| coredump | 64 KB | Core dump storage |

### Troubleshooting

**No serial output after flash:**
1. Ensure flash is completely erased: `pio run --target erase`
2. Re-upload firmware and filesystem
3. Press RST button on ESP32

**Upload filesystem failed:**
- Make sure both `board_build.flash_size` and `board_upload.flash_size` are set to `8MB`

---

## Font Generation

Generate custom binary fonts for the TTFontLoader using `lv_font_conv`.

### Prerequisites

```bash
npm install -g lv_font_conv
```

### Basic Command

```bash
lv_font_conv \
  --font fonts/中易宋体.ttf \
  --size 14 \
  --bpp 1 \
  --format bin \
  --no-compress \
  --range 0x20-0x7E \
  --range 0x3000-0x303F \
  --range 0xFF00-0xFFEF \
  --range 0x4E00-0x9FFF \
  -o data/fonts/chs_14.bin
```

### Parameters

| Parameter | Description |
|-----------|-------------|
| `--font` | Source TTF font file |
| `--size` | Font size in pixels |
| `--bpp` | Bits per pixel (1 = monochrome) |
| `--format bin` | Output format for TTFontLoader |
| `--no-compress` | Disable compression (required) |
| `--range` | Unicode range to include |
| `-o` | Output file path |

### Character Ranges

| Range | Description |
|-------|-------------|
| `0x20-0x7E` | ASCII (Basic Latin) |
| `0x3000-0x303F` | CJK Symbols and Punctuation (`。`、`「」` etc.) |
| `0xFF00-0xFFEF` | Fullwidth Forms (`，`、`：`、`？`、`！` etc.) |
| `0x4E00-0x9FFF` | CJK Unified Ideographs |

### File Size Estimates

| Content | Approximate Size |
|---------|------------------|
| ASCII only (95 chars) | ~5 KB |
| ASCII + GB2312 (~7000 chars) | ~500-600 KB |
| Full CJK (~20000 chars) | ~1.5-2 MB |

### English Font Extraction

For English-only fonts (e.g., pixel fonts), use `analyze_ttf_cmap.py` to extract encoding ranges.

#### 1. Setup Python Environment

```bash
cd tools
python3 -m venv venv
source venv/bin/activate  # macOS/Linux
pip install -r requirements.txt
```

#### 2. Analyze TTF Font Encoding

```bash
source tools/venv/bin/activate
python tools/analyze_ttf_cmap.py fonts/your_font.ttf
```

Output example:
```
# Font: Post Pixel-7
# Total Characters: 312
# Total Ranges: 25

--range=0x0020-0x007E,0x00A0-0x00AE,0x00B0-0x00FF,...
```

#### 3. Generate Font with Extracted Ranges

Copy the `--range=...` output and use it directly:

```bash
lv_font_conv \
  --font fonts/post_pixel-7.ttf \
  --size 16 \
  --bpp 1 \
  --format bin \
  --no-compress \
  --range=0x0020-0x007E,0x00A0-0x00AE,0x00B0-0x00FF,... \
  -o data/fonts/en_16.bin
```

#### 4. Dual-Font Setup (Chinese + English)

TTFontLoader supports fallback fonts. Characters in the fallback font are rendered with priority:

```cpp
// Load Chinese as main font, English as fallback (priority)
fontLoader.begin("/fonts/chs_16.bin", "/fonts/en_16.bin");
```

This allows mixing Chinese text with stylized English fonts (e.g., pixel fonts).

### Usage in Code

```cpp
#include "Base/TTFontLoader.h"

TTFontLoader font;

void setup() {
    LittleFS.begin();
    // Single font
    font.begin("/fonts/chs_14.bin");
    // Or dual font (English font has priority)
    font.begin("/fonts/chs_14.bin", "/fonts/en_14.bin");
}

void loop() {
    font.drawUTF8(display, 10, 30, "Hello 你好世界");
}
```

---

## E-Ink Refresh Strategy

### Partial vs Full Refresh

E-Ink displays support two refresh modes:

| Mode | Speed | Ghosting | Use Case |
|------|-------|----------|----------|
| Partial | Fast (~300ms) | May accumulate | Frequent updates |
| Full | Slow (~2s) | Clears completely | Periodic cleanup |

### Recommended Strategy

```cpp
#define FULL_REFRESH_INTERVAL 10  // Full refresh every 10 partial refreshes

static uint8_t partialRefreshCount = 0;

void refreshDisplay() {
    partialRefreshCount++;
    
    if (partialRefreshCount >= FULL_REFRESH_INTERVAL) {
        display.setFullWindow();
        // ... draw content
        display.display(false);  // Full refresh
        partialRefreshCount = 0;
    } else {
        display.setPartialWindow(x, y, w, h);
        // ... draw content
        display.display(true);   // Partial refresh
    }
}
```

### LVGL Integration

LVGL 9.x integration for E-Paper displays with optimized monochrome rendering.

#### Architecture Overview

```
┌─────────────┐     ┌──────────────┐     ┌─────────────┐
│   LVGL UI   │ ──> │  LvglDriver  │ ──> │  GxEPD2_BW  │ ──> E-Paper
│  (Widgets)  │     │ (Flush CB)   │     │  (SPI/EPD)  │
└─────────────┘     └──────────────┘     └─────────────┘
```

#### Display Configuration (`lv_conf.h`)

Key settings for E-Paper:

```c
// Monochrome color depth
#define LV_COLOR_DEPTH 1

// Minimal memory for embedded systems
#define LV_MEM_SIZE (32 * 1024U)

// Monochrome fonts (bitmap, no anti-aliasing)
#define LV_FONT_UNSCII_8  1
#define LV_FONT_UNSCII_16 1
#define LV_FONT_DEFAULT &lv_font_unscii_16

// Mono theme for E-Paper styling
#define LV_USE_THEME_MONO 1

// Disable animations (meaningless on E-Paper)
#define LV_USE_ANIM 0

// Only enable widgets you need
#define LV_USE_LABEL 1
```

#### Display Driver Initialization

```cpp
#include "LvglDriver.h"

// Create display with landscape dimensions
lv_display_t* disp = lv_display_create(296, 128);

// Set 1-bit color format (I1 = indexed 1-bit)
lv_display_set_color_format(disp, LV_COLOR_FORMAT_I1);

// Buffer size: (width * height / 8) + 8 bytes palette header
#define EPD_BUF_SIZE ((296 * 128 / 8) + 8)
static uint8_t drawBuf[EPD_BUF_SIZE];

// Single buffer, partial render mode
lv_display_set_buffers(disp, drawBuf, nullptr, sizeof(drawBuf), 
                       LV_DISPLAY_RENDER_MODE_PARTIAL);

// Set flush callback
lv_display_set_flush_cb(disp, flushCallback);
```

#### Flush Callback Implementation

Key points for the flush callback:

```cpp
void flushCallback(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
    // IMPORTANT: Skip 8-byte palette header in I1 format
    px_map += 8;
    
    int32_t x1 = area->x1, y1 = area->y1;
    int32_t x2 = area->x2, y2 = area->y2;
    int32_t w = x2 - x1 + 1;
    int32_t h = y2 - y1 + 1;
    
    // Buffer stride for partial area
    int32_t buf_stride = (w + 7) / 8;
    
    // Draw pixels
    for (int32_t y = y1; y <= y2; y++) {
        for (int32_t x = x1; x <= x2; x++) {
            int32_t rel_x = x - x1;
            int32_t rel_y = y - y1;
            
            int32_t byte_idx = rel_y * buf_stride + (rel_x / 8);
            int32_t bit_idx = 7 - (rel_x % 8);  // MSB first
            
            bool isSet = (px_map[byte_idx] >> bit_idx) & 0x01;
            
            // LVGL I1: 1 = foreground (white), 0 = background
            // Invert for E-Paper: white = paper, black = ink
            uint16_t color = isSet ? GxEPD_WHITE : GxEPD_BLACK;
            epd->drawPixel(x, y, color);
        }
    }
    
    // Always notify LVGL when done
    lv_display_flush_ready(disp);
}
```

#### Refresh Control

```cpp
void requestRefresh(bool fullRefresh) {
    // Invalidate screen to mark as dirty
    lv_obj_invalidate(lv_scr_act());
    
    // Trigger immediate render
    lv_refr_now(display);
}
```

| Setting | Value | Reason |
|---------|-------|--------|
| Color Format | `LV_COLOR_FORMAT_I1` | 1-bit indexed monochrome |
| Render Mode | `PARTIAL` | Only refresh changed areas |
| Buffer | Single | Sufficient for E-Paper (slow refresh) |
| Animations | Disabled | Prevent continuous updates |
| Palette Skip | 8 bytes | I1 format includes palette header |

#### UI Example

```cpp
void createUI() {
    lv_obj_t* scr = lv_scr_act();
    
    // White background
    lv_obj_set_style_bg_color(scr, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    
    // Create label with monochrome font
    lv_obj_t* label = lv_label_create(scr);
    lv_label_set_text(label, "Hello E-Paper");
    lv_obj_set_style_text_color(label, lv_color_black(), 0);
    lv_obj_set_style_text_font(label, &lv_font_unscii_16, 0);
    lv_obj_center(label);
}
```

---

## Project Structure

```
ePaperBillboard/
├── src/
│   ├── main.cpp              # Main application
│   ├── lv_conf.h             # LVGL configuration
│   ├── LvglDriver.h/cpp      # LVGL E-Paper display driver
│   ├── Base/
│   │   ├── Logger.h/cpp      # Logging utilities
│   │   ├── ErrorCheck.h      # Error handling macros
│   │   ├── TTStorage.h/cpp   # LittleFS wrapper
│   │   ├── TTPreference.h/cpp # Preferences storage
│   │   ├── TTWiFiManager.h/cpp # WiFi configuration
│   │   └── TTVTask.h/cpp     # FreeRTOS task wrapper
│   └── Tasks/
│       └── TTWiFiTask.h/cpp  # WiFi management task
├── data/
│   └── font14.bin            # Binary font file
├── fonts/
│   └── WenQuanZhengHei.ttf   # Source font
├── partitions_8MB.csv        # Custom partition table
└── platformio.ini            # PlatformIO configuration
```

## License

MIT
