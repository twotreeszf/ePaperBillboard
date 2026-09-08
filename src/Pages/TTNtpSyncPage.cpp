#include "TTNtpSyncPage.h"
#include "TTWiFiConfigPage.h"
#include "../Base/TTFontManager.h"
#include "../Base/TTInstance.h"
#include "../Base/TTPopupLayer.h"
#include "../Base/TTTextButton.h"
#include "../Base/Logger.h"
#include "../Tasks/TTWiFiTask.h"
#include <memory>

lv_obj_t* TTNtpSyncPage::createStatusRow(lv_obj_t* parent, lv_font_t* font, const char* title, lv_obj_t** valueOut) {
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_layout(row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(row, 8, 0);

    lv_obj_t* titleLabel = lv_label_create(row);
    lv_label_set_text(titleLabel, title);
    lv_obj_set_style_text_color(titleLabel, lv_color_black(), 0);
    lv_obj_set_style_text_font(titleLabel, font, 0);
    lv_obj_set_style_text_align(titleLabel, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_width(titleLabel, TT_NTP_TITLE_W);

    lv_obj_t* value = lv_label_create(row);
    lv_label_set_text(value, "--");
    lv_obj_set_style_text_color(value, lv_color_black(), 0);
    lv_obj_set_style_text_font(value, font, 0);
    lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_long_mode(value, LV_LABEL_LONG_DOT);
    lv_obj_set_flex_grow(value, 1);
    *valueOut = value;
    return row;
}

void TTNtpSyncPage::buildContent(lv_obj_t* screen) {
    TTFontManager& fm = TTFontManager::instance();
    lv_font_t* font16 = fm.getFont(16);
    lv_font_t* font12 = fm.getFont(12);

    lv_obj_set_style_bg_color(screen, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    lv_obj_t* statusBox = lv_obj_create(screen);
    lv_obj_set_size(statusBox, lv_pct(88), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(statusBox, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(statusBox, 0, 0);
    lv_obj_set_style_pad_all(statusBox, 0, 0);
    lv_obj_set_layout(statusBox, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(statusBox, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(statusBox, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(statusBox, TT_NTP_ROW_GAP, 0);
    lv_obj_align(statusBox, LV_ALIGN_TOP_LEFT, TT_NTP_STATUS_LEFT, TT_NTP_STATUS_TOP);

    createStatusRow(statusBox, font16, "时间", &_timeValue);
    createStatusRow(statusBox, font16, "时区", &_tzValue);

    _statusLabel = lv_label_create(screen);
    lv_label_set_text(_statusLabel, "");
    lv_obj_set_style_text_color(_statusLabel, lv_color_black(), 0);
    lv_obj_set_style_text_font(_statusLabel, font12, 0);
    lv_obj_set_width(_statusLabel, lv_pct(88));
    lv_obj_set_style_text_align(_statusLabel, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_align_to(_statusLabel, statusBox, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 10);

    _btnRow = lv_obj_create(screen);
    lv_obj_set_size(_btnRow, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(_btnRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(_btnRow, 0, 0);
    lv_obj_set_style_pad_all(_btnRow, 0, 0);
    lv_obj_set_layout(_btnRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(_btnRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(_btnRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(_btnRow, TT_NTP_BTN_GAP, 0);
    lv_obj_align(_btnRow, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_add_flag(_btnRow, LV_OBJ_FLAG_HIDDEN);

    _retryBtn = TTTextButton::create(_btnRow, "重新校时", font16, TT_NTP_BTN_W);
    lv_obj_add_event_cb(_retryBtn, onRetryEvent, LV_EVENT_CLICKED, this);
    addToFocusGroup(_retryBtn);

    _backBtn = TTTextButton::create(_btnRow, "返回", font16, TT_NTP_BTN_W);
    lv_obj_add_event_cb(_backBtn, onBackEvent, LV_EVENT_CLICKED, this);
    addToFocusGroup(_backBtn);
}

void TTNtpSyncPage::setup() {
    TTScreenPage::setup();
    subscribe<TTTimeSyncPayload>(
        TT_NOTIFICATION_TIME_SYNC,
        [this](const TTTimeSyncPayload& status) {
            applyStatus(status);
            requestRefresh(TT_REFRESH_PARTIAL);
        });
}

void TTNtpSyncPage::willAppear() {
    TTScreenPage::willAppear();
    _visible = true;
    startSync();
}

void TTNtpSyncPage::willDisappear() {
    TTScreenPage::willDisappear();
    _visible = false;
    dismissSyncLoading();
}

void TTNtpSyncPage::showSyncLoading(const char* text) {
    if (_loading) {
        TTInstanceOf<TTPopupLayer>().updateLoading(text);
        return;
    }
    _loading = true;
    LOG_I("NTP page: show loading %s", text != nullptr ? text : "");
    TTInstanceOf<TTPopupLayer>().showLoading(text);
}

void TTNtpSyncPage::dismissSyncLoading() {
    if (!_loading) {
        return;
    }
    _loading = false;
    TTInstanceOf<TTPopupLayer>().dismissLoading();
}

void TTNtpSyncPage::applyTimeRows(const TTTimeSyncPayload& status) {
    if (_timeValue != nullptr) {
        lv_label_set_text(_timeValue, status.timeText[0] != '\0' ? status.timeText : "--");
    }
    if (_tzValue != nullptr) {
        lv_label_set_text(_tzValue, status.timezone[0] != '\0' ? status.timezone : "未设置");
    }
}

void TTNtpSyncPage::hideButtons() {
    if (_btnRow != nullptr) {
        lv_obj_add_flag(_btnRow, LV_OBJ_FLAG_HIDDEN);
    }
}

void TTNtpSyncPage::showActionOnly() {
    if (_btnRow != nullptr) {
        lv_obj_remove_flag(_btnRow, LV_OBJ_FLAG_HIDDEN);
    }
    if (_backBtn != nullptr) {
        lv_obj_add_flag(_backBtn, LV_OBJ_FLAG_HIDDEN);
    }
    if (_retryBtn != nullptr) {
        lv_obj_remove_flag(_retryBtn, LV_OBJ_FLAG_HIDDEN);
        if (_group != nullptr) {
            lv_group_focus_obj(_retryBtn);
        }
    }
}

void TTNtpSyncPage::showDoneButtons() {
    if (_btnRow != nullptr) {
        lv_obj_remove_flag(_btnRow, LV_OBJ_FLAG_HIDDEN);
    }
    if (_retryBtn != nullptr) {
        TTTextButton::setText(_retryBtn, "重新校时");
        lv_obj_remove_flag(_retryBtn, LV_OBJ_FLAG_HIDDEN);
    }
    if (_backBtn != nullptr) {
        lv_obj_remove_flag(_backBtn, LV_OBJ_FLAG_HIDDEN);
        if (_group != nullptr) {
            lv_group_focus_obj(_backBtn);
        }
    }
}

void TTNtpSyncPage::applyStatus(const TTTimeSyncPayload& status) {
    if (!_visible) {
        return;
    }
    if (status.state != TT_TIME_SYNC_NEED_WIFI
        && status.state != TT_TIME_SYNC_SYNCING
        && status.state != TT_TIME_SYNC_OK
        && status.state != TT_TIME_SYNC_FAILED) {
        return;
    }
    _state = status.state;
    applyTimeRows(status);

    if (status.state == TT_TIME_SYNC_NEED_WIFI) {
        dismissSyncLoading();
        lv_label_set_text(_statusLabel, "未连接 Wi-Fi，请先完成 Web 设置。");
        TTTextButton::setText(_retryBtn, "去 Web 设置");
        showActionOnly();
        return;
    }

    if (status.state == TT_TIME_SYNC_SYNCING) {
        showSyncLoading(status.message[0] != '\0' ? status.message : "正在校时...");
        lv_label_set_text(_statusLabel, "");
        hideButtons();
        return;
    }

    dismissSyncLoading();
    if (status.state == TT_TIME_SYNC_OK) {
        lv_label_set_text(_statusLabel, "校时完成");
        showDoneButtons();
        return;
    }

    lv_label_set_text(_statusLabel, "校时失败，请确认 Wi-Fi 后重试。");
    TTTextButton::setText(_retryBtn, "重试");
    showActionOnly();
}

void TTNtpSyncPage::startSync() {
    LOG_I("NTP page: start");
    lv_label_set_text(_statusLabel, "");
    hideButtons();
    showSyncLoading("正在校时...");
    TTInstanceOf<TTWiFiTask>().requestNtpSyncAsync();
}

void TTNtpSyncPage::goWifiSettings() {
    getNavigationController()->pushPage(std::unique_ptr<TTScreenPage>(new TTWiFiConfigPage()));
}

void TTNtpSyncPage::goBack() {
    if (getNavigationController() != nullptr) {
        getNavigationController()->pop();
    }
}

void TTNtpSyncPage::onRetryEvent(lv_event_t* e) {
    TTNtpSyncPage* self = (TTNtpSyncPage*)lv_event_get_user_data(e);
    if (self == nullptr) {
        return;
    }
    if (self->_state == TT_TIME_SYNC_NEED_WIFI) {
        self->goWifiSettings();
        return;
    }
    self->startSync();
}

void TTNtpSyncPage::onBackEvent(lv_event_t* e) {
    TTNtpSyncPage* self = (TTNtpSyncPage*)lv_event_get_user_data(e);
    if (self != nullptr) {
        self->goBack();
    }
}
