#pragma once

#include <lvgl.h>
#include "TTRefreshLevel.h"

class ITTNavigationController;

enum TTKeyId {
    TT_KEY_LEFT = 0,
    TT_KEY_RIGHT,
    TT_KEY_CENTER,
};

enum TTKeyGesture {
    TT_KEY_CLICK = 0,
    TT_KEY_LONG_PRESS,
};

class ITTScreenPage {
public:
    virtual ~ITTScreenPage() = default;

    virtual const char* getName() const = 0;
    virtual void createScreen() = 0;
    virtual lv_obj_t* getScreen() const = 0;
    virtual lv_group_t* getGroup() const = 0;

    virtual void setNavigationController(ITTNavigationController* nav) = 0;
    virtual ITTNavigationController* getNavigationController() const = 0;
    virtual void addToFocusGroup(lv_obj_t* obj) = 0;
    virtual void requestRefresh(TTRefreshLevel level = TT_REFRESH_PARTIAL) = 0;

    virtual bool handleKeyAction(TTKeyId, TTKeyGesture) { return false; }

    virtual void setup() {}
    virtual void willDestroy() {}
    virtual void willAppear() {}
    virtual void willDisappear() {}
    virtual TTRefreshLevel enterRefreshLevel() const { return TT_REFRESH_FULL; }
};
