#include "TTHomePage.h"
#include "TTSettingsPage.h"
#include "TTClockScreenPage.h"
#include "TTWeatherPage.h"
#include "../Base/TTFontManager.h"
#include "../Base/TTStreamImage.h"
#include <memory>

void MenuItem::create(MenuItem* item, lv_obj_t* parent, TTScreenPage* page, const char* iconPath, const char* labelText, lv_font_t* font, std::function<void()> onClick) {
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
    lv_obj_set_style_radius(item->underline, TT_HOME_INDICATOR_RADIUS, 0);
    lv_obj_align(item->underline, LV_ALIGN_BOTTOM_MID, 0, -2);
    lv_obj_add_flag(item->underline, LV_OBJ_FLAG_HIDDEN);

    lv_obj_add_event_cb(item->btn, MenuItem::onEntryClicked, LV_EVENT_CLICKED, item);
    lv_obj_add_event_cb(item->btn, MenuItem::onFocusChanged, LV_EVENT_FOCUSED, item);
    lv_obj_add_event_cb(item->btn, MenuItem::onFocusChanged, LV_EVENT_DEFOCUSED, item);
    page->addToFocusGroup(item->btn);
}

void MenuItem::onFocusChanged(lv_event_t* e) {
    MenuItem* item = (MenuItem*)lv_event_get_user_data(e);
    if (item == nullptr) return;
    if (item->underline == nullptr) return;

    uint32_t code = lv_event_get_code(e);
    if (code == LV_EVENT_FOCUSED) {
        lv_obj_clear_flag(item->underline, LV_OBJ_FLAG_HIDDEN);
    } else if (code == LV_EVENT_DEFOCUSED) {
        lv_obj_add_flag(item->underline, LV_OBJ_FLAG_HIDDEN);
    }
}

void MenuItem::onEntryClicked(lv_event_t* e) {
    MenuItem* item = (MenuItem*)lv_event_get_user_data(e);
    if (item == nullptr) return;
    
    std::function<void()> onClick = item->onClick;
    if (!onClick) return;

    onClick();
}

void TTHomePage::buildContent(lv_obj_t* screen) {
    TTFontManager& fm = TTFontManager::instance();
    lv_font_t* fontBtn = fm.getFont(16);

    lv_obj_set_style_bg_color(screen, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    lv_obj_t* container = lv_obj_create(screen);
    lv_obj_set_size(container, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_style_pad_all(container, 0, 0);
    lv_obj_set_layout(container, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(container, TT_HOME_ITEMS_GAP, 0);
    lv_obj_align(container, LV_ALIGN_CENTER, 0, 0);

    MenuItem::create(&_items[0], container, this, "/icons/settings.i1", "设置", fontBtn,
        [this]() { getNavigationController()->pushPage(std::unique_ptr<TTScreenPage>(new TTSettingsPage())); });

    MenuItem::create(&_items[1], container, this, "/icons/clock.i1", "时钟", fontBtn,
        [this]() { getNavigationController()->pushPage(std::unique_ptr<TTScreenPage>(new TTClockScreenPage())); });

    MenuItem::create(&_items[2], container, this, "/icons/weather.i1", "天气", fontBtn,
        [this]() { getNavigationController()->pushPage(std::unique_ptr<TTScreenPage>(new TTWeatherPage())); });
}
