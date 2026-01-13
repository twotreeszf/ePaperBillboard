#include "TTWiFiTask.h"
#include "../Base/Logger.h"

void TTWiFiTask::setup()
{
    LOG_I("Starting WiFi task");
    _wifiManager.tryConfigWiFi();
}

void TTWiFiTask::loop()
{
    _wifiManager.process();
    if (_wifiManager.isAPMode())
    {
        delay(10);
    }
    else
    {
        delay(500);
    }
}
