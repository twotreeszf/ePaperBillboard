#pragma once

#include <lvgl.h>
#include "../Base/TTScreenPage.h"
#include "../Models/TTNotificationPayloads.h"

#define TT_TRANSFER_PAGE_LEFT          8
#define TT_TRANSFER_PAGE_TOP           8
#define TT_TRANSFER_PAGE_QR_RIGHT      8
#define TT_TRANSFER_PAGE_QR_GAP        8
#define TT_TRANSFER_PAGE_STEPS_TOP     52
#define TT_TRANSFER_PAGE_STATUS_BOTTOM 8
#define TT_TRANSFER_PAGE_LINE_SPACE    4
#define TT_TRANSFER_PAGE_TEXT_MAX      384
#define TT_TRANSFER_PAGE_STATUS_MAX    192
#define TT_TRANSFER_PAGE_TITLE_FONT    16
#define TT_TRANSFER_PAGE_TEXT_FONT     12

#define TT_TRANSFER_TEXT_CONNECTING      "正在连接 Wi-Fi"
#define TT_TRANSFER_TEXT_READY           "请用手机或电脑上传电子书"
#define TT_TRANSFER_TEXT_WAITING         "等待上传..."
#define TT_TRANSFER_TEXT_RECEIVING       "正在接收：%s"
#define TT_TRANSFER_TEXT_RECEIVING_IMAGE "正在接收图片：%s"
#define TT_TRANSFER_TEXT_NO_NETWORK      "尚未保存 Wi-Fi\n请先在 设置 > Web 设置 中配置"
#define TT_TRANSFER_TEXT_BUSY            "正在进行 Web 设置\n请稍后再试"
#define TT_TRANSFER_TEXT_CONNECT_FAILED  "无法连接已保存的 Wi-Fi\n请确认网络可用后重试"
#define TT_TRANSFER_TEXT_LINK_LOST       "Wi-Fi 连接已断开"
#define TT_TRANSFER_TEXT_SD_FAILED       "无法读取 SD 卡"
#define TT_TRANSFER_TEXT_CARD_REMOVED    "SD 卡已拔出"

class TTBookTransferPage : public TTScreenPage {
public:
    TTBookTransferPage() : TTScreenPage("传书") {}

    void setup() override;
    void willAppear() override;
    void willDisappear() override;

protected:
    void buildContent(lv_obj_t* screen) override;

private:
    void applyStatus(const TTBookTransferPayload& status);
    void showConnecting(const char* ssid);
    void showReady(const char* ssid, const char* url);
    void showUploadStatus(const TTBookTransferPayload& status);
    const char* errorText(TTBookTransferError error) const;
    void pollCard();
    void leaveWithMessage(const char* msg);

    lv_obj_t* _title = nullptr;
    lv_obj_t* _steps = nullptr;
    lv_obj_t* _status = nullptr;
    lv_obj_t* _qr = nullptr;
    uint32_t _pollHandle = 0;
    bool _visible = false;
    bool _started = false;
    bool _leaving = false;
};
