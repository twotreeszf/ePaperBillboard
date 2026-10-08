#include "TTUITask.h"
#include "../Pages/TTHomePage.h"
#include <SPI.h>
#include <LittleFS.h>
#include <memory>
#include "../Base/TTLvglEpdDriver.h"
#include "../Base/TTInstance.h"
#include "../Base/TTPopupLayer.h"
#include "../Base/Logger.h"
#include "../Base/ErrorCheck.h"
#include "../Base/TTFile.h"
#include "../Base/TTFontManager.h"
#include "../Base/TTPreference.h"
#include "../Base/Util.h"
#include "../Service/TTSleepService.h"
#include "../Service/TTTimeService.h"
#include "../Service/TTOtaService.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

void TTUITask::setup() {
    LOG_I("Initializing SPI (MOSI=%d, SCK=%d)...", TT_UI_EPD_MOSI, TT_UI_EPD_SCK);
    SPI.begin(TT_UI_EPD_SCK, -1, TT_UI_EPD_MOSI, TT_UI_EPD_CS);
    pinMode(TT_UI_EPD_CS, OUTPUT);
    pinMode(TT_UI_EPD_DC, OUTPUT);
    pinMode(TT_UI_EPD_RST, OUTPUT);
    digitalWrite(TT_UI_EPD_CS, HIGH);
    digitalWrite(TT_UI_EPD_DC, HIGH);
    digitalWrite(TT_UI_EPD_RST, HIGH);

    _selectPanel();
    LOG_I("Initializing E-Paper display...");
    _display.init(115200, true, 2, false, SPI, SPISettings(4000000, MSBFIRST, SPI_MODE0));

    ERR_CHECK_FAIL(LittleFS.begin());
    if (!LittleFS.exists(TT_FS_TMP_DIR) && !LittleFS.mkdir(TT_FS_TMP_DIR)) {
        LOG_E("LittleFS mkdir %s failed", TT_FS_TMP_DIR);
    }
    TTInstanceOf<TTOtaService>().applyPending();
    LOG_I("LittleFS initialized, heap=%u largest=%u",
          (unsigned)Util::heapFree(), (unsigned)Util::heapLargest());

    LOG_I("Initializing LVGL...");
    ERR_CHECK_FAIL(TTInstanceOf<TTLvglEpdDriver>().begin(_display));

    ERR_CHECK_FAIL(TTFontManager::instance().begin());
    LOG_I("Fonts ready, heap=%u largest=%u",
          (unsigned)Util::heapFree(), (unsigned)Util::heapLargest());
    TTInstanceOf<TTPopupLayer>().begin(TTInstanceOf<TTLvglEpdDriver>().getDisplay());

    lv_display_t* disp = TTInstanceOf<TTLvglEpdDriver>().getDisplay();
    ERR_CHECK_FAIL(_keypad.begin(disp));
    _nav.setKeypadInput(&_keypad.input());
    _keypad.input().setNavigationController(&_nav);
    TTInstanceOf<TTPopupLayer>().setKeypadInput(&_keypad.input());

    _nav.setRootPage(std::unique_ptr<TTScreenPage>(new TTHomePage()));
    TTInstanceOf<TTTimeService>().begin();

    LOG_I("UI task started, heap=%u largest=%u",
          (unsigned)Util::heapFree(), (unsigned)Util::heapLargest());
}

void TTUITask::requestDeepRefreshAsync() {
    auto* f = new std::function<void()>([]() {
        TTLvglEpdDriver& driver = TTInstanceOf<TTLvglEpdDriver>();
        if (!driver.isAutoDeepRefreshEnabled()) {
            return;
        }
        driver.requestRefresh(TT_REFRESH_DEEP);
    });
    enqueue(f);
}

void TTUITask::requestKeyGestureAsync(uint8_t key, uint8_t gesture) {
    auto* f = new std::function<void()>([this, key, gesture]() {
        _keypad.input().apply(key, gesture);
    });
    enqueue(f);
}

void TTUITask::requestRestartAsync() {
    auto* f = new std::function<void()>([]() {
        LOG_I("UI: restart requested, hibernate E-Paper");
        TTInstanceOf<TTLvglEpdDriver>().hibernate();
        ESP.restart();
    });
    enqueue(f);
}

void TTUITask::_selectPanel() {
#if defined(EPD_PANEL_SELECTABLE)
    String panel;
    TTInstanceOf<TTPreference>().get(PREF_EPD_PANEL, panel, String(TT_EPD_PANEL_DEFAULT));
    uint8_t variant = EPD_HINK_E042A13_A0;
    const char* use = TT_EPD_PANEL_A0;
    if (panel == TT_EPD_PANEL_B0) {
        variant = EPD_HINK_E042A13_B0;
        use = TT_EPD_PANEL_B0;
    } else if (panel == TT_EPD_PANEL_C0) {
        variant = EPD_HINK_E042A13_C0;
        use = TT_EPD_PANEL_C0;
    }
    _display.epd2.selectPanel(variant);
    LOG_I("E-Paper panel pref=%s use=%s", panel.c_str(), use);
#else
    LOG_I("E-Paper panel fixed at build time");
#endif
}

void TTUITask::loop() {
    lv_timer_handler();
    TTInstanceOf<TTSleepService>().tryEnter();
}
