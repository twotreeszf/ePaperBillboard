#include "TTWiFiConfigPage.h"
#include "../Base/TTFontManager.h"
#include "../Base/TTInstance.h"
#include "../Base/TTPopupLayer.h"
#include "../Base/TTTextButton.h"
#include "../Base/TTQrCode.h"
#include "../Base/Logger.h"
#include "../Tasks/TTWiFiTask.h"
#include <cstdio>

void TTWiFiConfigPage::buildContent(lv_obj_t* screen) {
    TTFontManager& fm = TTFontManager::instance();
    lv_font_t* font16 = fm.getFont(16);
    lv_font_t* font12 = fm.getFont(12);

    lv_obj_set_style_bg_color(screen, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    _statusLabel = lv_label_create(screen);
    lv_label_set_text(_statusLabel, "正在启动 Web 设置...");
    lv_obj_set_style_text_color(_statusLabel, lv_color_black(), 0);
    lv_obj_set_style_text_font(_statusLabel, font16, 0);
    lv_obj_set_width(_statusLabel, lv_pct(100));
    lv_obj_set_style_text_align(_statusLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(_statusLabel, LV_ALIGN_TOP_MID, 0, 8);

    _stepsLabel = lv_label_create(screen);
    lv_label_set_text(_stepsLabel, "");
    lv_obj_set_style_text_color(_stepsLabel, lv_color_black(), 0);
    lv_obj_set_style_text_font(_stepsLabel, font12, 0);
    lv_obj_set_width(_stepsLabel, lv_pct(92));
    lv_obj_set_style_text_align(_stepsLabel, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_long_mode(_stepsLabel, LV_LABEL_LONG_WRAP);
    lv_obj_align(_stepsLabel, LV_ALIGN_TOP_MID, 0, 40);

    _qr = TTQrCode::create(screen);
    lv_obj_align(_qr, LV_ALIGN_RIGHT_MID, -TT_WIFI_QR_RIGHT_PAD, -8);

    _qrHint = lv_label_create(screen);
    lv_label_set_text(_qrHint, "扫码连热点");
    lv_obj_set_style_text_color(_qrHint, lv_color_black(), 0);
    lv_obj_set_style_text_font(_qrHint, font12, 0);
    lv_obj_align_to(_qrHint, _qr, LV_ALIGN_OUT_BOTTOM_MID, 0, TT_WIFI_QR_HINT_GAP);
    lv_obj_add_flag(_qrHint, LV_OBJ_FLAG_HIDDEN);

    _actionBtn = TTTextButton::create(screen, "取消配置", font16);
    lv_obj_align(_actionBtn, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_add_event_cb(_actionBtn, onActionEvent, LV_EVENT_CLICKED, this);
    addToFocusGroup(_actionBtn);
}

void TTWiFiConfigPage::setup() {
    TTScreenPage::setup();
    subscribe<TTWiFiStatusPayload>(
        TT_NOTIFICATION_WIFI_STATUS,
        [this](const TTWiFiStatusPayload& status) {
            applyStatus(status);
            requestRefresh(TT_REFRESH_PARTIAL);
        });
}

void TTWiFiConfigPage::willAppear() {
    TTScreenPage::willAppear();
    _visible = true;
    lv_label_set_text(_stepsLabel, "正在启动热点...");
    startProvisioningWithLoading();
}

void TTWiFiConfigPage::willDisappear() {
    TTScreenPage::willDisappear();
    _visible = false;
    dismissStartLoading();
    if (_state != TT_WIFI_LINK_CONNECTED) {
        LOG_I("WiFi page: leave, stop provisioning if active");
        TTInstanceOf<TTWiFiTask>().requestStopProvisioningAsync();
    }
}

void TTWiFiConfigPage::applyStatus(const TTWiFiStatusPayload& status) {
    if (!_visible) {
        return;
    }
    dismissStartLoading();
    _state = status.state;
    char line[384];
    if (status.state == TT_WIFI_LINK_CONNECTED) {
        LOG_I("WiFi page: config done, leave");
        hideProvisionQr();
        if (getNavigationController() != nullptr) {
            getNavigationController()->pop();
        }
        return;
    }
    if (status.state == TT_WIFI_LINK_CONNECTING) {
        snprintf(line, sizeof(line), "正在连接  %s", status.ssid);
        lv_label_set_text(_statusLabel, line);
        lv_label_set_text(_stepsLabel, "请稍候...");
        lv_obj_add_flag(_actionBtn, LV_OBJ_FLAG_HIDDEN);
        hideProvisionQr();
        return;
    }
    if (status.state == TT_WIFI_LINK_PROVISIONING) {
        lv_label_set_text(_statusLabel, "请用手机完成设置");
        snprintf(line, sizeof(line),
                 "1. 扫描右侧二维码连接热点\n    %s\n    （无需密码）\n\n"
                 "2. 页面通常会自动弹出\n    或用浏览器打开\n    %s\n\n"
                 "3. 设置 Wi-Fi 和时区\n    点击完成配置后将自动连接",
                 status.apSsid, status.portalUrl);
        lv_label_set_text(_stepsLabel, line);
        TTTextButton::setText(_actionBtn, "取消配置");
        lv_obj_remove_flag(_actionBtn, LV_OBJ_FLAG_HIDDEN);
        showProvisionQr(status.apSsid);
        return;
    }
    lv_label_set_text(_statusLabel, "连接失败");
    lv_label_set_text(_stepsLabel, "配置可能已保存，但未能连上 Wi-Fi。请检查名称和密码后重新配置。");
    TTTextButton::setText(_actionBtn, "重新配置");
    lv_obj_remove_flag(_actionBtn, LV_OBJ_FLAG_HIDDEN);
    hideProvisionQr();
}

void TTWiFiConfigPage::showProvisionQr(const char* apSsid) {
    if (_qr == nullptr || _stepsLabel == nullptr) {
        return;
    }
    if (!TTQrCode::setWifiOpen(_qr, apSsid)) {
        hideProvisionQr();
        return;
    }
    lv_obj_set_width(_stepsLabel, TT_WIFI_STEPS_W_WITH_QR);
    lv_obj_align(_stepsLabel, LV_ALIGN_TOP_LEFT, 8, 40);
    lv_obj_align(_qr, LV_ALIGN_RIGHT_MID, -TT_WIFI_QR_RIGHT_PAD, -8);
    lv_obj_remove_flag(_qr, LV_OBJ_FLAG_HIDDEN);
    if (_qrHint != nullptr) {
        lv_obj_align_to(_qrHint, _qr, LV_ALIGN_OUT_BOTTOM_MID, 0, TT_WIFI_QR_HINT_GAP);
        lv_obj_remove_flag(_qrHint, LV_OBJ_FLAG_HIDDEN);
    }
    LOG_I("WiFi page: show AP QR ssid=%s", apSsid != nullptr ? apSsid : "");
}

void TTWiFiConfigPage::hideProvisionQr() {
    if (_qr != nullptr) {
        lv_obj_add_flag(_qr, LV_OBJ_FLAG_HIDDEN);
    }
    if (_qrHint != nullptr) {
        lv_obj_add_flag(_qrHint, LV_OBJ_FLAG_HIDDEN);
    }
    if (_stepsLabel != nullptr) {
        lv_obj_set_width(_stepsLabel, lv_pct(92));
        lv_obj_align(_stepsLabel, LV_ALIGN_TOP_MID, 0, 40);
    }
}

void TTWiFiConfigPage::startProvisioningWithLoading() {
    if (_startLoading) {
        return;
    }
    _startLoading = true;
    LOG_I("WiFi page: start provisioning");
    TTInstanceOf<TTPopupLayer>().showLoading("正在启动热点...");
    TTInstanceOf<TTWiFiTask>().requestStartProvisioningAsync();
}

void TTWiFiConfigPage::dismissStartLoading() {
    if (!_startLoading) {
        return;
    }
    _startLoading = false;
    TTInstanceOf<TTPopupLayer>().dismissLoading();
}

void TTWiFiConfigPage::onActionClicked() {
    if (_startLoading) {
        return;
    }
    if (_state == TT_WIFI_LINK_PROVISIONING) {
        TTInstanceOf<TTWiFiTask>().requestStopProvisioningAsync();
        return;
    }
    startProvisioningWithLoading();
}

void TTWiFiConfigPage::onActionEvent(lv_event_t* e) {
    TTWiFiConfigPage* self = (TTWiFiConfigPage*)lv_event_get_user_data(e);
    if (self != nullptr) {
        self->onActionClicked();
    }
}
