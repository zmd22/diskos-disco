/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Sendspin 1.0 security for the MA Sendspin player: see sendspin_crypto.h. The CPace generator and field arithmetic
 * follow draft-irtf-cfrg-cpace-21 as Sendspin instantiates it (pairing.md "PAKE"); tests/sendspin_crypto_test.c holds
 * the known-answer vectors (the spec's own, and sendspin-cpp's / aiosendspin's). */
#include "sendspin_crypto.h"
#include "vendor/monocypher/monocypher.h"
#include "vendor/monocypher/monocypher-ed25519.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/random.h>
#include <sys/stat.h>

/* ---------------------------------------------------------------- primitives */
int ssc_random(void *buf, size_t n){
    uint8_t *p = buf;
    while(n){
        ssize_t r = getrandom(p, n, 0);
        if(r < 0){
            if(errno == EINTR) continue;
            int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC); if(fd < 0) return -1;   /* very old kernels */
            while(n){ ssize_t k = read(fd, p, n); if(k <= 0){ if(k < 0 && errno == EINTR) continue; close(fd); return -1; } p += k; n -= (size_t)k; }
            close(fd); return 0;
        }
        p += r; n -= (size_t)r;
    }
    return 0;
}
void ssc_wipe(void *p, size_t n){ crypto_wipe(p, n); }
int ssc_equal(const void *a, const void *b, size_t n){
    const uint8_t *x = a, *y = b; uint8_t d = 0;
    for(size_t i = 0; i < n; i++) d |= x[i] ^ y[i];
    return d == 0;
}

static const uint32_t K256[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,
    0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,
    0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,
    0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2 };
#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
static void sha256_block(ssc_sha256_ctx *c, const uint8_t *p){
    uint32_t w[64], a, b, cc, d, e, f, g, h;
    for(int i = 0; i < 16; i++) w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
    for(int i = 16; i < 64; i++){
        uint32_t s0 = ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3), s1 = ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    a = c->h[0]; b = c->h[1]; cc = c->h[2]; d = c->h[3]; e = c->h[4]; f = c->h[5]; g = c->h[6]; h = c->h[7];
    for(int i = 0; i < 64; i++){
        uint32_t t1 = h + (ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25)) + ((e & f) ^ (~e & g)) + K256[i] + w[i];
        uint32_t t2 = (ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22)) + ((a & b) ^ (a & cc) ^ (b & cc));
        h = g; g = f; f = e; e = d + t1; d = cc; cc = b; b = a; a = t1 + t2;
    }
    c->h[0] += a; c->h[1] += b; c->h[2] += cc; c->h[3] += d; c->h[4] += e; c->h[5] += f; c->h[6] += g; c->h[7] += h;
}
void ssc_sha256_init(ssc_sha256_ctx *c){
    static const uint32_t iv[8] = { 0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19 };
    memcpy(c->h, iv, sizeof iv); c->len = 0; c->n = 0;
}
void ssc_sha256_update(ssc_sha256_ctx *c, const void *d, size_t n){
    const uint8_t *p = d; c->len += n;
    while(n){
        size_t k = 64 - c->n; if(k > n) k = n;
        memcpy(c->buf + c->n, p, k); c->n += k; p += k; n -= k;
        if(c->n == 64){ sha256_block(c, c->buf); c->n = 0; }
    }
}
void ssc_sha256_final(ssc_sha256_ctx *c, uint8_t out[32]){
    uint64_t bits = c->len * 8; uint8_t pad = 0x80, z = 0;
    ssc_sha256_update(c, &pad, 1);
    while(c->n != 56) ssc_sha256_update(c, &z, 1);
    uint8_t l[8]; for(int i = 0; i < 8; i++) l[i] = (uint8_t)(bits >> (56 - 8 * i));
    ssc_sha256_update(c, l, 8);
    for(int i = 0; i < 8; i++){ out[4 * i] = c->h[i] >> 24; out[4 * i + 1] = c->h[i] >> 16; out[4 * i + 2] = c->h[i] >> 8; out[4 * i + 3] = c->h[i]; }
    crypto_wipe(c, sizeof *c);
}
void ssc_sha256(const void *d, size_t n, uint8_t out[32]){ ssc_sha256_ctx c; ssc_sha256_init(&c); ssc_sha256_update(&c, d, n); ssc_sha256_final(&c, out); }
static void hmac2(const uint8_t key[32], const void *a, size_t an, const void *b, size_t bn, uint8_t out[32]){
    uint8_t k[64] = {0}, ih[32]; memcpy(k, key, 32);
    ssc_sha256_ctx c;
    for(int i = 0; i < 64; i++) k[i] ^= 0x36;
    ssc_sha256_init(&c); ssc_sha256_update(&c, k, 64); ssc_sha256_update(&c, a, an); if(bn) ssc_sha256_update(&c, b, bn); ssc_sha256_final(&c, ih);
    for(int i = 0; i < 64; i++) k[i] ^= 0x36 ^ 0x5c;
    ssc_sha256_init(&c); ssc_sha256_update(&c, k, 64); ssc_sha256_update(&c, ih, 32); ssc_sha256_final(&c, out);
    crypto_wipe(k, sizeof k); crypto_wipe(ih, sizeof ih);
}
void ssc_hmac_sha256(const uint8_t key[32], const void *d, size_t n, uint8_t out[32]){ hmac2(key, d, n, NULL, 0, out); }
void ssc_sha512(const void *d, size_t n, uint8_t out[64]){ crypto_sha512(out, d, n); }

static void poly_pad(crypto_poly1305_ctx *p, size_t n){ static const uint8_t z[16]; if(n % 16) crypto_poly1305_update(p, z, 16 - n % 16); }
static void aead_tag(const uint8_t k[32], const uint8_t nonce[12], const uint8_t *ad, size_t adn, const uint8_t *ct, size_t len, uint8_t tag[16]){
    uint8_t pk[64]; crypto_chacha20_ietf(pk, NULL, 64, k, nonce, 0);
    crypto_poly1305_ctx p; crypto_poly1305_init(&p, pk);
    if(adn) crypto_poly1305_update(&p, ad, adn);
    poly_pad(&p, adn);
    if(len) crypto_poly1305_update(&p, ct, len);
    poly_pad(&p, len);
    uint8_t l[16]; for(int i = 0; i < 8; i++){ l[i] = (uint8_t)((uint64_t)adn >> (8 * i)); l[8 + i] = (uint8_t)((uint64_t)len >> (8 * i)); }
    crypto_poly1305_update(&p, l, 16); crypto_poly1305_final(&p, tag);
    crypto_wipe(pk, sizeof pk);
}
static void noise_nonce(uint64_t n, uint8_t nonce[12]){ memset(nonce, 0, 4); for(int i = 0; i < 8; i++) nonce[4 + i] = (uint8_t)(n >> (8 * i)); }
void ssc_aead_seal(const uint8_t k[32], uint64_t n, const uint8_t *ad, size_t adn, const uint8_t *pt, size_t len, uint8_t *out){
    uint8_t nonce[12]; noise_nonce(n, nonce);
    if(len) crypto_chacha20_ietf(out, pt, len, k, nonce, 1);
    aead_tag(k, nonce, ad, adn, out, len, out + len);
}
int ssc_aead_open(const uint8_t k[32], uint64_t n, const uint8_t *ad, size_t adn, const uint8_t *ct, size_t len, uint8_t *out){
    if(len < 16) return -1;
    uint8_t nonce[12], tag[16]; noise_nonce(n, nonce);
    aead_tag(k, nonce, ad, adn, ct, len - 16, tag);
    if(crypto_verify16(tag, ct + len - 16)) return -1;
    if(len > 16) crypto_chacha20_ietf(out, ct, len - 16, k, nonce, 1);
    return 0;
}

static const char B64U[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
int ssc_b64url_enc(const uint8_t *in, size_t n, char *out){
    int o = 0; size_t i = 0;
    for(; i + 2 < n; i += 3){
        uint32_t v = (uint32_t)in[i] << 16 | (uint32_t)in[i + 1] << 8 | in[i + 2];
        out[o++] = B64U[v >> 18]; out[o++] = B64U[(v >> 12) & 63]; out[o++] = B64U[(v >> 6) & 63]; out[o++] = B64U[v & 63];
    }
    if(n - i == 1){ uint32_t v = (uint32_t)in[i] << 16; out[o++] = B64U[v >> 18]; out[o++] = B64U[(v >> 12) & 63]; }
    else if(n - i == 2){ uint32_t v = (uint32_t)in[i] << 16 | (uint32_t)in[i + 1] << 8; out[o++] = B64U[v >> 18]; out[o++] = B64U[(v >> 12) & 63]; out[o++] = B64U[(v >> 6) & 63]; }
    out[o] = 0; return o;
}
int ssc_b64url_dec(const char *s, size_t sn, uint8_t *out, size_t max){
    if(sn % 4 == 1) return -1;
    uint32_t acc = 0; int bits = 0; size_t o = 0;
    for(size_t i = 0; i < sn; i++){
        int c = (unsigned char)s[i], v;
        if(c >= 'A' && c <= 'Z') v = c - 'A'; else if(c >= 'a' && c <= 'z') v = c - 'a' + 26;
        else if(c >= '0' && c <= '9') v = c - '0' + 52; else if(c == '-') v = 62; else if(c == '_') v = 63; else return -1;
        acc = acc << 6 | (uint32_t)v; bits += 6;
        if(bits >= 8){ bits -= 8; if(o >= max) return -1; out[o++] = (uint8_t)(acc >> bits); }
    }
    return (int)o;
}

/* ---------------------------------------------------------------- Noise_KKpsk2_25519_ChaChaPoly_SHA256 */
static void noise_hkdf(const uint8_t ck[32], const uint8_t *ikm, size_t n, uint8_t *o1, uint8_t *o2, uint8_t *o3){
    /* Noise HKDF: tk = HMAC(ck, ikm); out1 = HMAC(tk, 0x01); out2 = HMAC(tk, out1 || 0x02); out3 = HMAC(tk, out2 || 0x03) */
    uint8_t tk[32], a[32], b[32], i1 = 1, i2 = 2, i3 = 3;
    ssc_hmac_sha256(ck, ikm, n, tk);
    hmac2(tk, &i1, 1, NULL, 0, a);
    hmac2(tk, a, 32, &i2, 1, b);
    if(o1) memcpy(o1, a, 32);
    if(o3) hmac2(tk, b, 32, &i3, 1, o3);
    if(o2) memcpy(o2, b, 32);
    crypto_wipe(tk, 32); crypto_wipe(a, 32); crypto_wipe(b, 32);
}
static void mix_hash(ssc_noise *hs, const void *d, size_t n){
    ssc_sha256_ctx c; ssc_sha256_init(&c); ssc_sha256_update(&c, hs->h, 32); ssc_sha256_update(&c, d, n); ssc_sha256_final(&c, hs->h);
}
static void mix_key(ssc_noise *hs, const uint8_t *ikm, size_t n){
    uint8_t ck[32]; noise_hkdf(hs->ck, ikm, n, ck, hs->cs.k, NULL); memcpy(hs->ck, ck, 32); hs->cs.n = 0; hs->cs.has_k = 1; crypto_wipe(ck, 32);
}
static void mix_key_and_hash(ssc_noise *hs, const uint8_t *ikm, size_t n){
    uint8_t ck[32], th[32]; noise_hkdf(hs->ck, ikm, n, ck, th, hs->cs.k); memcpy(hs->ck, ck, 32);
    mix_hash(hs, th, 32); hs->cs.n = 0; hs->cs.has_k = 1; crypto_wipe(ck, 32); crypto_wipe(th, 32);
}
static int dh(const uint8_t sk[32], const uint8_t pk[32], uint8_t out[32]){
    static const uint8_t zero[32];
    crypto_x25519(out, sk, pk);
    return crypto_verify32(out, zero) ? 0 : -1;     /* an all-zero result is a low-order peer key */
}
void ssc_noise_init(ssc_noise *hs, const uint8_t *prologue, size_t pn, const uint8_t s_priv[32], const uint8_t s_pub[32], const uint8_t rs[32]){
    static const char name[] = "Noise_KKpsk2_25519_ChaChaPoly_SHA256";       /* longer than HASHLEN: h = HASH(name) */
    memset(hs, 0, sizeof *hs);
    ssc_sha256(name, sizeof name - 1, hs->h); memcpy(hs->ck, hs->h, 32);
    mix_hash(hs, prologue, pn);
    memcpy(hs->s_priv, s_priv, 32); memcpy(hs->s_pub, s_pub, 32); memcpy(hs->rs, rs, 32);
    mix_hash(hs, rs, 32);                 /* pre-messages: -> s (the server, initiator), <- s (us) */
    mix_hash(hs, s_pub, 32);
}
static int encrypt_and_hash(ssc_noise *hs, const uint8_t *pt, size_t n, uint8_t *out){
    if(!hs->cs.has_k){ memmove(out, pt, n); mix_hash(hs, out, n); return (int)n; }
    ssc_aead_seal(hs->cs.k, hs->cs.n++, hs->h, 32, pt, n, out);
    mix_hash(hs, out, n + 16); return (int)n + 16;
}
static int decrypt_and_hash(ssc_noise *hs, const uint8_t *ct, size_t n, uint8_t *out){
    if(!hs->cs.has_k){ memmove(out, ct, n); mix_hash(hs, ct, n); return (int)n; }
    if(n < 16 || ssc_aead_open(hs->cs.k, hs->cs.n, hs->h, 32, ct, n, out)) return -1;
    hs->cs.n++; mix_hash(hs, ct, n); return (int)n - 16;
}
int ssc_noise_read1(ssc_noise *hs, const uint8_t *msg, size_t n, uint8_t *payload, size_t cap, size_t *pn){
    uint8_t k[32];
    if(n < 32 + 16 || n - 32 - 16 > cap) return -1;
    memcpy(hs->re, msg, 32);
    mix_hash(hs, hs->re, 32); mix_key(hs, hs->re, 32);             /* e (a psk handshake also mixes e into the key) */
    if(dh(hs->s_priv, hs->re, k)) return -1;    /* es: responder DH(s, re) */
    mix_key(hs, k, 32);
    if(dh(hs->s_priv, hs->rs, k)) return -1;    /* ss */
    mix_key(hs, k, 32);
    crypto_wipe(k, 32);
    int r = decrypt_and_hash(hs, msg + 32, n - 32, payload); if(r < 0) return -1;
    *pn = (size_t)r; return 0;
}
int ssc_noise_write2(ssc_noise *hs, const uint8_t psk[32], const uint8_t *payload, size_t pn, uint8_t *out, size_t *outn){
    uint8_t k[32];
    if(ssc_random(hs->e_priv, 32)) return -1;
    crypto_x25519_public_key(hs->e_pub, hs->e_priv);
    memcpy(out, hs->e_pub, 32);
    mix_hash(hs, hs->e_pub, 32); mix_key(hs, hs->e_pub, 32);        /* e */
    if(dh(hs->e_priv, hs->re, k)) return -1;    /* ee */
    mix_key(hs, k, 32);
    if(dh(hs->e_priv, hs->rs, k)) return -1;    /* se: responder DH(e, rs) */
    mix_key(hs, k, 32);
    crypto_wipe(k, 32);
    mix_key_and_hash(hs, psk, 32);                                  /* psk */
    int r = encrypt_and_hash(hs, payload, pn, out + 32); if(r < 0) return -1;
    *outn = 32 + (size_t)r; return 0;
}
void ssc_noise_split(ssc_noise *hs, ssc_cipher *rx, ssc_cipher *tx, uint8_t h[32]){
    memset(rx, 0, sizeof *rx); memset(tx, 0, sizeof *tx);
    noise_hkdf(hs->ck, NULL, 0, rx->k, tx->k, NULL);              /* k1: initiator -> responder, k2: responder -> initiator */
    rx->has_k = tx->has_k = 1;
    memcpy(h, hs->h, 32);
    crypto_wipe(hs, sizeof *hs);
}
int ssc_cipher_seal(ssc_cipher *c, const uint8_t *pt, size_t n, uint8_t *out){
    if(!c->has_k || c->n == UINT64_MAX) return -1;
    ssc_aead_seal(c->k, c->n++, NULL, 0, pt, n, out); return 0;
}
int ssc_cipher_open(ssc_cipher *c, const uint8_t *ct, size_t n, uint8_t *out){
    if(!c->has_k || c->n == UINT64_MAX || ssc_aead_open(c->k, c->n, NULL, 0, ct, n, out)) return -1;
    c->n++; return 0;
}

/* ---------------------------------------------------------------- PSKs */
const uint8_t SSC_SENTINEL_PSK[32] = {     /* SHA-256("sendspin-sentinel-psk-v1"), connection.md */
    0x1b,0x5e,0x24,0xdb,0xc1,0xae,0xd9,0x5f,0xc2,0xa5,0xa3,0x38,0xa9,0x0c,0x05,0xdf,
    0x44,0xbd,0x10,0xf5,0xec,0x1f,0x4c,0xd6,0x6c,0xbf,0x86,0x27,0x27,0x67,0xb9,0xd3 };
void ssc_psk_id(const uint8_t psk[32], char out[44]){
    static const char label[] = "sendspin-psk-id-v1";
    uint8_t d[32]; ssc_sha256_ctx c; ssc_sha256_init(&c); ssc_sha256_update(&c, label, sizeof label - 1); ssc_sha256_update(&c, psk, 32); ssc_sha256_final(&c, d);
    ssc_b64url_enc(d, 32, out);
}

/* ---------------------------------------------------------------- GF(2^255 - 19), TweetNaCl-style 16 x 16-bit limbs
 * Only the CPace generator needs it (Elligator 2, once per pairing round): small and plain, not fast. */
typedef int64_t gf[16];
static void gf_car(gf o){
    for(int i = 0; i < 16; i++){
        o[i] += (int64_t)1 << 16;
        int64_t c = o[i] >> 16;
        o[(i + 1) * (i < 15)] += c - 1 + 37 * (c - 1) * (i == 15);
        o[i] -= c * ((int64_t)1 << 16);
    }
}
static void gf_sel(gf p, gf q, int b){ int64_t c = ~(int64_t)(b - 1); for(int i = 0; i < 16; i++){ int64_t t = c & (p[i] ^ q[i]); p[i] ^= t; q[i] ^= t; } }
static void gf_pack(uint8_t o[32], const gf n){
    gf m, t; memcpy(t, n, sizeof t);
    gf_car(t); gf_car(t); gf_car(t);
    for(int j = 0; j < 2; j++){
        m[0] = t[0] - 0xffed;
        for(int i = 1; i < 15; i++){ m[i] = t[i] - 0xffff - ((m[i - 1] >> 16) & 1); m[i - 1] &= 0xffff; }
        m[15] = t[15] - 0x7fff - ((m[14] >> 16) & 1);
        int b = (int)((m[15] >> 16) & 1); m[14] &= 0xffff;
        gf_sel(t, m, 1 - b);
    }
    for(int i = 0; i < 16; i++){ o[2 * i] = (uint8_t)(t[i] & 0xff); o[2 * i + 1] = (uint8_t)(t[i] >> 8); }
}
static void gf_unpack(gf o, const uint8_t n[32]){ for(int i = 0; i < 16; i++) o[i] = n[2 * i] + ((int64_t)n[2 * i + 1] << 8); }   /* all 256 bits */
static void gf_add(gf o, const gf a, const gf b){ for(int i = 0; i < 16; i++) o[i] = a[i] + b[i]; }
static void gf_sub(gf o, const gf a, const gf b){ for(int i = 0; i < 16; i++) o[i] = a[i] - b[i]; }
static void gf_mul(gf o, const gf a, const gf b){
    int64_t t[31] = {0};
    for(int i = 0; i < 16; i++) for(int j = 0; j < 16; j++) t[i + j] += a[i] * b[j];
    for(int i = 0; i < 15; i++) t[i] += 38 * t[i + 16];
    for(int i = 0; i < 16; i++) o[i] = t[i];
    gf_car(o); gf_car(o);
}
static void gf_set(gf o, int64_t v){ memset(o, 0, sizeof(gf)); o[0] = v & 0xffff; o[1] = v >> 16; }
static void gf_pow(gf o, const gf a, const uint8_t e[32]){      /* e little-endian, below 2^255 */
    gf c; gf_set(c, 1);
    for(int i = 254; i >= 0; i--){ gf_mul(c, c, c); if((e[i / 8] >> (i % 8)) & 1) gf_mul(c, c, a); }
    memcpy(o, c, sizeof c);
}
void ssc_elligator2(const uint8_t r_le[32], uint8_t u[32]){
    static const uint8_t P_MINUS_2[32] = { 0xeb,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
                                           0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0x7f };
    static const uint8_t P_MINUS_1_HALF[32] = { 0xf6,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
                                                0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0x3f };
    gf r, r2, one, two, a, ahalf, zero, den, inv, v, v2, v3, t, poly, eps, x, ome;
    gf_unpack(r, r_le);
    gf_set(one, 1); gf_set(two, 2); gf_set(a, 486662); gf_set(ahalf, 243331); gf_set(zero, 0);
    gf_mul(r2, r, r); gf_mul(t, r2, two); gf_add(den, one, t);                 /* 1 + Z r^2, Z = 2 */
    gf_pow(inv, den, P_MINUS_2);
    gf_mul(t, a, inv); gf_sub(v, zero, t);                                     /* v = -A / (1 + Z r^2) */
    gf_mul(v2, v, v); gf_mul(v3, v2, v); gf_mul(t, a, v2);
    gf_add(poly, v3, t); gf_add(poly, poly, v);                                /* v^3 + A v^2 + v */
    gf_pow(eps, poly, P_MINUS_1_HALF);                                         /* Legendre symbol: 0, 1 or -1 */
    gf_mul(x, eps, v); gf_sub(ome, one, eps); gf_mul(t, ome, ahalf); gf_sub(x, x, t);   /* eps v - (1 - eps) A / 2 */
    gf_pack(u, x);
}

/* ---------------------------------------------------------------- CPace */
static size_t lv(uint8_t *o, const void *d, size_t n){        /* prepend_len: little-endian base-128 length, then data */
    size_t k = 0, l = n;
    do { o[k++] = (uint8_t)((l < 128 ? l : (l & 0x7f) | 0x80)); l >>= 7; } while(l);
    if(n) memcpy(o + k, d, n);
    return k + n;
}
void ssc_cpace_generator(const uint8_t *prs, size_t prsn, const uint8_t *sid, size_t sidn, uint8_t g[32]){
    uint8_t buf[512]; size_t o = 0;
    if(prsn > 120 || sidn > 120){ memset(g, 0, 32); return; }
    static const char dsi[] = "CPace255";
    size_t lv_dsi = 1 + sizeof dsi - 1, lv_prs = (prsn < 128 ? 1 : 2) + prsn;
    size_t zp = 128 > lv_dsi + lv_prs + 1 ? 128 - (lv_dsi + lv_prs + 1) : 0;   /* fills the first SHA-512 block */
    uint8_t zeros[128] = {0};
    o += lv(buf + o, dsi, sizeof dsi - 1); o += lv(buf + o, prs, prsn); o += lv(buf + o, zeros, zp);
    o += lv(buf + o, NULL, 0);                                                 /* CI: empty */
    o += lv(buf + o, sid, sidn);
    uint8_t h[64]; ssc_sha512(buf, o, h);
    h[31] &= 0x7f;                                                             /* decode_u: clear the unused top bit */
    ssc_elligator2(h, g);
    crypto_wipe(buf, sizeof buf); crypto_wipe(h, sizeof h);
}
static size_t lv_share_ad(uint8_t *o, const uint8_t y[32], const char *ad){ size_t k = lv(o, y, 32); return k + lv(o + k, ad, strlen(ad)); }
int ssc_cpace_respond(ssc_cpace *cp, const uint8_t *prs, size_t prsn, const uint8_t *sid, size_t sidn, const uint8_t ya[32]){
    static const uint8_t zero[32];
    uint8_t g[32], y[32], k[32], buf[256]; size_t o = 0;
    memset(cp, 0, sizeof *cp);
    if(sidn > sizeof cp->sid) return -1;
    memcpy(cp->sid, sid, sidn); cp->sidn = sidn; memcpy(cp->ya, ya, 32);
    ssc_cpace_generator(prs, prsn, sid, sidn, g);
    if(ssc_random(y, 32)) return -1;
    crypto_x25519(cp->yb, y, g);                                               /* Yb = X25519(y, g) (clamped scalar) */
    crypto_x25519(k, y, ya);                                                   /* K = X25519(y, Ya) */
    crypto_wipe(y, 32); crypto_wipe(g, 32);
    if(!crypto_verify32(k, zero) || !crypto_verify32(cp->yb, zero)){ crypto_wipe(k, 32); return -1; }   /* low-order point */
    static const char dsi_isk[] = "CPace255_ISK";
    o += lv(buf + o, dsi_isk, sizeof dsi_isk - 1); o += lv(buf + o, sid, sidn); o += lv(buf + o, k, 32);
    o += lv_share_ad(buf + o, ya, "server"); o += lv_share_ad(buf + o, cp->yb, "client");   /* initiator, then responder */
    ssc_sha512(buf, o, cp->isk);
    crypto_sha512_ctx c; crypto_sha512_init(&c);
    crypto_sha512_update(&c, (const uint8_t *)"CPaceMac", 8); crypto_sha512_update(&c, sid, sidn); crypto_sha512_update(&c, cp->isk, 64);
    crypto_sha512_final(&c, cp->mac_key);
    crypto_wipe(k, 32); crypto_wipe(buf, sizeof buf);
    cp->ok = 1; return 0;
}
static void cpace_tag(const ssc_cpace *cp, const uint8_t y[32], const char *ad, uint8_t t[64]){
    uint8_t buf[64]; size_t n = lv_share_ad(buf, y, ad);
    crypto_sha512_hmac(t, cp->mac_key, 64, buf, n);
}
int ssc_cpace_check_server(const ssc_cpace *cp, const uint8_t ta[64]){
    if(!cp->ok) return 0;
    uint8_t t[64]; cpace_tag(cp, cp->ya, "server", t);
    int ok = !crypto_verify64(t, ta); crypto_wipe(t, 64); return ok;
}
void ssc_cpace_client_tag(const ssc_cpace *cp, uint8_t tb[64]){ cpace_tag(cp, cp->yb, "client", tb); }
void ssc_wrap(const ssc_cpace *cp, const char *label, const uint8_t v[32], uint8_t out[48]){
    uint8_t k[32]; ssc_sha256_ctx c; ssc_sha256_init(&c);
    ssc_sha256_update(&c, label, strlen(label)); ssc_sha256_update(&c, cp->sid, cp->sidn); ssc_sha256_update(&c, cp->isk, 64);
    ssc_sha256_final(&c, k);
    ssc_aead_seal(k, 0, NULL, 0, v, 32, out);                                  /* 12-byte all-zero nonce, empty AD */
    crypto_wipe(k, 32);
}

/* ---------------------------------------------------------------- pairing codes and tokens */
void ssc_pair_digest(const uint8_t h[32], const uint8_t na[32], const uint8_t nb[32], uint8_t d[32]){
    static const char label[] = "sendspin-pairing-code-derive-v1";
    ssc_sha256_ctx c; ssc_sha256_init(&c); ssc_sha256_update(&c, label, sizeof label - 1);
    ssc_sha256_update(&c, h, 32); ssc_sha256_update(&c, na, 32); ssc_sha256_update(&c, nb, 32); ssc_sha256_final(&c, d);
}
void ssc_pair_digits(const uint8_t d[32], char out[7]){
    uint64_t r = 0; for(int i = 0; i < 32; i++) r = (r * 256 + d[i]) % 1000000;
    snprintf(out, 7, "%06u", (unsigned)r);
}
int ssc_token(char version, const uint8_t *p, size_t n, char *out, size_t cap){
    static const char B32[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
    size_t need = 4 + (n * 8 + 4) / 5 + 1; if(cap < need) return -1;
    size_t o = 0; out[o++] = 'S'; out[o++] = 'P'; out[o++] = ':'; out[o++] = version;
    uint32_t acc = 0; int bits = 0;
    for(size_t i = 0; i < n; i++){
        acc = acc << 8 | p[i]; bits += 8;
        while(bits >= 5){ bits -= 5; char c = B32[(acc >> bits) & 31]; out[o++] = c == '2' ? '9' : c; }
    }
    if(bits){ char c = B32[(acc << (5 - bits)) & 31]; out[o++] = c == '2' ? '9' : c; }
    out[o] = 0; return (int)o;
}

/* ---------------------------------------------------------------- keys file
 * Text, one item per line:  sendspin-keys 1 | identity <priv> | pairing <psk> | record <server_id> <psk> <used>
 * (values base64url). The identity and pairing PSK live as long as the file: deleting it is the factory reset. */
const char *ssc_keys_path(void){ const char *p = getenv("SS_KEYS"); return p && p[0] ? p : "/usr/data/diskos-sendspin.keys"; }
static int dec32(const char *s, uint8_t out[32]){ return strlen(s) == 43 && ssc_b64url_dec(s, 43, out, 32) == 32 ? 0 : -1; }
static int keys_parse(ssc_keys *k, FILE *f){
    char line[256]; int have_id = 0, have_pr = 0;
    while(fgets(line, sizeof line, f)){
        char a[16], b[64], c[64]; long long used = 0;
        int n = sscanf(line, "%15s %63s %63s %lld", a, b, c, &used);
        if(n >= 2 && !strcmp(a, "identity") && !dec32(b, k->s_priv)) have_id = 1;
        else if(n >= 2 && !strcmp(a, "pairing") && !dec32(b, k->pairing_psk)) have_pr = 1;
        else if(n >= 3 && !strcmp(a, "record") && strlen(b) == 43 && k->nrec < SSC_MAX_RECORDS){
            ssc_record *r = &k->rec[k->nrec];
            if(!dec32(c, r->psk)){ snprintf(r->server_id, sizeof r->server_id, "%s", b); r->used = used; k->nrec++; }
        }
    }
    crypto_wipe(line, sizeof line);
    return have_id && have_pr ? 0 : -1;
}
static int keys_write(const ssc_keys *k, const char *path){
    char e1[44], e2[44];
    FILE *f = fopen(path, "w"); if(!f) return -1;
    fchmod(fileno(f), 0600);
    ssc_b64url_enc(k->s_priv, 32, e1); ssc_b64url_enc(k->pairing_psk, 32, e2);
    fprintf(f, "sendspin-keys 1\nidentity %s\npairing %s\n", e1, e2);
    for(int i = 0; i < k->nrec; i++){ ssc_b64url_enc(k->rec[i].psk, 32, e1); fprintf(f, "record %s %s %lld\n", k->rec[i].server_id, e1, k->rec[i].used); }
    crypto_wipe(e1, sizeof e1); crypto_wipe(e2, sizeof e2);
    int ok = fflush(f) == 0 && fsync(fileno(f)) == 0;
    if(fclose(f) != 0) ok = 0;
    return ok ? 0 : -1;
}
static void keys_derive(ssc_keys *k){ crypto_x25519_public_key(k->s_pub, k->s_priv); ssc_b64url_enc(k->s_pub, 32, k->client_id); }
int ssc_keys_load(ssc_keys *k, int create){
    const char *path = ssc_keys_path();
    for(int attempt = 0; attempt < 3; attempt++){
        memset(k, 0, sizeof *k);
        FILE *f = fopen(path, "r");
        if(f){ int r = keys_parse(k, f); fclose(f); if(r == 0){ keys_derive(k); return 0; } }
        if(!create) return -1;
        /* a new identity: written to a temporary file and linked into place, so the UI and the helper making one at
         * the same moment agree on a single identity (the loser reads the winner's) */
        memset(k, 0, sizeof *k);
        if(ssc_random(k->s_priv, 32) || ssc_random(k->pairing_psk, 32)) return -1;
        char tmp[300]; snprintf(tmp, sizeof tmp, "%s.%d.new", path, (int)getpid());
        if(keys_write(k, tmp) < 0){ unlink(tmp); return -1; }
        int r = f ? rename(tmp, path) : link(tmp, path);   /* an unreadable / broken file is replaced */
        if(!f) unlink(tmp);
        if(r == 0){ keys_derive(k); return 0; }
        if(errno != EEXIST) return -1;
    }
    return -1;
}
int ssc_keys_save(const ssc_keys *k){
    char tmp[300]; snprintf(tmp, sizeof tmp, "%s.%d.new", ssc_keys_path(), (int)getpid());
    if(keys_write(k, tmp) < 0 || rename(tmp, ssc_keys_path()) < 0){ unlink(tmp); return -1; }
    return 0;
}
void ssc_keys_put(ssc_keys *k, const char *server_id, const uint8_t psk[32]){
    ssc_keys_drop(k, server_id);
    if(k->nrec == SSC_MAX_RECORDS){                                            /* full: the least recently used goes */
        int lru = 0; for(int i = 1; i < k->nrec; i++) if(k->rec[i].used < k->rec[lru].used) lru = i;
        k->rec[lru] = k->rec[--k->nrec];
    }
    ssc_record *r = &k->rec[k->nrec++];
    snprintf(r->server_id, sizeof r->server_id, "%s", server_id); memcpy(r->psk, psk, 32);
    long long mx = 0; for(int i = 0; i < k->nrec - 1; i++) if(k->rec[i].used > mx) mx = k->rec[i].used;
    r->used = mx + 1 > (long long)time(NULL) ? mx + 1 : (long long)time(NULL);
}
int ssc_keys_drop(ssc_keys *k, const char *server_id){
    for(int i = 0; i < k->nrec; i++) if(!strcmp(k->rec[i].server_id, server_id)){
        crypto_wipe(&k->rec[i], sizeof k->rec[i]); k->rec[i] = k->rec[--k->nrec]; return 1;
    }
    return 0;
}
int ssc_keys_token(const ssc_keys *k, char *out, size_t cap){
    uint8_t p[64]; memcpy(p, k->s_pub, 32); memcpy(p + 32, k->pairing_psk, 32);
    int r = ssc_token('0', p, 64, out, cap); crypto_wipe(p, sizeof p); return r;
}
