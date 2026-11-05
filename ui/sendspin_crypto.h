/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Sendspin 1.0 security for the MA Sendspin player (sendspin.c): the Noise KKpsk2 channel every connection now runs
 * in, CPace pairing codes, pairing tokens and the Disc's Sendspin keys. Spec: github.com/Sendspin/spec (connection.md
 * "Encryption", pairing.md). Primitives from Monocypher (vendor/monocypher): X25519, ChaCha20, Poly1305, SHA-512. */
#ifndef SENDSPIN_CRYPTO_H
#define SENDSPIN_CRYPTO_H
#include <stdint.h>
#include <stddef.h>

/* ---------------------------------------------------------------- primitives */
int  ssc_random(void *buf, size_t n);                                      /* 0 ok (getrandom / /dev/urandom) */
typedef struct { uint32_t h[8]; uint64_t len; uint8_t buf[64]; size_t n; } ssc_sha256_ctx;
void ssc_sha256_init(ssc_sha256_ctx *c);
void ssc_sha256_update(ssc_sha256_ctx *c, const void *d, size_t n);
void ssc_sha256_final(ssc_sha256_ctx *c, uint8_t out[32]);
void ssc_sha256(const void *d, size_t n, uint8_t out[32]);
void ssc_hmac_sha256(const uint8_t key[32], const void *d, size_t n, uint8_t out[32]);
void ssc_sha512(const void *d, size_t n, uint8_t out[64]);
/* ChaCha20-Poly1305 (RFC 8439) with the Noise nonce: 4 zero bytes || n little-endian. out = ciphertext || 16-byte tag */
void ssc_aead_seal(const uint8_t k[32], uint64_t n, const uint8_t *ad, size_t adn, const uint8_t *pt, size_t len, uint8_t *out);
int  ssc_aead_open(const uint8_t k[32], uint64_t n, const uint8_t *ad, size_t adn, const uint8_t *ct, size_t len, uint8_t *out); /* len includes the tag; 0 ok */
int  ssc_b64url_enc(const uint8_t *in, size_t n, char *out);              /* no padding; returns the length (out NUL-terminated) */
int  ssc_b64url_dec(const char *s, size_t sn, uint8_t *out, size_t max);  /* -1 bad input or too long */
void ssc_wipe(void *p, size_t n);
int  ssc_equal(const void *a, const void *b, size_t n);                    /* constant time, 1 equal */

/* ---------------------------------------------------------------- Noise_KKpsk2_25519_ChaChaPoly_SHA256, responder (the client) */
typedef struct { uint8_t k[32]; uint64_t n; int has_k; } ssc_cipher;
typedef struct {
    uint8_t ck[32], h[32]; ssc_cipher cs;
    uint8_t s_priv[32], s_pub[32], rs[32], e_priv[32], e_pub[32], re[32];
} ssc_noise;
#define SSC_SUITE "25519_ChaChaPoly_SHA256"
void ssc_noise_init(ssc_noise *hs, const uint8_t *prologue, size_t pn, const uint8_t s_priv[32], const uint8_t s_pub[32], const uint8_t rs[32]);
/* message 1 (server -> client: e, es, ss + payload); payload out is plaintext, *pn its length. 0 ok */
int  ssc_noise_read1(ssc_noise *hs, const uint8_t *msg, size_t n, uint8_t *payload, size_t cap, size_t *pn);
/* message 2 (client -> server: e, ee, se, psk + payload). out needs 32 + pn + 16 bytes. 0 ok */
int  ssc_noise_write2(ssc_noise *hs, const uint8_t psk[32], const uint8_t *payload, size_t pn, uint8_t *out, size_t *outn);
/* after message 2: rx decrypts server->client, tx encrypts client->server; h is the handshake hash. Wipes hs. */
void ssc_noise_split(ssc_noise *hs, ssc_cipher *rx, ssc_cipher *tx, uint8_t h[32]);
int  ssc_cipher_seal(ssc_cipher *c, const uint8_t *pt, size_t n, uint8_t *out);           /* out n + 16; 0 ok */
int  ssc_cipher_open(ssc_cipher *c, const uint8_t *ct, size_t n, uint8_t *out);           /* out n - 16; 0 ok */

/* ---------------------------------------------------------------- PSKs */
extern const uint8_t SSC_SENTINEL_PSK[32];
void ssc_psk_id(const uint8_t psk[32], char out[44]);                        /* base64url(SHA-256("sendspin-psk-id-v1" || PSK)) */

/* ---------------------------------------------------------------- CPACE-X25519-SHA512 (draft-irtf-cfrg-cpace-21), role B */
typedef struct { uint8_t yb[32], ya[32], isk[64], mac_key[64]; uint8_t sid[64]; size_t sidn; int ok; } ssc_cpace;
/* given the server's share Ya: makes our share Yb, the ISK and the MAC key. -1 on a bad / low-order share */
int  ssc_cpace_respond(ssc_cpace *cp, const uint8_t *prs, size_t prsn, const uint8_t *sid, size_t sidn, const uint8_t ya[32]);
int  ssc_cpace_check_server(const ssc_cpace *cp, const uint8_t ta[64]);    /* 1 if the server's tag Ta verifies */
void ssc_cpace_client_tag(const ssc_cpace *cp, uint8_t tb[64]);
void ssc_cpace_generator(const uint8_t *prs, size_t prsn, const uint8_t *sid, size_t sidn, uint8_t g[32]);   /* tests */
void ssc_elligator2(const uint8_t r[32], uint8_t u[32]);                                                     /* tests */
/* pairing.md "Wrapping": seals a 32-byte value under SHA-256(label || sid || ISK); out 48 bytes */
void ssc_wrap(const ssc_cpace *cp, const char *label, const uint8_t v[32], uint8_t out[48]);

/* ---------------------------------------------------------------- pairing codes and tokens */
void ssc_pair_digest(const uint8_t h[32], const uint8_t na[32], const uint8_t nb[32], uint8_t d[32]);
void ssc_pair_digits(const uint8_t d[32], char out[7]);                    /* uint256_be(digest) mod 10^6, 6 digits */
int  ssc_token(char version, const uint8_t *payload, size_t n, char *out, size_t cap);   /* "SP:" version base32 (2 -> 9) */

/* ---------------------------------------------------------------- the Disc's Sendspin keys (one file, 0600) */
#define SSC_MAX_RECORDS 8
typedef struct { char server_id[44]; uint8_t psk[32]; long long used; } ssc_record;
typedef struct {
    uint8_t s_priv[32], s_pub[32], pairing_psk[32];
    char client_id[44];
    ssc_record rec[SSC_MAX_RECORDS]; int nrec;
} ssc_keys;
const char *ssc_keys_path(void);                         /* $SS_KEYS, else /usr/data/diskos-sendspin.keys */
int  ssc_keys_load(ssc_keys *k, int create);             /* 0 ok; creates the identity and pairing PSK if missing */
int  ssc_keys_save(const ssc_keys *k);                   /* atomic */
void ssc_keys_put(ssc_keys *k, const char *server_id, const uint8_t psk[32]);   /* replaces / evicts least recently used */
int  ssc_keys_drop(ssc_keys *k, const char *server_id);  /* 1 if one was removed */
int  ssc_keys_token(const ssc_keys *k, char *out, size_t cap);   /* the version-0 pairing token (client key || pairing PSK) */
#endif
