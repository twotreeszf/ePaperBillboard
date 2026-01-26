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
  --font fonts/WenQuanZhengHei.ttf \
  --size 14 \
  --bpp 1 \
  --format bin \
  --no-compress \
  --range 0x20-0x7E \
  --range 0x4E00-0x9FFF \
  -o data/font14.bin
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
| `0x4E00-0x9FFF` | CJK Unified Ideographs |

### File Size Estimates

| Content | Approximate Size |
|---------|------------------|
| ASCII only (95 chars) | ~5 KB |
| ASCII + GB2312 (~7000 chars) | ~500-600 KB |
| Full CJK (~20000 chars) | ~1.5-2 MB |

### Usage in Code

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

### LVGL Integration (Optional)

For LVGL-based UI, configure for E-Ink:

```c
// Disable auto-refresh timer
lv_display_delete_refr_timer(display);

// Manual refresh control
lv_label_set_text(label, "Updated");
lv_refr_now(display);
```

| Setting | Value | Reason |
|---------|-------|--------|
| Render Mode | `PARTIAL` | Only refresh changed areas |
| Refresh Timer | Disabled | Manual control |
| Animations | Disabled | Prevent continuous updates |

---

## Project Structure

```
ePaperBillboard/
├── src/
│   ├── main.cpp              # Main application
│   ├── Base/
│   │   ├── Logger.h/cpp      # Logging utilities
│   │   ├── TTFontLoader.h/cpp # Custom font loader
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
