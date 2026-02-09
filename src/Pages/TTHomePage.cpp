#include "TTHomePage.h"
#include "TTWiFiDemoPage.h"
#include "TTNTPDemoPage.h"
#include "TTClockScreenPage.h"
#include "../Base/TTFontManager.h"
#include "../Base/TTNavigationController.h"
#include "../Base/TTStreamImage.h"
#include <memory>

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

    _btnWiFi = lv_btn_create(screen);
    lv_obj_set_size(_btnWiFi, TT_HOME_ITEM_W, TT_HOME_ITEM_H);
    lv_obj_align(_btnWiFi, LV_ALIGN_TOP_LEFT, x, yStart);
    lv_obj_set_style_radius(_btnWiFi, TT_HOME_ITEM_RADIUS, 0);
    lv_obj_set_style_bg_color(_btnWiFi, lv_color_white(), 0);
    lv_obj_set_style_border_width(_btnWiFi, 0, 0);
    lv_obj_set_style_outline_width(_btnWiFi, 1, 0);
    lv_obj_set_style_outline_color(_btnWiFi, lv_color_black(), 0);
    lv_obj_set_style_outline_pad(_btnWiFi, 0, 0);
    lv_obj_set_style_bg_color(_btnWiFi, lv_color_black(), LV_STATE_FOCUS_KEY);
    lv_obj_set_style_outline_color(_btnWiFi, lv_color_white(), LV_STATE_FOCUS_KEY);
    lv_obj_t* iconWiFi = tt_stream_image_create(_btnWiFi);
    tt_stream_image_set_src(iconWiFi, "/icons/wifi.png");
    lv_obj_set_size(iconWiFi, TT_HOME_ICON_SIZE, TT_HOME_ICON_SIZE);
    lv_obj_align(iconWiFi, LV_ALIGN_TOP_MID, 0, TT_HOME_ICON_PAD);
    lv_obj_t* lblWiFi = lv_label_create(_btnWiFi);
    lv_label_set_text(lblWiFi, "WiFi");
    lv_obj_set_style_text_color(lblWiFi, lv_color_black(), 0);
    lv_obj_set_style_text_color(lblWiFi, lv_color_white(), LV_STATE_FOCUS_KEY);
    lv_obj_set_style_text_font(lblWiFi, fontBtn, 0);
    lv_obj_align_to(lblWiFi, iconWiFi, LV_ALIGN_OUT_BOTTOM_MID, 0, TT_HOME_ICON_PAD);
    lv_obj_add_event_cb(_btnWiFi, onEntryClicked, LV_EVENT_CLICKED, this);
    addToFocusGroup(_btnWiFi);
    x += TT_HOME_ITEM_W + TT_HOME_ITEMS_GAP;

    _btnNTP = lv_btn_create(screen);
    lv_obj_set_size(_btnNTP, TT_HOME_ITEM_W, TT_HOME_ITEM_H);
    lv_obj_align(_btnNTP, LV_ALIGN_TOP_LEFT, x, yStart);
    lv_obj_set_style_radius(_btnNTP, TT_HOME_ITEM_RADIUS, 0);
    lv_obj_set_style_bg_color(_btnNTP, lv_color_white(), 0);
    lv_obj_set_style_border_width(_btnNTP, 0, 0);
    lv_obj_set_style_outline_width(_btnNTP, 1, 0);
    lv_obj_set_style_outline_color(_btnNTP, lv_color_black(), 0);
    lv_obj_set_style_outline_pad(_btnNTP, 0, 0);
    lv_obj_set_style_bg_color(_btnNTP, lv_color_black(), LV_STATE_FOCUS_KEY);
    lv_obj_set_style_outline_color(_btnNTP, lv_color_white(), LV_STATE_FOCUS_KEY);
    lv_obj_t* iconNTP = tt_stream_image_create(_btnNTP);
    tt_stream_image_set_src(iconNTP, "/icons/watch.png");
    lv_obj_set_size(iconNTP, TT_HOME_ICON_SIZE, TT_HOME_ICON_SIZE);
    lv_obj_align(iconNTP, LV_ALIGN_TOP_MID, 0, TT_HOME_ICON_PAD);
    lv_obj_t* lblNTP = lv_label_create(_btnNTP);
    lv_label_set_text(lblNTP, "NTP");
    lv_obj_set_style_text_color(lblNTP, lv_color_black(), 0);
    lv_obj_set_style_text_color(lblNTP, lv_color_white(), LV_STATE_FOCUS_KEY);
    lv_obj_set_style_text_font(lblNTP, fontBtn, 0);
    lv_obj_align_to(lblNTP, iconNTP, LV_ALIGN_OUT_BOTTOM_MID, 0, TT_HOME_ICON_PAD);
    lv_obj_add_event_cb(_btnNTP, onEntryClicked, LV_EVENT_CLICKED, this);
    addToFocusGroup(_btnNTP);
    x += TT_HOME_ITEM_W + TT_HOME_ITEMS_GAP;

    _btnClock = lv_btn_create(screen);
    lv_obj_set_size(_btnClock, TT_HOME_ITEM_W, TT_HOME_ITEM_H);
    lv_obj_align(_btnClock, LV_ALIGN_TOP_LEFT, x, yStart);
    lv_obj_set_style_radius(_btnClock, TT_HOME_ITEM_RADIUS, 0);
    lv_obj_set_style_bg_color(_btnClock, lv_color_white(), 0);
    lv_obj_set_style_border_width(_btnClock, 0, 0);
    lv_obj_set_style_outline_width(_btnClock, 1, 0);
    lv_obj_set_style_outline_color(_btnClock, lv_color_black(), 0);
    lv_obj_set_style_outline_pad(_btnClock, 0, 0);
    lv_obj_set_style_bg_color(_btnClock, lv_color_black(), LV_STATE_FOCUS_KEY);
    lv_obj_set_style_outline_color(_btnClock, lv_color_white(), LV_STATE_FOCUS_KEY);
    lv_obj_t* iconClock = tt_stream_image_create(_btnClock);
    tt_stream_image_set_src(iconClock, "/icons/clock.png");
    lv_obj_set_size(iconClock, TT_HOME_ICON_SIZE, TT_HOME_ICON_SIZE);
    lv_obj_align(iconClock, LV_ALIGN_TOP_MID, 0, TT_HOME_ICON_PAD);
    lv_obj_t* lblClock = lv_label_create(_btnClock);
    lv_label_set_text(lblClock, "Clock");
    lv_obj_set_style_text_color(lblClock, lv_color_black(), 0);
    lv_obj_set_style_text_color(lblClock, lv_color_white(), LV_STATE_FOCUS_KEY);
    lv_obj_set_style_text_font(lblClock, fontBtn, 0);
    lv_obj_align_to(lblClock, iconClock, LV_ALIGN_OUT_BOTTOM_MID, 0, TT_HOME_ICON_PAD);
    lv_obj_add_event_cb(_btnClock, onEntryClicked, LV_EVENT_CLICKED, this);
    addToFocusGroup(_btnClock);
}

void TTHomePage::onEntryClicked(lv_event_t* e) {
    TTHomePage* self = (TTHomePage*)lv_event_get_user_data(e);
    lv_obj_t* target = (lv_obj_t*)lv_event_get_target(e);
    if (self == nullptr || self->_controller == nullptr) return;

    if (target == self->_btnWiFi) {
        self->_controller->push(std::unique_ptr<TTScreenPage>(new TTWiFiDemoPage()));
    } else if (target == self->_btnNTP) {
        self->_controller->push(std::unique_ptr<TTScreenPage>(new TTNTPDemoPage()));
    } else if (target == self->_btnClock) {
        self->_controller->push(std::unique_ptr<TTScreenPage>(new TTClockScreenPage()));
    }
}
