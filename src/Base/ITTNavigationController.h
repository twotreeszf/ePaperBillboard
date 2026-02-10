#pragma once

#include <lvgl.h>
#include <memory>
#include <type_traits>
#include "TTRefreshLevel.h"

class ITTScreenPage;

class ITTNavigationController {
public:
    virtual ~ITTNavigationController() = default;

    virtual void setRoot(std::unique_ptr<ITTScreenPage> page) = 0;
    virtual void push(std::unique_ptr<ITTScreenPage> page) = 0;
    virtual void pop() = 0;

    virtual ITTScreenPage* getCurrentPage() = 0;
    virtual void requestRefresh(ITTScreenPage* page, TTRefreshLevel level = TT_REFRESH_PARTIAL) = 0;

    virtual bool canPop() const = 0;
    virtual size_t stackSize() const = 0;

    template<typename T>
    void pushPage(std::unique_ptr<T> page) {
        static_assert(std::is_base_of<ITTScreenPage, T>::value, "T must inherit from ITTScreenPage");
        push(std::unique_ptr<ITTScreenPage>(page.release()));
    }

    template<typename T>
    void setRootPage(std::unique_ptr<T> page) {
        static_assert(std::is_base_of<ITTScreenPage, T>::value, "T must inherit from ITTScreenPage");
        setRoot(std::unique_ptr<ITTScreenPage>(page.release()));
    }
};
