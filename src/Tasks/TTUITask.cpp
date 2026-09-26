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
#include "../Service/TTSleepService.h"
#include "../Service/TTTimeService.h"
#include "../Service/TTOtaService.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

void TTUITask::setup() {
    LOG_I("Initializing SPI (MOSI=%d, SCK=%d)...", TT_UI_EPD_MOSI, TT_UI_EPD_SCK);
    SPI.begin(TT_UI_EPD_SCK, -1, TT_UI_EPD_MOSI, TT_UI_EPD_CS);

    _selectPanel();
    LOG_I("Initializing E-Paper display...");
    _display.init(115200, true, 2, false, SPI, SPISettings(4000000, MSBFIRST, SPI_MODE0));

    ERR_CHECK_FAIL(LittleFS.begin());
    if (!LittleFS.exists(TT_FS_TMP_DIR) && !LittleFS.mkdir(TT_FS_TMP_DIR)) {
        LOG_E("LittleFS mkdir %s failed", TT_FS_TMP_DIR);
    }
    TTInstanceOf<TTOtaService>().applyPending();
    LOG_I("LittleFS initialized, heap=%u", (unsigned)ESP.getFreeHeap());

    LOG_I("Initializing LVGL...");
    ERR_CHECK_FAIL(TTInstanceOf<TTLvglEpdDriver>().begin(_display));

    ERR_CHECK_FAIL(TTFontManager::instance().begin());
    LOG_I("Fonts ready, heap=%u", (unsigned)ESP.getFreeHeap());
    TTInstanceOf<TTPopupLayer>().begin(TTInstanceOf<TTLvglEpdDriver>().getDisplay());

    lv_display_t* disp = TTInstanceOf<TTLvglEpdDriver>().getDisplay();
    ERR_CHECK_FAIL(_keypad.begin(disp));
    _nav.setKeypadInput(&_keypad);
    _keypad.setNavigationController(&_nav);
    TTInstanceOf<TTPopupLayer>().setKeypadInput(&_keypad);

    _nav.setRootPage(std::unique_ptr<TTScreenPage>(new TTHomePage()));
    TTInstanceOf<TTTimeService>().begin();

    LOG_I("UI task started, heap=%u", (unsigned)ESP.getFreeHeap());
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
    const bool isB0 = panel == TT_EPD_PANEL_B0;
    _display.epd2.selectPanel(isB0 ? EPD_HINK_E042A13_B0 : EPD_HINK_E042A13_A0);
    LOG_I("E-Paper panel pref=%s use=%s", panel.c_str(), isB0 ? TT_EPD_PANEL_B0 : TT_EPD_PANEL_A0);
#else
    LOG_I("E-Paper panel fixed at build time");
#endif
}

void TTUITask::loop() {
    _keypad.tick();
    lv_timer_handler();
    TTInstanceOf<TTSleepService>().tryEnter();
}
