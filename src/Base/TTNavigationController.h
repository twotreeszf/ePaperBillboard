#pragma once

#include <lvgl.h>
#include <memory>
#include <vector>

#include "TTScreenPage.h"

class TTNavigationController {
public:
    void setRoot(std::unique_ptr<TTScreenPage> page);
    void push(std::unique_ptr<TTScreenPage> page);
    void pop();

    void tick();

    TTScreenPage* getCurrentPage();
    void requestRefresh(TTScreenPage* page, bool fullRefresh);

    bool canPop() const { return _stack.size() > 1; }
    size_t stackSize() const { return _stack.size(); }

private:
    void loadScreen(TTScreenPage* page);

    std::vector<std::unique_ptr<TTScreenPage>> _stack;
};
