#pragma once

#include <lvgl.h>

#define TT_NAV_BAR_HEIGHT  20
#define TT_NAV_BAR_FONT    12
#define TT_NAV_BAR_PAD     4
#define TT_NAV_ARROW_W     14
#define TT_NAV_DIVIDER_H   1
#define TT_NAV_BACK_ICON   "/icons/back.png"

class ITTNavigationController;

class TTNavigationBar {
public:
    void begin(lv_obj_t* parent, ITTNavigationController* nav);
    void show(const char* title, bool showBack);
    void hide();
    bool isVisible() const { return _visible; }
    lv_obj_t* getObject() const { return _bar; }
    lv_obj_t* getBackButton() const { return _backBtn; }

private:
    static void onBackClicked(lv_event_t* e);

    ITTNavigationController* _nav = nullptr;
    lv_obj_t* _bar = nullptr;
    lv_obj_t* _title = nullptr;
    lv_obj_t* _backBtn = nullptr;
    bool _visible = false;
};
