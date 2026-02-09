#pragma once

#include <lvgl.h>

/* Three-button dial: Left, Right, Center (down). Active high (100kΩ pull-down to GND, pressed connects to C which is HIGH); GPIO 34/35/39 are input-only on ESP32. */
#define PIN_BUTTONL 35
#define PIN_BUTTONR 39
#define PIN_BUTTONC 34

class OneButton;

class TTKeypadInput {
public:
    TTKeypadInput() = default;

    bool begin(lv_display_t* display);
    void tick();

    lv_indev_t* getIndev() const { return _indev; }

    void emitKey(uint32_t key);

private:
    static void keypadReadCb(lv_indev_t* indev, lv_indev_data_t* data);

    OneButton* _btnL = nullptr;
    OneButton* _btnR = nullptr;
    OneButton* _btnC = nullptr;
    lv_indev_t* _indev = nullptr;
    volatile uint32_t _pendingKey = 0;
    volatile bool _pendingPress = false;
};
