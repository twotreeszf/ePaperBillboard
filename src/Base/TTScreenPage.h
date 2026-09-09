#pragma once

#include <functional>
#include <vector>
#include <lvgl.h>
#include "ITTScreenPage.h"
#include "ITTNavigationController.h"
#include "TTNotificationCenter.h"
#include "TTInstance.h"

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
    void requestRefresh(TTRefreshLevel level = TT_REFRESH_PARTIAL) override;
    void registerKeyAction(TTKeyId key, TTKeyGesture gesture, std::function<void()> action);
    bool handleKeyAction(TTKeyId key, TTKeyGesture gesture) override;

    void runOnce(uint32_t delayMs, std::function<void()> callback);
    uint32_t runRepeat(uint32_t intervalMs, std::function<void()> callback, bool executeImmediately = true);
    void cancelRepeat(uint32_t handle);

    template<typename PayloadType>
    void subscribe(const char* name, std::function<void(const PayloadType&)> callback);

    void setup() override;
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
    std::vector<uint32_t> _timerHandles;
    struct TTKeyBinding {
        TTKeyId key;
        TTKeyGesture gesture;
        std::function<void()> action;
    };
    std::vector<TTKeyBinding> _keyActions;
};

template<typename PayloadType>
void TTScreenPage::subscribe(const char* name, std::function<void(const PayloadType&)> callback) {
    TTInstanceOf<TTNotificationCenter>().subscribe<PayloadType>(name, this, std::move(callback));
}
