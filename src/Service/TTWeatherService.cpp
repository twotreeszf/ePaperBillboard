#include "TTWeatherService.h"
#include "../Base/Logger.h"
#include "../Base/TTHttpsClient.h"
#include "../Base/TTFile.h"
#include "../Base/TTInstance.h"
#include "../Base/TTNotificationPayloads.h"
#include "../Base/TTPreference.h"
#include "../Base/TTRtc.h"
#include "../Tasks/TTUITask.h"
#include "../Tasks/TTWiFiTask.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace {

bool httpGetJson(const char* url, JsonDocument& doc, JsonDocument* filter, size_t bodyMax) {
    TTHttpsResult res = {};
    TTHttpsRequest request = {};
    request.url = url;
    request.bodyMax = bodyMax;
    LOG_I("Weather: GET heap=%u largest=%u body_max=%u",
          (unsigned)ESP.getFreeHeap(),
          (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
          (unsigned)bodyMax);
    if (!tt_https_exchange_file(&request, TT_HTTPS_TMP_RESP, &res)) {
        LOG_E("Weather: HTTPS file get failed");
        return false;
    }
    if (res.status != 200) {
        LOG_E("Weather: HTTP %d body_len=%u", res.status, (unsigned)res.bodyLen);
        tt_file_remove(TT_HTTPS_TMP_RESP);
        return false;
    }
    if (res.bodyLen == 0) {
        LOG_E("Weather: empty body");
        tt_file_remove(TT_HTTPS_TMP_RESP);
        return false;
    }

    File file = tt_file_open(TT_HTTPS_TMP_RESP, "r");
    if (!file) {
        return false;
    }
    if (!file.seek(res.bodyOffset)) {
        LOG_E("Weather: seek body failed");
        file.close();
        tt_file_remove(TT_HTTPS_TMP_RESP);
        return false;
    }

    DeserializationError err;
    if (filter != nullptr) {
        err = deserializeJson(doc, file, DeserializationOption::Filter(*filter));
    } else {
        err = deserializeJson(doc, file);
    }
    file.close();
    tt_file_remove(TT_HTTPS_TMP_RESP);

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

const char* jsonText(JsonVariantConst value) {
    if (value.is<const char*>()) {
        const char* text = value.as<const char*>();
        return text != nullptr ? text : "";
    }
    return "";
}

float jsonIndex(JsonVariantConst value) {
    if (value.isNull()) {
        return 0.0f;
    }
    if (value.is<const char*>()) {
        return strtof(jsonText(value), nullptr);
    }
    return value | 0.0f;
}

time_t unixFromUtc(int year, int month, int day, int hour, int minute, int second) {
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = (unsigned)(year - era * 400);
    const unsigned doy = (153u * (unsigned)(month + (month > 2 ? -3 : 9)) + 2u) / 5u + (unsigned)day - 1u;
    const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    const int days = era * 146097 + (int)doe - 719468;
    return (time_t)days * 86400 + (time_t)hour * 3600 + (time_t)minute * 60 + second;
}

bool parseIsoLocal(const char* text, time_t& out) {
    if (text == nullptr || text[0] == '\0') {
        return false;
    }
    int year = 0;
    int mon = 0;
    int day = 0;
    int hour = 0;
    int min = 0;
    int sec = 0;
    if (sscanf(text, "%d-%d-%dT%d:%d:%d", &year, &mon, &day, &hour, &min, &sec) < 5) {
        return false;
    }
    const char* zone = strchr(text, 'T');
    zone = zone != nullptr ? strpbrk(zone + 1, "+-") : nullptr;
    char sign = '+';
    int offHour = 0;
    int offMin = 0;
    if (zone != nullptr) {
        sign = *zone;
        sscanf(zone + 1, "%d:%d", &offHour, &offMin);
    }
    if (year < 1970 || mon < 1 || mon > 12 || day < 1 || day > 31) {
        return false;
    }
    const time_t asUtc = unixFromUtc(year, mon, day, hour, min, sec);
    int offset = offHour * 3600 + offMin * 60;
    if (sign == '-') {
        offset = -offset;
    }
    out = asUtc - (time_t)offset;
    return true;
}

bool parseSunTime(const char* dateText, const char* clockText, time_t& out) {
    int year = 0;
    int mon = 0;
    int day = 0;
    int hour = 0;
    int min = 0;
    if (dateText == nullptr || clockText == nullptr
        || sscanf(dateText, "%d-%d-%d", &year, &mon, &day) != 3
        || sscanf(clockText, "%d:%d", &hour, &min) != 2) {
        return false;
    }
    const char* zone = strchr(dateText, 'T');
    zone = zone != nullptr ? strpbrk(zone + 1, "+-") : nullptr;
    char rebuilt[40];
    snprintf(rebuilt, sizeof(rebuilt), "%04d-%02d-%02dT%02d:%02d:00%s",
             year, mon, day, hour, min, zone != nullptr ? zone : "Z");
    return parseIsoLocal(rebuilt, out);
}

int skyconCode(const char* skycon) {
    if (skycon == nullptr || skycon[0] == '\0') {
        return 2;
    }
    if (strcmp(skycon, "CLEAR_DAY") == 0 || strcmp(skycon, "CLEAR_NIGHT") == 0) {
        return 0;
    }
    if (strcmp(skycon, "PARTLY_CLOUDY_DAY") == 0 || strcmp(skycon, "PARTLY_CLOUDY_NIGHT") == 0) {
        return 1;
    }
    if (strcmp(skycon, "CLOUDY") == 0) {
        return 3;
    }
    if (strcmp(skycon, "LIGHT_HAZE") == 0 || strcmp(skycon, "MODERATE_HAZE") == 0
        || strcmp(skycon, "HEAVY_HAZE") == 0 || strcmp(skycon, "FOG") == 0
        || strcmp(skycon, "DUST") == 0 || strcmp(skycon, "SAND") == 0) {
        return 45;
    }
    if (strcmp(skycon, "LIGHT_RAIN") == 0) {
        return 61;
    }
    if (strcmp(skycon, "MODERATE_RAIN") == 0) {
        return 63;
    }
    if (strcmp(skycon, "HEAVY_RAIN") == 0 || strcmp(skycon, "STORM_RAIN") == 0) {
        return 65;
    }
    if (strcmp(skycon, "LIGHT_SNOW") == 0) {
        return 71;
    }
    if (strcmp(skycon, "MODERATE_SNOW") == 0) {
        return 73;
    }
    if (strcmp(skycon, "HEAVY_SNOW") == 0 || strcmp(skycon, "STORM_SNOW") == 0) {
        return 75;
    }
    if (strcmp(skycon, "WIND") == 0) {
        return 2;
    }
    return 2;
}

int skyconDay(const char* skycon) {
    if (skycon == nullptr) {
        return -1;
    }
    if (strstr(skycon, "_NIGHT") != nullptr) {
        return 0;
    }
    if (strstr(skycon, "_DAY") != nullptr) {
        return 1;
    }
    return -1;
}

bool hourIsDay(time_t hour, const TTWeatherPayload& out, int skyDay) {
    if (skyDay >= 0) {
        return skyDay == 1;
    }
    for (int i = 0; i < TT_WEATHER_DAYS; i++) {
        const time_t dayStart = (time_t)out.daily[i].time;
        if (dayStart <= 0 || hour < dayStart || hour >= dayStart + 86400) {
            continue;
        }
        if (out.daily[i].sunrise > 0 && out.daily[i].sunset > out.daily[i].sunrise) {
            return hour >= out.daily[i].sunrise && hour < out.daily[i].sunset;
        }
    }
    return true;
}

float dewPointC(float tempC, float humidityPct) {
    if (humidityPct <= 0.0f) {
        return tempC;
    }
    if (humidityPct > TT_WEATHER_HUMIDITY_PERCENT) {
        humidityPct = TT_WEATHER_HUMIDITY_PERCENT;
    }
    const float a = 17.27f;
    const float b = 237.7f;
    const float gamma = logf(humidityPct / TT_WEATHER_HUMIDITY_PERCENT) + (a * tempC) / (b + tempC);
    const float denom = a - gamma;
    if (fabsf(denom) < 0.01f) {
        return tempC;
    }
    return (b * gamma) / denom;
}

}  // namespace

void TTWeatherService::publish(const TTWeatherPayload& payload) {
    TTInstanceOf<TTUITask>().postNotification(TT_NOTIFICATION_WEATHER, payload);
}

void TTWeatherService::publishStatus(TTWeatherState state, const char* message) {
    TTWeatherPayload payload = {};
    payload.state = state;
    payload.refreshFailed = state != TT_WEATHER_OK && state != TT_WEATHER_FETCHING;
    if (message != nullptr && message[0] != '\0') {
        strncpy(payload.message, message, sizeof(payload.message) - 1);
        payload.message[sizeof(payload.message) - 1] = '\0';
    }
    publish(payload);
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

struct TTWeatherCacheFile {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    float lat;
    float lon;
    TTWeatherPayload payload;
};

bool coordsMatch(float a, float b) {
    return fabsf(a - b) <= TT_WEATHER_CACHE_COORD_EPS;
}

bool TTWeatherService::useCaiyun() const {
    String region;
    TTInstanceOf<TTPreference>().get(PREF_WEATHER_REGION, region, String(TT_WEATHER_REGION_CN));
    return region != TT_WEATHER_REGION_OVERSEAS;
}

bool TTWeatherService::publishFreshCache(float lat, float lon, uint16_t source) {
    if (!TTInstanceOf<TTRtc>().isTimeValid()) {
        LOG_I("Weather: cache skipped, clock invalid");
        return false;
    }
    const time_t now = time(nullptr);
    if (now <= 0) {
        return false;
    }
    File file = tt_file_open(TT_WEATHER_CACHE_PATH, "r");
    if (!file) {
        return false;
    }
    TTWeatherCacheFile* cache = new TTWeatherCacheFile;
    if (cache == nullptr) {
        file.close();
        LOG_E("Weather: cache alloc failed");
        return false;
    }
    const size_t got = file.read((uint8_t*)cache, sizeof(*cache));
    file.close();
    if (got != sizeof(*cache)
        || cache->magic != TT_WEATHER_CACHE_MAGIC
        || cache->version != TT_WEATHER_CACHE_VERSION
        || cache->payload.state != TT_WEATHER_OK
        || cache->reserved != source
        || !coordsMatch(cache->lat, lat)
        || !coordsMatch(cache->lon, lon)) {
        delete cache;
        LOG_I("Weather: cache miss");
        return false;
    }
    const uint32_t fetchedAt = cache->payload.fetchedAt;
    if (fetchedAt == 0 || (time_t)fetchedAt > now
        || now - (time_t)fetchedAt > TT_WEATHER_CACHE_MAX_AGE_SEC) {
        delete cache;
        LOG_I("Weather: cache stale fetchedAt=%u now=%ld", (unsigned)fetchedAt, (long)now);
        return false;
    }
    LOG_I("Weather: reuse cache age=%ld s", (long)(now - (time_t)fetchedAt));
    publish(cache->payload);
    delete cache;
    return true;
}

void TTWeatherService::saveCache(float lat, float lon, uint16_t source, const TTWeatherPayload& payload) {
    TTWeatherCacheFile* cache = new TTWeatherCacheFile;
    if (cache == nullptr) {
        LOG_E("Weather: cache save alloc failed");
        return;
    }
    cache->magic = TT_WEATHER_CACHE_MAGIC;
    cache->version = TT_WEATHER_CACHE_VERSION;
    cache->reserved = source;
    cache->lat = lat;
    cache->lon = lon;
    cache->payload = payload;
    File file = tt_file_create(TT_WEATHER_CACHE_PATH);
    if (!file) {
        delete cache;
        LOG_E("Weather: cache create failed");
        return;
    }
    const size_t wrote = file.write((const uint8_t*)cache, sizeof(*cache));
    file.close();
    delete cache;
    if (wrote != sizeof(TTWeatherCacheFile)) {
        LOG_E("Weather: cache write %u/%u", (unsigned)wrote, (unsigned)sizeof(TTWeatherCacheFile));
        tt_file_remove(TT_WEATHER_CACHE_PATH);
        return;
    }
    LOG_I("Weather: cache saved %u bytes", (unsigned)wrote);
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
    if (!httpGetJson(url, doc, &filter, 0)) {
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

bool TTWeatherService::fetchCaiyun(float lat, float lon, TTWeatherPayload& out) {
    char url[TT_WEATHER_URL_MAX];
    snprintf(url, sizeof(url),
             "%s://%s/v2.6/%s/%.4f,%.4f/weather?dailysteps=%d&hourlysteps=%d&alert=false&unit=metric:v2",
             TT_WEATHER_URL_SCHEME, TT_WEATHER_CAIYUN_HOST, TT_WEATHER_CAIYUN_TOKEN,
             lon, lat, TT_WEATHER_DAYS, TT_WEATHER_HOURS);

    JsonDocument filter;
    filter["status"] = true;
    JsonObject realtime = filter["result"]["realtime"].to<JsonObject>();
    realtime["status"] = true;
    realtime["temperature"] = true;
    realtime["apparent_temperature"] = true;
    realtime["humidity"] = true;
    realtime["skycon"] = true;
    realtime["visibility"] = true;
    realtime["pressure"] = true;
    realtime["wind"]["speed"] = true;
    realtime["wind"]["direction"] = true;
    realtime["precipitation"]["local"]["intensity"] = true;
    realtime["air_quality"]["aqi"]["chn"] = true;
    realtime["life_index"]["ultraviolet"]["index"] = true;
    JsonObject hourly = filter["result"]["hourly"].to<JsonObject>();
    hourly["temperature"] = true;
    hourly["humidity"] = true;
    hourly["precipitation"] = true;
    hourly["skycon"] = true;
    JsonObject daily = filter["result"]["daily"].to<JsonObject>();
    daily["temperature"] = true;
    daily["precipitation"] = true;
    daily["skycon"] = true;
    daily["astro"] = true;
    daily["life_index"]["ultraviolet"] = true;

    JsonDocument doc;
    if (!httpGetJson(url, doc, &filter, TT_WEATHER_CAIYUN_BODY_MAX)) {
        return false;
    }
    if (strcmp(jsonText(doc["status"]), "ok") != 0) {
        LOG_E("Weather: caiyun status=%s", jsonText(doc["status"]));
        return false;
    }
    JsonObject resultRealtime = doc["result"]["realtime"];
    JsonArray dailyTemp = doc["result"]["daily"]["temperature"];
    if (resultRealtime.isNull() || resultRealtime["temperature"].isNull()
        || strcmp(jsonText(resultRealtime["status"]), "ok") != 0
        || dailyTemp.size() == 0) {
        LOG_E("Weather: caiyun missing realtime/daily");
        return false;
    }

    JsonArray dailySky = doc["result"]["daily"]["skycon"];
    JsonArray dailyPrecip = doc["result"]["daily"]["precipitation"];
    JsonArray dailyAstro = doc["result"]["daily"]["astro"];
    JsonArray dailyUv = doc["result"]["daily"]["life_index"]["ultraviolet"];
    const int dayCount = min((int)dailyTemp.size(), TT_WEATHER_DAYS);
    for (int i = 0; i < TT_WEATHER_DAYS; i++) {
        memset(&out.daily[i], 0, sizeof(out.daily[i]));
        if (i >= dayCount) {
            continue;
        }
        const char* dateText = jsonText(dailyTemp[i]["date"]);
        time_t dayTime = 0;
        if (!parseIsoLocal(dateText, dayTime)) {
            dateText = jsonText(dailySky[i]["date"]);
            parseIsoLocal(dateText, dayTime);
        }
        out.daily[i].time = (int64_t)dayTime;
        out.daily[i].weatherCode = skyconCode(jsonText(dailySky[i]["value"]));
        out.daily[i].tempMax = dailyTemp[i]["max"] | 0.0f;
        out.daily[i].tempMin = dailyTemp[i]["min"] | 0.0f;
        out.daily[i].precipSum = dailyPrecip[i]["avg"] | 0.0f;
        out.daily[i].precipProb = dailyPrecip[i]["probability"] | 0;
        time_t rise = 0;
        time_t set = 0;
        const char* astroDate = jsonText(dailyAstro[i]["date"]);
        if (astroDate[0] == '\0') {
            astroDate = dateText;
        }
        if (parseSunTime(astroDate, jsonText(dailyAstro[i]["sunrise"]["time"]), rise)) {
            out.daily[i].sunrise = (int64_t)rise;
        }
        if (parseSunTime(astroDate, jsonText(dailyAstro[i]["sunset"]["time"]), set)) {
            out.daily[i].sunset = (int64_t)set;
        }
    }

    const char* skycon = jsonText(resultRealtime["skycon"]);
    const float humidityPct = (resultRealtime["humidity"] | 0.0f) * TT_WEATHER_HUMIDITY_PERCENT;
    out.current.temp = resultRealtime["temperature"] | 0.0f;
    out.current.feelsLike = resultRealtime["apparent_temperature"] | out.current.temp;
    out.current.humidity = humidityPct;
    out.current.dewPoint = dewPointC(out.current.temp, humidityPct);
    out.current.pressure = (resultRealtime["pressure"] | 0.0f) / TT_WEATHER_PA_PER_HPA;
    out.current.windSpeed = (resultRealtime["wind"]["speed"] | 0.0f) / TT_WEATHER_KMH_PER_MS;
    out.current.windGust = 0.0f;
    out.current.windDeg = (int)(resultRealtime["wind"]["direction"] | 0.0f);
    out.current.precip = resultRealtime["precipitation"]["local"]["intensity"] | 0.0f;
    out.current.weatherCode = skyconCode(skycon);
    out.current.visibility = (resultRealtime["visibility"] | 0.0f) * TT_WEATHER_M_PER_KM;
    out.current.uvi = jsonIndex(resultRealtime["life_index"]["ultraviolet"]["index"]);
    if (out.current.uvi <= 0.0f && dayCount > 0) {
        out.current.uvi = jsonIndex(dailyUv[0]["index"]);
    }
    if (dayCount > 0) {
        out.current.sunrise = out.daily[0].sunrise;
        out.current.sunset = out.daily[0].sunset;
    }
    const int skyDay = skyconDay(skycon);
    const time_t now = time(nullptr);
    if (skyDay >= 0) {
        out.current.isDay = skyDay == 1;
    } else if (out.current.sunrise > 0 && out.current.sunset > out.current.sunrise) {
        out.current.isDay = now >= out.current.sunrise && now < out.current.sunset;
    } else {
        out.current.isDay = true;
    }
    JsonVariantConst aqi = resultRealtime["air_quality"]["aqi"]["chn"];
    if (aqi.isNull()) {
        out.current.hasAqi = false;
        out.current.aqi = 0;
        LOG_W("Weather: caiyun AQI missing");
    } else {
        out.current.aqi = aqi | 0;
        out.current.hasAqi = true;
    }

    JsonArray hourlyTemp = doc["result"]["hourly"]["temperature"];
    JsonArray hourlyHum = doc["result"]["hourly"]["humidity"];
    JsonArray hourlyPrecip = doc["result"]["hourly"]["precipitation"];
    JsonArray hourlySky = doc["result"]["hourly"]["skycon"];
    const int hourCount = (int)hourlyTemp.size();
    int start = 0;
    for (int i = 0; i < hourCount; i++) {
        time_t hourTime = 0;
        if (!parseIsoLocal(jsonText(hourlyTemp[i]["datetime"]), hourTime)) {
            continue;
        }
        if (hourTime <= now) {
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
        time_t hourTime = 0;
        parseIsoLocal(jsonText(hourlyTemp[src]["datetime"]), hourTime);
        const char* hourSky = jsonText(hourlySky[src]["value"]);
        out.hourly[i].time = (int64_t)hourTime;
        out.hourly[i].temp = hourlyTemp[src]["value"] | 0.0f;
        out.hourly[i].humidity = (hourlyHum[src]["value"] | 0.0f) * TT_WEATHER_HUMIDITY_PERCENT;
        out.hourly[i].precip = hourlyPrecip[src]["value"] | 0.0f;
        out.hourly[i].precipProb = hourlyPrecip[src]["probability"] | 0.0f;
        out.hourly[i].weatherCode = skyconCode(hourSky);
        out.hourly[i].uvi = 0.0f;
        out.hourly[i].isDay = hourIsDay(hourTime, out, skyconDay(hourSky));
    }

    LOG_I("Weather: caiyun ok temp=%.1f code=%d aqi=%d days=%d hours_from=%d",
          out.current.temp, out.current.weatherCode, out.current.aqi, dayCount, start);
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
    if (!httpGetJson(url, doc, &filter, 0)) {
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

void TTWeatherService::fetchWeather() {
    TTWeatherPayload draft = {};
    float lat = NAN;
    float lon = NAN;
    loadLocation(lat, lon, draft.city, sizeof(draft.city));

    if (WiFi.status() != WL_CONNECTED) {
        LOG_W("Weather: need Wi-Fi");
        publishStatus(TT_WEATHER_NEED_WIFI, "未连接 Wi-Fi");
        return;
    }
    if (!isfinite(lat) || !isfinite(lon)) {
        LOG_W("Weather: location not configured");
        publishStatus(TT_WEATHER_NEED_LOCATION, "未配置地点");
        return;
    }

    const bool caiyun = useCaiyun();
    LOG_I("Weather: fetch source=%s lat=%.4f lon=%.4f city=%s heap=%u largest=%u",
          caiyun ? "caiyun" : "open-meteo",
          lat, lon, draft.city,
          (unsigned)ESP.getFreeHeap(),
          (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));

    const bool fetched = caiyun ? fetchCaiyun(lat, lon, draft) : fetchForecast(lat, lon, draft);
    if (!fetched) {
        LOG_E("Weather: forecast failed");
        publishStatus(TT_WEATHER_FAILED, "获取天气失败");
        return;
    }
    if (!caiyun) {
        fetchAqi(lat, lon, draft);
    }
    draft.state = TT_WEATHER_OK;
    draft.refreshFailed = false;
    draft.message[0] = '\0';
    const time_t now = time(nullptr);
    draft.fetchedAt = (now > 0) ? (uint32_t)now : 1;
    saveCache(lat, lon, caiyun ? TT_WEATHER_SOURCE_CAIYUN : TT_WEATHER_SOURCE_OPEN_METEO, draft);
    publish(draft);
}

void TTWeatherService::requestFetch(bool force) {
    float lat = NAN;
    float lon = NAN;
    loadLocation(lat, lon, nullptr, 0);
    const uint16_t source = useCaiyun() ? TT_WEATHER_SOURCE_CAIYUN : TT_WEATHER_SOURCE_OPEN_METEO;
    if (!force && isfinite(lat) && isfinite(lon) && publishFreshCache(lat, lon, source)) {
        return;
    }
    TTInstanceOf<TTWiFiTask>().runWithRadio(
        "weather",
        [this]() {
            fetchWeather();
        },
        [this]() {
            LOG_W("Weather: radio failed");
            publishStatus(TT_WEATHER_NEED_WIFI, "未连接 Wi-Fi");
        });
}
