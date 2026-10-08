#include "TTKeypadInput.h"
#include <Arduino.h>
#include "ITTNavigationController.h"
#include "ITTScreenPage.h"
#include "Logger.h"
#include "TTKeypadConfig.h"
#include "../Tasks/TTUITask.h"
#include "TTInstance.h"
#include <OneButton.h>

TTKeypadInput::~TTKeypadInput() {
    delete _btnL;
    delete _btnR;
    delete _btnC;
    _btnL = nullptr;
    _btnR = nullptr;
    _btnC = nullptr;
}

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

void TTKeypadInput::init() {
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

void TTKeypadInput::idleButton(OneButton* btn) {
    if (btn == nullptr) {
        return;
    }
    btn->debounce(false);
    btn->reset();
}

void TTKeypadInput::suspend() {
    _suspended = true;
    LOG_I("Keypad: suspend, button state cleared until resume");
}

void TTKeypadInput::resume() {
    _suspended = false;
    LOG_I("Keypad: resume");
}

void TTKeypadInput::sample() {
    if (_suspended) {
        idleButton(_btnL);
        idleButton(_btnR);
        idleButton(_btnC);
        return;
    }
    if (_btnL) {
        _btnL->tick();
    }
    if (_btnR) {
        _btnR->tick();
    }
    if (_btnC) {
        _btnC->tick();
    }
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

bool TTKeypadInput::attachIndev(lv_display_t* display) {
    _indev = lv_indev_create();
    lv_indev_set_type(_indev, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(_indev, keypadReadCb);
    lv_indev_set_user_data(_indev, this);
    lv_indev_set_display(_indev, display);
    lv_indev_set_mode(_indev, LV_INDEV_MODE_TIMER);
    return _indev != nullptr;
}
