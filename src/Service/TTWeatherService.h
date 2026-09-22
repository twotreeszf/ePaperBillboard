#pragma once

#include "../Base/TTWeatherTypes.h"

#define TT_WEATHER_CACHE_PATH        "/weather.bin"
#define TT_WEATHER_CACHE_MAGIC       0x52544857u
#define TT_WEATHER_CACHE_VERSION     1
#define TT_WEATHER_CACHE_MAX_AGE_SEC (60 * 60)
#define TT_WEATHER_CACHE_COORD_EPS   0.0001f

class TTWeatherService {
public:
    void requestFetch(bool force = false);

private:
    void publish(const TTWeatherPayload& payload);
    void publishStatus(TTWeatherState state, const char* message);
    bool loadLocation(float& lat, float& lon, char* city, size_t cityMax);
    bool publishFreshCache(float lat, float lon);
    void saveCache(float lat, float lon, const TTWeatherPayload& payload);
    bool fetchForecast(float lat, float lon, TTWeatherPayload& out);
    bool fetchAqi(float lat, float lon, TTWeatherPayload& out);
    void fetchWeather();
};
