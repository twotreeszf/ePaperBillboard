#pragma once

#include <lvgl.h>
#include <memory>
#include <vector>

#include "ITTNavigationController.h"
#include "ITTScreenPage.h"

class TTKeypadInput;
class TTScreenPage;

class TTNavigationController : public ITTNavigationController {
public:
    void setRoot(std::unique_ptr<ITTScreenPage> page) override;
    void push(std::unique_ptr<ITTScreenPage> page) override;
    void pop() override;

    ITTScreenPage* getCurrentPage() override;
    void requestRefresh(ITTScreenPage* page, bool fullRefresh) override;

    void setKeypadInput(TTKeypadInput* keypad) { _keypad = keypad; }

    bool canPop() const override { return _stack.size() > 1; }
    size_t stackSize() const override { return _stack.size(); }

private:
    void loadScreen(ITTScreenPage* page);

    std::vector<std::unique_ptr<ITTScreenPage>> _stack;
    TTKeypadInput* _keypad = nullptr;
};
