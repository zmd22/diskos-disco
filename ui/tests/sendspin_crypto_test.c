/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Known-answer tests for sendspin_crypto.c. Vectors: the Sendspin spec (pairing tokens, Sentinel PSK / psk_id) and
 * sendspin-cpp's tests/test_cpace.cpp (Elligator 2), plus RFC 4231 (HMAC-SHA-256).
 * Build and run (host):  make -C ui sendspin-crypto-test */
#include "../sendspin_crypto.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int fails;
static void hex(const uint8_t *p, size_t n, char *out){ for(size_t i = 0; i < n; i++) sprintf(out + 2 * i, "%02x", p[i]); }
static void unhex(const char *s, uint8_t *out){ for(size_t i = 0; s[2 * i]; i++){ unsigned v; sscanf(s + 2 * i, "%2x", &v); out[i] = (uint8_t)v; } }
static void check(const char *what, const char *got, const char *want){
    if(strcmp(got, want)){ printf("FAIL %s\n  got  %s\n  want %s\n", what, got, want); fails++; } else printf("ok   %s\n", what);
}

int main(void){
    char g[512]; uint8_t b[64], o[64];

    ssc_sha256("abc", 3, o); hex(o, 32, g);
    check("sha256 abc", g, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    uint8_t k[32]; memset(k, 0, 32); memset(k, 0x0b, 20);   /* RFC 4231 case 1 uses a 20-byte key: zero-padded = same HMAC */
    ssc_hmac_sha256(k, "Hi There", 8, o); hex(o, 32, g);
    check("hmac-sha256 rfc4231#1", g, "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");

    ssc_sha256("sendspin-sentinel-psk-v1", 24, o); hex(o, 32, g);
    check("sentinel psk", g, "1b5e24dbc1aed95fc2a5a338a90c05df44bd10f5ec1f4cd66cbf86272767b9d3");
    ssc_psk_id(SSC_SENTINEL_PSK, g);
    check("sentinel psk_id", g, "GFsV9tLaSQm9HcFWpKsgYQOr7wFTvNUtkmFwuVz3zoo");

    for(int i = 0; i < 32; i++){ b[i] = (uint8_t)i; b[32 + i] = (uint8_t)(0xe0 + i); }
    ssc_token('0', b, 64, g, sizeof g);
    check("pairing token v0", g, "SP:0AAAQEAYEAUDAOCAJBIFQYDIOB4IBCEQTCQKRMFYYDENBWHA5DYP6BYPC4PSOLZXH5DU6V97M5XXO74HR6LZ7J5PW674PT6X37T6757Y");
    ssc_token('1', b + 32, 24, g, sizeof g);
    check("pairing token v1 (qr code)", g, "SP:14DQ6FY7E4XTOP9HJ5LV6Z3PO57YPD4XT6T97N5Y");

    static const char *ell[][2] = {
        { "efcdab9078563412efcdab9078563412efcdab9078563412efcdab9078563412", "406682120bd67c0502cb7afed2849918c58c0bc36e1cbdbc2184d407f5a9db19" },
        { "0000000000000000000000000000000000000000000000000000000000000000", "0000000000000000000000000000000000000000000000000000000000000000" },
        { "0100000000000000000000000000000000000000000000000000000000000000", "9cdb525555555555555555555555555555555555555555555555555555555555" },
        { "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff", "6edfd66733be3f52309983805b9fcdf8fe48c1640e026e7d36e3fb2305933908" },
        { "dededededededededededededededededededededededededededededededede", "42d8f3889968f3caa814ca11b2bc880b35c9c0897f193a1aec578043a43e3948" },
        { "0102030405060708010203040506070801020304050607080102030405060708", "bde03792c4903abf825e98667c9d4c4945665581b0e2306ad19e10c564e04773" },
        { "aa55aa55aa55aa55aa55aa55aa55aa55aa55aa55aa55aa55aa55aa55aa55aa55", "803fcc2dbcf575f784907707db8080a2657230a71c9caa4a87a57beccfdb323f" },
    };
    for(size_t i = 0; i < sizeof ell / sizeof ell[0]; i++){
        unhex(ell[i][0], b); ssc_elligator2(b, o); hex(o, 32, g);
        char w[48]; snprintf(w, sizeof w, "elligator2 #%zu", i); check(w, g, ell[i][1]);
    }

    /* AEAD round trip and tamper */
    uint8_t key[32], ct[64], pt[32]; for(int i = 0; i < 32; i++) key[i] = (uint8_t)(i * 7);
    ssc_aead_seal(key, 5, (const uint8_t *)"ad", 2, (const uint8_t *)"hello sendspin", 14, ct);
    int ok = ssc_aead_open(key, 5, (const uint8_t *)"ad", 2, ct, 30, pt) == 0 && !memcmp(pt, "hello sendspin", 14);
    ct[3] ^= 1; ok = ok && ssc_aead_open(key, 5, (const uint8_t *)"ad", 2, ct, 30, pt) != 0;
    check("aead round trip / tamper", ok ? "ok" : "bad", "ok");

    /* base64url */
    uint8_t d[40]; int n = ssc_b64url_dec("GFsV9tLaSQm9HcFWpKsgYQOr7wFTvNUtkmFwuVz3zoo", 43, d, sizeof d); hex(d, n > 0 ? (size_t)n : 0, g);
    check("b64url decode", g, "185b15f6d2da4909bd1dc156a4ab206103abef0153bcd52d926170b95cf7ce8a");
    check("b64url rejects junk", ssc_b64url_dec("ab=c", 4, d, sizeof d) < 0 ? "ok" : "bad", "ok");

    /* pairing digits: a digest of 0x00..0x1f -> uint256 mod 10^6 */
    for(int i = 0; i < 32; i++) b[i] = (uint8_t)i;
    char dg[7]; ssc_pair_digits(b, dg);
    check("pairing digits", dg, "440671");   /* python: int.from_bytes(bytes(range(32)),'big') % 10**6 */

    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
