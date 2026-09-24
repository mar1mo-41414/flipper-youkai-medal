#include "ym_crypto.h"

#include <string.h>

/* ---------- SHA-256 (FAP からファームの mbedtls は使えないので自前) ---------- */

typedef struct {
    uint32_t h[8];
    uint8_t buf[64];
    uint64_t len;
    size_t fill;
} Sha256;

static const uint32_t K256[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void sha256_block(Sha256* s, const uint8_t* p) {
    uint32_t w[64];
    for(int i = 0; i < 16; i++) {
        w[i] = (uint32_t)p[i * 4] << 24 | (uint32_t)p[i * 4 + 1] << 16 | (uint32_t)p[i * 4 + 2] << 8 |
               p[i * 4 + 3];
    }
    for(int i = 16; i < 64; i++) {
        uint32_t s0 = ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = s->h[0], b = s->h[1], c = s->h[2], d = s->h[3];
    uint32_t e = s->h[4], f = s->h[5], g = s->h[6], h = s->h[7];
    for(int i = 0; i < 64; i++) {
        uint32_t t1 = h + (ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25)) + ((e & f) ^ (~e & g)) + K256[i] + w[i];
        uint32_t t2 = (ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    s->h[0] += a;
    s->h[1] += b;
    s->h[2] += c;
    s->h[3] += d;
    s->h[4] += e;
    s->h[5] += f;
    s->h[6] += g;
    s->h[7] += h;
}

static void sha256_init(Sha256* s) {
    static const uint32_t iv[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    memcpy(s->h, iv, sizeof(iv));
    s->len = 0;
    s->fill = 0;
}

static void sha256_update(Sha256* s, const uint8_t* p, size_t n) {
    s->len += n;
    while(n) {
        size_t take = 64 - s->fill;
        if(take > n) take = n;
        memcpy(s->buf + s->fill, p, take);
        s->fill += take;
        p += take;
        n -= take;
        if(s->fill == 64) {
            sha256_block(s, s->buf);
            s->fill = 0;
        }
    }
}

static void sha256_final(Sha256* s, uint8_t out[32]) {
    uint64_t bits = s->len * 8;
    uint8_t pad = 0x80;
    sha256_update(s, &pad, 1);
    pad = 0;
    while(s->fill != 56) sha256_update(s, &pad, 1);
    uint8_t lb[8];
    for(int i = 0; i < 8; i++) lb[i] = (uint8_t)(bits >> (56 - 8 * i));
    sha256_update(s, lb, 8);
    for(int i = 0; i < 8; i++) {
        out[i * 4] = s->h[i] >> 24;
        out[i * 4 + 1] = s->h[i] >> 16;
        out[i * 4 + 2] = s->h[i] >> 8;
        out[i * 4 + 3] = s->h[i];
    }
}

/* 鍵は 64 バイト未満の前提 (ここで使う鍵は 10 バイト) */
static void hmac_sha256(const uint8_t* key, size_t klen, const uint8_t* msg, size_t mlen, uint8_t out[32]) {
    uint8_t pad[64];
    uint8_t inner[32];
    Sha256 s;

    memset(pad, 0x36, sizeof(pad));
    for(size_t i = 0; i < klen; i++) pad[i] ^= key[i];
    sha256_init(&s);
    sha256_update(&s, pad, 64);
    sha256_update(&s, msg, mlen);
    sha256_final(&s, inner);

    memset(pad, 0x5c, sizeof(pad));
    for(size_t i = 0; i < klen; i++) pad[i] ^= key[i];
    sha256_init(&s);
    sha256_update(&s, pad, 64);
    sha256_update(&s, inner, 32);
    sha256_final(&s, out);
}

/* ---------- AES-128 (暗号化方向のみ。CTR なので復号にもこれを使う) ---------- */

static const uint8_t SBOX_AES[256] = {
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
    0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16,
};

static uint8_t xtime(uint8_t x) {
    return (uint8_t)((x << 1) ^ ((x & 0x80) ? 0x1b : 0));
}

static void aes128_expand(const uint8_t key[16], uint8_t rk[176]) {
    memcpy(rk, key, 16);
    uint8_t rcon = 1;
    for(int i = 16; i < 176; i += 4) {
        uint8_t t[4] = {rk[i - 4], rk[i - 3], rk[i - 2], rk[i - 1]};
        if(i % 16 == 0) {
            uint8_t u = t[0];
            t[0] = SBOX_AES[t[1]] ^ rcon;
            t[1] = SBOX_AES[t[2]];
            t[2] = SBOX_AES[t[3]];
            t[3] = SBOX_AES[u];
            rcon = xtime(rcon);
        }
        for(int j = 0; j < 4; j++) rk[i + j] = rk[i - 16 + j] ^ t[j];
    }
}

static void aes128_encrypt_block(const uint8_t rk[176], const uint8_t in[16], uint8_t out[16]) {
    uint8_t s[16];
    for(int i = 0; i < 16; i++) s[i] = in[i] ^ rk[i];
    for(int r = 1; r <= 10; r++) {
        uint8_t t[16];
        /* SubBytes + ShiftRows (列優先: s[col*4+row]) */
        for(int c = 0; c < 4; c++)
            for(int rr = 0; rr < 4; rr++) t[c * 4 + rr] = SBOX_AES[s[((c + rr) % 4) * 4 + rr]];
        if(r != 10) {
            for(int c = 0; c < 4; c++) {
                uint8_t* col = &t[c * 4];
                uint8_t a0 = col[0], a1 = col[1], a2 = col[2], a3 = col[3];
                uint8_t all = a0 ^ a1 ^ a2 ^ a3;
                col[0] ^= all ^ xtime(a0 ^ a1);
                col[1] ^= all ^ xtime(a1 ^ a2);
                col[2] ^= all ^ xtime(a2 ^ a3);
                col[3] ^= all ^ xtime(a3 ^ a0);
            }
        }
        for(int i = 0; i < 16; i++) s[i] = t[i] ^ rk[r * 16 + i];
    }
    memcpy(out, s, 16);
}

void ym_aes128_ctr(const uint8_t key[16], const uint8_t iv[16], const uint8_t* in, uint8_t* out, size_t len) {
    uint8_t rk[176];
    uint8_t ctr[16];
    uint8_t ks[16];
    aes128_expand(key, rk);
    memcpy(ctr, iv, 16);
    for(size_t off = 0; off < len; off += 16) {
        aes128_encrypt_block(rk, ctr, ks);
        for(size_t i = 0; i < 16 && off + i < len; i++) out[off + i] = in[off + i] ^ ks[i];
        for(int i = 15; i >= 0; i--) /* 128bit ビッグエンディアンのカウンタ */
            if(++ctr[i]) break;
    }
}

/* ---------- ゲームごとの鍵導出 ---------- */

typedef struct {
    const char* key1;
    const char* key2;
    uint8_t sbox[16];
    uint8_t data_first_page;
} YmParams;

static const YmParams PARAMS[] = {
    [YmGameYw3] = {"SjqjE90z8w", "bYYw75Ks9K", {14, 2, 11, 7, 5, 0, 12, 8, 10, 13, 15, 4, 6, 3, 1, 9}, 6},
    [YmGameYw4] = {"DKtjn3JAZc", "5g9D63n8Mt", {4, 9, 15, 0, 5, 10, 14, 1, 6, 11, 13, 2, 7, 12, 3, 8}, 28},
};

void ym_derive(YmGame game, const uint8_t uid[YM_UID_LEN], YmKeys* out) {
    const YmParams* p = &PARAMS[game];
    uint8_t h1[32];
    uint8_t h2[32];
    hmac_sha256((const uint8_t*)p->key1, strlen(p->key1), uid, YM_UID_LEN, h1);
    for(int i = 0; i < 32; i++) h1[i] = (uint8_t)(p->sbox[h1[i] & 0xF] | (p->sbox[h1[i] >> 4] << 4));
    hmac_sha256((const uint8_t*)p->key2, strlen(p->key2), h1, 32, h2);

    memcpy(out->pwd, &h2[28], 4);
    memcpy(out->key, h2, 16);
    /* IV = (h2[24:32] || UID || 00) XOR (h2[0:15] || 00) */
    memcpy(out->iv, &h2[24], 8);
    memcpy(&out->iv[8], uid, 7);
    out->iv[15] = 0;
    for(int i = 0; i < 15; i++) out->iv[i] ^= h2[i];
}

bool ym_decrypt_data(YmGame game, const uint8_t uid[YM_UID_LEN], const uint8_t in[YM_DATA_LEN], uint8_t out[YM_DATA_LEN]) {
    YmKeys k;
    ym_derive(game, uid, &k);
    ym_aes128_ctr(k.key, k.iv, in, out, YM_DATA_LEN);
    bool ok = true;
    for(int b = 0; b < YM_DATA_LEN; b += 16) {
        uint8_t sum = 0;
        for(int i = 0; i < 15; i++) sum += out[b + i];
        if(sum != out[b + 15]) ok = false;
    }
    return ok;
}

uint8_t ym_data_first_page(YmGame game) {
    return PARAMS[game].data_first_page;
}
