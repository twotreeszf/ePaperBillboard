#pragma once

#include <stddef.h>
#include <stdint.h>

#define PREF_WEATHER_CITY  "weather_city"
#define PREF_WEATHER_LAT   "weather_lat"
#define PREF_WEATHER_LON   "weather_lon"

#define TT_WEATHER_CITY_MAX   32
#define TT_WEATHER_MSG_MAX    48
#define TT_WEATHER_HOURS      24
#define TT_WEATHER_DAYS            6
#define TT_WEATHER_FORECAST_OFFSET 1
#define TT_WEATHER_ICON_PATH_MAX  64
#define TT_WEATHER_STALE_MS       (15 * 60 * 1000)
#define TT_WEATHER_HTTP_TIMEOUT_MS  15000
#define TT_WEATHER_URL_MAX        768
#define TT_WEATHER_JSON_BODY_MAX  16384
#define TT_WEATHER_URL_SCHEME     "http"
#define TT_WEATHER_FORECAST_HOST  "api.open-meteo.com"
#define TT_WEATHER_AQI_HOST       "air-quality-api.open-meteo.com"

enum TTWeatherState {
    TT_WEATHER_IDLE = 0,
    TT_WEATHER_NEED_WIFI,
    TT_WEATHER_NEED_LOCATION,
    TT_WEATHER_FETCHING,
    TT_WEATHER_OK,
    TT_WEATHER_FAILED,
};

struct TTWeatherCurrent {
    float temp;
    float feelsLike;
    float humidity;
    float dewPoint;
    float pressure;
    float windSpeed;
    float windGust;
    float uvi;
    float visibility;
    float precip;
    int windDeg;
    int weatherCode;
    int aqi;
    bool isDay;
    bool hasAqi;
    int64_t sunrise;
    int64_t sunset;
};

struct TTWeatherDaily {
    int64_t time;
    int64_t sunrise;
    int64_t sunset;
    float tempMax;
    float tempMin;
    float precipSum;
    int weatherCode;
    int precipProb;
};

struct TTWeatherHourly {
    int64_t time;
    float temp;
    float humidity;
    float precip;
    float precipProb;
    float uvi;
    int weatherCode;
    bool isDay;
};

struct TTWeatherPayload {
    TTWeatherState state;
    char city[TT_WEATHER_CITY_MAX + 1];
    char message[TT_WEATHER_MSG_MAX + 1];
    uint32_t fetchedAtMs;
    bool refreshFailed;
    TTWeatherCurrent current;
    TTWeatherDaily daily[TT_WEATHER_DAYS];
    TTWeatherHourly hourly[TT_WEATHER_HOURS];
};

void tt_weather_condition_path(char* out, size_t outMax, int weatherCode, bool isDay, int size);
void tt_weather_wind_path(char* out, size_t outMax, int windDeg, int size);
const char* tt_weather_condition_text(int weatherCode);
const char* tt_weather_wind_dir_text(int windDeg);
