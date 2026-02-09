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

void TTVTask::registerPeriodicTask(std::function<void()> callback, uint32_t intervalMs, bool executeImmediately)
{
    TTPeriodicTask task;
    task.callback = callback;
    task.intervalMs = intervalMs;
    
    if (executeImmediately) {
        // Execute immediately on registration
        callback();
        // Record timestamp after execution
        task.lastExecuteTimeMs = millis();
    } else {
        // Set timestamp to past so it executes on next check
        task.lastExecuteTimeMs = millis() - intervalMs;
    }
    
    _periodicTasks.push_back(task);
}

void TTVTask::_checkPeriodicTasks()
{
    uint32_t nowMs = millis();

    for (auto& task : _periodicTasks)
    {
        if ((nowMs - task.lastExecuteTimeMs) >= task.intervalMs)
        {
            task.callback();
            task.lastExecuteTimeMs = nowMs;
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
