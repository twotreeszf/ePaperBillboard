#include "TTNTPDemoPage.h"
#include "../Base/TTFontManager.h"

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
}
