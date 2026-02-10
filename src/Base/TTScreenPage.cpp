#include "TTScreenPage.h"
#include "ITTNavigationController.h"
#include "Logger.h"
#include "TTInstance.h"
#include "../Tasks/TTUITask.h"

TTScreenPage::~TTScreenPage() {
    if (_group != nullptr) {
        lv_group_delete(_group);
        _group = nullptr;
    }
    if (_screen != nullptr) {
        lv_obj_delete(_screen);
        _screen = nullptr;
    }
}

void TTScreenPage::createScreen() {
    if (_screen != nullptr) return;
    LOG_I("Page[%s]: Creating screen", _name);
    _screen = lv_obj_create(NULL);
    buildContent(_screen);
    setup();
}

void TTScreenPage::setup() {
    LOG_I("Page[%s]: setup()", _name);
}

void TTScreenPage::willDestroy() {
    LOG_I("Page[%s]: willDestroy()", _name);
}

void TTScreenPage::willAppear() {
    LOG_I("Page[%s]: willAppear()", _name);
}

void TTScreenPage::willDisappear() {
    LOG_I("Page[%s]: willDisappear()", _name);
}

lv_group_t* TTScreenPage::createGroup() {
    if (_group == nullptr) {
        _group = lv_group_create();
    }
    return _group;
}

void TTScreenPage::addToFocusGroup(lv_obj_t* obj) {
    if (_group != nullptr) {
        lv_group_add_obj(_group, obj);
    }
}

void TTScreenPage::requestRefresh(bool fullRefresh) {
    if (_controller != nullptr) {
        _controller->requestRefresh(this, fullRefresh);
    }
}

void TTScreenPage::runOnce(uint32_t delayMs, std::function<void()> callback) {
    TTInstanceOf<TTUITask>().runOnce(delayMs, std::move(callback));
}

uint32_t TTScreenPage::runRepeat(uint32_t intervalMs, std::function<void()> callback, bool executeImmediately) {
    return TTInstanceOf<TTUITask>().runRepeat(intervalMs, std::move(callback), executeImmediately);
}

void TTScreenPage::cancelRepeat(uint32_t handle) {
    TTInstanceOf<TTUITask>().cancelRepeat(handle);
}
