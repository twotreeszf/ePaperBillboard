#include "TTKeypadInput.h"
#include "ITTNavigationController.h"
#include "ITTScreenPage.h"
#include "Logger.h"
#include "../Tasks/TTUITask.h"
#include "TTInstance.h"
#include <OneButton.h>

void TTKeypadInput::keypadReadCb(lv_indev_t* indev, lv_indev_data_t* data) {
    TTKeypadInput* self = (TTKeypadInput*)lv_indev_get_user_data(indev);
    if (self == nullptr) {
        data->state = LV_INDEV_STATE_RELEASED;
        data->key = 0;
        return;
    }
    if (self->_pendingKey != 0 && self->_pendingPress) {
        data->key = self->_pendingKey;
        data->state = LV_INDEV_STATE_PRESSED;
        self->_lastKey = self->_pendingKey;
        self->_pendingPress = false;
    } else {
        data->key = self->_lastKey;
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

void TTKeypadInput::setup() {
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

    _btnL->attachClick([](void* param) {
        static_cast<TTKeypadInput*>(param)->post(TT_KEY_LEFT, TT_KEY_CLICK);
    }, this);
    _btnL->attachLongPressStart([](void* param) {
        static_cast<TTKeypadInput*>(param)->post(TT_KEY_LEFT, TT_KEY_LONG_PRESS);
    }, this);
    _btnR->attachClick([](void* param) {
        static_cast<TTKeypadInput*>(param)->post(TT_KEY_RIGHT, TT_KEY_CLICK);
    }, this);
    _btnC->attachClick([](void* param) {
        static_cast<TTKeypadInput*>(param)->post(TT_KEY_CENTER, TT_KEY_CLICK);
    }, this);
    _btnC->attachLongPressStart([](void* param) {
        static_cast<TTKeypadInput*>(param)->post(TT_KEY_CENTER, TT_KEY_LONG_PRESS);
    }, this);

    delay(50);
    _btnL->reset();
    _btnR->reset();
    _btnC->reset();
    LOG_I("Keypad input: L=%d R=%d C=%d sampler %dms", PIN_BUTTONL, PIN_BUTTONR, PIN_BUTTONC, TT_KEYPAD_POLL_MS);
}

void TTKeypadInput::loop() {
    sample();
}

void TTKeypadInput::sample() {
    if (_btnL) _btnL->tick();
    if (_btnR) _btnR->tick();
    if (_btnC) _btnC->tick();
}

void TTKeypadInput::post(uint8_t key, uint8_t gesture) {
    TTInstanceOf<TTUITask>().requestKeyGestureAsync(key, gesture);
}

void TTKeypadInput::apply(uint8_t key, uint8_t gesture) {
    if (_pendingKey != 0 || _pendingPress) {
        post(key, gesture);
        return;
    }
    LOG_I("Keypad: key=%u gesture=%u", key, gesture);
    if (key == TT_KEY_LEFT && gesture == TT_KEY_LONG_PRESS) {
        if (_nav != nullptr) {
            _nav->pop();
        }
        return;
    }
    if (key == TT_KEY_CENTER && gesture == TT_KEY_LONG_PRESS) {
        if (_nav == nullptr) {
            return;
        }
        ITTScreenPage* page = _nav->getCurrentPage();
        if (page != nullptr && page->handleKeyAction(TT_KEY_CENTER, TT_KEY_LONG_PRESS)) {
            LOG_I("Keypad: center long press handled by %s", page->getName());
        }
        return;
    }
    if (gesture == TT_KEY_CLICK && _nav != nullptr && (key == TT_KEY_LEFT || key == TT_KEY_RIGHT)) {
        ITTScreenPage* page = _nav->getCurrentPage();
        if (page != nullptr && page->handleKeyAction((TTKeyId)key, TT_KEY_CLICK)) {
            return;
        }
    }
    if (gesture != TT_KEY_CLICK) {
        return;
    }
    if (key == TT_KEY_LEFT) {
        emitKey(LV_KEY_PREV);
    } else if (key == TT_KEY_RIGHT) {
        emitKey(LV_KEY_NEXT);
    } else if (key == TT_KEY_CENTER) {
        emitKey(LV_KEY_ENTER);
    }
}

bool TTKeypadInput::begin(lv_display_t* display) {
    if (!start(TT_KEYPAD_TASK_CORE, TT_KEYPAD_POLL_MS, TT_KEYPAD_TASK_PRIORITY)) {
        return false;
    }

    _indev = lv_indev_create();
    lv_indev_set_type(_indev, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(_indev, keypadReadCb);
    lv_indev_set_user_data(_indev, this);
    lv_indev_set_display(_indev, display);
    lv_indev_set_mode(_indev, LV_INDEV_MODE_TIMER);
    return true;
}
