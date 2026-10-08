#include "TTHomePage.h"
#include "TTSettingsPage.h"
#include "TTWeatherPage.h"
#include "TTCalendarPage.h"
#include "TTPictorialPage.h"
#include "../Base/TTFontManager.h"
#include "../Base/TTStreamImage.h"
#include "../Base/Logger.h"
#include <memory>

void MenuItem::create(MenuItem* item, lv_obj_t* parent, TTScreenPage* page, const char* iconPath, const char* labelText, lv_font_t* font, std::function<void()> onClick) {
    item->onClick = onClick;

    item->btn = lv_btn_create(parent);
    lv_obj_set_size(item->btn, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_min_width(item->btn, TT_HOME_ITEM_W, 0);
    lv_obj_set_style_min_height(item->btn, TT_HOME_ITEM_H, 0);
    lv_obj_set_style_pad_left(item->btn, TT_HOME_ITEM_PAD_X, 0);
    lv_obj_set_style_pad_right(item->btn, TT_HOME_ITEM_PAD_X, 0);
    lv_obj_set_style_pad_top(item->btn, TT_HOME_ITEM_PAD_Y, 0);
    lv_obj_set_style_pad_bottom(item->btn, TT_HOME_ITEM_PAD_Y, 0);
    lv_obj_set_style_radius(item->btn, TT_HOME_ITEM_RADIUS, 0);
    lv_obj_set_style_bg_color(item->btn, lv_color_white(), 0);
    lv_obj_set_style_border_width(item->btn, 0, 0);
    lv_obj_set_style_outline_width(item->btn, 0, 0);
    lv_obj_set_flex_flow(item->btn, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(item->btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(item->btn, TT_HOME_ICON_PAD, 0);

    lv_obj_t* icon = tt_stream_image_create(item->btn);
    tt_stream_image_set_src(icon, iconPath);
    lv_obj_set_size(icon, TT_HOME_ICON_SIZE, TT_HOME_ICON_SIZE);

    lv_obj_t* label = lv_label_create(item->btn);
    lv_obj_set_width(label, LV_SIZE_CONTENT);
    lv_label_set_text(label, labelText);
    lv_obj_set_style_text_color(label, lv_color_black(), 0);
    lv_obj_set_style_text_font(label, font, 0);

    item->underline = lv_obj_create(item->btn);
    lv_obj_set_size(item->underline, TT_HOME_INDICATOR_W, TT_HOME_INDICATOR_H);
    lv_obj_set_style_bg_color(item->underline, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(item->underline, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(item->underline, 0, 0);
    lv_obj_set_style_pad_all(item->underline, 0, 0);
    lv_obj_set_style_radius(item->underline, TT_HOME_INDICATOR_RADIUS, 0);
    lv_obj_set_style_margin_top(item->underline, TT_HOME_INDICATOR_GAP, 0);
    lv_obj_set_clickable(item->underline, false);

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
        lv_obj_set_style_bg_opa(item->underline, LV_OPA_COVER, 0);
    } else if (code == LV_EVENT_DEFOCUSED) {
        lv_obj_set_style_bg_opa(item->underline, LV_OPA_TRANSP, 0);
    }
}

void MenuItem::onEntryClicked(lv_event_t* e) {
    MenuItem* item = (MenuItem*)lv_event_get_user_data(e);
    if (item == nullptr) return;
    
    std::function<void()> onClick = item->onClick;
    if (!onClick) return;

    onClick();
}

static lv_obj_t* createRowMark(lv_obj_t* screen, const char* iconPath, lv_align_t align, int32_t x) {
    lv_obj_t* mark = tt_stream_image_create(screen);
    tt_stream_image_set_src(mark, iconPath);
    lv_obj_set_size(mark, TT_HOME_ROW_MARK_SIZE, TT_HOME_ROW_MARK_SIZE);
    lv_obj_align(mark, align, x, 0);
    lv_obj_set_hidden(mark, true);
    return mark;
}

void MenuRow::create(lv_obj_t* screen) {
    container = lv_obj_create(screen);
    lv_obj_set_size(container, TT_HOME_ROW_W, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_style_pad_all(container, 0, 0);
    lv_obj_set_style_radius(container, 0, 0);
    lv_obj_set_layout(container, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(container, TT_HOME_ITEMS_GAP, 0);
    lv_obj_set_scroll_dir(container, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(container, LV_SCROLLBAR_MODE_OFF);
    lv_obj_align(container, LV_ALIGN_CENTER, 0, 0);

    prevMark = createRowMark(screen, TT_FS_RES_DIR "/icons/arrow_left.i1", LV_ALIGN_LEFT_MID, TT_HOME_ROW_MARK_PAD);
    nextMark = createRowMark(screen, TT_FS_RES_DIR "/icons/arrow_right.i1", LV_ALIGN_RIGHT_MID, -TT_HOME_ROW_MARK_PAD);
}

void MenuRow::bind(MenuItem* menuItems, int itemCount) {
    items = menuItems;
    count = itemCount;
    first = 0;
    const lv_flex_align_t mainAlign = count > TT_HOME_ROW_VISIBLE ? LV_FLEX_ALIGN_START : LV_FLEX_ALIGN_CENTER;
    lv_obj_set_flex_align(container, mainAlign, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    int focused = 0;
    lv_group_t* group = count > 0 ? lv_obj_get_group(items[0].btn) : nullptr;
    lv_obj_t* focusedObj = group != nullptr ? lv_group_get_focused(group) : nullptr;
    for (int i = 0; i < count; i++) {
        lv_obj_set_scroll_on_focus(items[i].btn, false);
        lv_obj_add_event_cb(items[i].btn, MenuRow::onItemFocused, LV_EVENT_FOCUSED, this);
        if (items[i].btn == focusedObj) {
            focused = i;
        }
    }
    showItem(focused);
}

void MenuRow::showItem(int index) {
    if (container == nullptr || index < 0 || index >= count) {
        return;
    }
    if (index < first) {
        first = index;
    } else if (index >= first + TT_HOME_ROW_VISIBLE) {
        first = index - TT_HOME_ROW_VISIBLE + 1;
    }
    lv_obj_update_layout(container);
    lv_obj_scroll_to_x(container, lv_obj_get_x(items[first].btn), LV_ANIM_OFF);

    const bool hasPrev = first > 0;
    const bool hasNext = first + TT_HOME_ROW_VISIBLE < count;
    lv_obj_set_hidden(prevMark, !hasPrev);
    lv_obj_set_hidden(nextMark, !hasNext);

    lv_area_t band;
    lv_area_t mark;
    lv_obj_get_coords(container, &band);
    lv_obj_get_coords(prevMark, &mark);
    band.x1 = LV_MIN(band.x1, mark.x1);
    band.y1 = LV_MIN(band.y1, mark.y1);
    band.y2 = LV_MAX(band.y2, mark.y2);
    lv_obj_get_coords(nextMark, &mark);
    band.x2 = LV_MAX(band.x2, mark.x2);
    band.y1 = LV_MIN(band.y1, mark.y1);
    band.y2 = LV_MAX(band.y2, mark.y2);
    lv_obj_invalidate_area(lv_obj_get_parent(container), &band);
    LOG_I("MenuRow: focus=%d first=%d prev=%d next=%d band=(%d,%d)-(%d,%d)", index, first,
          hasPrev ? 1 : 0, hasNext ? 1 : 0, (int)band.x1, (int)band.y1, (int)band.x2, (int)band.y2);
}

void MenuRow::onItemFocused(lv_event_t* e) {
    MenuRow* row = (MenuRow*)lv_event_get_user_data(e);
    if (row == nullptr) return;
    lv_obj_t* btn = (lv_obj_t*)lv_event_get_current_target(e);
    for (int i = 0; i < row->count; i++) {
        if (row->items[i].btn == btn) {
            row->showItem(i);
            return;
        }
    }
}

void TTHomePage::buildContent(lv_obj_t* screen) {
    TTFontManager& fm = TTFontManager::instance();
    lv_font_t* fontBtn = fm.getFont(16);

    lv_obj_set_style_bg_color(screen, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    _row.create(screen);
    lv_obj_t* container = _row.container;

    MenuItem::create(&_items[0], container, this, TT_FS_RES_DIR "/icons/weather.i1", "天气", fontBtn,
        [this]() { getNavigationController()->pushPage(std::unique_ptr<TTScreenPage>(new TTWeatherPage())); });

    MenuItem::create(&_items[1], container, this, TT_FS_RES_DIR "/icons/calendar.i1", "日历", fontBtn,
        [this]() { getNavigationController()->pushPage(std::unique_ptr<TTScreenPage>(new TTCalendarPage())); });

    MenuItem::create(&_items[2], container, this, TT_FS_RES_DIR "/icons/pictorial.i1", "画报", fontBtn,
        [this]() { getNavigationController()->pushPage(std::unique_ptr<TTScreenPage>(new TTPictorialPage())); });

    MenuItem::create(&_items[3], container, this, TT_FS_RES_DIR "/icons/settings.i1", "设置", fontBtn,
        [this]() { getNavigationController()->pushPage(std::unique_ptr<TTScreenPage>(new TTSettingsPage())); });

    _row.bind(_items.data(), (int)_items.size());
}
