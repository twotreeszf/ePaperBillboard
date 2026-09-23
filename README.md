# ESP32 E-Paper Billboard

ESP32-WROOM-32E weather and calendar billboard: LVGL 9, custom binary fonts, LittleFS icons, indoor sensors, Open-Meteo, and CalDAV over a small TLS 1.2 client. UI is a page stack with an always-on status bar and E-Paper three-level refresh (partial / full-screen partial / deep full).

**Contents:** [Hardware](#hardware) · [Build & Flash](#build--flash) · [Fonts](#fonts) · [Icons](#icons) · [E-Ink / LVGL](#e-ink-refresh-strategy) · [Software Architecture](#software-architecture) · [Keypad & focus](#keypad--focus)

## Hardware

### Components

- **MCU**: ESP32-WROOM-32E (8MB Flash, no PSRAM)
- **Display** (pick one in `include/EPDConfig.h`; must match the panel):

| Macro | Panel | Size | Notes |
|-------|--------|------|--------|
| `EPD_PANEL_HINK_E042A13_A0` | HINK-E042A13-A0 4.2" | 400×300 | Current default |
| `EPD_PANEL_HINK_E029A01_A1` | HINK-E029A01-A1 2.9" | 296×128 | Landscape `EPD_ROTATION 3` |

A wrong panel macro applies the wrong waveform and scan count. Short tests usually only look wrong; leaving a mismatch running can ghost or damage the film.

- **Sensors (I2C, optional)**: AHT20 (temp/humidity, 0x38), BMP280 (pressure, 0x76/0x77), DS3231 RTC (0x68)
- **Keypad**: three-button dial, active-high (100 kΩ to GND)

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

**Sensors (I2C)**

| Signal | ESP32 GPIO |
|--------|------------|
| SDA | GPIO23 |
| SCL | GPIO22 |

**Keypad**

| Button | GPIO |
|--------|------|
| Left | 35 |
| Right | 39 |
| Center | 34 |

## Build & Flash

### Prerequisites

- [PlatformIO](https://platformio.org/)
- USB connected to the ESP32

### Initial Setup (First Time)

```bash
pio run --target erase
pio run --target upload
pio run --target uploadfs
pio device monitor
```

### Regular Development

```bash
pio run --target upload
pio device monitor
pio run --target uploadfs   # after changing data/res/fonts or data/res/icons
```

### Flash layout

Custom 8MB table (`partitions_8MB.csv`):

| Partition | Size | Description |
|-----------|------|-------------|
| app0 | 2 MB | Main application (OTA slot 0) |
| app1 | 2 MB | OTA slot 1 |
| spiffs | ~3.875 MB | LittleFS: `/res` fonts and icons, `/tmp` cache and scratch files |
| coredump | 64 KB | Core dump |

`board_build.flash_size` and `board_upload.flash_size` must both be `8MB`.

## Fonts

`TTFontManager` loads LittleFS binaries produced by `lv_font_conv`. Current entries (`TTFontManager.cpp`):

| Size | File | Role |
|------|------|------|
| 10 / 12 / 16 | `all_*.bin` | UI Chinese + ASCII (Source Han Sans CN) |
| 32 | `en_32.bin` | Clock-mode indoor T/H; calendar date and weekday (Google Sans Code Medium 500) |
| 40 | `en_40.bin` | Calendar clock and temperature (Google Sans Code Medium 500) |
| 48 | `en_48.bin` | Detail-mode temperature (Google Sans Code Medium 500) |
| 120 | `en_120.bin` | Clock digits `0-9`, space, `-`, `:` (Google Sans Code ExtraBold 800) |

Sources live in `tools/fonts/`. After replacing a `.bin`, run `uploadfs`.

### Batch generation

```bash
npm install -g lv_font_conv
cd tools
python3 -m venv venv
source venv/bin/activate
pip install -r requirements.txt
```

```text
python tools/generate_fonts.py <font.ttf> <sizes> [prefix] [size_offset]
```

CJK Extension ranges in `EXCLUDED_RANGES` are stripped so Simplified Chinese bins stay smaller. `analyze_ttf_cmap.py` prints `--range=` lines for a manual `lv_font_conv` run.

Clock / metric examples:

```bash
lv_font_conv --font tools/fonts/GoogleSansCode-ExtraBold.ttf --size 120 \
  --bpp 1 --format bin --no-compress \
  --range 0x20 --range 0x2D --range 0x30-0x3A \
  -o data/res/fonts/en_120.bin

lv_font_conv --font tools/fonts/GoogleSansCode-Medium.ttf --size 32 \
  --bpp 1 --format bin --no-compress \
  --range 0x20-0x7E --range 0x00B0 \
  -o data/res/fonts/en_32.bin

lv_font_conv --font tools/fonts/GoogleSansCode-Medium.ttf --size 40 \
  --bpp 1 --format bin --no-compress \
  --range 0x20-0x7E --range 0x00B0 \
  -o data/res/fonts/en_40.bin
```

`TTFontLoader` still accepts a main file plus optional ASCII overlay (`begin(path, asciiPath)`); ASCII hits the overlay first. Glyph cache is `TT_FONT_GLYPH_CACHE_MAX` (128). The glyph bitmap buffer is `120×120` for the large clock face.

```cpp
TTFontManager::instance().begin();   // after LittleFS.begin()
lv_font_t* font = TTFontManager::instance().getFont(16);
lv_obj_set_style_text_font(label, font, 0);
```

## Icons

On-device icons are uncompressed **TTI1** (`TTI1` + u16le width/height, 1 = white, 0 = ink). `TTStreamImage` caches them by path (8 KB cap) and blits I1 through `TTDrawBufPassthroughDecoder`.

| Pipeline | Script | Manifest / notes |
|----------|--------|------------------|
| Lucide (weather / calendar / settings / back) | `tools/icons/slice_lucide_icons.mjs` | `tools/icons/icons.json` |
| Weather Icons (conditions, wind, details) | `tools/icons/slice_weather_icons.mjs` | `tools/icons/weather.json` |
| Pixel / battery | `tools/icons/slice_battery_icons.mjs`, `slice_pixelart_icons.mjs` | nav status |
| Material Symbols | clock-mode T/H | `device_thermostat.svg`, `humidity_mid.svg` → `data/res/icons/weather/temp_32.i1`, `humidity_32.i1` |

```bash
cd tools/icons
npm install
node slice_lucide_icons.mjs
node slice_weather_icons.mjs
pio run --target uploadfs
```

## E-Ink Refresh Strategy

```
┌─────────────┐     ┌──────────────────┐     ┌─────────────┐
│   LVGL UI   │ ──> │ TTLvglEpdDriver  │ ──> │  GxEPD2_BW  │ ──> panel
│  (I1, 1bpp) │     │ (flush + levels) │     │  (SPI/EPD)  │
└─────────────┘     └──────────────────┘     └─────────────┘
```

Display size and rotation come from `EPDConfig.h`. Buffer is one I1 frame (`EPD_BUF_SIZE`), render mode `PARTIAL`. Flush skips the 8-byte I1 palette, then writes GxEPD2 pixels (LVGL 1 = white paper). Page paints are clipped above the nav bar; the bar is redrawn as an overlay when a flush overlaps it.

| Level | LVGL | Panel | Use |
|-------|------|-------|-----|
| `TT_REFRESH_PARTIAL` | Dirty areas | Partial LUT | Clock ticks, sensor, focus |
| `TT_REFRESH_FULL` | Invalidate page content | Partial LUT, full page | Content change without flashing the film |
| `TT_REFRESH_DEEP` | Invalidate + full window | Full waveform | First weather paint, mode switch; also every `EPD_FULL_REFRESH_INTERVAL` (64) partials |

```cpp
TTInstanceOf<TTLvglEpdDriver>().requestRefresh(TT_REFRESH_PARTIAL);
```

Do not use `translate_x` / `translate_y` for e-paper alignment. Partial dirty boxes follow layout coords, so translated ink is not erased and ghosts. Prefer padding or margin.

LVGL knobs live in `include/lv_conf.h` (`LV_COLOR_DEPTH 1`, animations off, mono theme).

## Software Architecture

### Tasks

`main.cpp` starts three FreeRTOS tasks, then idles:

| Task | Core | Role |
|------|------|------|
| **TTUITask** | 0 | SPI, LittleFS, LVGL, EPD, keypad, navigation, popup, sleep try-enter. Loop: `_keypad.tick()`, `lv_timer_handler()`, `TTSleepService::tryEnter()` every `TT_UI_LOOP_DELAY_MS` (5 ms). |
| **TTSensorTask** | 1 | I2C AHT20 / BMP280 / battery. Every `TT_SENSOR_UPDATE_INTERVAL` (60 s) posts `TT_NOTIFICATION_SENSOR_DATA_UPDATE`. |
| **TTWiFiTask** | 1 | Saved STA, SoftAP provisioning (`Billboard-XXXX` + captive portal). Posts `TT_NOTIFICATION_WIFI_STATUS`. |

**TTVTask**: `setup()` / `loop()`, `runOnce` / `runRepeat` / `cancelRepeat`, and `postNotification` into that task’s queue. Pages subscribe on the UI task via `TTNotificationCenter` and `unsubscribeByObserver(this)` in `willDestroy()`. Page timers (`runOnce` / `runRepeat`) run in the UI loop; no LVGL timers required.

### Pages

```
TTHomePage          天气 / 日历 / 设置
 ├─ TTWeatherPage   detail (default) or clock; left/right wheel switches
 ├─ TTCalendarPage  left weather/clock, right event timeline
 └─ TTSettingsPage  Web 设置 / Wi-Fi / NTP 校时
     ├─ TTWiFiConfigPage
     ├─ TTWiFiStatusPage
     └─ TTNtpSyncPage
```

**TTWeatherPage**

- Detail: current condition, 5-day forecast, 8 metric cells, 24 h temp/humidity graph (Open-Meteo + AQI).
- Clock: 120 px `hh:mm` (font colon), indoor T/H from the sensor notification, city/date on the top right.
- Center long-press force-refreshes weather. Fetch wakes at the first second of the next hour (`HH:00:01`).

**TTCalendarPage**

- Left column: condition icon, temperature (`en_40`), condition text, feels-like, date and weekday (`en_32`), clock (`en_40`). The nav-bar clock is hidden on this page.
- Right column: CalDAV events on a vertical timeline. Today’s header reads 今天; other days keep 月日 and weekday. Left/right page through the loaded window. The last page’s right click loads the next 7 days and shows the loading popup.
- Center long-press clears the list, shows 正在刷新, and refetches weather and calendar. The same idle-sleep and `HH:00:01` fetch wake updates both.

**TTNavigationBar** (top LVGL layer): back + title, time, indoor T/H/P, Wi-Fi, battery. Page content height is `EPD_HEIGHT - TT_NAV_PAGE_INSET`. `setTimeVisible(false)` hides only the clock.

**TTPopupLayer**: toast, loading (shown while pushing a page), dialog with its own keypad group.

### Network, time, sleep

- **TTHttpsClient** / **TTTlsClient** / **TTTlsCrypto**: TLS 1.2 (X25519, AES-128-GCM, `VERIFY_NONE`). Handshake and records use LittleFS temp files so the WROOM stays off mbedTLS. `tt_https_get_file` is GET; `tt_https_exchange_file` adds method, headers, and body for CalDAV. Weather and calendar stay on HTTPS.
- **TTWeatherService**: `api.open-meteo.com` + `air-quality-api.open-meteo.com`. Location prefs: `weather_city`, `weather_lat`, `weather_lon`.
- **TTCalendarService**: CalDAV (`PROPFIND` / `calendar-query` / `calendar-multiget`). Account prefs: `caldav_host`, `caldav_user`, `caldav_pass`, entered on the setup portal. Up to `TT_CAL_EVENT_MAX` (40) events, `TT_CAL_FETCH_DAYS` (7) per request.
- **TTRtc**: DS3231 if present, else ESP time; NTP `ntp.aliyun.com` / `pool.ntp.org`.
- **TTTimeService**: posts `TT_NOTIFICATION_TIME_TICK` on the minute for clocks and the nav bar. The tick is local `HH:MM:01` (`TT_RTC_MINUTE_TICK_SEC`).
- **TTSleepService**: light sleep after `TT_SLEEP_INPUT_IDLE_MS` (5 s) when a page requests it; wake on keypad, fetch deadline, or time. The fetch deadline is the first second of the next hour (`HH:00:01`). UART pins are floated in sleep.

### Storage

- **TTPreference**: named key-value map persisted as JSON (Wi-Fi networks, timezone, weather location, CalDAV account).
- **TTFile**: LittleFS helpers used by fonts, icons, and TLS/HTTPS scratch files.

### Notifications (`TTNotificationPayloads.h`)

| Name | Payload |
|------|---------|
| `TT_NOTIFICATION_SENSOR_DATA_UPDATE` | temp, humidity, pressure, battery |
| `TT_NOTIFICATION_WIFI_STATUS` | link / AP / portal |
| `TT_NOTIFICATION_TIME_SYNC` | NTP / timezone portal |
| `TT_NOTIFICATION_WEATHER` | forecast / AQI / status |
| `TT_NOTIFICATION_CALENDAR` | CalDAV events / status |
| `TT_NOTIFICATION_SLEEP_WAKE` | why sleep ended |
| `TT_NOTIFICATION_TIME_TICK` | minute tick |

## Keypad & focus

**TTKeypadInput** is an LVGL keypad indev. Left/Right click first call `ITTScreenPage::handleKeyAction`. If the page returns true (weather mode switch, or calendar paging), LVGL focus is not moved. Otherwise Left/Right emit `LV_KEY_PREV` / `LV_KEY_NEXT`. Center click is Enter; Center long-press is page-registered (weather and calendar force refresh). Left long-press pops the stack.

| Button | GPIO | Default |
|--------|------|---------|
| Left | 35 | `handleKeyAction` or focus prev; long-press pop |
| Right | 39 | `handleKeyAction` or focus next |
| Center | 34 | Enter; long-press is per-page |

To add focus on a page: `createGroup()` then `addToFocusGroup(obj)` in `buildContent()`. `TTNavigationController::loadScreen()` binds the indev group to the current page. No group means the dial has no LVGL focus target on that page.

## License

MIT
