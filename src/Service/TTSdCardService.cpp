#include "TTSdCardService.h"
#include "../Base/Logger.h"
#include "../Base/Util.h"
#include <SD.h>
#include <driver/gpio.h>
#include <esp_attr.h>
#include <esp_private/cache_utils.h>
#include <esp_rom_sys.h>
#include <soc/gpio_reg.h>

static_assert(TT_SD_PIN_POWER < 32, "SD power pin must use the low GPIO out register");

// Runs with flash cache off and the other core parked, so it must not touch flash.
static void IRAM_ATTR __attribute__((noinline)) sdPowerOnGuarded() {
    spi_flash_disable_interrupts_caches_and_other_cpu();
    REG_WRITE(TT_SD_POWER_ON_LEVEL == LOW ? GPIO_OUT_W1TC_REG : GPIO_OUT_W1TS_REG, BIT(TT_SD_PIN_POWER));
    esp_rom_delay_us(TT_SD_POWER_GUARD_US);
    spi_flash_enable_interrupts_caches_and_other_cpu();
}

void TTSdCardService::begin() {
    pinMode(TT_SD_PIN_DETECT, INPUT_PULLUP);
    pinMode(TT_SD_PIN_POWER, OUTPUT);
    digitalWrite(TT_SD_PIN_POWER, TT_SD_POWER_OFF_LEVEL);
    pinMode(TT_SD_PIN_SCLK, INPUT);
    pinMode(TT_SD_PIN_MOSI, INPUT);
    pinMode(TT_SD_PIN_MISO, INPUT);
    pinMode(TT_SD_PIN_CS, INPUT);
    LOG_I("SD: ready, power off, card %s", isCardInserted() ? "inserted" : "absent");
}

bool TTSdCardService::isCardInserted() const {
    return digitalRead(TT_SD_PIN_DETECT) == TT_SD_DETECT_LEVEL;
}

TTSdMountResult TTSdCardService::acquire() {
    std::lock_guard<std::mutex> guard(_lock);
    if (!isCardInserted()) {
        LOG_W("SD: acquire refused, no card refs=%u", (unsigned)_refs);
        return TT_SD_MOUNT_NO_CARD;
    }
    if (!_mounted && !mount()) {
        return TT_SD_MOUNT_FAILED;
    }
    _refs++;
    LOG_I("SD: acquire refs=%u", (unsigned)_refs);
    return TT_SD_MOUNT_OK;
}

void TTSdCardService::release() {
    std::lock_guard<std::mutex> guard(_lock);
    if (_refs == 0) {
        LOG_W("SD: release without acquire");
        return;
    }
    _refs--;
    LOG_I("SD: release refs=%u", (unsigned)_refs);
    if (_refs == 0 && _mounted) {
        unmount();
    }
}

void TTSdCardService::handleCardRemoved() {
    std::lock_guard<std::mutex> guard(_lock);
    LOG_W("SD: card removed, mounted=%d refs=%u", _mounted ? 1 : 0, (unsigned)_refs);
    if (_mounted) {
        unmount();
    }
}

bool TTSdCardService::spaceInfo(uint64_t& totalBytes, uint64_t& freeBytes) {
    std::lock_guard<std::mutex> guard(_lock);
    totalBytes = 0;
    freeBytes = 0;
    if (!_mounted) {
        return false;
    }
    totalBytes = SD.totalBytes();
    const uint64_t used = SD.usedBytes();
    freeBytes = totalBytes > used ? totalBytes - used : 0;
    return totalBytes > 0;
}

fs::FS& TTSdCardService::fs() {
    return SD;
}

bool TTSdCardService::mount() {
    const uint32_t start = millis();
    LOG_I("SD: mount begin heap=%u largest=%u", (unsigned)Util::heapFree(), (unsigned)Util::heapLargest());
    powerOn();
    if (!_spi.begin(TT_SD_PIN_SCLK, TT_SD_PIN_MISO, TT_SD_PIN_MOSI, -1)) {
        LOG_E("SD: SPI bus start failed");
        releaseBus();
        powerOff();
        return false;
    }
    gpio_pullup_en((gpio_num_t)TT_SD_PIN_MISO);

    uint32_t freqHz = TT_SD_SPI_FREQ_HZ;
    bool ok = tryMount(freqHz);
    if (!ok) {
        delay(TT_SD_MOUNT_RETRY_MS);
        freqHz = TT_SD_SPI_SAFE_FREQ_HZ;
        ok = tryMount(freqHz);
    }
    if (!ok) {
        LOG_E("SD: mount failed after retry, %u ms", (unsigned)(millis() - start));
        releaseBus();
        powerOff();
        return false;
    }
    _mounted = true;
    logCardInfo(freqHz);
    LOG_I("SD: mounted in %u ms heap=%u largest=%u", (unsigned)(millis() - start),
          (unsigned)Util::heapFree(), (unsigned)Util::heapLargest());
    return true;
}

bool TTSdCardService::tryMount(uint32_t freqHz) {
    LOG_I("SD: begin at %u kHz, format if no FAT", (unsigned)(freqHz / 1000));
    if (SD.begin(TT_SD_PIN_CS, _spi, freqHz, TT_SD_MOUNT_POINT, TT_SD_MAX_FILES, TT_SD_FORMAT_IF_EMPTY)) {
        return true;
    }
    LOG_W("SD: begin failed at %u kHz", (unsigned)(freqHz / 1000));
    SD.end();
    return false;
}

void TTSdCardService::unmount() {
    SD.end();
    _mounted = false;
    delay(TT_SD_POWER_SETTLE_MS);
    releaseBus();
    powerOff();
    LOG_I("SD: unmounted, power off");
}

void TTSdCardService::powerOn() {
#if TT_SD_POWER_GUARD_ENABLED
    LOG_I("SD: power on with flash guard %u us", (unsigned)TT_SD_POWER_GUARD_US);
    const uint32_t startUs = micros();
    sdPowerOnGuarded();
    LOG_I("SD: flash guard released after %u us", (unsigned)(micros() - startUs));
#else
    digitalWrite(TT_SD_PIN_POWER, TT_SD_POWER_ON_LEVEL);
#endif
    delay(TT_SD_POWER_SETTLE_MS);
    LOG_I("SD: power on");
}

void TTSdCardService::powerOff() {
    digitalWrite(TT_SD_PIN_POWER, TT_SD_POWER_OFF_LEVEL);
}

void TTSdCardService::releaseBus() {
    _spi.end();
    pinMode(TT_SD_PIN_SCLK, INPUT);
    pinMode(TT_SD_PIN_MOSI, INPUT);
    pinMode(TT_SD_PIN_CS, INPUT);
    pinMode(TT_SD_PIN_MISO, INPUT);
}

void TTSdCardService::logCardInfo(uint32_t freqHz) {
    const char* type = "unknown";
    switch (SD.cardType()) {
        case CARD_MMC:
            type = "MMC";
            break;
        case CARD_SD:
            type = "SDSC";
            break;
        case CARD_SDHC:
            type = "SDHC";
            break;
        default:
            break;
    }
    LOG_I("SD: type=%s size=%u MB clock=%u kHz",
          type,
          (unsigned)(SD.cardSize() / (1024ULL * 1024ULL)),
          (unsigned)(freqHz / 1000));
}
