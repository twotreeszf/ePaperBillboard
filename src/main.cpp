/**
 * ESP8266 + E029A01 2.9" E-Paper Clock Demo
 * 
 * Hardware Connection (ESP8266 NodeMCU -> E-Paper):
 * -------------------------------------------------
 * VCC  -> 3.3V
 * GND  -> GND
 * DIN  -> D7 (GPIO13, MOSI)
 * CLK  -> D5 (GPIO14, SCK)
 * CS   -> D8 (GPIO15)
 * DC   -> D2 (GPIO4)
 * RST  -> D4 (GPIO2)
 * BUSY -> D1 (GPIO5)
 */

#include <Arduino.h>
#include <GxEPD2_BW.h>

// E-Paper display pins for ESP8266 NodeMCU
#define EPD_CS    15  // D8
#define EPD_DC    4   // D2
#define EPD_RST   2   // D4
#define EPD_BUSY  5   // D1

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
    Serial.println("Full screen refresh...");
    display.setRotation(1);
    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        
        // Draw time
        drawTimeText();
        
        // Draw label at top
        display.setTextSize(1);
        display.setCursor(10, 5);
        display.print("E-Paper Clock Demo");
        
        // Draw note at bottom
        display.setCursor(10, DISPLAY_HEIGHT - 12);
        display.print("Time since boot - Partial refresh mode");
        
    } while (display.nextPage());
    
    partialRefreshCount = 0;
}

void drawTimePartial() {
    Serial.println("Partial refresh...");
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
    Serial.println("--- Chip Info ---");
    Serial.printf("Chip ID: %08X\n", ESP.getChipId());
    Serial.printf("Flash Chip ID: %08X\n", ESP.getFlashChipId());
    Serial.printf("Flash Size: %u KB (%u MB)\n", 
        ESP.getFlashChipSize() / 1024, 
        ESP.getFlashChipSize() / 1024 / 1024);
    Serial.printf("Flash Real Size: %u KB\n", ESP.getFlashChipRealSize() / 1024);
    Serial.printf("Flash Speed: %u MHz\n", ESP.getFlashChipSpeed() / 1000000);
    Serial.printf("Free Heap: %u bytes\n", ESP.getFreeHeap());
    Serial.printf("SDK Version: %s\n", ESP.getSdkVersion());
    Serial.println("-----------------");
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println();
    Serial.println("=================================");
    Serial.println("E-Paper Clock Demo (Partial Refresh)");
    Serial.println("=================================");
    
    printChipInfo();
    delay(200);
    
    // Initialize display
    Serial.println("Initializing display...");
    display.init(115200);
    
    // Initial full screen draw
    lastUpdateMs = millis();
    drawFullScreen();
    
    Serial.println("Clock started.");
    Serial.printf("Partial refresh enabled, full refresh every %d minutes.\n", FULL_REFRESH_INTERVAL);
}

void loop() {
    updateTime();
    
    static uint8_t lastMinute = 255;
    
    if (minutes != lastMinute) {
        lastMinute = minutes;
        
        Serial.printf("Time: %02d:%02d:%02d\n", hours, minutes, seconds);
        refreshDisplay();
        Serial.println("Display refreshed.");
    }
    
    delay(100);
}
