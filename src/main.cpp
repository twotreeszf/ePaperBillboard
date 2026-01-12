/**
 * ESP8266 + E029A01 2.9" E-Paper Display Demo
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

// E029A01 is a 2.9" display with 128x296 resolution
// Driver chip: IL3820 (GDEH029A1)

GxEPD2_BW<GxEPD2_290, GxEPD2_290::HEIGHT> display(
    GxEPD2_290(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

void drawHelloWorld() {
    display.setRotation(1);  // Landscape mode
    display.setTextColor(GxEPD_BLACK);
    display.setTextSize(2);
    
    int16_t tbx, tby;
    uint16_t tbw, tbh;
    const char* text = "Hello E-Paper!";
    display.getTextBounds(text, 0, 0, &tbx, &tby, &tbw, &tbh);
    
    // Center the text on display
    uint16_t x = ((display.width() - tbw) / 2) - tbx;
    uint16_t y = ((display.height() - tbh) / 2) - tby;
    
    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.setCursor(x, y);
        display.print(text);
    } while (display.nextPage());
}

void drawTestPattern() {
    display.setRotation(1);  // Landscape mode
    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        
        // Draw border
        display.drawRect(0, 0, display.width(), display.height(), GxEPD_BLACK);
        
        // Draw diagonal lines
        display.drawLine(0, 0, display.width(), display.height(), GxEPD_BLACK);
        display.drawLine(display.width(), 0, 0, display.height(), GxEPD_BLACK);
        
        // Draw center circle
        display.drawCircle(display.width() / 2, display.height() / 2, 30, GxEPD_BLACK);
        
        // Draw text
        display.setTextColor(GxEPD_BLACK);
        display.setTextSize(1);
        display.setCursor(10, 10);
        display.print("E029A01 2.9\" E-Paper");
        display.setCursor(10, 25);
        display.print("Resolution: 296x128");
        display.setCursor(10, 40);
        display.print("ESP8266 NodeMCU");
        
    } while (display.nextPage());
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println();
    Serial.println("=================================");
    Serial.println("E029A01 E-Paper Display Demo");
    Serial.println("=================================");
    
    // Initialize display
    Serial.println("Initializing display...");
    display.init(115200);  // Enable diagnostic output
    
    Serial.println("Drawing test pattern...");
    drawTestPattern();
    
    delay(3000);
    
    Serial.println("Drawing hello world...");
    drawHelloWorld();
    
    // Put display to sleep to save power
    display.hibernate();
    
    Serial.println("Display initialized and showing content.");
    Serial.println("Display is now in hibernate mode to save power.");
}

void loop() {
    // E-Paper displays don't need constant refresh
    // Content persists without power
    delay(10000);
}
