#include "TTSettingsPage.h"
#include "TTWiFiConfigPage.h"
#include "TTWiFiStatusPage.h"
#include "TTNtpSyncPage.h"
#include "TTUpdatePage.h"
#include "../Base/TTFile.h"
#include "../Base/TTFontManager.h"
#include "../Base/TTPopupLayer.h"
#include "../Base/TTInstance.h"
#include "../Base/Logger.h"
#include "../Tasks/TTUITask.h"
#include <memory>

void TTSettingsPage::buildContent(lv_obj_t* screen) {
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
    lv_obj_set_width(container, TT_SETTINGS_MENU_ROW_W);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(container, TT_HOME_ITEMS_GAP, 0);
    lv_obj_set_style_pad_row(container, TT_HOME_ITEMS_GAP, 0);
    lv_obj_align(container, LV_ALIGN_CENTER, 0, 0);

    MenuItem::create(&_items[0], container, this, TT_FS_RES_DIR "/icons/globe.i1", "Web 设置", fontBtn,
        [this]() { getNavigationController()->pushPage(std::unique_ptr<TTScreenPage>(new TTWiFiConfigPage())); });

    MenuItem::create(&_items[1], container, this, TT_FS_RES_DIR "/icons/wifi.i1", "Wi-Fi", fontBtn,
        [this]() { getNavigationController()->pushPage(std::unique_ptr<TTScreenPage>(new TTWiFiStatusPage())); });

    MenuItem::create(&_items[2], container, this, TT_FS_RES_DIR "/icons/ntp.i1", "NTP 校时", fontBtn,
        [this]() { getNavigationController()->pushPage(std::unique_ptr<TTScreenPage>(new TTNtpSyncPage())); });

    MenuItem::create(&_items[3], container, this, TT_FS_RES_DIR "/icons/update.i1", "系统更新", fontBtn,
        [this]() { getNavigationController()->pushPage(std::unique_ptr<TTScreenPage>(new TTUpdatePage())); });

    MenuItem::create(&_items[4], container, this, TT_FS_RES_DIR "/icons/restart.i1", "重启", fontBtn,
        [this]() { onRebootClicked(); });
}

void TTSettingsPage::onRebootClicked() {
    LOG_I("Settings: reboot requested");
    TTInstanceOf<TTPopupLayer>().showLoading("重启中...");
    requestRefresh(TT_REFRESH_PARTIAL);
    TTInstanceOf<TTUITask>().requestRestartAsync();
}
