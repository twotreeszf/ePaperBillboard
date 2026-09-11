#pragma once

#include <Arduino.h>
#include <time.h>
#include <Adafruit_AHTX0.h>
#include <Adafruit_BMP280.h>
#include "../Base/TTVTask.h"

#define TT_SENSOR_I2C_SDA   23
#define TT_SENSOR_I2C_SCL   22
#define TT_SENSOR_UPDATE_INTERVAL  60
#define TT_BATTERY_ADC_PIN          33
#define TT_BATTERY_CHARGE_PIN       26
#define TT_BATTERY_ADC_SCALE        7230
#define TT_BATTERY_ADC_MAX          4096
#define TT_BATTERY_USB_MV           4400
#define TT_BATTERY_EMPTY_MV         3500
#define TT_BATTERY_LOW_MV           3700
#define TT_BATTERY_MEDIUM_MV        3900
#define TT_BATTERY_FULL_MV          4200
#define TT_BATTERY_SOC_PAIRS        12
#define TT_BATTERY_SOC_MAP \
    3400, 0, \
    3500, 3, \
    3600, 8, \
    3650, 12, \
    3700, 18, \
    3750, 26, \
    3800, 36, \
    3850, 47, \
    3900, 58, \
    4000, 76, \
    4100, 91, \
    4200, 100

class TTSensorTask : public TTVTask {
public:
    TTSensorTask() : TTVTask("TTSensorTask", 4096) {}

    void requestSensorUpdateAsync();
    void requestRtcWriteAsync(time_t utc);

protected:
    void setup() override;
    void loop() override;

private:
    void performSensorRead();
    void arm();

    Adafruit_AHTX0 _aht20;
    Adafruit_BMP280 _bmp280;
    bool _bmp280Ok = false;
    uint32_t _tickHandle = 0;
};
