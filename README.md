# ESP32 E-Paper Billboard

ESP32-WROOM-32E + 2.9" E-Paper Display (E029A01) project with LVGL 9, custom binary fonts (Chinese/English), and optional I2C sensors (AHT20, BMP280). UI is built as a page stack (navigation + notifications) with E-Paper-optimized three-level refresh (partial / full-screen partial / deep full).

**Contents:** [Hardware](#hardware) · [Build & Flash](#build--flash) · [Font Generation](#font-generation) · [E-Ink / LVGL](#e-ink-refresh-strategy) · [Software Architecture](#software-architecture) · [Keypad & focus](#keypad--focus)

## Hardware

### Components

- **MCU**: ESP32-WROOM-32E (8MB Flash)
- **Display**: E029A01 2.9" E-Paper (128x296, IL3820 driver)

### Wiring

**E-Paper (SPI)**

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

**Sensors (I2C, optional)**

| Sensor | ESP32 GPIO | Description |
|--------|------------|-------------|
| SDA | GPIO23 | I2C Data |
| SCL | GPIO22 | I2C Clock |

AHT20 (0x38) and BMP280 (0x76/0x77) share the same I2C bus.

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
| spiffs | ~3.875 MB | LittleFS filesystem (fonts, config) |
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

Binary fonts for TTFontLoader are produced by `lv_font_conv`. The project provides **generate_fonts.py** for batch generation with automatic range extraction.

### Batch Generation (generate_fonts.py)

Generates multiple sizes from one TTF: reads the font cmap, optionally excludes CJK extension ranges to reduce size (Simplified Chinese), and calls `lv_font_conv` for each size. Output goes to `data/fonts/`.

#### Prerequisites

```bash
npm install -g lv_font_conv
cd tools
python3 -m venv venv
source venv/bin/activate   # macOS/Linux
pip install -r requirements.txt   # fonttools
```

#### Usage

```text
python tools/generate_fonts.py <font.ttf> <sizes> [prefix] [size_offset]
```

| Argument     | Description |
|-------------|-------------|
| `font.ttf`  | Source TTF font path |
| `sizes`     | Comma-separated sizes (e.g. `12,14,16`) |
| `prefix`    | Output filename prefix (default `en`) → `{prefix}_{size}.bin` |
| `size_offset` | Added to each size for actual rendering (default 0) |

#### Examples

```bash
# From project root
python tools/generate_fonts.py fonts/pixel.ttf 12,14,16
# → data/fonts/en_12.bin, en_14.bin, en_16.bin

python tools/generate_fonts.py fonts/pixel.ttf 12,14,16 pixel
# → data/fonts/pixel_12.bin, pixel_14.bin, pixel_16.bin

python tools/generate_fonts.py fonts/pixel.ttf 12,14,16 pixel 2
# → pixel_12.bin (14px), pixel_14.bin (16px), pixel_16.bin (18px)
```

Script behavior: encoding ranges are read from the TTF; ranges in `EXCLUDED_RANGES` (CJK Extension A/B/C/…, Compatibility Ideographs) are subtracted to keep files smaller when only Simplified Chinese is needed.

### Manual lv_font_conv

For a single size or fixed ranges:

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

| Parameter     | Description |
|---------------|-------------|
| `--font`      | Source TTF |
| `--size`      | Font size (px) |
| `--bpp`       | 1 = monochrome |
| `--format bin`| Output for TTFontLoader |
| `--no-compress` | Required |
| `--range`     | Unicode range(s) |
| `-o`          | Output path |

Common ranges: `0x20-0x7E` (ASCII), `0x3000-0x303F` (CJK punctuation), `0xFF00-0xFFEF` (fullwidth), `0x4E00-0x9FFF` (CJK Unified Ideographs). File size: ASCII only ~5 KB; ASCII + CJK (e.g. GB) ~500 KB–2 MB depending on ranges.

### Getting Ranges for Manual Use (analyze_ttf_cmap.py)

When using `lv_font_conv` by hand, you can get `--range=...` from the TTF:

```bash
source tools/venv/bin/activate
python tools/analyze_ttf_cmap.py fonts/your_font.ttf
```

Example output:

```text
# Font: Post Pixel-7
# Total Characters: 312
# Total Ranges: 25
--range=0x0020-0x007E,0x00A0-0x00AE,0x00B0-0x00FF,...
```

Copy the `--range=...` line into your `lv_font_conv` command.

### Dual-Font Setup (Chinese + English)

TTFontLoader supports a main font plus fallback; the fallback is used with priority for characters it contains:

```cpp
fontLoader.begin("/fonts/chs_16.bin", "/fonts/en_16.bin");
```

Useful for mixing Chinese with a stylized English font (e.g. pixel). Font paths are configured in `TTFontManager.cpp` (e.g. `TT_FONT_ENTRIES`).

### Usage in Code

Fonts are managed by `TTFontManager` (uses `TTFontLoader` per size). LittleFS must be mounted first.

```cpp
#include "Base/TTFontManager.h"

// In setup (after LittleFS.begin()):
TTFontManager::instance().begin();  // Loads fonts from data/fonts/ (see TTFontManager.cpp)

// In page buildContent():
lv_font_t* font_16 = TTFontManager::instance().getFont(16);
lv_obj_set_style_text_font(label, font_16, 0);
```

---

## E-Ink Refresh Strategy

### LVGL Integration

LVGL 9.x integration for E-Paper displays with optimized monochrome rendering.

#### Architecture Overview

```
┌─────────────┐     ┌──────────────┐     ┌─────────────┐
│   LVGL UI   │ ──> │  LvglDriver  │ ──> │  GxEPD2_BW  │ ──> E-Paper
│  (Widgets)  │     │ (Flush CB)   │     │  (SPI/EPD)  │
└─────────────┘     └──────────────┘     └─────────────┘
```

#### Display Configuration

LVGL config: `include/lv_conf.h`. Key settings for E-Paper:

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

`TTLvglEpdDriver` wraps LVGL display and GxEPD2. In code:

```cpp
#include "Base/TTLvglEpdDriver.h"

// After GxEPD2 display init: begin() creates LVGL display (296×128), I1 format, partial buffer
TTInstanceOf<TTLvglEpdDriver>().begin(epdDisplay);
lv_display_t* disp = TTInstanceOf<TTLvglEpdDriver>().getDisplay();

// Request E-Paper refresh: TT_REFRESH_PARTIAL, TT_REFRESH_FULL (full-screen redraw, partial waveform), or TT_REFRESH_DEEP (hardware full refresh)
TTInstanceOf<TTLvglEpdDriver>().requestRefresh(TT_REFRESH_PARTIAL);
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

#### Refresh levels (TTRefreshLevel)

Three levels are defined in `TTRefreshLevel.h` and used by `TTLvglEpdDriver`, navigation, and pages:

| Level | LVGL | E-Paper hardware | Use case |
|-------|------|------------------|----------|
| `TT_REFRESH_PARTIAL` | Redraw dirty areas only | Partial waveform | Frequent updates, minimal ghosting |
| `TT_REFRESH_FULL` | `lv_obj_invalidate(lv_scr_act())` then redraw | Partial waveform (full-screen area) | Full-screen content change without deep refresh |
| `TT_REFRESH_DEEP` | Full-screen invalidate + redraw | Full refresh waveform | Clear ghosting; used periodically (e.g. every `EPD_FULL_REFRESH_INTERVAL` partials) |

```cpp
#include "Base/TTRefreshLevel.h"
#include "Base/TTLvglEpdDriver.h"

TTInstanceOf<TTLvglEpdDriver>().requestRefresh(TT_REFRESH_PARTIAL);  // default
TTInstanceOf<TTLvglEpdDriver>().requestRefresh(TT_REFRESH_FULL);     // full-screen partial
TTInstanceOf<TTLvglEpdDriver>().requestRefresh(TT_REFRESH_DEEP);     // hardware full
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

## Software Architecture

### Entry Point

`main.cpp` starts two FreeRTOS tasks and then idles:

- **TTUITask** (core 0): SPI, LittleFS, LVGL, E-Paper driver, navigation, popup layer; root page is **TTHomePage** (WiFi / NTP / Clock entries). Runs `lv_timer_handler()` and `_keypad.tick()` every `TT_UI_LOOP_DELAY_MS` (5 ms). Page-level timing uses **runRepeat** / **runOnce** / **cancelRepeat** (driven in the same task loop; no LVGL timers required).
- **TTSensorTask** (core 1): I2C, AHT20 (temp/humidity), BMP280 (pressure). Reads sensors every `TT_SENSOR_UPDATE_INTERVAL` (60 s), posts `TT_NOTIFICATION_SENSOR_DATA_UPDATE` to the UI task. **requestSensorUpdateAsync()** allows other tasks to request an immediate read.

**TTWiFiTask** exists but is not started in `main.cpp`; add it if you need WiFi/AP config.

### Task Model (TTVTask)

- Base class for all tasks: `setup()` once, `loop()` in a FreeRTOS task, plus an internal queue and periodic task list.
- **Scheduling**: `runOnce(delayMs, callback)` runs the callback once after the delay; `runRepeat(intervalMs, callback, executeImmediately)` runs repeatedly (returns a handle); `cancelRepeat(handle)` cancels a repeat task immediately.
- Cross-task messaging: `postNotification(name, payload)` enqueues a call that runs in the task’s loop and forwards to `TTNotificationCenter::post()`.
- Observers (e.g. pages) subscribe via `TTNotificationCenter::subscribe<PayloadType>(name, observer, callback)` and must `unsubscribeByObserver(this)` in `willDestroy()`.

### UI Stack

- **ITTNavigationController** / **TTNavigationController**: Stack of **ITTScreenPage**; `setRoot` / `push` / `pop`; `requestRefresh(page, TTRefreshLevel)`. Binds keypad indev to the current page’s group on each `loadScreen()`. On setRoot, the root page gets `willAppear()` before load. When pushing a new page, shows a loading overlay via **TTPopupLayer** during `createScreen()` and dismisses after. Use **pushPage\<T\>()** / **setRootPage\<T\>()** for typed page pointers.
- **ITTScreenPage** / **TTScreenPage**: Lifecycle: `createScreen()` → `buildContent(screen)` → `setup()` (e.g. subscribe); then `willAppear` / `willDisappear` on navigation; `willDestroy()` on teardown (unsubscribe). **requestRefresh(TTRefreshLevel)** triggers E-Paper update (partial / full / deep). **runOnce(delayMs, callback)**, **runRepeat(intervalMs, callback, executeImmediately)** (returns handle), **cancelRepeat(handle)** for timing (all run in the UI task). Optional focus: **createGroup()** then **addToFocusGroup(obj)** (see [Keypad & focus](#keypad--focus)). **getName()** returns the page name (set in constructor).
- **TTPopupLayer**: Top LVGL layer; `showToast(text, durationMs)`, `dismissToast()`, `showLoading()`, `dismissLoading()`, **showDialog(msg, onOk, onCancel)** / **dismissDialog()**. Dialog has keypad focus (own group), focus indicator (underline under focused button). Loading overlay is shown automatically during page push.

### Keypad & focus

Hardware: three-button dial (Left GPIO 35, Right GPIO 39, Center GPIO 34) via **TTKeypadInput**; **TTUITask** calls `_keypad.tick()` in its loop (every `TT_UI_LOOP_DELAY_MS`). Keypad is event-driven and does not create or own LVGL groups. **TTNavigationController** switches the keypad’s group when the active page changes.

#### Button mapping

| Button | GPIO | LVGL key       | Effect in LVGL |
|--------|------|----------------|----------------|
| Left   | 35   | LV_KEY_LEFT    | Sent to focused object (e.g. focus prev in group) |
| Right  | 39   | LV_KEY_RIGHT   | Sent to focused object (e.g. focus next in group) |
| Center | 34   | LV_KEY_ENTER   | Confirm / press focused object |

LVGL uses **LV_KEY_NEXT** / **LV_KEY_PREV** for moving focus; Left/Right are passed to the focused widget. To use strict NEXT/PREV, change the mapping in `TTKeypadInput.cpp`.

#### How to add keypad focus to a page

If a page has no group, the keypad has no focus target. To make a page respond to the dial:

1. **Create a group** once: in `buildContent(screen)` (or `setup()`), call **`createGroup()`**.
2. **Add focusable widgets** in order: for each widget that should receive focus, call **`addToFocusGroup(obj)`**.

Example:

```cpp
void MyPage::buildContent(lv_obj_t* screen) {
    createGroup();

    lv_obj_t* btn1 = lv_btn_create(screen);
    // ... style, label ...
    addToFocusGroup(btn1);

    lv_obj_t* btn2 = lv_btn_create(screen);
    // ...
    addToFocusGroup(btn2);
}
```

- **Order**: The order of `addToFocusGroup` defines focus order (Right = next, Left = prev).
- **Widgets**: Only add objects that support focus (e.g. `lv_btn`, `lv_slider`). Enable the widget in `lv_conf.h` (e.g. `LV_USE_BUTTON`).
- **No group**: If you never call `createGroup()`, the keypad has no focus on that page (no crash).

To react to Center (Enter) on a widget, add an event handler; many widgets already handle ENTER as click:

```cpp
lv_obj_add_event_cb(btn, [](lv_event_t* e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) { /* Center pressed */ }
}, LV_EVENT_CLICKED, nullptr);
```

#### TTScreenPage API (focus)

| Method | Description |
|--------|-------------|
| `lv_group_t* getGroup() const` | This page’s group, or `nullptr` if none. |
| `lv_group_t* createGroup()` | (protected) Creates the page’s group; call from `buildContent()` or `setup()`. |
| `void addToFocusGroup(lv_obj_t* obj)` | Adds `obj` to this page’s group. No-op if no group. |

On setRoot / push / pop, **TTNavigationController::loadScreen()** calls `lv_indev_set_group(keypad_indev, page->getGroup())`. If `getGroup()` is `nullptr`, the keypad’s group is set to `nullptr`.

**Hardware**: GPIOs 34, 35, 39 are input-only on ESP32. Use 100 kΩ pull-down to GND (active-high); **TTKeypadInput** is configured for active-high. TTHomePage uses a bottom indicator bar for focus; dialog buttons use an underline under the focused label.

### Display and Fonts

- **TTRefreshLevel** (`TTRefreshLevel.h`): Enum `TT_REFRESH_PARTIAL`, `TT_REFRESH_FULL`, `TT_REFRESH_DEEP` for all refresh APIs.
- **TTLvglEpdDriver**: Creates LVGL display (296×128, I1, partial buffer), flush callback to GxEPD2; **requestRefresh(TTRefreshLevel)**. Deep refresh is used automatically every `EPD_FULL_REFRESH_INTERVAL` partials (and via **requestFullRefreshAsync()**); a pending flag avoids duplicate enqueue. Clock time label is wrapped in a fixed-size container to limit partial refresh area.
- **TTFontManager**: Singleton; `begin()` loads binary fonts from LittleFS (paths in `TTFontManager.cpp`); `getFont(size)` returns `lv_font_t*` for use in LVGL widgets.
- **TTFontLoader**: Loads one or two binary font files (main + optional fallback); **glyph cache** (e.g. up to 1000 entries) reduces LittleFS lookups for repeated characters. Used by TTFontManager per size.
- **TTStreamImage**: LVGL-compatible stream PNG widget (libspng + zlib, vendored in `lib/spng` and `lib/zlib`); decode to screen with I1 passthrough, no cache. Icons and assets live in `data/icons/` (e.g. `clock.png`, `wifi.png`, `watch.png`).

### Storage and Config

- **TTStorage**: LittleFS wrapper; `saveConfig` / `loadConfig` with `ArduinoJson`; default file `/config.json`.
- **TTPreference**: Key-value config in memory + JSON file; `get` / `set` / `sync`; used by TTWiFiManager for WiFi credentials.

### Utilities

- **TTInstanceOf\<T\>()**: Singleton access.
- **Logger** / **LOG_I**, **LOG_W**, **LOG_E**, etc.: Leveled logging.
- **ErrorCheck.h**: `ERR_CHECK_RET`, `ERR_CHECK_FAIL`, `ERR_CHECK_LOG`.
- **Util**: `format`, `printChipInfo`, `disableBrownoutDetector`, etc.
- **TTReleasePool**: RAII-style release callbacks in reverse order.

### Notifications

Event names and payloads are in `TTNotificationPayloads.h`, e.g. `TT_NOTIFICATION_SENSOR_DATA_UPDATE` with `TTSensorDataPayload` (temperature, humidity, pressure).

## License

MIT
