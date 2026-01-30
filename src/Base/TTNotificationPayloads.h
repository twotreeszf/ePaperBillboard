#pragma once

#define TT_NOTIFICATION_SENSOR_DATA_UPDATE "TTNotify.SensorDataUpdate"

struct TTSensorDataPayload {
    float temperature;
    float humidity;
    float pressure;
};
