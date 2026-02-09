#include "TTSensorTask.h"
#include <Wire.h>
#include "../Base/Logger.h"
#include "../Base/TTInstance.h"
#include "../Base/TTNotificationPayloads.h"
#include "TTUITask.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/timers.h>

void TTSensorTask::setup() {
    LOG_I("Initializing I2C (SDA=%d, SCL=%d)...", TT_SENSOR_I2C_SDA, TT_SENSOR_I2C_SCL);
    Wire.begin(TT_SENSOR_I2C_SDA, TT_SENSOR_I2C_SCL);

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

    _lastUpdateTime = xTaskGetTickCount();
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

    TTSensorDataPayload payload = { temperature, humidity, pressure };
    TTInstanceOf<TTUITask>().postNotification(TT_NOTIFICATION_SENSOR_DATA_UPDATE, payload);
}

void TTSensorTask::requestSensorUpdateAsync() {
    auto* f = new std::function<void()>([this]() {
        performSensorRead();
    });
    enqueue(f);
}

void TTSensorTask::loop() {
    uint32_t currentTime = xTaskGetTickCount();
    uint32_t elapsedSeconds = (currentTime - _lastUpdateTime) / configTICK_RATE_HZ;

    if (elapsedSeconds >= TT_SENSOR_UPDATE_INTERVAL) {
        performSensorRead();
        _lastUpdateTime = currentTime;
    }

    vTaskDelay(pdMS_TO_TICKS(100));
}
