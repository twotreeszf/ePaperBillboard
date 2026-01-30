#include "TTScreenPage.h"
#include "TTNavigationController.h"

TTScreenPage::~TTScreenPage() {
    if (_screen != nullptr) {
        lv_obj_delete(_screen);
        _screen = nullptr;
    }
}

void TTScreenPage::createScreen() {
    if (_screen != nullptr) return;
    _screen = lv_obj_create(NULL);
    buildContent(_screen);
    setup();
}

void TTScreenPage::requestRefresh(bool fullRefresh) {
    if (_controller != nullptr) {
        _controller->requestRefresh(this, fullRefresh);
    }
}
