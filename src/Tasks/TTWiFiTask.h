#ifndef TTWIFITASK_H
#define TTWIFITASK_H

#pragma once

#include "../Base/TTVTask.h"
#include "../Base/TTWiFiManager.h"

class TTWiFiTask : public TTVTask
{
public:
    TTWiFiTask() : TTVTask("WiFiTask", 8192) {}
    bool isConnected() { return _wifiManager.isConnected(); }
    
protected:
    void setup() override;
    void loop() override;
    
private:
    TTWiFiManager _wifiManager;
};

#endif
