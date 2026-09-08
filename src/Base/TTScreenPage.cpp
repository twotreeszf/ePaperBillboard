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
    for (uint32_t handle : _timerHandles) {
        TTInstanceOf<TTUITask>().cancelRepeat(handle);
    }
    if (!_timerHandles.empty()) {
        LOG_I("Page[%s]: cancelled %u timer(s)", _name, (unsigned)_timerHandles.size());
        _timerHandles.clear();
    }
    TTInstanceOf<TTNotificationCenter>().unsubscribeByObserver(this);
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
    if (_group == nullptr) {
        createGroup();
    }
    if (_group != nullptr) {
        lv_group_add_obj(_group, obj);
    }
}

void TTScreenPage::requestRefresh(TTRefreshLevel level) {
    if (_controller != nullptr) {
        _controller->requestRefresh(this, level);
    }
}

void TTScreenPage::runOnce(uint32_t delayMs, std::function<void()> callback) {
    uint32_t handle = TTInstanceOf<TTUITask>().runOnce(delayMs, std::move(callback));
    if (handle != 0) {
        _timerHandles.push_back(handle);
    }
}

uint32_t TTScreenPage::runRepeat(uint32_t intervalMs, std::function<void()> callback, bool executeImmediately) {
    uint32_t handle = TTInstanceOf<TTUITask>().runRepeat(intervalMs, std::move(callback), executeImmediately);
    if (handle != 0) {
        _timerHandles.push_back(handle);
    }
    return handle;
}

void TTScreenPage::cancelRepeat(uint32_t handle) {
    TTInstanceOf<TTUITask>().cancelRepeat(handle);
    for (size_t i = 0; i < _timerHandles.size(); ++i) {
        if (_timerHandles[i] == handle) {
            _timerHandles.erase(_timerHandles.begin() + static_cast<std::ptrdiff_t>(i));
            break;
        }
    }
}
