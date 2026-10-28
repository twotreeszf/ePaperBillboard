#pragma once

#include <EPDConfig.h>
#include <FS.h>
#include <stddef.h>
#include <stdint.h>
#include <lvgl.h>

#define TT_SHOT_DIR              "/shots"
#define TT_SHOT_NAME_MAX         48
#define TT_SHOT_PATH_MAX         64
#define TT_SHOT_TOKEN_MAX        24
#define TT_SHOT_MAX              32
#define TT_SHOT_RESERVE_BYTES    (256 * 1024)
#define TT_SHOT_TOAST_MS         2000
#define TT_SHOT_TOAST_TEXT       "已截图"
#define TT_SHOT_NO_ROOM_TEXT     "空间不足"
#define PREF_DEV_SHOT            "dev_shot"

enum TTShotResult {
    TT_SHOT_OK = 0,
    TT_SHOT_NO_ROOM,
    TT_SHOT_FAILED,
};
#define TT_SHOT_FRAME_STRIDE     (((EPD_WIDTH) + 7) / 8)
#define TT_SHOT_FRAME_BYTES      (TT_SHOT_FRAME_STRIDE * (EPD_HEIGHT))
#define TT_SHOT_ROW_STRIDE       ((((EPD_WIDTH) + 31) / 32) * 4)
#define TT_SHOT_FILE_BYTES       (62 + (TT_SHOT_ROW_STRIDE * (EPD_HEIGHT)))
#define TT_SHOT_DEBUG_PATH       "/tmp/debug-shot.bmp"
#define TT_SHOT_STREAM_CHUNK     256
#define TT_SHOT_WRITE_BYTES      1024
#define TT_SHOT_WRITE_BLOCKS     (((TT_SHOT_FILE_BYTES) + (TT_SHOT_WRITE_BYTES) - 1) / (TT_SHOT_WRITE_BYTES))

typedef bool (*TTShotWriter)(void* ctx, const uint8_t* data, size_t len);

class TTScreenshotDisplay {
public:
    TTShotResult capture(const char* pageName);
    TTShotResult renderFrame(const char* pageName);
    bool writeFrameBmp(TTShotWriter write, void* ctx);
    void releaseFrame();
    bool shotsEnabled() const;
    bool isBusy() const { return _busy; }

    int list(char names[][TT_SHOT_NAME_MAX], int max);
    bool resolve(const char* name, char* path, size_t pathMax);
    bool removeOne(const char* name);
    int removeAll();
    bool zipBytes(size_t* outBytes);
    bool writeZip(TTShotWriter write, void* ctx);

    void blit(const lv_area_t* area, const uint8_t* px);

private:
    bool ensureFrame();
    bool hasRoom() const;
    bool save(const char* pageName);
    bool openDebugFile();
    bool loadCaptureBlock(uint32_t filePos);
    bool storeCaptureBlock();
    bool flushDebugFile();
    void closeDebugFile();
    void discardDebugFile();
    bool streamDebugFile(TTShotWriter write, void* ctx);
    void blitToFile(const lv_area_t* area, const uint8_t* px);

    uint8_t* _frame = nullptr;
    fs::File _captureFile;
    char _capturePath[TT_SHOT_PATH_MAX] = {};
    uint8_t _captureBlock[TT_SHOT_WRITE_BYTES] = {};
    uint32_t _captureBlockPos = 0;
    uint32_t _captureBlockMask = 0;
    uint16_t _captureBlockLen = 0;
    uint16_t _captureWriteCount = 0;
    bool _captureBlockValid = false;
    bool _captureBlockDirty = false;
    bool _captureIoOk = true;
    bool _busy = false;
};
