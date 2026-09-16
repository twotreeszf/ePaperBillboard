#include "TTHttpsClient.h"
#include "TTTlsClient.h"
#include "TTFile.h"
#include "Logger.h"
#include <WiFi.h>
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <fcntl.h>
#include <lwip/sockets.h>
#include <cstring>
#include <cstdlib>

namespace {

struct TTHttpsCtx {
    int socket;
    unsigned long handshakeTimeout;
    unsigned long ioTimeout;
};

void logHeap(const char* tag) {
    LOG_I("HTTPS: %s heap=%u largest=%u",
          tag,
          (unsigned)ESP.getFreeHeap(),
          (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
}

void ctxInit(TTHttpsCtx* ctx) {
    ctx->socket = -1;
    ctx->handshakeTimeout = TT_HTTPS_HANDSHAKE_MS;
    ctx->ioTimeout = TT_HTTPS_IO_TIMEOUT_MS;
}

void ctxFree(TTHttpsCtx* ctx) {
    if (ctx->socket >= 0) {
        lwip_close(ctx->socket);
        ctx->socket = -1;
    }
}

bool parseUrl(const char* url, char* host, size_t hostMax, uint16_t* port, char* path, size_t pathMax) {
    if (strncmp(url, "https://", 8) != 0) {
        LOG_E("HTTPS: only https:// supported");
        return false;
    }
    const char* start = url + 8;
    *port = TT_HTTPS_PORT;
    const char* end = start;
    while (*end != '\0' && *end != '/' && *end != ':') {
        end++;
    }
    const size_t hostLen = (size_t)(end - start);
    if (hostLen == 0 || hostLen >= hostMax) {
        return false;
    }
    memcpy(host, start, hostLen);
    host[hostLen] = '\0';
    if (*end == ':') {
        char* next = nullptr;
        *port = (uint16_t)strtoul(end + 1, &next, 10);
        end = (next != nullptr && *next != '\0') ? next : "";
    }
    if (*end == '\0') {
        strncpy(path, "/", pathMax - 1);
    } else {
        strncpy(path, end, pathMax - 1);
    }
    path[pathMax - 1] = '\0';
    return true;
}

bool tcpConnect(TTHttpsCtx* ctx, const IPAddress& ip, uint16_t port) {
    ctx->socket = lwip_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (ctx->socket < 0) {
        LOG_E("HTTPS: socket failed");
        return false;
    }
    fcntl(ctx->socket, F_SETFL, fcntl(ctx->socket, F_GETFL, 0) | O_NONBLOCK);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = ip;
    addr.sin_port = htons(port);

    const int timeout = (int)ctx->ioTimeout;
    struct timeval tv;
    tv.tv_sec = timeout / 1000;
    tv.tv_usec = (timeout % 1000) * 1000;

    int res = lwip_connect(ctx->socket, (struct sockaddr*)&addr, sizeof(addr));
    if (res < 0 && errno != EINPROGRESS) {
        LOG_E("HTTPS: connect errno=%d", errno);
        return false;
    }

    fd_set fdset;
    FD_ZERO(&fdset);
    FD_SET(ctx->socket, &fdset);
    res = select(ctx->socket + 1, nullptr, &fdset, nullptr, &tv);
    if (res <= 0) {
        LOG_E("HTTPS: TCP select %d", res);
        return false;
    }
    int sockerr = 0;
    socklen_t len = sizeof(sockerr);
    if (getsockopt(ctx->socket, SOL_SOCKET, SO_ERROR, &sockerr, &len) < 0 || sockerr != 0) {
        LOG_E("HTTPS: TCP sockerr=%d", sockerr);
        return false;
    }

    int enable = 1;
    lwip_setsockopt(ctx->socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    lwip_setsockopt(ctx->socket, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    lwip_setsockopt(ctx->socket, IPPROTO_TCP, TCP_NODELAY, &enable, sizeof(enable));
    return true;
}

bool tlsWriteAll(TTTlsSession* tls, const uint8_t* data, size_t len) {
    const int ret = tt_tls_write(tls, data, len);
    if (ret < 0 || (size_t)ret != len) {
        LOG_E("HTTPS: tls write failed");
        return false;
    }
    return true;
}

bool writeResponseToFile(TTTlsSession* tls, File& file, size_t* written) {
    uint8_t chunk[TT_HTTPS_CHUNK_MAX];
    *written = 0;
    while (true) {
        const int ret = tt_tls_read(tls, chunk, sizeof(chunk));
        if (ret > 0) {
            if (file.write(chunk, (size_t)ret) != (size_t)ret) {
                LOG_E("HTTPS: tmp write failed at %u", (unsigned)*written);
                return false;
            }
            *written += (size_t)ret;
            if (*written > TT_HTTPS_BODY_MAX + 1024) {
                LOG_E("HTTPS: response too large %u", (unsigned)*written);
                return false;
            }
            continue;
        }
        if (ret == 0) {
            return true;
        }
        LOG_E("HTTPS: tls read failed written=%u", (unsigned)*written);
        return false;
    }
}

bool parseResponseFile(File& file, TTHttpsResult* out) {
    file.seek(0);
    char line[128];
    size_t lineLen = 0;
    size_t pos = 0;
    out->status = 0;
    out->bodyOffset = 0;
    out->bodyLen = 0;

    while (file.available()) {
        const int c = file.read();
        if (c < 0) {
            break;
        }
        pos++;
        if (c == '\r') {
            continue;
        }
        if (c == '\n') {
            line[lineLen] = '\0';
            if (out->status == 0) {
                const char* p = strchr(line, ' ');
                if (p != nullptr) {
                    out->status = atoi(p + 1);
                }
            }
            if (lineLen == 0) {
                out->bodyOffset = pos;
                const size_t total = file.size();
                if (total >= out->bodyOffset) {
                    out->bodyLen = total - out->bodyOffset;
                }
                return out->status > 0;
            }
            lineLen = 0;
            continue;
        }
        if (lineLen + 1 < sizeof(line)) {
            line[lineLen++] = (char)c;
        }
    }
    LOG_E("HTTPS: header parse failed");
    return false;
}

}  // namespace

bool tt_https_get_file(const char* url, const char* tmpPath, TTHttpsResult* out) {
    if (url == nullptr || tmpPath == nullptr || out == nullptr) {
        return false;
    }
    memset(out, 0, sizeof(*out));

    char host[TT_HTTPS_HOST_MAX];
    char path[TT_HTTPS_PATH_MAX];
    uint16_t port = TT_HTTPS_PORT;
    if (!parseUrl(url, host, sizeof(host), &port, path, sizeof(path))) {
        LOG_E("HTTPS: bad url");
        return false;
    }

    IPAddress ip;
    const uint32_t dnsStart = millis();
    if (!WiFi.hostByName(host, ip)) {
        LOG_E("HTTPS: DNS fail host=%s", host);
        return false;
    }
    LOG_I("HTTPS: DNS %s -> %s elapsed=%u",
          host, ip.toString().c_str(), (unsigned)(millis() - dnsStart));

    tt_file_remove(tmpPath);

    TTHttpsCtx ctx;
    ctxInit(&ctx);
    TTTlsSession tls;
    memset(&tls, 0, sizeof(tls));
    tls.socket = -1;

    bool ok = false;
    do {
        const uint32_t tcpStart = millis();
        if (!tcpConnect(&ctx, ip, port)) {
            break;
        }
        LOG_I("HTTPS: TCP %s:%u ok elapsed=%u",
              ip.toString().c_str(), (unsigned)port, (unsigned)(millis() - tcpStart));
        logHeap("before tls handshake");
        if (!tt_tls_handshake(&tls, ctx.socket, host, (uint32_t)ctx.handshakeTimeout)) {
            LOG_E("HTTPS: handshake failed");
            break;
        }
        logHeap("after tls handshake");
        tt_tls_set_timeout(&tls, (uint32_t)ctx.ioTimeout);

        char req[TT_HTTPS_REQ_MAX];
        const int reqLen = snprintf(req, sizeof(req),
                                    "GET %s HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n\r\n",
                                    path, host);
        if (reqLen <= 0 || reqLen >= (int)sizeof(req)) {
            LOG_E("HTTPS: request too long");
            break;
        }
        if (!tlsWriteAll(&tls, (const uint8_t*)req, (size_t)reqLen)) {
            break;
        }

        File file = tt_file_create(tmpPath);
        if (!file) {
            break;
        }
        size_t written = 0;
        ok = writeResponseToFile(&tls, file, &written);
        file.flush();
        if (!ok) {
            file.close();
            break;
        }
        LOG_I("HTTPS: tmp %s bytes=%u heap=%u largest=%u",
              tmpPath, (unsigned)written,
              (unsigned)ESP.getFreeHeap(),
              (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
        ok = parseResponseFile(file, out);
        file.close();
        if (!ok) {
            break;
        }
        if (out->bodyLen > TT_HTTPS_BODY_MAX) {
            LOG_E("HTTPS: body too large %u", (unsigned)out->bodyLen);
            ok = false;
            break;
        }
        LOG_I("HTTPS: status=%d body_off=%u body_len=%u",
              out->status, (unsigned)out->bodyOffset, (unsigned)out->bodyLen);
    } while (false);

    tt_tls_close(&tls);
    ctxFree(&ctx);
    if (!ok) {
        tt_file_remove(tmpPath);
    }
    return ok;
}
