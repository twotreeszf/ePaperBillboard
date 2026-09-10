#pragma once

#include <lvgl.h>
#include "../Base/TTScreenPage.h"
#include "../Base/TTNotificationPayloads.h"

#define TT_WEATHER_TEMP_FONT      48
#define TT_WEATHER_TEMP_LINE_H    49
#define TT_WEATHER_FEELS_FONT     12
#define TT_WEATHER_FEELS_LINE_H   16
#define TT_WEATHER_COND_FONT      16
#define TT_WEATHER_COND_PAD_X     5
#define TT_WEATHER_COND_PAD_Y     1
#define TT_WEATHER_COND_LINE_H    (16 + TT_WEATHER_COND_PAD_Y * 2)
#define TT_WEATHER_TEXT_STACK_GAP 2
#define TT_WEATHER_FEELS_GAP      (TT_WEATHER_TEXT_STACK_GAP - 3)
#define TT_WEATHER_TEXT_STACK_H   (TT_WEATHER_TEMP_LINE_H + TT_WEATHER_TEXT_STACK_GAP + \
                                   TT_WEATHER_FEELS_LINE_H + TT_WEATHER_TEXT_STACK_GAP + \
                                   TT_WEATHER_COND_LINE_H)
#define TT_WEATHER_ICON_CURRENT   97
#define TT_WEATHER_TEMP_DOT_SIZE  6
#define TT_WEATHER_TEMP_DOT_GAP_X 3
#define TT_WEATHER_TEMP_DOT_GAP_Y 8
#define TT_WEATHER_ICON_DAY       45
#define TT_WEATHER_ICON_HOUR      24
#define TT_WEATHER_ICON_CELL      40
#define TT_WEATHER_FORECAST_N     5
#define TT_WEATHER_FORECAST_Y     34
#define TT_WEATHER_FORECAST_ICON_Y  (TT_WEATHER_FORECAST_Y + 12)
#define TT_WEATHER_FORECAST_TEMPS_Y (TT_WEATHER_FORECAST_ICON_Y + TT_WEATHER_ICON_DAY + 2)
#define TT_WEATHER_CURRENT_X      4
#define TT_WEATHER_CURRENT_Y      8
#define TT_WEATHER_TEMP_Y         (TT_WEATHER_CURRENT_Y + \
                                   (TT_WEATHER_ICON_CURRENT - TT_WEATHER_TEXT_STACK_H) / 2)
#define TT_WEATHER_TEMP_X         (TT_WEATHER_CURRENT_X + TT_WEATHER_ICON_CURRENT + 8)
#define TT_WEATHER_CITY_Y         2
#define TT_WEATHER_DETAIL_N       8
#define TT_WEATHER_DETAIL_WIND    2
#define TT_WEATHER_DETAIL_UVI     4
#define TT_WEATHER_DETAIL_AQI     6
#define TT_WEATHER_DETAIL_Y       122
#define TT_WEATHER_DETAIL_ROW_H   40
#define TT_WEATHER_DETAIL_COL_W   84
#define TT_WEATHER_DETAIL_PAD_X   2
#define TT_WEATHER_DETAIL_DIV_N   3
#define TT_WEATHER_DETAIL_DIV_X   TT_WEATHER_DETAIL_PAD_X
#define TT_WEATHER_DETAIL_DIV_W   (TT_WEATHER_GRAPH_X - TT_WEATHER_DETAIL_DIV_X - 2)
#define TT_WEATHER_DETAIL_DIV_H   (TT_WEATHER_DETAIL_ROW_H / 2 + 4)
#define TT_WEATHER_DETAIL_DIV_Y(i) \
    (TT_WEATHER_DETAIL_Y + ((i) + 1) * TT_WEATHER_DETAIL_ROW_H - \
     TT_WEATHER_DETAIL_DIV_H + 1)
#define TT_WEATHER_DETAIL_LINE_H  13
#define TT_WEATHER_DETAIL_TEXT_H  (TT_WEATHER_DETAIL_LINE_H * 2)
#define TT_WEATHER_DETAIL_TEXT_GAP 2
#define TT_WEATHER_DETAIL_VALUE_Y (TT_WEATHER_DETAIL_Y + (TT_WEATHER_DETAIL_ROW_H - TT_WEATHER_DETAIL_TEXT_H) / 2)
#define TT_WEATHER_DETAIL_ICON_Y  (TT_WEATHER_DETAIL_Y + \
    (TT_WEATHER_DETAIL_ROW_H - TT_WEATHER_ICON_CELL) / 2)
#define TT_WEATHER_GRAPH_ICON_Y   (TT_WEATHER_DETAIL_ICON_Y + 4)
#define TT_WEATHER_GRAPH_DASH     3
#define TT_WEATHER_GRAPH_DASH_GAP 3
#define TT_WEATHER_GRAPH_DOT_GAP  2
#define TT_WEATHER_GRAPH_DOT_PITCH (TT_WEATHER_GRAPH_DOT_GAP + 1)
#define TT_WEATHER_LEVEL_Y        (-3)
#define TT_WEATHER_GRAPH_X        164
#define TT_WEATHER_GRAPH_Y        TT_WEATHER_DETAIL_VALUE_Y
#define TT_WEATHER_GRAPH_W        236
#define TT_WEATHER_GRAPH_H        153
#define TT_WEATHER_GRAPH_PAD_L    22
#define TT_WEATHER_GRAPH_PAD_R    20
#define TT_WEATHER_GRAPH_PAD_T    32
#define TT_WEATHER_GRAPH_PAD_B    16
#define TT_WEATHER_CITY_X         TT_WEATHER_GRAPH_X
#define TT_WEATHER_CITY_W         TT_WEATHER_GRAPH_W
#define TT_WEATHER_AGE_TEXT_W     72
#define TT_WEATHER_AGE_ICON       8
#define TT_WEATHER_AGE_ICON_GAP   2
#define TT_WEATHER_AGE_TEXT_X     TT_WEATHER_TEMP_X
#define TT_WEATHER_AGE_X          (TT_WEATHER_AGE_TEXT_X - TT_WEATHER_AGE_ICON - \
                                   TT_WEATHER_AGE_ICON_GAP)
#define TT_WEATHER_AGE_Y          (TT_WEATHER_TEMP_Y + TT_WEATHER_TEXT_STACK_H + \
                                   TT_WEATHER_TEXT_STACK_GAP)
#define TT_WEATHER_AGE_ICON_Y     (TT_WEATHER_AGE_Y + 3)
#define TT_WEATHER_AGE_OK_SRC     "/icons/weather/check_8.i1"
#define TT_WEATHER_AGE_FAIL_SRC   "/icons/weather/cross_8.i1"
#define TT_WEATHER_AGE_TICK_MS    (60 * 1000)
#define TT_WEATHER_CLOCK_TICK_MS  1000
#define TT_WEATHER_FORECAST_X     TT_WEATHER_GRAPH_X
#define TT_WEATHER_FORECAST_COL_X(i) \
    (TT_WEATHER_FORECAST_X + (i) * TT_WEATHER_GRAPH_W / TT_WEATHER_FORECAST_N)
#define TT_WEATHER_FORECAST_COL_W(i) \
    (TT_WEATHER_FORECAST_COL_X((i) + 1) - TT_WEATHER_FORECAST_COL_X(i))
#define TT_WEATHER_DIV_H          1
#define TT_WEATHER_FORECAST_DIV_GAP 4
#define TT_WEATHER_FORECAST_DIV_X (TT_WEATHER_FORECAST_X - TT_WEATHER_FORECAST_DIV_GAP)
#define TT_WEATHER_FORECAST_DIV_LEFT_X (TT_WEATHER_FORECAST_DIV_X + 2)
#define TT_WEATHER_FORECAST_DIV_BOT (TT_WEATHER_FORECAST_TEMPS_Y + 12 + 2 + 3)
#define TT_WEATHER_FORECAST_DIV_H ((TT_WEATHER_FORECAST_TEMPS_Y + 12 + 2 - \
                                   TT_WEATHER_FORECAST_Y + TT_WEATHER_DIV_H) / 2)
#define TT_WEATHER_FORECAST_DIV_Y (TT_WEATHER_FORECAST_DIV_BOT - TT_WEATHER_FORECAST_DIV_H + \
                                   TT_WEATHER_DIV_H)
#define TT_WEATHER_FORECAST_DIV_W (TT_WEATHER_FORECAST_X + TT_WEATHER_GRAPH_W - \
                                   TT_WEATHER_FORECAST_DIV_LEFT_X)
#define TT_WEATHER_FORECAST_DIV_RADIUS 6
#define TT_WEATHER_FORECAST_DIV_ARC_N  6
#define TT_WEATHER_FORECAST_DIV_DASH 3
#define TT_WEATHER_FORECAST_DIV_DASH_GAP 3
#define TT_WEATHER_FORECAST_DIV_PI    3.14159265f
#define TT_WEATHER_BTN_W          120
#define TT_WEATHER_BTN_GAP        12
#define TT_WEATHER_BTN_BOTTOM     -10
#define TT_WEATHER_BEAUFORT_MS    0.836f
#define TT_WEATHER_UVI_LOW_MAX    2.0f
#define TT_WEATHER_UVI_MID_MAX    5.0f
#define TT_WEATHER_AQI_GOOD_MAX   50
#define TT_WEATHER_AQI_FAIR_MAX   100
#define TT_WEATHER_AQI_MID_MAX    150
#define TT_WEATHER_PAGE_REFRESH_MS  (30 * 60 * 1000)
#define TT_WEATHER_GRAPH_TEMP_PAD 2.0f
#define TT_WEATHER_GRAPH_TEMP_LABEL_H 12
#define TT_WEATHER_GRAPH_X_TICKS  8
#define TT_WEATHER_GRAPH_TEMP_STEPS  24
#define TT_WEATHER_GRAPH_TEMP_POINTS \
    ((TT_WEATHER_GRAPH_X_TICKS - 1) * TT_WEATHER_GRAPH_TEMP_STEPS + 1)

struct TTWeatherForecastCol {
    lv_obj_t* weekday = nullptr;
    lv_obj_t* icon = nullptr;
    lv_obj_t* temps = nullptr;
};

struct TTWeatherDetailCell {
    lv_obj_t* icon = nullptr;
    lv_obj_t* value = nullptr;
    lv_obj_t* label = nullptr;
};

class TTWeatherPage : public TTScreenPage {
public:
    TTWeatherPage() : TTScreenPage("天气") {}

    void setup() override;
    void willAppear() override;
    void willDisappear() override;
    TTRefreshLevel enterRefreshLevel() const override;

protected:
    void buildContent(lv_obj_t* screen) override;

private:
    bool applyWeather(const TTWeatherPayload& payload);
    void bindOk(const TTWeatherPayload& payload);
    void bindDetails(const TTWeatherPayload& payload);
    void bindGraph(const TTWeatherPayload& payload);
    void forceRefresh();
    void requestFetch(bool force);
    void onWifiStatus(const TTWiFiStatusPayload& status);
    static bool fetchBusy();
    void setMessage(const char* text);
    void showContent(bool show);
    void showEmpty(bool show);
    void showEmptyActions(bool showWeb, bool showRetry);
    void goWebSettings();
    void goBack();
    static void onWebSettingsEvent(lv_event_t* e);
    static void onRetryEvent(lv_event_t* e);
    static void onBackEvent(lv_event_t* e);
    static void onGraphDraw(lv_event_t* e);
    void formatLocalHm(int64_t unixTime, char* out, size_t outMax);
    void formatWeekday(int64_t unixTime, char* out, size_t outMax);
    void formatDate(char* out, size_t outMax);
    void bindAge(uint32_t fetchedAtMs);
    void updateAge(bool refreshIfChanged);
    void updateClock(bool refreshIfChanged);

    lv_obj_t* _content = nullptr;
    lv_obj_t* _empty = nullptr;
    lv_obj_t* _message = nullptr;
    lv_obj_t* _btnRow = nullptr;
    lv_obj_t* _retryBtn = nullptr;
    lv_obj_t* _webBtn = nullptr;
    lv_obj_t* _backBtn = nullptr;
    lv_obj_t* _ageIcon = nullptr;
    lv_obj_t* _ageLabel = nullptr;
    lv_obj_t* _cityLabel = nullptr;
    lv_obj_t* _currentIcon = nullptr;
    lv_obj_t* _tempLabel = nullptr;
    lv_obj_t* _tempUnit = nullptr;
    lv_obj_t* _feelsLabel = nullptr;
    lv_obj_t* _condLabel = nullptr;
    lv_obj_t* _uviLevel = nullptr;
    lv_obj_t* _aqiLevel = nullptr;
    lv_obj_t* _windLevel = nullptr;
    lv_obj_t* _graph = nullptr;
    lv_obj_t* _tempLine = nullptr;
    lv_obj_t* _hourIcons[TT_WEATHER_GRAPH_X_TICKS] = {};
    lv_font_t* _graphFont = nullptr;
    uint8_t _humidH[TT_WEATHER_HOURS] = {};
    lv_point_precise_t _tempPoints[TT_WEATHER_GRAPH_TEMP_POINTS] = {};
    float _graphTMin = 0;
    float _graphTMax = 0;
    float _graphTStart = 0;
    float _graphTHi = 0;
    float _graphTLo = 0;
    float _graphHHi = 0;
    float _graphHLo = 0;
    int64_t _graphTime0 = 0;
    TTWeatherForecastCol _forecast[TT_WEATHER_FORECAST_N];
    TTWeatherDetailCell _details[TT_WEATHER_DETAIL_N];
    bool _visible = false;
    bool _forceRefreshing = false;
    bool _waitingWifi = false;
    bool _ageOk = true;
    uint32_t _fetchedAtMs = 0;
    uint32_t _refreshHandle = 0;
    uint32_t _ageHandle = 0;
    uint32_t _clockHandle = 0;
    int _lastClockMinute = -1;
    char _cityName[TT_WEATHER_CITY_MAX + 1] = {};
};
