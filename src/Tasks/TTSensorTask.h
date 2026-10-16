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
#define TT_BATTERY_CHARGE_DEBOUNCE_MS  200
#define TT_BATTERY_USB_POLL_MS      200
#define TT_BATTERY_ADC_SCALE        7230
#define TT_BATTERY_ADC_MAX          4096
#define TT_BATTERY_USB_MV           4400
#define TT_BATTERY_USB_OFF_MV       4300
#define TT_BATTERY_EMPTY_MV         3280
#define TT_BATTERY_LOW_MV           3500
#define TT_BATTERY_MEDIUM_MV        3660
#define TT_BATTERY_FULL_MV          4120
#define TT_BATTERY_SOC_PAIRS        12
#define TT_BATTERY_SOC_MAP \
    3280, 0, \
    3420, 4, \
    3550, 10, \
    3650, 18, \
    3730, 28, \
    3800, 38, \
    3860, 48, \
    3920, 58, \
    3980, 68, \
    4040, 76, \
    4090, 84, \
    4120, 100

class TTAHT20 : public Adafruit_AHTX0 {
public:
    bool begin(TwoWire *wire = &Wire, int32_t sensorId = 0,
               uint8_t i2cAddress = AHTX0_I2CADDR_DEFAULT);
};

class TTSensorTask : public TTVTask {
public:
    TTSensorTask() : TTVTask("TTSensorTask", 4096) {}

    void requestSensorUpdateAsync();
    void requestRtcWriteAsync(time_t utc);
    void requestRtcLoadAsync();
    bool copyLastIndoor(float& temperature, float& humidity) const;

protected:
    void setup() override;
    void loop() override;

private:
    void performSensorRead();
    void pollUsbPlug();
    void arm();

    TTAHT20 _aht20;
    Adafruit_BMP280 _bmp280;
    bool _bmp280Ok = false;
    bool _hasIndoor = false;
    float _lastTemperature = 0;
    float _lastHumidity = 0;
    uint32_t _tickHandle = 0;
    uint32_t _chargeIrqMs = 0;
    uint32_t _usbPollMs = 0;
    bool _usbKnown = false;
    bool _lastUsbPlugged = false;
};
