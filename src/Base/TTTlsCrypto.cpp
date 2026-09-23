#include "TTTlsCrypto.h"

#include <string.h>

static uint32_t rotr32(uint32_t x, uint32_t n) {
    return (x >> n) | (x << (32 - n));
}

static uint32_t load32_be(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static void store32_be(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static void store64_be(uint8_t* p, uint64_t v) {
    for (int i = 7; i >= 0; --i) {
        p[i] = (uint8_t)v;
        v >>= 8;
    }
}

static void xor_block(uint8_t* d, const uint8_t* s, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        d[i] ^= s[i];
    }
}

static const uint32_t kSha256K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

void tt_sha256_init(TTSha256* ctx) {
    ctx->state[0] = 0x6a09e667;
    ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372;
    ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f;
    ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab;
    ctx->state[7] = 0x5be0cd19;
    ctx->bitCount = 0;
    ctx->bufferLen = 0;
}

static void sha256_compress(TTSha256* ctx, const uint8_t block[64]) {
    uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
        w[i] = load32_be(block + i * 4);
    }
    for (int i = 16; i < 64; ++i) {
        const uint32_t s0 = rotr32(w[i - 15], 7) ^ rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const uint32_t s1 = rotr32(w[i - 2], 17) ^ rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    uint32_t a = ctx->state[0];
    uint32_t b = ctx->state[1];
    uint32_t c = ctx->state[2];
    uint32_t d = ctx->state[3];
    uint32_t e = ctx->state[4];
    uint32_t f = ctx->state[5];
    uint32_t g = ctx->state[6];
    uint32_t h = ctx->state[7];

    for (int i = 0; i < 64; ++i) {
        const uint32_t S1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
        const uint32_t ch = (e & f) ^ ((~e) & g);
        const uint32_t t1 = h + S1 + ch + kSha256K[i] + w[i];
        const uint32_t S0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
        const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t t2 = S0 + maj;
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

void tt_sha256_update(TTSha256* ctx, const uint8_t* data, size_t len) {
    if (data == nullptr || len == 0) {
        return;
    }
    ctx->bitCount += (uint64_t)len * 8;
    while (len > 0) {
        const size_t room = 64 - ctx->bufferLen;
        const size_t take = len < room ? len : room;
        memcpy(ctx->buffer + ctx->bufferLen, data, take);
        ctx->bufferLen += take;
        data += take;
        len -= take;
        if (ctx->bufferLen == 64) {
            sha256_compress(ctx, ctx->buffer);
            ctx->bufferLen = 0;
        }
    }
}

void tt_sha256_final(TTSha256* ctx, uint8_t out[TT_SHA256_LEN]) {
    const uint64_t bits = ctx->bitCount;
    const size_t n = ctx->bufferLen;
    if (n >= 56) {
        ctx->buffer[n] = 0x80;
        if (n + 1 < 64) {
            memset(ctx->buffer + n + 1, 0, 64 - n - 1);
        }
        sha256_compress(ctx, ctx->buffer);
        memset(ctx->buffer, 0, 56);
    } else {
        ctx->buffer[n] = 0x80;
        if (n + 1 < 56) {
            memset(ctx->buffer + n + 1, 0, 56 - n - 1);
        }
    }
    store64_be(ctx->buffer + 56, bits);
    sha256_compress(ctx, ctx->buffer);
    for (int i = 0; i < 8; ++i) {
        store32_be(out + i * 4, ctx->state[i]);
    }
}

void tt_hmac_sha256(const uint8_t* key, size_t keyLen,
                    const uint8_t* data, size_t dataLen,
                    uint8_t out[TT_SHA256_LEN]) {
    uint8_t kpad[64];
    memset(kpad, 0, sizeof(kpad));
    if (keyLen > 64) {
        TTSha256 ctx;
        tt_sha256_init(&ctx);
        tt_sha256_update(&ctx, key, keyLen);
        tt_sha256_final(&ctx, kpad);
    } else if (key != nullptr && keyLen > 0) {
        memcpy(kpad, key, keyLen);
    }

    uint8_t ipad[64];
    uint8_t opad[64];
    for (int i = 0; i < 64; ++i) {
        ipad[i] = (uint8_t)(kpad[i] ^ 0x36);
        opad[i] = (uint8_t)(kpad[i] ^ 0x5c);
    }

    TTSha256 ctx;
    tt_sha256_init(&ctx);
    tt_sha256_update(&ctx, ipad, 64);
    tt_sha256_update(&ctx, data, dataLen);
    uint8_t inner[TT_SHA256_LEN];
    tt_sha256_final(&ctx, inner);

    tt_sha256_init(&ctx);
    tt_sha256_update(&ctx, opad, 64);
    tt_sha256_update(&ctx, inner, TT_SHA256_LEN);
    tt_sha256_final(&ctx, out);
}

void tt_tls12_prf(const uint8_t* secret, size_t secretLen,
                  const char* label,
                  const uint8_t* seed, size_t seedLen,
                  uint8_t* out, size_t outLen) {
    const size_t labelLen = strlen(label);
    uint8_t labelSeed[96];
    memcpy(labelSeed, label, labelLen);
    if (seed != nullptr && seedLen > 0) {
        memcpy(labelSeed + labelLen, seed, seedLen);
    }
    const size_t lsLen = labelLen + seedLen;

    uint8_t a[TT_SHA256_LEN];
    tt_hmac_sha256(secret, secretLen, labelSeed, lsLen, a);

    size_t produced = 0;
    while (produced < outLen) {
        uint8_t blockIn[96 + TT_SHA256_LEN];
        memcpy(blockIn, a, TT_SHA256_LEN);
        memcpy(blockIn + TT_SHA256_LEN, labelSeed, lsLen);
        uint8_t block[TT_SHA256_LEN];
        tt_hmac_sha256(secret, secretLen, blockIn, TT_SHA256_LEN + lsLen, block);
        const size_t take = (outLen - produced) < TT_SHA256_LEN ? (outLen - produced) : TT_SHA256_LEN;
        memcpy(out + produced, block, take);
        produced += take;
        tt_hmac_sha256(secret, secretLen, a, TT_SHA256_LEN, a);
    }
}

typedef int64_t gf[16];

static const gf k121665 = {0xDB41, 1};
static const uint8_t kX25519Base[32] = {9};

static void car25519(gf o) {
    for (int i = 0; i < 16; ++i) {
        o[i] += (1LL << 16);
        const int64_t c = o[i] >> 16;
        o[(i + 1) * (i < 15)] += c - 1 + 37 * (c - 1) * (i == 15);
        o[i] -= c << 16;
    }
}

static void sel25519(gf p, gf q, int b) {
    const int64_t c = ~(int64_t)(b - 1);
    for (int i = 0; i < 16; ++i) {
        const int64_t t = c & (p[i] ^ q[i]);
        p[i] ^= t;
        q[i] ^= t;
    }
}

static void pack25519(uint8_t* o, const gf n) {
    gf m;
    gf t;
    for (int i = 0; i < 16; ++i) {
        t[i] = n[i];
    }
    car25519(t);
    car25519(t);
    car25519(t);
    for (int j = 0; j < 2; ++j) {
        m[0] = t[0] - 0xffed;
        for (int i = 1; i < 15; ++i) {
            m[i] = t[i] - 0xffff - ((m[i - 1] >> 16) & 1);
            m[i - 1] &= 0xffff;
        }
        m[15] = t[15] - 0x7fff - ((m[14] >> 16) & 1);
        const int b = (int)((m[15] >> 16) & 1);
        m[14] &= 0xffff;
        sel25519(t, m, 1 - b);
    }
    for (int i = 0; i < 16; ++i) {
        o[2 * i] = (uint8_t)t[i];
        o[2 * i + 1] = (uint8_t)(t[i] >> 8);
    }
}

static void unpack25519(gf o, const uint8_t* n) {
    for (int i = 0; i < 16; ++i) {
        o[i] = n[2 * i] + ((int64_t)n[2 * i + 1] << 8);
    }
    o[15] &= 0x7fff;
}

static void fe_add(gf o, const gf a, const gf b) {
    for (int i = 0; i < 16; ++i) {
        o[i] = a[i] + b[i];
    }
}

static void fe_sub(gf o, const gf a, const gf b) {
    for (int i = 0; i < 16; ++i) {
        o[i] = a[i] - b[i];
    }
}

static void fe_mul(gf o, const gf a, const gf b) {
    int64_t t[31];
    memset(t, 0, sizeof(t));
    for (int i = 0; i < 16; ++i) {
        for (int j = 0; j < 16; ++j) {
            t[i + j] += a[i] * b[j];
        }
    }
    for (int i = 0; i < 15; ++i) {
        t[i] += 38 * t[i + 16];
    }
    for (int i = 0; i < 16; ++i) {
        o[i] = t[i];
    }
    car25519(o);
    car25519(o);
}

static void fe_sq(gf o, const gf a) {
    fe_mul(o, a, a);
}

static void fe_inv(gf o, const gf i) {
    gf c;
    for (int a = 0; a < 16; ++a) {
        c[a] = i[a];
    }
    for (int a = 253; a >= 0; --a) {
        fe_sq(c, c);
        if (a != 2 && a != 4) {
            fe_mul(c, c, i);
        }
    }
    for (int a = 0; a < 16; ++a) {
        o[a] = c[a];
    }
}

void tt_x25519_base(uint8_t out[TT_X25519_LEN], const uint8_t scalar[TT_X25519_LEN]) {
    tt_x25519(out, scalar, kX25519Base);
}

void tt_x25519(uint8_t out[TT_X25519_LEN],
               const uint8_t scalar[TT_X25519_LEN],
               const uint8_t point[TT_X25519_LEN]) {
    uint8_t z[32];
    memcpy(z, scalar, 32);
    z[0] &= 248;
    z[31] = (uint8_t)((z[31] & 127) | 64);

    gf x;
    unpack25519(x, point);

    gf a;
    gf b;
    gf c;
    gf d;
    gf e;
    gf f;
    memset(a, 0, sizeof(a));
    memset(c, 0, sizeof(c));
    memset(d, 0, sizeof(d));
    a[0] = 1;
    d[0] = 1;
    for (int i = 0; i < 16; ++i) {
        b[i] = x[i];
    }

    for (int i = 254; i >= 0; --i) {
        const int r = (z[i >> 3] >> (i & 7)) & 1;
        sel25519(a, b, r);
        sel25519(c, d, r);
        fe_add(e, a, c);
        fe_sub(a, a, c);
        fe_add(c, b, d);
        fe_sub(b, b, d);
        fe_sq(d, e);
        fe_sq(f, a);
        fe_mul(a, c, a);
        fe_mul(c, b, e);
        fe_add(e, a, c);
        fe_sub(a, a, c);
        fe_sq(b, a);
        fe_sub(c, d, f);
        fe_mul(a, c, k121665);
        fe_add(a, a, d);
        fe_mul(c, c, a);
        fe_mul(a, d, f);
        fe_mul(d, b, x);
        fe_sq(b, e);
        sel25519(a, b, r);
        sel25519(c, d, r);
    }

    fe_inv(c, c);
    fe_mul(a, a, c);
    pack25519(out, a);
}

static const uint8_t kAesSbox[256] = {
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
    0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
    0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
    0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
    0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
    0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
    0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
    0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
    0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
    0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
    0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
    0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
    0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
    0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
    0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
    0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16
};

static const uint8_t kAesRcon[10] = {
    0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36
};

static uint8_t xtime(uint8_t a) {
    return (uint8_t)((a << 1) ^ ((a & 0x80) ? 0x1b : 0x00));
}

static void aes_expand(const uint8_t key[16], uint8_t rk[176]) {
    memcpy(rk, key, 16);
    for (int i = 4; i < 44; ++i) {
        uint8_t t[4];
        memcpy(t, rk + (i - 1) * 4, 4);
        if ((i % 4) == 0) {
            const uint8_t tmp = t[0];
            t[0] = (uint8_t)(kAesSbox[t[1]] ^ kAesRcon[i / 4 - 1]);
            t[1] = kAesSbox[t[2]];
            t[2] = kAesSbox[t[3]];
            t[3] = kAesSbox[tmp];
        }
        for (int j = 0; j < 4; ++j) {
            rk[i * 4 + j] = (uint8_t)(rk[(i - 4) * 4 + j] ^ t[j]);
        }
    }
}

static void aes_encrypt(const uint8_t rk[176], const uint8_t in[16], uint8_t out[16]) {
    uint8_t s[16];
    memcpy(s, in, 16);
    xor_block(s, rk, 16);

    for (int round = 1; round < 10; ++round) {
        uint8_t t[16];
        for (int i = 0; i < 16; ++i) {
            t[i] = kAesSbox[s[i]];
        }
        s[0] = t[0];
        s[1] = t[5];
        s[2] = t[10];
        s[3] = t[15];
        s[4] = t[4];
        s[5] = t[9];
        s[6] = t[14];
        s[7] = t[3];
        s[8] = t[8];
        s[9] = t[13];
        s[10] = t[2];
        s[11] = t[7];
        s[12] = t[12];
        s[13] = t[1];
        s[14] = t[6];
        s[15] = t[11];

        for (int i = 0; i < 4; ++i) {
            const uint8_t a0 = s[i * 4];
            const uint8_t a1 = s[i * 4 + 1];
            const uint8_t a2 = s[i * 4 + 2];
            const uint8_t a3 = s[i * 4 + 3];
            const uint8_t u = (uint8_t)(a0 ^ a1 ^ a2 ^ a3);
            const uint8_t v = a0;
            s[i * 4] = (uint8_t)(a0 ^ xtime((uint8_t)(a0 ^ a1)) ^ u);
            s[i * 4 + 1] = (uint8_t)(a1 ^ xtime((uint8_t)(a1 ^ a2)) ^ u);
            s[i * 4 + 2] = (uint8_t)(a2 ^ xtime((uint8_t)(a2 ^ a3)) ^ u);
            s[i * 4 + 3] = (uint8_t)(a3 ^ xtime((uint8_t)(a3 ^ v)) ^ u);
        }
        xor_block(s, rk + round * 16, 16);
    }

    uint8_t t[16];
    for (int i = 0; i < 16; ++i) {
        t[i] = kAesSbox[s[i]];
    }
    s[0] = t[0];
    s[1] = t[5];
    s[2] = t[10];
    s[3] = t[15];
    s[4] = t[4];
    s[5] = t[9];
    s[6] = t[14];
    s[7] = t[3];
    s[8] = t[8];
    s[9] = t[13];
    s[10] = t[2];
    s[11] = t[7];
    s[12] = t[12];
    s[13] = t[1];
    s[14] = t[6];
    s[15] = t[11];
    xor_block(s, rk + 160, 16);
    memcpy(out, s, 16);
}

static void gf128_mul(uint8_t x[16], const uint8_t y[16]) {
    uint8_t z[16];
    uint8_t v[16];
    memset(z, 0, 16);
    memcpy(v, y, 16);
    for (int i = 0; i < 128; ++i) {
        if ((x[i / 8] & (uint8_t)(1 << (7 - (i % 8)))) != 0) {
            xor_block(z, v, 16);
        }
        const uint8_t lsb = (uint8_t)(v[15] & 1);
        for (int j = 15; j > 0; --j) {
            v[j] = (uint8_t)((v[j] >> 1) | (v[j - 1] << 7));
        }
        v[0] = (uint8_t)(v[0] >> 1);
        if (lsb != 0) {
            v[0] ^= 0xe1;
        }
    }
    memcpy(x, z, 16);
}

static void ghash_block(uint8_t y[16], const uint8_t H[16], const uint8_t block[16]) {
    xor_block(y, block, 16);
    gf128_mul(y, H);
}

static void ghash_bytes(TTGcmCtx* ctx, const uint8_t* data, size_t len) {
    while (len > 0) {
        const size_t room = 16 - ctx->bufLen;
        const size_t take = len < room ? len : room;
        memcpy(ctx->buf + ctx->bufLen, data, take);
        ctx->bufLen += take;
        data += take;
        len -= take;
        if (ctx->bufLen == 16) {
            ghash_block(ctx->y, ctx->H, ctx->buf);
            ctx->bufLen = 0;
        }
    }
}

static void ghash_pad(TTGcmCtx* ctx) {
    if (ctx->bufLen > 0) {
        memset(ctx->buf + ctx->bufLen, 0, 16 - ctx->bufLen);
        ghash_block(ctx->y, ctx->H, ctx->buf);
        ctx->bufLen = 0;
    }
}

static void inc32(uint8_t ctr[16]) {
    for (int i = 15; i >= 12; --i) {
        if (++ctr[i] != 0) {
            break;
        }
    }
}

void tt_gcm_init(TTGcmCtx* ctx, const uint8_t key[16],
                 const uint8_t nonce[TT_GCM_NONCE], bool encrypt) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->encrypt = encrypt;
    aes_expand(key, ctx->rk);
    aes_encrypt(ctx->rk, ctx->H, ctx->H);
    memcpy(ctx->J0, nonce, 12);
    ctx->J0[15] = 1;
    memcpy(ctx->ctr, ctx->J0, 16);
    inc32(ctx->ctr);
}

void tt_gcm_aad(TTGcmCtx* ctx, const uint8_t* aad, size_t len) {
    ctx->aadBits += (uint64_t)len * 8;
    ghash_bytes(ctx, aad, len);
}

void tt_gcm_update(TTGcmCtx* ctx, const uint8_t* input, uint8_t* output, size_t len) {
    if (!ctx->ctStarted) {
        ghash_pad(ctx);
        ctx->ctStarted = true;
    }
    uint8_t ks[16];
    size_t offset = 0;
    while (offset < len) {
        aes_encrypt(ctx->rk, ctx->ctr, ks);
        inc32(ctx->ctr);
        const size_t take = (len - offset) < 16 ? (len - offset) : 16;
        if (!ctx->encrypt) {
            ghash_bytes(ctx, input + offset, take);
        }
        for (size_t i = 0; i < take; ++i) {
            output[offset + i] = (uint8_t)(input[offset + i] ^ ks[i]);
        }
        if (ctx->encrypt) {
            ghash_bytes(ctx, output + offset, take);
        }
        ctx->ctBits += (uint64_t)take * 8;
        offset += take;
    }
}

void tt_gcm_final(TTGcmCtx* ctx, uint8_t tag[TT_GCM_TAG_LEN]) {
    if (!ctx->ctStarted) {
        ghash_pad(ctx);
        ctx->ctStarted = true;
    }
    ghash_pad(ctx);
    uint8_t lenBlock[16];
    memset(lenBlock, 0, 16);
    store64_be(lenBlock, ctx->aadBits);
    store64_be(lenBlock + 8, ctx->ctBits);
    ghash_block(ctx->y, ctx->H, lenBlock);

    uint8_t s[16];
    aes_encrypt(ctx->rk, ctx->J0, s);
    for (int i = 0; i < 16; ++i) {
        tag[i] = (uint8_t)(s[i] ^ ctx->y[i]);
    }
}

bool tt_aes128_gcm(const uint8_t key[16], const uint8_t nonce[TT_GCM_NONCE],
                   const uint8_t* aad, size_t aadLen,
                   const uint8_t* input, uint8_t* output, size_t len,
                   uint8_t tag[TT_GCM_TAG_LEN], bool encrypt) {
    TTGcmCtx ctx;
    tt_gcm_init(&ctx, key, nonce, encrypt);
    if (aad != nullptr && aadLen > 0) {
        tt_gcm_aad(&ctx, aad, aadLen);
    }
    if (input != nullptr && len > 0) {
        tt_gcm_update(&ctx, input, output, len);
    }
    tt_gcm_final(&ctx, tag);
    return true;
}
