#include "TTVTask.h"
#include "Logger.h"
#include "ErrorCheck.h"
#include <freertos/task.h>
#include <Arduino.h>

void TTVTask::start(int coreId, uint32_t loopDelayMs)
{
    _loopDelayMs = loopDelayMs;
    // Create queue for function pointers
    _queue = xQueueCreate(10, sizeof(std::function<void()> *));

    // Create task with lambda
    xTaskCreatePinnedToCore(
        [](void *param)
        {
            TTVTask *task = static_cast<TTVTask *>(param);
            task->_task();
            vTaskDelete(nullptr);
        },
        _name,      // Text name for the task
        _stackSize, // Stack size in bytes
        this,       // Parameter passed into the task
        1,          // Task priority
        NULL,       // Task handle
        coreId      // Core where the task should run
    );

    LOG_I("Task %s started on core %d", _name, coreId);
}

void TTVTask::enqueue(std::function<void()> *func)
{
    if (_queue != nullptr)
    {
        xQueueSend(_queue, &func, portMAX_DELAY);
    }
}

void TTVTask::_registerPeriodicTask(std::function<void()> callback, uint32_t intervalMs, bool executeImmediately, bool runOnce, uint32_t* outId)
{
    TTPeriodicTask task;
    task.callback = std::move(callback);
    task.intervalMs = intervalMs;
    task.runOnce = runOnce;
    if (!runOnce && outId != nullptr) {
        task.id = ++_nextPeriodicId;
        *outId = task.id;
    }

    if (executeImmediately) {
        task.callback();
        task.lastExecuteTimeMs = millis();
    } else {
        task.lastExecuteTimeMs = millis() - intervalMs;
    }

    _periodicTasks.push_back(std::move(task));
}

void TTVTask::runOnce(uint32_t delayMs, std::function<void()> callback)
{
    _registerPeriodicTask(std::move(callback), delayMs, false, true, nullptr);
}

uint32_t TTVTask::runRepeat(uint32_t intervalMs, std::function<void()> callback, bool executeImmediately)
{
    uint32_t id = 0;
    _registerPeriodicTask(std::move(callback), intervalMs, executeImmediately, false, &id);
    return id;
}

void TTVTask::cancelRepeat(uint32_t handle)
{
    if (handle == 0) return;
    for (size_t i = 0; i < _periodicTasks.size(); ++i) {
        if (_periodicTasks[i].id == handle) {
            _periodicTasks.erase(_periodicTasks.begin() + static_cast<std::ptrdiff_t>(i));
            break;
        }
    }
}

void TTVTask::_checkPeriodicTasks()
{
    uint32_t nowMs = millis();
    std::vector<size_t> toRemove;

    for (size_t i = 0; i < _periodicTasks.size(); ++i)
    {
        auto& task = _periodicTasks[i];
        if ((nowMs - task.lastExecuteTimeMs) >= task.intervalMs) {
            task.callback();
            if (task.runOnce)
                toRemove.push_back(i);
            else
                task.lastExecuteTimeMs = nowMs;
        }
    }
    for (auto it = toRemove.rbegin(); it != toRemove.rend(); ++it)
        _periodicTasks.erase(_periodicTasks.begin() + static_cast<std::ptrdiff_t>(*it));
}

void TTVTask::_task()
{
    // Call setup once
    _taskStartTime = millis();
    setup();

    std::function<void()> *func;

    // Run the task loop
    while (true)
    {
        // Process any queued functions
        while (xQueueReceive(_queue, &func, 0) == pdTRUE)
        {
            (*func)();
            delete func;
        }

        // Check periodic tasks
        _checkPeriodicTasks();

        // Run the main loop
        loop();

        vTaskDelay(pdMS_TO_TICKS(_loopDelayMs));
    }
}
