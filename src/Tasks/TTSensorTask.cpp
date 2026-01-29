#include "TTSensorTask.h"
#include <Wire.h>
#include "../Base/Logger.h"
#include "../Base/TTInstance.h"
#include "TTUITask.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

void TTSensorTask::setup() {
    LOG_I("Initializing I2C (SDA=%d, SCL=%d)...", TT_SENSOR_I2C_SDA, TT_SENSOR_I2C_SCL);
    Wire.begin(TT_SENSOR_I2C_SDA, TT_SENSOR_I2C_SCL);

    LOG_I("Initializing AHT20 sensor...");
    if (_aht20.begin()) {
        _aht20Available = true;
        LOG_I("AHT20 sensor initialized");
    } else {
        _aht20Available = false;
        LOG_W("AHT20 sensor not found! Check wiring.");
    }

    LOG_I("Initializing BMP280 sensor...");
    if (_bmp280.begin(BMP280_ADDRESS)) {
        _bmp280Available = true;
        LOG_I("BMP280 sensor initialized");
    } else {
        _bmp280Available = false;
        LOG_W("BMP280 sensor not found! Check wiring or I2C address.");
    }
}

void TTSensorTask::loop() {
    float temperature = 0.0f;
    float humidity = 0.0f;
    float pressure = 0.0f;

    if (_aht20Available) {
        sensors_event_t humidityEvent, tempEvent;
        if (_aht20.getEvent(&humidityEvent, &tempEvent)) {
            temperature = tempEvent.temperature;
            humidity = humidityEvent.relative_humidity;
            LOG_I("AHT20: Temperature=%.1f°C, Humidity=%.1f%%", temperature, humidity);
        }
    }

    if (_bmp280Available) {
        pressure = _bmp280.readPressure() / 100.0f;  // Pa to hPa
        LOG_I("BMP280: Pressure=%.1f hPa", pressure);
    }

    TTInstanceOf<TTUITask>().submitSensorData(temperature, humidity, pressure,
        _aht20Available, _bmp280Available);

    vTaskDelay(pdMS_TO_TICKS((uint32_t)TT_SENSOR_UPDATE_INTERVAL * 1000));
}
