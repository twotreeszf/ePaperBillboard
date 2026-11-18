#include "TTHttpFile.h"
#include "Logger.h"
#include "TTFile.h"

#include <Arduino.h>
#include <WiFi.h>
#include <strings.h>
#include <cstring>

namespace {

const char* contentTypeFor(const char* path) {
    const char* dot = strrchr(path, '.');
    if (dot == nullptr) {
        return "application/octet-stream";
    }
    if (strcasecmp(dot, ".html") == 0) {
        return "text/html; charset=utf-8";
    }
    if (strcasecmp(dot, ".json") == 0) {
        return "application/json";
    }
    if (strcasecmp(dot, ".css") == 0) {
        return "text/css";
    }
    if (strcasecmp(dot, ".js") == 0) {
        return "text/javascript";
    }
    if (strcasecmp(dot, ".bmp") == 0) {
        return "image/bmp";
    }
    return "application/octet-stream";
}

bool writeAll(WiFiClient& client, const uint8_t* data, size_t len, const char* path) {
    size_t sent = 0;
    int spins = 0;
    while (sent < len) {
        const int n = client.write(data + sent, len - sent);
        if (n > 0) {
            sent += (size_t)n;
            spins = 0;
            continue;
        }
        if (!client.connected() || ++spins > 40) {
            LOG_E("Http: send stalled %s", path);
            return false;
        }
        delay(10);
    }
    return true;
}

}

bool ttHttpWwwPath(const char* path) {
    if (path == nullptr) {
        return false;
    }
    const size_t len = strlen(path);
    const size_t root = strlen(TT_FS_WWW_DIR);
    if (len < root + 2 || len >= TT_FILE_FULL_PATH_MAX) {
        return false;
    }
    if (strncmp(path, TT_FS_WWW_DIR "/", root + 1) != 0 || strstr(path, "..") != nullptr) {
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        if (path[i] == '\\') {
            return false;
        }
    }
    return true;
}

bool ttHttpSendFile(WebServer& server, const char* path, const char* contentType) {
    if (path == nullptr) {
        return false;
    }
    File file = tt_file_open(path, "r");
    if (!file) {
        return false;
    }
    const size_t size = file.size();
    if (contentType == nullptr) {
        contentType = contentTypeFor(path);
    }
    server.setContentLength(size);
    server.send(200, contentType, "");
    WiFiClient client = server.client();
    uint8_t buf[TT_FILE_BUF_SIZE];
    bool ok = true;
    while (ok) {
        const int n = file.read(buf, sizeof(buf));
        if (n < 0) {
            ok = false;
            LOG_E("Http: read failed %s", path);
            break;
        }
        if (n == 0) {
            break;
        }
        if (!writeAll(client, buf, (size_t)n, path)) {
            ok = false;
            break;
        }
        yield();
    }
    file.close();
    if (ok) {
        LOG_I("Http: sent %s %u", path, (unsigned)size);
    }
    return true;
}
