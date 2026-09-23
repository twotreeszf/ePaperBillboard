#pragma once

#include "TTFile.h"
#include <stddef.h>
#include <stdint.h>

#define TT_HTTPS_PORT              443
#define TT_HTTPS_CHUNK_MAX         512
#define TT_HTTPS_HOST_MAX          64
#define TT_HTTPS_PATH_MAX          768
#define TT_HTTPS_REQ_MAX           896
#define TT_HTTPS_TMP_DIR           TT_FS_TMP_DIR
#define TT_HTTPS_TMP_RESP          TT_FS_TMP_DIR "/https_resp"
#define TT_HTTPS_HANDSHAKE_MS      15000
#define TT_HTTPS_IO_TIMEOUT_MS     15000
#define TT_HTTPS_BODY_MAX          16384

struct TTHttpsResult {
    int status;
    size_t bodyOffset;
    size_t bodyLen;
};

typedef bool (*TTHttpsBodyFn)(void* ctx, const uint8_t* data, size_t len);

struct TTHttpsRequest {
    const char* url;
    const char* method;
    const char* contentType;
    const char* body;
    const char* extraHeaders;
    size_t bodyMax;
    TTHttpsBodyFn bodyWriter;
    void* bodyWriterCtx;
};

bool tt_https_get_file(const char* url, const char* tmpPath, TTHttpsResult* out);
bool tt_https_get_body(const char* url, size_t bodyMax, TTHttpsBodyFn writer, void* ctx, TTHttpsResult* out);
bool tt_https_exchange_file(const TTHttpsRequest* request, const char* tmpPath, TTHttpsResult* out);
