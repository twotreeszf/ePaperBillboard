#pragma once

#include "TTWeatherTypes.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class TTWeatherService {
public:
    void requestFetch(bool force);
    bool isBusy();
    void peek(TTWeatherPayload& payload, bool& hasOk, uint32_t& lastOkMs);
    void holdWifi();
    void releaseWifi();

private:
    void ensureMux();
    void lock();
    void unlock();
    bool tryBeginFetch();
    void endFetch();
    void publishFromCache();
    void cachePatchStatus(TTWeatherState state, bool refreshFailed, const char* message);
    void cacheCommitOk(const TTWeatherPayload& next);
    bool loadLocation(float& lat, float& lon, char* city, size_t cityMax);
    bool fetchForecast(float lat, float lon, TTWeatherPayload& out);
    bool fetchAqi(float lat, float lon, TTWeatherPayload& out);
    void fetchWeather(bool force);

    TTWeatherPayload _payload = {};
    uint32_t _lastOkMs = 0;
    bool _hasOk = false;
    bool _fetchBusy = false;
    bool _wifiHeld = false;
    SemaphoreHandle_t _cacheMux = nullptr;
};
