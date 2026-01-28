/**
 * ESP32-WROOM-32E + E029A01 2.9" E-Paper Clock Demo (LVGL Version)
 * 
 * Hardware Connection (ESP32 -> E-Paper):
 * -------------------------------------------------
 * VCC  -> 3.3V
 * GND  -> GND
 * DIN  -> GPIO4  (MOSI)
 * CLK  -> GPIO16 (SCK)
 * CS   -> GPIO17
 * DC   -> GPIO5
 * RST  -> GPIO18
 * BUSY -> GPIO19
 */

#include <Arduino.h>
#include <SPI.h>
#include <GxEPD2_BW.h>
#include <LittleFS.h>
#include <lvgl.h>
#include "LvglDriver.h"
#include "Base/Logger.h"
#include "Base/ErrorCheck.h"
#include "Base/TTFontLoader.h"

// E-Paper SPI pins
#define EPD_MOSI  4
#define EPD_SCK   16
#define EPD_CS    17
#define EPD_DC    5
#define EPD_RST   18
#define EPD_BUSY  19

// Full refresh interval (every 10 minutes to prevent ghosting)
#define FULL_REFRESH_INTERVAL  10

// E-Paper display instance
GxEPD2_BW<GxEPD2_290, GxEPD2_290::HEIGHT> display(
    GxEPD2_290(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

// Font loaders (streaming from LittleFS)
TTFontLoader fontLarge;   // 16px for title
TTFontLoader fontSmall;   // 14px for content
TTFontLoader fontClock;   // 48px for clock digits

// LVGL UI elements
static lv_obj_t* titleLabel = nullptr;
static lv_obj_t* timeLabel = nullptr;
static lv_obj_t* statusLabel = nullptr;

// Time tracking (starts from 00:00:00 at boot)
static uint8_t hours = 0;
static uint8_t minutes = 0;
static uint8_t seconds = 0;
static unsigned long lastUpdateMs = 0;
static uint8_t partialRefreshCount = 0;

void updateTime() {
    unsigned long currentMs = millis();
    unsigned long elapsedMs = currentMs - lastUpdateMs;
    
    if (elapsedMs >= 1000) {
        uint32_t elapsedSeconds = elapsedMs / 1000;
        lastUpdateMs = currentMs - (elapsedMs % 1000);
        
        seconds += elapsedSeconds;
        while (seconds >= 60) {
            seconds -= 60;
            minutes++;
        }
        while (minutes >= 60) {
            minutes -= 60;
            hours++;
        }
        while (hours >= 24) {
            hours -= 24;
        }
    }
}

void createClockUI() {
    // Get active screen
    lv_obj_t* scr = lv_scr_act();
    
    // Set screen background to white
    lv_obj_set_style_bg_color(scr, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    
    // Get fonts
    lv_font_t* font16 = fontLarge.getLvglFont();
    lv_font_t* font14 = fontSmall.getLvglFont();
    lv_font_t* font48 = fontClock.getLvglFont();
    
    // Create title label at top (16px font)
    titleLabel = lv_label_create(scr);
    lv_label_set_text(titleLabel, "电子墨水屏时钟 E-Paper Clock");
    lv_obj_set_style_text_color(titleLabel, lv_color_black(), 0);
    lv_obj_set_style_text_font(titleLabel, font16, 0);
    lv_obj_align(titleLabel, LV_ALIGN_TOP_MID, 0, 4);
    
    // Create time label in center (48px ASCII font)
    timeLabel = lv_label_create(scr);
    lv_label_set_text(timeLabel, "00:00");
    lv_obj_set_style_text_color(timeLabel, lv_color_black(), 0);
    lv_obj_set_style_text_font(timeLabel, font48, 0);
    lv_obj_center(timeLabel);
    
    // Create status label at bottom (14px font, auto wrap)
    statusLabel = lv_label_create(scr);
    lv_obj_set_width(statusLabel, lv_pct(100));
    lv_obj_set_style_pad_left(statusLabel, 4, 0);
    lv_obj_set_style_pad_right(statusLabel, 4, 0);
    lv_obj_set_style_text_line_space(statusLabel, 4, 0);
    lv_label_set_long_mode(statusLabel, LV_LABEL_LONG_WRAP);
    lv_label_set_text(statusLabel, "奥特曼亲口承认 GPT-5.2 搞砸了，这是 OpenAI CEO 最特别的一次直播");
    lv_obj_set_style_text_color(statusLabel, lv_color_black(), 0);
    lv_obj_set_style_text_font(statusLabel, font14, 0);
    lv_obj_align(statusLabel, LV_ALIGN_BOTTOM_LEFT, 0, -4);
    
    LOG_I("LVGL UI created with dual fonts");
}

void updateClockDisplay() {
    char timeStr[8];
    snprintf(timeStr, sizeof(timeStr), "%02d:%02d", hours, minutes);
    
    lv_label_set_text(timeLabel, timeStr);
    
    // Determine if we need full refresh
    bool needFullRefresh = (partialRefreshCount >= FULL_REFRESH_INTERVAL);
    
    if (needFullRefresh) {
        LOG_I("Full screen refresh...");
        partialRefreshCount = 0;
    } else {
        LOG_I("Partial refresh...");
        partialRefreshCount++;
    }
    
    // Request display refresh
    lvglDriver.requestRefresh(needFullRefresh);
}

void printChipInfo() {
    LOG_I("--- Chip Info ---");
    LOG_I("Chip Model: %s", ESP.getChipModel());
    LOG_I("Chip Revision: %d", ESP.getChipRevision());
    LOG_I("CPU Freq: %u MHz", ESP.getCpuFreqMHz());
    LOG_I("Flash Size: %u KB (%u MB)", 
        ESP.getFlashChipSize() / 1024, 
        ESP.getFlashChipSize() / 1024 / 1024);
    LOG_I("Flash Speed: %u MHz", ESP.getFlashChipSpeed() / 1000000);
    
    if (LittleFS.begin()) {
        LOG_I("LittleFS Total: %u KB", LittleFS.totalBytes() / 1024);
        LOG_I("LittleFS Used: %u KB", LittleFS.usedBytes() / 1024);
    }
    
    LOG_I("Free Heap: %u bytes", ESP.getFreeHeap());
    LOG_I("SDK Version: %s", ESP.getSdkVersion());
    LOG_I("-----------------");
}

void setup() {
    Serial.begin(115200);
    delay(100);
    LOG_I("");
    LOG_I("=================================");
    LOG_I("E-Paper Clock Demo (LVGL Version)");
    LOG_I("=================================");
    
    printChipInfo();
    delay(200);
    
    // Initialize SPI with custom pins
    LOG_I("Initializing SPI (MOSI=%d, SCK=%d)...", EPD_MOSI, EPD_SCK);
    SPI.begin(EPD_SCK, -1, EPD_MOSI, EPD_CS);
    
    // Initialize E-Paper display
    LOG_I("Initializing E-Paper display...");
    display.init(115200, true, 2, false, SPI, SPISettings(4000000, MSBFIRST, SPI_MODE0));
    
    // Initialize LittleFS for font loading
    ERR_CHECK_FAIL(LittleFS.begin());
    LOG_I("LittleFS initialized");
    
    // Load fonts from LittleFS (streaming mode)
    // Chinese fonts with ASCII fallback for better English rendering
    if (fontLarge.begin("/fonts/chs_16.bin", "/fonts/en_16.bin")) {
        LOG_I("16px font loaded (CHS + EN)");
    } else {
        LOG_W("Failed to load 16px font");
    }
    
    if (fontSmall.begin("/fonts/chs_14.bin", "/fonts/en_14.bin")) {
        LOG_I("14px font loaded (CHS + EN)");
    } else {
        LOG_W("Failed to load 14px font");
    }
    
    if (fontClock.begin("/fonts/en_48.bin")) {
        LOG_I("48px clock font loaded");
    } else {
        LOG_W("Failed to load 48px clock font");
    }
    
    // Initialize LVGL display driver
    LOG_I("Initializing LVGL...");
    ERR_CHECK_FAIL(lvglDriver.begin(display));
    
    // Create clock UI
    createClockUI();
    
    // Initial display update
    lastUpdateMs = millis();
    updateClockDisplay();
    
    LOG_I("Clock started.");
    LOG_I("Partial refresh enabled, full refresh every %d minutes", FULL_REFRESH_INTERVAL);
}

void loop() {
    updateTime();
    
    static uint8_t lastMinute = 255;
    
    if (minutes != lastMinute) {
        lastMinute = minutes;
        
        LOG_I("Time: %02d:%02d:%02d", hours, minutes, seconds);
        updateClockDisplay();
        LOG_I("Display refreshed.");
    }
    
    // Process LVGL tasks (even though we disabled auto-refresh, 
    // this handles internal housekeeping)
    lv_timer_handler();
    
    delay(100);
}
