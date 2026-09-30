#include "TTPictorialPage.h"
#include "../Base/Logger.h"
#include "../Base/TTFile.h"
#include "../Base/TTFontManager.h"
#include "../Base/TTInstance.h"
#include "../Base/TTLvglEpdDriver.h"
#include "../Base/TTNavigationBar.h"
#include "../Base/TTPopupLayer.h"
#include "../Base/TTRtc.h"
#include "../Base/TTStreamImage.h"
#include "../Pages/TTCalendarPage.h"
#include "../Service/TTPictorialService.h"
#include "../Service/TTSleepService.h"
#include "../Service/TTWeatherService.h"
#include <EPDConfig.h>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <sys/time.h>

namespace {

const char* weekEn(int wday) {
    const char* names[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
    if (wday < 0 || wday > 6) {
        return "---";
    }
    return names[wday];
}

lv_obj_t* createLabel(lv_obj_t* parent, const lv_font_t* font, lv_color_t color, const char* text) {
    lv_obj_t* label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_label_set_text(label, text != nullptr ? text : "");
    return label;
}

uint32_t msUntilDaySwitch() {
    struct timeval tv;
    memset(&tv, 0, sizeof(tv));
    struct tm local = {};
    if (!TTInstanceOf<TTRtc>().isTimeValid()
        || !TTInstanceOf<TTRtc>().getLocalTime(local)
        || gettimeofday(&tv, nullptr) != 0
        || tv.tv_sec <= 0) {
        return 60000;
    }
    const int secOfDay = local.tm_hour * 3600 + local.tm_min * 60 + local.tm_sec;
    int remainSec = 24 * 3600 - secOfDay;
    if (remainSec <= 0) {
        remainSec = 24 * 3600;
    }
    const int msIntoSec = (int)(tv.tv_usec / 1000);
    int64_t ms = (int64_t)remainSec * 1000 - msIntoSec + TT_PIC_DAY_SWITCH_MS;
    if (ms < 1) {
        ms = 24LL * 3600 * 1000;
    }
    return (uint32_t)ms;
}

}  // namespace

void TTPictorialPage::buildContent(lv_obj_t* screen) {
    TTFontManager& fonts = TTFontManager::instance();
    lv_font_t* font16 = fonts.getFont(TT_CAL_TEXT_FONT);
    lv_font_t* fontTemp = fonts.getFont(TT_CAL_TEMP_FONT);
    lv_font_t* fontClock = fonts.getFont(TT_CAL_TIME_FONT);
    lv_font_t* fontDate = fonts.getFont(TT_CAL_DATE_EN_FONT);
    lv_font_t* fontWeek = fonts.getFont(TT_CAL_WEEK_FONT);
    lv_font_t* fontCond = fonts.getFont(TT_CAL_COND_FONT);
    lv_font_t* font10 = fonts.getFont(TT_CAL_PAGE_FONT);
    if (fontTemp == nullptr) {
        fontTemp = font16;
    }
    if (font10 == nullptr) {
        font10 = font16;
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
    if (fontCond == nullptr) {
        fontCond = font16;
    }
    const int height = EPD_HEIGHT - TT_NAV_PAGE_INSET;
    const int artW = EPD_WIDTH - TT_CAL_SIDE_W;

    _side = lv_obj_create(screen);
    lv_obj_set_pos(_side, 0, 0);
    lv_obj_set_size(_side, TT_CAL_SIDE_W, height);
    lv_obj_set_style_bg_color(_side, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(_side, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_side, 0, 0);
    lv_obj_set_style_pad_all(_side, 0, 0);
    lv_obj_set_style_radius(_side, 0, 0);
    lv_obj_set_scrollable(_side, false);

    _weatherIcon = tt_stream_image_create(_side);
    lv_obj_set_size(_weatherIcon, TT_CAL_ICON, TT_CAL_ICON);
    lv_obj_align(_weatherIcon, LV_ALIGN_TOP_MID, 0, TT_CAL_ICON_Y);

    _tempLabel = createLabel(_side, fontTemp, lv_color_black(), "");
    lv_obj_set_pos(_tempLabel, TT_CAL_TEMP_X, TT_CAL_TEMP_Y);
    lv_obj_set_hidden(_tempLabel, true);

    _tempUnit = lv_obj_create(_side);
    lv_obj_set_size(_tempUnit, TT_CAL_DOT_LG, TT_CAL_DOT_LG);
    lv_obj_set_style_bg_opa(_tempUnit, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(_tempUnit, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(_tempUnit, TT_CAL_DOT_BORDER, 0);
    lv_obj_set_style_border_color(_tempUnit, lv_color_black(), 0);
    lv_obj_set_style_border_opa(_tempUnit, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(_tempUnit, 0, 0);
    lv_obj_set_scrollable(_tempUnit, false);
    lv_obj_set_clickable(_tempUnit, false);
    lv_obj_set_hidden(_tempUnit, true);

    _condLabel = createLabel(_side, fontCond, lv_color_black(), "");
    lv_label_set_long_mode(_condLabel, LV_LABEL_LONG_CLIP);

    _dateLabel = createLabel(_side, fontDate, lv_color_black(), "--");
    _weekLabel = createLabel(_side, fontWeek, lv_color_black(), "---");
    lv_obj_set_width(_weekLabel, LV_SIZE_CONTENT);

    _clockLabel = createLabel(_side, fontClock, lv_color_black(), "--");
    _clockColon = createLabel(_side, fontClock, lv_color_black(), ":");
    _clockMin = createLabel(_side, fontClock, lv_color_black(), "--");

    _art = tt_stream_image_create(screen);
    lv_obj_set_pos(_art, TT_CAL_SIDE_W, 0);
    lv_obj_set_hidden(_art, true);

    _status = createLabel(screen, font16, lv_color_black(), "正在获取画报");
    lv_obj_set_width(_status, artW - 16);
    lv_obj_set_style_text_align(_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_bg_color(_status, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(_status, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(_status, 4, 0);
    lv_obj_align(_status, LV_ALIGN_CENTER, TT_CAL_SIDE_W / 2, 0);

    _pickPanel = lv_obj_create(screen);
    lv_obj_set_size(_pickPanel, artW - 24,
                    TT_PIC_PICK_ROWS * TT_PIC_PICK_ROW_H + TT_PIC_PICK_ORDINAL_H + TT_PIC_PICK_INSET * 2
                        + TT_PIC_PICK_BORDER * 2);
    lv_obj_align(_pickPanel, LV_ALIGN_CENTER, TT_CAL_SIDE_W / 2, 0);
    lv_obj_set_style_bg_color(_pickPanel, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(_pickPanel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_pickPanel, TT_PIC_PICK_BORDER, 0);
    lv_obj_set_style_border_color(_pickPanel, lv_color_black(), 0);
    lv_obj_set_style_border_opa(_pickPanel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_side(_pickPanel, LV_BORDER_SIDE_FULL, 0);
    lv_obj_set_style_border_post(_pickPanel, true, 0);
    lv_obj_set_style_radius(_pickPanel, 0, 0);
    lv_obj_set_style_pad_all(_pickPanel, TT_PIC_PICK_INSET, 0);
    lv_obj_set_style_pad_row(_pickPanel, 0, 0);
    lv_obj_set_layout(_pickPanel, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(_pickPanel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollable(_pickPanel, false);
    lv_obj_set_clickable(_pickPanel, false);
    lv_obj_set_hidden(_pickPanel, true);
    for (int i = 0; i < TT_PIC_PICK_ROWS; i++) {
        _pickRows[i] = lv_obj_create(_pickPanel);
        lv_obj_set_width(_pickRows[i], LV_PCT(100));
        lv_obj_set_height(_pickRows[i], TT_PIC_PICK_ROW_H);
        lv_obj_set_style_bg_opa(_pickRows[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(_pickRows[i], 0, 0);
        lv_obj_set_style_pad_all(_pickRows[i], 0, 0);
        lv_obj_set_style_pad_left(_pickRows[i], 8, 0);
        lv_obj_set_style_pad_right(_pickRows[i], 8, 0);
        lv_obj_set_clickable(_pickRows[i], false);

        _pickNames[i] = createLabel(_pickRows[i], font16, lv_color_black(), "");
        lv_obj_set_style_bg_opa(_pickNames[i], LV_OPA_TRANSP, 0);
        lv_obj_set_width(_pickNames[i], LV_PCT(100));
        lv_label_set_long_mode(_pickNames[i], LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_align(_pickNames[i], LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_align(_pickNames[i], LV_ALIGN_LEFT_MID, 0, 0);
    }

    _pickOrdinal = createLabel(_pickPanel, font10, lv_color_black(), "");
    lv_obj_set_width(_pickOrdinal, LV_PCT(100));
    lv_obj_set_height(_pickOrdinal, TT_PIC_PICK_ORDINAL_H);
    lv_obj_set_style_bg_opa(_pickOrdinal, LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_align(_pickOrdinal, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_pad_right(_pickOrdinal, 2, 0);

    _hit = lv_btn_create(screen);
    lv_obj_set_pos(_hit, 0, 0);
    lv_obj_set_size(_hit, EPD_WIDTH, height);
    lv_obj_set_style_bg_opa(_hit, LV_OPA_TRANSP, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(_hit, LV_OPA_TRANSP, LV_STATE_FOCUSED);
    lv_obj_set_style_bg_opa(_hit, LV_OPA_TRANSP, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(_hit, 0, 0);
    lv_obj_set_style_shadow_width(_hit, 0, 0);
    lv_obj_set_style_outline_width(_hit, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_outline_width(_hit, 0, LV_STATE_FOCUSED);
    lv_obj_set_style_radius(_hit, 0, 0);
    lv_obj_add_event_cb(_hit, [](lv_event_t* e) {
        TTPictorialPage* self = (TTPictorialPage*)lv_event_get_user_data(e);
        if (self != nullptr && self->_picking) {
            self->closePicker(true);
        }
    }, LV_EVENT_CLICKED, this);
    addToFocusGroup(_hit);

    layoutSide();
}

void TTPictorialPage::setup() {
    TTScreenPage::setup();
    subscribe<TTPicPayload>(TT_NOTIFICATION_PICTORIAL, [this](const TTPicPayload& payload) {
        applyPictorial(payload);
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
        if (_picking) {
            closePicker(false);
        } else {
            openPicker();
        }
    });
}

void TTPictorialPage::willAppear() {
    TTScreenPage::willAppear();
    _visible = true;
    TTInstanceOf<TTLvglEpdDriver>().setAutoDeepRefresh(false);
    setStatusTimeVisible(false);
    updateClock(false);
    layoutSide();
    if (_hit != nullptr) {
        lv_group_focus_obj(_hit);
    }
    scheduleDaySwitch();
    if (_artFetching) {
        TTInstanceOf<TTPopupLayer>().showLoading(TT_PIC_UPDATING_TEXT);
    }
    requestWeather();
    TTInstanceOf<TTPictorialService>().requestToday();
}

void TTPictorialPage::willDisappear() {
    _visible = false;
    _picking = false;
    if (_pickPanel != nullptr) {
        lv_obj_set_hidden(_pickPanel, true);
    }
    _sleepAfterTimeTick = false;
    cancelInputIdleSleep();
    cancelSleepSettle();
    if (_artFetching) {
        TTInstanceOf<TTPopupLayer>().dismissLoading();
    }
    if (_dayHandle != 0) {
        cancelRepeat(_dayHandle);
        _dayHandle = 0;
    }
    setStatusTimeVisible(true);
    TTInstanceOf<TTLvglEpdDriver>().setAutoDeepRefresh(true);
    TTScreenPage::willDisappear();
}

bool TTPictorialPage::handleKeyAction(TTKeyId key, TTKeyGesture gesture) {
    if (gesture == TT_KEY_CLICK && (key == TT_KEY_LEFT || key == TT_KEY_RIGHT)) {
        if (_picking) {
            const uint8_t count = TTInstanceOf<TTPictorialService>().seriesCount();
            if (count == 0) {
                return true;
            }
            if (key == TT_KEY_LEFT) {
                _pickIndex = (uint8_t)((_pickIndex + 1) % count);
            } else {
                _pickIndex = (uint8_t)((_pickIndex + count - 1) % count);
            }
            showPicker();
            if (_visible) {
                requestRefresh(TT_REFRESH_PARTIAL);
            }
            return true;
        }
        LOG_I("Pictorial page: dial next");
        cancelLightSleep();
        cancelSleepSettle();
        _sleepAfterTimeTick = false;
        cancelInputIdleSleep();
        TTInstanceOf<TTPictorialService>().requestAnother();
        return true;
    }
    return TTScreenPage::handleKeyAction(key, gesture);
}

void TTPictorialPage::requestWeather(bool force) {
    if (_weatherFetching) {
        LOG_I("Pictorial page: weather fetch ignored (busy)");
        return;
    }
    cancelLightSleep();
    cancelSleepSettle();
    _sleepAfterTimeTick = false;
    _weatherFetching = true;
    LOG_I("Pictorial page: weather fetch force=%d", force ? 1 : 0);
    TTInstanceOf<TTWeatherService>().requestFetch(force);
}

void TTPictorialPage::applyWeather(const TTWeatherPayload& payload) {
    if (payload.state == TT_WEATHER_FETCHING) {
        return;
    }
    _weatherFetching = false;
    if (_tempLabel == nullptr) {
        tryRequestLightSleep();
        return;
    }
    if (payload.state != TT_WEATHER_OK) {
        layoutSide();
        LOG_I("Pictorial page: weather failed");
        if (_visible) {
            requestRefresh(TT_REFRESH_DEEP);
        }
        tryRequestLightSleep(TT_SLEEP_AFTER_REFRESH_MS);
        return;
    }
    _haveWeather = true;
    _weatherCode = payload.current.weatherCode;
    _weatherDay = payload.current.isDay;
    char path[TT_WEATHER_ICON_PATH_MAX];
    tt_weather_condition_path(path, sizeof(path), _weatherCode, _weatherDay, TT_CAL_ICON);
    tt_stream_image_set_src(_weatherIcon, path);
    char buf[24];
    snprintf(buf, sizeof(buf), "%.0f", payload.current.temp);
    lv_label_set_text(_tempLabel, buf);
    lv_label_set_text(_condLabel, tt_weather_condition_text(_weatherCode));
    layoutSide();
    if (_visible) {
        requestRefresh(TT_REFRESH_FULL);
    }
    tryRequestLightSleep(TT_SLEEP_AFTER_REFRESH_MS);
}

void TTPictorialPage::applyPictorial(const TTPicPayload& payload) {
    if (!_visible && payload.state != TT_PIC_OK) {
        return;
    }
    if (payload.state == TT_PIC_FETCHING) {
        _artFetching = true;
        cancelLightSleep();
        cancelSleepSettle();
        _sleepAfterTimeTick = false;
        cancelInputIdleSleep();
        if (!_artReady && _status != nullptr) {
            lv_label_set_text(_status, payload.message);
            lv_obj_set_hidden(_status, false);
        }
        LOG_I("Pictorial page: fetching");
        if (_visible) {
            TTInstanceOf<TTPopupLayer>().showLoading(TT_PIC_UPDATING_TEXT);
        }
        return;
    }
    const bool updating = _artFetching;
    _artFetching = false;
    if (payload.state == TT_PIC_MANIFEST) {
        auto& service = TTInstanceOf<TTPictorialService>();
        LOG_I("Pictorial page: manifest series=%u", (unsigned)service.seriesCount());
        if (service.seriesCount() == 0) {
            if (updating) {
                TTInstanceOf<TTPopupLayer>().dismissLoading();
            }
            tryRequestLightSleep();
            return;
        }
        _picking = true;
        _pickIndex = service.selectedIndex();
        showPicker();
        if (updating) {
            TTInstanceOf<TTPopupLayer>().dismissLoading();
        } else if (_visible) {
            requestRefresh(TT_REFRESH_PARTIAL);
        }
        return;
    }
    if (payload.state != TT_PIC_OK) {
        LOG_W("Pictorial page: %s", payload.message);
        if (_artPath[0] != '\0') {
            showArt(_artPath);
            _artReady = true;
            LOG_I("Pictorial page: keep %s", _artPath);
        } else if (_status != nullptr) {
            lv_label_set_text(_status, payload.message[0] != '\0' ? payload.message : "画报下载失败");
            lv_obj_set_hidden(_status, false);
        }
        if (updating) {
            TTInstanceOf<TTPopupLayer>().dismissLoading();
        } else if (_visible && _artPath[0] == '\0') {
            requestRefresh(TT_REFRESH_PARTIAL);
        }
        tryRequestLightSleep();
        return;
    }
    showArt(payload.path);
    _artReady = true;
    LOG_I("Pictorial page: show %s (%s)", payload.path, payload.name);
    if (updating) {
        TTInstanceOf<TTPopupLayer>().dismissLoading(false);
    }
    if (_visible) {
        requestRefresh(TT_REFRESH_DEEP);
    }
    tryRequestLightSleep(TT_SLEEP_AFTER_REFRESH_MS);
}

void TTPictorialPage::onSleepWake(const TTSleepWakePayload& wake) {
    if (!_visible) {
        return;
    }
    LOG_I("Pictorial page: sleep wake reason=%d", (int)wake.reason);
    switch (wake.reason) {
        case TT_SLEEP_WAKE_FETCH:
            requestWeather();
            if (!TTInstanceOf<TTPictorialService>().hasToday()) {
                TTInstanceOf<TTPictorialService>().requestDaily();
            }
            break;
        case TT_SLEEP_WAKE_POWER:
        case TT_SLEEP_WAKE_INPUT:
            _sleepAfterTimeTick = false;
            cancelInputIdleSleep();
            if (!TTInstanceOf<TTPictorialService>().hasToday()) {
                TTInstanceOf<TTPictorialService>().requestDaily();
            }
            if (_weatherFetching || _artFetching || _picking) {
                break;
            }
            armInputIdleSleep();
            break;
        case TT_SLEEP_WAKE_TIME:
            if (_weatherFetching || _artFetching || _picking) {
                break;
            }
            _sleepAfterTimeTick = true;
            LOG_I("Pictorial page: wait time tick then sleep");
            break;
    }
}

void TTPictorialPage::onTimeTick() {
    if (!_visible) {
        return;
    }
    LOG_I("Pictorial page: time tick");
    const bool sleepPending = _sleepAfterTimeTick || _sleepSettleHandle != 0
        || TTInstanceOf<TTSleepService>().isLightSleepRequested();
    if (sleepPending) {
        cancelLightSleep();
        cancelSleepSettle();
        _sleepAfterTimeTick = false;
    }
    updateClock(true);
    if (!TTInstanceOf<TTPictorialService>().hasToday()) {
        LOG_I("Pictorial page: day changed");
        TTInstanceOf<TTPictorialService>().requestDaily();
    }
    if (sleepPending && !_artFetching) {
        tryRequestLightSleep();
    }
}

void TTPictorialPage::setStatusTimeVisible(bool visible) {
    ITTNavigationController* nav = getNavigationController();
    TTNavigationBar* bar = nav != nullptr ? nav->getNavBar() : nullptr;
    if (bar != nullptr) {
        bar->setTimeVisible(visible);
    }
}

void TTPictorialPage::layoutSide() {
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
    if (_tempLabel == nullptr || _tempUnit == nullptr || _condLabel == nullptr) {
        return;
    }
    if (_haveWeather) {
        lv_obj_set_hidden(_tempLabel, false);
        lv_obj_set_hidden(_tempUnit, false);
    } else {
        lv_obj_set_hidden(_tempLabel, true);
        lv_obj_set_hidden(_tempUnit, true);
    }
    lv_obj_update_layout(_tempLabel);
    lv_obj_update_layout(_tempUnit);
    lv_obj_update_layout(_condLabel);
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
    const int stackY = tempY + (tempH - condH) / 2;
    const int infoX = lv_obj_get_x(_tempUnit) + lv_obj_get_width(_tempUnit) + TT_CAL_INFO_GAP_X;
    int infoW = sideW - infoX - TT_CAL_SIDE_PAD;
    if (infoW < 24) {
        infoW = 24;
    }
    lv_obj_set_style_max_width(_condLabel, infoW, 0);
    lv_obj_set_pos(_condLabel, infoX, stackY);

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

void TTPictorialPage::updateClock(bool refreshIfChanged) {
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
    lv_label_set_text(_weekLabel, weekEn(local.tm_wday));
    layoutSide();
    if (refreshIfChanged && _visible) {
        requestRefresh(TT_REFRESH_PARTIAL);
    }
}

void TTPictorialPage::showArt(const char* path) {
    if (_art == nullptr || path == nullptr || path[0] == '\0') {
        return;
    }
    char dropped[TT_STREAM_IMAGE_PATH_MAX];
    dropped[0] = '\0';
    if (path != _artPath && _artPath[0] != '\0' && strcmp(path, _artPath) != 0) {
        strncpy(dropped, _artPath, sizeof(dropped) - 1);
        dropped[sizeof(dropped) - 1] = '\0';
    }
    if (path != _artPath) {
        strncpy(_artPath, path, sizeof(_artPath) - 1);
        _artPath[sizeof(_artPath) - 1] = '\0';
    }
    tt_stream_image_set_src(_art, _artPath);
    if (dropped[0] != '\0') {
        LOG_I("Pictorial page: drop %s", dropped);
        tt_file_remove(dropped);
    }
    lv_obj_update_layout(_art);
    const int paneW = EPD_WIDTH - TT_CAL_SIDE_W;
    const int paneH = EPD_HEIGHT - TT_NAV_PAGE_INSET;
    const int w = lv_obj_get_width(_art);
    const int h = lv_obj_get_height(_art);
    int x = TT_CAL_SIDE_W + (paneW - w) / 2;
    int y = (paneH - h) / 2;
    if (x < TT_CAL_SIDE_W) {
        x = TT_CAL_SIDE_W;
    }
    if (y < 0) {
        y = 0;
    }
    lv_obj_set_pos(_art, x, y);
    lv_obj_set_hidden(_art, false);
    if (_status != nullptr && !_picking) {
        lv_obj_set_hidden(_status, true);
    }
    if (_hit != nullptr) {
        lv_obj_move_foreground(_hit);
    }
}

void TTPictorialPage::scheduleDaySwitch() {
    if (_dayHandle != 0) {
        cancelRepeat(_dayHandle);
        _dayHandle = 0;
    }
    const uint32_t delayMs = msUntilDaySwitch();
    LOG_I("Pictorial page: day switch in %u ms", (unsigned)delayMs);
    _dayHandle = runOnceWall(delayMs, [this]() {
        _dayHandle = 0;
        LOG_I("Pictorial page: midnight switch");
        TTInstanceOf<TTPictorialService>().requestDaily();
        if (_visible) {
            scheduleDaySwitch();
        }
    });
}

void TTPictorialPage::openPicker() {
    auto& service = TTInstanceOf<TTPictorialService>();
    if (service.busy()) {
        LOG_I("Pictorial page: picker ignored, busy");
        return;
    }
    cancelLightSleep();
    cancelSleepSettle();
    _sleepAfterTimeTick = false;
    cancelInputIdleSleep();
    service.requestManifest();
}

void TTPictorialPage::closePicker(bool apply) {
    const uint8_t index = _pickIndex;
    _picking = false;
    if (_pickPanel != nullptr) {
        lv_obj_set_hidden(_pickPanel, true);
    }
    if (_status != nullptr && _artReady) {
        lv_obj_set_hidden(_status, true);
    }
    if (!apply) {
        if (_visible) {
            requestRefresh(TT_REFRESH_PARTIAL);
        }
        tryRequestLightSleep();
        return;
    }
    LOG_I("Pictorial page: select series %u", (unsigned)index);
    TTInstanceOf<TTPictorialService>().requestSeries(index);
}

void TTPictorialPage::showPicker() {
    auto& service = TTInstanceOf<TTPictorialService>();
    const uint8_t count = service.seriesCount();
    if (count == 0 || _pickPanel == nullptr) {
        return;
    }
    if (_pickIndex >= count) {
        _pickIndex = 0;
    }
    uint8_t window = 0;
    if (count > TT_PIC_PICK_ROWS) {
        const int centered = (int)_pickIndex - TT_PIC_PICK_ROWS / 2;
        window = centered < 0 ? 0 : (uint8_t)centered;
        if (window + TT_PIC_PICK_ROWS > count) {
            window = (uint8_t)(count - TT_PIC_PICK_ROWS);
        }
    }
    for (int row = 0; row < TT_PIC_PICK_ROWS; row++) {
        const uint8_t index = (uint8_t)(window + row);
        if (index >= count) {
            lv_obj_set_hidden(_pickRows[row], true);
            continue;
        }
        TTPicSeries series = {};
        if (!service.seriesAt(index, &series)) {
            lv_obj_set_hidden(_pickRows[row], true);
            continue;
        }
        const bool selected = index == _pickIndex;
        const lv_color_t ink = selected ? lv_color_white() : lv_color_black();
        const lv_color_t paper = selected ? lv_color_black() : lv_color_white();
        lv_label_set_text(_pickNames[row], series.name);
        lv_obj_set_style_bg_color(_pickRows[row], paper, 0);
        lv_obj_set_style_text_color(_pickNames[row], ink, 0);
        lv_obj_set_hidden(_pickRows[row], false);
    }
    if (_pickOrdinal != nullptr) {
        char ordinal[16];
        snprintf(ordinal, sizeof(ordinal), "%u/%u", (unsigned)(_pickIndex + 1), (unsigned)count);
        lv_label_set_text(_pickOrdinal, ordinal);
    }
    if (_status != nullptr) {
        lv_obj_set_hidden(_status, true);
    }
    lv_obj_set_hidden(_pickPanel, false);
    lv_obj_move_foreground(_pickPanel);
    if (_hit != nullptr) {
        lv_obj_move_foreground(_hit);
    }
    LOG_I("Pictorial page: picker index=%u window=%u count=%u",
          (unsigned)_pickIndex, (unsigned)window, (unsigned)count);
}

void TTPictorialPage::armInputIdleSleep() {
    cancelInputIdleSleep();
    cancelSleepSettle();
    if (!_visible || _weatherFetching || _artFetching || _picking || !_artReady) {
        return;
    }
    _inputIdleSleepHandle = runOnce(TT_SLEEP_INPUT_IDLE_MS, [this]() {
        _inputIdleSleepHandle = 0;
        if (!_visible || _weatherFetching || _artFetching || _picking || !_artReady) {
            return;
        }
        requestLightSleep();
    });
    LOG_I("Pictorial page: sleep in %d s if idle", TT_SLEEP_INPUT_IDLE_MS / 1000);
}

void TTPictorialPage::cancelInputIdleSleep() {
    if (_inputIdleSleepHandle == 0) {
        return;
    }
    cancelRepeat(_inputIdleSleepHandle);
    _inputIdleSleepHandle = 0;
    LOG_I("Pictorial page: cancel idle sleep timer");
}

void TTPictorialPage::cancelSleepSettle() {
    if (_sleepSettleHandle == 0) {
        return;
    }
    cancelRepeat(_sleepSettleHandle);
    _sleepSettleHandle = 0;
}

void TTPictorialPage::tryRequestLightSleep(uint32_t delayMs) {
    if (!_visible || _weatherFetching || _artFetching || _picking || !_artReady) {
        cancelSleepSettle();
        if (_visible && !_weatherFetching && !_artFetching && !_picking && !_artReady) {
            LOG_I("Pictorial page: skip sleep, no art");
        }
        return;
    }
    cancelSleepSettle();
    LOG_I("Pictorial page: sleep in %u ms", (unsigned)delayMs);
    _sleepSettleHandle = runOnce(delayMs, [this]() {
        _sleepSettleHandle = 0;
        if (!_visible || _weatherFetching || _artFetching || _picking || !_artReady) {
            return;
        }
        requestLightSleep();
    });
}
