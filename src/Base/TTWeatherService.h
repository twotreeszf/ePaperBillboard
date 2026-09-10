#pragma once

#include "TTWeatherTypes.h"

class TTWeatherService {
public:
    void requestFetch();
    void holdWifi();
    void releaseWifi();

private:
    void publish(const TTWeatherPayload& payload);
    void publishStatus(TTWeatherState state, const char* message);
    bool loadLocation(float& lat, float& lon, char* city, size_t cityMax);
    bool fetchForecast(float lat, float lon, TTWeatherPayload& out);
    bool fetchAqi(float lat, float lon, TTWeatherPayload& out);
    void fetchWeather();

    bool _wifiHeld = false;
};
