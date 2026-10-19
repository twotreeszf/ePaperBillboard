#include "TTBookTransferPage.h"
#include "../Base/Logger.h"
#include "../Base/TTFontManager.h"
#include "../Base/TTInstance.h"
#include "../Base/TTPopupLayer.h"
#include "../Base/TTQrCode.h"
#include "../Service/TTSdCardService.h"
#include "../Tasks/TTWiFiTask.h"
#include <EPDConfig.h>
#include <cstdio>

#define TT_TRANSFER_PAGE_STATUS_W (EPD_WIDTH - 2 * TT_TRANSFER_PAGE_LEFT)

void TTBookTransferPage::buildContent(lv_obj_t* screen) {
    TTFontManager& fonts = TTFontManager::instance();
    lv_font_t* titleFont = fonts.getFont(TT_TRANSFER_PAGE_TITLE_FONT);
    lv_font_t* textFont = fonts.getFont(TT_TRANSFER_PAGE_TEXT_FONT);
    if (textFont == nullptr) {
        textFont = titleFont;
    }

    _title = lv_label_create(screen);
    lv_obj_set_style_text_font(_title, titleFont, 0);
    lv_obj_set_style_text_color(_title, lv_color_black(), 0);
    lv_obj_set_width(_title, lv_pct(100));
    lv_obj_set_style_text_align(_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(_title, TT_TRANSFER_TEXT_CONNECTING);
    lv_obj_align(_title, LV_ALIGN_TOP_MID, 0, TT_TRANSFER_PAGE_TOP);

    _steps = lv_label_create(screen);
    lv_obj_set_style_text_font(_steps, textFont, 0);
    lv_obj_set_style_text_color(_steps, lv_color_black(), 0);
    lv_obj_set_style_text_line_space(_steps, TT_TRANSFER_PAGE_LINE_SPACE, 0);
    lv_obj_set_width(_steps, EPD_WIDTH - TT_TRANSFER_PAGE_LEFT - TTQrCode::pixelSize()
                                 - TT_TRANSFER_PAGE_QR_RIGHT - TT_TRANSFER_PAGE_QR_GAP);
    lv_label_set_long_mode(_steps, LV_LABEL_LONG_WRAP);
    lv_label_set_text(_steps, "");
    lv_obj_set_pos(_steps, TT_TRANSFER_PAGE_LEFT, TT_TRANSFER_PAGE_STEPS_TOP);

    _qr = TTQrCode::create(screen);
    lv_obj_align(_qr, LV_ALIGN_RIGHT_MID, -TT_TRANSFER_PAGE_QR_RIGHT, 0);

    _status = lv_label_create(screen);
    lv_obj_set_style_text_font(_status, textFont, 0);
    lv_obj_set_style_text_color(_status, lv_color_black(), 0);
    lv_obj_set_width(_status, TT_TRANSFER_PAGE_STATUS_W);
    lv_label_set_long_mode(_status, LV_LABEL_LONG_DOT);
    lv_label_set_text(_status, "");
    lv_obj_align(_status, LV_ALIGN_BOTTOM_LEFT, TT_TRANSFER_PAGE_LEFT, -TT_TRANSFER_PAGE_STATUS_BOTTOM);

    createGroup();
}

void TTBookTransferPage::setup() {
    TTScreenPage::setup();
    subscribe<TTBookTransferPayload>(
        TT_NOTIFICATION_BOOK_TRANSFER,
        [this](const TTBookTransferPayload& status) {
            if (!_visible || _leaving) {
                return;
            }
            applyStatus(status);
            requestRefresh(TT_REFRESH_PARTIAL);
        });
}

void TTBookTransferPage::willAppear() {
    TTScreenPage::willAppear();
    _visible = true;
    if (!_started) {
        _started = true;
        LOG_I("Transfer page: request start");
        TTInstanceOf<TTWiFiTask>().requestStartBookTransferAsync();
    }
    _pollHandle = runRepeat(TT_SD_DETECT_POLL_MS, [this]() { pollCard(); }, false);
}

void TTBookTransferPage::willDisappear() {
    _visible = false;
    if (_pollHandle != 0) {
        cancelRepeat(_pollHandle);
        _pollHandle = 0;
    }
    if (_leaving && TTInstanceOf<TTPopupLayer>().isDialogShown()) {
        TTInstanceOf<TTPopupLayer>().dismissDialog();
    }
    LOG_I("Transfer page: leave, request stop");
    TTInstanceOf<TTWiFiTask>().requestStopBookTransferAsync();
    TTScreenPage::willDisappear();
}

void TTBookTransferPage::applyStatus(const TTBookTransferPayload& status) {
    switch (status.state) {
        case TT_BOOK_TRANSFER_CONNECTING:
            showConnecting(status.ssid);
            break;
        case TT_BOOK_TRANSFER_READY:
            showReady(status.ssid, status.url);
            break;
        case TT_BOOK_TRANSFER_RECEIVING:
        case TT_BOOK_TRANSFER_RECEIVED:
        case TT_BOOK_TRANSFER_UPLOAD_FAILED:
        case TT_BOOK_TRANSFER_DELETED:
            showUploadStatus(status);
            break;
        case TT_BOOK_TRANSFER_FAILED:
            leaveWithMessage(errorText(status.error));
            break;
        default:
            break;
    }
}

void TTBookTransferPage::showConnecting(const char* ssid) {
    char text[TT_TRANSFER_PAGE_TEXT_MAX];
    snprintf(text, sizeof(text), "正在连接  %s\n请稍候...", ssid);
    lv_label_set_text(_title, TT_TRANSFER_TEXT_CONNECTING);
    lv_label_set_text(_steps, text);
    lv_label_set_text(_status, "");
    lv_obj_set_hidden(_qr, true);
}

void TTBookTransferPage::showReady(const char* ssid, const char* url) {
    char text[TT_TRANSFER_PAGE_TEXT_MAX];
    snprintf(text, sizeof(text),
             "1. 手机或电脑连接同一个 Wi-Fi\n    %s\n\n"
             "2. 扫描右侧二维码\n    或用浏览器打开\n    %s\n\n"
             "3. 选择 TXT 或 MD 文件上传\n    请使用 UTF-8 编码",
             ssid, url);
    lv_label_set_text(_title, TT_TRANSFER_TEXT_READY);
    lv_label_set_text(_steps, text);
    lv_label_set_text(_status, TT_TRANSFER_TEXT_WAITING);
    const bool qrReady = TTQrCode::setText(_qr, url);
    lv_obj_set_hidden(_qr, !qrReady);
    LOG_I("Transfer page: ready ssid=%s url=%s qr=%d", ssid, url, qrReady ? 1 : 0);
}

void TTBookTransferPage::showUploadStatus(const TTBookTransferPayload& status) {
    char text[TT_TRANSFER_PAGE_STATUS_MAX];
    if (status.state == TT_BOOK_TRANSFER_RECEIVING) {
        snprintf(text, sizeof(text), "正在接收：%s", status.fileName);
    } else if (status.state == TT_BOOK_TRANSFER_RECEIVED) {
        snprintf(text, sizeof(text), "已收到 %u 本，最新：%s", (unsigned)status.receivedCount, status.fileName);
    } else if (status.state == TT_BOOK_TRANSFER_DELETED) {
        snprintf(text, sizeof(text), "已删除：%s", status.fileName);
    } else {
        snprintf(text, sizeof(text), "上传失败：%s（%s）", status.fileName, status.message);
    }
    lv_label_set_text(_status, text);
}

const char* TTBookTransferPage::errorText(TTBookTransferError error) const {
    switch (error) {
        case TT_BOOK_TRANSFER_ERROR_NO_NETWORK:
            return TT_TRANSFER_TEXT_NO_NETWORK;
        case TT_BOOK_TRANSFER_ERROR_BUSY:
            return TT_TRANSFER_TEXT_BUSY;
        case TT_BOOK_TRANSFER_ERROR_LINK_LOST:
            return TT_TRANSFER_TEXT_LINK_LOST;
        case TT_BOOK_TRANSFER_ERROR_SD_FAILED:
            return TT_TRANSFER_TEXT_SD_FAILED;
        case TT_BOOK_TRANSFER_ERROR_CONNECT_FAILED:
        default:
            return TT_TRANSFER_TEXT_CONNECT_FAILED;
    }
}

void TTBookTransferPage::pollCard() {
    if (_leaving || TTInstanceOf<TTSdCardService>().isCardInserted()) {
        return;
    }
    LOG_W("Transfer page: card removed");
    TTInstanceOf<TTWiFiTask>().requestStopBookTransferAsync();
    leaveWithMessage(TT_TRANSFER_TEXT_CARD_REMOVED);
}

void TTBookTransferPage::leaveWithMessage(const char* msg) {
    if (_leaving) {
        return;
    }
    _leaving = true;
    LOG_W("Transfer page: leave with message: %s", msg);
    if (_pollHandle != 0) {
        cancelRepeat(_pollHandle);
        _pollHandle = 0;
    }
    lv_obj_set_hidden(_qr, true);
    TTInstanceOf<TTPopupLayer>().showConfirm(msg, [this]() {
        runOnce(0, [this]() {
            ITTNavigationController* nav = getNavigationController();
            if (nav != nullptr && nav->canPop()) {
                nav->pop();
            }
        });
    });
}
