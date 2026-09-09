#include "TTAsyncQueue.h"
#include "Logger.h"

void TTAsyncQueue::setup() {
    LOG_I("AsyncQueue: started stack=%u", (unsigned)TT_ASYNC_STACK);
}

void TTAsyncQueue::loop() {
}

bool TTAsyncQueue::post(std::function<void()> job) {
    _pending++;
    auto* func = new std::function<void()>([this, job]() {
        job();
        if (_pending > 0) {
            _pending--;
        }
    });
    enqueue(func);
    return true;
}

bool TTAsyncQueue::busy() const {
    return _pending > 0;
}
