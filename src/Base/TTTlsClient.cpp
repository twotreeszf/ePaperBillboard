#include "TTTlsClient.h"
#include "TTFile.h"
#include "Logger.h"
#include <Arduino.h>
#include <lwip/sockets.h>
#include <esp_system.h>
#include <string.h>

namespace {

uint16_t get16(const uint8_t* p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

void put16(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

void put24(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v >> 16);
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)v;
}

void put64(uint8_t* p, uint64_t v) {
    for (int i = 7; i >= 0; --i) {
        p[i] = (uint8_t)v;
        v >>= 8;
    }
}

uint32_t get24(const uint8_t* p) {
    return ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
}

bool timeLeft(const TTTlsSession* s) {
    return (int32_t)(s->deadline - millis()) > 0;
}

void noteIoProgress(TTTlsSession* s) {
    if (s->ioTimeoutMs > 0) {
        s->deadline = millis() + s->ioTimeoutMs;
    }
}

bool waitReadable(int fd, uint32_t deadline) {
    while (true) {
        const int32_t left = (int32_t)(deadline - millis());
        if (left <= 0) {
            return false;
        }
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        struct timeval tv;
        tv.tv_sec = left / 1000;
        tv.tv_usec = (left % 1000) * 1000;
        const int rc = select(fd + 1, &rfds, nullptr, nullptr, &tv);
        if (rc > 0) {
            return true;
        }
        if (rc < 0 && errno != EINTR) {
            return false;
        }
    }
}

bool waitWritable(int fd, uint32_t deadline) {
    while (true) {
        const int32_t left = (int32_t)(deadline - millis());
        if (left <= 0) {
            return false;
        }
        fd_set wfds;
        FD_ZERO(&wfds);
        FD_SET(fd, &wfds);
        struct timeval tv;
        tv.tv_sec = left / 1000;
        tv.tv_usec = (left % 1000) * 1000;
        const int rc = select(fd + 1, nullptr, &wfds, nullptr, &tv);
        if (rc > 0) {
            return true;
        }
        if (rc < 0 && errno != EINTR) {
            return false;
        }
    }
}

bool readExact(TTTlsSession* s, uint8_t* buf, size_t len) {
    size_t got = 0;
    while (got < len) {
        if (!waitReadable(s->socket, s->deadline)) {
            LOG_E("TLS: read timeout got=%u need=%u", (unsigned)got, (unsigned)len);
            return false;
        }
        const int n = lwip_recv(s->socket, buf + got, len - got, 0);
        if (n == 0) {
            LOG_E("TLS: peer closed during read");
            return false;
        }
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                continue;
            }
            LOG_E("TLS: recv errno=%d", errno);
            return false;
        }
        got += (size_t)n;
    }
    return true;
}

int readSome(TTTlsSession* s, uint8_t* buf, size_t len) {
    if (!waitReadable(s->socket, s->deadline)) {
        return -1;
    }
    const int n = lwip_recv(s->socket, buf, len, 0);
    if (n == 0) {
        return 0;
    }
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            return -2;
        }
        return -1;
    }
    return n;
}

bool writeExact(TTTlsSession* s, const uint8_t* buf, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        if (!waitWritable(s->socket, s->deadline)) {
            LOG_E("TLS: write timeout");
            return false;
        }
        const int n = lwip_send(s->socket, buf + sent, len - sent, 0);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                continue;
            }
            LOG_E("TLS: send errno=%d", errno);
            return false;
        }
        sent += (size_t)n;
    }
    return true;
}

bool openTrunc(const char* path, File* out) {
    *out = tt_file_create(path);
    return (bool)(*out);
}

bool copySockToFile(TTTlsSession* s, File& file, size_t len) {
    uint8_t chunk[TT_TLS_CHUNK];
    while (len > 0) {
        const size_t take = len < sizeof(chunk) ? len : sizeof(chunk);
        if (!readExact(s, chunk, take)) {
            return false;
        }
        if (file.write(chunk, take) != take) {
            LOG_E("TLS: tmp write failed");
            return false;
        }
        len -= take;
    }
    return true;
}

void makeAad(uint8_t aad[13], uint64_t seq, uint8_t type, uint16_t plainLen) {
    put64(aad, seq);
    aad[8] = type;
    put16(aad + 9, TT_TLS_VERSION);
    put16(aad + 11, plainLen);
}

bool gcmCryptRam(const uint8_t key[16], const uint8_t iv[4], const uint8_t explicitNonce[8],
                 uint64_t seq, uint8_t type, const uint8_t* input, uint8_t* output,
                 size_t len, uint8_t tag[TT_GCM_TAG_LEN], bool encrypt) {
    uint8_t nonce[TT_GCM_NONCE];
    memcpy(nonce, iv, 4);
    memcpy(nonce + 4, explicitNonce, 8);
    uint8_t aad[13];
    makeAad(aad, seq, type, (uint16_t)len);
    return tt_aes128_gcm(key, nonce, aad, sizeof(aad), input, output, len, tag, encrypt);
}

bool sendPlainRecord(TTTlsSession* s, uint8_t type, const uint8_t* data, size_t len) {
    uint8_t hdr[5];
    hdr[0] = type;
    put16(hdr + 1, TT_TLS_VERSION);
    put16(hdr + 3, (uint16_t)len);
    return writeExact(s, hdr, 5) && writeExact(s, data, len);
}

bool sendEncryptedRecord(TTTlsSession* s, uint8_t type, const uint8_t* data, size_t len) {
    uint8_t explicitNonce[8];
    put64(explicitNonce, s->clientSeq);
    uint8_t tag[TT_GCM_TAG_LEN];
    uint8_t hdr[5];
    hdr[0] = type;
    put16(hdr + 1, TT_TLS_VERSION);
    put16(hdr + 3, (uint16_t)(8 + len + TT_GCM_TAG_LEN));

    if (len <= TT_TLS_RECORD_RAM) {
        uint8_t ct[TT_TLS_RECORD_RAM];
        if (!gcmCryptRam(s->clientKey, s->clientIV, explicitNonce, s->clientSeq,
                         type, data, ct, len, tag, true)) {
            return false;
        }
        if (!writeExact(s, hdr, 5) || !writeExact(s, explicitNonce, 8) ||
            !writeExact(s, ct, len) || !writeExact(s, tag, TT_GCM_TAG_LEN)) {
            return false;
        }
    } else {
        File rec;
        if (!openTrunc(TT_TLS_TMP_REC, &rec)) {
            return false;
        }
        uint8_t nonce[TT_GCM_NONCE];
        memcpy(nonce, s->clientIV, 4);
        memcpy(nonce + 4, explicitNonce, 8);
        uint8_t aad[13];
        makeAad(aad, s->clientSeq, type, (uint16_t)len);
        TTGcmCtx gcm;
        tt_gcm_init(&gcm, s->clientKey, nonce, true);
        tt_gcm_aad(&gcm, aad, sizeof(aad));
        size_t off = 0;
        uint8_t chunk[TT_TLS_CHUNK];
        while (off < len) {
            const size_t take = (len - off) < sizeof(chunk) ? (len - off) : sizeof(chunk);
            tt_gcm_update(&gcm, data + off, chunk, take);
            if (rec.write(chunk, take) != take) {
                rec.close();
                return false;
            }
            off += take;
        }
        tt_gcm_final(&gcm, tag);
        rec.close();
        if (!writeExact(s, hdr, 5) || !writeExact(s, explicitNonce, 8)) {
            return false;
        }
        File src = tt_file_open(TT_TLS_TMP_REC, "r");
        if (!src) {
            return false;
        }
        size_t remain = len;
        while (remain > 0) {
            const size_t take = remain < sizeof(chunk) ? remain : sizeof(chunk);
            if (src.read(chunk, take) != (int)take || !writeExact(s, chunk, take)) {
                src.close();
                return false;
            }
            remain -= take;
        }
        src.close();
        if (!writeExact(s, tag, TT_GCM_TAG_LEN)) {
            return false;
        }
    }
    s->clientSeq++;
    return true;
}

bool sendHandshake(TTTlsSession* s, const uint8_t* msg, size_t len) {
    tt_sha256_update(&s->hsHash, msg, len);
    if (s->sendEncrypted) {
        return sendEncryptedRecord(s, 22, msg, len);
    }
    return sendPlainRecord(s, 22, msg, len);
}

bool recvRawRecord(TTTlsSession* s, uint8_t* type, uint8_t* ram, size_t ramMax,
                   size_t* recLen, bool* onRam) {
    uint8_t hdr[5];
    if (!readExact(s, hdr, 5)) {
        return false;
    }
    *type = hdr[0];
    const uint16_t ver = get16(hdr + 1);
    *recLen = get16(hdr + 3);
    if (ver != TT_TLS_VERSION && ver != 0x0301) {
        LOG_E("TLS: bad record ver=0x%04x", ver);
        return false;
    }
    if (*recLen == 0 || *recLen > TT_TLS_MAX_RECORD) {
        LOG_E("TLS: bad record len=%u", (unsigned)*recLen);
        return false;
    }
    if (*recLen <= ramMax) {
        *onRam = true;
        return readExact(s, ram, *recLen);
    }
    *onRam = false;
    File rec;
    if (!openTrunc(TT_TLS_TMP_REC, &rec)) {
        return false;
    }
    const bool ok = copySockToFile(s, rec, *recLen);
    rec.close();
    return ok;
}

bool decryptRam(TTTlsSession* s, uint8_t type, const uint8_t* rec, size_t recLen,
                uint8_t* plain, size_t* plainLen) {
    if (recLen < 8 + TT_GCM_TAG_LEN) {
        LOG_E("TLS: record too short %u", (unsigned)recLen);
        return false;
    }
    const size_t ctLen = recLen - 8 - TT_GCM_TAG_LEN;
    uint8_t tag[TT_GCM_TAG_LEN];
    if (!gcmCryptRam(s->serverKey, s->serverIV, rec, s->serverSeq,
                     type, rec + 8, plain, ctLen, tag, false)) {
        return false;
    }
    if (memcmp(tag, rec + 8 + ctLen, TT_GCM_TAG_LEN) != 0) {
        LOG_E("TLS: gcm tag mismatch rec=%u ct=%u type=%u",
              (unsigned)recLen, (unsigned)ctLen, type);
        return false;
    }
    s->serverSeq++;
    *plainLen = ctLen;
    return true;
}

bool decryptFile(TTTlsSession* s, uint8_t type, size_t recLen, size_t* plainLen) {
    if (recLen < 8 + TT_GCM_TAG_LEN) {
        return false;
    }
    const size_t ctLen = recLen - 8 - TT_GCM_TAG_LEN;
    uint8_t explicitNonce[8];
    File rec = tt_file_open(TT_TLS_TMP_REC, "r");
    File plain;
    if (!rec || rec.read(explicitNonce, 8) != 8 || !openTrunc(TT_TLS_TMP_PLAIN, &plain)) {
        if (rec) {
            rec.close();
        }
        return false;
    }
    uint8_t nonce[TT_GCM_NONCE];
    memcpy(nonce, s->serverIV, 4);
    memcpy(nonce + 4, explicitNonce, 8);
    uint8_t aad[13];
    makeAad(aad, s->serverSeq, type, (uint16_t)ctLen);
    TTGcmCtx gcm;
    tt_gcm_init(&gcm, s->serverKey, nonce, false);
    tt_gcm_aad(&gcm, aad, sizeof(aad));
    uint8_t chunk[TT_TLS_CHUNK];
    size_t remain = ctLen;
    while (remain > 0) {
        const size_t take = remain < sizeof(chunk) ? remain : sizeof(chunk);
        if (rec.read(chunk, take) != (int)take) {
            rec.close();
            plain.close();
            return false;
        }
        tt_gcm_update(&gcm, chunk, chunk, take);
        if (plain.write(chunk, take) != take) {
            rec.close();
            plain.close();
            return false;
        }
        remain -= take;
    }
    uint8_t gotTag[TT_GCM_TAG_LEN];
    uint8_t calcTag[TT_GCM_TAG_LEN];
    if (rec.read(gotTag, TT_GCM_TAG_LEN) != TT_GCM_TAG_LEN) {
        rec.close();
        plain.close();
        return false;
    }
    rec.close();
    tt_gcm_final(&gcm, calcTag);
    plain.close();
    if (memcmp(gotTag, calcTag, TT_GCM_TAG_LEN) != 0) {
        LOG_E("TLS: gcm tag mismatch (file)");
        return false;
    }
    s->serverSeq++;
    *plainLen = ctLen;
    return true;
}

int readPlainFile(TTTlsSession* s, uint8_t* data, size_t len) {
    File plainFile = tt_file_open(TT_TLS_TMP_PLAIN, "r");
    if (!plainFile) {
        return -1;
    }
    const size_t take = s->plainFileLen < len ? s->plainFileLen : len;
    if (!plainFile.seek((uint32_t)s->plainFileOff) || plainFile.read(data, take) != (int)take) {
        LOG_E("TLS: plain file read failed off=%u len=%u",
              (unsigned)s->plainFileOff, (unsigned)take);
        plainFile.close();
        return -1;
    }
    plainFile.close();
    s->plainFileOff += take;
    s->plainFileLen -= take;
    return (int)take;
}

bool parseServerHello(TTTlsSession* s, const uint8_t* body, size_t len) {
    if (len < 38) {
        return false;
    }
    memcpy(s->serverRandom, body + 2, 32);
    const uint8_t sidLen = body[34];
    if (len < (size_t)(35 + sidLen + 3)) {
        return false;
    }
    s->cipherSuite = get16(body + 35 + sidLen);
    if (s->cipherSuite != TT_TLS_SUITE_ECDHE_RSA && s->cipherSuite != TT_TLS_SUITE_ECDHE_ECDSA) {
        LOG_E("TLS: unsupported suite 0x%04x", s->cipherSuite);
        return false;
    }
    LOG_I("TLS: ServerHello suite=0x%04x", s->cipherSuite);
    return true;
}

bool parseServerKeyExchange(TTTlsSession* s, const uint8_t* body, size_t len) {
    if (len < 4 + TT_X25519_LEN) {
        LOG_E("TLS: SKE too short %u", (unsigned)len);
        return false;
    }
    if (body[0] != 3) {
        LOG_E("TLS: SKE curve_type=%u", body[0]);
        return false;
    }
    const uint16_t group = get16(body + 1);
    if (group != TT_TLS_GROUP_X25519) {
        LOG_E("TLS: SKE group=0x%04x need x25519", group);
        return false;
    }
    if (body[3] != TT_X25519_LEN) {
        LOG_E("TLS: SKE pub_len=%u", body[3]);
        return false;
    }
    memcpy(s->serverPub, body + 4, TT_X25519_LEN);
    s->gotServerKey = true;
    LOG_I("TLS: ServerKeyExchange x25519");
    return true;
}

bool finishHsMessage(TTTlsSession* s) {
    LOG_I("TLS: hs type=%u len=%u", s->hsType, (unsigned)s->hsBodyNeed);
    switch (s->hsType) {
        case 2:
            return parseServerHello(s, s->hsBody, s->hsBodyGot);
        case 11:
            return true;
        case 12:
            return parseServerKeyExchange(s, s->hsBody, s->hsBodyGot);
        case 13:
            s->sawCertReq = true;
            return true;
        case 14:
            s->gotHelloDone = true;
            return true;
        case 4:
            return true;
        case 20:
            s->gotFinished = true;
            return true;
        default:
            LOG_I("TLS: skip hs type=%u", s->hsType);
            return true;
    }
}

bool feedHandshake(TTTlsSession* s, const uint8_t* data, size_t len) {
    tt_sha256_update(&s->hsHash, data, len);
    while (len > 0) {
        if (s->hsHdrGot < 4) {
            const size_t take = (4 - s->hsHdrGot) < len ? (4 - s->hsHdrGot) : len;
            memcpy(s->hsHdr + s->hsHdrGot, data, take);
            s->hsHdrGot += take;
            data += take;
            len -= take;
            if (s->hsHdrGot < 4) {
                return true;
            }
            s->hsType = s->hsHdr[0];
            s->hsBodyNeed = get24(s->hsHdr + 1);
            s->hsBodyGot = 0;
            s->hsStoreBody = (s->hsType == 2 || s->hsType == 12 || s->hsType == 20);
            if (s->hsStoreBody && s->hsBodyNeed > TT_TLS_HS_BODY_MAX) {
                LOG_E("TLS: hs body too large type=%u len=%u", s->hsType, (unsigned)s->hsBodyNeed);
                return false;
            }
            if (s->hsBodyNeed == 0) {
                s->hsHdrGot = 0;
                if (!finishHsMessage(s)) {
                    return false;
                }
            }
            continue;
        }
        const size_t remain = s->hsBodyNeed - s->hsBodyGot;
        const size_t take = remain < len ? remain : len;
        if (s->hsStoreBody) {
            memcpy(s->hsBody + s->hsBodyGot, data, take);
        }
        s->hsBodyGot += (uint32_t)take;
        data += take;
        len -= take;
        if (s->hsBodyGot >= s->hsBodyNeed) {
            s->hsHdrGot = 0;
            if (!finishHsMessage(s)) {
                return false;
            }
        }
    }
    return true;
}

bool deriveKeys(TTTlsSession* s, const uint8_t clientPriv[32]) {
    uint8_t premaster[TT_X25519_LEN];
    tt_x25519(premaster, clientPriv, s->serverPub);

    uint8_t seed[64];
    memcpy(seed, s->clientRandom, 32);
    memcpy(seed + 32, s->serverRandom, 32);
    tt_tls12_prf(premaster, sizeof(premaster), "master secret", seed, 64, s->master, 48);
    memset(premaster, 0, sizeof(premaster));

    uint8_t seed2[64];
    memcpy(seed2, s->serverRandom, 32);
    memcpy(seed2 + 32, s->clientRandom, 32);
    uint8_t keyBlock[40];
    tt_tls12_prf(s->master, 48, "key expansion", seed2, 64, keyBlock, sizeof(keyBlock));
    memcpy(s->clientKey, keyBlock, 16);
    memcpy(s->serverKey, keyBlock + 16, 16);
    memcpy(s->clientIV, keyBlock + 32, 4);
    memcpy(s->serverIV, keyBlock + 36, 4);
    memset(keyBlock, 0, sizeof(keyBlock));
    return true;
}

size_t buildClientHello(uint8_t* out, size_t max, const char* sni, const uint8_t* random) {
    if (sni == nullptr) {
        return 0;
    }
    const size_t hostLen = strlen(sni);
    if (hostLen == 0 || hostLen > 64 || max < 180 + hostLen) {
        return 0;
    }

    uint8_t* p = out + 4;
    put16(p, TT_TLS_VERSION);
    p += 2;
    memcpy(p, random, 32);
    p += 32;
    *p++ = 0;
    put16(p, 4);
    p += 2;
    put16(p, TT_TLS_SUITE_ECDHE_RSA);
    p += 2;
    put16(p, TT_TLS_SUITE_ECDHE_ECDSA);
    p += 2;
    *p++ = 1;
    *p++ = 0;

    uint8_t* extLenAt = p;
    p += 2;

    put16(p, 0);
    p += 2;
    put16(p, (uint16_t)(5 + hostLen));
    p += 2;
    put16(p, (uint16_t)(3 + hostLen));
    p += 2;
    *p++ = 0;
    put16(p, (uint16_t)hostLen);
    p += 2;
    memcpy(p, sni, hostLen);
    p += hostLen;

    put16(p, 10);
    p += 2;
    put16(p, 4);
    p += 2;
    put16(p, 2);
    p += 2;
    put16(p, TT_TLS_GROUP_X25519);
    p += 2;

    put16(p, 11);
    p += 2;
    put16(p, 2);
    p += 2;
    *p++ = 1;
    *p++ = 0;

    put16(p, 13);
    p += 2;
    put16(p, 8);
    p += 2;
    put16(p, 6);
    p += 2;
    put16(p, 0x0401);
    p += 2;
    put16(p, 0x0403);
    p += 2;
    put16(p, 0x0804);
    p += 2;

    put16(p, 1);
    p += 2;
    put16(p, 1);
    p += 2;
    *p++ = 2;

    put16(p, 0xff01);
    p += 2;
    put16(p, 1);
    p += 2;
    *p++ = 0;

    put16(extLenAt, (uint16_t)(p - extLenAt - 2));
    const size_t bodyLen = (size_t)(p - out - 4);
    out[0] = 1;
    put24(out + 1, (uint32_t)bodyLen);
    return bodyLen + 4;
}

bool sendClientKeyExchange(TTTlsSession* s, const uint8_t pub[32]) {
    uint8_t msg[37];
    msg[0] = 16;
    put24(msg + 1, 33);
    msg[4] = 32;
    memcpy(msg + 5, pub, 32);
    if (s->sawCertReq) {
        uint8_t emptyCert[7];
        emptyCert[0] = 11;
        put24(emptyCert + 1, 3);
        put24(emptyCert + 4, 0);
        if (!sendHandshake(s, emptyCert, sizeof(emptyCert))) {
            return false;
        }
    }
    return sendHandshake(s, msg, sizeof(msg));
}

bool sendFinished(TTTlsSession* s) {
    uint8_t hash[TT_SHA256_LEN];
    TTSha256 tmp = s->hsHash;
    tt_sha256_final(&tmp, hash);
    uint8_t verify[12];
    tt_tls12_prf(s->master, 48, "client finished", hash, TT_SHA256_LEN, verify, 12);

    uint8_t msg[16];
    msg[0] = 20;
    put24(msg + 1, 12);
    memcpy(msg + 4, verify, 12);
    tt_sha256_update(&s->hsHash, msg, sizeof(msg));
    return sendEncryptedRecord(s, 22, msg, sizeof(msg));
}

bool verifyServerFinished(TTTlsSession* s, const uint8_t* msg, size_t len) {
    if (len != 16 || msg[0] != 20 || get24(msg + 1) != 12) {
        LOG_E("TLS: bad finished len=%u", (unsigned)len);
        return false;
    }
    uint8_t hash[TT_SHA256_LEN];
    TTSha256 tmp = s->hsHash;
    tt_sha256_final(&tmp, hash);
    uint8_t expect[12];
    tt_tls12_prf(s->master, 48, "server finished", hash, TT_SHA256_LEN, expect, 12);
    if (memcmp(expect, msg + 4, 12) != 0) {
        LOG_E("TLS: server finished mismatch");
        return false;
    }
    return true;
}

bool recvNext(TTTlsSession* s, uint8_t* type, uint8_t* rec, uint8_t* plain,
              size_t* plainLen, bool* onRam) {
    size_t recLen = 0;
    bool rawRam = false;
    if (!recvRawRecord(s, type, rec, TT_TLS_RECORD_RAM, &recLen, &rawRam)) {
        return false;
    }
    if (*type == 21) {
        uint8_t alert[2] = {0, 0};
        if (rawRam && recLen >= 2) {
            memcpy(alert, rec, 2);
        }
        LOG_E("TLS: alert %u %u", alert[0], alert[1]);
        return false;
    }
    if (s->recvEncrypted && *type != 20) {
        if (rawRam) {
            if (!decryptRam(s, *type, rec, recLen, plain, plainLen)) {
                return false;
            }
            *onRam = true;
            return true;
        }
        if (!decryptFile(s, *type, recLen, plainLen)) {
            return false;
        }
        *onRam = false;
        return true;
    }
    if (rawRam) {
        memcpy(plain, rec, recLen);
    }
    *plainLen = recLen;
    *onRam = rawRam;
    return true;
}

bool recvHandshakeFlight(TTTlsSession* s) {
    uint8_t rec[TT_TLS_RECORD_RAM];
    uint8_t plain[TT_TLS_RECORD_RAM];
    while (!s->gotHelloDone) {
        if (!timeLeft(s)) {
            LOG_E("TLS: handshake timeout before SHD");
            return false;
        }
        uint8_t type = 0;
        size_t len = 0;
        bool onRam = false;
        if (!recvNext(s, &type, rec, plain, &len, &onRam)) {
            return false;
        }
        if (type != 22) {
            LOG_E("TLS: unexpected type %u before SHD", type);
            return false;
        }
        if (onRam) {
            if (!feedHandshake(s, plain, len)) {
                return false;
            }
        } else {
            File src = tt_file_open(TT_TLS_TMP_REC, "r");
            if (!src) {
                return false;
            }
            uint8_t chunk[TT_TLS_CHUNK];
            while (len > 0) {
                const size_t take = len < sizeof(chunk) ? len : sizeof(chunk);
                if (src.read(chunk, take) != (int)take || !feedHandshake(s, chunk, take)) {
                    src.close();
                    return false;
                }
                len -= take;
            }
            src.close();
        }
    }
    return s->gotServerKey;
}

bool recvServerFinishFlight(TTTlsSession* s) {
    uint8_t rec[TT_TLS_RECORD_RAM];
    uint8_t plain[TT_TLS_RECORD_RAM];
    bool gotCcs = false;
    while (!s->gotFinished) {
        if (!timeLeft(s)) {
            LOG_E("TLS: timeout waiting finished");
            return false;
        }
        uint8_t type = 0;
        size_t len = 0;
        bool onRam = false;
        if (!recvNext(s, &type, rec, plain, &len, &onRam)) {
            return false;
        }
        if (type == 20) {
            uint8_t ccs = 1;
            if (onRam) {
                ccs = rec[0];
            } else {
                File src = tt_file_open(TT_TLS_TMP_REC, "r");
                if (!src || src.read(&ccs, 1) != 1) {
                    if (src) {
                        src.close();
                    }
                    LOG_E("TLS: bad CCS");
                    return false;
                }
                src.close();
            }
            if (ccs != 1) {
                LOG_E("TLS: bad CCS");
                return false;
            }
            s->recvEncrypted = true;
            gotCcs = true;
            continue;
        }
        if (type == 22) {
            if (gotCcs) {
                if (!onRam || !verifyServerFinished(s, plain, len)) {
                    return false;
                }
                s->gotFinished = true;
                return true;
            }
            if (onRam) {
                if (!feedHandshake(s, plain, len)) {
                    return false;
                }
            }
            continue;
        }
        LOG_E("TLS: unexpected type %u before finished", type);
        return false;
    }
    return s->gotFinished;
}

}  // namespace

bool tt_tls_handshake(TTTlsSession* session, int socket, const char* sni, uint32_t timeoutMs) {
    if (session == nullptr || socket < 0 || sni == nullptr) {
        return false;
    }
    memset(session, 0, sizeof(*session));
    session->socket = socket;
    session->deadline = millis() + timeoutMs;
    tt_sha256_init(&session->hsHash);

    esp_fill_random(session->clientRandom, 32);
    uint8_t clientPriv[32];
    uint8_t clientPub[32];
    esp_fill_random(clientPriv, 32);
    tt_x25519_base(clientPub, clientPriv);

    uint8_t hello[256];
    const size_t helloLen = buildClientHello(hello, sizeof(hello), sni, session->clientRandom);
    if (helloLen == 0) {
        LOG_E("TLS: ClientHello build failed");
        memset(clientPriv, 0, sizeof(clientPriv));
        return false;
    }
    LOG_I("TLS: send ClientHello sni=%s len=%u", sni, (unsigned)helloLen);
    if (!sendHandshake(session, hello, helloLen)) {
        memset(clientPriv, 0, sizeof(clientPriv));
        return false;
    }
    if (!recvHandshakeFlight(session)) {
        memset(clientPriv, 0, sizeof(clientPriv));
        return false;
    }
    if (!deriveKeys(session, clientPriv)) {
        memset(clientPriv, 0, sizeof(clientPriv));
        return false;
    }
    memset(clientPriv, 0, sizeof(clientPriv));

    if (!sendClientKeyExchange(session, clientPub)) {
        return false;
    }
    const uint8_t ccs = 1;
    if (!sendPlainRecord(session, 20, &ccs, 1)) {
        return false;
    }
    session->sendEncrypted = true;
    session->clientSeq = 0;
    if (!sendFinished(session)) {
        return false;
    }
    if (!recvServerFinishFlight(session)) {
        return false;
    }

    memset(session->master, 0, sizeof(session->master));
    LOG_I("TLS: handshake ok suite=0x%04x", session->cipherSuite);
    return true;
}

void tt_tls_set_timeout(TTTlsSession* session, uint32_t timeoutMs) {
    if (session != nullptr) {
        session->ioTimeoutMs = timeoutMs;
        session->deadline = millis() + timeoutMs;
    }
}

int tt_tls_write(TTTlsSession* session, const uint8_t* data, size_t len) {
    if (session == nullptr || data == nullptr || !session->sendEncrypted) {
        return -1;
    }
    size_t sent = 0;
    while (sent < len) {
        const size_t take = (len - sent) < TT_TLS_CHUNK ? (len - sent) : TT_TLS_CHUNK;
        if (!sendEncryptedRecord(session, 23, data + sent, take)) {
            return -1;
        }
        sent += take;
    }
    return (int)sent;
}

int tt_tls_read(TTTlsSession* session, uint8_t* data, size_t len) {
    if (session == nullptr || data == nullptr || len == 0) {
        return -1;
    }
    if (session->leftoverLen > 0) {
        const size_t take = session->leftoverLen < len ? session->leftoverLen : len;
        memcpy(data, session->leftover + session->leftoverOff, take);
        session->leftoverOff += take;
        session->leftoverLen -= take;
        noteIoProgress(session);
        return (int)take;
    }
    if (session->plainFileLen > 0) {
        noteIoProgress(session);
        return readPlainFile(session, data, len);
    }

    while (true) {
        if (!timeLeft(session)) {
            LOG_E("TLS: app read timeout");
            return -1;
        }
        uint8_t hdr[5];
        const int peek = readSome(session, hdr, 5);
        if (peek == 0) {
            return 0;
        }
        if (peek == -2) {
            continue;
        }
        if (peek < 0) {
            return -1;
        }
        if (peek < 5 && !readExact(session, hdr + peek, 5 - (size_t)peek)) {
            return -1;
        }
        const uint8_t type = hdr[0];
        const size_t recLen = get16(hdr + 3);
        if (recLen == 0 || recLen > TT_TLS_MAX_RECORD) {
            LOG_E("TLS: bad app record len=%u", (unsigned)recLen);
            return -1;
        }

        uint8_t rec[TT_TLS_RECORD_RAM];
        uint8_t plain[TT_TLS_RECORD_RAM];
        bool onRam = recLen <= sizeof(rec);
        if (onRam) {
            if (!readExact(session, rec, recLen)) {
                return -1;
            }
        } else {
            File recFile;
            if (!openTrunc(TT_TLS_TMP_REC, &recFile) || !copySockToFile(session, recFile, recLen)) {
                if (recFile) {
                    recFile.close();
                }
                return -1;
            }
            recFile.close();
        }
        noteIoProgress(session);

        if (type == 21) {
            uint8_t alert[2] = {0, 0};
            size_t plainLen = 0;
            if (session->recvEncrypted) {
                if (onRam) {
                    if (!decryptRam(session, type, rec, recLen, plain, &plainLen) || plainLen < 2) {
                        return -1;
                    }
                    memcpy(alert, plain, 2);
                } else if (!decryptFile(session, type, recLen, &plainLen)) {
                    return -1;
                }
            } else if (onRam) {
                memcpy(alert, rec, 2);
            }
            if (alert[1] == 0) {
                return 0;
            }
            LOG_E("TLS: alert %u %u", alert[0], alert[1]);
            return -1;
        }
        if (type == 20) {
            session->recvEncrypted = true;
            continue;
        }
        if (type == 22) {
            size_t skipLen = 0;
            if (onRam) {
                decryptRam(session, type, rec, recLen, plain, &skipLen);
            }
            continue;
        }
        if (type != 23) {
            LOG_E("TLS: unexpected app type %u", type);
            return -1;
        }

        size_t plainLen = 0;
        if (onRam) {
            if (!decryptRam(session, type, rec, recLen, plain, &plainLen)) {
                return -1;
            }
            const size_t take = plainLen < len ? plainLen : len;
            memcpy(data, plain, take);
            if (plainLen > take) {
                memcpy(session->leftover, plain + take, plainLen - take);
                session->leftoverOff = 0;
                session->leftoverLen = plainLen - take;
            }
            return (int)take;
        }
        if (!decryptFile(session, type, recLen, &plainLen)) {
            return -1;
        }
        session->plainFileOff = 0;
        session->plainFileLen = plainLen;
        return readPlainFile(session, data, len);
    }
}

void tt_tls_close(TTTlsSession* session) {
    tt_file_remove(TT_TLS_TMP_REC);
    tt_file_remove(TT_TLS_TMP_PLAIN);
    if (session != nullptr) {
        memset(session->clientKey, 0, sizeof(session->clientKey));
        memset(session->serverKey, 0, sizeof(session->serverKey));
        memset(session->master, 0, sizeof(session->master));
    }
}
