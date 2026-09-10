#include "TTWeatherService.h"
#include "Logger.h"
#include "TTAsyncQueue.h"
#include "TTInstance.h"
#include "TTNotificationPayloads.h"
#include "TTPreference.h"
#include "../Tasks/TTUITask.h"
#include "../Tasks/TTWiFiTask.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <esp_heap_caps.h>
#include <cmath>
#include <cstring>
#include <ctime>

namespace {

bool httpGetJson(const char* url, JsonDocument& doc, JsonDocument* filter) {
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

}  // namespace

void TTWeatherService::ensureMux() {
    if (_cacheMux == nullptr) {
        _cacheMux = xSemaphoreCreateMutex();
        if (_cacheMux == nullptr) {
            LOG_E("Weather: cache mutex create failed");
        }
    }
}

void TTWeatherService::lock() {
    ensureMux();
    if (_cacheMux != nullptr) {
        xSemaphoreTake(_cacheMux, portMAX_DELAY);
    }
}

void TTWeatherService::unlock() {
    if (_cacheMux != nullptr) {
        xSemaphoreGive(_cacheMux);
    }
}

bool TTWeatherService::isBusy() {
    lock();
    const bool busy = _fetchBusy;
    unlock();
    return busy;
}

void TTWeatherService::peek(TTWeatherPayload& payload, bool& hasOk, uint32_t& lastOkMs) {
    lock();
    payload = _payload;
    hasOk = _hasOk;
    lastOkMs = _lastOkMs;
    unlock();
}

void TTWeatherService::holdWifi() {
    if (_wifiHeld) {
        return;
    }
    _wifiHeld = true;
    LOG_I("Weather: hold Wi-Fi");
    TTInstanceOf<TTWiFiTask>().requestAcquireAsync("weather");
}

void TTWeatherService::releaseWifi() {
    if (!_wifiHeld) {
        return;
    }
    _wifiHeld = false;
    LOG_I("Weather: release Wi-Fi");
    TTInstanceOf<TTWiFiTask>().requestReleaseAsync("weather");
}

bool TTWeatherService::tryBeginFetch() {
    lock();
    if (_fetchBusy) {
        unlock();
        return false;
    }
    _fetchBusy = true;
    unlock();
    return true;
}

void TTWeatherService::endFetch() {
    lock();
    _fetchBusy = false;
    unlock();
}

void TTWeatherService::publishFromCache() {
    TTWeatherPayload copy;
    lock();
    if (_payload.state == TT_WEATHER_OK) {
        _payload.fetchedAtMs = _lastOkMs;
    }
    copy = _payload;
    unlock();
    TTInstanceOf<TTUITask>().postNotification(TT_NOTIFICATION_WEATHER, copy);
}

void TTWeatherService::cachePatchStatus(TTWeatherState state, bool refreshFailed, const char* message) {
    lock();
    _payload.state = state;
    _payload.refreshFailed = refreshFailed;
    if (message == nullptr || message[0] == '\0') {
        _payload.message[0] = '\0';
    } else {
        strncpy(_payload.message, message, sizeof(_payload.message) - 1);
        _payload.message[sizeof(_payload.message) - 1] = '\0';
    }
    unlock();
    publishFromCache();
}

void TTWeatherService::cacheCommitOk(const TTWeatherPayload& next) {
    lock();
    _payload = next;
    _payload.state = TT_WEATHER_OK;
    _payload.refreshFailed = false;
    _payload.message[0] = '\0';
    _hasOk = true;
    _lastOkMs = millis();
    unlock();
    publishFromCache();
}

bool TTWeatherService::loadLocation(float& lat, float& lon, char* city, size_t cityMax) {
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

bool TTWeatherService::fetchForecast(float lat, float lon, TTWeatherPayload& out) {
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
    out.current.temp = current["temperature_2m"] | 0.0f;
    out.current.feelsLike = current["apparent_temperature"] | 0.0f;
    out.current.humidity = current["relative_humidity_2m"] | 0.0f;
    out.current.dewPoint = current["dew_point_2m"] | 0.0f;
    out.current.pressure = current["pressure_msl"] | 0.0f;
    out.current.windSpeed = current["wind_speed_10m"] | 0.0f;
    out.current.windGust = current["wind_gusts_10m"] | 0.0f;
    out.current.windDeg = current["wind_direction_10m"] | 0;
    out.current.precip = current["precipitation"] | 0.0f;
    out.current.weatherCode = current["weather_code"] | 0;
    out.current.visibility = current["visibility"] | 0.0f;
    out.current.isDay = (current["is_day"] | 1) != 0;

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
        memset(&out.daily[i], 0, sizeof(out.daily[i]));
        if (i >= dayCount) {
            continue;
        }
        out.daily[i].time = dailyTime[i] | (int64_t)0;
        out.daily[i].weatherCode = dailyCode[i] | 0;
        out.daily[i].tempMax = dailyMax[i] | 0.0f;
        out.daily[i].tempMin = dailyMin[i] | 0.0f;
        out.daily[i].precipSum = dailyPrecip[i] | 0.0f;
        out.daily[i].precipProb = dailyProb[i] | 0;
        out.daily[i].sunrise = dailyRise[i] | (int64_t)0;
        out.daily[i].sunset = dailySet[i] | (int64_t)0;
    }
    if (dayCount > 0) {
        out.current.sunrise = out.daily[0].sunrise;
        out.current.sunset = out.daily[0].sunset;
        out.current.uvi = dailyUvi[0] | 0.0f;
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
        memset(&out.hourly[i], 0, sizeof(out.hourly[i]));
        const int src = start + i;
        if (src >= hourCount) {
            continue;
        }
        out.hourly[i].time = hourlyTime[src] | (int64_t)0;
        out.hourly[i].temp = hourlyTemp[src] | 0.0f;
        out.hourly[i].humidity = hourlyHum[src] | 0.0f;
        out.hourly[i].precip = hourlyPrecip[src] | 0.0f;
        out.hourly[i].precipProb = hourlyProb[src] | 0.0f;
        out.hourly[i].weatherCode = hourlyCode[src] | 0;
        out.hourly[i].uvi = hourlyUvi[src] | 0.0f;
        out.hourly[i].isDay = (hourlyDay[src] | 1) != 0;
    }
    if (hourCount > start) {
        out.current.uvi = out.hourly[0].uvi;
    }

    LOG_I("Weather: forecast ok temp=%.1f code=%d days=%d hours_from=%d",
          out.current.temp, out.current.weatherCode, dayCount, start);
    return true;
}

bool TTWeatherService::fetchAqi(float lat, float lon, TTWeatherPayload& out) {
    char url[TT_WEATHER_URL_MAX];
    snprintf(url, sizeof(url),
             "%s://%s/v1/air-quality?latitude=%.4f&longitude=%.4f&current=us_aqi",
             TT_WEATHER_URL_SCHEME, TT_WEATHER_AQI_HOST, lat, lon);
    JsonDocument filter;
    filter["current"]["us_aqi"] = true;
    JsonDocument doc;
    if (!httpGetJson(url, doc, &filter)) {
        out.current.hasAqi = false;
        out.current.aqi = 0;
        LOG_W("Weather: AQI fetch failed");
        return false;
    }
    JsonVariantConst aqi = doc["current"]["us_aqi"];
    if (aqi.isNull()) {
        out.current.hasAqi = false;
        out.current.aqi = 0;
        LOG_W("Weather: AQI missing");
        return false;
    }
    out.current.aqi = aqi | 0;
    out.current.hasAqi = true;
    LOG_I("Weather: AQI=%d", out.current.aqi);
    return true;
}

void TTWeatherService::fetchWeather(bool force) {
    TTWeatherPayload draft = {};
    bool hasOk = false;
    uint32_t lastOkMs = 0;
    peek(draft, hasOk, lastOkMs);

    float lat = NAN;
    float lon = NAN;
    loadLocation(lat, lon, draft.city, sizeof(draft.city));

    if (WiFi.status() != WL_CONNECTED) {
        LOG_W("Weather: need Wi-Fi");
        if (hasOk && !force) {
            cachePatchStatus(TT_WEATHER_OK, true, "");
            return;
        }
        cachePatchStatus(TT_WEATHER_NEED_WIFI, false, "未连接 Wi-Fi");
        return;
    }
    if (!isfinite(lat) || !isfinite(lon)) {
        LOG_W("Weather: location not configured");
        if (hasOk && !force) {
            cachePatchStatus(TT_WEATHER_OK, true, "");
            return;
        }
        cachePatchStatus(TT_WEATHER_NEED_LOCATION, false, "未配置地点");
        return;
    }

    if (!force && hasOk && (int32_t)(millis() - lastOkMs) < (int32_t)TT_WEATHER_STALE_MS) {
        LOG_I("Weather: use cached data age=%u ms", (unsigned)(millis() - lastOkMs));
        cachePatchStatus(TT_WEATHER_OK, false, "");
        return;
    }

    if (!hasOk) {
        cachePatchStatus(TT_WEATHER_FETCHING, false, "正在获取天气");
    }

    LOG_I("Weather: fetch lat=%.4f lon=%.4f city=%s force=%d heap=%u largest=%u",
          lat, lon, draft.city, force ? 1 : 0,
          (unsigned)ESP.getFreeHeap(),
          (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));

    if (!fetchForecast(lat, lon, draft)) {
        LOG_E("Weather: forecast failed");
        if (hasOk && !force) {
            cachePatchStatus(TT_WEATHER_OK, true, "");
            return;
        }
        cachePatchStatus(TT_WEATHER_FAILED, false, "获取天气失败");
        return;
    }
    fetchAqi(lat, lon, draft);
    cacheCommitOk(draft);
}

void TTWeatherService::requestFetch(bool force) {
    holdWifi();
    if (!tryBeginFetch()) {
        LOG_I("Weather: fetch ignored (busy)");
        return;
    }
    if (!TTInstanceOf<TTAsyncQueue>().post([this, force]() {
            fetchWeather(force);
            endFetch();
            releaseWifi();
        })) {
        endFetch();
        releaseWifi();
    }
}
