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

    void suspend();
    void resume();
    void setDoubleClick(bool enabled);

private:
    static void keypadReadCb(lv_indev_t* indev, lv_indev_data_t* data);
    static void idleButton(OneButton* btn);

    void post(uint8_t key, uint8_t gesture);
    void emitKey(uint32_t key);
    void noteClick(uint8_t key);
    void noteLongPress(uint8_t key);
    void flushPendingClick();
    bool skipKeyHeld(uint8_t key) const;

    volatile bool _suppressUpLongPress = false;

    OneButton* _btnL = nullptr;
    OneButton* _btnR = nullptr;
    OneButton* _btnC = nullptr;
    lv_indev_t* _indev = nullptr;
    volatile uint32_t _pendingKey = 0;
    volatile bool _pendingPress = false;
    volatile bool _suspended = false;
    volatile bool _doubleClick = false;
    volatile bool _pendingSkip = false;
    uint8_t _pendingSkipKey = 0;
    uint32_t _pendingSkipMs = 0;
    uint32_t _lastKey = 0;
    ITTNavigationController* _nav = nullptr;
};
