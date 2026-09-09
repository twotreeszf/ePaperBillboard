#pragma once

#include "TTVTask.h"
#include <functional>

#define TT_ASYNC_STACK    16384
#define TT_ASYNC_LOOP_MS  20
#define TT_ASYNC_NAME     "AsyncQueue"

class TTAsyncQueue : public TTVTask {
public:
    TTAsyncQueue() : TTVTask(TT_ASYNC_NAME, TT_ASYNC_STACK) {}

    bool post(std::function<void()> job);
    bool busy() const;

protected:
    void setup() override;
    void loop() override;

private:
    volatile int _pending = 0;
};
