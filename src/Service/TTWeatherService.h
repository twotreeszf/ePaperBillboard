#pragma once

#include "../Base/TTFile.h"
#include "../Base/TTWeatherTypes.h"

#define TT_WEATHER_CACHE_PATH        TT_FS_TMP_DIR "/weather.bin"
#define TT_WEATHER_CACHE_MAGIC       0x52544857u
#define TT_WEATHER_CACHE_VERSION     2
#define TT_WEATHER_SOURCE_OPEN_METEO 0
#define TT_WEATHER_SOURCE_CAIYUN     1
#define TT_WEATHER_CAIYUN_HOST       "api.caiyunapp.com"
#define TT_WEATHER_CAIYUN_TOKEN      "NYEoULA67nCT0xpX"
#define TT_WEATHER_CAIYUN_BODY_MAX   (48 * 1024)
#define TT_WEATHER_KMH_PER_MS        3.6f
#define TT_WEATHER_PA_PER_HPA        100.0f
#define TT_WEATHER_M_PER_KM          1000.0f
#define TT_WEATHER_HUMIDITY_PERCENT  100.0f
#define TT_WEATHER_CACHE_MAX_AGE_SEC (30 * 60)
#define TT_WEATHER_CACHE_COORD_EPS   0.0001f

class TTWeatherService {
public:
    void requestFetch(bool force = false);

private:
    void publish(const TTWeatherPayload& payload);
    void publishStatus(TTWeatherState state, const char* message);
    bool loadLocation(float& lat, float& lon, char* city, size_t cityMax);
    bool useCaiyun() const;
    bool publishFreshCache(float lat, float lon, uint16_t source);
    void saveCache(float lat, float lon, uint16_t source, const TTWeatherPayload& payload);
    bool fetchForecast(float lat, float lon, TTWeatherPayload& out);
    bool fetchCaiyun(float lat, float lon, TTWeatherPayload& out);
    bool fetchAqi(float lat, float lon, TTWeatherPayload& out);
    void fetchWeather();
};
