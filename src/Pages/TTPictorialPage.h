#pragma once

#include "../Base/TTFile.h"
#include "../Base/TTScreenPage.h"
#include "../Models/TTPictorialTypes.h"
#include "../Models/TTNotificationPayloads.h"
#include "../Service/TTSleepService.h"
#include "TTDaySide.h"

#define TT_PIC_UPDATING_TEXT  "正在更新"
#define TT_PIC_PICK_ROWS      3
#define TT_PIC_PICK_ROW_H     28
#define TT_PIC_PICK_ORDINAL_H 12
#define TT_PIC_PICK_BORDER    2
#define TT_PIC_PICK_INSET     12

class TTPictorialPage : public TTScreenPage {
public:
    TTPictorialPage() : TTScreenPage("画报") {}

    void setup() override;
    void willAppear() override;
    void willDisappear() override;

protected:
    void buildContent(lv_obj_t* screen) override;
    bool handleKeyAction(TTKeyId key, TTKeyGesture gesture) override;

private:
    void requestWeather(bool force = false);
    void applyWeather(const TTWeatherPayload& payload);
    void applyPictorial(const TTPicPayload& payload);
    void onSleepWake(const TTSleepWakePayload& wake);
    void onSchedule(const TTSchedulePayload& event);
    void onTimeTick();
    void layoutSide();
    void setStatusTimeVisible(bool visible);
    void updateClock(bool refreshIfChanged);
    void showArt(const char* path);
    void openPicker();
    void closePicker(bool apply);
    void showPicker();
    void tryRequestLightSleep(uint32_t delayMs = TT_SLEEP_AFTER_REFRESH_MS);
    void armInputIdleSleep();
    void cancelInputIdleSleep();
    void cancelSleepSettle();

    TTDaySide _daySide;
    lv_obj_t* _art = nullptr;
    lv_obj_t* _status = nullptr;
    lv_obj_t* _pickPanel = nullptr;
    lv_obj_t* _pickRows[TT_PIC_PICK_ROWS] = {};
    lv_obj_t* _pickNames[TT_PIC_PICK_ROWS] = {};
    lv_obj_t* _pickOrdinal = nullptr;
    lv_obj_t* _hit = nullptr;

    bool _visible = false;
    bool _weatherFetching = false;
    bool _artReady = false;
    char _artPath[TT_STREAM_IMAGE_PATH_MAX] = {};
    bool _artFetching = false;
    bool _picking = false;
    uint32_t _inputIdleSleepHandle = 0;
    uint32_t _sleepSettleHandle = 0;
    bool _sleepAfterTimeTick = false;
    bool _dialArtChange = false;
    uint8_t _pickIndex = 0;
};
