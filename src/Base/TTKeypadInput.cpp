#include "TTKeypadInput.h"
#include "TTScreenshotDisplay.h"
#include "TTPopupLayer.h"
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
        static_cast<TTKeypadInput*>(param)->noteClick(TT_KEY_LEFT);
    }, this);
    _btnL->attachLongPressStart([](void* param) {
        static_cast<TTKeypadInput*>(param)->noteLongPress(TT_KEY_LEFT);
    }, this);
    _btnR->attachClick([](void* param) {
        static_cast<TTKeypadInput*>(param)->noteClick(TT_KEY_RIGHT);
    }, this);
    _btnR->attachLongPressStart([](void* param) {
        static_cast<TTKeypadInput*>(param)->noteLongPress(TT_KEY_RIGHT);
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
    _pendingSkip = false;
    LOG_I("Keypad: suspend, button state cleared until resume");
}

void TTKeypadInput::resume() {
    _suspended = false;
    LOG_I("Keypad: resume");
}

void TTKeypadInput::setDoubleClick(bool enabled) {
    _doubleClick = enabled;
    if (!enabled) {
        _pendingSkip = false;
    }
    LOG_I("Keypad: double click %s", enabled ? "on" : "off");
}

bool TTKeypadInput::skipKeyHeld(uint8_t key) const {
    const int pin = key == TT_KEY_LEFT ? PIN_BUTTONL : PIN_BUTTONR;
    return digitalRead(pin) == HIGH;
}

void TTKeypadInput::flushPendingClick() {
    if (!_pendingSkip) {
        return;
    }
    const uint8_t key = _pendingSkipKey;
    _pendingSkip = false;
    post(key, TT_KEY_CLICK);
}

void TTKeypadInput::noteClick(uint8_t key) {
    if (!_doubleClick) {
        _pendingSkip = false;
        post(key, TT_KEY_CLICK);
        return;
    }
    if (_pendingSkip && _pendingSkipKey == key) {
        _pendingSkip = false;
        LOG_I("Keypad: double key=%u", key);
        post(key, TT_KEY_DOUBLE_CLICK);
        return;
    }
    if (_pendingSkip) {
        flushPendingClick();
    }
    _pendingSkip = true;
    _pendingSkipKey = key;
    _pendingSkipMs = millis();
}

void TTKeypadInput::noteLongPress(uint8_t key) {
    flushPendingClick();
    post(key, TT_KEY_LONG_PRESS);
}

void TTKeypadInput::sample() {
    if (_pendingSkip && (!_doubleClick || _suspended)) {
        _pendingSkip = false;
    } else if (_pendingSkip && (millis() - _pendingSkipMs) >= TT_KEYPAD_DOUBLE_CLICK_MS
               && !skipKeyHeld(_pendingSkipKey)) {
        flushPendingClick();
    }
    if (_suppressUpLongPress && digitalRead(PIN_BUTTONR) == LOW) {
        _suppressUpLongPress = false;
        LOG_I("Keypad: up long press accepted again");
    }
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
    if (key == TT_KEY_RIGHT && gesture == TT_KEY_LONG_PRESS) {
        if (_suppressUpLongPress || TTInstanceOf<TTScreenshotDisplay>().isBusy()) {
            LOG_I("Keypad: ignore up long press during shot");
            return;
        }
        _suppressUpLongPress = true;
    }
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
    if (key == TT_KEY_RIGHT && gesture == TT_KEY_LONG_PRESS) {
        const char* pageName = "page";
        if (_nav != nullptr) {
            ITTScreenPage* page = _nav->getCurrentPage();
            if (page != nullptr && page->getName() != nullptr && page->getName()[0] != '\0') {
                pageName = page->getName();
            }
        }
        const TTShotResult shot = TTInstanceOf<TTScreenshotDisplay>().capture(pageName);
        if (shot == TT_SHOT_OK) {
            TTInstanceOf<TTPopupLayer>().showToast(TT_SHOT_TOAST_TEXT, TT_SHOT_TOAST_MS);
        } else if (shot == TT_SHOT_NO_ROOM) {
            TTInstanceOf<TTPopupLayer>().showToast(TT_SHOT_NO_ROOM_TEXT, TT_SHOT_TOAST_MS);
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
    if ((gesture == TT_KEY_CLICK || gesture == TT_KEY_DOUBLE_CLICK) && _nav != nullptr
        && (key == TT_KEY_LEFT || key == TT_KEY_RIGHT)) {
        ITTScreenPage* page = _nav->getCurrentPage();
        if (page != nullptr && page->handleKeyAction((TTKeyId)key, (TTKeyGesture)gesture)) {
            return;
        }
        if (gesture == TT_KEY_DOUBLE_CLICK) {
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
