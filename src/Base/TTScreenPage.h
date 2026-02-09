#pragma once

#include <lvgl.h>
#include "ITTScreenPage.h"
#include "ITTNavigationController.h"

class TTScreenPage : public ITTScreenPage {
public:
    virtual ~TTScreenPage();

    const char* getName() const override { return _name; }
    void createScreen() override;
    lv_obj_t* getScreen() const override { return _screen; }
    lv_group_t* getGroup() const override { return _group; }

    void setNavigationController(ITTNavigationController* nav) override { _controller = nav; }
    ITTNavigationController* getNavigationController() const override { return _controller; }
    void addToFocusGroup(lv_obj_t* obj) override;
    void requestRefresh(bool fullRefresh = false) override;

    /** Called once in createScreen() right after buildContent(screen). Use for post-build init (e.g. subscribe to notifications). */
    void setup() override;
    /** Called just before the page is removed from the nav stack (setRoot: each page; pop: the popped page). Use for teardown (e.g. unsubscribe). */
    void willDestroy() override;

    /** Called when this page is about to become the active screen (push: new page; pop: previous page). */
    void willAppear() override;
    /** Called when this page is about to leave the top (setRoot/push/pop). */
    void willDisappear() override;

protected:
    lv_group_t* createGroup();

    TTScreenPage(const char* name) : _name(name) {}
    TTScreenPage(const TTScreenPage&) = delete;
    TTScreenPage& operator=(const TTScreenPage&) = delete;

    virtual void buildContent(lv_obj_t* screen) = 0;

    const char* _name = nullptr;
    lv_obj_t* _screen = nullptr;
    lv_group_t* _group = nullptr;
    ITTNavigationController* _controller = nullptr;
};
