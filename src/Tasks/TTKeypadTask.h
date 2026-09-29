#pragma once

#include "../Base/TTKeypadInput.h"
#include "../Base/TTVTask.h"

#define TT_KEYPAD_TASK_STACK       3072
#define TT_KEYPAD_TASK_PRIORITY    2
#define TT_KEYPAD_TASK_CORE        0

class TTKeypadTask : public TTVTask {
public:
    TTKeypadTask() : TTVTask("Keypad", TT_KEYPAD_TASK_STACK) {}

    bool begin(lv_display_t* display);

    TTKeypadInput& input() { return _input; }
    const TTKeypadInput& input() const { return _input; }

protected:
    void setup() override;
    void loop() override;

private:
    TTKeypadInput _input;
};
