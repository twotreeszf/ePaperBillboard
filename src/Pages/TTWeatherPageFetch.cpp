#include "TTWeatherPage.h"
#include "../Tasks/TTUITask.h"
#include "../Base/Logger.h"
#include "../Base/TTAsyncQueue.h"
#include "../Base/TTInstance.h"
#include "../Base/TTPreference.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <esp_heap_caps.h>
#include <cmath>
#include <cstring>
#include <ctime>

static TTWeatherPayload s_payload = {};
static uint32_t s_lastOkMs = 0;
static bool s_hasOk = false;
static volatile bool s_fetchBusy = false;

static void publish() {
    if (s_payload.state == TT_WEATHER_OK) {
        s_payload.fetchedAtMs = s_lastOkMs;
    }
    TTInstanceOf<TTUITask>().postNotification(TT_NOTIFICATION_WEATHER, s_payload);
}

static bool loadLocation(float& lat, float& lon, char* city, size_t cityMax) {
    auto& pref = TTInstanceOf<TTPreference>();
    String cityStr;
    pref.get(PREF_WEATHER_CITY, cityStr, String(""));
    if (city != nullptr && cityMax > 0) {
        strncpy(city, cityStr.c_str(), cityMax - 1);
        city[cityMax - 1] = '\0';
    }

    const float missing = NAN;
    pref.get(PREF_WEATHER_LAT, lat, missing);
    pref.get(PREF_WEATHER_LON, lon, missing);
    if (!isfinite(lat) || !isfinite(lon)) {
        return false;
    }
    if (lat < -90.0f || lat > 90.0f || lon < -180.0f || lon > 180.0f) {
        return false;
    }
    return true;
}

static bool httpGetJson(const char* url, JsonDocument& doc, JsonDocument* filter) {
    WiFiClient client;
    HTTPClient http;
    http.setTimeout(TT_WEATHER_HTTP_TIMEOUT_MS);
    http.useHTTP10(true);
    if (!http.begin(client, url)) {
        LOG_E("Weather: http begin failed");
        return false;
    }
    LOG_I("Weather: GET %s heap=%u largest=%u",
          url,
          (unsigned)ESP.getFreeHeap(),
          (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    const int code = http.GET();
    const int size = http.getSize();
    LOG_I("Weather: HTTP %d size=%d heap=%u largest=%u",
          code, size,
          (unsigned)ESP.getFreeHeap(),
          (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    if (code != HTTP_CODE_OK) {
        http.end();
        return false;
    }
    if (size == 0) {
        LOG_E("Weather: empty body");
        http.end();
        return false;
    }
    if (size > TT_WEATHER_JSON_BODY_MAX) {
        LOG_E("Weather: body too large %d", size);
        http.end();
        return false;
    }

    String body = http.getString();
    http.end();
    if (body.length() == 0) {
        LOG_E("Weather: empty body");
        return false;
    }
    LOG_I("Weather: body len=%u head=%.64s", (unsigned)body.length(), body.c_str());

    DeserializationError err;
    if (filter != nullptr) {
        err = deserializeJson(doc, body, DeserializationOption::Filter(*filter));
    } else {
        err = deserializeJson(doc, body);
    }
    const bool overflowed = doc.overflowed();
    LOG_I("Weather: JSON err=%s overflow=%d heap=%u largest=%u",
          err ? err.c_str() : "ok",
          overflowed ? 1 : 0,
          (unsigned)ESP.getFreeHeap(),
          (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    if (err) {
        LOG_E("Weather: JSON %s", err.c_str());
        return false;
    }
    if (overflowed) {
        LOG_E("Weather: JSON overflowed");
        return false;
    }
    return true;
}

static bool fetchForecast(float lat, float lon) {
    char url[TT_WEATHER_URL_MAX];
    snprintf(url, sizeof(url),
             "%s://%s/v1/forecast?latitude=%.4f&longitude=%.4f"
             "&timezone=auto&forecast_days=%d&forecast_hours=%d"
             "&wind_speed_unit=ms&timeformat=unixtime"
             "&current=temperature_2m,apparent_temperature,relative_humidity_2m,dew_point_2m,"
             "pressure_msl,wind_speed_10m,wind_direction_10m,wind_gusts_10m,precipitation,"
             "weather_code,visibility,is_day"
             "&hourly=temperature_2m,relative_humidity_2m,precipitation,precipitation_probability,weather_code,uv_index,is_day"
             "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_sum,"
             "precipitation_probability_max,sunrise,sunset,uv_index_max",
             TT_WEATHER_URL_SCHEME, TT_WEATHER_FORECAST_HOST, lat, lon,
             TT_WEATHER_DAYS, TT_WEATHER_HOURS);

    JsonDocument filter;
    filter["current"] = true;
    filter["hourly"]["time"] = true;
    filter["hourly"]["temperature_2m"] = true;
    filter["hourly"]["relative_humidity_2m"] = true;
    filter["hourly"]["precipitation"] = true;
    filter["hourly"]["precipitation_probability"] = true;
    filter["hourly"]["weather_code"] = true;
    filter["hourly"]["uv_index"] = true;
    filter["hourly"]["is_day"] = true;
    filter["daily"] = true;

    JsonDocument doc;
    if (!httpGetJson(url, doc, &filter)) {
        return false;
    }

    JsonObject current = doc["current"];
    JsonArray dailyTime = doc["daily"]["time"];
    if (current.isNull() || current["temperature_2m"].isNull() || dailyTime.size() == 0) {
        LOG_E("Weather: JSON missing current/daily current_null=%d daily=%u",
              current.isNull() ? 1 : 0, (unsigned)dailyTime.size());
        return false;
    }
    s_payload.current.temp = current["temperature_2m"] | 0.0f;
    s_payload.current.feelsLike = current["apparent_temperature"] | 0.0f;
    s_payload.current.humidity = current["relative_humidity_2m"] | 0.0f;
    s_payload.current.dewPoint = current["dew_point_2m"] | 0.0f;
    s_payload.current.pressure = current["pressure_msl"] | 0.0f;
    s_payload.current.windSpeed = current["wind_speed_10m"] | 0.0f;
    s_payload.current.windGust = current["wind_gusts_10m"] | 0.0f;
    s_payload.current.windDeg = current["wind_direction_10m"] | 0;
    s_payload.current.precip = current["precipitation"] | 0.0f;
    s_payload.current.weatherCode = current["weather_code"] | 0;
    s_payload.current.visibility = current["visibility"] | 0.0f;
    s_payload.current.isDay = (current["is_day"] | 1) != 0;

    JsonArray dailyCode = doc["daily"]["weather_code"];
    JsonArray dailyMax = doc["daily"]["temperature_2m_max"];
    JsonArray dailyMin = doc["daily"]["temperature_2m_min"];
    JsonArray dailyPrecip = doc["daily"]["precipitation_sum"];
    JsonArray dailyProb = doc["daily"]["precipitation_probability_max"];
    JsonArray dailyRise = doc["daily"]["sunrise"];
    JsonArray dailySet = doc["daily"]["sunset"];
    JsonArray dailyUvi = doc["daily"]["uv_index_max"];
    const int dayCount = min((int)dailyTime.size(), TT_WEATHER_DAYS);
    for (int i = 0; i < TT_WEATHER_DAYS; i++) {
        memset(&s_payload.daily[i], 0, sizeof(s_payload.daily[i]));
        if (i >= dayCount) {
            continue;
        }
        s_payload.daily[i].time = dailyTime[i] | (int64_t)0;
        s_payload.daily[i].weatherCode = dailyCode[i] | 0;
        s_payload.daily[i].tempMax = dailyMax[i] | 0.0f;
        s_payload.daily[i].tempMin = dailyMin[i] | 0.0f;
        s_payload.daily[i].precipSum = dailyPrecip[i] | 0.0f;
        s_payload.daily[i].precipProb = dailyProb[i] | 0;
        s_payload.daily[i].sunrise = dailyRise[i] | (int64_t)0;
        s_payload.daily[i].sunset = dailySet[i] | (int64_t)0;
    }
    if (dayCount > 0) {
        s_payload.current.sunrise = s_payload.daily[0].sunrise;
        s_payload.current.sunset = s_payload.daily[0].sunset;
        s_payload.current.uvi = dailyUvi[0] | 0.0f;
    }

    JsonArray hourlyTime = doc["hourly"]["time"];
    JsonArray hourlyTemp = doc["hourly"]["temperature_2m"];
    JsonArray hourlyHum = doc["hourly"]["relative_humidity_2m"];
    JsonArray hourlyPrecip = doc["hourly"]["precipitation"];
    JsonArray hourlyProb = doc["hourly"]["precipitation_probability"];
    JsonArray hourlyCode = doc["hourly"]["weather_code"];
    JsonArray hourlyUvi = doc["hourly"]["uv_index"];
    JsonArray hourlyDay = doc["hourly"]["is_day"];
    const int hourCount = (int)hourlyTime.size();
    time_t now = time(nullptr);
    int start = 0;
    for (int i = 0; i < hourCount; i++) {
        const time_t t = (time_t)(hourlyTime[i] | (int64_t)0);
        if (t <= now) {
            start = i;
        } else {
            break;
        }
    }
    for (int i = 0; i < TT_WEATHER_HOURS; i++) {
        memset(&s_payload.hourly[i], 0, sizeof(s_payload.hourly[i]));
        const int src = start + i;
        if (src >= hourCount) {
            continue;
        }
        s_payload.hourly[i].time = hourlyTime[src] | (int64_t)0;
        s_payload.hourly[i].temp = hourlyTemp[src] | 0.0f;
        s_payload.hourly[i].humidity = hourlyHum[src] | 0.0f;
        s_payload.hourly[i].precip = hourlyPrecip[src] | 0.0f;
        s_payload.hourly[i].precipProb = hourlyProb[src] | 0.0f;
        s_payload.hourly[i].weatherCode = hourlyCode[src] | 0;
        s_payload.hourly[i].uvi = hourlyUvi[src] | 0.0f;
        s_payload.hourly[i].isDay = (hourlyDay[src] | 1) != 0;
    }
    if (hourCount > start) {
        s_payload.current.uvi = s_payload.hourly[0].uvi;
    }

    LOG_I("Weather: forecast ok temp=%.1f code=%d days=%d hours_from=%d",
          s_payload.current.temp, s_payload.current.weatherCode, dayCount, start);
    return true;
}

static bool fetchAqi(float lat, float lon) {
    char url[TT_WEATHER_URL_MAX];
    snprintf(url, sizeof(url),
             "%s://%s/v1/air-quality?latitude=%.4f&longitude=%.4f&current=us_aqi",
             TT_WEATHER_URL_SCHEME, TT_WEATHER_AQI_HOST, lat, lon);
    JsonDocument filter;
    filter["current"]["us_aqi"] = true;
    JsonDocument doc;
    if (!httpGetJson(url, doc, &filter)) {
        s_payload.current.hasAqi = false;
        s_payload.current.aqi = 0;
        LOG_W("Weather: AQI fetch failed");
        return false;
    }
    JsonVariantConst aqi = doc["current"]["us_aqi"];
    if (aqi.isNull()) {
        s_payload.current.hasAqi = false;
        s_payload.current.aqi = 0;
        LOG_W("Weather: AQI missing");
        return false;
    }
    s_payload.current.aqi = aqi | 0;
    s_payload.current.hasAqi = true;
    LOG_I("Weather: AQI=%d", s_payload.current.aqi);
    return true;
}

static void fetchWeather(bool force) {
    float lat = NAN;
    float lon = NAN;
    loadLocation(lat, lon, s_payload.city, sizeof(s_payload.city));

    if (WiFi.status() != WL_CONNECTED) {
        LOG_W("Weather: need Wi-Fi");
        s_payload.state = TT_WEATHER_NEED_WIFI;
        strncpy(s_payload.message, "未连接 Wi-Fi", sizeof(s_payload.message) - 1);
        s_payload.message[sizeof(s_payload.message) - 1] = '\0';
        publish();
        return;
    }
    if (!isfinite(lat) || !isfinite(lon)) {
        LOG_W("Weather: location not configured");
        s_payload.state = TT_WEATHER_NEED_LOCATION;
        strncpy(s_payload.message, "未配置地点", sizeof(s_payload.message) - 1);
        s_payload.message[sizeof(s_payload.message) - 1] = '\0';
        publish();
        return;
    }

    if (!force && s_hasOk && (int32_t)(millis() - s_lastOkMs) < (int32_t)TT_WEATHER_STALE_MS) {
        LOG_I("Weather: use cached data age=%u ms", (unsigned)(millis() - s_lastOkMs));
        s_payload.state = TT_WEATHER_OK;
        s_payload.message[0] = '\0';
        publish();
        return;
    }

    if (!s_hasOk) {
        s_payload.state = TT_WEATHER_FETCHING;
        strncpy(s_payload.message, "正在获取天气", sizeof(s_payload.message) - 1);
        s_payload.message[sizeof(s_payload.message) - 1] = '\0';
        publish();
    }

    LOG_I("Weather: fetch lat=%.4f lon=%.4f city=%s force=%d heap=%u largest=%u",
          lat, lon, s_payload.city, force ? 1 : 0,
          (unsigned)ESP.getFreeHeap(),
          (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));

    if (!fetchForecast(lat, lon)) {
        LOG_E("Weather: forecast failed");
        s_payload.state = TT_WEATHER_FAILED;
        strncpy(s_payload.message, "获取天气失败", sizeof(s_payload.message) - 1);
        s_payload.message[sizeof(s_payload.message) - 1] = '\0';
        publish();
        return;
    }
    fetchAqi(lat, lon);

    s_payload.state = TT_WEATHER_OK;
    s_payload.message[0] = '\0';
    s_hasOk = true;
    s_lastOkMs = millis();
    publish();
}

bool TTWeatherPage::fetchBusy() {
    return s_fetchBusy;
}

void TTWeatherPage::requestFetch(bool force) {
    if (s_fetchBusy) {
        LOG_I("Weather page: fetch ignored (busy)");
        return;
    }
    if (!force && s_hasOk) {
        if (_visible) {
            applyWeather(s_payload);
        }
        if ((int32_t)(millis() - s_lastOkMs) < (int32_t)TT_WEATHER_STALE_MS) {
            LOG_I("Weather page: use cached data age=%u ms", (unsigned)(millis() - s_lastOkMs));
            return;
        }
    }

    s_fetchBusy = true;
    if (!TTInstanceOf<TTAsyncQueue>().post([force]() {
            fetchWeather(force);
            s_fetchBusy = false;
        })) {
        s_fetchBusy = false;
    }
}
