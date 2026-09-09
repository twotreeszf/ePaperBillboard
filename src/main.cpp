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
#include "Base/Logger.h"
#include "Base/TTInstance.h"
#include "Base/TTPreference.h"
#include "Base/Util.h"
#include "Tasks/TTUITask.h"
#include "Tasks/TTSensorTask.h"
#include "Tasks/TTWiFiTask.h"
#include "Base/TTAsyncQueue.h"

void setup() {
    _logger.setLevel(LOG_LEVEL_DEBUG);
    Serial.begin(115200);
    delay(100);
    LOG_I("");
    LOG_I("=================================");
    LOG_I("E-Paper Weather Clock (LVGL Version)");
    LOG_I("=================================");

    Util::printChipInfo();
    if (!TTInstanceOf<TTPreference>().begin()) {
        LOG_E("Preference begin failed");
    }
    delay(200);

    TTInstanceOf<TTUITask>().start(0, TT_UI_LOOP_DELAY_MS);
    TTInstanceOf<TTSensorTask>().start(1);
    TTInstanceOf<TTWiFiTask>().start(TT_WIFI_TASK_CORE, TT_WIFI_LOOP_DELAY_MS);
    TTInstanceOf<TTAsyncQueue>().start(TT_WIFI_TASK_CORE, TT_ASYNC_LOOP_MS);
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(1000));
}
