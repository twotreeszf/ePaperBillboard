#include "TTKeypadInput.h"
#include "Logger.h"
#include <OneButton.h>

#define BUTTON_ACTIVE_LOW  true
#define BUTTON_PULLUP      false

static void onLeftClickCb(void* param) {
    ((TTKeypadInput*)param)->onLeftClick();
}
static void onRightClickCb(void* param) {
    ((TTKeypadInput*)param)->onRightClick();
}
static void onCenterClickCb(void* param) {
    ((TTKeypadInput*)param)->onCenterClick();
}

void TTKeypadInput::keypadReadCb(lv_indev_t* indev, lv_indev_data_t* data) {
    TTKeypadInput* self = (TTKeypadInput*)lv_indev_get_user_data(indev);
    if (self == nullptr) {
        data->state = LV_INDEV_STATE_RELEASED;
        data->key = 0;
        return;
    }
    static uint32_t lastKey = 0;
    if (self->_pendingKey != 0 && self->_pendingPress) {
        data->key = self->_pendingKey;
        data->state = LV_INDEV_STATE_PRESSED;
        lastKey = self->_pendingKey;
        self->_pendingPress = false;
    } else {
        data->key = lastKey;
        data->state = LV_INDEV_STATE_RELEASED;
        if (self->_pendingKey != 0) {
            self->_pendingKey = 0;
        }
    }
}

void TTKeypadInput::onLeftClick() {
    _pendingKey = LV_KEY_LEFT;
    _pendingPress = true;
    lv_indev_read(_indev);
    lv_indev_read(_indev);
}

void TTKeypadInput::onRightClick() {
    _pendingKey = LV_KEY_RIGHT;
    _pendingPress = true;
    lv_indev_read(_indev);
    lv_indev_read(_indev);
}

void TTKeypadInput::onCenterClick() {
    _pendingKey = LV_KEY_ENTER;
    _pendingPress = true;
    lv_indev_read(_indev);
    lv_indev_read(_indev);
}

bool TTKeypadInput::begin(lv_display_t* display) {
    _btnL = new OneButton(PIN_BUTTONL, BUTTON_ACTIVE_LOW, BUTTON_PULLUP);
    _btnR = new OneButton(PIN_BUTTONR, BUTTON_ACTIVE_LOW, BUTTON_PULLUP);
    _btnC = new OneButton(PIN_BUTTONC, BUTTON_ACTIVE_LOW, BUTTON_PULLUP);

    _btnL->attachClick(onLeftClickCb, this);
    _btnR->attachClick(onRightClickCb, this);
    _btnC->attachClick(onCenterClickCb, this);

    pinMode(PIN_BUTTONL, INPUT);
    pinMode(PIN_BUTTONR, INPUT);
    pinMode(PIN_BUTTONC, INPUT);

    _indev = lv_indev_create();
    lv_indev_set_type(_indev, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(_indev, keypadReadCb);
    lv_indev_set_user_data(_indev, this);
    lv_indev_set_display(_indev, display);
    lv_indev_set_mode(_indev, LV_INDEV_MODE_EVENT);

    LOG_I("Keypad input: L=%d R=%d C=%d (event-driven, no pullup)", PIN_BUTTONL, PIN_BUTTONR, PIN_BUTTONC);
    return true;
}

void TTKeypadInput::tick() {
    if (_btnL) _btnL->tick();
    if (_btnR) _btnR->tick();
    if (_btnC) _btnC->tick();
}
