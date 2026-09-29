#pragma once

/* Three-button dial: Left, Right, Center (down). Active high (100kΩ pull-down to GND, pressed connects to C which is HIGH); GPIO 34/35/39 are input-only on ESP32. */
#define PIN_BUTTONL 35
#define PIN_BUTTONR 39
#define PIN_BUTTONC 34

#define TT_KEYPAD_POLL_MS  5
