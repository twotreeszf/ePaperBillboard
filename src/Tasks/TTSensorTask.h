#pragma once

#include <Arduino.h>
#include <Adafruit_AHTX0.h>
#include <Adafruit_BMP280.h>
#include "../Base/TTVTask.h"

#define TT_SENSOR_I2C_SDA   23
#define TT_SENSOR_I2C_SCL   22
#define TT_SENSOR_UPDATE_INTERVAL  10 * 60

class TTSensorTask : public TTVTask {
public:
    TTSensorTask() : TTVTask("TTSensorTask", 4096) {}

protected:
    void setup() override;
    void loop() override;

private:
    Adafruit_AHTX0 _aht20;
    Adafruit_BMP280 _bmp280;
    bool _bmp280Ok = false;
};
