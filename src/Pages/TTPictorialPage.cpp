#include "TTPictorialPage.h"
#include "../Base/Logger.h"
#include "../Base/TTFile.h"
#include "../Base/TTFontManager.h"
#include "../Base/TTInstance.h"
#include "../Base/TTLvglEpdDriver.h"
#include "../Base/TTNavigationBar.h"
#include "../Base/TTPopupLayer.h"
#include "../Base/TTStreamImage.h"
#include "../Pages/TTCalendarPage.h"
#include "../Service/TTPictorialService.h"
#include "../Service/TTSleepService.h"
#include "../Service/TTWeatherService.h"
#include "../Tasks/TTSensorTask.h"
#include <EPDConfig.h>
#include <cstdio>
#include <cstring>

namespace {

lv_obj_t* createLabel(lv_obj_t* parent, const lv_font_t* font, lv_color_t color, const char* text) {
    lv_obj_t* label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_label_set_text(label, text != nullptr ? text : "");
    return label;
}

}  // namespace

void TTPictorialPage::buildContent(lv_obj_t* screen) {
    TTFontManager& fonts = TTFontManager::instance();
    lv_font_t* font16 = fonts.getFont(TT_CAL_TEXT_FONT);
    lv_font_t* font10 = fonts.getFont(TT_CAL_PAGE_FONT);
    if (font10 == nullptr) {
        font10 = font16;
    }
    const int height = EPD_HEIGHT - TT_NAV_PAGE_INSET;
    const int artW = EPD_WIDTH - TT_CAL_SIDE_W;
    _daySide.build(screen);

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
    subscribe<TTSchedulePayload>(TT_NOTIFICATION_SCHEDULE, [this](const TTSchedulePayload& event) {
        onSchedule(event);
    });
    registerKeyAction(TT_KEY_CENTER, TT_KEY_LONG_PRESS, [this]() {
        if (TTInstanceOf<TTPictorialService>().customOn()) {
            LOG_I("Pictorial page: custom mode ignores series picker");
            return;
        }
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
    if (_artFetching) {
        TTInstanceOf<TTPopupLayer>().showLoading(TT_PIC_UPDATING_TEXT);
    }
    requestWeather();
    TTInstanceOf<TTPictorialService>().requestToday();
    LOG_I("Pictorial page: request sensor update");
    TTInstanceOf<TTSensorTask>().requestSensorUpdateAsync();
}

void TTPictorialPage::willDisappear() {
    _visible = false;
    _dialArtChange = false;
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
        _dialArtChange = true;
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
    if (!_daySide.ready()) {
        tryRequestLightSleep(TT_SLEEP_AFTER_SYNC_MS);
        return;
    }
    if (payload.state != TT_WEATHER_OK) {
        layoutSide();
        LOG_I("Pictorial page: weather failed");
        if (_visible) {
            requestRefresh(TT_REFRESH_DEEP);
        }
        tryRequestLightSleep(TT_SLEEP_AFTER_SYNC_MS);
        return;
    }
    _daySide.setWeather(payload.current.weatherCode, payload.current.isDay, payload.current.temp);
    layoutSide();
    if (_visible) {
        requestRefresh(TT_REFRESH_FULL);
    }
    tryRequestLightSleep(TT_SLEEP_AFTER_SYNC_MS);
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
        if (_dialArtChange) {
            _dialArtChange = false;
            tryRequestLightSleep(TT_SLEEP_AFTER_REFRESH_MS);
        } else {
            tryRequestLightSleep();
        }
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
    const uint32_t sleepDelay = _dialArtChange ? TT_SLEEP_AFTER_REFRESH_MS : TT_SLEEP_AFTER_SYNC_MS;
    _dialArtChange = false;
    tryRequestLightSleep(sleepDelay);
}

void TTPictorialPage::onSleepWake(const TTSleepWakePayload& wake) {
    if (!_visible) {
        return;
    }
    LOG_I("Pictorial page: sleep wake reason=%d", (int)wake.reason);
    switch (wake.reason) {
        case TT_SLEEP_WAKE_POWER:
        case TT_SLEEP_WAKE_INPUT:
            _sleepAfterTimeTick = false;
            cancelInputIdleSleep();
            if (_weatherFetching || _artFetching || _picking) {
                break;
            }
            armInputIdleSleep();
            break;
        case TT_SLEEP_WAKE_TIME:
        case TT_SLEEP_WAKE_FETCH:
            break;
    }
}

void TTPictorialPage::onSchedule(const TTSchedulePayload& event) {
    if (!_visible) {
        return;
    }
    LOG_I("Pictorial page: schedule event=%d resume=%d", (int)event.event, event.resumeSleep ? 1 : 0);
    switch (event.event) {
        case TT_SCHEDULE_FETCH:
            requestWeather();
            TTInstanceOf<TTPictorialService>().requestExtend();
            break;
        case TT_SCHEDULE_TIME:
            if (_weatherFetching || _artFetching || _picking) {
                break;
            }
            updateClock(true);
            if (event.resumeSleep) {
                _sleepAfterTimeTick = true;
                LOG_I("Pictorial page: wait time tick then sleep");
            }
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
    if (sleepPending && !_artFetching) {
        tryRequestLightSleep(TT_SLEEP_AFTER_SYNC_MS);
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
    _daySide.layout();
}

void TTPictorialPage::updateClock(bool refreshIfChanged) {
    if (!_daySide.updateClock()) {
        return;
    }
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
    _dialArtChange = true;
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
    if (delayMs == 0) {
        requestLightSleep();
        return;
    }
    LOG_I("Pictorial page: sleep in %u ms", (unsigned)delayMs);
    _sleepSettleHandle = runOnce(delayMs, [this]() {
        _sleepSettleHandle = 0;
        if (!_visible || _weatherFetching || _artFetching || _picking || !_artReady) {
            return;
        }
        requestLightSleep();
    });
}
