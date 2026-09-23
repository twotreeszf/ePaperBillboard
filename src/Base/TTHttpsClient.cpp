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

bool writeResponseToFile(TTTlsSession* tls, File& file, size_t* written, size_t limit) {
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
            if (*written > limit + 1024) {
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

bool writeResponseToSink(TTTlsSession* tls, const TTHttpsRequest* request, TTHttpsResult* out, size_t limit) {
    uint8_t chunk[TT_HTTPS_CHUNK_MAX];
    uint8_t header[1024];
    size_t headerLen = 0;
    bool headerDone = false;
    out->status = 0;
    out->bodyOffset = 0;
    out->bodyLen = 0;

    while (true) {
        const int ret = tt_tls_read(tls, chunk, sizeof(chunk));
        if (ret > 0) {
            const uint8_t* data = chunk;
            size_t len = (size_t)ret;
            if (!headerDone) {
                size_t used = 0;
                while (used < len && headerLen + 1 < sizeof(header)) {
                    header[headerLen++] = data[used++];
                    if (headerLen >= 4 && memcmp(header + headerLen - 4, "\r\n\r\n", 4) == 0) {
                        headerDone = true;
                        header[headerLen] = '\0';
                        const char* space = strchr((const char*)header, ' ');
                        if (space != nullptr) {
                            out->status = atoi(space + 1);
                        }
                        break;
                    }
                }
                if (!headerDone) {
                    if (headerLen + 1 >= sizeof(header)) {
                        LOG_E("HTTPS: header too large");
                        return false;
                    }
                    continue;
                }
                data += used;
                len -= used;
            }
            if (len == 0) {
                continue;
            }
            if (out->bodyLen + len > limit) {
                LOG_E("HTTPS: body too large %u", (unsigned)(out->bodyLen + len));
                return false;
            }
            if (request->bodyWriter != nullptr
                && !request->bodyWriter(request->bodyWriterCtx, data, len)) {
                LOG_E("HTTPS: body writer failed at %u", (unsigned)out->bodyLen);
                return false;
            }
            out->bodyLen += len;
            continue;
        }
        if (ret == 0) {
            return headerDone && out->status > 0;
        }
        LOG_E("HTTPS: tls read failed written=%u", (unsigned)out->bodyLen);
        return false;
    }
}

}  // namespace

bool tt_https_get_file(const char* url, const char* tmpPath, TTHttpsResult* out) {
    TTHttpsRequest request = {};
    request.url = url;
    return tt_https_exchange_file(&request, tmpPath, out);
}

bool tt_https_get_body(const char* url, size_t bodyMax, TTHttpsBodyFn writer, void* ctx, TTHttpsResult* out) {
    TTHttpsRequest request = {};
    request.url = url;
    request.bodyMax = bodyMax;
    request.bodyWriter = writer;
    request.bodyWriterCtx = ctx;
    return tt_https_exchange_file(&request, nullptr, out);
}

bool tt_https_exchange_file(const TTHttpsRequest* request, const char* tmpPath, TTHttpsResult* out) {
    if (request == nullptr || request->url == nullptr || out == nullptr) {
        return false;
    }
    if (request->bodyWriter == nullptr && tmpPath == nullptr) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    const char* url = request->url;
    const char* method = (request->method != nullptr && request->method[0] != '\0')
        ? request->method : "GET";
    const size_t limit = request->bodyMax > 0 ? request->bodyMax : (size_t)TT_HTTPS_BODY_MAX;

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

    if (tmpPath != nullptr) {
        tt_file_remove(tmpPath);
    }

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
        const size_t contentLen = request->body != nullptr ? strlen(request->body) : 0;
        int reqLen = snprintf(req, sizeof(req),
                              "%s %s HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n",
                              method, path, host);
        if (reqLen > 0 && reqLen < (int)sizeof(req)
            && request->extraHeaders != nullptr && request->extraHeaders[0] != '\0') {
            const int extra = snprintf(req + reqLen, sizeof(req) - (size_t)reqLen, "%s",
                                       request->extraHeaders);
            if (extra < 0 || reqLen + extra >= (int)sizeof(req)) {
                reqLen = (int)sizeof(req);
            } else {
                reqLen += extra;
            }
        }
        if (reqLen > 0 && reqLen < (int)sizeof(req) && contentLen > 0) {
            const char* type = request->contentType != nullptr ? request->contentType : "application/xml";
            const int extra = snprintf(req + reqLen, sizeof(req) - (size_t)reqLen,
                                       "Content-Type: %s\r\nContent-Length: %u\r\n",
                                       type, (unsigned)contentLen);
            if (extra < 0 || reqLen + extra >= (int)sizeof(req)) {
                reqLen = (int)sizeof(req);
            } else {
                reqLen += extra;
            }
        }
        if (reqLen > 0 && reqLen + 2 < (int)sizeof(req)) {
            req[reqLen++] = '\r';
            req[reqLen++] = '\n';
            req[reqLen] = '\0';
        }
        if (reqLen <= 0 || reqLen >= (int)sizeof(req)) {
            LOG_E("HTTPS: request too long");
            break;
        }
        if (!tlsWriteAll(&tls, (const uint8_t*)req, (size_t)reqLen)) {
            break;
        }
        if (contentLen > 0 && !tlsWriteAll(&tls, (const uint8_t*)request->body, contentLen)) {
            break;
        }

        if (request->bodyWriter != nullptr) {
            ok = writeResponseToSink(&tls, request, out, limit);
            if (!ok) {
                break;
            }
            LOG_I("HTTPS: stream status=%d body=%u", out->status, (unsigned)out->bodyLen);
        } else {
            File file = tt_file_create(tmpPath);
            if (!file) {
                break;
            }
            size_t written = 0;
            ok = writeResponseToFile(&tls, file, &written, limit);
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
            if (out->bodyLen > limit) {
                LOG_E("HTTPS: body too large %u", (unsigned)out->bodyLen);
                ok = false;
                break;
            }
            LOG_I("HTTPS: status=%d body_off=%u body_len=%u",
                  out->status, (unsigned)out->bodyOffset, (unsigned)out->bodyLen);
        }
    } while (false);

    tt_tls_close(&tls);
    ctxFree(&ctx);
    if (!ok && tmpPath != nullptr) {
        tt_file_remove(tmpPath);
    }
    return ok;
}
