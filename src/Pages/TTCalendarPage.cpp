#include "TTCalendarPage.h"
#include "../Base/Logger.h"
#include "../Base/TTCalendarService.h"
#include "../Base/TTFontManager.h"
#include "../Base/TTInstance.h"
#include "../Base/TTNotificationPayloads.h"
#include "../Base/TTRtc.h"
#include "../Base/TTSleepService.h"
#include "../Base/TTStreamImage.h"
#include "../Base/TTNavigationBar.h"
#include "../Base/TTPopupLayer.h"
#include "../Base/TTWeatherService.h"
#include <EPDConfig.h>
#include <WiFi.h>
#include <cstdio>
#include <cstring>

namespace {

const char* kWeekdays[] = { "周日", "周一", "周二", "周三", "周四", "周五", "周六" };
const char* kWeekEn[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };

void sameDay(time_t unixTime, int* year, int* yday, struct tm* out);

void formatEventTime(const TTCalEvent& event, char* text, size_t textLen) {
    if (event.allDay) {
        snprintf(text, textLen, "全天");
        return;
    }
    struct tm local = {};
    sameDay(event.startUnix, nullptr, nullptr, &local);
    snprintf(text, textLen, "%02d:%02d", local.tm_hour, local.tm_min);
}

int eventTitleWidth(int textW) {
    int width = textW - TT_CAL_EVENT_TIME_W - TT_CAL_EVENT_TIME_GAP;
    if (width < 1) {
        width = 1;
    }
    return width;
}

int eventRowBody(const char* text, const lv_font_t* font, int textW) {
    int lineH = TT_CAL_ROW_H;
    if (font != nullptr) {
        lineH = lv_font_get_line_height(font);
    }
    int textH = lineH;
    if (font != nullptr && text != nullptr && text[0] != '\0' && textW > 0) {
        lv_point_t size = {};
        lv_text_get_size(&size, text, font, 0, 0, textW, LV_TEXT_FLAG_NONE);
        if (size.y > textH) {
            textH = size.y;
        }
    }
    int lead = TT_CAL_ROW_H - lineH - TT_CAL_EVENT_LEAD_TRIM;
    if (lead < 0) {
        lead = 0;
    }
    return textH + lead;
}

void formatAge(uint32_t fetchedAt, char* buf, size_t bufLen) {
    if (buf == nullptr || bufLen == 0) {
        return;
    }
    if (fetchedAt == 0) {
        snprintf(buf, bufLen, "--");
        return;
    }
    const time_t now = time(nullptr);
    const int minutes = (now > 0 && (uint32_t)now >= fetchedAt)
        ? (int)(((uint32_t)now - fetchedAt) / 60u)
        : 0;
    if (minutes <= 0) {
        snprintf(buf, bufLen, "刚刚");
    } else {
        snprintf(buf, bufLen, "%d分钟前", minutes);
    }
}

void drawHSpan(lv_layer_t* layer, const lv_draw_rect_dsc_t* dsc, int x1, int x2, int y) {
    if (x2 < x1) {
        return;
    }
    lv_area_t area;
    area.x1 = x1;
    area.x2 = x2;
    area.y1 = y;
    area.y2 = y;
    lv_draw_rect(layer, dsc, &area);
}

void fillDiskAt(lv_layer_t* layer, int cx, int cy, int radius, lv_color_t color) {
    if (layer == nullptr || radius <= 0) {
        return;
    }
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = color;
    dsc.bg_opa = LV_OPA_COVER;
    dsc.border_width = 0;
    dsc.radius = 0;
    const int limit = radius * radius;
    for (int dy = -radius; dy <= radius; dy++) {
        int run = radius + 1;
        for (int dx = -radius; dx <= radius; dx++) {
            if (dx * dx + dy * dy <= limit) {
                if (run > radius) {
                    run = dx;
                }
                continue;
            }
            if (run > radius) {
                continue;
            }
            drawHSpan(layer, &dsc, cx + run, cx + dx - 1, cy + dy);
            run = radius + 1;
        }
        if (run <= radius) {
            drawHSpan(layer, &dsc, cx + run, cx + radius, cy + dy);
        }
    }
}

lv_obj_t* createLabel(lv_obj_t* parent, lv_font_t* font, lv_color_t color, const char* text) {
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_set_style_bg_opa(label, LV_OPA_TRANSP, 0);
    return label;
}

void sameDay(time_t unixTime, int* year, int* yday, struct tm* out) {
    struct tm local = {};
    localtime_r(&unixTime, &local);
    if (year != nullptr) {
        *year = local.tm_year;
    }
    if (yday != nullptr) {
        *yday = local.tm_yday;
    }
    if (out != nullptr) {
        *out = local;
    }
}

}  // namespace

void TTCalendarPage::buildContent(lv_obj_t* screen) {
    TTFontManager& fonts = TTFontManager::instance();
    lv_font_t* font16 = fonts.getFont(TT_CAL_TEXT_FONT);
    lv_font_t* fontTemp = fonts.getFont(TT_CAL_TEMP_FONT);
    lv_font_t* fontClock = fonts.getFont(TT_CAL_TIME_FONT);
    lv_font_t* fontDate = fonts.getFont(TT_CAL_DATE_EN_FONT);
    lv_font_t* fontWeek = fonts.getFont(TT_CAL_WEEK_FONT);
    lv_font_t* fontFeels = fonts.getFont(TT_CAL_FEELS_FONT);
    lv_font_t* fontCond = fonts.getFont(TT_CAL_COND_FONT);
    lv_font_t* font10 = fonts.getFont(TT_CAL_PAGE_FONT);
    if (fontTemp == nullptr) {
        fontTemp = font16;
    }
    if (fontClock == nullptr) {
        fontClock = fontTemp;
    }
    if (fontDate == nullptr) {
        fontDate = fontTemp;
    }
    if (fontWeek == nullptr) {
        fontWeek = fontDate;
    }
    if (fontFeels == nullptr) {
        fontFeels = font16;
    }
    if (fontCond == nullptr) {
        fontCond = fontFeels;
    }
    if (font10 == nullptr) {
        font10 = fontFeels;
    }
    const int height = EPD_HEIGHT - TT_NAV_PAGE_INSET;
    const int listW = EPD_WIDTH - TT_CAL_SIDE_W;

    _side = lv_obj_create(screen);
    lv_obj_set_pos(_side, 0, 0);
    lv_obj_set_size(_side, TT_CAL_SIDE_W, height);
    lv_obj_set_style_bg_color(_side, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(_side, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_side, 0, 0);
    lv_obj_set_style_pad_all(_side, 0, 0);
    lv_obj_set_style_radius(_side, 0, 0);
    lv_obj_remove_flag(_side, LV_OBJ_FLAG_SCROLLABLE);

    _weatherIcon = tt_stream_image_create(_side);
    lv_obj_set_size(_weatherIcon, TT_CAL_ICON, TT_CAL_ICON);
    lv_obj_align(_weatherIcon, LV_ALIGN_TOP_MID, 0, TT_CAL_ICON_Y);

    _tempLabel = createLabel(_side, fontTemp, lv_color_black(), "");
    lv_obj_set_pos(_tempLabel, TT_CAL_TEMP_X, TT_CAL_TEMP_Y);
    lv_obj_add_flag(_tempLabel, LV_OBJ_FLAG_HIDDEN);

    _tempUnit = lv_obj_create(_side);
    lv_obj_set_size(_tempUnit, TT_CAL_DOT_LG, TT_CAL_DOT_LG);
    lv_obj_set_style_bg_opa(_tempUnit, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(_tempUnit, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(_tempUnit, TT_CAL_DOT_BORDER, 0);
    lv_obj_set_style_border_color(_tempUnit, lv_color_black(), 0);
    lv_obj_set_style_border_opa(_tempUnit, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(_tempUnit, 0, 0);
    lv_obj_remove_flag(_tempUnit, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(_tempUnit, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(_tempUnit, LV_OBJ_FLAG_HIDDEN);

    _feelsLabel = createLabel(_side, fontFeels, lv_color_black(), "");
    lv_label_set_long_mode(_feelsLabel, LV_LABEL_LONG_CLIP);

    _condLabel = createLabel(_side, fontCond, lv_color_black(), "");
    lv_label_set_long_mode(_condLabel, LV_LABEL_LONG_CLIP);

    _dateLabel = createLabel(_side, fontDate, lv_color_black(), "--");

    _weekLabel = createLabel(_side, fontWeek, lv_color_black(), "---");
    lv_obj_set_width(_weekLabel, LV_SIZE_CONTENT);

    _clockLabel = createLabel(_side, fontClock, lv_color_black(), "--");
    _clockColon = createLabel(_side, fontClock, lv_color_black(), ":");
    _clockMin = createLabel(_side, fontClock, lv_color_black(), "--");

    _list = lv_obj_create(screen);
    lv_obj_set_pos(_list, TT_CAL_SIDE_W, 0);
    lv_obj_set_size(_list, listW, height);
    lv_obj_set_style_bg_color(_list, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(_list, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_list, 0, 0);
    lv_obj_set_style_pad_all(_list, 0, 0);
    lv_obj_set_style_radius(_list, 0, 0);
    lv_obj_remove_flag(_list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(_list, onListDraw, LV_EVENT_DRAW_MAIN, this);

    const int textW = listW - TT_CAL_TEXT_X - TT_CAL_PAD;
    lv_font_t* fontEvent = fonts.getFont(TT_CAL_EVENT_FONT);
    if (fontEvent == nullptr) {
        fontEvent = font16;
    }
    for (int i = 0; i < TT_CAL_PAGE_SLOTS; i++) {
        _eventTimes[i] = createLabel(_list, fontEvent, lv_color_black(), "");
        lv_obj_set_width(_eventTimes[i], TT_CAL_EVENT_TIME_W);
        lv_obj_set_style_text_align(_eventTimes[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(_eventTimes[i], LV_LABEL_LONG_CLIP);
        lv_obj_add_flag(_eventTimes[i], LV_OBJ_FLAG_HIDDEN);

        _rows[i] = createLabel(_list, font16, lv_color_black(), "");
        lv_obj_set_width(_rows[i], textW);
        lv_label_set_long_mode(_rows[i], LV_LABEL_LONG_CLIP);
        lv_obj_set_pos(_rows[i], TT_CAL_TEXT_X, TT_CAL_PAD + i * TT_CAL_ROW_H);
        lv_obj_add_flag(_rows[i], LV_OBJ_FLAG_HIDDEN);
    }

    _status = createLabel(_list, font16, lv_color_black(), "正在同步日历");
    lv_obj_set_width(_status, listW - 16);
    lv_obj_set_style_text_align(_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(_status, LV_ALIGN_CENTER, 0, 0);

    _pageLabel = createLabel(_list, font10, lv_color_black(), "");
    lv_obj_align(_pageLabel, LV_ALIGN_BOTTOM_RIGHT, -4, -2);
    lv_obj_add_flag(_pageLabel, LV_OBJ_FLAG_HIDDEN);

    _ageIcon = tt_stream_image_create(screen);
    lv_obj_set_size(_ageIcon, TT_CAL_AGE_ICON, TT_CAL_AGE_ICON);
    tt_stream_image_set_src(_ageIcon, TT_CAL_AGE_OK_SRC);
    _ageLabel = createLabel(screen, font10, lv_color_black(), "");
    lv_label_set_long_mode(_ageLabel, LV_LABEL_LONG_CLIP);
    lv_obj_add_flag(_ageLabel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(_ageIcon, LV_OBJ_FLAG_HIDDEN);
    layoutSide();
}

void TTCalendarPage::setup() {
    TTScreenPage::setup();
    subscribe<TTCalendarPayload>(TT_NOTIFICATION_CALENDAR, [this](const TTCalendarPayload& payload) {
        applyCalendar(payload);
    });
    subscribe<TTWeatherPayload>(TT_NOTIFICATION_WEATHER, [this](const TTWeatherPayload& payload) {
        applyWeather(payload);
    });
    subscribe<TTTimeTickPayload>(TT_NOTIFICATION_TIME_TICK, [this](const TTTimeTickPayload&) {
        onTimeTick();
    });
    subscribe<TTSleepWakePayload>(TT_NOTIFICATION_SLEEP_WAKE, [this](const TTSleepWakePayload& wake) {
        onSleepWake(wake);
    });
    registerKeyAction(TT_KEY_CENTER, TT_KEY_LONG_PRESS, [this]() {
        forceRefresh();
    });
}

void TTCalendarPage::willAppear() {
    TTScreenPage::willAppear();
    _visible = true;
    setStatusTimeVisible(false);
    updateClock(false);
    layoutSide();
    requestCalendar(false);
    requestWeather();
}

void TTCalendarPage::willDisappear() {
    _visible = false;
    _forceRefreshing = false;
    _sleepAfterTimeTick = false;
    cancelInputIdleSleep();
    dismissExtendLoading();
    setStatusTimeVisible(true);
    TTScreenPage::willDisappear();
}

bool TTCalendarPage::handleKeyAction(TTKeyId key, TTKeyGesture gesture) {
    if (gesture == TT_KEY_CLICK && (key == TT_KEY_LEFT || key == TT_KEY_RIGHT)) {
        pageBy(key == TT_KEY_RIGHT ? 1 : -1);
        return true;
    }
    return TTScreenPage::handleKeyAction(key, gesture);
}

void TTCalendarPage::requestCalendar(bool extend) {
    if (_calendarFetching) {
        LOG_I("Calendar page: calendar fetch ignored (busy)");
        return;
    }
    cancelLightSleep();
    _sleepAfterTimeTick = false;
    _calendarFetching = true;
    if (!extend && !_calendarReady && !_forceRefreshing) {
        lv_label_set_text(_status, "正在同步日历");
        lv_obj_remove_flag(_status, LV_OBJ_FLAG_HIDDEN);
    }
    if (extend) {
        _extendLoading = true;
        TTInstanceOf<TTPopupLayer>().showLoading("加载中...");
    }
    LOG_I("Calendar page: fetch extend=%d", extend ? 1 : 0);
    TTInstanceOf<TTCalendarService>().requestFetch(extend);
}

void TTCalendarPage::requestWeather() {
    if (_weatherFetching) {
        LOG_I("Calendar page: weather fetch ignored (busy)");
        return;
    }
    cancelLightSleep();
    _sleepAfterTimeTick = false;
    _weatherFetching = true;
    LOG_I("Calendar page: weather fetch");
    TTInstanceOf<TTWeatherService>().requestFetch();
}

void TTCalendarPage::forceRefresh() {
    if (_calendarFetching || _weatherFetching || _forceRefreshing) {
        LOG_I("Calendar page: force refresh ignored (busy)");
        return;
    }
    cancelInputIdleSleep();
    _forceRefreshing = true;
    _planCount = 0;
    _pageCount = 0;
    _page = 0;
    lv_label_set_text(_status, "正在刷新");
    showPage();
    requestRefresh(TT_REFRESH_PARTIAL);
    LOG_I("Calendar page: force refresh");
    requestCalendar(false);
    requestWeather();
}

void TTCalendarPage::onSleepWake(const TTSleepWakePayload& wake) {
    if (!_visible) {
        return;
    }
    LOG_I("Calendar page: sleep wake reason=%d", (int)wake.reason);
    switch (wake.reason) {
        case TT_SLEEP_WAKE_FETCH:
            LOG_I("Calendar page: fetch period");
            requestCalendar(false);
            requestWeather();
            break;
        case TT_SLEEP_WAKE_POWER:
        case TT_SLEEP_WAKE_INPUT:
            _sleepAfterTimeTick = false;
            cancelInputIdleSleep();
            if (_calendarFetching || _weatherFetching || _forceRefreshing) {
                break;
            }
            _inputIdleSleepHandle = runOnce(TT_SLEEP_INPUT_IDLE_MS, [this]() {
                _inputIdleSleepHandle = 0;
                tryRequestLightSleep();
            });
            LOG_I("Calendar page: sleep in %d s if idle", TT_SLEEP_INPUT_IDLE_MS / 1000);
            break;
        case TT_SLEEP_WAKE_TIME:
            if (_calendarFetching || _weatherFetching || _forceRefreshing) {
                break;
            }
            _sleepAfterTimeTick = true;
            LOG_I("Calendar page: wait time tick then sleep");
            break;
    }
}

void TTCalendarPage::onTimeTick() {
    if (!_visible) {
        return;
    }
    LOG_I("Calendar page: time tick");
    const bool fetchedBeforeClock =
        _fetchedAt != 0 && _fetchedAt < (uint32_t)TT_RTC_MIN_UNIX;
    if (fetchedBeforeClock && TTInstanceOf<TTRtc>().isTimeValid()) {
        LOG_W("Calendar page: drop fetchedAt=%u after clock fix", (unsigned)_fetchedAt);
        _fetchedAt = 0;
        updateClock(false);
        if (WiFi.status() == WL_CONNECTED) {
            requestCalendar(false);
            requestWeather();
        }
    } else {
        const int previous = _lastMinute;
        updateClock(false);
        if (_lastMinute != previous && _count > 0) {
            showPage();
        }
        if (_lastMinute != previous) {
            requestRefresh(TT_REFRESH_PARTIAL);
        }
    }
    if (_sleepAfterTimeTick) {
        _sleepAfterTimeTick = false;
        tryRequestLightSleep();
    }
}

void TTCalendarPage::cancelInputIdleSleep() {
    if (_inputIdleSleepHandle == 0) {
        return;
    }
    cancelRepeat(_inputIdleSleepHandle);
    _inputIdleSleepHandle = 0;
    LOG_I("Calendar page: cancel idle sleep timer");
}

void TTCalendarPage::finishFetch() {
    if (_calendarFetching || _weatherFetching) {
        return;
    }
    _forceRefreshing = false;
    tryRequestLightSleep();
}

void TTCalendarPage::dismissExtendLoading() {
    if (!_extendLoading) {
        return;
    }
    _extendLoading = false;
    TTInstanceOf<TTPopupLayer>().dismissLoading();
}

void TTCalendarPage::tryRequestLightSleep() {
    if (!_visible || _calendarFetching || _weatherFetching || _forceRefreshing) {
        return;
    }
    if (!_calendarReady) {
        LOG_I("Calendar page: skip sleep, no calendar content");
        return;
    }
    requestLightSleep();
}

void TTCalendarPage::applyWeather(const TTWeatherPayload& payload) {
    if (payload.state == TT_WEATHER_FETCHING) {
        return;
    }
    _weatherFetching = false;
    if (_tempLabel == nullptr) {
        finishFetch();
        return;
    }
    if (payload.state != TT_WEATHER_OK) {
        _ageOk = false;
        layoutSide();
        LOG_I("Calendar page: weather failed, age cross");
        if (_visible) {
            requestRefresh(TT_REFRESH_PARTIAL);
        }
        finishFetch();
        return;
    }
    _haveWeather = true;
    _ageOk = true;
    _weatherCode = payload.current.weatherCode;
    _weatherDay = payload.current.isDay;
    char path[TT_WEATHER_ICON_PATH_MAX];
    tt_weather_condition_path(path, sizeof(path), _weatherCode, _weatherDay, TT_CAL_ICON);
    tt_stream_image_set_src(_weatherIcon, path);
    char buf[24];
    snprintf(buf, sizeof(buf), "%.0f", payload.current.temp);
    lv_label_set_text(_tempLabel, buf);
    snprintf(buf, sizeof(buf), "体感 %.0f°", payload.current.feelsLike);
    lv_label_set_text(_feelsLabel, buf);
    lv_label_set_text(_condLabel, tt_weather_condition_text(_weatherCode));
    _fetchedAt = payload.fetchedAt;
    if (_fetchedAt == 0) {
        const time_t now = time(nullptr);
        _fetchedAt = (now > 0) ? (uint32_t)now : 1;
    }
    formatAge(_fetchedAt, buf, sizeof(buf));
    lv_label_set_text(_ageLabel, buf);
    layoutSide();
    if (_visible) {
        LOG_I("Calendar page: weather ready, full refresh");
        requestRefresh(TT_REFRESH_FULL);
    }
    finishFetch();
}

void TTCalendarPage::setStatusTimeVisible(bool visible) {
    ITTNavigationController* nav = getNavigationController();
    TTNavigationBar* bar = nav != nullptr ? nav->getNavBar() : nullptr;
    if (bar != nullptr) {
        bar->setTimeVisible(visible);
    }
}

void TTCalendarPage::layoutSide() {
    if (_side == nullptr || _clockLabel == nullptr || _clockColon == nullptr || _clockMin == nullptr) {
        return;
    }
    lv_obj_update_layout(_clockLabel);
    lv_obj_update_layout(_clockColon);
    lv_obj_update_layout(_clockMin);
    int clockW = lv_obj_get_width(_clockLabel) + lv_obj_get_width(_clockColon) + lv_obj_get_width(_clockMin);
    if (clockW < TT_CAL_TIME_ADV) {
        clockW = TT_CAL_CLOCK_GLYPHS * TT_CAL_TIME_ADV;
    }
    const int sideW = TT_CAL_SIDE_W;
    if (lv_obj_get_width(_side) != sideW) {
        LOG_I("Calendar page: side width=%d clock=%d", sideW, clockW);
        lv_obj_set_width(_side, sideW);
        if (_list != nullptr) {
            const int listW = EPD_WIDTH - sideW;
            lv_obj_set_x(_list, sideW);
            lv_obj_set_width(_list, listW);
            if (_status != nullptr) {
                lv_obj_set_width(_status, listW - 16);
            }
            const int textW = listW - TT_CAL_TEXT_X - TT_CAL_PAD;
            const int titleW = eventTitleWidth(textW);
            for (int i = 0; i < TT_CAL_PAGE_SLOTS; i++) {
                if (_rows[i] == nullptr || _slotCapsule[i] != 0) {
                    continue;
                }
                const bool timed = _eventTimes[i] != nullptr
                    && !lv_obj_has_flag(_eventTimes[i], LV_OBJ_FLAG_HIDDEN);
                lv_obj_set_width(_rows[i], timed ? titleW : textW);
            }
        }
    }
    const int clockX = (sideW - clockW) / 2;
    lv_obj_align(_clockLabel, LV_ALIGN_BOTTOM_LEFT, clockX, -TT_CAL_CLOCK_INSET);
    lv_obj_align_to(_clockColon, _clockLabel, LV_ALIGN_OUT_RIGHT_BOTTOM, 0, -TT_CAL_COLON_SHIFT);
    lv_obj_align_to(_clockMin, _clockColon, LV_ALIGN_OUT_RIGHT_BOTTOM, 0, TT_CAL_COLON_SHIFT);
    if (_weatherIcon != nullptr) {
        lv_obj_align(_weatherIcon, LV_ALIGN_TOP_MID, 0, TT_CAL_ICON_Y);
    }
    if (_weekLabel != nullptr) {
        lv_obj_set_width(_weekLabel, LV_SIZE_CONTENT);
    }
    layoutAge();
    if (_tempLabel == nullptr || _tempUnit == nullptr || _condLabel == nullptr || _feelsLabel == nullptr) {
        return;
    }
    if (_haveWeather) {
        lv_obj_remove_flag(_tempLabel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(_tempUnit, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(_tempLabel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(_tempUnit, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_update_layout(_tempLabel);
    lv_obj_update_layout(_tempUnit);
    lv_obj_update_layout(_condLabel);
    lv_obj_update_layout(_feelsLabel);
    const int tempW = _haveWeather ? lv_obj_get_width(_tempLabel) : 0;
    const int unitW = _haveWeather ? lv_obj_get_width(_tempUnit) : 0;
    const int condW = lv_obj_get_width(_condLabel);
    const int groupW = tempW + TT_CAL_TEMP_DOT_GAP_X + unitW + TT_CAL_INFO_GAP_X + condW;
    int groupX = (sideW - groupW) / 2;
    if (groupX < 0) {
        groupX = 0;
    }
    lv_obj_set_pos(_tempLabel, groupX, TT_CAL_TEMP_Y);
    lv_obj_align_to(_tempUnit, _tempLabel, LV_ALIGN_OUT_RIGHT_TOP,
                    TT_CAL_TEMP_DOT_GAP_X, TT_CAL_TEMP_DOT_GAP_Y);
    lv_obj_update_layout(_tempLabel);
    lv_obj_update_layout(_tempUnit);
    const int tempY = lv_obj_get_y(_tempLabel);
    const int tempH = lv_obj_get_height(_tempLabel);
    const int condH = lv_obj_get_height(_condLabel);
    const int feelsH = lv_obj_get_height(_feelsLabel);
    const int stackH = condH + TT_CAL_META_GAP + feelsH;
    const int stackY = tempY + (tempH - stackH) / 2;
    const int infoX = lv_obj_get_x(_tempUnit) + lv_obj_get_width(_tempUnit) + TT_CAL_INFO_GAP_X;
    int infoW = sideW - infoX - TT_CAL_SIDE_PAD;
    if (infoW < 24) {
        infoW = 24;
    }
    if (lv_obj_get_width(_feelsLabel) > infoW) {
        lv_obj_set_width(_feelsLabel, infoW);
    }
    lv_obj_set_style_max_width(_condLabel, infoW, 0);
    lv_obj_set_pos(_condLabel, infoX, stackY);
    lv_obj_set_pos(_feelsLabel, infoX, stackY + condH + TT_CAL_META_GAP);

    if (_dateLabel == nullptr || _weekLabel == nullptr) {
        return;
    }
    lv_obj_update_layout(_dateLabel);
    lv_obj_update_layout(_weekLabel);
    lv_obj_update_layout(_clockLabel);
    const int clockTop = lv_obj_get_y(_clockLabel);
    const int weekH = lv_obj_get_height(_weekLabel);
    const int dateH = lv_obj_get_height(_dateLabel);
    const int weekBoxGap = TT_CAL_DATE_TIME_GAP
        - (weekH - 1 - TT_CAL_EN32_INK_BOTTOM) - TT_CAL_EN40_INK_TOP;
    const int dateBoxGap = TT_CAL_DATE_TIME_GAP
        - (dateH - 1 - TT_CAL_EN32_INK_BOTTOM) - TT_CAL_EN32_INK_TOP;
    const int weekY = clockTop - weekBoxGap - weekH;
    const int dateY = weekY - dateBoxGap - dateH;
    lv_obj_align(_dateLabel, LV_ALIGN_TOP_MID, 0, dateY);
    lv_obj_align(_weekLabel, LV_ALIGN_TOP_MID, 0, weekY);
}

void TTCalendarPage::layoutAge() {
    if (_ageLabel == nullptr) {
        return;
    }
    if (_fetchedAt == 0) {
        lv_obj_add_flag(_ageLabel, LV_OBJ_FLAG_HIDDEN);
        if (_ageIcon != nullptr) {
            lv_obj_add_flag(_ageIcon, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }
    lv_obj_remove_flag(_ageLabel, LV_OBJ_FLAG_HIDDEN);
    if (_ageIcon != nullptr) {
        lv_obj_remove_flag(_ageIcon, LV_OBJ_FLAG_HIDDEN);
    }
    int ageY = TT_CAL_AGE_PAD;
    if (_visibleSlots > 0 && _rows[0] != nullptr && _list != nullptr
        && !lv_obj_has_flag(_rows[0], LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_update_layout(_rows[0]);
        lv_obj_update_layout(_ageLabel);
        const int rowY = lv_obj_get_y(_list) + lv_obj_get_y(_rows[0]);
        const int rowH = lv_obj_get_height(_rows[0]);
        const int ageH = lv_obj_get_height(_ageLabel);
        ageY = rowY + (rowH - ageH) / 2;
        if (ageY < 0) {
            ageY = 0;
        }
    }
    lv_obj_align(_ageLabel, LV_ALIGN_TOP_RIGHT, -TT_CAL_AGE_PAD, ageY);
    lv_obj_move_foreground(_ageLabel);
    if (_ageIcon != nullptr) {
        tt_stream_image_set_src(_ageIcon, _ageOk ? TT_CAL_AGE_OK_SRC : TT_CAL_AGE_FAIL_SRC);
        lv_obj_align_to(_ageIcon, _ageLabel, LV_ALIGN_OUT_LEFT_TOP,
                        -TT_CAL_AGE_ICON_GAP, TT_CAL_AGE_ICON_DY);
        lv_obj_move_foreground(_ageIcon);
    }
}

void TTCalendarPage::updateClock(bool refreshIfChanged) {
    if (_clockLabel == nullptr) {
        return;
    }
    struct tm local = {};
    if (!TTInstanceOf<TTRtc>().getLocalTime(local)) {
        return;
    }
    const int minute = local.tm_yday * 24 * 60 + local.tm_hour * 60 + local.tm_min;
    if (minute == _lastMinute) {
        return;
    }
    _lastMinute = minute;
    char buf[16];
    snprintf(buf, sizeof(buf), "%02d", local.tm_hour);
    lv_label_set_text(_clockLabel, buf);
    snprintf(buf, sizeof(buf), "%02d", local.tm_min);
    lv_label_set_text(_clockMin, buf);
    snprintf(buf, sizeof(buf), "%d-%d", local.tm_mon + 1, local.tm_mday);
    lv_label_set_text(_dateLabel, buf);
    lv_label_set_text(_weekLabel, kWeekEn[local.tm_wday]);
    if (_ageLabel != nullptr && _fetchedAt != 0) {
        char age[24];
        formatAge(_fetchedAt, age, sizeof(age));
        lv_label_set_text(_ageLabel, age);
    }
    layoutSide();
    if (refreshIfChanged && _visible) {
        requestRefresh(TT_REFRESH_PARTIAL);
    }
}

void TTCalendarPage::rebuildRows() {
    _planCount = 0;
    _pageCount = 0;
    const int pageLimit = EPD_HEIGHT - TT_NAV_PAGE_INSET - TT_CAL_PAGE_FOOT;
    int slot = 0;
    int used = 0;
    bool open = false;
    auto newPage = [&]() -> bool {
        if (_pageCount >= TT_CAL_PAGE_MAX || _planCount >= TT_CAL_ROW_MAX) {
            return false;
        }
        _pageStart[_pageCount++] = _planCount;
        slot = 0;
        used = TT_CAL_PAD;
        open = true;
        return true;
    };
    auto push = [&](uint8_t kind, uint8_t eventIndex, int body, int extra) -> bool {
        if (!open || slot >= TT_CAL_PAGE_SLOTS || used + body + extra > pageLimit || _planCount >= TT_CAL_ROW_MAX) {
            return false;
        }
        _plan[_planCount].kind = kind;
        _plan[_planCount].eventIndex = eventIndex;
        _plan[_planCount].height = (int16_t)body;
        _planCount++;
        slot++;
        used += body + extra;
        return true;
    };

    TTFontManager& fonts = TTFontManager::instance();
    lv_font_t* fontEvent = fonts.getFont(TT_CAL_EVENT_FONT);
    if (fontEvent == nullptr) {
        fontEvent = fonts.getFont(TT_CAL_TEXT_FONT);
    }
    int textW = TT_CAL_TEXT_X;
    if (_list != nullptr) {
        textW = lv_obj_get_width(_list) - TT_CAL_TEXT_X - TT_CAL_PAD;
    }
    if (textW < 1) {
        textW = 1;
    }

    uint8_t index = 0;
    while (index < _count) {
        int year = 0;
        int yday = 0;
        sameDay(_events[index].startUnix, &year, &yday, nullptr);
        const uint8_t begin = index;
        while (index < _count) {
            int eventYear = 0;
            int eventDay = 0;
            sameDay(_events[index].startUnix, &eventYear, &eventDay, nullptr);
            if (eventYear != year || eventDay != yday) {
                break;
            }
            index++;
        }
        if (!open && !newPage()) {
            break;
        }
        int rule = slot > 0 ? TT_CAL_DAY_RULE : 0;
        const int titleW = eventTitleWidth(textW);
        const int firstH = eventRowBody(_events[begin].title, fontEvent, titleW);
        const int dayBlock = rule + TT_CAL_ROW_H + TT_CAL_DATE_EVENT_GAP + firstH;
        if (slot + 2 > TT_CAL_PAGE_SLOTS || used + dayBlock > pageLimit) {
            if (!newPage()) {
                break;
            }
            rule = 0;
        }
        if (!push(TT_CAL_ROW_DAY, begin, TT_CAL_ROW_H, rule)) {
            break;
        }
        for (uint8_t event = begin; event < index; event++) {
            const bool afterDay = _planCount > 0 && _plan[_planCount - 1].kind == TT_CAL_ROW_DAY;
            const int rowH = eventRowBody(_events[event].title, fontEvent, titleW);
            if (rowH > TT_CAL_ROW_H) {
                LOG_I("Calendar page: wrap event=%u height=%d", (unsigned)event, rowH);
            }
            const int gap = afterDay ? TT_CAL_DATE_EVENT_GAP : 0;
            if (!push(TT_CAL_ROW_EVENT, event, rowH, gap)) {
                if (!newPage() || !push(TT_CAL_ROW_DAY, begin, TT_CAL_ROW_H, 0)
                    || !push(TT_CAL_ROW_EVENT, event, rowH, TT_CAL_DATE_EVENT_GAP)) {
                    index = _count;
                    break;
                }
            }
        }
    }
    LOG_I("Calendar page: rows=%u pages=%u", (unsigned)_planCount, (unsigned)_pageCount);
}

void TTCalendarPage::showPage() {
    if (_pageCount == 0) {
        _visibleSlots = 0;
        for (int i = 0; i < TT_CAL_PAGE_SLOTS; i++) {
            lv_obj_add_flag(_rows[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(_eventTimes[i], LV_OBJ_FLAG_HIDDEN);
            _slotMark[i] = TT_CAL_MARK_NONE;
            _slotCapsule[i] = 0;
            _slotRule[i] = 0;
        }
        lv_obj_remove_flag(_status, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(_pageLabel, LV_OBJ_FLAG_HIDDEN);
        layoutAge();
        lv_obj_invalidate(_list);
        return;
    }
    if (_page >= _pageCount) {
        _page = (uint8_t)(_pageCount - 1);
    }
    const uint16_t begin = _pageStart[_page];
    const uint16_t end = (_page + 1 < _pageCount) ? _pageStart[_page + 1] : _planCount;
    const time_t now = time(nullptr);
    struct tm today = {};
    localtime_r(&now, &today);
    TTFontManager& fonts = TTFontManager::instance();
    lv_font_t* fontDate = fonts.getFont(TT_CAL_DATE_FONT);
    lv_font_t* fontEvent = fonts.getFont(TT_CAL_EVENT_FONT);
    if (fontDate == nullptr) {
        fontDate = fonts.getFont(TT_CAL_TEXT_FONT);
    }
    if (fontEvent == nullptr) {
        fontEvent = fontDate;
    }
    const int textW = lv_obj_get_width(_list) - TT_CAL_TEXT_X - TT_CAL_PAD;
    int nextIndex = -1;
    for (uint8_t i = 0; i < _count; i++) {
        if (_events[i].startUnix > now
            && (nextIndex < 0 || _events[i].startUnix < _events[nextIndex].startUnix)) {
            nextIndex = (int)i;
        }
    }

    _visibleSlots = 0;
    lv_obj_add_flag(_status, LV_OBJ_FLAG_HIDDEN);
    int cursor = TT_CAL_PAD;
    bool prevDay = false;
    for (uint16_t row = begin; row < end && _visibleSlots < TT_CAL_PAGE_SLOTS; row++) {
        const uint8_t slot = _visibleSlots;
        const TTCalRow& item = _plan[row];
        const TTCalEvent& event = _events[item.eventIndex];
        char text[96];
        char timeText[8];
        timeText[0] = '\0';
        struct tm local = {};
        sameDay(event.startUnix, nullptr, nullptr, &local);
        if (item.kind == TT_CAL_ROW_DAY) {
            const bool headerToday = local.tm_year == today.tm_year && local.tm_yday == today.tm_yday;
            if (headerToday) {
                snprintf(text, sizeof(text), "今天");
            } else {
                snprintf(text, sizeof(text), "%d月%d日 %s", local.tm_mon + 1, local.tm_mday,
                         kWeekdays[local.tm_wday]);
            }
            _slotMark[slot] = TT_CAL_MARK_NONE;
        } else {
            uint8_t mark = TT_CAL_MARK_SMALL;
            if (event.startUnix <= now && now < event.endUnix) {
                mark = TT_CAL_MARK_FILLED;
            } else if ((int)item.eventIndex == nextIndex) {
                mark = TT_CAL_MARK_HOLLOW;
            }
            _slotMark[slot] = mark;
            formatEventTime(event, timeText, sizeof(timeText));
            snprintf(text, sizeof(text), "%s", event.title);
        }
        const bool dayRow = item.kind == TT_CAL_ROW_DAY;
        const bool isToday = dayRow && local.tm_year == today.tm_year && local.tm_yday == today.tm_yday;
        lv_font_t* font = dayRow ? fontDate : fontEvent;
        if (font == nullptr) {
            font = fontDate != nullptr ? fontDate : fontEvent;
        }
        if (dayRow && isToday) {
            _slotCapsule[slot] = TT_CAL_CAPSULE_FILL;
        } else {
            _slotCapsule[slot] = TT_CAL_CAPSULE_NONE;
        }
        if (slot > 0 && dayRow) {
            cursor += TT_CAL_DAY_RULE;
        } else if (slot > 0 && prevDay) {
            cursor += TT_CAL_DATE_EVENT_GAP;
        }
        _slotRule[slot] = (slot > 0 && dayRow) ? 1 : 0;
        _slotY[slot] = (int16_t)cursor;
        lv_obj_set_style_text_font(_rows[slot], font, 0);
        lv_obj_set_style_bg_color(_rows[slot], lv_color_black(), 0);
        lv_obj_set_style_border_color(_rows[slot], lv_color_black(), 0);
        lv_obj_set_style_border_opa(_rows[slot], LV_OPA_COVER, 0);
        if (isToday) {
            lv_obj_set_style_text_color(_rows[slot], lv_color_white(), 0);
            lv_obj_set_style_bg_opa(_rows[slot], LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(_rows[slot], 0, 0);
            lv_obj_set_style_radius(_rows[slot], LV_RADIUS_CIRCLE, 0);
        } else {
            lv_obj_set_style_text_color(_rows[slot], lv_color_black(), 0);
            lv_obj_set_style_bg_opa(_rows[slot], LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(_rows[slot], 0, 0);
            lv_obj_set_style_radius(_rows[slot], 0, 0);
        }
        const int padY = isToday ? TT_CAL_CAPSULE_PAD_Y : 0;
        const int padX = isToday ? TT_CAL_CAPSULE_PAD_X : 0;
        lv_obj_set_style_pad_left(_rows[slot], padX, 0);
        lv_obj_set_style_pad_right(_rows[slot], padX, 0);
        lv_obj_set_style_pad_top(_rows[slot], padY, 0);
        lv_obj_set_style_pad_bottom(_rows[slot], padY, 0);
        lv_label_set_long_mode(_rows[slot], dayRow ? LV_LABEL_LONG_CLIP : LV_LABEL_LONG_WRAP);
        lv_obj_set_height(_rows[slot], LV_SIZE_CONTENT);
        lv_obj_set_width(_rows[slot], dayRow ? LV_SIZE_CONTENT : eventTitleWidth(textW));
        lv_label_set_text(_rows[slot], text);
        const int lineH = font != nullptr ? lv_font_get_line_height(font) : TT_CAL_ROW_H;
        const int boxH = lineH + padY * 2;
        const int rowH = item.height > 0 ? item.height : TT_CAL_ROW_H;
        int y = cursor;
        if (dayRow) {
            y = cursor + (TT_CAL_ROW_H - boxH) / 2;
        } else {
            const int lead = TT_CAL_ROW_H - lineH;
            if (lead > 0) {
                y = cursor + lead / 2;
            }
        }
        const int x = isToday ? TT_CAL_TEXT_X - padX : (dayRow ? TT_CAL_TEXT_X : TT_CAL_TEXT_X + TT_CAL_EVENT_TIME_W + TT_CAL_EVENT_TIME_GAP);
        lv_obj_set_pos(_rows[slot], x, y);
        if (!dayRow) {
            lv_obj_set_style_text_font(_eventTimes[slot], font, 0);
            lv_label_set_text(_eventTimes[slot], timeText);
            lv_obj_set_pos(_eventTimes[slot], TT_CAL_TEXT_X, y);
            lv_obj_remove_flag(_eventTimes[slot], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(_eventTimes[slot], LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_remove_flag(_rows[slot], LV_OBJ_FLAG_HIDDEN);
        cursor += dayRow ? TT_CAL_ROW_H : rowH;
        prevDay = dayRow;
        _visibleSlots++;
    }
    for (uint8_t slot = _visibleSlots; slot < TT_CAL_PAGE_SLOTS; slot++) {
        lv_obj_add_flag(_rows[slot], LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(_eventTimes[slot], LV_OBJ_FLAG_HIDDEN);
        _slotMark[slot] = TT_CAL_MARK_NONE;
        _slotCapsule[slot] = 0;
        _slotRule[slot] = 0;
    }
    if (_pageCount > 1) {
        char pageText[12];
        snprintf(pageText, sizeof(pageText), "%u/%u", (unsigned)_page + 1, (unsigned)_pageCount);
        lv_label_set_text(_pageLabel, pageText);
        lv_obj_remove_flag(_pageLabel, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(_pageLabel, LV_OBJ_FLAG_HIDDEN);
    }
    layoutAge();
    lv_obj_invalidate(_list);
}

void TTCalendarPage::applyCalendar(const TTCalendarPayload& payload) {
    _calendarFetching = false;
    const int32_t previousEnd = _rangeEnd;
    _count = TTInstanceOf<TTCalendarService>().copyEvents(_events, TT_CAL_EVENT_MAX);
    if (_count > payload.count) {
        _count = payload.count;
    }
    _rangeStart = payload.rangeStart;
    _rangeEnd = payload.rangeEnd;
    LOG_I("Calendar page: state=%d events=%u msg=%s",
          (int)payload.state, (unsigned)_count, payload.message);
    _calendarReady = payload.state == TT_CAL_OK;
    if (payload.state != TT_CAL_OK) {
        _planCount = 0;
        _pageCount = 0;
        _page = 0;
        lv_label_set_text(_status, payload.message[0] != '\0' ? payload.message : "同步失败");
        showPage();
    } else if (_count == 0) {
        _planCount = 0;
        _pageCount = 0;
        _page = 0;
        lv_label_set_text(_status, payload.message[0] != '\0' ? payload.message : "这7天没有日程");
        showPage();
    } else {
        rebuildRows();
        if (!payload.extended) {
            _page = 0;
        } else {
            _page = _pageCount > 0 ? (uint8_t)(_pageCount - 1) : 0;
            for (uint8_t page = 0; page < _pageCount; page++) {
                const uint16_t begin = _pageStart[page];
                const uint16_t end = (page + 1 < _pageCount) ? _pageStart[page + 1] : _planCount;
                for (uint16_t row = begin; row < end; row++) {
                    if (_plan[row].kind == TT_CAL_ROW_EVENT
                        && _events[_plan[row].eventIndex].startUnix >= previousEnd) {
                        _page = page;
                        page = _pageCount;
                        break;
                    }
                }
            }
        }
        showPage();
    }
    if (_extendLoading) {
        dismissExtendLoading();
    } else if (_visible) {
        requestRefresh(payload.extended ? TT_REFRESH_PARTIAL : TT_REFRESH_DEEP);
    }
    finishFetch();
}

void TTCalendarPage::pageBy(int delta) {
    if (delta < 0) {
        if (_page == 0) {
            return;
        }
        _page--;
        showPage();
        requestRefresh(TT_REFRESH_PARTIAL);
        return;
    }
    if (_page + 1 < _pageCount) {
        _page++;
        showPage();
        requestRefresh(TT_REFRESH_PARTIAL);
        return;
    }
    LOG_I("Calendar page: load next range");
    requestCalendar(true);
}

void TTCalendarPage::onListDraw(lv_event_t* e) {
    TTCalendarPage* self = (TTCalendarPage*)lv_event_get_user_data(e);
    if (self == nullptr || self->_list == nullptr || self->_visibleSlots == 0) {
        return;
    }
    lv_layer_t* layer = lv_event_get_layer(e);
    lv_area_t coords;
    lv_obj_get_coords(self->_list, &coords);
    lv_draw_rect_dsc_t ink;
    lv_draw_rect_dsc_init(&ink);
    ink.bg_color = lv_color_black();
    ink.bg_opa = LV_OPA_COVER;
    ink.border_width = 0;
    ink.radius = 0;
    const int rulePeriod = TT_CAL_RULE_DASH + TT_CAL_RULE_GAP;
    for (uint8_t i = 0; i < self->_visibleSlots; i++) {
        if (self->_slotRule[i] == 0) {
            continue;
        }
        if (i == 0) {
            continue;
        }
        lv_area_t above;
        lv_area_t below;
        lv_obj_get_coords(self->_rows[i - 1], &above);
        lv_obj_get_coords(self->_rows[i], &below);
        const int y = (above.y2 + below.y1) / 2;
        const int xStart = coords.x1 + TT_CAL_TEXT_X;
        const int xEnd = coords.x2 - TT_CAL_PAD;
        for (int x = xStart; x <= xEnd; x += rulePeriod) {
            int segEnd = x + TT_CAL_RULE_DASH - 1;
            if (segEnd > xEnd) {
                segEnd = xEnd;
            }
            lv_area_t seg;
            seg.x1 = x;
            seg.x2 = segEnd;
            seg.y1 = y;
            seg.y2 = y;
            lv_draw_rect(layer, &ink, &seg);
        }
    }

    int first = -1;
    int last = -1;
    for (uint8_t i = 0; i < self->_visibleSlots; i++) {
        if (self->_slotMark[i] == TT_CAL_MARK_NONE) {
            continue;
        }
        if (first < 0) {
            first = (int)i;
        }
        last = (int)i;
    }
    if (first < 0) {
        return;
    }

    const int axisX = coords.x1 + TT_CAL_AXIS_X;
    auto centerY = [&](int slot) {
        lv_obj_t* row = self->_rows[slot];
        lv_area_t area;
        lv_obj_get_coords(row, &area);
        const lv_font_t* font = lv_obj_get_style_text_font(row, LV_PART_MAIN);
        if (font == nullptr || font->line_height <= 0) {
            return (area.y1 + area.y2) / 2;
        }
        const int padTop = lv_obj_get_style_pad_top(row, LV_PART_MAIN);
        const int ascent = font->line_height - font->base_line;
        return area.y1 + padTop + ascent / 2 + TT_CAL_MARK_DY;
    };

    const int y1 = centerY(first);
    const int y2 = centerY(last);
    lv_area_t axis;
    axis.x1 = axisX;
    axis.x2 = axisX;
    axis.y1 = y1;
    axis.y2 = y2;
    lv_draw_rect(layer, &ink, &axis);

    auto markRadius = [&](int slot) {
        if (self->_slotMark[slot] == TT_CAL_MARK_SMALL) {
            return TT_CAL_MARK_SM / 2;
        }
        if (self->_slotMark[slot] == TT_CAL_MARK_HOLLOW) {
            return TT_CAL_MARK_NEXT / 2;
        }
        return TT_CAL_MARK_LG / 2;
    };
    auto drawStub = [&](int innerY, int dir) {
        const int period = TT_CAL_RULE_DASH + TT_CAL_RULE_GAP;
        for (int offset = 0; offset < TT_CAL_AXIS_STUB; offset++) {
            if (offset % period >= TT_CAL_RULE_DASH) {
                continue;
            }
            const int y = innerY + dir * offset;
            if (y < coords.y1 || y > coords.y2) {
                continue;
            }
            lv_area_t seg;
            seg.x1 = axisX;
            seg.x2 = axisX;
            seg.y1 = y;
            seg.y2 = y;
            lv_draw_rect(layer, &ink, &seg);
        }
    };
    drawStub(y1 - markRadius(first) - TT_CAL_AXIS_STUB_GAP, -1);
    drawStub(y2 + markRadius(last) + TT_CAL_AXIS_STUB_GAP, 1);

    for (uint8_t i = 0; i < self->_visibleSlots; i++) {
        if (self->_slotMark[i] == TT_CAL_MARK_NONE) {
            continue;
        }
        const int cy = centerY((int)i);
        if (self->_slotMark[i] == TT_CAL_MARK_SMALL) {
            const int radius = TT_CAL_MARK_SM / 2;
            fillDiskAt(layer, axisX, cy, radius, lv_color_black());
            const int inner = radius - TT_CAL_MARK_SM_BORDER;
            if (inner > 0) {
                fillDiskAt(layer, axisX, cy, inner, lv_color_white());
            }
            continue;
        }
        if (self->_slotMark[i] == TT_CAL_MARK_HOLLOW) {
            const int radius = TT_CAL_MARK_NEXT / 2;
            fillDiskAt(layer, axisX, cy, radius, lv_color_black());
            const int inner = radius - TT_CAL_MARK_BORDER;
            if (inner > 0) {
                fillDiskAt(layer, axisX, cy, inner, lv_color_white());
            }
            continue;
        }
        const int radius = TT_CAL_MARK_LG / 2;
        fillDiskAt(layer, axisX, cy, radius, lv_color_black());
        const int inner = radius - TT_CAL_MARK_BORDER;
        if (inner > 0) {
            fillDiskAt(layer, axisX, cy, inner, lv_color_white());
        }
        fillDiskAt(layer, axisX, cy, TT_CAL_MARK_CORE / 2, lv_color_black());
    }
}
