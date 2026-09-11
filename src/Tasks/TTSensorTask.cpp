#include "TTSensorTask.h"
#include <Wire.h>
#include "../Base/Logger.h"
#include "../Base/TTInstance.h"
#include "../Base/TTNotificationPayloads.h"
#include "../Base/TTRtc.h"
#include "TTUITask.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static uint8_t batteryPercentFromMv(int16_t voltageMv) {
    const int16_t kMap[] = { TT_BATTERY_SOC_MAP };
    if (voltageMv <= kMap[0]) {
        return (uint8_t)kMap[1];
    }
    const int last = (TT_BATTERY_SOC_PAIRS - 1) * 2;
    if (voltageMv >= kMap[last]) {
        return (uint8_t)kMap[last + 1];
    }
    for (int i = 0; i < TT_BATTERY_SOC_PAIRS - 1; i++) {
        const int16_t mv0 = kMap[i * 2];
        const int16_t pct0 = kMap[i * 2 + 1];
        const int16_t mv1 = kMap[(i + 1) * 2];
        const int16_t pct1 = kMap[(i + 1) * 2 + 1];
        if (voltageMv <= mv1) {
            const int32_t span = mv1 - mv0;
            if (span <= 0) {
                return (uint8_t)pct1;
            }
            return (uint8_t)(pct0 + (int32_t)(voltageMv - mv0) * (pct1 - pct0) / span);
        }
    }
    return 100;
}

void TTSensorTask::setup() {
    LOG_I("Initializing I2C (SDA=%d, SCL=%d)...", TT_SENSOR_I2C_SDA, TT_SENSOR_I2C_SCL);
    Wire.begin(TT_SENSOR_I2C_SDA, TT_SENSOR_I2C_SCL);
    TTInstanceOf<TTRtc>().begin();

    LOG_I("Initializing AHT20 sensor...");
    if (_aht20.begin()) {
        LOG_I("AHT20 sensor initialized");
    } else {
        LOG_W("AHT20 sensor not found! Check wiring.");
    }

    LOG_I("Initializing BMP280 sensor...");
    _bmp280Ok = _bmp280.begin(BMP280_ADDRESS);
    if (_bmp280Ok) {
        LOG_I("BMP280 sensor initialized");
    } else {
        LOG_W("BMP280 sensor not found! Check wiring or I2C address.");
    }

    pinMode(TT_BATTERY_ADC_PIN, INPUT);
    pinMode(TT_BATTERY_CHARGE_PIN, INPUT);
    LOG_I("Battery ADC GPIO%d, charge GPIO%d (low=charging)",
          TT_BATTERY_ADC_PIN, TT_BATTERY_CHARGE_PIN);

    LOG_I("Sensor read interval %d s", TT_SENSOR_UPDATE_INTERVAL);
    runRepeatWall(TT_SENSOR_UPDATE_INTERVAL * 1000, [this]() {
        performSensorRead();
    });
}

void TTSensorTask::performSensorRead() {
    float temperature = 0.0f;
    float humidity = 0.0f;
    float pressure = 0.0f;

    sensors_event_t humidityEvent, tempEvent;
    if (_aht20.getEvent(&humidityEvent, &tempEvent)) {
        temperature = tempEvent.temperature;
        humidity = humidityEvent.relative_humidity;
        LOG_I("AHT20: Temperature=%.1f°C, Humidity=%.1f%%", temperature, humidity);
    }

    if (_bmp280Ok) {
        pressure = _bmp280.readPressure() / 100.0f;  // Pa to hPa
        LOG_I("BMP280: Pressure=%.1f hPa", pressure);
    }

    const long adc = analogRead(TT_BATTERY_ADC_PIN);
    const int16_t voltageMv = (int16_t)(adc * TT_BATTERY_ADC_SCALE / TT_BATTERY_ADC_MAX);
    const bool usbPlugged = voltageMv > TT_BATTERY_USB_MV;
    const bool charging = digitalRead(TT_BATTERY_CHARGE_PIN) == 0;

    const uint8_t percent = usbPlugged ? 100 : batteryPercentFromMv(voltageMv);

    LOG_I("Battery: %dmV percent=%u usb=%d charging=%d",
          voltageMv, (unsigned)percent, usbPlugged ? 1 : 0, charging ? 1 : 0);

    TTSensorDataPayload payload = {
        temperature, humidity, pressure, voltageMv, percent, charging, usbPlugged
    };
    TTInstanceOf<TTUITask>().postNotification(TT_NOTIFICATION_SENSOR_DATA_UPDATE, payload);
}

void TTSensorTask::requestSensorUpdateAsync() {
    auto* f = new std::function<void()>([this]() {
        performSensorRead();
    });
    enqueue(f);
}

void TTSensorTask::requestRtcWriteAsync(time_t utc) {
    auto* f = new std::function<void()>([utc]() {
        TTInstanceOf<TTRtc>().writeHardware(utc);
    });
    enqueue(f);
}

void TTSensorTask::loop() {
    // Empty loop to keep the task running
}
