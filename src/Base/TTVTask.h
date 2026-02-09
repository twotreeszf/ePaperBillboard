#ifndef TTVTASK_H
#define TTVTASK_H

#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <functional>
#include <vector>
#include "TTNotificationCenter.h"
#include "TTInstance.h"

struct TTPeriodicTask {
    std::function<void()> callback;
    uint32_t intervalMs;
    uint32_t lastExecuteTimeMs;
};

class TTVTask
{
public:
    TTVTask(const char* name = "TTVtask", uint32_t stackSize = 2048) 
        : _name(name), _stackSize(stackSize) {}
    virtual ~TTVTask() = default;

    void start(int coreId = 0, uint32_t loopDelayMs = 100);

    template<typename PayloadType>
    void postNotification(const char* name, const PayloadType& payload);

protected:
    virtual void setup() = 0;
    virtual void loop() = 0;
    void enqueue(std::function<void()>* func);
    void registerPeriodicTask(std::function<void()> callback, uint32_t intervalMs, bool executeImmediately = true);

private:
    void _task();
    void _checkPeriodicTasks();
    QueueHandle_t _queue = nullptr;
    std::vector<TTPeriodicTask> _periodicTasks;
    uint32_t _taskStartTime = 0;
    uint32_t _loopDelayMs = 100;
    const char* _name;
    uint32_t _stackSize;
};

template<typename PayloadType>
void TTVTask::postNotification(const char* name, const PayloadType& payload) {
    auto* f = new std::function<void()>([name, payload]() {
        TTInstanceOf<TTNotificationCenter>().post(name, payload);
    });
    enqueue(f);
}

#endif