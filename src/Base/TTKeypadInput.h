#pragma once

#include <lvgl.h>

class OneButton;
class ITTNavigationController;

class TTKeypadInput {
public:
    TTKeypadInput() = default;
    ~TTKeypadInput();

    void init();
    bool attachIndev(lv_display_t* display);

    lv_indev_t* getIndev() const { return _indev; }

    void setNavigationController(ITTNavigationController* nav) { _nav = nav; }

    void sample();
    void apply(uint8_t key, uint8_t gesture);

private:
    static void keypadReadCb(lv_indev_t* indev, lv_indev_data_t* data);

    void post(uint8_t key, uint8_t gesture);
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
