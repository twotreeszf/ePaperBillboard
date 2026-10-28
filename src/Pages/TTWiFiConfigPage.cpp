#include "TTWiFiConfigPage.h"
#include "../Base/TTFontManager.h"
#include "../Base/TTInstance.h"
#include "../Base/TTPopupLayer.h"
#include "../Base/TTTextButton.h"
#include "../Base/TTQrCode.h"
#include "../Base/TTRtc.h"
#include "../Base/TTNavigationBar.h"
#include "../Base/TTStreamImage.h"
#include "../Base/Logger.h"
#include "../Tasks/TTWiFiTask.h"
#include <EPDConfig.h>
#include <cstdio>
#include <memory>

void TTWiFiConfigPage::buildContent(lv_obj_t* screen) {
    TTFontManager& fm = TTFontManager::instance();
    lv_font_t* font16 = fm.getFont(16);
    lv_font_t* font12 = fm.getFont(12);

    lv_obj_set_style_bg_color(screen, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    _statusLabel = lv_label_create(screen);
    lv_label_set_text(_statusLabel, _kind == TT_PORTAL_PAGE_WEB ? "正在打开 Web 设置..." : "正在启动联网设置...");
    lv_obj_set_style_text_color(_statusLabel, lv_color_black(), 0);
    lv_obj_set_style_text_font(_statusLabel, font16, 0);
    lv_obj_set_width(_statusLabel, lv_pct(100));
    lv_obj_set_style_text_align(_statusLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(_statusLabel, LV_ALIGN_TOP_MID, 0, 8);

    _titleRow = lv_obj_create(screen);
    lv_obj_set_size(_titleRow, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(_titleRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(_titleRow, 0, 0);
    lv_obj_set_style_pad_all(_titleRow, 0, 0);
    lv_obj_set_layout(_titleRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(_titleRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(_titleRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(_titleRow, TT_WIFI_CONNECTED_ICON_GAP, 0);
    lv_obj_align(_titleRow, LV_ALIGN_TOP_MID, 0, 8);
    lv_obj_t* connectedIcon = tt_stream_image_create(_titleRow);
    tt_stream_image_set_src(connectedIcon, TT_NAV_ICON_WIFI_ON);
    lv_obj_set_size(connectedIcon, TT_NAV_WIFI_ICON_W, TT_NAV_WIFI_ICON_H);
    lv_obj_t* connectedLabel = lv_label_create(_titleRow);
    lv_label_set_text(connectedLabel, "已连接");
    lv_obj_set_style_text_color(connectedLabel, lv_color_black(), 0);
    lv_obj_set_style_text_font(connectedLabel, font16, 0);
    lv_obj_set_hidden(_titleRow, true);

    _stepsLabel = lv_label_create(screen);
    lv_label_set_text(_stepsLabel, "");
    lv_obj_set_style_text_color(_stepsLabel, lv_color_black(), 0);
    lv_obj_set_style_text_font(_stepsLabel, font12, 0);
    lv_obj_set_width(_stepsLabel, lv_pct(92));
    lv_obj_set_style_text_align(_stepsLabel, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_long_mode(_stepsLabel, LV_LABEL_LONG_WRAP);
    lv_obj_align(_stepsLabel, LV_ALIGN_TOP_MID, 0, 40);

    _infoBox = lv_obj_create(screen);
    lv_obj_set_width(_infoBox, EPD_WIDTH - TT_WIFI_INFO_SIDE * 2);
    lv_obj_set_height(_infoBox, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(_infoBox, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(_infoBox, 0, 0);
    lv_obj_set_style_pad_all(_infoBox, 0, 0);
    lv_obj_set_layout(_infoBox, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(_infoBox, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(_infoBox, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(_infoBox, TT_WIFI_INFO_SECTION_GAP, 0);
    lv_obj_align(_infoBox, LV_ALIGN_TOP_LEFT, TT_WIFI_INFO_SIDE, TT_WIFI_INFO_TOP);
    createInfoSection(_infoBox, font16, font12, "WiFi名称", &_ssidValue);
    createInfoSection(_infoBox, font16, font12, "IP地址", &_ipValue);
    createInfoSection(_infoBox, font16, font12, "时区", &_tzValue);
    lv_obj_set_hidden(_infoBox, true);

    _qr = TTQrCode::create(screen);
    lv_obj_align(_qr, LV_ALIGN_RIGHT_MID, -TT_WIFI_QR_RIGHT_PAD, -8);

    _qrHint = lv_label_create(screen);
    lv_label_set_text(_qrHint, "扫码打开设置");
    lv_obj_set_style_text_color(_qrHint, lv_color_black(), 0);
    lv_obj_set_style_text_font(_qrHint, font12, 0);
    lv_obj_align_to(_qrHint, _qr, LV_ALIGN_OUT_BOTTOM_MID, 0, TT_WIFI_QR_HINT_GAP);
    lv_obj_set_hidden(_qrHint, true);

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
            if (_visible) {
                requestRefresh(TT_REFRESH_PARTIAL);
            }
        });
}

void TTWiFiConfigPage::willAppear() {
    TTScreenPage::willAppear();
    _visible = true;
    if (_kind == TT_PORTAL_PAGE_WEB) {
        lv_label_set_text(_stepsLabel, "正在连接...");
        startLanPortalWithLoading();
        return;
    }
    lv_label_set_text(_stepsLabel, "正在启动热点...");
    startProvisioningWithLoading();
}

void TTWiFiConfigPage::willDisappear() {
    TTScreenPage::willDisappear();
    _visible = false;
    dismissStartLoading();
    if (_kind == TT_PORTAL_PAGE_WEB) {
        LOG_I("WiFi page: leave web settings");
        TTInstanceOf<TTWiFiTask>().requestStopLanPortalAsync();
        return;
    }
    LOG_I("WiFi page: leave, radio off");
    TTInstanceOf<TTWiFiTask>().requestRadioOffAsync();
}

void TTWiFiConfigPage::applyStatus(const TTWiFiStatusPayload& status) {
    if (!_visible) {
        return;
    }
    dismissStartLoading();
    _state = status.state;
    if (_kind == TT_PORTAL_PAGE_WEB) {
        applyLanStatus(status);
        return;
    }
    char line[384];
    if (status.state == TT_WIFI_LINK_CONNECTED) {
        char tz[TT_TZ_TEXT_MAX + 1];
        const char* tzText = "未设置";
        if (TTRtc::loadTimezoneLabel(tz, sizeof(tz)) || TTRtc::loadTimezone(tz, sizeof(tz))) {
            tzText = tz;
        }
        lv_obj_set_hidden(_statusLabel, true);
        lv_obj_set_hidden(_titleRow, false);
        lv_label_set_text(_ssidValue, status.ssid[0] != '\0' ? status.ssid : "--");
        lv_label_set_text(_ipValue, status.ip[0] != '\0' ? status.ip : "--");
        lv_label_set_text(_tzValue, tzText);
        lv_obj_set_hidden(_stepsLabel, true);
        lv_obj_set_hidden(_infoBox, false);
        hideProvisionQr();
        lv_obj_set_hidden(_actionBtn, true);
        LOG_I("WiFi page: connected ssid=%s ip=%s tz=%s", status.ssid, status.ip, tzText);
        return;
    }
    lv_obj_set_hidden(_titleRow, true);
    lv_obj_set_hidden(_statusLabel, false);
    lv_obj_set_hidden(_infoBox, true);
    lv_obj_set_hidden(_stepsLabel, false);
    lv_obj_set_style_text_font(_stepsLabel, TTFontManager::instance().getFont(12), 0);
    if (status.state == TT_WIFI_LINK_CONNECTING) {
        snprintf(line, sizeof(line), "正在连接  %s", status.ssid);
        lv_label_set_text(_statusLabel, line);
        lv_label_set_text(_stepsLabel, "请稍候...");
        lv_obj_set_hidden(_actionBtn, true);
        hideProvisionQr();
        return;
    }
    if (status.state == TT_WIFI_LINK_PROVISIONING) {
        lv_label_set_text(_statusLabel, "请用手机完成设置");
        snprintf(line, sizeof(line),
                 "1. 手机连接热点\n    %s\n    （无需密码）\n\n"
                 "2. 扫描右侧二维码\n    或用浏览器打开\n    %s\n\n"
                 "3. 设置 Wi-Fi 和时区\n    点击完成配置后将自动连接",
                 status.apSsid, status.portalUrl);
        lv_label_set_text(_stepsLabel, line);
        TTTextButton::setText(_actionBtn, "取消配置");
        lv_obj_set_hidden(_actionBtn, false);
        showProvisionQr(status.portalUrl);
        return;
    }
    lv_label_set_text(_statusLabel, "连接失败");
    lv_label_set_text(_stepsLabel, "配置可能已保存，但未能连上 Wi-Fi。请检查名称和密码后重新配置。");
    TTTextButton::setText(_actionBtn, "重新配置");
    lv_obj_set_hidden(_actionBtn, false);
    hideProvisionQr();
}

void TTWiFiConfigPage::applyLanStatus(const TTWiFiStatusPayload& status) {
    char line[384];
    if (status.state == TT_WIFI_LINK_CONNECTED && status.portalUrl[0] != '\0') {
        lv_label_set_text(_statusLabel, "请用手机完成设置");
        snprintf(line, sizeof(line),
                 "1. 手机或电脑连接同一个 Wi-Fi\n    %s\n\n"
                 "2. 扫描右侧二维码\n    或用浏览器打开\n    %s\n\n"
                 "3. 设置天气、日历和硬件\n    完成后可关闭页面",
                 status.ssid, status.portalUrl);
        lv_label_set_text(_stepsLabel, line);
        TTTextButton::setText(_actionBtn, "关闭");
        lv_obj_set_hidden(_actionBtn, false);
        showProvisionQr(status.portalUrl);
        return;
    }
    hideProvisionQr();
    if (status.state == TT_WIFI_LINK_PROVISIONING) {
        lv_label_set_text(_statusLabel, "热点占用中");
        lv_label_set_text(_stepsLabel, "正在进行联网设置，请先退出后再打开 Web 设置。");
        TTTextButton::setText(_actionBtn, "关闭");
        lv_obj_set_hidden(_actionBtn, false);
        return;
    }
    if (status.state == TT_WIFI_LINK_CONNECTING
        || (status.state == TT_WIFI_LINK_CONNECTED && !TTInstanceOf<TTWiFiTask>().isBookTransferActive())) {
        lv_label_set_text(_statusLabel, "正在连接 Wi-Fi");
        lv_label_set_text(_stepsLabel, "请稍候...");
        lv_obj_set_hidden(_actionBtn, true);
        return;
    }
    if (status.state == TT_WIFI_LINK_CONNECTED) {
        lv_label_set_text(_statusLabel, "暂时无法打开");
        lv_label_set_text(_stepsLabel, "传书正在使用网络，请退出后再打开 Web 设置。");
        TTTextButton::setText(_actionBtn, "关闭");
        lv_obj_set_hidden(_actionBtn, false);
        return;
    }
    lv_label_set_text(_statusLabel, "未连接 Wi-Fi");
    lv_label_set_text(_stepsLabel, "请先在联网设置中连接网络。");
    TTTextButton::setText(_actionBtn, "去联网设置");
    lv_obj_set_hidden(_actionBtn, false);
}

void TTWiFiConfigPage::createInfoSection(lv_obj_t* parent, lv_font_t* captionFont, lv_font_t* valueFont,
                                          const char* caption, lv_obj_t** valueOut) {
    lv_obj_t* section = lv_obj_create(parent);
    lv_obj_set_width(section, lv_pct(100));
    lv_obj_set_height(section, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(section, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(section, 0, 0);
    lv_obj_set_style_pad_all(section, 0, 0);
    lv_obj_set_layout(section, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(section, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(section, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(section, TT_WIFI_INFO_LINE_GAP, 0);

    lv_obj_t* captionLabel = lv_label_create(section);
    lv_label_set_text(captionLabel, caption);
    lv_obj_set_style_text_color(captionLabel, lv_color_black(), 0);
    lv_obj_set_style_text_font(captionLabel, captionFont, 0);

    lv_obj_t* value = lv_label_create(section);
    lv_label_set_text(value, "--");
    lv_obj_set_style_text_color(value, lv_color_black(), 0);
    lv_obj_set_style_text_font(value, valueFont, 0);
    lv_obj_set_width(value, lv_pct(100));
    lv_label_set_long_mode(value, LV_LABEL_LONG_DOT);
    *valueOut = value;
}

void TTWiFiConfigPage::showProvisionQr(const char* url) {
    if (_qr == nullptr || _stepsLabel == nullptr) {
        return;
    }
    if (!TTQrCode::setText(_qr, url)) {
        hideProvisionQr();
        return;
    }
    lv_obj_set_width(_stepsLabel, TT_WIFI_STEPS_W_WITH_QR);
    lv_obj_align(_stepsLabel, LV_ALIGN_TOP_LEFT, 8, 40);
    lv_obj_align(_qr, LV_ALIGN_RIGHT_MID, -TT_WIFI_QR_RIGHT_PAD, -8);
    lv_obj_set_hidden(_qr, false);
    if (_qrHint != nullptr) {
        lv_obj_align_to(_qrHint, _qr, LV_ALIGN_OUT_BOTTOM_MID, 0, TT_WIFI_QR_HINT_GAP);
        lv_obj_set_hidden(_qrHint, false);
    }
    LOG_I("WiFi page: show portal QR url=%s", url != nullptr ? url : "");
}

void TTWiFiConfigPage::hideProvisionQr() {
    if (_qr != nullptr) {
        lv_obj_set_hidden(_qr, true);
    }
    if (_qrHint != nullptr) {
        lv_obj_set_hidden(_qrHint, true);
    }
    if (_stepsLabel != nullptr) {
        lv_obj_set_width(_stepsLabel, lv_pct(92));
        lv_obj_align(_stepsLabel, LV_ALIGN_TOP_MID, 0, 40);
    }
}

void TTWiFiConfigPage::startLanPortalWithLoading() {
    if (_startLoading) {
        return;
    }
    _startLoading = true;
    LOG_I("WiFi page: start lan portal");
    TTInstanceOf<TTPopupLayer>().showLoading("正在打开 Web 设置...");
    TTInstanceOf<TTWiFiTask>().requestStartLanPortalAsync();
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
    if (_kind == TT_PORTAL_PAGE_WEB) {
        if (_state != TT_WIFI_LINK_CONNECTED && _state != TT_WIFI_LINK_CONNECTING
            && _state != TT_WIFI_LINK_PROVISIONING) {
            LOG_I("WiFi page: open network settings");
            getNavigationController()->pushPage(
                std::unique_ptr<TTScreenPage>(new TTWiFiConfigPage(TT_PORTAL_PAGE_NETWORK)));
            return;
        }
        if (getNavigationController() != nullptr) {
            getNavigationController()->pop();
        }
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
