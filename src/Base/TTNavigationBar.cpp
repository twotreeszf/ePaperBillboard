#include "TTNavigationBar.h"
#include "ITTNavigationController.h"
#include "TTFontManager.h"
#include "TTStreamImage.h"
#include "Logger.h"
#include <EPDConfig.h>

void TTNavigationBar::begin(lv_obj_t* parent, ITTNavigationController* nav) {
    if (_bar != nullptr) return;
    if (parent == nullptr) {
        LOG_E("NavBar: begin failed, parent is null");
        return;
    }

    _nav = nav;

    lv_font_t* font = TTFontManager::instance().getFont(TT_NAV_BAR_FONT);

    _bar = lv_obj_create(parent);
    lv_obj_add_flag(_bar, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_add_flag(_bar, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_size(_bar, EPD_WIDTH, TT_NAV_BAR_HEIGHT);
    lv_obj_align(_bar, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(_bar, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_bar, 0, 0);
    lv_obj_set_style_pad_all(_bar, 0, 0);
    lv_obj_set_style_radius(_bar, 0, 0);
    lv_obj_remove_flag(_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(_bar, LV_OBJ_FLAG_HIDDEN);

    _backBtn = lv_btn_create(_bar);
    lv_obj_set_size(_backBtn, TT_NAV_ARROW_W, TT_NAV_BAR_HEIGHT - TT_NAV_DIVIDER_H);
    lv_obj_align(_backBtn, LV_ALIGN_LEFT_MID, TT_NAV_BAR_PAD, -(TT_NAV_DIVIDER_H / 2));
    lv_obj_set_style_bg_opa(_backBtn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(_backBtn, LV_OPA_TRANSP, LV_STATE_FOCUS_KEY);
    lv_obj_set_style_border_width(_backBtn, 0, 0);
    lv_obj_set_style_outline_width(_backBtn, 0, 0);
    lv_obj_set_style_shadow_width(_backBtn, 0, 0);
    lv_obj_set_style_pad_all(_backBtn, 0, 0);
    lv_obj_set_style_radius(_backBtn, 0, 0);
    lv_obj_add_event_cb(_backBtn, onBackClicked, LV_EVENT_CLICKED, this);

    lv_obj_t* arrow = tt_stream_image_create(_backBtn);
    tt_stream_image_set_src(arrow, TT_NAV_BACK_ICON);
    lv_obj_center(arrow);

    _title = lv_label_create(_bar);
    lv_label_set_text(_title, "");
    lv_obj_set_style_text_color(_title, lv_color_black(), 0);
    lv_obj_set_style_text_font(_title, font, 0);
    lv_obj_set_style_text_align(_title, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_align_to(_title, _backBtn, LV_ALIGN_OUT_RIGHT_MID, TT_NAV_BAR_PAD, 0);

    lv_obj_t* divider = lv_obj_create(_bar);
    lv_obj_set_size(divider, EPD_WIDTH, TT_NAV_DIVIDER_H);
    lv_obj_align(divider, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_bg_color(divider, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(divider, 0, 0);
    lv_obj_set_style_pad_all(divider, 0, 0);
    lv_obj_set_style_radius(divider, 0, 0);

    LOG_I("NavBar: created");
}

void TTNavigationBar::show(const char* title, bool showBack) {
    if (_bar == nullptr) return;
    lv_label_set_text(_title, title != nullptr ? title : "");
    if (showBack) {
        lv_obj_remove_flag(_backBtn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_align_to(_title, _backBtn, LV_ALIGN_OUT_RIGHT_MID, TT_NAV_BAR_PAD, 0);
    } else {
        lv_obj_add_flag(_backBtn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_align(_title, LV_ALIGN_LEFT_MID, TT_NAV_BAR_PAD, -(TT_NAV_DIVIDER_H / 2));
    }
    lv_obj_remove_flag(_bar, LV_OBJ_FLAG_HIDDEN);
    _visible = true;
    LOG_I("NavBar: show title=%s back=%d", title != nullptr ? title : "", showBack ? 1 : 0);
}

void TTNavigationBar::hide() {
    if (_bar == nullptr) return;
    lv_obj_add_flag(_bar, LV_OBJ_FLAG_HIDDEN);
    _visible = false;
    LOG_I("NavBar: hide");
}

void TTNavigationBar::onBackClicked(lv_event_t* e) {
    TTNavigationBar* self = (TTNavigationBar*)lv_event_get_user_data(e);
    if (self == nullptr || self->_nav == nullptr) return;
    LOG_I("NavBar: back clicked");
    self->_nav->pop();
}
