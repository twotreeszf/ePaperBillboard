#pragma once

#include "TTWeatherTypes.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class TTWeatherService {
public:
    void requestFetch();
    bool isBusy();
    void holdWifi();
    void releaseWifi();

private:
    void ensureMux();
    void lock();
    void unlock();
    bool tryBeginFetch();
    void endFetch();
    void publish(const TTWeatherPayload& payload);
    void publishStatus(TTWeatherState state, const char* message);
    bool loadLocation(float& lat, float& lon, char* city, size_t cityMax);
    bool fetchForecast(float lat, float lon, TTWeatherPayload& out);
    bool fetchAqi(float lat, float lon, TTWeatherPayload& out);
    void fetchWeather();

    bool _fetchBusy = false;
    bool _wifiHeld = false;
    SemaphoreHandle_t _mux = nullptr;
};
