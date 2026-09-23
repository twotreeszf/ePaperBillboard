#pragma once

#include "../Base/TTFile.h"
#include <stddef.h>

#define TT_OTA_MANIFEST_URL     "https://epaper-board.tos-cn-beijing.volces.com/firmware/manifest.json"
#define TT_OTA_URL_BASE         "https://epaper-board.tos-cn-beijing.volces.com/firmware/"
#define TT_OTA_LOCAL_MANIFEST   TT_FS_RES_DIR "/manifest.json"
#define TT_OTA_REMOTE_MANIFEST  TT_FS_TMP_DIR "/ota_remote.json"
#define TT_OTA_STAGED_MANIFEST  TT_FS_TMP_DIR "/ota_manifest.json"
#define TT_OTA_DELETE_LIST      TT_FS_TMP_DIR "/ota_delete.lst"
#define TT_OTA_PENDING_VER      TT_FS_TMP_DIR "/ota_pending.txt"
#define TT_OTA_PART_PATH        TT_FS_TMP_DIR "/ota_part"
#define TT_OTA_MANIFEST_MAX     65536
#define TT_OTA_REMOTE_MAX       96
#define TT_OTA_SHA_HEX          64
#define TT_OTA_VER_MAX          16
#define TT_OTA_LINE_MAX         512
#define TT_OTA_FW_MAX           (2 * 1024 * 1024)
#define TT_OTA_URL_MAX          192
#define TT_OTA_PROGRESS_PERCENT 10
#define TT_OTA_PROGRESS_MIN_MS  1000
#define TT_OTA_PROGRESS_DETAIL  (16 * 1024)
#define TT_OTA_PROGRESS_NAME    48
#define TT_OTA_UPDATING_HINT    "正在更新，不要退出界面或关闭设备"

class TTOtaService {
public:
    void readLocalVersion(char* out, size_t outLen);
    void readLocalNotes(char* out, size_t outLen);
    void checkAsync();
    void upgradeAsync();
    void applyPending();

private:
    void checkNow();
    void upgradeNow();
};
