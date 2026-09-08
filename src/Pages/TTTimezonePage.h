#pragma once

#include <lvgl.h>
#include "../Base/TTScreenPage.h"

#define TT_TZ_BTN_W     126
#define TT_TZ_BTN_H     26
#define TT_TZ_BTN_GAP   8
#define TT_TZ_GRID_W    260
#define TT_TZ_GRID_H    72
#define TT_TZ_HINT_TOP  8
#define TT_TZ_HINT_LEFT 16
#define TT_TZ_BACK_INDEX  (-1)

class TTTimezonePage : public TTScreenPage {
public:
    TTTimezonePage() : TTScreenPage("时区") {}

protected:
    void buildContent(lv_obj_t* screen) override;

private:
    void showRegions();
    void showCities(int regionIndex);
    void clearChoices();
    void addChoice(const char* text, int index);
    void onChoice(int index);
    void saveCity(int cityIndex);
    static void onChoiceEvent(lv_event_t* e);

    lv_obj_t* _hintLabel = nullptr;
    lv_obj_t* _grid = nullptr;
    int _regionIndex = -1;
};
