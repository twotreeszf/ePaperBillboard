#include "TTVTask.h"
#include "Logger.h"
#include "ErrorCheck.h"
#include <freertos/task.h>
#include <Arduino.h>

void TTVTask::start(int coreId, uint32_t loopDelayMs)
{
    _loopDelayMs = loopDelayMs;
    _queue = xQueueCreate(10, sizeof(std::function<void()> *));
    if (_queue == nullptr) {
        LOG_E("Task %s: queue create failed", _name);
        return;
    }
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
    if (func == nullptr) {
        return;
    }
    if (_queue == nullptr) {
        LOG_E("Task %s: enqueue before start, drop", _name);
        delete func;
        return;
    }
    if (xQueueSend(_queue, &func, portMAX_DELAY) != pdTRUE) {
        LOG_E("Task %s: enqueue failed", _name);
        delete func;
    }
}

void TTVTask::_registerPeriodicTask(std::function<void()> callback, uint32_t intervalMs, bool executeImmediately, bool runOnce, uint32_t* outId)
{
    TTPeriodicTask task;
    task.callback = std::move(callback);
    task.intervalMs = intervalMs;
    task.runOnce = runOnce;
    if (outId != nullptr) {
        task.id = ++_nextPeriodicId;
        *outId = task.id;
    }

    if (executeImmediately) {
        task.callback();
        task.lastExecuteTimeMs = millis();
    } else {
        task.lastExecuteTimeMs = millis();
    }

    _periodicTasks.push_back(std::move(task));
}

uint32_t TTVTask::runOnce(uint32_t delayMs, std::function<void()> callback)
{
    uint32_t id = 0;
    _registerPeriodicTask(std::move(callback), delayMs, false, true, &id);
    return id;
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
    std::vector<uint32_t> onceIds;
    std::vector<std::function<void()>> due;

    for (auto& task : _periodicTasks) {
        if ((nowMs - task.lastExecuteTimeMs) < task.intervalMs) {
            continue;
        }
        due.push_back(task.callback);
        if (task.runOnce) {
            onceIds.push_back(task.id);
        } else {
            task.lastExecuteTimeMs = nowMs;
        }
    }
    for (uint32_t id : onceIds) {
        cancelRepeat(id);
    }
    for (auto& callback : due) {
        if (callback) {
            callback();
        }
    }
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
