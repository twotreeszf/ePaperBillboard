#include "TTWeatherPage.h"
#include "TTWiFiConfigPage.h"
#include "../Base/Logger.h"
#include "../Base/TTFontManager.h"
#include "../Base/TTStreamImage.h"
#include "../Base/TTInstance.h"
#include "../Base/TTRtc.h"
#include "../Base/TTTextButton.h"
#include "../Base/TTNavigationBar.h"
#include <Arduino.h>
#include <EPDConfig.h>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <cmath>
#include <memory>

static float windSpeedToBeaufort(float ms) {
    if (ms < 0.0f) {
        ms = 0.0f;
    }
    return powf(ms / TT_WEATHER_BEAUFORT_MS, 2.0f / 3.0f);
}

static const char* uviLevelText(float uvi) {
    if (uvi <= TT_WEATHER_UVI_LOW_MAX) {
        return "低";
    }
    if (uvi <= TT_WEATHER_UVI_MID_MAX) {
        return "中";
    }
    return "高";
}

static const char* aqiLevelText(int aqi) {
    if (aqi <= TT_WEATHER_AQI_GOOD_MAX) {
        return "优";
    }
    if (aqi <= TT_WEATHER_AQI_FAIR_MAX) {
        return "良";
    }
    if (aqi <= TT_WEATHER_AQI_MID_MAX) {
        return "中";
    }
    return "差";
}

static void alignLevelLabel(lv_obj_t* level, lv_obj_t* value) {
    if (level == nullptr || value == nullptr) {
        return;
    }
    lv_obj_align_to(level, value, LV_ALIGN_OUT_RIGHT_MID, 2, TT_WEATHER_LEVEL_Y);
}

static lv_obj_t* createPlainLabel(lv_obj_t* parent, lv_font_t* font, const char* text) {
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_black(), 0);
    lv_obj_set_style_text_font(label, font, 0);
    return label;
}

static void drawForecastDivArc(lv_layer_t* layer, lv_draw_line_dsc_t* dsc,
                              int cx, int cy, float startDeg, float endDeg, int r) {
    const float span = endDeg - startDeg;
    float prevRad = startDeg * TT_WEATHER_FORECAST_DIV_PI / 180.0f;
    float prevX = (float)cx + cosf(prevRad) * (float)r;
    float prevY = (float)cy + sinf(prevRad) * (float)r;
    for (int i = 1; i <= TT_WEATHER_FORECAST_DIV_ARC_N; i++) {
        const float deg = startDeg + span * (float)i / (float)TT_WEATHER_FORECAST_DIV_ARC_N;
        const float rad = deg * TT_WEATHER_FORECAST_DIV_PI / 180.0f;
        const float x = (float)cx + cosf(rad) * (float)r;
        const float y = (float)cy + sinf(rad) * (float)r;
        dsc->p1.x = (lv_value_precise_t)(prevX + 0.5f);
        dsc->p1.y = (lv_value_precise_t)(prevY + 0.5f);
        dsc->p2.x = (lv_value_precise_t)(x + 0.5f);
        dsc->p2.y = (lv_value_precise_t)(y + 0.5f);
        lv_draw_line(layer, dsc);
        prevX = x;
        prevY = y;
    }
}

static void drawLDiv(lv_event_t* e, bool roundRight) {
    lv_layer_t* layer = lv_event_get_layer(e);
    lv_obj_t* obj = (lv_obj_t*)lv_event_get_current_target(e);
    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);

    const int x1 = coords.x1;
    const int y1 = coords.y1;
    const int x2 = coords.x2;
    const int y2 = coords.y2;
    const int r = TT_WEATHER_FORECAST_DIV_RADIUS;
    const int dashEnd = y2 - r;
    const int cy = y2 - r;

    lv_draw_line_dsc_t lineDsc;
    lv_draw_line_dsc_init(&lineDsc);
    lineDsc.color = lv_color_black();
    lineDsc.width = 1;

    for (int y = y1; y <= dashEnd; y += TT_WEATHER_FORECAST_DIV_DASH + TT_WEATHER_FORECAST_DIV_DASH_GAP) {
        int yEnd = y + TT_WEATHER_FORECAST_DIV_DASH - 1;
        if (yEnd > dashEnd) {
            yEnd = dashEnd;
        }
        lineDsc.p1.x = (lv_value_precise_t)x1;
        lineDsc.p2.x = (lv_value_precise_t)x1;
        lineDsc.p1.y = (lv_value_precise_t)y;
        lineDsc.p2.y = (lv_value_precise_t)yEnd;
        lv_draw_line(layer, &lineDsc);
    }

    drawForecastDivArc(layer, &lineDsc, x1 + r, cy, 180.0f, 90.0f, r);
    lineDsc.p1.y = (lv_value_precise_t)y2;
    lineDsc.p2.y = (lv_value_precise_t)y2;
    if (roundRight) {
        lineDsc.p1.x = (lv_value_precise_t)(x1 + r);
        lineDsc.p2.x = (lv_value_precise_t)(x2 - r);
        lv_draw_line(layer, &lineDsc);
        drawForecastDivArc(layer, &lineDsc, x2 - r, cy, 90.0f, 0.0f, r);
    } else {
        lineDsc.p1.x = (lv_value_precise_t)(x1 + r);
        lineDsc.p2.x = (lv_value_precise_t)x2;
        lv_draw_line(layer, &lineDsc);
    }
}

static void drawForecastDiv(lv_event_t* e) {
    drawLDiv(e, true);
}

static void drawDetailDiv(lv_event_t* e) {
    drawLDiv(e, false);
}

static int graphTickHour(int tick) {
    return tick * (TT_WEATHER_HOURS - 1) / (TT_WEATHER_GRAPH_X_TICKS - 1);
}

static float sampleTempAt(const float* samples, int count, int index) {
    if (index < 0) {
        return 2.0f * samples[0] - samples[1];
    }
    if (index >= count) {
        return 2.0f * samples[count - 1] - samples[count - 2];
    }
    return samples[index];
}

static int tempToPlotY(float temp, float tMin, float tMax, int plotY1, int plotH) {
    float span = tMax - tMin;
    if (span < 0.001f) {
        span = 0.001f;
    }
    float ratio = (temp - tMin) / span;
    if (ratio < 0.0f) {
        ratio = 0.0f;
    }
    if (ratio > 1.0f) {
        ratio = 1.0f;
    }
    return plotY1 + (int)((1.0f - ratio) * (float)plotH + 0.5f);
}

static float catmullRom(float p0, float p1, float p2, float p3, float t) {
    const float t2 = t * t;
    const float t3 = t2 * t;
    return 0.5f * ((2.0f * p1) + (-p0 + p2) * t +
                   (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
                   (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
}

void TTWeatherPage::buildContent(lv_obj_t* screen) {
    TTFontManager& fm = TTFontManager::instance();
    lv_font_t* font10 = fm.getFont(10);
    lv_font_t* font12 = fm.getFont(12);
    lv_font_t* font16 = fm.getFont(16);
    lv_font_t* font48 = fm.getFont(TT_WEATHER_TEMP_FONT);
    _graphFont = font10;

    lv_obj_set_style_bg_color(screen, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    _empty = lv_obj_create(screen);
    lv_obj_set_size(_empty, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(_empty, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(_empty, 0, 0);
    lv_obj_set_style_pad_all(_empty, 0, 0);
    lv_obj_set_style_radius(_empty, 0, 0);
    lv_obj_align(_empty, LV_ALIGN_CENTER, 0, 0);
    lv_obj_remove_flag(_empty, LV_OBJ_FLAG_SCROLLABLE);

    _message = createPlainLabel(_empty, font16, "");
    lv_obj_set_width(_message, EPD_WIDTH - 16);
    lv_obj_set_style_text_align(_message, LV_TEXT_ALIGN_CENTER, 0);

    _btnRow = lv_obj_create(screen);
    lv_obj_set_size(_btnRow, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(_btnRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(_btnRow, 0, 0);
    lv_obj_set_style_pad_all(_btnRow, 0, 0);
    lv_obj_set_style_radius(_btnRow, 0, 0);
    lv_obj_set_layout(_btnRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(_btnRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(_btnRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(_btnRow, TT_WEATHER_BTN_GAP, 0);
    lv_obj_align(_btnRow, LV_ALIGN_BOTTOM_MID, 0, TT_WEATHER_BTN_BOTTOM);
    lv_obj_remove_flag(_btnRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(_btnRow, LV_OBJ_FLAG_HIDDEN);

    _retryBtn = TTTextButton::create(_btnRow, "重试", font16, TT_WEATHER_BTN_W);
    lv_obj_add_event_cb(_retryBtn, onRetryEvent, LV_EVENT_CLICKED, this);
    addToFocusGroup(_retryBtn);
    lv_obj_add_flag(_retryBtn, LV_OBJ_FLAG_HIDDEN);

    _webBtn = TTTextButton::create(_btnRow, "Web 设置", font16, TT_WEATHER_BTN_W);
    lv_obj_add_event_cb(_webBtn, onWebSettingsEvent, LV_EVENT_CLICKED, this);
    addToFocusGroup(_webBtn);

    _backBtn = TTTextButton::create(_btnRow, "返回", font16, TT_WEATHER_BTN_W);
    lv_obj_add_event_cb(_backBtn, onBackEvent, LV_EVENT_CLICKED, this);
    addToFocusGroup(_backBtn);

    _content = lv_obj_create(screen);
    lv_obj_set_pos(_content, 0, 0);
    lv_obj_set_size(_content, EPD_WIDTH, EPD_HEIGHT - TT_NAV_PAGE_INSET);
    lv_obj_set_style_bg_opa(_content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(_content, 0, 0);
    lv_obj_set_style_pad_all(_content, 0, 0);
    lv_obj_set_style_radius(_content, 0, 0);
    lv_obj_remove_flag(_content, LV_OBJ_FLAG_SCROLLABLE);

    _currentIcon = tt_stream_image_create(_content);
    lv_obj_set_pos(_currentIcon, TT_WEATHER_CURRENT_X, TT_WEATHER_CURRENT_Y);
    lv_obj_set_size(_currentIcon, TT_WEATHER_ICON_CURRENT, TT_WEATHER_ICON_CURRENT);

    _tempLabel = createPlainLabel(_content, font48, "--");
    lv_obj_set_pos(_tempLabel, TT_WEATHER_TEMP_X, TT_WEATHER_TEMP_Y);

    _tempUnit = lv_obj_create(_content);
    lv_obj_set_size(_tempUnit, TT_WEATHER_TEMP_DOT_SIZE, TT_WEATHER_TEMP_DOT_SIZE);
    lv_obj_set_style_bg_color(_tempUnit, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(_tempUnit, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(_tempUnit, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(_tempUnit, 0, 0);
    lv_obj_set_style_pad_all(_tempUnit, 0, 0);
    lv_obj_remove_flag(_tempUnit, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(_tempUnit, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align_to(_tempUnit, _tempLabel, LV_ALIGN_OUT_RIGHT_TOP,
                    TT_WEATHER_TEMP_DOT_GAP_X, TT_WEATHER_TEMP_DOT_GAP_Y);

    _feelsLabel = createPlainLabel(_content, font12, "");
    lv_obj_align_to(_feelsLabel, _tempLabel, LV_ALIGN_OUT_BOTTOM_LEFT, 0, TT_WEATHER_FEELS_GAP);

    _condLabel = createPlainLabel(_content, font16, "");
    lv_obj_set_style_text_color(_condLabel, lv_color_white(), 0);
    lv_obj_set_style_bg_color(_condLabel, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(_condLabel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(_condLabel, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_hor(_condLabel, TT_WEATHER_COND_PAD_X, 0);
    lv_obj_set_style_pad_ver(_condLabel, TT_WEATHER_COND_PAD_Y, 0);
    lv_label_set_long_mode(_condLabel, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_max_width(_condLabel, TT_WEATHER_CITY_X - TT_WEATHER_TEMP_X - 4, 0);
    lv_obj_align_to(_condLabel, _feelsLabel, LV_ALIGN_OUT_BOTTOM_LEFT, 0, TT_WEATHER_TEXT_STACK_GAP);

    _ageIcon = tt_stream_image_create(_content);
    lv_obj_set_size(_ageIcon, TT_WEATHER_AGE_ICON, TT_WEATHER_AGE_ICON);
    lv_obj_set_pos(_ageIcon, TT_WEATHER_AGE_X, TT_WEATHER_AGE_ICON_Y);
    tt_stream_image_set_src(_ageIcon, TT_WEATHER_AGE_OK_SRC);

    _ageLabel = createPlainLabel(_content, font10, "--");
    lv_obj_set_width(_ageLabel, TT_WEATHER_AGE_TEXT_W);
    lv_label_set_long_mode(_ageLabel, LV_LABEL_LONG_CLIP);
    lv_obj_set_pos(_ageLabel, TT_WEATHER_AGE_TEXT_X, TT_WEATHER_AGE_Y);

    _cityLabel = createPlainLabel(_content, font16, "");
    lv_obj_set_style_text_color(_cityLabel, lv_color_white(), 0);
    lv_obj_set_style_bg_color(_cityLabel, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(_cityLabel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(_cityLabel, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_hor(_cityLabel, TT_WEATHER_COND_PAD_X, 0);
    lv_obj_set_style_pad_ver(_cityLabel, TT_WEATHER_COND_PAD_Y, 0);
    lv_label_set_long_mode(_cityLabel, LV_LABEL_LONG_DOT);
    lv_obj_set_style_max_width(_cityLabel, TT_WEATHER_CITY_W, 0);
    lv_obj_align(_cityLabel, LV_ALIGN_TOP_RIGHT, 0, TT_WEATHER_CITY_Y);

    for (int i = 0; i < TT_WEATHER_FORECAST_N; i++) {
        const int x = TT_WEATHER_FORECAST_COL_X(i);
        const int colW = TT_WEATHER_FORECAST_COL_W(i);
        _forecast[i].weekday = createPlainLabel(_content, font10, "");
        lv_obj_set_width(_forecast[i].weekday, colW);
        lv_obj_set_style_text_align(_forecast[i].weekday, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(_forecast[i].weekday, x, TT_WEATHER_FORECAST_Y);

        _forecast[i].icon = tt_stream_image_create(_content);
        lv_obj_set_size(_forecast[i].icon, TT_WEATHER_ICON_DAY, TT_WEATHER_ICON_DAY);
        lv_obj_set_pos(_forecast[i].icon, x + (colW - TT_WEATHER_ICON_DAY) / 2,
                       TT_WEATHER_FORECAST_ICON_Y);

        _forecast[i].temps = createPlainLabel(_content, font10, "");
        lv_obj_set_width(_forecast[i].temps, colW);
        lv_obj_set_style_text_align(_forecast[i].temps, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(_forecast[i].temps, x, TT_WEATHER_FORECAST_TEMPS_Y);
    }
    lv_obj_t* forecastDiv = lv_obj_create(_content);
    lv_obj_set_pos(forecastDiv, TT_WEATHER_FORECAST_DIV_LEFT_X, TT_WEATHER_FORECAST_DIV_Y);
    lv_obj_set_size(forecastDiv, TT_WEATHER_FORECAST_DIV_W, TT_WEATHER_FORECAST_DIV_H);
    lv_obj_set_style_bg_opa(forecastDiv, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(forecastDiv, 0, 0);
    lv_obj_set_style_pad_all(forecastDiv, 0, 0);
    lv_obj_set_style_radius(forecastDiv, 0, 0);
    lv_obj_remove_flag(forecastDiv, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(forecastDiv, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(forecastDiv, drawForecastDiv, LV_EVENT_DRAW_MAIN, nullptr);

    static const char* kDetailLabels[] = {
        "日出", "日落", "风", "湿度", "紫外线",
        "气压", "空气质量", "能见度"
    };
    static const char* kDetailIcons[] = {
        "/icons/weather/wi_sunrise_40.i1",
        "/icons/weather/wi_sunset_40.i1",
        "/icons/weather/wi_strong_wind_40.i1",
        "/icons/weather/wi_humidity_40.i1",
        "/icons/weather/wi_hot_40.i1",
        "/icons/weather/wi_barometer_40.i1",
        "/icons/weather/air_filter_40.i1",
        "/icons/weather/visibility_icon_40.i1"
    };
    for (int i = 0; i < TT_WEATHER_DETAIL_N; i++) {
        const int col = i % 2;
        const int row = i / 2;
        const int x = col * TT_WEATHER_DETAIL_COL_W + TT_WEATHER_DETAIL_PAD_X;
        const int y = TT_WEATHER_DETAIL_Y + row * TT_WEATHER_DETAIL_ROW_H;
        const int iconY = y + (TT_WEATHER_DETAIL_ROW_H - TT_WEATHER_ICON_CELL) / 2;
        const int textX = x + TT_WEATHER_ICON_CELL + TT_WEATHER_DETAIL_TEXT_GAP;
        const int textW = TT_WEATHER_DETAIL_COL_W - TT_WEATHER_ICON_CELL - TT_WEATHER_DETAIL_TEXT_GAP - 2;
        const int textY = y + (TT_WEATHER_DETAIL_ROW_H - TT_WEATHER_DETAIL_TEXT_H) / 2;

        _details[i].icon = tt_stream_image_create(_content);
        tt_stream_image_set_src(_details[i].icon, kDetailIcons[i]);
        lv_obj_set_size(_details[i].icon, TT_WEATHER_ICON_CELL, TT_WEATHER_ICON_CELL);
        lv_obj_set_pos(_details[i].icon, x, iconY);

        _details[i].value = createPlainLabel(_content, font12, "--");
        if (i == TT_WEATHER_DETAIL_UVI || i == TT_WEATHER_DETAIL_AQI
            || i == TT_WEATHER_DETAIL_WIND) {
            lv_obj_set_pos(_details[i].value, textX, textY);
            lv_obj_t* level = createPlainLabel(_content, font10, "");
            alignLevelLabel(level, _details[i].value);
            if (i == TT_WEATHER_DETAIL_UVI) {
                _uviLevel = level;
            } else if (i == TT_WEATHER_DETAIL_AQI) {
                _aqiLevel = level;
            } else {
                _windLevel = level;
            }
        } else {
            lv_obj_set_width(_details[i].value, textW);
            lv_label_set_long_mode(_details[i].value, LV_LABEL_LONG_DOT);
            lv_obj_set_pos(_details[i].value, textX, textY);
        }

        _details[i].label = createPlainLabel(_content, font10, kDetailLabels[i]);
        lv_obj_set_width(_details[i].label, textW);
        lv_label_set_long_mode(_details[i].label, LV_LABEL_LONG_DOT);
        lv_obj_set_pos(_details[i].label, textX, textY + TT_WEATHER_DETAIL_LINE_H);
    }
    for (int i = 0; i < TT_WEATHER_DETAIL_DIV_N; i++) {
        lv_obj_t* detailDiv = lv_obj_create(_content);
        lv_obj_set_pos(detailDiv, TT_WEATHER_DETAIL_DIV_X, TT_WEATHER_DETAIL_DIV_Y(i));
        lv_obj_set_size(detailDiv, TT_WEATHER_DETAIL_DIV_W, TT_WEATHER_DETAIL_DIV_H);
        lv_obj_set_style_bg_opa(detailDiv, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(detailDiv, 0, 0);
        lv_obj_set_style_pad_all(detailDiv, 0, 0);
        lv_obj_set_style_radius(detailDiv, 0, 0);
        lv_obj_remove_flag(detailDiv, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(detailDiv, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(detailDiv, drawDetailDiv, LV_EVENT_DRAW_MAIN, nullptr);
    }

    _graph = lv_obj_create(_content);
    lv_obj_set_pos(_graph, TT_WEATHER_GRAPH_X, TT_WEATHER_GRAPH_Y);
    lv_obj_set_size(_graph, TT_WEATHER_GRAPH_W, TT_WEATHER_GRAPH_H);
    lv_obj_set_style_bg_opa(_graph, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(_graph, 0, 0);
    lv_obj_set_style_pad_all(_graph, 0, 0);
    lv_obj_set_style_radius(_graph, 0, 0);
    lv_obj_remove_flag(_graph, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(_graph, onGraphDraw, LV_EVENT_DRAW_MAIN, this);

    _tempLine = lv_line_create(_graph);
    lv_obj_set_pos(_tempLine, 0, 0);
    lv_obj_set_size(_tempLine, TT_WEATHER_GRAPH_W, TT_WEATHER_GRAPH_H);
    lv_obj_set_style_pad_all(_tempLine, 0, 0);
    lv_obj_set_style_line_width(_tempLine, 1, 0);
    lv_obj_set_style_line_color(_tempLine, lv_color_black(), 0);
    lv_obj_set_style_line_rounded(_tempLine, true, 0);

    for (int i = 0; i < TT_WEATHER_GRAPH_X_TICKS; i++) {
        _hourIcons[i] = tt_stream_image_create(_content);
        lv_obj_set_size(_hourIcons[i], TT_WEATHER_ICON_HOUR, TT_WEATHER_ICON_HOUR);
    }

    showContent(false);
    showEmpty(true);
    showEmptyActions(false, false);
    setMessage("正在获取天气");
    LOG_I("Weather page: built heap=%u", (unsigned)ESP.getFreeHeap());
}

void TTWeatherPage::setup() {
    TTScreenPage::setup();
    subscribe<TTWeatherPayload>(
        TT_NOTIFICATION_WEATHER,
        [this](const TTWeatherPayload& payload) {
            const bool hadContent = _content != nullptr
                && !lv_obj_has_flag(_content, LV_OBJ_FLAG_HIDDEN);
            if (!applyWeather(payload)) {
                return;
            }
            if (payload.state == TT_WEATHER_OK && !hadContent) {
                requestRefresh(TT_REFRESH_DEEP);
            } else {
                requestRefresh(TT_REFRESH_PARTIAL);
            }
        });
    registerKeyAction(TT_KEY_CENTER, TT_KEY_LONG_PRESS, [this]() {
        forceRefresh();
    });
}

TTRefreshLevel TTWeatherPage::enterRefreshLevel() const {
    if (_content != nullptr && !lv_obj_has_flag(_content, LV_OBJ_FLAG_HIDDEN)) {
        return TT_REFRESH_DEEP;
    }
    return TT_REFRESH_FULL;
}

void TTWeatherPage::willAppear() {
    TTScreenPage::willAppear();
    _visible = true;
    requestFetch(false);
    if (_refreshHandle == 0) {
        _refreshHandle = runRepeat(TT_WEATHER_PAGE_REFRESH_MS, [this]() {
            requestFetch(false);
        }, false);
    }
    if (_ageHandle == 0) {
        _ageHandle = runRepeat(TT_WEATHER_AGE_TICK_MS, [this]() {
            updateAge(true);
        }, false);
    }
}

void TTWeatherPage::willDisappear() {
    TTScreenPage::willDisappear();
    _visible = false;
    _forceRefreshing = false;
    if (_refreshHandle != 0) {
        cancelRepeat(_refreshHandle);
        _refreshHandle = 0;
    }
    if (_ageHandle != 0) {
        cancelRepeat(_ageHandle);
        _ageHandle = 0;
    }
}

void TTWeatherPage::setMessage(const char* text) {
    if (_message == nullptr) {
        return;
    }
    lv_label_set_text(_message, text != nullptr ? text : "");
}

void TTWeatherPage::showContent(bool show) {
    if (_content == nullptr) {
        return;
    }
    if (show) {
        lv_obj_remove_flag(_content, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(_content, LV_OBJ_FLAG_HIDDEN);
    }
}

void TTWeatherPage::showEmpty(bool show) {
    if (_empty == nullptr) {
        return;
    }
    if (show) {
        lv_obj_remove_flag(_empty, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(_empty, LV_OBJ_FLAG_HIDDEN);
    }
}

void TTWeatherPage::showEmptyActions(bool showWeb, bool showRetry) {
    if (_btnRow == nullptr) {
        return;
    }
    if (_retryBtn != nullptr) {
        if (showRetry) {
            lv_obj_remove_flag(_retryBtn, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(_retryBtn, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (_webBtn != nullptr) {
        if (showWeb) {
            lv_obj_remove_flag(_webBtn, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(_webBtn, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (showWeb || showRetry) {
        lv_obj_remove_flag(_btnRow, LV_OBJ_FLAG_HIDDEN);
        if (_group != nullptr) {
            if (showRetry && _retryBtn != nullptr) {
                lv_group_focus_obj(_retryBtn);
            } else if (showWeb && _webBtn != nullptr) {
                lv_group_focus_obj(_webBtn);
            }
        }
    } else {
        lv_obj_add_flag(_btnRow, LV_OBJ_FLAG_HIDDEN);
    }
}

void TTWeatherPage::goWebSettings() {
    if (getNavigationController() != nullptr) {
        LOG_I("Weather page: open Web settings");
        getNavigationController()->pushPage(std::unique_ptr<TTScreenPage>(new TTWiFiConfigPage()));
    }
}

void TTWeatherPage::goBack() {
    if (getNavigationController() != nullptr) {
        getNavigationController()->pop();
    }
}

void TTWeatherPage::onWebSettingsEvent(lv_event_t* e) {
    TTWeatherPage* self = (TTWeatherPage*)lv_event_get_user_data(e);
    if (self != nullptr) {
        self->goWebSettings();
    }
}

void TTWeatherPage::onRetryEvent(lv_event_t* e) {
    TTWeatherPage* self = (TTWeatherPage*)lv_event_get_user_data(e);
    if (self != nullptr) {
        LOG_I("Weather page: retry");
        self->forceRefresh();
    }
}

void TTWeatherPage::onBackEvent(lv_event_t* e) {
    TTWeatherPage* self = (TTWeatherPage*)lv_event_get_user_data(e);
    if (self != nullptr) {
        self->goBack();
    }
}

void TTWeatherPage::formatLocalHm(int64_t unixTime, char* out, size_t outMax) {
    if (out == nullptr || outMax == 0) {
        return;
    }
    if (unixTime <= 0) {
        strncpy(out, "--:--", outMax - 1);
        out[outMax - 1] = '\0';
        return;
    }
    time_t t = (time_t)unixTime;
    struct tm local = {};
    localtime_r(&t, &local);
    snprintf(out, outMax, "%02d:%02d", local.tm_hour, local.tm_min);
}

void TTWeatherPage::formatWeekday(int64_t unixTime, char* out, size_t outMax) {
    static const char* kDays[] = { "周日", "周一", "周二", "周三", "周四", "周五", "周六" };
    if (out == nullptr || outMax == 0) {
        return;
    }
    if (unixTime <= 0) {
        strncpy(out, "--", outMax - 1);
        out[outMax - 1] = '\0';
        return;
    }
    time_t t = (time_t)unixTime;
    struct tm local = {};
    localtime_r(&t, &local);
    strncpy(out, kDays[local.tm_wday], outMax - 1);
    out[outMax - 1] = '\0';
}

void TTWeatherPage::formatDate(char* out, size_t outMax) {
    static const char* kDays[] = { "周日", "周一", "周二", "周三", "周四", "周五", "周六" };
    struct tm t;
    if (!TTInstanceOf<TTRtc>().getLocalTime(t)) {
        strncpy(out, "", outMax - 1);
        if (outMax > 0) {
            out[outMax - 1] = '\0';
        }
        return;
    }
    snprintf(out, outMax, "%s %d月%d日", kDays[t.tm_wday], t.tm_mon + 1, t.tm_mday);
}

void TTWeatherPage::bindDetails(const TTWeatherPayload& payload) {
    char buf[24];
    formatLocalHm(payload.current.sunrise, buf, sizeof(buf));
    lv_label_set_text(_details[0].value, buf);
    formatLocalHm(payload.current.sunset, buf, sizeof(buf));
    lv_label_set_text(_details[1].value, buf);

    snprintf(buf, sizeof(buf), "%.1f", windSpeedToBeaufort(payload.current.windSpeed));
    lv_label_set_text(_details[TT_WEATHER_DETAIL_WIND].value, buf);
    if (_windLevel != nullptr) {
        lv_label_set_text(_windLevel, "级");
        alignLevelLabel(_windLevel, _details[TT_WEATHER_DETAIL_WIND].value);
    }
    snprintf(buf, sizeof(buf), "%s风", tt_weather_wind_dir_text(payload.current.windDeg));
    lv_label_set_text(_details[2].label, buf);
    char windPath[TT_WEATHER_ICON_PATH_MAX];
    tt_weather_wind_path(windPath, sizeof(windPath), payload.current.windDeg, TT_WEATHER_ICON_CELL);
    tt_stream_image_set_src(_details[2].icon, windPath);

    snprintf(buf, sizeof(buf), "%.0f%%", payload.current.humidity);
    lv_label_set_text(_details[3].value, buf);
    snprintf(buf, sizeof(buf), "%.1f", payload.current.uvi);
    lv_label_set_text(_details[TT_WEATHER_DETAIL_UVI].value, buf);
    if (_uviLevel != nullptr) {
        lv_label_set_text(_uviLevel, uviLevelText(payload.current.uvi));
        alignLevelLabel(_uviLevel, _details[TT_WEATHER_DETAIL_UVI].value);
    }
    snprintf(buf, sizeof(buf), "%.0fp", payload.current.pressure);
    lv_label_set_text(_details[5].value, buf);

    if (payload.current.hasAqi) {
        snprintf(buf, sizeof(buf), "%d", payload.current.aqi);
        lv_label_set_text(_details[TT_WEATHER_DETAIL_AQI].value, buf);
        if (_aqiLevel != nullptr) {
            lv_label_set_text(_aqiLevel, aqiLevelText(payload.current.aqi));
            alignLevelLabel(_aqiLevel, _details[TT_WEATHER_DETAIL_AQI].value);
        }
    } else {
        lv_label_set_text(_details[TT_WEATHER_DETAIL_AQI].value, "--");
        if (_aqiLevel != nullptr) {
            lv_label_set_text(_aqiLevel, "");
        }
    }

    snprintf(buf, sizeof(buf), "%.1fkm", payload.current.visibility / 1000.0f);
    lv_label_set_text(_details[7].value, buf);
}

void TTWeatherPage::bindGraph(const TTWeatherPayload& payload) {
    float samples[TT_WEATHER_GRAPH_X_TICKS];
    for (int i = 0; i < TT_WEATHER_GRAPH_X_TICKS; i++) {
        samples[i] = payload.hourly[graphTickHour(i)].temp;
    }

    float tMin = samples[0];
    float tMax = samples[0];
    for (int i = 1; i < TT_WEATHER_GRAPH_X_TICKS; i++) {
        if (samples[i] < tMin) {
            tMin = samples[i];
        }
        if (samples[i] > tMax) {
            tMax = samples[i];
        }
    }
    _graphTStart = samples[0];
    _graphTHi = tMax;
    _graphTLo = tMin;
    if (tMax - tMin < 1.0f) {
        tMax = tMin + 1.0f;
    }
    tMin -= TT_WEATHER_GRAPH_TEMP_PAD;
    tMax += TT_WEATHER_GRAPH_TEMP_PAD;
    _graphTMin = tMin;
    _graphTMax = tMax;
    _graphTime0 = payload.hourly[0].time;

    const int plotW = TT_WEATHER_GRAPH_W - TT_WEATHER_GRAPH_PAD_L - TT_WEATHER_GRAPH_PAD_R;
    const int plotH = TT_WEATHER_GRAPH_H - TT_WEATHER_GRAPH_PAD_T - TT_WEATHER_GRAPH_PAD_B;
    _graphHHi = 0.0f;
    _graphHLo = 100.0f;
    for (int i = 0; i < TT_WEATHER_HOURS; i++) {
        float hum = payload.hourly[i].humidity;
        if (hum < 0.0f) {
            hum = 0.0f;
        }
        if (hum > 100.0f) {
            hum = 100.0f;
        }
        if (hum > _graphHHi) {
            _graphHHi = hum;
        }
        if (hum < _graphHLo) {
            _graphHLo = hum;
        }
        int barH = (int)(hum * (float)plotH / 100.0f);
        if (barH > plotH) {
            barH = plotH;
        }
        _humidH[i] = (uint8_t)(barH > 0 ? barH : 0);
    }

    int pointN = 0;
    const float yTop = (float)TT_WEATHER_GRAPH_PAD_T;
    const float yBot = (float)(TT_WEATHER_GRAPH_PAD_T + plotH);
    for (int i = 0; i < TT_WEATHER_GRAPH_X_TICKS - 1; i++) {
        const float p0 = sampleTempAt(samples, TT_WEATHER_GRAPH_X_TICKS, i - 1);
        const float p1 = samples[i];
        const float p2 = samples[i + 1];
        const float p3 = sampleTempAt(samples, TT_WEATHER_GRAPH_X_TICKS, i + 2);
        const int last = (i == TT_WEATHER_GRAPH_X_TICKS - 2)
            ? TT_WEATHER_GRAPH_TEMP_STEPS
            : TT_WEATHER_GRAPH_TEMP_STEPS - 1;
        for (int s = 0; s <= last; s++) {
            const float t = (float)s / (float)TT_WEATHER_GRAPH_TEMP_STEPS;
            const float temp = catmullRom(p0, p1, p2, p3, t);
            float ratio = (temp - tMin) / (tMax - tMin);
            if (ratio < 0.0f) {
                ratio = 0.0f;
            }
            if (ratio > 1.0f) {
                ratio = 1.0f;
            }
            float y = yTop + (1.0f - ratio) * (float)plotH;
            if (y < yTop) {
                y = yTop;
            }
            if (y > yBot) {
                y = yBot;
            }
            const float x = (float)TT_WEATHER_GRAPH_PAD_L +
                            ((float)i + t) * (float)plotW / (float)(TT_WEATHER_GRAPH_X_TICKS - 1);
            _tempPoints[pointN].x = (lv_value_precise_t)(x + 0.5f);
            _tempPoints[pointN].y = (lv_value_precise_t)(y + 0.5f);
            pointN++;
        }
    }
    if (_graph != nullptr) {
        lv_obj_invalidate(_graph);
    }
    lv_line_set_points(_tempLine, _tempPoints, pointN);

    char iconPath[TT_WEATHER_ICON_PATH_MAX];
    for (int i = 0; i < TT_WEATHER_GRAPH_X_TICKS; i++) {
        if (_hourIcons[i] == nullptr) {
            continue;
        }
        const int hourIndex = graphTickHour(i);
        const int x = TT_WEATHER_GRAPH_PAD_L + i * plotW / (TT_WEATHER_GRAPH_X_TICKS - 1);
        int iconX = x - TT_WEATHER_ICON_HOUR / 2;
        if (iconX < 0) {
            iconX = 0;
        }
        if (iconX + TT_WEATHER_ICON_HOUR > TT_WEATHER_GRAPH_W) {
            iconX = TT_WEATHER_GRAPH_W - TT_WEATHER_ICON_HOUR;
        }
        lv_obj_set_pos(_hourIcons[i], TT_WEATHER_GRAPH_X + iconX, TT_WEATHER_GRAPH_ICON_Y);
        tt_weather_condition_path(iconPath, sizeof(iconPath), payload.hourly[hourIndex].weatherCode,
                                  payload.hourly[hourIndex].isDay, TT_WEATHER_ICON_HOUR);
        tt_stream_image_set_src(_hourIcons[i], iconPath);
    }
}

void TTWeatherPage::onGraphDraw(lv_event_t* e) {
    TTWeatherPage* self = (TTWeatherPage*)lv_event_get_user_data(e);
    if (self == nullptr || self->_graph == nullptr) {
        return;
    }
    lv_layer_t* layer = lv_event_get_layer(e);
    lv_area_t coords;
    lv_obj_get_coords(self->_graph, &coords);

    const int plotX1 = coords.x1 + TT_WEATHER_GRAPH_PAD_L;
    const int plotY1 = coords.y1 + TT_WEATHER_GRAPH_PAD_T;
    const int plotX2 = coords.x2 - TT_WEATHER_GRAPH_PAD_R;
    const int plotY2 = coords.y2 - TT_WEATHER_GRAPH_PAD_B;
    const int plotW = plotX2 - plotX1;
    const int plotH = plotY2 - plotY1;

    lv_draw_line_dsc_t lineDsc;
    lv_draw_line_dsc_init(&lineDsc);
    lineDsc.color = lv_color_black();
    lineDsc.width = 1;

    for (int i = 0; i < 4; i++) {
        const int y = plotY1 + i * plotH / 3;
        lineDsc.p1.y = (lv_value_precise_t)y;
        lineDsc.p2.y = (lv_value_precise_t)y;
        for (int x = plotX1; x < plotX2; x += TT_WEATHER_GRAPH_DASH + TT_WEATHER_GRAPH_DASH_GAP) {
            int x2 = x + TT_WEATHER_GRAPH_DASH - 1;
            if (x2 > plotX2) {
                x2 = plotX2;
            }
            lineDsc.p1.x = (lv_value_precise_t)x;
            lineDsc.p2.x = (lv_value_precise_t)x2;
            lv_draw_line(layer, &lineDsc);
        }
    }

    lv_draw_rect_dsc_t dotDsc;
    lv_draw_rect_dsc_init(&dotDsc);
    dotDsc.bg_color = lv_color_black();
    dotDsc.bg_opa = LV_OPA_COVER;
    dotDsc.border_width = 0;
    dotDsc.radius = 0;
    const int pitch = TT_WEATHER_GRAPH_DOT_PITCH;
    for (int i = 0; i < TT_WEATHER_HOURS - 1; i++) {
        int x0;
        int x1;
        if (TT_WEATHER_HOURS <= 1) {
            x0 = plotX1;
            x1 = plotX2;
        } else {
            x0 = plotX1 + i * plotW / (TT_WEATHER_HOURS - 1);
            x1 = plotX1 + (i + 1) * plotW / (TT_WEATHER_HOURS - 1);
        }
        const int h0 = (int)self->_humidH[i];
        const int h1 = (int)self->_humidH[i + 1];
        if (x1 <= x0) {
            continue;
        }
        int x = x0;
        const int xMod = (x - plotX1) % pitch;
        if (xMod != 0) {
            x += pitch - xMod;
        }
        for (; x < x1; x += pitch) {
            const int h = h0 + (h1 - h0) * (x - x0) / (x1 - x0);
            int y = plotY2 - h + 1;
            const int yMod = (plotY2 - y) % pitch;
            if (yMod != 0) {
                y += yMod;
            }
            for (; y <= plotY2; y += pitch) {
                lv_area_t dot;
                dot.x1 = x;
                dot.y1 = y;
                dot.x2 = x;
                dot.y2 = y;
                lv_draw_rect(layer, &dotDsc, &dot);
            }
        }
    }

    if (self->_graphFont == nullptr) {
        return;
    }
    lv_draw_label_dsc_t textDsc;
    lv_draw_label_dsc_init(&textDsc);
    textDsc.font = self->_graphFont;
    textDsc.color = lv_color_black();
    char buf[12];

    textDsc.align = LV_TEXT_ALIGN_RIGHT;
    const float tempTicks[] = { self->_graphTHi, self->_graphTLo, self->_graphTStart };
    int usedY[3] = {};
    int usedN = 0;
    for (int i = 0; i < 3; i++) {
        const int y = tempToPlotY(tempTicks[i], self->_graphTMin, self->_graphTMax, plotY1, plotH);
        bool overlap = false;
        for (int u = 0; u < usedN; u++) {
            int d = y - usedY[u];
            if (d < 0) {
                d = -d;
            }
            if (d < TT_WEATHER_GRAPH_TEMP_LABEL_H) {
                overlap = true;
                break;
            }
        }
        if (overlap) {
            continue;
        }
        usedY[usedN++] = y;
        snprintf(buf, sizeof(buf), "%.0f°", tempTicks[i]);
        textDsc.text = buf;
        lv_area_t a;
        a.x1 = coords.x1;
        a.x2 = plotX1 - 2;
        a.y1 = y - TT_WEATHER_GRAPH_TEMP_LABEL_H / 2;
        a.y2 = a.y1 + TT_WEATHER_GRAPH_TEMP_LABEL_H;
        lv_draw_label(layer, &textDsc, &a);
    }

    const float humidTicks[] = { self->_graphHHi, self->_graphHLo };
    int humidUsedY[2] = {};
    int humidUsedN = 0;
    for (int i = 0; i < 2; i++) {
        float hum = humidTicks[i];
        if (hum < 0.0f) {
            hum = 0.0f;
        }
        if (hum > 100.0f) {
            hum = 100.0f;
        }
        const int y = plotY1 + (int)((1.0f - hum / 100.0f) * (float)plotH + 0.5f);
        bool overlap = false;
        for (int u = 0; u < humidUsedN; u++) {
            int d = y - humidUsedY[u];
            if (d < 0) {
                d = -d;
            }
            if (d < TT_WEATHER_GRAPH_TEMP_LABEL_H * 2) {
                overlap = true;
                break;
            }
        }
        if (overlap) {
            continue;
        }
        humidUsedY[humidUsedN++] = y;
        snprintf(buf, sizeof(buf), "%.0f", hum);
        textDsc.text = buf;
        lv_area_t a;
        a.x1 = plotX2 + 2;
        a.x2 = coords.x2;
        a.y1 = y - TT_WEATHER_GRAPH_TEMP_LABEL_H;
        a.y2 = y;
        if (a.y1 < coords.y1) {
            a.y1 = coords.y1;
            a.y2 = a.y1 + TT_WEATHER_GRAPH_TEMP_LABEL_H;
        }
        lv_draw_label(layer, &textDsc, &a);

        a.y1 = a.y2;
        a.y2 = a.y1 + TT_WEATHER_GRAPH_TEMP_LABEL_H;
        if (a.y2 > coords.y2) {
            a.y2 = coords.y2;
            a.y1 = a.y2 - TT_WEATHER_GRAPH_TEMP_LABEL_H;
        }
        textDsc.text = "%";
        lv_draw_label(layer, &textDsc, &a);
    }

    textDsc.align = LV_TEXT_ALIGN_CENTER;
    for (int i = 0; i < TT_WEATHER_GRAPH_X_TICKS; i++) {
        const int hourIndex = graphTickHour(i);
        const int64_t ts = self->_graphTime0 + (int64_t)hourIndex * 3600;
        time_t t = (time_t)ts;
        struct tm local = {};
        if (ts > 0) {
            localtime_r(&t, &local);
            snprintf(buf, sizeof(buf), "%02d", local.tm_hour);
        } else {
            buf[0] = '\0';
        }
        textDsc.text = buf;
        const int x = plotX1 + i * plotW / (TT_WEATHER_GRAPH_X_TICKS - 1);
        lv_area_t a;
        a.x1 = x - 10;
        a.x2 = x + 10;
        a.y1 = plotY2 + 1;
        a.y2 = coords.y2;
        lv_draw_label(layer, &textDsc, &a);
    }
}

void TTWeatherPage::bindAge(uint32_t fetchedAtMs) {
    _fetchedAtMs = fetchedAtMs != 0 ? fetchedAtMs : millis();
    if (_fetchedAtMs == 0) {
        _fetchedAtMs = 1;
    }
    updateAge(false);
}

void TTWeatherPage::updateAge(bool refreshIfChanged) {
    if (_ageLabel == nullptr || !_visible) {
        return;
    }
    const bool contentHidden = _content == nullptr
        || lv_obj_has_flag(_content, LV_OBJ_FLAG_HIDDEN);
    if (refreshIfChanged && contentHidden) {
        return;
    }
    if (_ageIcon != nullptr) {
        tt_stream_image_set_src(_ageIcon, _ageOk ? TT_WEATHER_AGE_OK_SRC : TT_WEATHER_AGE_FAIL_SRC);
    }
    char buf[24];
    if (_fetchedAtMs == 0) {
        strncpy(buf, "--", sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
    } else {
        const int minutes = (int)((millis() - _fetchedAtMs) / 60000u);
        if (minutes <= 0) {
            strncpy(buf, "刚刚", sizeof(buf) - 1);
            buf[sizeof(buf) - 1] = '\0';
        } else {
            snprintf(buf, sizeof(buf), "%d分钟前", minutes);
        }
    }
    const char* cur = lv_label_get_text(_ageLabel);
    if (cur != nullptr && strcmp(cur, buf) == 0) {
        if (refreshIfChanged) {
            requestRefresh(TT_REFRESH_PARTIAL);
        }
        return;
    }
    lv_label_set_text(_ageLabel, buf);
    LOG_I("Weather page: refresh age=%s ok=%d", buf, _ageOk ? 1 : 0);
    if (refreshIfChanged) {
        requestRefresh(TT_REFRESH_PARTIAL);
    }
}

void TTWeatherPage::bindOk(const TTWeatherPayload& payload) {
    char dateBuf[32];
    formatDate(dateBuf, sizeof(dateBuf));
    char cityLine[64];
    if (payload.city[0] != '\0') {
        snprintf(cityLine, sizeof(cityLine), "%s  %s", payload.city, dateBuf);
    } else {
        snprintf(cityLine, sizeof(cityLine), "%s", dateBuf);
    }
    lv_label_set_text(_cityLabel, cityLine);
    lv_obj_align(_cityLabel, LV_ALIGN_TOP_RIGHT, 0, TT_WEATHER_CITY_Y);
    bindAge(payload.fetchedAtMs);

    char iconPath[TT_WEATHER_ICON_PATH_MAX];
    tt_weather_condition_path(iconPath, sizeof(iconPath), payload.current.weatherCode,
                              payload.current.isDay, TT_WEATHER_ICON_CURRENT);
    tt_stream_image_set_src(_currentIcon, iconPath);

    char buf[32];
    snprintf(buf, sizeof(buf), "%.0f", payload.current.temp);
    lv_label_set_text(_tempLabel, buf);
    if (_tempUnit != nullptr) {
        lv_obj_align_to(_tempUnit, _tempLabel, LV_ALIGN_OUT_RIGHT_TOP,
                        TT_WEATHER_TEMP_DOT_GAP_X, TT_WEATHER_TEMP_DOT_GAP_Y);
    }
    snprintf(buf, sizeof(buf), "体感 %.0f°", payload.current.feelsLike);
    lv_label_set_text(_feelsLabel, buf);
    lv_label_set_text(_condLabel, tt_weather_condition_text(payload.current.weatherCode));
    lv_obj_align_to(_condLabel, _feelsLabel, LV_ALIGN_OUT_BOTTOM_LEFT, 0, TT_WEATHER_TEXT_STACK_GAP);

    for (int i = 0; i < TT_WEATHER_FORECAST_N; i++) {
        const int day = i + TT_WEATHER_FORECAST_OFFSET;
        formatWeekday(payload.daily[day].time, buf, sizeof(buf));
        lv_label_set_text(_forecast[i].weekday, buf);
        tt_weather_condition_path(iconPath, sizeof(iconPath), payload.daily[day].weatherCode, true,
                                  TT_WEATHER_ICON_DAY);
        tt_stream_image_set_src(_forecast[i].icon, iconPath);
        snprintf(buf, sizeof(buf), "%.0f°|%.0f°", payload.daily[day].tempMax, payload.daily[day].tempMin);
        lv_label_set_text(_forecast[i].temps, buf);
    }

    bindDetails(payload);
    bindGraph(payload);
}

void TTWeatherPage::forceRefresh() {
    if (_forceRefreshing || fetchBusy()) {
        LOG_I("Weather page: force refresh ignored (busy)");
        return;
    }
    _forceRefreshing = true;
    showContent(false);
    showEmpty(true);
    showEmptyActions(false, false);
    setMessage("正在刷新天气");
    requestRefresh(TT_REFRESH_PARTIAL);
    LOG_I("Weather page: force refresh");
    requestFetch(true);
}

bool TTWeatherPage::applyWeather(const TTWeatherPayload& payload) {
    if (!_visible) {
        return false;
    }
    if (payload.state == TT_WEATHER_OK) {
        _forceRefreshing = false;
        _ageOk = !payload.refreshFailed;
        if (!payload.refreshFailed) {
            bindOk(payload);
        } else {
            LOG_I("Weather page: silent refresh failed, keep last ok");
            updateAge(false);
        }
        showContent(true);
        showEmpty(false);
        showEmptyActions(false, false);
        return true;
    }
    if (payload.state == TT_WEATHER_FETCHING) {
        return false;
    }
    if (payload.state != TT_WEATHER_FETCHING) {
        _forceRefreshing = false;
    }
    showContent(false);
    showEmpty(true);
    if (_forceRefreshing && payload.state == TT_WEATHER_FETCHING) {
        setMessage("正在刷新天气");
    } else if (payload.message[0] != '\0') {
        setMessage(payload.message);
    } else if (payload.state == TT_WEATHER_NEED_WIFI) {
        setMessage("未连接 Wi-Fi");
    } else if (payload.state == TT_WEATHER_NEED_LOCATION) {
        setMessage("未配置地点");
    } else if (payload.state == TT_WEATHER_FAILED) {
        setMessage("获取天气失败");
    } else {
        setMessage(_forceRefreshing ? "正在刷新天气" : "正在获取天气");
    }
    const bool needSetup = payload.state == TT_WEATHER_NEED_WIFI
        || payload.state == TT_WEATHER_NEED_LOCATION;
    const bool needRetry = payload.state == TT_WEATHER_FAILED;
    showEmptyActions(needSetup, needRetry);
    return true;
}
