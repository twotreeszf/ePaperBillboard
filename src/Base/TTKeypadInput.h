#pragma once

#include <lvgl.h>
#include "TTVTask.h"

/* Three-button dial: Left, Right, Center (down). Active high (100kΩ pull-down to GND, pressed connects to C which is HIGH); GPIO 34/35/39 are input-only on ESP32.
 * Sampling runs on its own task. Gestures are posted to the UI task, so a press during an e-paper refresh is not dropped. */
#define PIN_BUTTONL 35
#define PIN_BUTTONR 39
#define PIN_BUTTONC 34

#define TT_KEYPAD_POLL_MS          5
#define TT_KEYPAD_TASK_STACK       3072
#define TT_KEYPAD_TASK_PRIORITY    2
#define TT_KEYPAD_TASK_CORE        0

class OneButton;
class ITTNavigationController;
class TTUITask;

class TTKeypadInput : public TTVTask {
    friend class TTUITask;
public:
    TTKeypadInput() : TTVTask("Keypad", TT_KEYPAD_TASK_STACK) {}

    bool begin(lv_display_t* display);

    lv_indev_t* getIndev() const { return _indev; }

    void setNavigationController(ITTNavigationController* nav) { _nav = nav; }

protected:
    void setup() override;
    void loop() override;

private:
    static void keypadReadCb(lv_indev_t* indev, lv_indev_data_t* data);

    void sample();
    void post(uint8_t key, uint8_t gesture);
    void apply(uint8_t key, uint8_t gesture);
    void emitKey(uint32_t key);

    OneButton* _btnL = nullptr;
    OneButton* _btnR = nullptr;
    OneButton* _btnC = nullptr;
    lv_indev_t* _indev = nullptr;
    volatile uint32_t _pendingKey = 0;
    volatile bool _pendingPress = false;
    uint32_t _lastKey = 0;
    ITTNavigationController* _nav = nullptr;
};
