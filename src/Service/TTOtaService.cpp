#include "TTOtaService.h"
#include "../Base/Logger.h"
#include "../Base/TTFile.h"
#include "../Base/TTFirmwareVersion.h"
#include "../Base/TTHttpsClient.h"
#include "../Base/TTInstance.h"
#include "../Base/TTNotificationPayloads.h"
#include "../Tasks/TTUITask.h"
#include "../Tasks/TTWiFiTask.h"
#include <Arduino.h>
#include <LittleFS.h>
#include <Update.h>
#include <mbedtls/sha256.h>
#include <cstring>
#include <strings.h>

namespace {

struct TTOtaEntry {
    char remote[TT_OTA_REMOTE_MAX];
    char sha[TT_OTA_SHA_HEX + 1];
    uint32_t size;
    bool firmware;
};

struct TTOtaDoc {
    char version[TT_OTA_VER_MAX];
    char notes[TT_OTA_MSG_MAX];
    size_t bodyOffset;
    size_t bodyLen;
};

typedef bool (*TTOtaEntryFn)(const TTOtaEntry* entry, void* ctx);

struct TTOtaWriteCtx {
    File file;
    const char* path;
    TTHttpsResult* result;
    mbedtls_sha256_context sha;
    size_t written;
    uint32_t expect;
    size_t fileIndex;
    size_t fileTotal;
    uint32_t lastProgressMs;
    uint8_t lastPercent;
    bool opened;
    bool shaOn;
    bool useUpdate;
};

void postOta(TTOtaPhase phase, const char* version, const char* message) {
    TTOtaPayload payload = {};
    payload.phase = phase;
    if (version != nullptr) {
        strncpy(payload.version, version, sizeof(payload.version) - 1);
    }
    if (message != nullptr) {
        strncpy(payload.message, message, sizeof(payload.message) - 1);
    }
    LOG_I("OTA: phase=%d %s", (int)phase, payload.message);
    TTInstanceOf<TTUITask>().postNotification(TT_NOTIFICATION_OTA, payload);
}

void hexEncode(const uint8_t* in, size_t len, char* out, size_t outLen) {
    static const char* kHex = "0123456789abcdef";
    size_t n = 0;
    for (size_t i = 0; i < len && n + 2 < outLen; i++) {
        out[n++] = kHex[in[i] >> 4];
        out[n++] = kHex[in[i] & 0x0f];
    }
    out[n] = '\0';
}

int hexValue(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

size_t encodeUtf8(uint32_t cp, char* out) {
    if (cp < 0x80) {
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    out[0] = (char)(0xE0 | (cp >> 12));
    out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[2] = (char)(0x80 | (cp & 0x3F));
    return 3;
}

bool jsonQuoted(const char* line, const char* key, char* out, size_t max, bool* truncated) {
    if (truncated != nullptr) {
        *truncated = false;
    }
    const char* p = strstr(line, key);
    if (p == nullptr) {
        return false;
    }
    p = strchr(p, ':');
    if (p == nullptr) {
        return false;
    }
    p = strchr(p, '"');
    if (p == nullptr) {
        return false;
    }
    p++;
    size_t n = 0;
    bool cut = false;
    while (*p != '\0' && *p != '"') {
        unsigned char bytes[3];
        size_t count = 1;
        bool encoded = false;
        char c = *p++;
        if (c == '\\' && *p != '\0') {
            char esc = *p++;
            if (esc == 'n') {
                c = '\n';
            } else if (esc == 't') {
                c = '\t';
            } else if (esc == 'u') {
                uint32_t cp = 0;
                for (int i = 0; i < 4; i++) {
                    const int h = hexValue(*p);
                    if (h < 0) {
                        return false;
                    }
                    cp = (cp << 4) | (uint32_t)h;
                    p++;
                }
                count = encodeUtf8(cp, (char*)bytes);
                encoded = true;
            } else {
                c = esc;
            }
        }
        if (!encoded) {
            bytes[0] = (unsigned char)c;
            count = 1;
        }
        for (size_t i = 0; i < count; i++) {
            if (n + 1 >= max) {
                cut = true;
                break;
            }
            out[n++] = (char)bytes[i];
        }
    }
    out[n] = '\0';
    if (truncated != nullptr) {
        *truncated = cut || *p != '"';
    }
    return *p == '"' && !cut;
}

bool jsonUint(const char* line, const char* key, uint32_t* out) {
    const char* p = strstr(line, key);
    if (p == nullptr) {
        return false;
    }
    p = strchr(p, ':');
    if (p == nullptr) {
        return false;
    }
    char* end = nullptr;
    unsigned long value = strtoul(p + 1, &end, 10);
    if (end == p + 1) {
        return false;
    }
    *out = (uint32_t)value;
    return true;
}

bool readManifestLine(File& file, size_t endPos, char* buf, size_t max) {
    size_t n = 0;
    if (file.position() >= endPos) {
        buf[0] = '\0';
        return false;
    }
    while (file.position() < endPos) {
        int c = file.read();
        if (c < 0) {
            break;
        }
        if (c == '\r') {
            continue;
        }
        if (c == '\n') {
            buf[n] = '\0';
            return true;
        }
        if (n + 1 < max) {
            buf[n++] = (char)c;
        }
    }
    buf[n] = '\0';
    return n > 0;
}

bool isFirmwarePath(const char* remote) {
    const char* slash = strrchr(remote, '/');
    const char* name = slash != nullptr ? slash + 1 : remote;
    return strcmp(name, "firmware.bin") == 0 && strstr(remote, "/res/") == nullptr;
}

bool isPackedManifest(const char* remote) {
    return strstr(remote, "/res/manifest.json") != nullptr;
}

bool resLocalPath(const char* remote, char* local, size_t max) {
    const char* mark = strstr(remote, "/res/");
    if (mark == nullptr || isPackedManifest(remote)) {
        return false;
    }
    if (strlen(mark) >= max || strlen(mark) >= TT_FILE_FULL_PATH_MAX) {
        return false;
    }
    memcpy(local, mark, strlen(mark) + 1);
    return true;
}

bool ensureParent(const char* filePath) {
    char buf[TT_FILE_FULL_PATH_MAX];
    if (filePath == nullptr || snprintf(buf, sizeof(buf), "%s", filePath) >= (int)sizeof(buf)) {
        return false;
    }
    for (char* p = buf + 1; *p != '\0'; p++) {
        if (*p != '/') {
            continue;
        }
        *p = '\0';
        if (!LittleFS.exists(buf) && !LittleFS.mkdir(buf)) {
            LOG_E("OTA: mkdir %s failed", buf);
            return false;
        }
        *p = '/';
    }
    return true;
}

bool sha256File(const char* path, char* hex, size_t hexLen) {
    if (!tt_file_exists(path)) {
        return false;
    }
    File file = tt_file_open(path, "r");
    if (!file) {
        return false;
    }
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    if (mbedtls_sha256_starts_ret(&ctx, 0) != 0) {
        mbedtls_sha256_free(&ctx);
        file.close();
        return false;
    }
    uint8_t buf[512];
    while (file.available()) {
        size_t n = file.readBytes((char*)buf, sizeof(buf));
        if (n == 0) {
            break;
        }
        if (mbedtls_sha256_update_ret(&ctx, buf, n) != 0) {
            mbedtls_sha256_free(&ctx);
            file.close();
            return false;
        }
        yield();
    }
    file.close();
    uint8_t dig[32];
    if (mbedtls_sha256_finish_ret(&ctx, dig) != 0) {
        mbedtls_sha256_free(&ctx);
        return false;
    }
    mbedtls_sha256_free(&ctx);
    hexEncode(dig, sizeof(dig), hex, hexLen);
    return hex[0] != '\0';
}

bool finishHash(TTOtaWriteCtx* writer, const char* expect) {
    if (!writer->shaOn) {
        return false;
    }
    uint8_t dig[32];
    bool ok = mbedtls_sha256_finish_ret(&writer->sha, dig) == 0;
    mbedtls_sha256_free(&writer->sha);
    writer->shaOn = false;
    if (!ok) {
        return false;
    }
    char hex[TT_OTA_SHA_HEX + 1];
    hexEncode(dig, sizeof(dig), hex, sizeof(hex));
    return strcasecmp(hex, expect) == 0;
}

void dropHash(TTOtaWriteCtx* writer) {
    if (!writer->shaOn) {
        return;
    }
    uint8_t dig[32];
    mbedtls_sha256_finish_ret(&writer->sha, dig);
    mbedtls_sha256_free(&writer->sha);
    writer->shaOn = false;
}

void copyProgressName(const char* path, char* out, size_t outLen) {
    if (outLen == 0) {
        return;
    }
    out[0] = '\0';
    if (path == nullptr) {
        return;
    }
    const size_t len = strlen(path);
    if (len < outLen) {
        memcpy(out, path, len + 1);
        return;
    }
    memcpy(out, path + len - (outLen - 1), outLen - 1);
    out[outLen - 1] = '\0';
}

void reportWriteProgress(TTOtaWriteCtx* writer) {
    if (writer->expect == 0) {
        return;
    }
    if (!writer->useUpdate && writer->fileTotal == 0) {
        return;
    }
    unsigned percent = (unsigned)(((uint64_t)writer->written * 100) / writer->expect);
    if (percent > 100) {
        percent = 100;
    }
    const bool detail = writer->useUpdate || writer->expect >= TT_OTA_PROGRESS_DETAIL;
    if (!detail && writer->written > 0) {
        return;
    }
    if (writer->lastPercent != 255) {
        if (percent == writer->lastPercent) {
            return;
        }
        if (percent < 100
            && percent < (unsigned)writer->lastPercent + TT_OTA_PROGRESS_PERCENT) {
            return;
        }
        if (percent < 100
            && (uint32_t)(millis() - writer->lastProgressMs) < TT_OTA_PROGRESS_MIN_MS) {
            return;
        }
    }
    writer->lastPercent = (uint8_t)percent;
    writer->lastProgressMs = millis();
    char message[TT_OTA_MSG_MAX];
    if (writer->useUpdate) {
        snprintf(message, sizeof(message), "写入固件\n%u%%", percent);
    } else if (detail) {
        char name[TT_OTA_PROGRESS_NAME];
        copyProgressName(writer->path, name, sizeof(name));
        snprintf(message, sizeof(message), "更新 %u/%u\n%s\n%u%%",
                 (unsigned)writer->fileIndex, (unsigned)writer->fileTotal, name, percent);
    } else {
        char name[TT_OTA_PROGRESS_NAME];
        copyProgressName(writer->path, name, sizeof(name));
        snprintf(message, sizeof(message), "更新 %u/%u\n%s",
                 (unsigned)writer->fileIndex, (unsigned)writer->fileTotal, name);
    }
    postOta(TT_OTA_PHASE_PROGRESS, nullptr, message);
}

bool otaWriteBody(void* ctx, const uint8_t* data, size_t len) {
    TTOtaWriteCtx* writer = static_cast<TTOtaWriteCtx*>(ctx);
    if (writer == nullptr || writer->result == nullptr || writer->result->status != 200) {
        return false;
    }
    if (!writer->opened) {
        if (!writer->useUpdate) {
            if (!ensureParent(writer->path)) {
                return false;
            }
            writer->file = tt_file_create(writer->path);
            if (!writer->file) {
                return false;
            }
        }
        mbedtls_sha256_init(&writer->sha);
        if (mbedtls_sha256_starts_ret(&writer->sha, 0) != 0) {
            mbedtls_sha256_free(&writer->sha);
            return false;
        }
        writer->shaOn = true;
        writer->opened = true;
    }
    if (mbedtls_sha256_update_ret(&writer->sha, data, len) != 0) {
        return false;
    }
    if (writer->useUpdate) {
        if (Update.write(const_cast<uint8_t*>(data), len) != len) {
            LOG_E("OTA: flash write failed at %u", (unsigned)writer->written);
            return false;
        }
    } else if (writer->file.write(data, len) != len) {
        LOG_E("OTA: file write failed %s", writer->path != nullptr ? writer->path : "");
        return false;
    }
    writer->written += len;
    if (writer->useUpdate && (writer->written % (64 * 1024)) < len) {
        LOG_I("OTA: firmware wrote %u", (unsigned)writer->written);
    }
    reportWriteProgress(writer);
    yield();
    return true;
}

bool readManifest(const char* path, size_t offset, size_t bodyLen, TTOtaDoc* doc,
                  TTOtaEntryFn fn, void* ctx) {
    File file = tt_file_open(path, "r");
    if (!file) {
        return false;
    }
    char version[TT_OTA_VER_MAX];
    char notes[TT_OTA_MSG_MAX];
    version[0] = '\0';
    notes[0] = '\0';
    if (doc != nullptr) {
        doc->version[0] = '\0';
        doc->notes[0] = '\0';
    }
    char* versionOut = doc != nullptr ? doc->version : version;
    char* notesOut = doc != nullptr ? doc->notes : notes;
    const size_t versionMax = doc != nullptr ? sizeof(doc->version) : sizeof(version);
    const size_t notesMax = doc != nullptr ? sizeof(doc->notes) : sizeof(notes);

    bool ok = false;
    bool havePath = false;
    bool haveSize = false;
    bool skipEntry = false;
    bool sawVersion = false;
    bool sawNotes = false;
    char entryPath[TT_OTA_REMOTE_MAX];
    char sha[TT_OTA_SHA_HEX + 1];
    uint32_t size = 0;
    entryPath[0] = '\0';
    sha[0] = '\0';

    if (!file.seek((uint32_t)offset)) {
        LOG_E("OTA: manifest seek failed");
        file.close();
        return false;
    }
    const size_t endPos = offset + bodyLen;
    char line[TT_OTA_LINE_MAX];
    while (readManifestLine(file, endPos, line, sizeof(line))) {
        bool truncated = false;
        if (!sawVersion && jsonQuoted(line, "\"version\"", versionOut, versionMax, &truncated)) {
            if (truncated || versionOut[0] == '\0') {
                LOG_E("OTA: bad version");
                file.close();
                return false;
            }
            sawVersion = true;
            continue;
        }
        if (!sawNotes && strstr(line, "\"notes\"") != nullptr) {
            jsonQuoted(line, "\"notes\"", notesOut, notesMax, nullptr);
            sawNotes = true;
            continue;
        }
        if (jsonQuoted(line, "\"path\"", entryPath, sizeof(entryPath), &truncated)) {
            if (havePath) {
                LOG_E("OTA: manifest entry missing sha");
                file.close();
                return false;
            }
            if (truncated) {
                LOG_E("OTA: path too long");
                file.close();
                return false;
            }
            havePath = true;
            haveSize = false;
            sha[0] = '\0';
            size = 0;
            skipEntry = isPackedManifest(entryPath);
            if (skipEntry) {
                havePath = false;
            }
            continue;
        }
        if (skipEntry) {
            if (strstr(line, "\"sha256\"") != nullptr) {
                skipEntry = false;
            }
            continue;
        }
        if (havePath && jsonUint(line, "\"size\"", &size)) {
            haveSize = true;
            continue;
        }
        if (!havePath || !jsonQuoted(line, "\"sha256\"", sha, sizeof(sha), &truncated)) {
            continue;
        }
        if (truncated || strlen(sha) != TT_OTA_SHA_HEX || !haveSize || size > TT_OTA_FW_MAX) {
            LOG_E("OTA: bad file entry %s", entryPath);
            file.close();
            return false;
        }
        TTOtaEntry entry = {};
        strncpy(entry.remote, entryPath, sizeof(entry.remote) - 1);
        strncpy(entry.sha, sha, sizeof(entry.sha) - 1);
        entry.size = size;
        entry.firmware = isFirmwarePath(entryPath);
        if (!entry.firmware && strstr(entryPath, "/res/") == nullptr) {
            LOG_E("OTA: unknown path %s", entryPath);
            file.close();
            return false;
        }
        havePath = false;
        if (fn != nullptr && !fn(&entry, ctx)) {
            file.close();
            return false;
        }
    }
    if (havePath || !sawVersion) {
        LOG_E("OTA: manifest incomplete");
    } else {
        ok = true;
    }
    file.close();
    return ok;
}

bool downloadManifest(TTOtaDoc* doc) {
    memset(doc, 0, sizeof(*doc));
    TTHttpsRequest request = {};
    request.url = TT_OTA_MANIFEST_URL;
    request.bodyMax = TT_OTA_MANIFEST_MAX;
    TTHttpsResult result = {};
    LOG_I("OTA: GET %s heap=%u", TT_OTA_MANIFEST_URL, (unsigned)ESP.getFreeHeap());
    if (!tt_https_exchange_file(&request, TT_OTA_REMOTE_MANIFEST, &result)) {
        LOG_E("OTA: manifest get failed");
        return false;
    }
    if (result.status != 200 || result.bodyLen == 0) {
        LOG_E("OTA: manifest status=%d len=%u", result.status, (unsigned)result.bodyLen);
        tt_file_remove(TT_OTA_REMOTE_MANIFEST);
        return false;
    }
    doc->bodyOffset = result.bodyOffset;
    doc->bodyLen = result.bodyLen;
    if (!readManifest(TT_OTA_REMOTE_MANIFEST, doc->bodyOffset, doc->bodyLen, doc, nullptr, nullptr)) {
        tt_file_remove(TT_OTA_REMOTE_MANIFEST);
        return false;
    }
    LOG_I("OTA: manifest version=%s notes=%s", doc->version, doc->notes);
    return true;
}

bool copyRange(const char* src, size_t offset, size_t len, const char* dst) {
    File in = tt_file_open(src, "r");
    if (!in || !in.seek((uint32_t)offset)) {
        if (in) {
            in.close();
        }
        LOG_E("OTA: copy open %s failed", src);
        return false;
    }
    if (!ensureParent(dst)) {
        in.close();
        return false;
    }
    File out = tt_file_create(dst);
    if (!out) {
        in.close();
        return false;
    }
    uint8_t buf[256];
    size_t left = len;
    bool ok = true;
    while (left > 0) {
        size_t n = left > sizeof(buf) ? sizeof(buf) : left;
        size_t got = in.readBytes((char*)buf, n);
        if (got != n || out.write(buf, got) != got) {
            ok = false;
            break;
        }
        left -= got;
    }
    in.close();
    out.close();
    if (!ok) {
        LOG_E("OTA: copy %s failed", dst);
        tt_file_remove(dst);
    }
    return ok;
}

bool writeText(const char* path, const char* text) {
    File file = tt_file_create(path);
    if (!file) {
        return false;
    }
    const size_t n = strlen(text);
    const bool ok = file.write((const uint8_t*)text, n) == n;
    file.close();
    if (!ok) {
        tt_file_remove(path);
    }
    return ok;
}

bool readText(const char* path, char* out, size_t outLen) {
    out[0] = '\0';
    File file = tt_file_open(path, "r");
    if (!file) {
        return false;
    }
    size_t n = 0;
    while (file.available() && n + 1 < outLen) {
        int c = file.read();
        if (c < 0) {
            break;
        }
        if (c == '\r' || c == '\n') {
            break;
        }
        out[n++] = (char)c;
    }
    file.close();
    out[n] = '\0';
    return n > 0;
}

void discardPending() {
    tt_file_remove(TT_OTA_DELETE_LIST);
    tt_file_remove(TT_OTA_STAGED_MANIFEST);
    tt_file_remove(TT_OTA_PENDING_VER);
}

struct TTOtaFind {
    const char* localPath;
    bool found;
};

bool onFindEntry(const TTOtaEntry* entry, void* ctx) {
    TTOtaFind* find = static_cast<TTOtaFind*>(ctx);
    if (entry->firmware) {
        return true;
    }
    char local[TT_FILE_FULL_PATH_MAX];
    if (!resLocalPath(entry->remote, local, sizeof(local))) {
        return true;
    }
    if (strcmp(local, find->localPath) == 0) {
        find->found = true;
    }
    return true;
}

bool manifestLists(size_t offset, size_t bodyLen, const char* localPath, bool* found) {
    TTOtaFind find = {};
    find.localPath = localPath;
    if (!readManifest(TT_OTA_REMOTE_MANIFEST, offset, bodyLen, nullptr, onFindEntry, &find)) {
        return false;
    }
    *found = find.found;
    return true;
}

bool writeExtras(const char* dirPath, size_t offset, size_t bodyLen, File& out) {
    File dir = LittleFS.open(dirPath);
    if (!dir || !dir.isDirectory()) {
        LOG_E("OTA: open dir %s failed", dirPath);
        return false;
    }
    File child = dir.openNextFile();
    while (child) {
        const char* path = child.path();
        const bool isDir = child.isDirectory();
        char stored[TT_FILE_FULL_PATH_MAX];
        if (path == nullptr || strlen(path) >= sizeof(stored)) {
            LOG_E("OTA: dir entry too long");
            child.close();
            dir.close();
            return false;
        }
        memcpy(stored, path, strlen(path) + 1);
        child.close();
        if (isDir) {
            if (!writeExtras(stored, offset, bodyLen, out)) {
                dir.close();
                return false;
            }
        } else if (strcmp(stored, TT_OTA_LOCAL_MANIFEST) != 0) {
            bool listed = false;
            if (!manifestLists(offset, bodyLen, stored, &listed)) {
                dir.close();
                return false;
            }
            if (listed) {
                child = dir.openNextFile();
                continue;
            }
            LOG_I("OTA: pending delete %s", stored);
            const size_t len = strlen(stored);
            if (out.write((const uint8_t*)stored, len) != len || out.write((const uint8_t*)"\n", 1) != 1) {
                dir.close();
                return false;
            }
        }
        child = dir.openNextFile();
    }
    dir.close();
    return true;
}

bool buildDeleteList(size_t offset, size_t bodyLen) {
    File out = tt_file_create(TT_OTA_DELETE_LIST);
    if (!out) {
        return false;
    }
    bool ok = true;
    if (LittleFS.exists(TT_FS_RES_DIR)) {
        ok = writeExtras(TT_FS_RES_DIR, offset, bodyLen, out);
    }
    out.close();
    if (!ok) {
        tt_file_remove(TT_OTA_DELETE_LIST);
    }
    return ok;
}

bool removeResFile(const char* path) {
    const size_t prefix = strlen(TT_FS_RES_DIR) + 1;
    if (strncmp(path, TT_FS_RES_DIR "/", prefix) != 0 || strcmp(path, TT_OTA_LOCAL_MANIFEST) == 0) {
        LOG_E("OTA: refuse delete %s", path);
        return false;
    }
    if (!tt_file_exists(path)) {
        return true;
    }
    LittleFS.remove(path);
    if (tt_file_exists(path)) {
        LOG_E("OTA: delete failed %s", path);
        return false;
    }
    LOG_I("OTA: deleted %s", path);
    return true;
}

bool applyDeleteList() {
    File file = tt_file_open(TT_OTA_DELETE_LIST, "r");
    if (!file) {
        return false;
    }
    char line[TT_FILE_FULL_PATH_MAX];
    size_t n = 0;
    bool ok = true;
    bool skip = false;
    while (true) {
        const int c = file.read();
        if (c < 0 || c == '\n') {
            if (!skip && n > 0) {
                line[n] = '\0';
                if (!removeResFile(line)) {
                    ok = false;
                }
            }
            n = 0;
            skip = false;
            if (c < 0) {
                break;
            }
            continue;
        }
        if (c == '\r' || skip) {
            continue;
        }
        if (n + 1 >= sizeof(line)) {
            LOG_E("OTA: delete path too long");
            ok = false;
            skip = true;
            n = 0;
            continue;
        }
        line[n++] = (char)c;
    }
    file.close();
    return ok;
}

struct TTOtaScan {
    size_t replaceCount;
    size_t firmwareCount;
    TTOtaEntry firmware;
};

bool onScanEntry(const TTOtaEntry* entry, void* ctx) {
    TTOtaScan* scan = static_cast<TTOtaScan*>(ctx);
    if (entry->firmware) {
        scan->firmwareCount++;
        if (scan->firmwareCount == 1) {
            scan->firmware = *entry;
        }
        return true;
    }
    char local[TT_FILE_FULL_PATH_MAX];
    if (!resLocalPath(entry->remote, local, sizeof(local))) {
        LOG_E("OTA: bad res path %s", entry->remote);
        return false;
    }
    char hex[TT_OTA_SHA_HEX + 1];
    if (!sha256File(local, hex, sizeof(hex)) || strcasecmp(hex, entry->sha) != 0) {
        scan->replaceCount++;
        LOG_I("OTA: need replace %s", local);
    }
    return true;
}

bool downloadRes(const TTOtaEntry& entry, size_t index, size_t total);

struct TTOtaFetch {
    size_t total;
    size_t done;
    bool failed;
};

bool onFetchEntry(const TTOtaEntry* entry, void* ctx) {
    if (entry->firmware) {
        return true;
    }
    char local[TT_FILE_FULL_PATH_MAX];
    if (!resLocalPath(entry->remote, local, sizeof(local))) {
        return false;
    }
    char hex[TT_OTA_SHA_HEX + 1];
    if (sha256File(local, hex, sizeof(hex)) && strcasecmp(hex, entry->sha) == 0) {
        return true;
    }
    TTOtaFetch* fetch = static_cast<TTOtaFetch*>(ctx);
    fetch->done++;
    if (!downloadRes(*entry, fetch->done, fetch->total)) {
        fetch->failed = true;
        return false;
    }
    return true;
}

bool downloadRes(const TTOtaEntry& entry, size_t index, size_t total) {
    char local[TT_FILE_FULL_PATH_MAX];
    char url[TT_OTA_URL_MAX];
    if (!resLocalPath(entry.remote, local, sizeof(local))) {
        return false;
    }
    if (snprintf(url, sizeof(url), "%s%s", TT_OTA_URL_BASE, entry.remote) >= (int)sizeof(url)) {
        LOG_E("OTA: url too long %s", entry.remote);
        return false;
    }
    LOG_I("OTA: GET %s -> %s", url, local);
    TTHttpsResult result = {};
    TTOtaWriteCtx writer = {};
    writer.path = local;
    writer.result = &result;
    writer.expect = entry.size;
    writer.fileIndex = index;
    writer.fileTotal = total;
    writer.lastPercent = 255;
    reportWriteProgress(&writer);
    const bool httpOk = tt_https_get_body(url, entry.size, otaWriteBody, &writer, &result);
    if (writer.file) {
        writer.file.flush();
        writer.file.close();
    }
    const bool hashOk = httpOk && result.status == 200 && result.bodyLen == entry.size
        && finishHash(&writer, entry.sha);
    if (!hashOk) {
        dropHash(&writer);
        LOG_E("OTA: replace failed %s status=%d body=%u",
              local, result.status, (unsigned)result.bodyLen);
        tt_file_remove(local);
        return false;
    }
    LOG_I("OTA: replaced %s", local);
    return true;
}

bool flashFirmware(const TTOtaEntry& entry) {
    if (entry.size == 0 || entry.size > TT_OTA_FW_MAX) {
        LOG_E("OTA: firmware size %u", (unsigned)entry.size);
        return false;
    }
    char url[TT_OTA_URL_MAX];
    if (snprintf(url, sizeof(url), "%s%s", TT_OTA_URL_BASE, entry.remote) >= (int)sizeof(url)) {
        LOG_E("OTA: firmware url too long");
        return false;
    }
    if (Update.isRunning()) {
        Update.abort();
    }
    if (!Update.begin(entry.size)) {
        LOG_E("OTA: begin failed %s", Update.errorString());
        return false;
    }
    LOG_I("OTA: GET firmware %s size=%u", url, (unsigned)entry.size);
    TTHttpsResult result = {};
    TTOtaWriteCtx writer = {};
    writer.useUpdate = true;
    writer.result = &result;
    writer.expect = entry.size;
    writer.lastPercent = 255;
    reportWriteProgress(&writer);
    const bool httpOk = tt_https_get_body(url, entry.size, otaWriteBody, &writer, &result);
    const bool hashOk = httpOk && result.status == 200 && result.bodyLen == entry.size
        && finishHash(&writer, entry.sha);
    if (!hashOk) {
        dropHash(&writer);
        LOG_E("OTA: firmware verify failed status=%d body=%u err=%s",
              result.status, (unsigned)result.bodyLen, Update.errorString());
        Update.abort();
        return false;
    }
    if (!Update.end(true)) {
        LOG_E("OTA: end failed %s", Update.errorString());
        return false;
    }
    LOG_I("OTA: firmware stored");
    return true;
}

}  // namespace

void TTOtaService::readLocalVersion(char* out, size_t outLen) {
    if (out == nullptr || outLen == 0) {
        return;
    }
    out[0] = '\0';
    if (!tt_file_exists(TT_OTA_LOCAL_MANIFEST)) {
        return;
    }
    File file = tt_file_open(TT_OTA_LOCAL_MANIFEST, "r");
    if (!file) {
        return;
    }
    char line[TT_OTA_LINE_MAX];
    const size_t endPos = file.size();
    while (readManifestLine(file, endPos, line, sizeof(line))) {
        if (jsonQuoted(line, "\"version\"", out, outLen, nullptr)) {
            break;
        }
    }
    file.close();
}

void TTOtaService::readLocalNotes(char* out, size_t outLen) {
    if (out == nullptr || outLen == 0) {
        return;
    }
    out[0] = '\0';
    if (!tt_file_exists(TT_OTA_LOCAL_MANIFEST)) {
        return;
    }
    File file = tt_file_open(TT_OTA_LOCAL_MANIFEST, "r");
    if (!file) {
        return;
    }
    char line[TT_OTA_LINE_MAX];
    const size_t endPos = file.size();
    while (readManifestLine(file, endPos, line, sizeof(line))) {
        if (strstr(line, "\"notes\"") != nullptr && jsonQuoted(line, "\"notes\"", out, outLen, nullptr)) {
            break;
        }
    }
    file.close();
}

void TTOtaService::checkAsync() {
    TTInstanceOf<TTWiFiTask>().runWithRadio("ota", [this]() {
        checkNow();
    }, []() {
        postOta(TT_OTA_PHASE_FAILED, nullptr, "无法连接 Wi-Fi");
    });
}

void TTOtaService::upgradeAsync() {
    TTInstanceOf<TTWiFiTask>().runWithRadio("ota-up", [this]() {
        upgradeNow();
    }, []() {
        postOta(TT_OTA_PHASE_FAILED, nullptr, "无法连接 Wi-Fi");
    });
}

void TTOtaService::checkNow() {
    TTOtaDoc doc = {};
    if (!downloadManifest(&doc)) {
        postOta(TT_OTA_PHASE_FAILED, nullptr, "清单下载失败");
        return;
    }
    char local[TT_OTA_VER_MAX];
    readLocalVersion(local, sizeof(local));
    LOG_I("OTA: compare remote=%s local=%s fw=%s", doc.version, local, TT_FW_VERSION);
    const bool sameFw = strcmp(doc.version, TT_FW_VERSION) == 0;
    const bool sameManifest = local[0] != '\0' && strcmp(doc.version, local) == 0;
    if (sameFw && sameManifest) {
        postOta(TT_OTA_PHASE_UP_TO_DATE, doc.version, "已是最新");
        tt_file_remove(TT_OTA_REMOTE_MANIFEST);
        return;
    }
    char message[TT_OTA_MSG_MAX];
    if (!sameFw) {
        snprintf(message, sizeof(message), "发现新版本 %s", doc.version);
    } else {
        snprintf(message, sizeof(message), "需要同步资源");
    }
    if (doc.notes[0] != '\0' && strlen(doc.notes) <= 36) {
        const size_t used = strlen(message);
        if (used + 2 < sizeof(message)) {
            message[used] = '\n';
            strncpy(message + used + 1, doc.notes, sizeof(message) - used - 2);
            message[sizeof(message) - 1] = '\0';
        }
    }
    postOta(TT_OTA_PHASE_AVAILABLE, doc.version, message);
    tt_file_remove(TT_OTA_REMOTE_MANIFEST);
}

void TTOtaService::upgradeNow() {
    postOta(TT_OTA_PHASE_PROGRESS, nullptr, "正在下载清单");
    TTOtaDoc doc = {};
    if (!downloadManifest(&doc)) {
        postOta(TT_OTA_PHASE_FAILED, nullptr, "清单下载失败");
        return;
    }
    char local[TT_OTA_VER_MAX];
    readLocalVersion(local, sizeof(local));
    const bool sameFw = strcmp(doc.version, TT_FW_VERSION) == 0;
    const bool sameManifest = local[0] != '\0' && strcmp(doc.version, local) == 0;
    if (sameFw && sameManifest) {
        postOta(TT_OTA_PHASE_UP_TO_DATE, doc.version, "已是最新");
        tt_file_remove(TT_OTA_REMOTE_MANIFEST);
        return;
    }

    postOta(TT_OTA_PHASE_PROGRESS, doc.version, "正在比对资源");
    TTOtaScan scan = {};
    if (!readManifest(TT_OTA_REMOTE_MANIFEST, doc.bodyOffset, doc.bodyLen, nullptr, onScanEntry, &scan)
        || scan.firmwareCount != 1) {
        LOG_E("OTA: firmware entries=%u", (unsigned)scan.firmwareCount);
        postOta(TT_OTA_PHASE_FAILED, nullptr, scan.firmwareCount == 1 ? "资源比对失败" : "固件清单缺失");
        tt_file_remove(TT_OTA_REMOTE_MANIFEST);
        return;
    }
    LOG_I("OTA: replace=%u", (unsigned)scan.replaceCount);
    TTOtaFetch fetch = {};
    fetch.total = scan.replaceCount;
    if (scan.replaceCount > 0
        && (!readManifest(TT_OTA_REMOTE_MANIFEST, doc.bodyOffset, doc.bodyLen, nullptr, onFetchEntry, &fetch)
            || fetch.failed)) {
        postOta(TT_OTA_PHASE_FAILED, nullptr, "资源写入失败");
        tt_file_remove(TT_OTA_REMOTE_MANIFEST);
        return;
    }
    if (!buildDeleteList(doc.bodyOffset, doc.bodyLen)) {
        postOta(TT_OTA_PHASE_FAILED, nullptr, "删除列表生成失败");
        tt_file_remove(TT_OTA_REMOTE_MANIFEST);
        return;
    }

    if (!copyRange(TT_OTA_REMOTE_MANIFEST, doc.bodyOffset, doc.bodyLen, TT_OTA_STAGED_MANIFEST)) {
        postOta(TT_OTA_PHASE_FAILED, nullptr, "清单保存失败");
        discardPending();
        tt_file_remove(TT_OTA_REMOTE_MANIFEST);
        return;
    }
    char pending[TT_OTA_VER_MAX + 2];
    snprintf(pending, sizeof(pending), "%s\n", doc.version);
    if (!writeText(TT_OTA_PENDING_VER, pending)) {
        postOta(TT_OTA_PHASE_FAILED, nullptr, "升级状态保存失败");
        discardPending();
        tt_file_remove(TT_OTA_REMOTE_MANIFEST);
        return;
    }
    const TTOtaEntry firmwareCopy = scan.firmware;
    tt_file_remove(TT_OTA_REMOTE_MANIFEST);

    if (sameFw) {
        LOG_I("OTA: firmware already %s, apply deletes", TT_FW_VERSION);
        File staged = tt_file_open(TT_OTA_STAGED_MANIFEST, "r");
        if (!staged) {
            postOta(TT_OTA_PHASE_FAILED, nullptr, "清单保存失败");
            return;
        }
        const size_t stagedLen = staged.size();
        staged.close();
        const bool deleted = applyDeleteList();
        const bool copied = stagedLen > 0
            && copyRange(TT_OTA_STAGED_MANIFEST, 0, stagedLen, TT_OTA_LOCAL_MANIFEST);
        if (!deleted || !copied) {
            postOta(TT_OTA_PHASE_FAILED, nullptr, "重启后继续清理");
            return;
        }
        discardPending();
        postOta(TT_OTA_PHASE_UP_TO_DATE, nullptr, "资源已同步");
        return;
    }

    if (!flashFirmware(firmwareCopy)) {
        discardPending();
        postOta(TT_OTA_PHASE_FAILED, nullptr, "固件升级失败");
        return;
    }
    postOta(TT_OTA_PHASE_REBOOT, nullptr, "正在重启");
    delay(300);
    ESP.restart();
}

void TTOtaService::applyPending() {
    if (!tt_file_exists(TT_OTA_PENDING_VER)) {
        if (tt_file_exists(TT_OTA_STAGED_MANIFEST) || tt_file_exists(TT_OTA_DELETE_LIST)) {
            LOG_W("OTA: drop incomplete pending files");
            discardPending();
        }
        return;
    }
    char version[TT_OTA_VER_MAX];
    if (!readText(TT_OTA_PENDING_VER, version, sizeof(version)) || strcmp(version, TT_FW_VERSION) != 0) {
        LOG_W("OTA: pending %s does not match fw %s", version, TT_FW_VERSION);
        discardPending();
        return;
    }
    if (!tt_file_exists(TT_OTA_STAGED_MANIFEST) || !tt_file_exists(TT_OTA_DELETE_LIST)) {
        LOG_W("OTA: pending bundle incomplete");
        discardPending();
        return;
    }
    File staged = tt_file_open(TT_OTA_STAGED_MANIFEST, "r");
    if (!staged) {
        discardPending();
        return;
    }
    const size_t stagedLen = staged.size();
    staged.close();
    LOG_I("OTA: apply pending %s", version);
    const bool deleted = applyDeleteList();
    const bool copied = copyRange(TT_OTA_STAGED_MANIFEST, 0, stagedLen, TT_OTA_LOCAL_MANIFEST);
    if (!deleted || !copied) {
        LOG_E("OTA: pending apply incomplete");
        return;
    }
    discardPending();
    LOG_I("OTA: pending applied");
}
