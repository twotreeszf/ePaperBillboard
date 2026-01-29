#include "TTVtask.h"
#include "Logger.h"
#include "ErrorCheck.h"
#include <freertos/task.h>

void TTVTask::start(int coreId)
{
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

void TTVTask::_task()
{
    // Call setup once
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

        // Run the main loop
        loop();
    }
}
