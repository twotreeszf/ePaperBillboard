#include "TTTimeService.h"
#include "Logger.h"
#include "TTInstance.h"
#include "TTNotificationCenter.h"
#include "TTNotificationPayloads.h"
#include "TTRtc.h"
#include "../Tasks/TTUITask.h"

void TTTimeService::begin() {
    LOG_I("Time: service start");
    TTInstanceOf<TTNotificationCenter>().subscribe<TTTimeSyncPayload>(
        TT_NOTIFICATION_TIME_SYNC,
        this,
        [this](const TTTimeSyncPayload& status) {
            onTimeSync(status);
        });
    arm();
}

void TTTimeService::onTimeSync(const TTTimeSyncPayload& status) {
    if (status.state != TT_TIME_SYNC_OK) {
        return;
    }
    LOG_I("Time: NTP ok, tick now");
    onTick();
}

void TTTimeService::arm() {
    if (_handle != 0) {
        TTInstanceOf<TTUITask>().cancelRepeat(_handle);
        _handle = 0;
    }
    const uint32_t delayMs = TTInstanceOf<TTRtc>().msUntilNextTimeTick();
    LOG_I("Time: next tick in %.1f s", delayMs / 1000.0);
    _handle = TTInstanceOf<TTUITask>().runOnceWall(delayMs, [this]() {
        _handle = 0;
        onTick();
    });
}

void TTTimeService::onTick() {
    LOG_I("Time: tick");
    TTTimeTickPayload payload;
    TTInstanceOf<TTNotificationCenter>().sendNotification(TT_NOTIFICATION_TIME_TICK, payload);
    arm();
}
