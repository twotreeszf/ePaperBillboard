#pragma once

#include <lvgl.h>

#define TT_CAL_CLOCK_GLYPHS    5
#define TT_CAL_ICON            97
#define TT_CAL_ICON_Y          2
#define TT_CAL_TEMP_FONT       40
#define TT_CAL_TEMP_X          2
#define TT_CAL_TEMP_Y          (TT_CAL_ICON_Y + TT_CAL_ICON + 2)
#define TT_CAL_TEMP_DOT_GAP_X  3
#define TT_CAL_TEMP_DOT_GAP_Y  7
#define TT_CAL_DOT_LG          8
#define TT_CAL_DOT_BORDER      1
#define TT_CAL_INFO_GAP_X      6
#define TT_CAL_DATE_EN_FONT    32
#define TT_CAL_WEEK_FONT       32
#define TT_CAL_TIME_FONT       40
#define TT_CAL_TIME_ADV        24
#define TT_CAL_CLOCK_INSET     4
#define TT_CAL_COLON_SHIFT     3
#define TT_CAL_DATE_TIME_GAP   10
#define TT_CAL_EN32_INK_TOP    5
#define TT_CAL_EN32_INK_BOTTOM 29
#define TT_CAL_EN40_INK_TOP    7
#define TT_CAL_COND_FONT       16
#define TT_CAL_CAPSULE_PAD_X   5
#define TT_CAL_COND_TEXT_W     40
#define TT_CAL_COND_PAD_Y      1
#define TT_CAL_TEMP_GLYPHS     2
#define TT_CAL_SIDE_PAD        2
#define TT_CAL_SIDE_WEATHER_W  (TT_CAL_TEMP_GLYPHS * TT_CAL_TIME_ADV + TT_CAL_TEMP_DOT_GAP_X \
    + TT_CAL_DOT_LG + TT_CAL_INFO_GAP_X + TT_CAL_COND_TEXT_W + TT_CAL_CAPSULE_PAD_X * 2 \
    + TT_CAL_SIDE_PAD * 2)
#define TT_CAL_SIDE_CLOCK_W    (TT_CAL_CLOCK_GLYPHS * TT_CAL_TIME_ADV)
#define TT_CAL_SIDE_W          ((TT_CAL_SIDE_WEATHER_W) > (TT_CAL_SIDE_CLOCK_W) ? \
    (TT_CAL_SIDE_WEATHER_W) : (TT_CAL_SIDE_CLOCK_W))

class TTDaySide {
public:
    void build(lv_obj_t* parent);
    void layout();
    bool updateClock();
    void setWeather(int code, bool isDay, float tempC);
    bool ready() const;
    int minute() const;
    lv_obj_t* root() const;
    lv_obj_t* clockLabel() const;

private:
    static void drawCondCapsule(lv_event_t* e);

    lv_obj_t* _side = nullptr;
    lv_obj_t* _weatherIcon = nullptr;
    lv_obj_t* _tempLabel = nullptr;
    lv_obj_t* _tempUnit = nullptr;
    lv_obj_t* _condLabel = nullptr;
    lv_obj_t* _dateLabel = nullptr;
    lv_obj_t* _weekLabel = nullptr;
    lv_obj_t* _clockLabel = nullptr;
    lv_obj_t* _clockColon = nullptr;
    lv_obj_t* _clockMin = nullptr;
    int _lastMinute = -1;
    bool _haveWeather = false;
};
