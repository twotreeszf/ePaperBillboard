#include "TTHomePage.h"
#include "TTWiFiDemoPage.h"
#include "TTNTPDemoPage.h"
#include "TTClockScreenPage.h"
#include "../Base/TTFontManager.h"
#include "../Base/TTNavigationController.h"
#include "../Base/TTStreamImage.h"
#include <memory>

void HomeItem::create(HomeItem* item, lv_obj_t* parent, TTHomePage* page, int x, int y, const char* iconPath, const char* labelText, lv_font_t* font, std::function<void()> onClick) {
    item->onClick = onClick;

    item->btn = lv_btn_create(parent);
    lv_obj_set_size(item->btn, TT_HOME_ITEM_W, TT_HOME_ITEM_H);
    lv_obj_align(item->btn, LV_ALIGN_TOP_LEFT, x, y);
    lv_obj_set_style_radius(item->btn, TT_HOME_ITEM_RADIUS, 0);
    lv_obj_set_style_bg_color(item->btn, lv_color_white(), 0);
    lv_obj_set_style_border_width(item->btn, 0, 0);
    lv_obj_set_style_outline_width(item->btn, 0, 0);

    lv_obj_t* icon = tt_stream_image_create(item->btn);
    tt_stream_image_set_src(icon, iconPath);
    lv_obj_set_size(icon, TT_HOME_ICON_SIZE, TT_HOME_ICON_SIZE);
    lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, TT_HOME_ICON_PAD);

    lv_obj_t* label = lv_label_create(item->btn);
    lv_label_set_text(label, labelText);
    lv_obj_set_style_text_color(label, lv_color_black(), 0);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_align_to(label, icon, LV_ALIGN_OUT_BOTTOM_MID, 0, TT_HOME_ICON_PAD);

    lv_obj_update_layout(label);
    int labelWidth = lv_obj_get_content_width(label);
    if (labelWidth == 0) labelWidth = 28;
    item->underline = lv_obj_create(item->btn);
    lv_obj_set_size(item->underline, labelWidth, 1);
    lv_obj_set_style_bg_color(item->underline, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(item->underline, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(item->underline, 0, 0);
    lv_obj_set_style_pad_all(item->underline, 0, 0);
    lv_obj_set_style_radius(item->underline, 0, 0);
    lv_obj_align_to(item->underline, label, LV_ALIGN_OUT_BOTTOM_MID, 0, 1);
    lv_obj_add_flag(item->underline, LV_OBJ_FLAG_HIDDEN);

    lv_obj_add_event_cb(item->btn, HomeItem::onEntryClicked, LV_EVENT_CLICKED, item);
    lv_obj_add_event_cb(item->btn, HomeItem::onFocusChanged, LV_EVENT_FOCUSED, item);
    lv_obj_add_event_cb(item->btn, HomeItem::onFocusChanged, LV_EVENT_DEFOCUSED, item);
    page->addToFocusGroup(item->btn);
}

void HomeItem::onFocusChanged(lv_event_t* e) {
    HomeItem* item = (HomeItem*)lv_event_get_user_data(e);
    if (item == nullptr) return;
    if (item->underline == nullptr) return;

    uint32_t code = lv_event_get_code(e);
    if (code == LV_EVENT_FOCUSED) {
        lv_obj_clear_flag(item->underline, LV_OBJ_FLAG_HIDDEN);
    } else if (code == LV_EVENT_DEFOCUSED) {
        lv_obj_add_flag(item->underline, LV_OBJ_FLAG_HIDDEN);
    }
}

void HomeItem::onEntryClicked(lv_event_t* e) {
    HomeItem* item = (HomeItem*)lv_event_get_user_data(e);
    if (item == nullptr) return;
    
    std::function<void()> onClick = item->onClick;
    if (!onClick) return;

    onClick();
}

void TTHomePage::buildContent(lv_obj_t* screen) {
    TTFontManager& fm = TTFontManager::instance();
    lv_font_t* fontTitle = fm.getFont(16);
    lv_font_t* fontBtn = fm.getFont(12);

    lv_obj_set_style_bg_color(screen, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    lv_obj_t* title = lv_label_create(screen);
    lv_label_set_text(title, "Home");
    lv_obj_set_style_text_color(title, lv_color_black(), 0);
    lv_obj_set_style_text_font(title, fontTitle, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

    createGroup();

    const int pad = 8;
    const int yStart = 28;
    int x = pad;

    HomeItem::create(&_items[0], screen, this, x, yStart, "/icons/wifi.png", "WiFi", fontBtn, 
        [this]() { _controller->push(std::unique_ptr<TTScreenPage>(new TTWiFiDemoPage())); });
    x += TT_HOME_ITEM_W + TT_HOME_ITEMS_GAP;

    HomeItem::create(&_items[1], screen, this, x, yStart, "/icons/watch.png", "NTP", fontBtn,
        [this]() { _controller->push(std::unique_ptr<TTScreenPage>(new TTNTPDemoPage())); });
    x += TT_HOME_ITEM_W + TT_HOME_ITEMS_GAP;

    HomeItem::create(&_items[2], screen, this, x, yStart, "/icons/clock.png", "Clock", fontBtn,
        [this]() { _controller->push(std::unique_ptr<TTScreenPage>(new TTClockScreenPage())); });
}
