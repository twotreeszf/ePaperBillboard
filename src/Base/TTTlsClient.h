#pragma once

#include "TTFile.h"
#include "TTTlsCrypto.h"

#include <stddef.h>
#include <stdint.h>

#define TT_TLS_CHUNK              512
#define TT_TLS_MAX_RECORD         18432
#define TT_TLS_MFL                1024
#define TT_TLS_RECORD_RAM         1100
#define TT_TLS_HS_BODY_MAX        320
#define TT_TLS_TMP_REC            TT_FS_TMP_DIR "/tls_rec"
#define TT_TLS_TMP_PLAIN          TT_FS_TMP_DIR "/tls_plain"
#define TT_TLS_VERSION            0x0303
#define TT_TLS_SUITE_ECDHE_RSA    0xC02F
#define TT_TLS_SUITE_ECDHE_ECDSA  0xC02B
#define TT_TLS_GROUP_X25519       0x001d

struct TTTlsSession {
    int socket;
    uint32_t deadline;
    uint8_t clientRandom[32];
    uint8_t serverRandom[32];
    uint8_t serverPub[TT_X25519_LEN];
    uint8_t clientKey[16];
    uint8_t serverKey[16];
    uint8_t clientIV[4];
    uint8_t serverIV[4];
    uint8_t master[48];
    uint64_t clientSeq;
    uint64_t serverSeq;
    uint16_t cipherSuite;
    bool sendEncrypted;
    bool recvEncrypted;
    bool sawCertReq;
    bool gotHelloDone;
    bool gotServerKey;
    bool gotFinished;
    TTSha256 hsHash;
    uint8_t leftover[TT_TLS_RECORD_RAM];
    size_t leftoverOff;
    size_t leftoverLen;
    uint8_t hsHdr[4];
    size_t hsHdrGot;
    uint8_t hsType;
    uint32_t hsBodyNeed;
    uint32_t hsBodyGot;
    uint8_t hsBody[TT_TLS_HS_BODY_MAX];
    bool hsStoreBody;
};

bool tt_tls_handshake(TTTlsSession* session, int socket, const char* sni, uint32_t timeoutMs);
void tt_tls_set_timeout(TTTlsSession* session, uint32_t timeoutMs);
int tt_tls_write(TTTlsSession* session, const uint8_t* data, size_t len);
int tt_tls_read(TTTlsSession* session, uint8_t* data, size_t len);
void tt_tls_close(TTTlsSession* session);
