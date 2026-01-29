#pragma once

#include <lvgl.h>

class TTScreenPage {
public:
    virtual ~TTScreenPage();

    void createScreen();
    lv_obj_t* getScreen() const { return _screen; }

    virtual void loop() {}

    virtual void willAppear() {}
    virtual void didAppear() {}
    virtual void willDisappear() {}
    virtual void didDisappear() {}

    virtual void onSensorData(float temperature, float humidity, float pressure,
        bool ahtAvailable, bool bmp280Available) {
        (void)temperature;
        (void)humidity;
        (void)pressure;
        (void)ahtAvailable;
        (void)bmp280Available;
    }

protected:
    TTScreenPage() = default;
    TTScreenPage(const TTScreenPage&) = delete;
    TTScreenPage& operator=(const TTScreenPage&) = delete;

    virtual void buildContent() = 0;

    lv_obj_t* _screen = nullptr;
};
