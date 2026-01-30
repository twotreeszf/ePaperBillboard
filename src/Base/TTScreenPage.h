#pragma once

#include <lvgl.h>

class TTNavigationController;

class TTScreenPage {
public:
    virtual ~TTScreenPage();

    void createScreen();
    lv_obj_t* getScreen() const { return _screen; }

    void setNavigationController(TTNavigationController* nav) { _controller = nav; }
    void requestRefresh(bool fullRefresh = false);

    virtual void loop() {}

    virtual void willAppear() {}
    virtual void didAppear() {}
    virtual void willDisappear() {}
    virtual void didDisappear() {}

protected:
    TTScreenPage() = default;
    TTScreenPage(const TTScreenPage&) = delete;
    TTScreenPage& operator=(const TTScreenPage&) = delete;

    virtual void buildContent() = 0;

    lv_obj_t* _screen = nullptr;
    TTNavigationController* _controller = nullptr;
};
