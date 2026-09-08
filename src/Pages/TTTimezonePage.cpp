#include "TTTimezonePage.h"
#include "../Base/TTFontManager.h"
#include "../Base/TTTextButton.h"
#include "../Base/TTRtc.h"
#include "../Base/Logger.h"

#define TT_TZ_CITY_MAX  8

struct TTTzCity {
    const char* city;
    const char* posix;
};

struct TTTzRegion {
    const char* name;
    const TTTzCity cities[TT_TZ_CITY_MAX];
    uint8_t count;
};

#define TT_TZ_REGION_COUNT  5

static const TTTzRegion kRegions[TT_TZ_REGION_COUNT] = {
    { "亚洲", {
        { "上海", "CST-8" },
        { "香港", "HKT-8" },
        { "台北", "CST-8" },
        { "新加坡", "SGT-8" },
        { "东京", "JST-9" },
        { "首尔", "KST-9" },
        { "曼谷", "ICT-7" },
        { "迪拜", "GST-4" },
    }, 8 },
    { "欧洲", {
        { "伦敦", "GMT0" },
        { "巴黎", "CET-1" },
        { "柏林", "CET-1" },
        { "莫斯科", "MSK-3" },
    }, 4 },
    { "美洲", {
        { "纽约", "EST5" },
        { "芝加哥", "CST6" },
        { "洛杉矶", "PST8" },
        { "丹佛", "MST7" },
        { "圣保罗", "BRT3" },
    }, 5 },
    { "大洋洲", {
        { "悉尼", "AEST-10" },
        { "奥克兰", "NZST-12" },
    }, 2 },
    { "UTC", {
        { "UTC", "GMT0" },
    }, 1 },
};

void TTTimezonePage::buildContent(lv_obj_t* screen) {
    TTFontManager& fm = TTFontManager::instance();
    lv_font_t* font16 = fm.getFont(16);

    lv_obj_set_style_bg_color(screen, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    _hintLabel = lv_label_create(screen);
    lv_label_set_text(_hintLabel, "选择州");
    lv_obj_set_style_text_color(_hintLabel, lv_color_black(), 0);
    lv_obj_set_style_text_font(_hintLabel, font16, 0);
    lv_obj_set_style_text_align(_hintLabel, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_align(_hintLabel, LV_ALIGN_TOP_LEFT, TT_TZ_HINT_LEFT, TT_TZ_HINT_TOP);

    _grid = lv_obj_create(screen);
    lv_obj_set_size(_grid, TT_TZ_GRID_W, TT_TZ_GRID_H);
    lv_obj_set_style_bg_opa(_grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(_grid, 0, 0);
    lv_obj_set_style_pad_all(_grid, 0, 0);
    lv_obj_set_style_pad_row(_grid, TT_TZ_BTN_GAP, 0);
    lv_obj_set_style_pad_column(_grid, TT_TZ_BTN_GAP, 0);
    lv_obj_set_layout(_grid, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(_grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(_grid, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(_grid, LV_DIR_VER);
    lv_obj_align_to(_grid, _hintLabel, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 8);

    showRegions();
}

void TTTimezonePage::clearChoices() {
    if (_grid == nullptr) {
        return;
    }
    while (lv_obj_get_child_count(_grid) > 0) {
        lv_obj_t* child = lv_obj_get_child(_grid, 0);
        if (_group != nullptr) {
            lv_group_remove_obj(child);
        }
        lv_obj_delete(child);
    }
}

void TTTimezonePage::addChoice(const char* text, int index) {
    TTFontManager& fm = TTFontManager::instance();
    lv_obj_t* btn = TTTextButton::create(_grid, text, fm.getFont(12), TT_TZ_BTN_W, TT_TZ_BTN_H);
    lv_obj_add_event_cb(btn, onChoiceEvent, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(btn, [](lv_event_t* e) {
        lv_obj_t* target = (lv_obj_t*)lv_event_get_current_target(e);
        lv_obj_scroll_to_view(target, LV_ANIM_OFF);
    }, LV_EVENT_FOCUSED, nullptr);
    lv_obj_set_user_data(btn, (void*)(intptr_t)index);
    addToFocusGroup(btn);
}

void TTTimezonePage::showRegions() {
    _regionIndex = -1;
    lv_label_set_text(_hintLabel, "选择州");
    clearChoices();
    for (int i = 0; i < TT_TZ_REGION_COUNT; ++i) {
        addChoice(kRegions[i].name, i);
    }
    if (_group != nullptr && lv_obj_get_child_count(_grid) > 0) {
        lv_group_focus_obj(lv_obj_get_child(_grid, 0));
    }
}

void TTTimezonePage::showCities(int regionIndex) {
    if (regionIndex < 0 || regionIndex >= TT_TZ_REGION_COUNT) {
        return;
    }
    _regionIndex = regionIndex;
    lv_label_set_text(_hintLabel, "选择城市");
    clearChoices();
    const TTTzRegion& region = kRegions[regionIndex];
    for (uint8_t i = 0; i < region.count; ++i) {
        addChoice(region.cities[i].city, (int)i);
    }
    addChoice("返回", TT_TZ_BACK_INDEX);
    if (_group != nullptr && lv_obj_get_child_count(_grid) > 0) {
        lv_group_focus_obj(lv_obj_get_child(_grid, 0));
    }
}

void TTTimezonePage::saveCity(int cityIndex) {
    if (_regionIndex < 0 || _regionIndex >= TT_TZ_REGION_COUNT) {
        return;
    }
    const TTTzRegion& region = kRegions[_regionIndex];
    if (cityIndex < 0 || cityIndex >= region.count) {
        return;
    }
    const TTTzCity& city = region.cities[cityIndex];
    LOG_I("Timezone: save %s/%s posix=%s", region.name, city.city, city.posix);
    if (!TTRtc::saveTimezone(city.posix, city.city)) {
        LOG_E("Timezone: save failed");
        return;
    }
    if (getNavigationController() != nullptr) {
        getNavigationController()->pop();
    }
}

void TTTimezonePage::onChoice(int index) {
    if (_regionIndex < 0) {
        showCities(index);
        requestRefresh(TT_REFRESH_PARTIAL);
        return;
    }
    if (index == TT_TZ_BACK_INDEX) {
        showRegions();
        requestRefresh(TT_REFRESH_PARTIAL);
        return;
    }
    saveCity(index);
}

void TTTimezonePage::onChoiceEvent(lv_event_t* e) {
    TTTimezonePage* self = (TTTimezonePage*)lv_event_get_user_data(e);
    lv_obj_t* btn = (lv_obj_t*)lv_event_get_current_target(e);
    if (self == nullptr || btn == nullptr) {
        return;
    }
    self->onChoice((int)(intptr_t)lv_obj_get_user_data(btn));
}
