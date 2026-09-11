#pragma once

#include <stdint.h>
#include "TTNotificationPayloads.h"

class TTTimeService {
public:
    void begin();

private:
    void arm();
    void onTick();
    void onTimeSync(const TTTimeSyncPayload& status);

    uint32_t _handle = 0;
};
