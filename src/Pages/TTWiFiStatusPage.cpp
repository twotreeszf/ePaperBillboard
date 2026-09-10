#include "TTWiFiStatusPage.h"
#include "TTWiFiConfigPage.h"
#include "../Base/TTFontManager.h"
#include "../Base/TTInstance.h"
#include "../Base/TTPopupLayer.h"
#include "../Base/TTTextButton.h"
#include "../Base/Logger.h"
#include "../Tasks/TTWiFiTask.h"
#include <memory>

lv_obj_t* TTWiFiStatusPage::createStatusRow(lv_obj_t* parent, lv_font_t* font, const char* title, lv_obj_t** valueOut) {
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
    lv_obj_set_width(titleLabel, TT_WIFI_STATUS_TITLE_W);

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

void TTWiFiStatusPage::buildContent(lv_obj_t* screen) {
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
    lv_obj_set_style_pad_row(statusBox, TT_WIFI_STATUS_ROW_GAP, 0);
    lv_obj_align(statusBox, LV_ALIGN_TOP_LEFT, TT_WIFI_STATUS_LEFT, TT_WIFI_STATUS_TOP);

    createStatusRow(statusBox, font16, "状态", &_stateValue);
    createStatusRow(statusBox, font16, "名称", &_ssidValue);
    createStatusRow(statusBox, font16, "地址", &_ipValue);

    _hintLabel = lv_label_create(screen);
    lv_label_set_text(_hintLabel, "");
    lv_obj_set_style_text_color(_hintLabel, lv_color_black(), 0);
    lv_obj_set_style_text_font(_hintLabel, font12, 0);
    lv_obj_set_width(_hintLabel, lv_pct(88));
    lv_obj_set_style_text_align(_hintLabel, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_align_to(_hintLabel, statusBox, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 10);

    _btnRow = lv_obj_create(screen);
    lv_obj_set_size(_btnRow, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(_btnRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(_btnRow, 0, 0);
    lv_obj_set_style_pad_all(_btnRow, 0, 0);
    lv_obj_set_layout(_btnRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(_btnRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(_btnRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(_btnRow, TT_WIFI_STATUS_BTN_GAP, 0);
    lv_obj_align(_btnRow, LV_ALIGN_BOTTOM_MID, 0, -10);

    _reconnectBtn = TTTextButton::create(_btnRow, "重连", font16, TT_WIFI_STATUS_BTN_W);
    lv_obj_add_event_cb(_reconnectBtn, onReconnectEvent, LV_EVENT_CLICKED, this);
    addToFocusGroup(_reconnectBtn);
    lv_obj_add_flag(_reconnectBtn, LV_OBJ_FLAG_HIDDEN);

    _actionBtn = TTTextButton::create(_btnRow, "去 Web 设置", font16, TT_WIFI_STATUS_BTN_W);
    lv_obj_add_event_cb(_actionBtn, onActionEvent, LV_EVENT_CLICKED, this);
    addToFocusGroup(_actionBtn);
    lv_obj_add_flag(_actionBtn, LV_OBJ_FLAG_HIDDEN);

    _backBtn = TTTextButton::create(_btnRow, "返回", font16, TT_WIFI_STATUS_BTN_W);
    lv_obj_add_event_cb(_backBtn, onBackEvent, LV_EVENT_CLICKED, this);
    addToFocusGroup(_backBtn);
}

void TTWiFiStatusPage::setup() {
    TTScreenPage::setup();
    subscribe<TTWiFiStatusPayload>(
        TT_NOTIFICATION_WIFI_STATUS,
        [this](const TTWiFiStatusPayload& status) {
            applyStatus(status);
            requestRefresh(TT_REFRESH_PARTIAL);
        });
}

void TTWiFiStatusPage::willAppear() {
    TTScreenPage::willAppear();
    _visible = true;
    showReadLoading("正在读取 Wi-Fi 状态...");
    TTInstanceOf<TTWiFiTask>().requestStatusAsync();
}

void TTWiFiStatusPage::willDisappear() {
    TTScreenPage::willDisappear();
    _visible = false;
    dismissReadLoading();
}

void TTWiFiStatusPage::showReadLoading(const char* text) {
    if (_loading) {
        TTInstanceOf<TTPopupLayer>().updateLoading(text);
        return;
    }
    _loading = true;
    LOG_I("WiFi status page: show loading %s", text != nullptr ? text : "");
    TTInstanceOf<TTPopupLayer>().showLoading(text);
}

void TTWiFiStatusPage::dismissReadLoading() {
    if (!_loading) {
        return;
    }
    _loading = false;
    TTInstanceOf<TTPopupLayer>().dismissLoading();
}

void TTWiFiStatusPage::applyStatus(const TTWiFiStatusPayload& status) {
    if (!_visible) {
        return;
    }
    dismissReadLoading();

    const char* stateText = "未连接";
    const char* hint = "尚未配置网络，可前往 Web 设置。";
    bool showWeb = true;
    bool showReconnect = false;
    if (status.state == TT_WIFI_LINK_CONNECTED) {
        stateText = "已连接";
        hint = "";
        showWeb = false;
    } else if (status.state == TT_WIFI_LINK_CONNECTING) {
        stateText = "正在连接";
        hint = "请稍候...";
        showWeb = false;
    } else if (status.state == TT_WIFI_LINK_PROVISIONING) {
        stateText = "配置中";
        hint = "正在进行 Web 设置。";
        showWeb = false;
    } else if (status.ssid[0] != '\0') {
        hint = "已保存网络，但未连上。可重连或前往 Web 设置。";
        showReconnect = true;
    }

    lv_label_set_text(_stateValue, stateText);
    lv_label_set_text(_ssidValue, status.ssid[0] != '\0' ? status.ssid : "--");
    lv_label_set_text(_ipValue, status.ip[0] != '\0' ? status.ip : "--");
    lv_label_set_text(_hintLabel, hint);

    if (showReconnect) {
        lv_obj_remove_flag(_reconnectBtn, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(_reconnectBtn, LV_OBJ_FLAG_HIDDEN);
    }
    if (showWeb) {
        lv_obj_remove_flag(_actionBtn, LV_OBJ_FLAG_HIDDEN);
        if (_group != nullptr) {
            lv_group_focus_obj(showReconnect ? _reconnectBtn : _actionBtn);
        }
    } else {
        lv_obj_add_flag(_actionBtn, LV_OBJ_FLAG_HIDDEN);
        if (_group != nullptr) {
            lv_group_focus_obj(_backBtn);
        }
    }
    LOG_I("WiFi status page: state=%d ssid=%s ip=%s", (int)status.state, status.ssid, status.ip);
}

void TTWiFiStatusPage::reconnect() {
    if (_loading) {
        return;
    }
    LOG_I("WiFi status page: reconnect");
    showReadLoading("正在连接...");
    TTInstanceOf<TTWiFiTask>().requestConnectAsync();
}

void TTWiFiStatusPage::goWebSettings() {
    getNavigationController()->pushPage(std::unique_ptr<TTScreenPage>(new TTWiFiConfigPage()));
}

void TTWiFiStatusPage::goBack() {
    if (getNavigationController() != nullptr) {
        getNavigationController()->pop();
    }
}

void TTWiFiStatusPage::onActionEvent(lv_event_t* e) {
    TTWiFiStatusPage* self = (TTWiFiStatusPage*)lv_event_get_user_data(e);
    if (self != nullptr) {
        self->goWebSettings();
    }
}

void TTWiFiStatusPage::onReconnectEvent(lv_event_t* e) {
    TTWiFiStatusPage* self = (TTWiFiStatusPage*)lv_event_get_user_data(e);
    if (self != nullptr) {
        self->reconnect();
    }
}

void TTWiFiStatusPage::onBackEvent(lv_event_t* e) {
    TTWiFiStatusPage* self = (TTWiFiStatusPage*)lv_event_get_user_data(e);
    if (self != nullptr) {
        self->goBack();
    }
}
