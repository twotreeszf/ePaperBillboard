/**
 * ESP32-WROOM-32E + E029A01 2.9" E-Paper Clock Demo
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
#include "Base/TTStorage.h"
#include "Base/Logger.h"
#include "Base/TTFontLoader.h"
#include "Base/ErrorCheck.h"

// E-Paper SPI pins
#define EPD_MOSI  4
#define EPD_SCK   16
#define EPD_CS    17
#define EPD_DC    5
#define EPD_RST   18
#define EPD_BUSY  19

// Display dimensions in landscape mode
#define DISPLAY_WIDTH   296
#define DISPLAY_HEIGHT  128

// Time text size and dimensions (size 5 = 5*6=30 width, 5*8=40 height per char)
#define TIME_TEXT_SIZE   5
#define TIME_TEXT_H      40

// Time area for partial refresh - centered on screen
// "HH:MM" = 5 chars, width = 5 * 30 = 150, add padding
#define TIME_AREA_W      180
#define TIME_AREA_H      50
#define TIME_AREA_X      ((DISPLAY_WIDTH - TIME_AREA_W) / 2)
#define TIME_AREA_Y      ((DISPLAY_HEIGHT - TIME_AREA_H) / 2)

// Full refresh interval (every 10 minutes to prevent ghosting)
#define FULL_REFRESH_INTERVAL  10

GxEPD2_BW<GxEPD2_290, GxEPD2_290::HEIGHT> display(
    GxEPD2_290(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

TTFontLoader font14;

// Time tracking (starts from 00:00:00 at boot)
uint8_t hours = 0;
uint8_t minutes = 0;
uint8_t seconds = 0;
unsigned long lastUpdateMs = 0;
uint8_t partialRefreshCount = 0;

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

void drawTimeText() {
    display.setTextColor(GxEPD_BLACK);
    display.setTextSize(TIME_TEXT_SIZE);
    
    char timeStr[8];
    snprintf(timeStr, sizeof(timeStr), "%02d:%02d", hours, minutes);
    
    int16_t tbx, tby;
    uint16_t tbw, tbh;
    display.getTextBounds(timeStr, 0, 0, &tbx, &tby, &tbw, &tbh);
    
    // Center in the time area
    uint16_t x = TIME_AREA_X + (TIME_AREA_W - tbw) / 2 - tbx;
    uint16_t y = TIME_AREA_Y + (TIME_AREA_H - tbh) / 2 - tby;
    
    display.setCursor(x, y);
    display.print(timeStr);
}

void drawFullScreen() {
    LOG_I("Full screen refresh...");
    display.setRotation(1);
    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        
        // Draw time
        drawTimeText();
        
        font14.drawUTF8(display, 4, 4, "电子墨水屏时钟演示");
        font14.drawUTF8(display, 4, DISPLAY_HEIGHT - 18, "自启动时间 - 局部刷新模式");
        
    } while (display.nextPage());
    
    partialRefreshCount = 0;
}

void drawTimePartial() {
    LOG_I("Partial refresh...");
    display.setRotation(1);
    display.setPartialWindow(TIME_AREA_X, TIME_AREA_Y, TIME_AREA_W, TIME_AREA_H);
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        drawTimeText();
    } while (display.nextPage());
    
    partialRefreshCount++;
}

void refreshDisplay() {
    if (partialRefreshCount >= FULL_REFRESH_INTERVAL) {
        drawFullScreen();
    } else {
        drawTimePartial();
    }
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
    LOG_I("E-Paper Clock Demo (Partial Refresh)");
    LOG_I("=================================");
    
    printChipInfo();
    delay(200);
    
    // Initialize SPI with custom pins
    LOG_I("Initializing SPI (MOSI=%d, SCK=%d)...", EPD_MOSI, EPD_SCK);
    SPI.begin(EPD_SCK, -1, EPD_MOSI, EPD_CS);
    
    // Initialize display
    LOG_I("Initializing display...");
    display.init(115200, true, 2, false, SPI, SPISettings(4000000, MSBFIRST, SPI_MODE0));
    
    // Initialize font loader from LittleFS
    ERR_CHECK_FAIL(LittleFS.begin());
    ERR_CHECK_FAIL(font14.begin("/font14.bin"));
    font14.setTextColor(GxEPD_BLACK);
    LOG_I("Font loader initialized");
    
    // Initial full screen draw
    lastUpdateMs = millis();
    drawFullScreen();
    
    LOG_I("Clock started.");
    LOG_I("Partial refresh enabled, full refresh every %d minutes", FULL_REFRESH_INTERVAL);
}

void loop() {
    updateTime();
    
    static uint8_t lastMinute = 255;
    
    if (minutes != lastMinute) {
        lastMinute = minutes;
        
        LOG_I("Time: %02d:%02d:%02d", hours, minutes, seconds);
        refreshDisplay();
        LOG_I("Display refreshed.");
    }
    
    delay(100);
}
