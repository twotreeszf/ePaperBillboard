#include "TTNTPDemoPage.h"
#include "../Base/TTFontManager.h"
#include "../Base/TTNavigationController.h"

void TTNTPDemoPage::buildContent(lv_obj_t* screen) {
    TTFontManager& fm = TTFontManager::instance();
    lv_font_t* fontTitle = fm.getFont(16);
    lv_font_t* fontText = fm.getFont(12);

    lv_obj_set_style_bg_color(screen, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    lv_obj_t* title = lv_label_create(screen);
    lv_label_set_text(title, "NTP");
    lv_obj_set_style_text_color(title, lv_color_black(), 0);
    lv_obj_set_style_text_font(title, fontTitle, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

    lv_obj_t* desc = lv_label_create(screen);
    lv_label_set_text(desc, "NTP sync demo.");
    lv_obj_set_style_text_color(desc, lv_color_black(), 0);
    lv_obj_set_style_text_font(desc, fontText, 0);
    lv_obj_align(desc, LV_ALIGN_CENTER, 0, 0);

    createGroup();
    lv_obj_t* back = lv_btn_create(screen);
    lv_obj_set_size(back, 60, 24);
    lv_obj_align(back, LV_ALIGN_BOTTOM_LEFT, 8, -8);
    lv_obj_set_style_bg_color(back, lv_color_white(), 0);
    lv_obj_set_style_border_color(back, lv_color_black(), 0);
    lv_obj_set_style_border_width(back, 1, 0);
    lv_obj_t* lblBack = lv_label_create(back);
    lv_label_set_text(lblBack, "Back");
    lv_obj_set_style_text_color(lblBack, lv_color_black(), 0);
    lv_obj_center(lblBack);
    lv_obj_add_event_cb(back, onBackClicked, LV_EVENT_CLICKED, this);
    addToFocusGroup(back);
}

void TTNTPDemoPage::onBackClicked(lv_event_t* e) {
    TTNTPDemoPage* self = (TTNTPDemoPage*)lv_event_get_user_data(e);
    if (self != nullptr && self->_controller != nullptr) {
        self->_controller->pop();
    }
}
