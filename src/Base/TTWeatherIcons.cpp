#include "TTWeatherTypes.h"
#include <cstdio>
#include <cstring>

static const char* conditionBase(int weatherCode, bool isDay) {
    switch (weatherCode) {
        case 0:
            return isDay ? "wi_day_sunny" : "wi_night_clear";
        case 1:
            return isDay ? "wi_day_sunny_overcast" : "wi_night_partly_cloudy";
        case 2:
            return isDay ? "wi_day_cloudy" : "wi_night_alt_cloudy";
        case 3:
            return "wi_cloudy";
        case 45:
        case 48:
            return isDay ? "wi_day_fog" : "wi_night_fog";
        case 51:
        case 53:
        case 55:
        case 56:
        case 57:
            return isDay ? "wi_day_sprinkle" : "wi_night_alt_sprinkle";
        case 61:
        case 63:
        case 65:
        case 66:
        case 67:
            return isDay ? "wi_day_rain" : "wi_night_alt_rain";
        case 71:
        case 73:
        case 75:
        case 77:
        case 85:
        case 86:
            return isDay ? "wi_day_snow" : "wi_night_alt_snow";
        case 80:
        case 81:
        case 82:
            return isDay ? "wi_day_showers" : "wi_night_alt_showers";
        case 95:
        case 96:
        case 99:
            return isDay ? "wi_day_thunderstorm" : "wi_night_thunderstorm";
        default:
            return isDay ? "wi_day_cloudy" : "wi_night_alt_cloudy";
    }
}

void tt_weather_condition_path(char* out, size_t outMax, int weatherCode, bool isDay, int size) {
    if (out == nullptr || outMax == 0) {
        return;
    }
    snprintf(out, outMax, "/icons/weather/%s_%d.i1", conditionBase(weatherCode, isDay), size);
}

void tt_weather_wind_path(char* out, size_t outMax, int windDeg, int size) {
    if (out == nullptr || outMax == 0) {
        return;
    }
    int deg = windDeg % 360;
    if (deg < 0) {
        deg += 360;
    }
    static const int kDegs[] = { 0, 45, 90, 135, 180, 225, 270, 315 };
    int best = 0;
    int bestDelta = 360;
    for (int i = 0; i < 8; i++) {
        int delta = deg - kDegs[i];
        if (delta < 0) {
            delta = -delta;
        }
        if (delta > 180) {
            delta = 360 - delta;
        }
        if (delta < bestDelta) {
            bestDelta = delta;
            best = kDegs[i];
        }
    }
    snprintf(out, outMax, "/icons/weather/wi_wind_%ddeg_%d.i1", best, size);
}

const char* tt_weather_condition_text(int weatherCode) {
    switch (weatherCode) {
        case 0:
            return "晴";
        case 1:
            return "晴间多云";
        case 2:
            return "多云";
        case 3:
            return "阴";
        case 45:
        case 48:
            return "雾";
        case 51:
        case 53:
        case 55:
        case 56:
        case 57:
            return "毛毛雨";
        case 61:
        case 63:
        case 65:
        case 66:
        case 67:
            return "雨";
        case 71:
        case 73:
        case 75:
        case 77:
            return "雪";
        case 80:
        case 81:
        case 82:
            return "阵雨";
        case 85:
        case 86:
            return "阵雪";
        case 95:
        case 96:
        case 99:
            return "雷暴";
        default:
            return "天气";
    }
}

const char* tt_weather_wind_dir_text(int windDeg) {
    static const char* kDirs[] = { "北", "东北", "东", "东南", "南", "西南", "西", "西北" };
    int deg = windDeg % 360;
    if (deg < 0) {
        deg += 360;
    }
    int idx = ((deg + 22) / 45) % 8;
    return kDirs[idx];
}
