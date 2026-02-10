#pragma once

#include <lvgl.h>
#include "TTRefreshLevel.h"

class ITTNavigationController;

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

    virtual void setup() {}
    virtual void willDestroy() {}
    virtual void willAppear() {}
    virtual void willDisappear() {}
};
