#include "TTClockScreenPage.h"
#include "../Base/Logger.h"
#include "../Base/TTFontManager.h"
#include "../Base/TTStreamImage.h"
#include "../Base/TTInstance.h"
#include "../Base/TTRtc.h"
#include <ctime>

void TTClockScreenPage::buildContent(lv_obj_t* screen) {
    TTFontManager& fm = TTFontManager::instance();
    lv_font_t* font_16 = fm.getFont(16);
    lv_font_t* font_10 = fm.getFont(10);
    lv_font_t* font_48 = fm.getFont(48);

    lv_obj_set_style_bg_color(screen, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    _titleLabel = lv_label_create(screen);
    lv_label_set_text(_titleLabel, "电子墨水屏时钟");
    lv_obj_set_style_text_color(_titleLabel, lv_color_black(), 0);
    lv_obj_set_style_text_font(_titleLabel, font_16, 0);
    lv_obj_align(_titleLabel, LV_ALIGN_TOP_MID, 0, 4);

    lv_obj_t* testLabel = lv_label_create(screen);
    lv_label_set_text(testLabel, "Claude Code、Cursor 和 Lovable 等 AI 辅助编程助手让用户几乎无需手动编码就能将其意图转化为可工作的应用。");
    lv_obj_set_style_text_color(testLabel, lv_color_black(), 0);
    lv_obj_set_style_text_font(testLabel, font_10, 0);
    lv_obj_set_width(testLabel, lv_pct(100));
    lv_obj_set_style_pad_left(testLabel, 4, 0);
    lv_obj_set_style_pad_right(testLabel, 4, 0);
    lv_obj_set_style_text_line_space(testLabel, 4, 0);
    lv_obj_align(testLabel, LV_ALIGN_TOP_MID, 0, 24);

    lv_obj_t* timeContainer = lv_obj_create(screen);
    lv_obj_set_size(timeContainer,124, 44);
    lv_obj_set_style_bg_opa(timeContainer, LV_OPA_TRANSP, 0);
    lv_obj_align(timeContainer, LV_ALIGN_CENTER, 0, 18);

    _timeLabel = lv_label_create(timeContainer);
    lv_label_set_text(_timeLabel, "00:00");
    lv_obj_set_style_text_color(_timeLabel, lv_color_black(), 0);
    lv_obj_set_style_text_font(_timeLabel, font_48, 0);
    lv_obj_align(_timeLabel, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t* timeIcon = tt_stream_image_create(screen);
    tt_stream_image_set_src(timeIcon, "/icons/clock_sm.i1");
    lv_obj_align_to(timeIcon, timeContainer, LV_ALIGN_OUT_LEFT_MID, -6, 0);
}

void TTClockScreenPage::setup() {
    TTScreenPage::setup();
    _lastMinute = -1;
    updateClockDisplay();

    runRepeat(TT_CLOCK_TIMER_MS, [this]() { onTimerTick(); }, false);
}

void TTClockScreenPage::willAppear() {
    TTScreenPage::willAppear();
    _lastMinute = -1;
    updateClockDisplay();
}

void TTClockScreenPage::onTimerTick() {
    struct tm t;
    if (!TTInstanceOf<TTRtc>().getLocalTime(t)) {
        if (_lastMinute != -2) {
            _lastMinute = -2;
            updateClockDisplay();
            requestRefresh(TT_REFRESH_PARTIAL);
        }
        return;
    }
    if (t.tm_min == _lastMinute) {
        return;
    }
    _lastMinute = t.tm_min;
    LOG_I("Time: %02d:%02d:%02d", t.tm_hour, t.tm_min, t.tm_sec);
    updateClockDisplay();
    requestRefresh(TT_REFRESH_PARTIAL);
    LOG_I("Clock refreshed.");
}

void TTClockScreenPage::updateClockDisplay() {
    struct tm t;
    if (!TTInstanceOf<TTRtc>().getLocalTime(t)) {
        lv_label_set_text(_timeLabel, "--:--");
        return;
    }
    char timeStr[8];
    snprintf(timeStr, sizeof(timeStr), "%02d:%02d", t.tm_hour, t.tm_min);
    lv_label_set_text(_timeLabel, timeStr);
}
