#include "TTKeypadInput.h"
#include "Logger.h"
#include <OneButton.h>

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

void TTKeypadInput::emitKey(uint32_t key) {
    _pendingKey = key;
    _pendingPress = true;
}

bool TTKeypadInput::begin(lv_display_t* display) {
    _btnL = new OneButton(PIN_BUTTONL);
    _btnR = new OneButton(PIN_BUTTONR);
    _btnC = new OneButton(PIN_BUTTONC);

    _btnL->attachClick([](void* param) { static_cast<TTKeypadInput*>(param)->emitKey(LV_KEY_LEFT); }, this);
    _btnR->attachClick([](void* param) { static_cast<TTKeypadInput*>(param)->emitKey(LV_KEY_RIGHT); }, this);
    _btnC->attachClick([](void* param) { static_cast<TTKeypadInput*>(param)->emitKey(LV_KEY_ENTER); }, this);

    pinMode(PIN_BUTTONL, INPUT);
    pinMode(PIN_BUTTONR, INPUT);
    pinMode(PIN_BUTTONC, INPUT);

    _indev = lv_indev_create();
    lv_indev_set_type(_indev, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(_indev, keypadReadCb);
    lv_indev_set_user_data(_indev, this);
    lv_indev_set_display(_indev, display);
    lv_indev_set_mode(_indev, LV_INDEV_MODE_TIMER);

    LOG_I("Keypad input: L=%d R=%d C=%d (indev TIMER mode)", PIN_BUTTONL, PIN_BUTTONR, PIN_BUTTONC);
    return true;
}

void TTKeypadInput::tick() {
    if (_btnL) _btnL->tick();
    if (_btnR) _btnR->tick();
    if (_btnC) _btnC->tick();
}
