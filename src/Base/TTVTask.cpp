#include "TTVTask.h"
#include "Logger.h"
#include "ErrorCheck.h"
#include <freertos/task.h>
#include <Arduino.h>
#include <cstring>
#include <sys/time.h>

static bool tt_wall_time_ok(time_t now) {
    return now >= TT_PERIODIC_WALL_MIN_UNIX;
}

static int64_t tt_wall_now_ms() {
    struct timeval tv;
    memset(&tv, 0, sizeof(tv));
    if (gettimeofday(&tv, nullptr) != 0 || tv.tv_sec <= 0) {
        return 0;
    }
    return (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

bool TTVTask::start(int coreId, uint32_t loopDelayMs, uint32_t priority)
{
    _loopDelayMs = loopDelayMs;
    _queue = xQueueCreate(_queueLength, sizeof(std::function<void()> *));
    if (_queue == nullptr) {
        LOG_E("Task %s: queue create failed", _name);
        return false;
    }
    const BaseType_t created = xTaskCreatePinnedToCore(
        [](void *param)
        {
            TTVTask *task = static_cast<TTVTask *>(param);
            task->_task();
            vTaskDelete(nullptr);
        },
        _name,
        _stackSize,
        this,
        priority,
        NULL,
        coreId
    );
    if (created != pdPASS) {
        LOG_E("Task %s: create failed", _name);
        vQueueDelete(_queue);
        _queue = nullptr;
        return false;
    }

    LOG_I("Task %s started on core %d priority %u", _name, coreId, priority);
    return true;
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

void TTVTask::_registerPeriodicTask(std::function<void()> callback, uint32_t intervalMs, bool executeImmediately,
                                    bool runOnce, TTPeriodicClock clock, uint32_t* outId)
{
    TTPeriodicTask task;
    task.callback = std::move(callback);
    task.intervalMs = intervalMs;
    task.runOnce = runOnce;
    task.clock = clock;
    if (outId != nullptr) {
        task.id = ++_nextPeriodicId;
        *outId = task.id;
    }

    const uint32_t nowMs = millis();
    const time_t nowUnix = time(nullptr);
    if (executeImmediately) {
        task.callback();
    }
    _markPeriodicRan(task, nowMs, nowUnix);

    _periodicTasks.push_back(std::move(task));
}

uint32_t TTVTask::runOnce(uint32_t delayMs, std::function<void()> callback)
{
    uint32_t id = 0;
    _registerPeriodicTask(std::move(callback), delayMs, false, true, TT_PERIODIC_MILLIS, &id);
    return id;
}

uint32_t TTVTask::runRepeat(uint32_t intervalMs, std::function<void()> callback, bool executeImmediately)
{
    uint32_t id = 0;
    _registerPeriodicTask(std::move(callback), intervalMs, executeImmediately, false, TT_PERIODIC_MILLIS, &id);
    return id;
}

uint32_t TTVTask::runOnceWall(uint32_t delayMs, std::function<void()> callback)
{
    uint32_t id = 0;
    _registerPeriodicTask(std::move(callback), delayMs, false, true, TT_PERIODIC_WALL, &id);
    return id;
}

uint32_t TTVTask::runRepeatWall(uint32_t intervalMs, std::function<void()> callback, bool executeImmediately)
{
    uint32_t id = 0;
    _registerPeriodicTask(std::move(callback), intervalMs, executeImmediately, false, TT_PERIODIC_WALL, &id);
    return id;
}

bool TTVTask::_isPeriodicDue(const TTPeriodicTask& task, uint32_t nowMs, time_t nowUnix) const
{
    if (task.clock == TT_PERIODIC_MILLIS) {
        return (nowMs - task.lastExecuteTimeMs) >= task.intervalMs;
    }
    if (!tt_wall_time_ok(nowUnix)) {
        return false;
    }
    if (!tt_wall_time_ok(task.lastExecuteUnix) || nowUnix < task.lastExecuteUnix) {
        return false;
    }
    const int64_t nowUnixMs = tt_wall_now_ms();
    if (nowUnixMs <= 0 || task.lastExecuteUnixMs <= 0) {
        return false;
    }
    return (nowUnixMs - task.lastExecuteUnixMs) >= (int64_t)task.intervalMs;
}

void TTVTask::_markPeriodicRan(TTPeriodicTask& task, uint32_t nowMs, time_t nowUnix)
{
    task.lastExecuteTimeMs = nowMs;
    if (tt_wall_time_ok(nowUnix)) {
        task.lastExecuteUnix = nowUnix;
        task.lastExecuteUnixMs = tt_wall_now_ms();
    } else {
        task.lastExecuteUnix = 0;
        task.lastExecuteUnixMs = 0;
    }
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
    const uint32_t nowMs = millis();
    const time_t nowUnix = time(nullptr);
    std::vector<uint32_t> onceIds;
    std::vector<std::function<void()>> due;

    for (auto& task : _periodicTasks) {
        if (task.clock == TT_PERIODIC_WALL && tt_wall_time_ok(nowUnix)
            && (!tt_wall_time_ok(task.lastExecuteUnix) || nowUnix < task.lastExecuteUnix)) {
            task.lastExecuteUnix = nowUnix;
            task.lastExecuteUnixMs = tt_wall_now_ms();
            continue;
        }
        if (!_isPeriodicDue(task, nowMs, nowUnix)) {
            continue;
        }
        due.push_back(task.callback);
        if (task.runOnce) {
            onceIds.push_back(task.id);
        } else {
            _markPeriodicRan(task, nowMs, nowUnix);
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
        // Only the work already queued. Anything posted from a callback waits until the next loop.
        const UBaseType_t waiting = (_queue == nullptr) ? 0 : uxQueueMessagesWaiting(_queue);
        for (UBaseType_t i = 0; i < waiting; i++) {
            if (xQueueReceive(_queue, &func, 0) != pdTRUE) {
                break;
            }
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
