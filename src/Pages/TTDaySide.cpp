#include "TTDaySide.h"
#include "../Base/TTBoldLabel.h"
#include "../Base/TTNavigationBar.h"
#include "../Base/TTFontManager.h"
#include "../Base/TTInstance.h"
#include "../Base/TTRtc.h"
#include "../Base/TTStreamImage.h"
#include "../Service/TTWeatherService.h"
#include <EPDConfig.h>
#include <cstdio>
#include <ctime>

namespace {

const char* weekEn(int wday) {
    const char* names[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
    if (wday < 0 || wday > 6) {
        return "---";
    }
    return names[wday];
}

lv_obj_t* createLabel(lv_obj_t* parent, const lv_font_t* font, lv_color_t color, const char* text) {
    lv_obj_t* label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_label_set_text(label, text != nullptr ? text : "");
    return label;
}

void drawHSpan(lv_layer_t* layer, const lv_draw_rect_dsc_t* dsc, int x1, int x2, int y) {
    if (x2 < x1) {
        return;
    }
    lv_area_t area;
    area.x1 = x1;
    area.x2 = x2;
    area.y1 = y;
    area.y2 = y;
    lv_draw_rect(layer, dsc, &area);
}

void fillDiskAt(lv_layer_t* layer, int cx, int cy, int radius, lv_color_t color) {
    if (layer == nullptr || radius <= 0) {
        return;
    }
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = color;
    dsc.bg_opa = LV_OPA_COVER;
    dsc.border_width = 0;
    dsc.radius = 0;
    const int limit = radius * radius;
    for (int dy = -radius; dy <= radius; dy++) {
        int run = radius + 1;
        for (int dx = -radius; dx <= radius; dx++) {
            if (dx * dx + dy * dy <= limit) {
                if (run > radius) {
                    run = dx;
                }
                continue;
            }
            if (run > radius) {
                continue;
            }
            drawHSpan(layer, &dsc, cx + run, cx + dx - 1, cy + dy);
            run = radius + 1;
        }
        if (run <= radius) {
            drawHSpan(layer, &dsc, cx + run, cx + radius, cy + dy);
        }
    }
}

void drawPill(lv_layer_t* layer, int x1, int y1, int x2, int y2) {
    if (layer == nullptr || x2 < x1 || y2 < y1) {
        return;
    }
    int height = y2 - y1 + 1;
    if ((height & 1) == 0) {
        y2++;
        height++;
    }
    const int radius = height / 2;
    if (radius <= 0) {
        return;
    }
    const int cy = y1 + radius;
    const int left = x1 + radius;
    const int right = x2 - radius;
    if (right < left) {
        fillDiskAt(layer, (x1 + x2) / 2, cy, radius, lv_color_black());
        return;
    }
    fillDiskAt(layer, left, cy, radius, lv_color_black());
    fillDiskAt(layer, right, cy, radius, lv_color_black());
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = lv_color_black();
    dsc.bg_opa = LV_OPA_COVER;
    dsc.border_width = 0;
    dsc.radius = 0;
    lv_area_t body;
    body.x1 = left;
    body.x2 = right;
    body.y1 = y1;
    body.y2 = y2;
    lv_draw_rect(layer, &dsc, &body);
}

lv_font_t* fontOr(TTFontManager& fonts, int size, lv_font_t* fallback) {
    lv_font_t* font = fonts.getFont(size);
    return font != nullptr ? font : fallback;
}

}  // namespace

void TTDaySide::drawCondCapsule(lv_event_t* e) {
    lv_obj_t* label = static_cast<lv_obj_t*>(lv_event_get_target(e));
    const char* text = lv_label_get_text(label);
    if (label == nullptr || text == nullptr || text[0] == '\0') {
        return;
    }
    lv_area_t area;
    lv_obj_get_coords(label, &area);
    drawPill(lv_event_get_layer(e), area.x1, area.y1, area.x2, area.y2);
}

void TTDaySide::build(lv_obj_t* parent) {
    TTFontManager& fonts = TTFontManager::instance();
    lv_font_t* font16 = fonts.getFont(16);
    lv_font_t* fontTemp = fontOr(fonts, TT_CAL_TEMP_FONT, font16);
    lv_font_t* fontClock = fontOr(fonts, TT_CAL_TIME_FONT, fontTemp);
    lv_font_t* fontDate = fontOr(fonts, TT_CAL_DATE_EN_FONT, fontTemp);
    lv_font_t* fontWeek = fontOr(fonts, TT_CAL_WEEK_FONT, fontDate);
    lv_font_t* fontCond = fontOr(fonts, TT_CAL_COND_FONT, font16);
    const int height = EPD_HEIGHT - TT_NAV_PAGE_INSET;

    _side = lv_obj_create(parent);
    lv_obj_set_pos(_side, 0, 0);
    lv_obj_set_size(_side, TT_CAL_SIDE_W, height);
    lv_obj_set_style_bg_color(_side, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(_side, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_side, 0, 0);
    lv_obj_set_style_pad_all(_side, 0, 0);
    lv_obj_set_style_radius(_side, 0, 0);
    lv_obj_set_scrollable(_side, false);

    _weatherIcon = tt_stream_image_create(_side);
    lv_obj_set_size(_weatherIcon, TT_CAL_ICON, TT_CAL_ICON);
    lv_obj_align(_weatherIcon, LV_ALIGN_TOP_MID, 0, TT_CAL_ICON_Y);

    _tempLabel = createLabel(_side, fontTemp, lv_color_black(), "");
    lv_obj_set_pos(_tempLabel, TT_CAL_TEMP_X, TT_CAL_TEMP_Y);
    lv_obj_set_hidden(_tempLabel, true);

    _tempUnit = lv_obj_create(_side);
    lv_obj_set_size(_tempUnit, TT_CAL_DOT_LG, TT_CAL_DOT_LG);
    lv_obj_set_style_bg_opa(_tempUnit, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(_tempUnit, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(_tempUnit, TT_CAL_DOT_BORDER, 0);
    lv_obj_set_style_border_color(_tempUnit, lv_color_black(), 0);
    lv_obj_set_style_border_opa(_tempUnit, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(_tempUnit, 0, 0);
    lv_obj_set_scrollable(_tempUnit, false);
    lv_obj_set_clickable(_tempUnit, false);
    lv_obj_set_hidden(_tempUnit, true);

    _condLabel = TTBoldLabel::create(parent, "", fontCond);
    lv_obj_set_style_text_color(_condLabel, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(_condLabel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_hor(_condLabel, TT_CAL_CAPSULE_PAD_X, 0);
    lv_obj_set_style_pad_ver(_condLabel, TT_CAL_COND_PAD_Y, 0);
    lv_label_set_long_mode(_condLabel, LV_LABEL_LONG_CLIP);
    lv_obj_add_event_cb(_condLabel, drawCondCapsule, LV_EVENT_DRAW_MAIN_BEGIN, nullptr);
    lv_obj_set_hidden(_condLabel, true);

    _dateLabel = createLabel(_side, fontDate, lv_color_black(), "--");
    _weekLabel = createLabel(_side, fontWeek, lv_color_black(), "---");
    lv_obj_set_width(_weekLabel, LV_SIZE_CONTENT);

    _clockLabel = createLabel(_side, fontClock, lv_color_black(), "--");
    _clockColon = createLabel(_side, fontClock, lv_color_black(), ":");
    _clockMin = createLabel(_side, fontClock, lv_color_black(), "--");
}

void TTDaySide::layout() {
    if (_side == nullptr || _clockLabel == nullptr || _clockColon == nullptr || _clockMin == nullptr) {
        return;
    }
    lv_obj_update_layout(_clockLabel);
    lv_obj_update_layout(_clockColon);
    lv_obj_update_layout(_clockMin);
    int clockW = lv_obj_get_width(_clockLabel) + lv_obj_get_width(_clockColon) + lv_obj_get_width(_clockMin);
    if (clockW < TT_CAL_TIME_ADV) {
        clockW = TT_CAL_CLOCK_GLYPHS * TT_CAL_TIME_ADV;
    }
    const int sideW = TT_CAL_SIDE_W;
    if (lv_obj_get_width(_side) != sideW) {
        lv_obj_set_width(_side, sideW);
    }
    const int clockX = (sideW - clockW) / 2;
    lv_obj_align(_clockLabel, LV_ALIGN_BOTTOM_LEFT, clockX, -TT_CAL_CLOCK_INSET);
    lv_obj_align_to(_clockColon, _clockLabel, LV_ALIGN_OUT_RIGHT_BOTTOM, 0, -TT_CAL_COLON_SHIFT);
    lv_obj_align_to(_clockMin, _clockColon, LV_ALIGN_OUT_RIGHT_BOTTOM, 0, TT_CAL_COLON_SHIFT);
    if (_weatherIcon != nullptr) {
        lv_obj_align(_weatherIcon, LV_ALIGN_TOP_MID, 0, TT_CAL_ICON_Y);
    }
    if (_weekLabel != nullptr) {
        lv_obj_set_width(_weekLabel, LV_SIZE_CONTENT);
    }
    if (_tempLabel == nullptr || _tempUnit == nullptr || _condLabel == nullptr) {
        return;
    }
    if (_haveWeather) {
        lv_obj_set_hidden(_tempLabel, false);
        lv_obj_set_hidden(_tempUnit, false);
    } else {
        lv_obj_set_hidden(_tempLabel, true);
        lv_obj_set_hidden(_tempUnit, true);
    }
    const char* condText = lv_label_get_text(_condLabel);
    const bool showCond = condText != nullptr && condText[0] != '\0';
    lv_obj_set_hidden(_condLabel, !showCond);
    lv_obj_update_layout(_tempLabel);
    lv_obj_update_layout(_tempUnit);
    lv_obj_update_layout(_condLabel);
    const int tempW = _haveWeather ? lv_obj_get_width(_tempLabel) : 0;
    const int unitW = _haveWeather ? lv_obj_get_width(_tempUnit) : 0;
    const int condW = showCond ? lv_obj_get_width(_condLabel) : 0;
    const int groupW = tempW + TT_CAL_TEMP_DOT_GAP_X + unitW + TT_CAL_INFO_GAP_X + condW;
    int groupX = (sideW - groupW) / 2;
    if (groupX < 0) {
        groupX = 0;
    }
    lv_obj_set_pos(_tempLabel, groupX, TT_CAL_TEMP_Y);
    lv_obj_align_to(_tempUnit, _tempLabel, LV_ALIGN_OUT_RIGHT_TOP,
                    TT_CAL_TEMP_DOT_GAP_X, TT_CAL_TEMP_DOT_GAP_Y);
    lv_obj_update_layout(_tempLabel);
    lv_obj_update_layout(_tempUnit);
    const int tempY = lv_obj_get_y(_tempLabel);
    const int tempH = lv_obj_get_height(_tempLabel);
    const int condH = lv_obj_get_height(_condLabel);
    const int stackY = tempY + (tempH - condH) / 2;
    const int infoX = lv_obj_get_x(_tempUnit) + lv_obj_get_width(_tempUnit) + TT_CAL_INFO_GAP_X;
    int infoW = sideW - infoX - TT_CAL_SIDE_PAD;
    if (infoW < 24) {
        infoW = 24;
    }
    lv_obj_set_style_max_width(_condLabel, infoW, 0);
    lv_obj_set_style_text_color(_condLabel, lv_color_white(), 0);
    lv_obj_set_pos(_condLabel, infoX, stackY);

    if (_dateLabel == nullptr || _weekLabel == nullptr) {
        return;
    }
    lv_obj_update_layout(_dateLabel);
    lv_obj_update_layout(_weekLabel);
    lv_obj_update_layout(_clockLabel);
    const int clockTop = lv_obj_get_y(_clockLabel);
    const int weekH = lv_obj_get_height(_weekLabel);
    const int dateH = lv_obj_get_height(_dateLabel);
    const int weekBoxGap = TT_CAL_DATE_TIME_GAP
        - (weekH - 1 - TT_CAL_EN32_INK_BOTTOM) - TT_CAL_EN40_INK_TOP;
    const int dateBoxGap = TT_CAL_DATE_TIME_GAP
        - (dateH - 1 - TT_CAL_EN32_INK_BOTTOM) - TT_CAL_EN32_INK_TOP;
    const int weekY = clockTop - weekBoxGap - weekH;
    const int dateY = weekY - dateBoxGap - dateH;
    lv_obj_align(_dateLabel, LV_ALIGN_TOP_MID, 0, dateY);
    lv_obj_align(_weekLabel, LV_ALIGN_TOP_MID, 0, weekY);
}

bool TTDaySide::updateClock() {
    if (_clockLabel == nullptr || _clockMin == nullptr || _dateLabel == nullptr || _weekLabel == nullptr) {
        return false;
    }
    struct tm local = {};
    if (!TTInstanceOf<TTRtc>().getLocalTime(local)) {
        return false;
    }
    const int minute = local.tm_yday * 24 * 60 + local.tm_hour * 60 + local.tm_min;
    if (minute == _lastMinute) {
        return false;
    }
    _lastMinute = minute;
    char buf[16];
    snprintf(buf, sizeof(buf), "%02d", local.tm_hour);
    lv_label_set_text(_clockLabel, buf);
    snprintf(buf, sizeof(buf), "%02d", local.tm_min);
    lv_label_set_text(_clockMin, buf);
    snprintf(buf, sizeof(buf), "%d-%d", local.tm_mon + 1, local.tm_mday);
    lv_label_set_text(_dateLabel, buf);
    lv_label_set_text(_weekLabel, weekEn(local.tm_wday));
    return true;
}

void TTDaySide::setWeather(int code, bool isDay, float tempC) {
    if (_tempLabel == nullptr || _condLabel == nullptr || _weatherIcon == nullptr) {
        return;
    }
    _haveWeather = true;
    char path[TT_WEATHER_ICON_PATH_MAX];
    tt_weather_condition_path(path, sizeof(path), code, isDay, TT_CAL_ICON);
    tt_stream_image_set_src(_weatherIcon, path);
    char buf[24];
    snprintf(buf, sizeof(buf), "%.0f", tempC);
    lv_label_set_text(_tempLabel, buf);
    lv_label_set_text(_condLabel, tt_weather_condition_text(code));
}

bool TTDaySide::ready() const {
    return _tempLabel != nullptr;
}

int TTDaySide::minute() const {
    return _lastMinute;
}

lv_obj_t* TTDaySide::root() const {
    return _side;
}

lv_obj_t* TTDaySide::clockLabel() const {
    return _clockLabel;
}
