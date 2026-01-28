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
 * 
 * Hardware Connection (ESP32 -> AHT20/BMP280 I2C):
 * -------------------------------------------------
 * VCC  -> 3.3V
 * GND  -> GND
 * SDA  -> GPIO23
 * SCL  -> GPIO22
 * 
 * Note: AHT20 (0x38) and BMP280 (0x76/0x77) share the same I2C bus
 */

#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <GxEPD2_BW.h>
#include <LittleFS.h>
#include <lvgl.h>
#include <Adafruit_AHTX0.h>
#include <Adafruit_BMP280.h>
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

// I2C pins for AHT20 sensor
#define I2C_SDA   23
#define I2C_SCL   22

// Full refresh interval (every 10 minutes to prevent ghosting)
#define FULL_REFRESH_INTERVAL  10

// Sensor update interval in seconds
#define SENSOR_UPDATE_INTERVAL  30

// E-Paper display instance
GxEPD2_BW<GxEPD2_290, GxEPD2_290::HEIGHT> display(
    GxEPD2_290(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

// AHT20 sensor instance
Adafruit_AHTX0 aht20;
bool aht20Available = false;

// BMP280 sensor instance
Adafruit_BMP280 bmp280;
bool bmp280Available = false;

// Font loaders (streaming from LittleFS)
TTFontLoader fontLarge;   // 16px for title
TTFontLoader fontSmall;   // 12px for status
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

// Sensor data
static float temperature = 0.0f;
static float humidity = 0.0f;
static float pressure = 0.0f;
static unsigned long lastSensorUpdateSec = 0;

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
    lv_font_t* font12 = fontSmall.getLvglFont();
    lv_font_t* font48 = fontClock.getLvglFont();
    
    // Create title label at top (16px font)
    titleLabel = lv_label_create(scr);
    lv_label_set_text(titleLabel, "电子墨水屏时钟 E-Paper Clock");
    lv_obj_set_style_text_color(titleLabel, lv_color_black(), 0);
    lv_obj_set_style_text_font(titleLabel, font16, 0);
    lv_obj_set_style_text_letter_space(titleLabel, 1, 0);
    lv_obj_align(titleLabel, LV_ALIGN_TOP_MID, 0, 4);
    
    // Create time label in center (48px ASCII font)
    timeLabel = lv_label_create(scr);
    lv_label_set_text(timeLabel, "00:00");
    lv_obj_set_style_text_color(timeLabel, lv_color_black(), 0);
    lv_obj_set_style_text_font(timeLabel, font48, 0);
    lv_obj_center(timeLabel);
    
    // Create status label at bottom (12px font) for sensor data
    statusLabel = lv_label_create(scr);
    lv_obj_set_width(statusLabel, lv_pct(100));
    lv_obj_set_style_pad_left(statusLabel, 4, 0);
    lv_obj_set_style_pad_right(statusLabel, 4, 0);
    lv_obj_set_style_text_line_space(statusLabel, 4, 0);
    lv_obj_set_style_text_letter_space(statusLabel, 1, 0);
    lv_label_set_long_mode(statusLabel, LV_LABEL_LONG_WRAP);
    lv_label_set_text(statusLabel, "正在读取传感器... / Reading sensor...");
    lv_obj_set_style_text_color(statusLabel, lv_color_black(), 0);
    lv_obj_set_style_text_font(statusLabel, font12, 0);
    lv_obj_align(statusLabel, LV_ALIGN_BOTTOM_LEFT, 0, -4);
    
    LOG_I("LVGL UI created with dual fonts");
}

bool readAHT20Sensor() {
    if (!aht20Available) {
        return false;
    }
    
    sensors_event_t humidityEvent, tempEvent;
    if (!aht20.getEvent(&humidityEvent, &tempEvent)) {
        LOG_W("Failed to read AHT20 sensor");
        return false;
    }
    
    temperature = tempEvent.temperature;
    humidity = humidityEvent.relative_humidity;
    
    LOG_I("AHT20: Temperature=%.1f°C, Humidity=%.1f%%", temperature, humidity);
    return true;
}

bool readBMP280Sensor() {
    if (!bmp280Available) {
        return false;
    }
    
    pressure = bmp280.readPressure() / 100.0f;  // Convert Pa to hPa
    
    LOG_I("BMP280: Pressure=%.1f hPa", pressure);
    return true;
}

void updateSensorDisplay() {
    if (!aht20Available && !bmp280Available) {
        lv_label_set_text(statusLabel, "传感器未连接 / Sensor not found");
        return;
    }
    
    char sensorStr[96];
    if (aht20Available && bmp280Available) {
        snprintf(sensorStr, sizeof(sensorStr), 
                 "温度: %.1f°C | 湿度: %.1f%% | 气压: %.0f hPa", 
                 temperature, humidity, pressure);
    } else if (aht20Available) {
        snprintf(sensorStr, sizeof(sensorStr), 
                 "温度: %.1f°C | 湿度: %.1f%%", 
                 temperature, humidity);
    } else {
        snprintf(sensorStr, sizeof(sensorStr), 
                 "气压: %.0f hPa", pressure);
    }
    lv_label_set_text(statusLabel, sensorStr);
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
    
    // Initialize I2C for AHT20 sensor
    LOG_I("Initializing I2C (SDA=%d, SCL=%d)...", I2C_SDA, I2C_SCL);
    Wire.begin(I2C_SDA, I2C_SCL);
    
    // Initialize AHT20 sensor
    LOG_I("Initializing AHT20 sensor...");
    if (aht20.begin()) {
        aht20Available = true;
        LOG_I("AHT20 sensor initialized");
    } else {
        aht20Available = false;
        LOG_W("AHT20 sensor not found! Check wiring.");
    }
    
    // Initialize BMP280 sensor
    LOG_I("Initializing BMP280 sensor...");
    if (bmp280.begin(BMP280_ADDRESS)) {  // Default I2C address
        bmp280Available = true;
        LOG_I("BMP280 sensor initialized");
    } else {
        bmp280Available = false;
        LOG_W("BMP280 sensor not found! Check wiring or I2C address.");
    }
    
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
    
    if (fontSmall.begin("/fonts/chs_12.bin", "/fonts/en_12.bin")) {
        LOG_I("12px font loaded (CHS + EN)");
    } else {
        LOG_W("Failed to load 12px font");
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
    
    // Initial sensor reading
    readAHT20Sensor();
    readBMP280Sensor();
    updateSensorDisplay();
    
    // Initial display update
    lastUpdateMs = millis();
    lastSensorUpdateSec = 0;
    updateClockDisplay();
    
    LOG_I("Clock started.");
    LOG_I("Partial refresh enabled, full refresh every %d minutes", FULL_REFRESH_INTERVAL);
    LOG_I("Sensor update interval: %d seconds", SENSOR_UPDATE_INTERVAL);
}

void loop() {
    updateTime();
    
    static uint8_t lastMinute = 255;
    bool needRefresh = false;
    
    // Check if sensor update is needed
    unsigned long currentTotalSec = (unsigned long)hours * 3600 + minutes * 60 + seconds;
    if (currentTotalSec - lastSensorUpdateSec >= SENSOR_UPDATE_INTERVAL) {
        lastSensorUpdateSec = currentTotalSec;
        bool ahtOk = readAHT20Sensor();
        bool bmpOk = readBMP280Sensor();
        if (ahtOk || bmpOk) {
            updateSensorDisplay();
            needRefresh = true;
        }
    }
    
    // Check if time display update is needed (every minute)
    if (minutes != lastMinute) {
        lastMinute = minutes;
        LOG_I("Time: %02d:%02d:%02d", hours, minutes, seconds);
        needRefresh = true;
    }
    
    // Refresh display if needed
    if (needRefresh) {
        updateClockDisplay();
        LOG_I("Display refreshed.");
    }
    
    // Process LVGL tasks (even though we disabled auto-refresh, 
    // this handles internal housekeeping)
    lv_timer_handler();
    
    delay(100);
}
