#pragma once

#include <lvgl.h>

class TTNavigationController;

class TTScreenPage {
public:
    virtual ~TTScreenPage();

    void createScreen();
    lv_obj_t* getScreen() const { return _screen; }
    lv_group_t* getGroup() const { return _group; }

    void setNavigationController(TTNavigationController* nav) { _controller = nav; }
    void addToFocusGroup(lv_obj_t* obj);
    void requestRefresh(bool fullRefresh = false);

    /** Called once in createScreen() right after buildContent(screen). Use for post-build init (e.g. subscribe to notifications). */
    virtual void setup() {}
    /** Called just before the page is removed from the nav stack (setRoot: each page; pop: the popped page). Use for teardown (e.g. unsubscribe). */
    virtual void willDestroy() {}

    /** Called when this page is about to become the active screen (push: new page; pop: previous page). */
    virtual void willAppear() {}
    /** Called when this page is about to leave the top (setRoot/push/pop). */
    virtual void willDisappear() {}

    /** Called every tick by the nav controller for the current top page only. */
    virtual void loop() {}

protected:
    lv_group_t* createGroup();

    TTScreenPage() = default;
    TTScreenPage(const TTScreenPage&) = delete;
    TTScreenPage& operator=(const TTScreenPage&) = delete;

    virtual void buildContent(lv_obj_t* screen) = 0;

    lv_obj_t* _screen = nullptr;
    lv_group_t* _group = nullptr;
    TTNavigationController* _controller = nullptr;
};
