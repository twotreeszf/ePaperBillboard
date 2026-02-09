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
    /* PCB: BUTTON1/2/3 have 100kΩ pull-down to GND, C has 10kΩ pull-up to 3V3.
     * When pressed, button connects C (HIGH) to BUTTON pin, so BUTTON goes HIGH.
     * So idle = LOW (100kΩ pull-down), pressed = HIGH (active-high).
     * GPIO 34/35/39 have no internal pull-up, use INPUT and rely on circuit. */
    pinMode(PIN_BUTTONL, INPUT);
    pinMode(PIN_BUTTONR, INPUT);
    pinMode(PIN_BUTTONC, INPUT);

    _btnL = new OneButton(PIN_BUTTONL, false, false);
    _btnR = new OneButton(PIN_BUTTONR, false, false);
    _btnC = new OneButton(PIN_BUTTONC, false, false);

    _btnL->attachClick([](void* param) { static_cast<TTKeypadInput*>(param)->emitKey(LV_KEY_PREV); }, this);
    _btnR->attachClick([](void* param) { static_cast<TTKeypadInput*>(param)->emitKey(LV_KEY_NEXT); }, this);
    _btnC->attachClick([](void* param) { static_cast<TTKeypadInput*>(param)->emitKey(LV_KEY_ENTER); }, this);

    delay(50);
    _btnL->reset();
    _btnR->reset();
    _btnC->reset();

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
