#include "TTKeypadTask.h"
#include "../Base/TTKeypadInput.h"
#include "../Base/Logger.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

void TTKeypadTask::setup() {
    LOG_I("KeypadTask started (tick every %d ms).", TT_KEYPAD_TICK_MS);
}

void TTKeypadTask::loop() {
    if (_keypad != nullptr) {
        _keypad->tick();
    }
    vTaskDelay(pdMS_TO_TICKS(TT_KEYPAD_TICK_MS));
}
