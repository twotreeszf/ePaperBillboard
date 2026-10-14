#include "TTScreenshotDisplay.h"

#include "Logger.h"
#include "TTFile.h"
#include "TTInstance.h"
#include "TTLvglEpdDriver.h"
#include "TTRtc.h"
#include "Util.h"
#include <LittleFS.h>
#include <esp_heap_caps.h>
#include <cstdio>
#include <cstring>

struct TTShotZipItem {
    char name[TT_SHOT_NAME_MAX];
    uint32_t size;
    uint32_t crc;
    uint32_t localOffset;
};

static void copyToken(char* out, size_t outMax, const char* name) {
    if (outMax == 0) {
        return;
    }
    size_t n = 0;
    if (name == nullptr || name[0] == '\0') {
        name = "page";
    }
    while (name[0] != '\0' && n + 1 < outMax) {
        const unsigned char c = (unsigned char)name[0];
        if (c < 0x80) {
            if (c < 0x20 || c == '/' || c == '\\' || c == '"' || c == '?' || c == '*' || c == ':') {
                name++;
                continue;
            }
            out[n++] = (c == ' ') ? '_' : (char)c;
            name++;
            continue;
        }
        size_t len = 1;
        if ((c & 0xE0) == 0xC0) {
            len = 2;
        } else if ((c & 0xF0) == 0xE0) {
            len = 3;
        } else if ((c & 0xF8) == 0xF0) {
            len = 4;
        }
        if (n + len >= outMax) {
            break;
        }
        bool ok = true;
        for (size_t i = 1; i < len; i++) {
            if (((unsigned char)name[i] & 0xC0) != 0x80) {
                ok = false;
            }
        }
        if (!ok) {
            name++;
            continue;
        }
        memcpy(out + n, name, len);
        n += len;
        name += len;
    }
    if (n == 0) {
        snprintf(out, outMax, "page");
        return;
    }
    out[n] = '\0';
}

static bool isSafeName(const char* name) {
    if (name == nullptr || name[0] == '\0' || name[0] == '.') {
        return false;
    }
    if (strlen(name) >= TT_SHOT_NAME_MAX) {
        return false;
    }
    if (strstr(name, "..") != nullptr) {
        return false;
    }
    for (const char* p = name; *p != '\0'; p++) {
        const unsigned char c = (unsigned char)*p;
        if (c < 0x20 || c == '/' || c == '\\' || c == '"' || c == '?' || c == '*' || c == ':') {
            return false;
        }
    }
    const size_t len = strlen(name);
    return len >= 4 && strcmp(name + len - 4, ".bmp") == 0;
}

static const char* baseName(const char* name) {
    if (name == nullptr) {
        return "";
    }
    const char* slash = strrchr(name, '/');
    return slash != nullptr ? slash + 1 : name;
}

static void putU16(uint8_t* dst, uint16_t value) {
    dst[0] = (uint8_t)(value & 0xFF);
    dst[1] = (uint8_t)((value >> 8) & 0xFF);
}

static void putU32(uint8_t* dst, uint32_t value) {
    dst[0] = (uint8_t)(value & 0xFF);
    dst[1] = (uint8_t)((value >> 8) & 0xFF);
    dst[2] = (uint8_t)((value >> 16) & 0xFF);
    dst[3] = (uint8_t)((value >> 24) & 0xFF);
}

static uint32_t crc32Update(uint32_t crc, const uint8_t* data, size_t len) {
    crc = ~crc;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            const uint32_t mask = -(crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

static bool fileCrc(const char* path, uint32_t* outCrc, uint32_t* outSize) {
    File file = tt_file_open(path, "r");
    if (!file) {
        return false;
    }
    uint32_t crc = 0;
    uint32_t size = 0;
    uint8_t buf[TT_FILE_BUF_SIZE];
    while (file.available()) {
        const int n = file.read(buf, sizeof(buf));
        if (n < 0) {
            file.close();
            return false;
        }
        if (n == 0) {
            break;
        }
        crc = crc32Update(crc, buf, (size_t)n);
        size += (uint32_t)n;
    }
    file.close();
    *outCrc = crc;
    *outSize = size;
    return true;
}

static bool ensureShotDir() {
    if (LittleFS.exists(TT_SHOT_DIR)) {
        return true;
    }
    if (!LittleFS.mkdir(TT_SHOT_DIR)) {
        LOG_E("Shot: mkdir %s failed", TT_SHOT_DIR);
        return false;
    }
    LOG_I("Shot: created %s", TT_SHOT_DIR);
    return true;
}

static int collectNames(char names[][TT_SHOT_NAME_MAX], int max) {
    if (!LittleFS.exists(TT_SHOT_DIR)) {
        return 0;
    }
    File dir = LittleFS.open(TT_SHOT_DIR);
    if (!dir) {
        LOG_E("Shot: open %s failed", TT_SHOT_DIR);
        return 0;
    }
    int count = 0;
    File file = dir.openNextFile();
    while (file) {
        const char* base = baseName(file.name());
        const bool isDir = file.isDirectory();
        file.close();
        if (!isDir && isSafeName(base)) {
            if (names != nullptr && count < max) {
                snprintf(names[count], TT_SHOT_NAME_MAX, "%s", base);
            }
            count++;
        }
        file = dir.openNextFile();
    }
    dir.close();
    return count;
}

bool TTScreenshotDisplay::ensureFrame() {
    if (_frame != nullptr) {
        return true;
    }
    _frame = (uint8_t*)heap_caps_malloc(TT_SHOT_FRAME_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (_frame == nullptr) {
        LOG_E("Shot: alloc %u failed, heap=%u",
              (unsigned)TT_SHOT_FRAME_BYTES, (unsigned)Util::heapFree());
        return false;
    }
    LOG_I("Shot: buffer %u bytes", (unsigned)TT_SHOT_FRAME_BYTES);
    return true;
}

bool TTScreenshotDisplay::hasRoom() const {
    const size_t total = LittleFS.totalBytes();
    const size_t used = LittleFS.usedBytes();
    const size_t freeBytes = total > used ? total - used : 0;
    const size_t need = TT_SHOT_FILE_BYTES + TT_SHOT_RESERVE_BYTES;
    LOG_I("Shot: free=%u need=%u used=%u total=%u",
          (unsigned)freeBytes, (unsigned)need, (unsigned)used, (unsigned)total);
    return freeBytes >= need;
}

void TTScreenshotDisplay::blit(const lv_area_t* area, const uint8_t* px) {
    if (_frame == nullptr || area == nullptr || px == nullptr) {
        return;
    }
    const int32_t srcW = area->x2 - area->x1 + 1;
    if (srcW <= 0) {
        return;
    }
    const int32_t srcStride = (srcW + 7) / 8;
    for (int32_t y = area->y1; y <= area->y2; y++) {
        if (y < 0 || y >= EPD_HEIGHT) {
            continue;
        }
        for (int32_t x = area->x1; x <= area->x2; x++) {
            if (x < 0 || x >= EPD_WIDTH) {
                continue;
            }
            const int32_t relX = x - area->x1;
            const int32_t relY = y - area->y1;
            const int32_t srcByte = relY * srcStride + (relX / 8);
            const int srcBit = 7 - (relX % 8);
            const bool white = ((px[srcByte] >> srcBit) & 0x01) != 0;
            const int32_t dstByte = y * TT_SHOT_FRAME_STRIDE + (x / 8);
            const uint8_t dstMask = (uint8_t)(1u << (7 - (x % 8)));
            if (white) {
                _frame[dstByte] |= dstMask;
            } else {
                _frame[dstByte] &= (uint8_t)~dstMask;
            }
        }
    }
}

bool TTScreenshotDisplay::save(const char* pageName) {
    if (!ensureShotDir()) {
        return false;
    }
    char token[TT_SHOT_TOKEN_MAX];
    copyToken(token, sizeof(token), pageName);
    char name[TT_SHOT_NAME_MAX];
    struct tm local;
    auto& rtc = TTInstanceOf<TTRtc>();
    if (rtc.isTimeValid() && rtc.getLocalTime(local)) {
        snprintf(name, sizeof(name), "%04d%02d%02d_%02d%02d%02d_%s.bmp",
                 local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
                 local.tm_hour, local.tm_min, local.tm_sec, token);
    } else {
        snprintf(name, sizeof(name), "m%08lu_%s.bmp", (unsigned long)millis(), token);
        LOG_W("Shot: clock invalid, using millis name");
    }

    char path[TT_SHOT_PATH_MAX];
    snprintf(path, sizeof(path), "%s/%s", TT_SHOT_DIR, name);
    for (int extra = 2; LittleFS.exists(path) && extra < 10; extra++) {
        if (rtc.isTimeValid()) {
            snprintf(name, sizeof(name), "%04d%02d%02d_%02d%02d%02d_%s_%d.bmp",
                     local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
                     local.tm_hour, local.tm_min, local.tm_sec, token, extra);
        } else {
            snprintf(name, sizeof(name), "m%08lu_%s_%d.bmp", (unsigned long)millis(), token, extra);
        }
        snprintf(path, sizeof(path), "%s/%s", TT_SHOT_DIR, name);
    }
    if (LittleFS.exists(path)) {
        LOG_E("Shot: name still exists %s", path);
        return false;
    }

    File file = tt_file_create(path);
    if (!file) {
        LOG_E("Shot: create %s failed", path);
        return false;
    }

    uint8_t header[62];
    memset(header, 0, sizeof(header));
    header[0] = 'B';
    header[1] = 'M';
    putU32(header + 2, TT_SHOT_FILE_BYTES);
    putU32(header + 10, 62);
    putU32(header + 14, 40);
    putU32(header + 18, EPD_WIDTH);
    putU32(header + 22, EPD_HEIGHT);
    putU16(header + 26, 1);
    putU16(header + 28, 1);
    putU32(header + 34, TT_SHOT_ROW_STRIDE * EPD_HEIGHT);
    header[58] = 255;
    header[59] = 255;
    header[60] = 255;
    if (file.write(header, sizeof(header)) != sizeof(header)) {
        file.close();
        tt_file_remove(path);
        LOG_E("Shot: write header failed %s", path);
        return false;
    }

    uint8_t row[TT_SHOT_ROW_STRIDE];
    bool ok = true;
    for (int32_t y = EPD_HEIGHT - 1; y >= 0; y--) {
        memset(row, 0, sizeof(row));
        memcpy(row, _frame + (y * TT_SHOT_FRAME_STRIDE), TT_SHOT_FRAME_STRIDE);
        if (file.write(row, sizeof(row)) != sizeof(row)) {
            ok = false;
            break;
        }
    }
    file.close();
    if (!ok) {
        tt_file_remove(path);
        LOG_E("Shot: write pixels failed %s", path);
        return false;
    }
    LOG_I("Shot: saved %s (%u bytes)", path, (unsigned)TT_SHOT_FILE_BYTES);
    return true;
}

TTShotResult TTScreenshotDisplay::capture(const char* pageName) {
    if (_busy) {
        LOG_W("Shot: capture already running");
        return TT_SHOT_FAILED;
    }
    if (!hasRoom()) {
        LOG_W("Shot: skip, free space below reserve");
        return TT_SHOT_NO_ROOM;
    }
    if (!ensureFrame()) {
        return TT_SHOT_FAILED;
    }

    _busy = true;
    memset(_frame, 0xFF, TT_SHOT_FRAME_BYTES);
    auto& driver = TTInstanceOf<TTLvglEpdDriver>();
    LOG_I("Shot: switch render to capture display, page=%s", pageName != nullptr ? pageName : "");
    driver.setCapture(this);
    driver.requestRefresh(TT_REFRESH_FULL);
    driver.setCapture(nullptr);
    const bool ok = save(pageName);
    _busy = false;
    LOG_I("Shot: render restored, saved=%d", ok ? 1 : 0);
    return ok ? TT_SHOT_OK : TT_SHOT_FAILED;
}

int TTScreenshotDisplay::list(char names[][TT_SHOT_NAME_MAX], int max) {
    if (max < 0) {
        max = 0;
    }
    const int found = collectNames(names, max);
    if (found > max) {
        LOG_W("Shot: list truncated %d/%d", max, found);
        return max;
    }
    return found;
}

bool TTScreenshotDisplay::resolve(const char* name, char* path, size_t pathMax) {
    if (!isSafeName(name) || path == nullptr || pathMax < 8) {
        return false;
    }
    const int n = snprintf(path, pathMax, "%s/%s", TT_SHOT_DIR, name);
    if (n <= 0 || (size_t)n >= pathMax) {
        return false;
    }
    return LittleFS.exists(path);
}

bool TTScreenshotDisplay::removeOne(const char* name) {
    char path[TT_SHOT_PATH_MAX];
    if (!resolve(name, path, sizeof(path))) {
        LOG_W("Shot: delete missing %s", name != nullptr ? name : "");
        return false;
    }
    tt_file_remove(path);
    LOG_I("Shot: deleted %s", path);
    return !LittleFS.exists(path);
}

int TTScreenshotDisplay::removeAll() {
    char names[TT_SHOT_MAX][TT_SHOT_NAME_MAX];
    int removed = 0;
    for (;;) {
        const int count = list(names, TT_SHOT_MAX);
        if (count <= 0) {
            break;
        }
        for (int i = 0; i < count; i++) {
            if (removeOne(names[i])) {
                removed++;
            }
        }
        if (count < TT_SHOT_MAX) {
            break;
        }
    }
    LOG_I("Shot: deleted %d files", removed);
    return removed;
}

static TTShotZipItem* loadZipItems(int* count, uint32_t* total) {
    char names[TT_SHOT_MAX][TT_SHOT_NAME_MAX];
    const int found = collectNames(names, TT_SHOT_MAX);
    const int n = found > TT_SHOT_MAX ? TT_SHOT_MAX : found;
    if (n <= 0) {
        *count = 0;
        *total = 0;
        return nullptr;
    }
    TTShotZipItem* items = new TTShotZipItem[n];
    if (items == nullptr) {
        LOG_E("Shot: zip index alloc failed");
        *count = 0;
        *total = 0;
        return nullptr;
    }
    uint32_t offset = 0;
    uint32_t central = 0;
    for (int i = 0; i < n; i++) {
        snprintf(items[i].name, sizeof(items[i].name), "%s", names[i]);
        char path[TT_SHOT_PATH_MAX];
        if (snprintf(path, sizeof(path), "%s/%s", TT_SHOT_DIR, items[i].name) <= 0
            || !fileCrc(path, &items[i].crc, &items[i].size)) {
            delete[] items;
            LOG_E("Shot: zip stat failed %s", names[i]);
            *count = 0;
            *total = 0;
            return nullptr;
        }
        items[i].localOffset = offset;
        const uint32_t nameLen = (uint32_t)strlen(items[i].name);
        offset += 30 + nameLen + items[i].size;
        central += 46 + nameLen;
    }
    *count = n;
    *total = offset + central + 22;
    return items;
}

bool TTScreenshotDisplay::zipBytes(size_t* outBytes) {
    int count = 0;
    uint32_t total = 0;
    TTShotZipItem* items = loadZipItems(&count, &total);
    delete[] items;
    if (outBytes != nullptr) {
        *outBytes = total;
    }
    return count > 0;
}

bool TTScreenshotDisplay::writeZip(TTShotWriter write, void* ctx) {
    if (write == nullptr) {
        return false;
    }
    int count = 0;
    uint32_t total = 0;
    TTShotZipItem* items = loadZipItems(&count, &total);
    if (items == nullptr || count <= 0) {
        delete[] items;
        return false;
    }
    uint32_t offset = 0;
    uint32_t central = 0;
    for (int i = 0; i < count; i++) {
        const uint32_t nameLen = (uint32_t)strlen(items[i].name);
        offset += 30 + nameLen + items[i].size;
        central += 46 + nameLen;
    }

    struct tm local;
    uint16_t dosTime = 0;
    uint16_t dosDate = 0;
    auto& rtc = TTInstanceOf<TTRtc>();
    if (rtc.isTimeValid() && rtc.getLocalTime(local)) {
        dosTime = (uint16_t)((local.tm_hour << 11) | (local.tm_min << 5) | (local.tm_sec / 2));
        dosDate = (uint16_t)(((local.tm_year + 1900 - 1980) << 9) | ((local.tm_mon + 1) << 5) | local.tm_mday);
    }

    bool ok = true;
    for (int i = 0; i < count && ok; i++) {
        const uint16_t nameLen = (uint16_t)strlen(items[i].name);
        uint8_t header[30];
        memset(header, 0, sizeof(header));
        putU32(header, 0x04034b50);
        putU16(header + 4, 20);
        putU16(header + 6, 0x0800);
        putU16(header + 10, dosTime);
        putU16(header + 12, dosDate);
        putU32(header + 14, items[i].crc);
        putU32(header + 18, items[i].size);
        putU32(header + 22, items[i].size);
        putU16(header + 26, nameLen);
        if (!write(ctx, header, sizeof(header)) || !write(ctx, (const uint8_t*)items[i].name, nameLen)) {
            ok = false;
            break;
        }
        char path[TT_SHOT_PATH_MAX];
        if (!resolve(items[i].name, path, sizeof(path))) {
            ok = false;
            break;
        }
        File file = tt_file_open(path, "r");
        if (!file) {
            ok = false;
            break;
        }
        uint8_t buf[TT_FILE_BUF_SIZE];
        while (ok && file.available()) {
            const int n = file.read(buf, sizeof(buf));
            if (n <= 0 || !write(ctx, buf, (size_t)n)) {
                ok = false;
            }
        }
        file.close();
    }

    const uint32_t centralOffset = offset;
    for (int i = 0; i < count && ok; i++) {
        const uint16_t nameLen = (uint16_t)strlen(items[i].name);
        uint8_t header[46];
        memset(header, 0, sizeof(header));
        putU32(header, 0x02014b50);
        putU16(header + 4, 20);
        putU16(header + 6, 20);
        putU16(header + 8, 0x0800);
        putU16(header + 12, dosTime);
        putU16(header + 14, dosDate);
        putU32(header + 16, items[i].crc);
        putU32(header + 20, items[i].size);
        putU32(header + 24, items[i].size);
        putU16(header + 28, nameLen);
        putU32(header + 42, items[i].localOffset);
        if (!write(ctx, header, sizeof(header)) || !write(ctx, (const uint8_t*)items[i].name, nameLen)) {
            ok = false;
        }
    }
    if (ok) {
        uint8_t end[22];
        memset(end, 0, sizeof(end));
        putU32(end, 0x06054b50);
        putU16(end + 8, (uint16_t)count);
        putU16(end + 10, (uint16_t)count);
        putU32(end + 12, central);
        putU32(end + 16, centralOffset);
        ok = write(ctx, end, sizeof(end));
    }
    delete[] items;
    if (!ok) {
        LOG_E("Shot: zip write failed");
    } else {
        LOG_I("Shot: zip %u bytes, %d files", (unsigned)total, count);
    }
    return ok;
}
