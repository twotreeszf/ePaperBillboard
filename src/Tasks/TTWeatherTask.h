#pragma once

#include "../Base/TTVTask.h"
#include "../Base/TTNotificationPayloads.h"

#define TT_WEATHER_TASK_STACK     16384
#define TT_WEATHER_LOOP_DELAY_MS  200
#define TT_WEATHER_STALE_MS       (15 * 60 * 1000)
#define TT_WEATHER_HTTP_TIMEOUT_MS  15000
#define TT_WEATHER_URL_MAX        768
#define TT_WEATHER_FORECAST_HOURS 24
#define TT_WEATHER_JSON_BODY_MAX  16384

#define TT_WEATHER_URL_SCHEME     "http"
#define TT_WEATHER_FORECAST_HOST  "api.open-meteo.com"
#define TT_WEATHER_AQI_HOST       "air-quality-api.open-meteo.com"

class TTWeatherTask : public TTVTask {
public:
    TTWeatherTask() : TTVTask("WeatherTask", TT_WEATHER_TASK_STACK) {}

    void requestFetchAsync(bool force = false);
    bool hasOk() const { return _hasOk; }
    const TTWeatherPayload& lastPayload() const { return _payload; }

protected:
    void setup() override;
    void loop() override;

private:
    void fetchWeather(bool force);
    void publish();
    bool loadLocation(float& lat, float& lon, char* city, size_t cityMax);
    bool fetchForecast(float lat, float lon);
    bool fetchAqi(float lat, float lon);

    TTWeatherPayload _payload = {};
    uint32_t _lastOkMs = 0;
    bool _hasOk = false;
};
