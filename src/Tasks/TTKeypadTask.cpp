#include "TTKeypadTask.h"
#include "../Base/TTKeypadConfig.h"

bool TTKeypadTask::begin(lv_display_t* display) {
    if (!start(TT_KEYPAD_TASK_CORE, TT_KEYPAD_POLL_MS, TT_KEYPAD_TASK_PRIORITY)) {
        return false;
    }
    return _input.attachIndev(display);
}

void TTKeypadTask::setup() {
    _input.init();
}

void TTKeypadTask::loop() {
    _input.sample();
}
