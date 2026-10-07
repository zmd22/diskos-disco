/* Minimal crypto for an AirPlay 1 sender: SHA-1, RSA-OAEP(SHA-1) encrypt with e=65537, AES-128-CBC encrypt, base64.
 * Self-contained, no dependencies. SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdint.h>
#include <string.h>

/* ---------- SHA-1 ---------- */
static uint32_t rol32(uint32_t x, int n){ return (x << n) | (x >> (32 - n)); }
static void sha1(const uint8_t *msg, size_t len, uint8_t out[20]){
    uint32_t h[5] = { 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0 };
    size_t total = ((len + 8) / 64 + 1) * 64;
    uint8_t blk[64];
    for(size_t off = 0; off < total; off += 64){
        for(int i = 0; i < 64; i++){
            size_t p = off + i; uint8_t b;
            if(p < len) b = msg[p];
            else if(p == len) b = 0x80;
            else if(p >= total - 8) b = (uint8_t)(((uint64_t)len * 8) >> (8 * (total - 1 - p)));
            else b = 0;
            blk[i] = b;
        }
        uint32_t w[80];
        for(int i = 0; i < 16; i++) w[i] = (uint32_t)blk[4*i] << 24 | blk[4*i+1] << 16 | blk[4*i+2] << 8 | blk[4*i+3];
        for(int i = 16; i < 80; i++) w[i] = rol32(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for(int i = 0; i < 80; i++){
            uint32_t f, k;
            if(i < 20){ f = (b & c) | (~b & d); k = 0x5A827999; }
            else if(i < 40){ f = b ^ c ^ d; k = 0x6ED9EBA1; }
            else if(i < 60){ f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
            else { f = b ^ c ^ d; k = 0xCA62C1D6; }
            uint32_t t = rol32(a, 5) + f + e + k + w[i];
            e = d; d = c; c = rol32(b, 30); b = a; a = t;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    }
    for(int i = 0; i < 5; i++){ out[4*i] = h[i] >> 24; out[4*i+1] = h[i] >> 16; out[4*i+2] = h[i] >> 8; out[4*i+3] = h[i]; }
}

/* ---------- base64 ---------- */
static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static int b64_dec(const char *s, uint8_t *out){
    int n = 0, bits = 0; uint32_t acc = 0;
    for(; *s && *s != '='; s++){
        const char *p = strchr(B64, *s); if(!p) continue;
        acc = acc << 6 | (uint32_t)(p - B64); bits += 6;
        if(bits >= 8){ bits -= 8; out[n++] = (uint8_t)(acc >> bits); }
    }
    return n;
}
static void b64_enc(const uint8_t *in, int len, char *out){   /* without '=' padding, as AirPlay senders do */
    int o = 0, bits = 0; uint32_t acc = 0;
    for(int i = 0; i < len; i++){
        acc = acc << 8 | in[i]; bits += 8;
        while(bits >= 6){ bits -= 6; out[o++] = B64[(acc >> bits) & 63]; }
    }
    if(bits) out[o++] = B64[(acc << (6 - bits)) & 63];
    out[o] = 0;
}

/* ---------- RSA (2048-bit, e = 65537) ---------- */
#define BN 64                                   /* 64 x 32-bit limbs = 2048 bits, little-endian limbs */
typedef struct { uint32_t v[BN]; } bn_t;
static void bn_from_bytes(bn_t *r, const uint8_t *b, int n){   /* big-endian bytes */
    memset(r, 0, sizeof *r);
    for(int i = 0; i < n; i++){ int bi = n - 1 - i; r->v[bi / 4] |= (uint32_t)b[i] << (8 * (bi % 4)); }
}
static void bn_to_bytes(const bn_t *a, uint8_t *b, int n){
    for(int i = 0; i < n; i++){ int bi = n - 1 - i; b[i] = (uint8_t)(a->v[bi / 4] >> (8 * (bi % 4))); }
}
/* r = a*b mod m, by shift-and-add over the bits of b (keeps everything below 2m) */
static int bn_cmp(const uint32_t *a, const uint32_t *b, int n){
    for(int i = n - 1; i >= 0; i--) if(a[i] != b[i]) return a[i] > b[i] ? 1 : -1;
    return 0;
}
static void bn_sub(uint32_t *a, const uint32_t *b, int n){
    uint64_t br = 0;
    for(int i = 0; i < n; i++){ uint64_t t = (uint64_t)a[i] - b[i] - br; a[i] = (uint32_t)t; br = (t >> 63) & 1; }
}
static void bn_mulmod(bn_t *r, const bn_t *a, const bn_t *b, const bn_t *m){
    uint32_t acc[BN + 1] = {0}, mm[BN + 1] = {0};
    memcpy(mm, m->v, sizeof m->v);
    for(int i = BN * 32 - 1; i >= 0; i--){
        uint32_t c = 0;                                          /* acc <<= 1 */
        for(int k = 0; k <= BN; k++){ uint32_t nc = acc[k] >> 31; acc[k] = acc[k] << 1 | c; c = nc; }
        if(bn_cmp(acc, mm, BN + 1) >= 0) bn_sub(acc, mm, BN + 1);
        if(b->v[i / 32] >> (i % 32) & 1){                        /* acc += a */
            uint64_t cy = 0;
            for(int k = 0; k <= BN; k++){ uint64_t t = (uint64_t)acc[k] + (k < BN ? a->v[k] : 0) + cy; acc[k] = (uint32_t)t; cy = t >> 32; }
            if(bn_cmp(acc, mm, BN + 1) >= 0) bn_sub(acc, mm, BN + 1);
        }
    }
    memcpy(r->v, acc, sizeof r->v);
}
static void rsa_pub(uint8_t out[256], const uint8_t in[256], const uint8_t mod[256]){
    bn_t m, x, r;
    bn_from_bytes(&m, mod, 256); bn_from_bytes(&x, in, 256);
    r = x;
    for(int i = 0; i < 16; i++) bn_mulmod(&r, &r, &r, &m);       /* x^(2^16) */
    bn_mulmod(&r, &r, &x, &m);                                     /* * x  -> x^65537 */
    bn_to_bytes(&r, out, 256);
}
static void mgf1(const uint8_t *seed, int sl, uint8_t *mask, int ml){
    uint8_t buf[300], h[20];
    for(int c = 0, o = 0; o < ml; c++){
        memcpy(buf, seed, sl); buf[sl] = c >> 24; buf[sl+1] = c >> 16; buf[sl+2] = c >> 8; buf[sl+3] = c;
        sha1(buf, sl + 4, h);
        for(int i = 0; i < 20 && o < ml; i++) mask[o++] = h[i];
    }
}
/* RSA-OAEP (SHA-1, empty label) of msg into out[256] */
static void rsa_oaep(uint8_t out[256], const uint8_t *msg, int ml, const uint8_t mod[256], const uint8_t seed[20]){
    uint8_t em[256] = {0}, db[235], dbm[235], sm[20];
    sha1((const uint8_t *)"", 0, db);                              /* lHash */
    memset(db + 20, 0, 235 - 20);
    db[235 - ml - 1] = 0x01; memcpy(db + 235 - ml, msg, ml);
    mgf1(seed, 20, dbm, 235);
    for(int i = 0; i < 235; i++) db[i] ^= dbm[i];
    mgf1(db, 235, sm, 20);
    em[0] = 0;
    for(int i = 0; i < 20; i++) em[1 + i] = seed[i] ^ sm[i];
    memcpy(em + 21, db, 235);
    rsa_pub(out, em, mod);
}

/* ---------- AES-128 encrypt ---------- */
static const uint8_t SBOX[256] = {
0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16};
typedef struct { uint8_t rk[176]; } aes_t;
static void aes_init(aes_t *a, const uint8_t key[16]){
    static const uint8_t rcon[10] = { 1, 2, 4, 8, 16, 32, 64, 128, 0x1b, 0x36 };
    memcpy(a->rk, key, 16);
    for(int i = 16; i < 176; i += 4){
        uint8_t t[4] = { a->rk[i-4], a->rk[i-3], a->rk[i-2], a->rk[i-1] };
        if(i % 16 == 0){ uint8_t u = t[0]; t[0] = SBOX[t[1]] ^ rcon[i/16 - 1]; t[1] = SBOX[t[2]]; t[2] = SBOX[t[3]]; t[3] = SBOX[u]; }
        for(int k = 0; k < 4; k++) a->rk[i+k] = a->rk[i-16+k] ^ t[k];
    }
}
static uint8_t xt(uint8_t x){ return (uint8_t)(x << 1) ^ ((x >> 7) * 0x1b); }
static void aes_block(const aes_t *a, uint8_t s[16]){
    for(int i = 0; i < 16; i++) s[i] ^= a->rk[i];
    for(int r = 1; r <= 10; r++){
        uint8_t t[16];
        for(int i = 0; i < 16; i++) t[i] = SBOX[s[i]];
        for(int c = 0; c < 4; c++) for(int rr = 0; rr < 4; rr++) s[4*c + rr] = t[4*((c + rr) % 4) + rr];   /* ShiftRows */
        if(r < 10) for(int c = 0; c < 4; c++){                                                            /* MixColumns */
            uint8_t *p = s + 4*c, a0 = p[0], a1 = p[1], a2 = p[2], a3 = p[3], all = a0 ^ a1 ^ a2 ^ a3;
            p[0] ^= all ^ xt(a0 ^ a1); p[1] ^= all ^ xt(a1 ^ a2); p[2] ^= all ^ xt(a2 ^ a3); p[3] ^= all ^ xt(a3 ^ a0);
        }
        for(int i = 0; i < 16; i++) s[i] ^= a->rk[16*r + i];
    }
}
/* Table-driven AES-128 encrypt (the classic T-table form): ~4x faster than the byte-wise rounds above, which matters
 * on the Disc's CPU at 125 packets a second. The tables are built once from SBOX. */
static uint32_t TE[4][256]; static int te_ready;
static void aes_tables(void){
    for(int i = 0; i < 256; i++){
        uint8_t s1 = SBOX[i], s2 = xt(s1), s3 = s2 ^ s1;
        uint32_t t = (uint32_t)s2 << 24 | (uint32_t)s1 << 16 | (uint32_t)s1 << 8 | s3;
        TE[0][i] = t; TE[1][i] = t >> 8 | t << 24; TE[2][i] = t >> 16 | t << 16; TE[3][i] = t >> 24 | t << 8;
    }
    te_ready = 1;
}
static void aes_block_fast(const aes_t *a, uint8_t b[16]){
    const uint8_t *rk = a->rk;
    #define LD(p) ((uint32_t)(p)[0] << 24 | (uint32_t)(p)[1] << 16 | (uint32_t)(p)[2] << 8 | (p)[3])
    uint32_t s0 = LD(b) ^ LD(rk), s1 = LD(b + 4) ^ LD(rk + 4), s2 = LD(b + 8) ^ LD(rk + 8), s3 = LD(b + 12) ^ LD(rk + 12);
    for(int r = 1; r < 10; r++){
        const uint8_t *k = rk + 16 * r;
        uint32_t t0 = TE[0][s0 >> 24] ^ TE[1][(s1 >> 16) & 255] ^ TE[2][(s2 >> 8) & 255] ^ TE[3][s3 & 255] ^ LD(k);
        uint32_t t1 = TE[0][s1 >> 24] ^ TE[1][(s2 >> 16) & 255] ^ TE[2][(s3 >> 8) & 255] ^ TE[3][s0 & 255] ^ LD(k + 4);
        uint32_t t2 = TE[0][s2 >> 24] ^ TE[1][(s3 >> 16) & 255] ^ TE[2][(s0 >> 8) & 255] ^ TE[3][s1 & 255] ^ LD(k + 8);
        uint32_t t3 = TE[0][s3 >> 24] ^ TE[1][(s0 >> 16) & 255] ^ TE[2][(s1 >> 8) & 255] ^ TE[3][s2 & 255] ^ LD(k + 12);
        s0 = t0; s1 = t1; s2 = t2; s3 = t3;
    }
    const uint8_t *k = rk + 160;
    uint32_t o[4] = {
        ((uint32_t)SBOX[s0 >> 24] << 24 | (uint32_t)SBOX[(s1 >> 16) & 255] << 16 | (uint32_t)SBOX[(s2 >> 8) & 255] << 8 | SBOX[s3 & 255]) ^ LD(k),
        ((uint32_t)SBOX[s1 >> 24] << 24 | (uint32_t)SBOX[(s2 >> 16) & 255] << 16 | (uint32_t)SBOX[(s3 >> 8) & 255] << 8 | SBOX[s0 & 255]) ^ LD(k + 4),
        ((uint32_t)SBOX[s2 >> 24] << 24 | (uint32_t)SBOX[(s3 >> 16) & 255] << 16 | (uint32_t)SBOX[(s0 >> 8) & 255] << 8 | SBOX[s1 & 255]) ^ LD(k + 8),
        ((uint32_t)SBOX[s3 >> 24] << 24 | (uint32_t)SBOX[(s0 >> 16) & 255] << 16 | (uint32_t)SBOX[(s1 >> 8) & 255] << 8 | SBOX[s2 & 255]) ^ LD(k + 12) };
    #undef LD
    for(int i = 0; i < 4; i++){ b[4*i] = o[i] >> 24; b[4*i+1] = o[i] >> 16; b[4*i+2] = o[i] >> 8; b[4*i+3] = o[i]; }
}
/* AirPlay: CBC over the whole 16-byte blocks of each packet, IV restarted per packet, tail left in the clear */
static void aes_cbc_packet(const aes_t *a, const uint8_t iv[16], uint8_t *buf, int len){
    uint8_t prev[16]; memcpy(prev, iv, 16);
    for(int o = 0; o + 16 <= len; o += 16){
        for(int i = 0; i < 16; i++) buf[o+i] ^= prev[i];
        if(!te_ready) aes_tables();
        aes_block_fast(a, buf + o);
        memcpy(prev, buf + o, 16);
    }
}
