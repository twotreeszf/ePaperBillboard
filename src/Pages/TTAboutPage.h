#pragma once

#include "../Base/TTFile.h"
#include "../Base/TTScreenPage.h"
#include <EPDConfig.h>

#define TT_ABOUT_DEVICE_NAME   "墨鱼桌搭"
#define TT_ABOUT_MANUAL_URL    "https://epaper-board.tos-cn-beijing.volces.com/manual/manual.jpg"
#define TT_ABOUT_XIANYU_SHOP   "TTStudio"
#define TT_ABOUT_QQ_NAME       "TT Studio"
#define TT_ABOUT_QQ_NUMBER     "703058070"
#define TT_ABOUT_QQ_URL        "mqqapi://card/show_pslcard?src_type=internal&version=1&uin=703058070&card_type=group&source=qrcode"
#define TT_ABOUT_QR_VERSION    6
#define TT_ABOUT_PAGE_COUNT    3
#define TT_ABOUT_PAD           16
#define TT_ABOUT_TITLE_FONT    16
#define TT_ABOUT_BODY_FONT     12
#define TT_ABOUT_LINE_NUM      6
#define TT_ABOUT_LINE_DEN      5
#define TT_ABOUT_PARA_TIMES    2
#define TT_ABOUT_QR_GAP        6
#define TT_ABOUT_QR_TOP        4
#define TT_ABOUT_INDEX_BOTTOM  8
#define TT_ABOUT_ICON          TT_FS_RES_DIR "/icons/info.i1"
#define TT_ABOUT_BODY_W        (EPD_WIDTH - (TT_ABOUT_PAD * 2))

class TTAboutPage : public TTScreenPage {
public:
    TTAboutPage() : TTScreenPage("关于") {}

    bool handleKeyAction(TTKeyId key, TTKeyGesture gesture) override;

protected:
    void buildContent(lv_obj_t* screen) override;

private:
    void showPage(int index);
    lv_obj_t* addLine(lv_obj_t* parent, const char* title, const char* value,
                      lv_font_t* titleFont, lv_font_t* valueFont, int32_t lineGap);
    lv_obj_t* makePanel(lv_obj_t* parent);
    void placeQr(lv_obj_t* panel, const char* title, const char* caption, const char* payload,
                 lv_font_t* titleFont, lv_font_t* captionFont);

    lv_obj_t* _panels[TT_ABOUT_PAGE_COUNT] = {};
    lv_obj_t* _indexLabel = nullptr;
    int _page = 0;
};
