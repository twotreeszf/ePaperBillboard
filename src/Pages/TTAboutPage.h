#pragma once

#include "../Base/TTFile.h"
#include "../Base/TTScreenPage.h"
#include <EPDConfig.h>

#define TT_ABOUT_DEVICE_NAME   "墨鱼桌搭"
#define TT_ABOUT_MANUAL_URL    "https://epaper-board.tos-cn-beijing.volces.com/manual/manual.jpg"
#define TT_ABOUT_XIANYU_SHOP   "TTStudio"
#define TT_ABOUT_XIANYU_TAGS   "鱼小铺 · 电子创客和3D打印"
#define TT_ABOUT_XIANYU_STATS  "166 宝贝    1421 粉丝"
#define TT_ABOUT_XIANYU_BIO    "折腾和制作各种数码产品"
#define TT_ABOUT_XIANYU_URL    "https://p.goofish.com/p/w5pcMB2W"
#define TT_ABOUT_XIANYU_QR_VERSION  4
#define TT_ABOUT_QQ_NAME       "TT Studio"
#define TT_ABOUT_QQ_NUMBER     "703058070"
#define TT_ABOUT_QQ_URL        "https://qm.qq.com/q/ashf7IZUOY"
#define TT_ABOUT_QR_VERSION    6
#define TT_ABOUT_PAGE_COUNT    4
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
    void placeShop(lv_obj_t* panel, lv_font_t* titleFont, lv_font_t* bodyFont);

    lv_obj_t* _panels[TT_ABOUT_PAGE_COUNT] = {};
    lv_obj_t* _indexLabel = nullptr;
    int _page = 0;
};
