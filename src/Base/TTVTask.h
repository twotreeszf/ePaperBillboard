#ifndef TTVTASK_H
#define TTVTASK_H

#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <functional>

class TTVTask
{
public:
    TTVTask(const char* name = "TTVtask", uint32_t stackSize = 2048) 
        : _name(name), _stackSize(stackSize) {}
    virtual ~TTVTask() = default;

    // Public interface
    void start(int coreId = 0);
    

// Interface for derived classes
protected:
    // Called once when task starts
    virtual void setup() = 0;    

    // Called repeatedly in task loop
    virtual void loop() = 0;     

    // Add external task to queue
    void enqueue(std::function<void()>* func);
    
private:
    void _task();           // Main task function
    QueueHandle_t _queue = nullptr;
    const char* _name;
    uint32_t _stackSize;
};

#endif