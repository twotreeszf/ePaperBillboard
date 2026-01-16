# LVGL E-Ink Display Refresh Guide

## Overview

LVGL (Light and Versatile Graphics Library) supports **on-demand refresh** (partial updates) mechanism, which is ideal for E-Ink/E-Paper displays that are sensitive to frequent refreshes.

---

## How LVGL Refresh Mechanism Works

### Dirty Area Management

LVGL tracks "dirty areas" (invalidated regions) when UI objects change. The refresh flow is:

```
UI object changes → Mark dirty area → Timer checks → Render if dirty → Call flush_cb
        ↓
   No changes → No dirty area → flush_cb NOT called
```

**Key Point**: If the display content remains static, `flush_cb` will NOT be triggered.

---

## Configuring LVGL for E-Ink Displays

### 1. Render Mode

Set the render mode to `PARTIAL` for optimal E-Ink performance:

```c
// LVGL v9
lv_display_set_buffers(display, buf1, buf2, buf_size, LV_DISPLAY_RENDER_MODE_PARTIAL);

// LVGL v8
disp_drv.full_refresh = 0;  // Default, enables partial refresh
```

| Render Mode | Description |
|-------------|-------------|
| `PARTIAL` | Only refresh changed areas (recommended for E-Ink) |
| `DIRECT` | Draw changes directly in full-screen buffer |
| `FULL` | Always refresh entire screen |

### 2. Refresh Period

Configure the refresh check period in `lv_conf.h`:

```c
#define LV_DEF_REFR_PERIOD 100  // Default is 33ms, increase for E-Ink
```

This only controls how often LVGL checks for dirty areas. No `flush_cb` call occurs if nothing changed.

### 3. Manual Refresh Control (Recommended)

For E-Ink displays, disable automatic refresh and control it manually:

```c
// After display initialization, delete the auto-refresh timer
lv_display_delete_refr_timer(display);

// Refresh only when needed (e.g., after user interaction)
void on_user_event(void) {
    lv_label_set_text(label, "Updated Text");  // Modify UI
    lv_refr_now(display);                       // Trigger refresh immediately
}
```

This ensures `flush_cb` is called **only when you explicitly request it AND there are dirty areas**.

---

## Implementing flush_cb for E-Ink

The `flush_cb` receives the area coordinates that need to be refreshed:

```c
void eink_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    // Calculate area dimensions
    int32_t x1 = area->x1;
    int32_t y1 = area->y1;
    int32_t x2 = area->x2;
    int32_t y2 = area->y2;
    
    // Call your E-Ink driver's partial refresh API
    epd_set_window(x1, y1, x2, y2);
    epd_write_image_data(px_map);
    epd_partial_refresh();
    
    // Notify LVGL that flush is complete
    lv_display_flush_ready(disp);
}
```

**Important**: Do NOT ignore the `area` parameter. Using it correctly enables true partial refresh.

---

## Buffer Configuration

For partial refresh mode, the buffer can be smaller than the full screen:

```c
// Buffer size can be 1/10 of screen size to save RAM
#define BUF_SIZE (LV_HOR_RES * LV_VER_RES / 10)

static uint8_t buf1[BUF_SIZE];
static uint8_t buf2[BUF_SIZE];  // Optional second buffer for DMA

lv_display_set_buffers(display, buf1, buf2, BUF_SIZE, LV_DISPLAY_RENDER_MODE_PARTIAL);
```

---

## Hardware Requirements

Partial refresh capability depends on the E-Ink controller chip:

| Controller | Partial Refresh Support |
|------------|------------------------|
| SSD1680 | ✅ Supported |
| UC8176 | ✅ Supported |
| IL0373 | ✅ Supported |
| IL0398 | ✅ Supported |
| 3-Color (BWR) | ⚠️ Limited or unsupported |

---

## Best Practices for E-Ink

### Recommended Configuration

| Setting | Value | Reason |
|---------|-------|--------|
| Render Mode | `PARTIAL` | Only refresh changed areas |
| Refresh Timer | Disabled (manual) | Full control over refresh timing |
| Animations | Disabled | Animations cause continuous dirty areas |
| Buffer Size | 1/10 screen | Save RAM while supporting partial updates |

### Disable Animations

```c
// In lv_conf.h
#define LV_USE_ANIMATION 0
```

Or disable specific animations at runtime:

```c
lv_anim_del_all();
```

### Handle Ghosting

E-Ink displays may show ghosting after multiple partial refreshes. Implement periodic full refresh:

```c
static uint8_t partial_refresh_count = 0;
#define FULL_REFRESH_INTERVAL 10

void eink_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    partial_refresh_count++;
    
    if (partial_refresh_count >= FULL_REFRESH_INTERVAL) {
        epd_full_refresh();  // Clear ghosting
        partial_refresh_count = 0;
    } else {
        epd_partial_refresh_area(area, px_map);
    }
    
    lv_display_flush_ready(disp);
}
```

---

## Summary

| Feature | LVGL Support |
|---------|-------------|
| On-demand refresh (only when UI changes) | ✅ Yes |
| Partial area refresh | ✅ Yes |
| Manual refresh control | ✅ Yes |
| Small buffer support | ✅ Yes |

LVGL's dirty area tracking ensures `flush_cb` is only called when actual UI changes occur, making it well-suited for E-Ink displays when configured properly.
