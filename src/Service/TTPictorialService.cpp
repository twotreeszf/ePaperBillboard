#include "TTPictorialService.h"
#include "../Base/Logger.h"
#include "../Base/TTFile.h"
#include "../Base/TTHttpsClient.h"
#include "../Base/TTInstance.h"
#include "../Base/TTNotificationPayloads.h"
#include "../Base/TTPreference.h"
#include "../Base/TTRtc.h"
#include "../Tasks/TTUITask.h"
#include "../Tasks/TTWiFiTask.h"
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <esp_system.h>
#include <cstdio>
#include <cstring>

namespace {

void copyText(char* dst, size_t dstLen, const char* src) {
    if (dst == nullptr || dstLen == 0) {
        return;
    }
    if (src == nullptr) {
        dst[0] = '\0';
        return;
    }
    strncpy(dst, src, dstLen - 1);
    dst[dstLen - 1] = '\0';
}

bool pinyinOk(const char* text) {
    if (text == nullptr || text[0] == '\0') {
        return false;
    }
    size_t len = 0;
    for (const char* p = text; *p != '\0'; p++) {
        const char c = *p;
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
            || (c >= '0' && c <= '9') || c == '_' || c == '-';
        if (!ok) {
            return false;
        }
        len++;
        if (len >= TT_PIC_PINYIN_MAX) {
            return false;
        }
    }
    return true;
}

bool copyBody(const char* src, size_t offset, size_t len, const char* dst) {
    File in = tt_file_open(src, "r");
    if (!in || !in.seek((uint32_t)offset)) {
        if (in) {
            in.close();
        }
        LOG_E("Pictorial: copy open %s failed", src);
        return false;
    }
    File out = tt_file_create(dst);
    if (!out) {
        in.close();
        LOG_E("Pictorial: create %s failed", dst);
        return false;
    }
    uint8_t buf[256];
    size_t left = len;
    bool ok = true;
    while (left > 0) {
        const size_t n = left > sizeof(buf) ? sizeof(buf) : left;
        const size_t got = in.readBytes((char*)buf, n);
        if (got != n || out.write(buf, got) != got) {
            ok = false;
            break;
        }
        left -= got;
    }
    in.close();
    out.close();
    if (!ok) {
        LOG_E("Pictorial: copy %s failed", dst);
        tt_file_remove(dst);
    }
    return ok;
}

bool i1Ok(const char* path) {
    File file = tt_file_open(path, "r");
    if (!file) {
        return false;
    }
    uint8_t buf[TT_I1_HEADER_SIZE];
    const bool read = file.read(buf, sizeof(buf)) == sizeof(buf);
    file.close();
    if (!read) {
        return false;
    }
    if (buf[0] != TT_I1_MAGIC0 || buf[1] != TT_I1_MAGIC1
        || buf[2] != TT_I1_MAGIC2 || buf[3] != TT_I1_MAGIC3) {
        return false;
    }
    const int32_t w = (int32_t)((uint16_t)buf[4] | ((uint16_t)buf[5] << 8));
    const int32_t h = (int32_t)((uint16_t)buf[6] | ((uint16_t)buf[7] << 8));
    if (w <= 0 || h <= 0 || w > TT_STREAM_IMAGE_MAX_W || h > TT_STREAM_IMAGE_MAX_H) {
        LOG_E("Pictorial: image %ld x %ld out of range", (long)w, (long)h);
        return false;
    }
    return true;
}

bool httpGet(const char* url, size_t bodyMax, const char* tmpPath, TTHttpsResult* out) {
    TTHttpsRequest request = {};
    request.url = url;
    request.bodyMax = bodyMax;
    LOG_I("Pictorial: GET %s", url);
    if (!tt_https_exchange_file(&request, tmpPath, out)) {
        LOG_E("Pictorial: HTTPS failed %s", url);
        return false;
    }
    LOG_I("Pictorial: HTTP %d body=%u", out->status, (unsigned)out->bodyLen);
    return out->status == 200 && out->bodyLen > 0;
}

}  // namespace

bool TTPictorialService::seriesAt(uint8_t index, TTPicSeries* out) const {
    if (out == nullptr || index >= _seriesCount) {
        return false;
    }
    *out = _series[index];
    return true;
}

int TTPictorialService::todayKey() const {
    if (!TTInstanceOf<TTRtc>().isTimeValid()) {
        return 0;
    }
    struct tm local = {};
    if (!TTInstanceOf<TTRtc>().getLocalTime(local)) {
        return 0;
    }
    return (local.tm_year + 1900) * 1000 + local.tm_yday;
}

bool TTPictorialService::hasToday() const {
    const int today = todayKey();
    if (today == 0) {
        return false;
    }
    int day = 0;
    TTInstanceOf<TTPreference>().get(PREF_PIC_DAY, day, 0);
    if (day != today) {
        return false;
    }
    String path;
    TTInstanceOf<TTPreference>().get(PREF_PIC_PATH, path, String(""));
    return path.length() > 0 && tt_file_exists(path.c_str());
}

void TTPictorialService::publish(TTPicState state, const char* message, const char* path, const char* name) {
    TTPicPayload payload = {};
    payload.state = state;
    copyText(payload.message, sizeof(payload.message), message);
    copyText(payload.path, sizeof(payload.path), path);
    copyText(payload.name, sizeof(payload.name), name);
    LOG_I("Pictorial: state=%d name=%s path=%s msg=%s",
          (int)state, payload.name, payload.path, payload.message);
    TTInstanceOf<TTUITask>().postNotification(TT_NOTIFICATION_PICTORIAL, payload);
}

void TTPictorialService::remember(const char* pinyin, uint16_t index, int dayKey, const char* path) {
    const char* name = "";
    if (_selected < _seriesCount) {
        name = _series[_selected].name;
    }
    auto& pref = TTInstanceOf<TTPreference>();
    pref.set(PREF_PIC_PINYIN, String(pinyin != nullptr ? pinyin : ""));
    pref.set(PREF_PIC_INDEX, (int)index);
    pref.set(PREF_PIC_DAY, dayKey);
    pref.set(PREF_PIC_PATH, String(path != nullptr ? path : ""));
    pref.set(PREF_PIC_NAME, String(name));
    if (!pref.sync()) {
        LOG_E("Pictorial: preference sync failed");
    }
    _imageIndex = index;
}

void TTPictorialService::removeStale(const char* keep) {
    File dir = LittleFS.open(TT_FS_TMP_DIR);
    if (!dir) {
        return;
    }
    char stale[8][TT_STREAM_IMAGE_PATH_MAX];
    int staleCount = 0;
    File file = dir.openNextFile();
    while (file && staleCount < 8) {
        const char* name = file.name();
        const char* base = name;
        if (name != nullptr) {
            const char* slash = strrchr(name, '/');
            if (slash != nullptr) {
                base = slash + 1;
            }
        }
        char full[TT_STREAM_IMAGE_PATH_MAX];
        full[0] = '\0';
        if (base != nullptr && base[0] == 'p') {
            snprintf(full, sizeof(full), "%s/%s", TT_FS_TMP_DIR, base);
        }
        file.close();
        if (full[0] != '\0' && strstr(full, ".i1") != nullptr
            && (keep == nullptr || strcmp(full, keep) != 0)) {
            copyText(stale[staleCount], sizeof(stale[staleCount]), full);
            staleCount++;
        }
        file = dir.openNextFile();
    }
    dir.close();
    for (int i = 0; i < staleCount; i++) {
        LOG_I("Pictorial: delete %s", stale[i]);
        tt_file_remove(stale[i]);
    }
}

uint16_t TTPictorialService::pickIndex(uint16_t count, uint16_t avoid, bool avoidCurrent) const {
    if (count <= 1) {
        return 1;
    }
    uint16_t index = (uint16_t)(esp_random() % count) + 1;
    if (avoidCurrent && index == avoid) {
        index = (uint16_t)(index % count) + 1;
    }
    return index;
}

bool TTPictorialService::loadManifest() {
    TTHttpsResult res = {};
    if (!httpGet(TT_PIC_MANIFEST_URL, TT_PIC_MANIFEST_MAX, TT_PIC_HTTP_PATH, &res)) {
        tt_file_remove(TT_PIC_HTTP_PATH);
        return false;
    }
    File file = tt_file_open(TT_PIC_HTTP_PATH, "r");
    if (!file || !file.seek(res.bodyOffset)) {
        if (file) {
            file.close();
        }
        tt_file_remove(TT_PIC_HTTP_PATH);
        return false;
    }
    JsonDocument doc;
    const DeserializationError err = deserializeJson(doc, file);
    file.close();
    tt_file_remove(TT_PIC_HTTP_PATH);
    if (err || doc.overflowed()) {
        LOG_E("Pictorial: manifest JSON %s", err ? err.c_str() : "overflow");
        return false;
    }

    JsonArray items = doc["items"].as<JsonArray>();
    if (items.isNull()) {
        items = doc.as<JsonArray>();
    }
    _seriesCount = 0;
    for (JsonObject item : items) {
        if (_seriesCount >= TT_PIC_SERIES_MAX) {
            LOG_W("Pictorial: manifest truncated at %d", TT_PIC_SERIES_MAX);
            break;
        }
        const char* pinyin = item["pinyin"] | "";
        const int count = item["count"] | 0;
        if (!pinyinOk(pinyin) || count <= 0 || count > 9999) {
            LOG_W("Pictorial: skip series pinyin=%s count=%d", pinyin, count);
            continue;
        }
        TTPicSeries* series = &_series[_seriesCount];
        memset(series, 0, sizeof(*series));
        const char* name = item["name"] | pinyin;
        copyText(series->name, sizeof(series->name), name);
        copyText(series->pinyin, sizeof(series->pinyin), pinyin);
        series->count = (uint16_t)count;
        _seriesCount++;
    }
    LOG_I("Pictorial: manifest series=%u", (unsigned)_seriesCount);
    return _seriesCount > 0;
}

bool TTPictorialService::ensureManifest() {
    if (_seriesCount > 0) {
        return true;
    }
    return loadManifest();
}

int TTPictorialService::resolveSelected() const {
    String pinyin;
    TTInstanceOf<TTPreference>().get(PREF_PIC_PINYIN, pinyin, String(""));
    if (pinyin.length() == 0) {
        return 0;
    }
    for (uint8_t i = 0; i < _seriesCount; i++) {
        if (pinyin == _series[i].pinyin) {
            return (int)i;
        }
    }
    LOG_W("Pictorial: saved series %s missing, use first", pinyin.c_str());
    return 0;
}

bool TTPictorialService::downloadIndex(const TTPicSeries& series, uint16_t index, bool updateDay) {
    char url[192];
    snprintf(url, sizeof(url), "%s%s/%04u.i1", TT_PIC_URL_BASE, series.pinyin, (unsigned)index);
    LOG_I("Pictorial: series=%s index=%u count=%u", series.pinyin, (unsigned)index, (unsigned)series.count);

    TTHttpsResult res = {};
    if (!httpGet(url, TT_PIC_IMAGE_MAX, TT_PIC_HTTP_PATH, &res)) {
        tt_file_remove(TT_PIC_HTTP_PATH);
        return false;
    }

    char nextPath[TT_STREAM_IMAGE_PATH_MAX];
    snprintf(nextPath, sizeof(nextPath), "%s/p%08lx.i1", TT_FS_TMP_DIR, (unsigned long)esp_random());
    String previous;
    TTInstanceOf<TTPreference>().get(PREF_PIC_PATH, previous, String(""));
    if (previous == nextPath) {
        snprintf(nextPath, sizeof(nextPath), "%s/p%08lx.i1", TT_FS_TMP_DIR, (unsigned long)esp_random());
    }

    const bool copied = copyBody(TT_PIC_HTTP_PATH, res.bodyOffset, res.bodyLen, nextPath);
    tt_file_remove(TT_PIC_HTTP_PATH);
    if (!copied || !i1Ok(nextPath)) {
        LOG_E("Pictorial: bad image %s", nextPath);
        tt_file_remove(nextPath);
        return false;
    }

    const int dayKey = updateDay ? todayKey() : 0;
    int savedDay = 0;
    TTInstanceOf<TTPreference>().get(PREF_PIC_DAY, savedDay, 0);
    remember(series.pinyin, index, dayKey > 0 ? dayKey : savedDay, nextPath);
    publish(TT_PIC_OK, "", nextPath, series.name);
    if (previous.length() > 0 && previous != nextPath) {
        LOG_I("Pictorial: replace %s -> %s", previous.c_str(), nextPath);
    }
    removeStale(nextPath);
    return true;
}

void TTPictorialService::runJob(TTPicJob job, uint8_t seriesIndex) {
    if (!ensureManifest()) {
        _busy = false;
        publish(TT_PIC_FAILED, "找不到漫画列表", "", "");
        return;
    }
    _selected = (uint8_t)resolveSelected();
    if (job == TT_PIC_JOB_SERIES) {
        if (seriesIndex >= _seriesCount) {
            _busy = false;
            publish(TT_PIC_FAILED, "没有这部漫画", "", "");
            return;
        }
        _selected = seriesIndex;
    }
    if (job == TT_PIC_JOB_TODAY || job == TT_PIC_JOB_DAILY) {
        if (hasToday()) {
            _busy = false;
            String path;
            String name;
            TTInstanceOf<TTPreference>().get(PREF_PIC_PATH, path, String(""));
            TTInstanceOf<TTPreference>().get(PREF_PIC_NAME, name, String(""));
            publish(TT_PIC_OK, "", path.c_str(), name.c_str());
            return;
        }
    }

    const TTPicSeries series = _series[_selected];
    int savedIndex = 0;
    TTInstanceOf<TTPreference>().get(PREF_PIC_INDEX, savedIndex, 0);
    const uint16_t index = pickIndex(series.count, (uint16_t)savedIndex, true);
    const bool updateDay = job != TT_PIC_JOB_ANOTHER || todayKey() != 0;
    if (!downloadIndex(series, index, updateDay)) {
        _busy = false;
        publish(TT_PIC_FAILED, "画报下载失败", "", series.name);
        return;
    }
    _busy = false;
}

void TTPictorialService::enqueue(TTPicJob job, uint8_t seriesIndex) {
    if (_busy) {
        LOG_I("Pictorial: ignore job %d, busy", (int)job);
        return;
    }
    if (todayKey() == 0 && (job == TT_PIC_JOB_TODAY || job == TT_PIC_JOB_DAILY)) {
        LOG_W("Pictorial: clock invalid, skip day job");
        publish(TT_PIC_FAILED, "时间未同步", "", "");
        return;
    }
    _busy = true;
    if (job != TT_PIC_JOB_DAILY || !hasToday()) {
        publish(TT_PIC_FETCHING, "正在获取画报", "", "");
    }
    LOG_I("Pictorial: enqueue job=%d series=%u", (int)job, (unsigned)seriesIndex);
    TTInstanceOf<TTWiFiTask>().runWithRadio(
        "pictorial",
        [this, job, seriesIndex]() { runJob(job, seriesIndex); },
        [this]() {
            _busy = false;
            LOG_W("Pictorial: radio failed");
            publish(TT_PIC_FAILED, "未连接 Wi-Fi", "", "");
        });
}

void TTPictorialService::requestToday() {
    if (hasToday() && _seriesCount > 0) {
        String path;
        String name;
        TTInstanceOf<TTPreference>().get(PREF_PIC_PATH, path, String(""));
        TTInstanceOf<TTPreference>().get(PREF_PIC_NAME, name, String(""));
        publish(TT_PIC_OK, "", path.c_str(), name.c_str());
        return;
    }
    enqueue(TT_PIC_JOB_TODAY, 0);
}

void TTPictorialService::requestAnother() {
    enqueue(TT_PIC_JOB_ANOTHER, 0);
}

void TTPictorialService::requestDaily() {
    if (hasToday()) {
        return;
    }
    enqueue(TT_PIC_JOB_DAILY, 0);
}

void TTPictorialService::requestSeries(uint8_t index) {
    enqueue(TT_PIC_JOB_SERIES, index);
}
