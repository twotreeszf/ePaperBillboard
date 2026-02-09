#include "TTHomePage.h"
#include "TTWiFiDemoPage.h"
#include "TTNTPDemoPage.h"
#include "TTClockScreenPage.h"
#include "../Base/TTFontManager.h"
#include "../Base/TTStreamImage.h"
#include <memory>

void HomeItem::create(HomeItem* item, lv_obj_t* parent, TTHomePage* page, const char* iconPath, const char* labelText, lv_font_t* font, std::function<void()> onClick) {
    item->onClick = onClick;

    item->btn = lv_btn_create(parent);
    lv_obj_set_size(item->btn, TT_HOME_ITEM_W, TT_HOME_ITEM_H);
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

    item->underline = lv_obj_create(item->btn);
    lv_obj_set_size(item->underline, TT_HOME_INDICATOR_W, TT_HOME_INDICATOR_H);
    lv_obj_set_style_bg_color(item->underline, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(item->underline, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(item->underline, 0, 0);
    lv_obj_set_style_pad_all(item->underline, 0, 0);
    lv_obj_set_style_radius(item->underline, 0, 0);
    lv_obj_align(item->underline, LV_ALIGN_BOTTOM_MID, 0, 0);
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

    lv_obj_t* divider = lv_obj_create(screen);
    lv_obj_set_size(divider, lv_pct(100), 1);
    lv_obj_set_style_bg_color(divider, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(divider, 0, 0);
    lv_obj_set_style_pad_all(divider, 0, 0);
    lv_obj_set_style_radius(divider, 0, 0);
    lv_obj_align_to(divider, title, LV_ALIGN_OUT_BOTTOM_MID, 0, 4);

    createGroup();

    lv_obj_update_layout(divider);
    int32_t divider_bottom = lv_obj_get_y2(divider);
    int32_t screen_height = lv_obj_get_height(screen);
    int32_t remaining_height = screen_height - divider_bottom;

    lv_obj_t* wrapper = lv_obj_create(screen);
    lv_obj_set_width(wrapper, lv_pct(100));
    lv_obj_set_height(wrapper, remaining_height);
    lv_obj_set_pos(wrapper, 0, divider_bottom);
    lv_obj_set_style_bg_opa(wrapper, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(wrapper, 0, 0);
    lv_obj_set_style_pad_all(wrapper, 0, 0);
    lv_obj_set_layout(wrapper, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(wrapper, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(wrapper, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* container = lv_obj_create(wrapper);
    lv_obj_set_size(container, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_style_pad_all(container, 0, 0);
    lv_obj_set_layout(container, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(container, TT_HOME_ITEMS_GAP, 0);

    HomeItem::create(&_items[0], container, this, "/icons/wifi.png", "WiFi", fontBtn, 
        [this]() { getNavigationController()->pushPage(std::unique_ptr<TTScreenPage>(new TTWiFiDemoPage())); });

    HomeItem::create(&_items[1], container, this, "/icons/watch.png", "NTP", fontBtn,
        [this]() { getNavigationController()->pushPage(std::unique_ptr<TTScreenPage>(new TTNTPDemoPage())); });

    HomeItem::create(&_items[2], container, this, "/icons/clock.png", "Clock", fontBtn,
        [this]() { getNavigationController()->pushPage(std::unique_ptr<TTScreenPage>(new TTClockScreenPage())); });
}
