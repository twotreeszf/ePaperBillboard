#include "TTNavigationController.h"
#include "TTKeypadInput.h"
#include "Logger.h"
#include "TTInstance.h"
#include "TTLvglEpdDriver.h"

void TTNavigationController::setRoot(std::unique_ptr<TTScreenPage> page) {
    for (auto& p : _stack) {
        p->willDisappear();
        p->willDestroy();
    }
    _stack.clear();
    if (!page) return;
    page->createScreen();
    _stack.push_back(std::move(page));
    _stack.back()->setNavigationController(this);
    loadScreen(_stack.back().get());
}

void TTNavigationController::push(std::unique_ptr<TTScreenPage> page) {
    if (!page) return;
    TTScreenPage* raw = page.get();
    raw->createScreen();
    if (!_stack.empty()) {
        _stack.back()->willDisappear();
    }
    raw->willAppear();
    loadScreen(raw);
    _stack.push_back(std::move(page));
    _stack.back()->setNavigationController(this);
}

void TTNavigationController::pop() {
    if (_stack.size() <= 1) {
        LOG_W("Nav: pop ignored (stack size %u)", (unsigned)_stack.size());
        return;
    }
    _stack.back()->willDisappear();
    TTScreenPage* prev = _stack[_stack.size() - 2].get();
    prev->willAppear();
    loadScreen(prev);
    _stack.back()->willDestroy();
    _stack.pop_back();
}

void TTNavigationController::tick() {
    if (!_stack.empty()) {
        _stack.back()->loop();
    }
}

TTScreenPage* TTNavigationController::getCurrentPage() {
    return _stack.empty() ? nullptr : _stack.back().get();
}

void TTNavigationController::loadScreen(TTScreenPage* page) {
    lv_screen_load(page->getScreen());
    if (_keypad != nullptr) {
        /* nullptr when page has no group: clears keypad focus so it does not target previous page. */
        lv_indev_set_group(_keypad->getIndev(), page->getGroup());
    }
}

void TTNavigationController::requestRefresh(TTScreenPage* page, bool fullRefresh) {
    if (getCurrentPage() == page) {
        TTInstanceOf<TTLvglEpdDriver>().requestRefresh(fullRefresh);
    }
}
