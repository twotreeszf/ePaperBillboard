#include "TTScreenPage.h"
#include "ITTNavigationController.h"

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
    _screen = lv_obj_create(NULL);
    buildContent(_screen);
    setup();
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
