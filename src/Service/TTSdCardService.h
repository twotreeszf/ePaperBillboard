#pragma once

#include <Arduino.h>
#include <FS.h>
#include <SPI.h>
#include <mutex>

#define TT_SD_PIN_SCLK          13
#define TT_SD_PIN_MOSI          12
#define TT_SD_PIN_MISO          15
#define TT_SD_PIN_CS            14
#define TT_SD_PIN_DETECT        2
#define TT_SD_PIN_POWER         27
#define TT_SD_DETECT_LEVEL      LOW
#define TT_SD_POWER_ON_LEVEL    LOW
#define TT_SD_POWER_OFF_LEVEL   HIGH
#define TT_SD_POWER_SETTLE_MS   50
#define TT_SD_MOUNT_RETRY_MS    100
#define TT_SD_FORMAT_IF_EMPTY   true
#define TT_SD_SPI_FREQ_HZ       20000000
#define TT_SD_SPI_SAFE_FREQ_HZ  4000000
#define TT_SD_MOUNT_POINT       "/sd"
#define TT_SD_MAX_FILES         3
#define TT_SD_DETECT_POLL_MS    1000

enum TTSdMountResult {
    TT_SD_MOUNT_OK = 0,
    TT_SD_MOUNT_NO_CARD,
    TT_SD_MOUNT_FAILED,
};

class TTSdCardService {
public:
    TTSdCardService() : _spi(HSPI) {}

    void begin();
    bool isCardInserted() const;
    bool isMounted() const { return _mounted; }
    TTSdMountResult acquire();
    void release();
    void handleCardRemoved();
    bool spaceInfo(uint64_t& totalBytes, uint64_t& freeBytes);
    fs::FS& fs();

private:
    bool mount();
    void unmount();
    bool tryMount(uint32_t freqHz);
    void powerOn();
    void powerOff();
    void releaseBus();
    void logCardInfo(uint32_t freqHz);

    SPIClass _spi;
    std::mutex _lock;
    uint8_t _refs = 0;
    bool _mounted = false;
};
