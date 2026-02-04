#pragma once

#include "../Base/TTVTask.h"

#define TT_KEYPAD_TICK_MS  15

class TTKeypadInput;

class TTKeypadTask : public TTVTask {
public:
    TTKeypadTask() : TTVTask("TTKeypadTask", 2048) {}

    void setKeypad(TTKeypadInput* keypad) { _keypad = keypad; }

protected:
    void setup() override;
    void loop() override;

private:
    TTKeypadInput* _keypad = nullptr;
};
