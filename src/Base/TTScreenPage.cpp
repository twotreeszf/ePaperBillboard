#include "TTScreenPage.h"

TTScreenPage::~TTScreenPage() {
    if (_screen != nullptr) {
        lv_obj_delete(_screen);
        _screen = nullptr;
    }
}

void TTScreenPage::createScreen() {
    if (_screen != nullptr) return;
    _screen = lv_obj_create(NULL);
    buildContent();
}
