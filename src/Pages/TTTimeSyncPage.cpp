#include "TTTimeSyncPage.h"
#include "TTNtpSyncPage.h"
#include "TTTimezonePage.h"
#include "../Base/TTFontManager.h"
#include <memory>

void TTTimeSyncPage::buildContent(lv_obj_t* screen) {
    TTFontManager& fm = TTFontManager::instance();
    lv_font_t* font16 = fm.getFont(16);

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

    MenuItem::create(&_items[0], container, this, "/icons/watch.png", "NTP 校准", font16,
        [this]() { getNavigationController()->pushPage(std::unique_ptr<TTScreenPage>(new TTNtpSyncPage())); });
    MenuItem::create(&_items[1], container, this, "/icons/clock.png", "设置时区", font16,
        [this]() { getNavigationController()->pushPage(std::unique_ptr<TTScreenPage>(new TTTimezonePage())); });
}
