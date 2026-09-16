#pragma once

#include <stddef.h>
#include <stdint.h>

#define TT_SHA256_LEN   32
#define TT_AES_BLOCK    16
#define TT_GCM_TAG_LEN  16
#define TT_GCM_NONCE    12
#define TT_X25519_LEN   32

struct TTSha256 {
    uint32_t state[8];
    uint64_t bitCount;
    uint8_t buffer[64];
    size_t bufferLen;
};

struct TTGcmCtx {
    uint8_t rk[176];
    uint8_t H[TT_AES_BLOCK];
    uint8_t J0[TT_AES_BLOCK];
    uint8_t ctr[TT_AES_BLOCK];
    uint8_t y[TT_AES_BLOCK];
    uint8_t buf[TT_AES_BLOCK];
    size_t bufLen;
    uint64_t aadBits;
    uint64_t ctBits;
    bool encrypt;
    bool ctStarted;
};

void tt_sha256_init(TTSha256* ctx);
void tt_sha256_update(TTSha256* ctx, const uint8_t* data, size_t len);
void tt_sha256_final(TTSha256* ctx, uint8_t out[TT_SHA256_LEN]);
void tt_hmac_sha256(const uint8_t* key, size_t keyLen,
                    const uint8_t* data, size_t dataLen,
                    uint8_t out[TT_SHA256_LEN]);
void tt_tls12_prf(const uint8_t* secret, size_t secretLen,
                  const char* label,
                  const uint8_t* seed, size_t seedLen,
                  uint8_t* out, size_t outLen);
void tt_x25519(uint8_t out[TT_X25519_LEN],
               const uint8_t scalar[TT_X25519_LEN],
               const uint8_t point[TT_X25519_LEN]);
void tt_x25519_base(uint8_t out[TT_X25519_LEN], const uint8_t scalar[TT_X25519_LEN]);
void tt_gcm_init(TTGcmCtx* ctx, const uint8_t key[16],
                 const uint8_t nonce[TT_GCM_NONCE], bool encrypt);
void tt_gcm_aad(TTGcmCtx* ctx, const uint8_t* aad, size_t len);
void tt_gcm_update(TTGcmCtx* ctx, const uint8_t* input, uint8_t* output, size_t len);
void tt_gcm_final(TTGcmCtx* ctx, uint8_t tag[TT_GCM_TAG_LEN]);
bool tt_aes128_gcm(const uint8_t key[16], const uint8_t nonce[TT_GCM_NONCE],
                   const uint8_t* aad, size_t aadLen,
                   const uint8_t* input, uint8_t* output, size_t len,
                   uint8_t tag[TT_GCM_TAG_LEN], bool encrypt);
