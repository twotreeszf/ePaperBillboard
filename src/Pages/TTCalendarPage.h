#pragma once

#include "../Base/TTFile.h"
#include "../Base/TTScreenPage.h"
#include "../Base/TTCalendarTypes.h"
#include "../Base/TTNotificationPayloads.h"
#include "../Base/TTWeatherTypes.h"

#define TT_CAL_CLOCK_GLYPHS    5
#define TT_CAL_AGE_ICON        8
#define TT_CAL_AGE_ICON_GAP    2
#define TT_CAL_AGE_ICON_DY     3
#define TT_CAL_AGE_PAD         4
#define TT_CAL_AGE_OK_SRC      TT_FS_RES_DIR "/icons/weather/check_8.i1"
#define TT_CAL_AGE_FAIL_SRC    TT_FS_RES_DIR "/icons/weather/cross_8.i1"
#define TT_CAL_PAGE_SLOTS   10
#define TT_CAL_ROW_H        26
#define TT_CAL_EVENT_LEAD_TRIM 2
#define TT_CAL_ROW_MAX      80
#define TT_CAL_PAGE_MAX     16
#define TT_CAL_PAD             6
#define TT_CAL_PAGE_FOOT       12
#define TT_CAL_DATE_EVENT_GAP  6
#define TT_CAL_DAY_RULE        16
#define TT_CAL_AXIS_GAP        ((TT_CAL_MARK_LG / 2) + TT_CAL_SIDE_W - ((TT_CAL_SIDE_W + TT_CAL_ICON) / 2))
#define TT_CAL_AXIS_X          (TT_CAL_MARK_LG / 2)
#define TT_CAL_TEXT_X          (TT_CAL_AXIS_X + TT_CAL_AXIS_GAP)
#define TT_CAL_DOT_SM          4
#define TT_CAL_DOT_LG          8
#define TT_CAL_DOT_BORDER      1
#define TT_CAL_MARK_SM         8
#define TT_CAL_MARK_SM_BORDER  1
#define TT_CAL_MARK_LG         16
#define TT_CAL_MARK_NEXT       12
#define TT_CAL_MARK_CORE       8
#define TT_CAL_MARK_BORDER     1
#define TT_CAL_MARK_DY         1
#define TT_CAL_CURRENT_LEAD_SEC (15 * 60)
#define TT_CAL_RULE_DASH       3
#define TT_CAL_RULE_GAP        2
#define TT_CAL_AXIS_STUB_GAP   2
#define TT_CAL_AXIS_STUB       (TT_CAL_RULE_DASH * 3 + TT_CAL_RULE_GAP * 2)
#define TT_CAL_ICON            97
#define TT_CAL_ICON_Y          2
#define TT_CAL_TEMP_FONT       40
#define TT_CAL_TEMP_X          2
#define TT_CAL_TEMP_Y          (TT_CAL_ICON_Y + TT_CAL_ICON + 2)
#define TT_CAL_TEMP_DOT_GAP_X  3
#define TT_CAL_TEMP_DOT_GAP_Y  7
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
#define TT_CAL_TEXT_FONT       16
#define TT_CAL_COND_FONT       16
#define TT_CAL_DATE_FONT       16
#define TT_CAL_EVENT_FONT      12
#define TT_CAL_EVENT_DIGIT_ADV 7
#define TT_CAL_EVENT_COLON_ADV 5
#define TT_CAL_EVENT_TIME_W    (TT_CAL_EVENT_DIGIT_ADV * 4 + TT_CAL_EVENT_COLON_ADV)
#define TT_CAL_EVENT_TIME_GAP  6
#define TT_CAL_PAGE_FONT       10
#define TT_CAL_CAPSULE_PAD_X   5
#define TT_CAL_CAPSULE_PAD_Y   1
#define TT_CAL_CAPSULE_BORDER  1
#define TT_CAL_COND_TEXT_W     40
#define TT_CAL_TEMP_GLYPHS     2
#define TT_CAL_SIDE_PAD        2
#define TT_CAL_SIDE_WEATHER_W  (TT_CAL_TEMP_GLYPHS * TT_CAL_TIME_ADV + TT_CAL_TEMP_DOT_GAP_X \
    + TT_CAL_DOT_LG + TT_CAL_INFO_GAP_X + TT_CAL_COND_TEXT_W + TT_CAL_CAPSULE_PAD_X * 2 \
    + TT_CAL_SIDE_PAD * 2)
#define TT_CAL_SIDE_CLOCK_W    (TT_CAL_CLOCK_GLYPHS * TT_CAL_TIME_ADV)
#define TT_CAL_SIDE_W          ((TT_CAL_SIDE_WEATHER_W) > (TT_CAL_SIDE_CLOCK_W) ? \
    (TT_CAL_SIDE_WEATHER_W) : (TT_CAL_SIDE_CLOCK_W))

enum TTCalCapsule {
    TT_CAL_CAPSULE_NONE = 0,
    TT_CAL_CAPSULE_FILL,
    TT_CAL_CAPSULE_STROKE,
};

enum TTCalMark {
    TT_CAL_MARK_NONE = 0,
    TT_CAL_MARK_SMALL,
    TT_CAL_MARK_HOLLOW,
    TT_CAL_MARK_FILLED,
};

enum TTCalRowKind {
    TT_CAL_ROW_DAY = 1,
    TT_CAL_ROW_EVENT,
};

struct TTCalRow {
    uint8_t kind;
    uint8_t eventIndex;
    int16_t height;
};

class TTCalendarPage : public TTScreenPage {
public:
    TTCalendarPage() : TTScreenPage("日历") {}

    void setup() override;
    void willAppear() override;
    void willDisappear() override;

protected:
    void buildContent(lv_obj_t* screen) override;
    bool handleKeyAction(TTKeyId key, TTKeyGesture gesture) override;

private:
    void requestCalendar(bool extend, bool force = false);
    void requestWeather(bool force = false);
    void applyCalendar(const TTCalendarPayload& payload);
    void applyWeather(const TTWeatherPayload& payload);
    void forceRefresh();
    void onSleepWake(const TTSleepWakePayload& wake);
    void onTimeTick();
    void finishFetch();
    void tryRequestLightSleep();
    void cancelInputIdleSleep();
    void dismissExtendLoading();
    void layoutSide();
    void layoutAge();
    void setStatusTimeVisible(bool visible);
    void updateClock(bool refreshIfChanged);
    void rebuildRows();
    void showPage();
    void pageBy(int delta);
    static void onListDraw(lv_event_t* e);

    lv_obj_t* _side = nullptr;
    lv_obj_t* _weatherIcon = nullptr;
    lv_obj_t* _tempLabel = nullptr;
    lv_obj_t* _tempUnit = nullptr;
    lv_obj_t* _condLabel = nullptr;
    lv_obj_t* _ageIcon = nullptr;
    lv_obj_t* _ageLabel = nullptr;
    lv_obj_t* _dateLabel = nullptr;
    lv_obj_t* _weekLabel = nullptr;
    lv_obj_t* _clockLabel = nullptr;
    lv_obj_t* _clockColon = nullptr;
    lv_obj_t* _clockMin = nullptr;
    lv_obj_t* _list = nullptr;
    lv_obj_t* _status = nullptr;
    lv_obj_t* _pageLabel = nullptr;
    lv_obj_t* _rows[TT_CAL_PAGE_SLOTS] = {};
    lv_obj_t* _eventTimes[TT_CAL_PAGE_SLOTS] = {};

    TTCalEvent _events[TT_CAL_EVENT_MAX];
    TTCalRow _plan[TT_CAL_ROW_MAX];
    uint16_t _pageStart[TT_CAL_PAGE_MAX] = {};
    uint8_t _slotMark[TT_CAL_PAGE_SLOTS] = {};
    uint8_t _slotCapsule[TT_CAL_PAGE_SLOTS] = {};
    uint8_t _slotRule[TT_CAL_PAGE_SLOTS] = {};
    int16_t _slotY[TT_CAL_PAGE_SLOTS] = {};
    uint8_t _count = 0;
    uint16_t _planCount = 0;
    uint8_t _pageCount = 0;
    uint8_t _page = 0;
    uint8_t _visibleSlots = 0;
    int32_t _rangeStart = 0;
    int32_t _rangeEnd = 0;
    int _lastMinute = -1;
    bool _visible = false;
    bool _haveWeather = false;
    bool _calendarReady = false;
    bool _forceRefreshing = false;
    bool _calendarFetching = false;
    bool _extendLoading = false;
    bool _weatherFetching = false;
    bool _sleepAfterTimeTick = false;
    bool _ageOk = true;
    uint32_t _inputIdleSleepHandle = 0;
    int _weatherCode = 0;
    bool _weatherDay = true;
    uint32_t _fetchedAt = 0;
};
