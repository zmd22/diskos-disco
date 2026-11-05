/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Sendspin player for Music Assistant ("MA Sendspin"), run by mq_ui as a child: mq_ui --sendspin ...
 * mq_player owns the audio hardware, so the audio goes to the Disc's own AirPlay receiver (loopback, ss_airplay.h).
 * Sendspin 1.0 (github.com/Sendspin/spec): every connection is end-to-end encrypted (Noise KKpsk2, sendspin_crypto.c);
 * roles player@v1 (FLAC or raw PCM, 44.1/16/2; time filter, volume/mute in software), metadata@v1, artwork@v1 (one 360 x 360
 * JPEG channel), controller@v1. Pairing: the Pairing PSK (the token in Settings) and the dynamic pairing code (shown on
 * the Disc, typed into Music Assistant). Unpaired playback is allowed while Settings > MA Sendspin > Allow Unpaired is on.
 * UI link (dir $SS_DIR, default /tmp/ma): status (key=value, rewritten atomically), cmd (FIFO, one command per line),
 *          a<N>/cover.jpg (the current cover; the UI shows <dir>/track as the "song", its sidecar cover).
 * Usage: --sendspin <server[:port]|auto> <name> <client_id-ignored> <delay_ms> [airplay_latency_ms] [airplay_port] [unpaired 0|1] */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "ss_airplay.h"
#include "sendspin_crypto.h"
#include "version.h"
#include <math.h>
#include <netdb.h>
#include <signal.h>
#include <stdarg.h>
#include <ctype.h>
#include <sys/stat.h>
#include <strings.h>

/* ---------------------------------------------------------------- utils */
static long long now_us(void){ struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1000000LL + t.tv_nsec / 1000; }
static void logf_(const char *fmt, ...){
    struct timespec t; clock_gettime(CLOCK_REALTIME, &t); struct tm tm; localtime_r(&t.tv_sec, &tm);
    fprintf(stderr, "%02d:%02d:%02d.%03ld ", tm.tm_hour, tm.tm_min, tm.tm_sec, t.tv_nsec / 1000000);
    va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap); fputc('\n', stderr);
}
static char g_dir[96] = "/tmp/ma";                  /* $SS_DIR: the UI link (tests point it elsewhere) */
static void dpath(char *out, size_t n, const char *leaf){ snprintf(out, n, "%s/%s", g_dir, leaf); }

/* ---------------------------------------------------------------- JSON reading
 * A small, strict reader: values are found by key in one object (no "anywhere in the text" matching, so a title can't
 * pose as a field), strings are unescaped (\uXXXX and surrogate pairs to UTF-8). Peers may be unauthenticated. */
static const char *js_ws(const char *p){ while(*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++; return p; }
static const char *js_skip(const char *p, int depth);
static const char *js_skip_str(const char *p){                 /* p at '"' -> after the closing quote, NULL if bad */
    if(*p != '"') return NULL;
    for(p++; *p; p++){
        if(*p == '"') return p + 1;
        if(*p == '\\'){ if(!p[1]) return NULL; p++; }
        else if((unsigned char)*p < 0x20) return NULL;
    }
    return NULL;
}
static const char *js_skip(const char *p, int depth){          /* p at a value -> after it, NULL if bad */
    if(depth > 16) return NULL;
    p = js_ws(p);
    if(*p == '"') return js_skip_str(p);
    if(*p == '{' || *p == '['){
        char close = *p == '{' ? '}' : ']'; int obj = *p == '{';
        p = js_ws(p + 1);
        if(*p == close) return p + 1;
        for(;;){
            if(obj){ p = js_skip_str(js_ws(p)); if(!p) return NULL; p = js_ws(p); if(*p != ':') return NULL; p++; }
            p = js_skip(p, depth + 1); if(!p) return NULL;
            p = js_ws(p);
            if(*p == ',') { p++; continue; }
            if(*p == close) return p + 1;
            return NULL;
        }
    }
    if(*p == '-' || isdigit((unsigned char)*p)){ p++; while(isdigit((unsigned char)*p) || *p == '.' || *p == 'e' || *p == 'E' || *p == '+' || *p == '-') p++; return p; }
    if(!strncmp(p, "true", 4)) return p + 4;
    if(!strncmp(p, "false", 5)) return p + 5;
    if(!strncmp(p, "null", 4)) return p + 4;
    return NULL;
}
static int js_keyeq(const char *p, const char *key){           /* p at '"': the key equals `key` (keys are plain ASCII) */
    size_t n = strlen(key); return !strncmp(p + 1, key, n) && p[1 + n] == '"';
}
static const char *js_get(const char *obj, const char *key){    /* obj at '{' -> the value of `key`, or NULL */
    if(!obj) return NULL;
    const char *p = js_ws(obj); if(*p != '{') return NULL;
    p = js_ws(p + 1); if(*p == '}') return NULL;
    for(;;){
        p = js_ws(p); if(*p != '"') return NULL;
        const char *k = p; p = js_skip_str(p); if(!p) return NULL;
        p = js_ws(p); if(*p != ':') return NULL;
        const char *v = js_ws(p + 1);
        if(js_keyeq(k, key)) return v;
        p = js_skip(v, 0); if(!p) return NULL;
        p = js_ws(p);
        if(*p == ',') { p++; continue; }
        return NULL;
    }
}
static int js_isobj(const char *v){ return v && *v == '{'; }
static void utf8_put(char *out, int n, int *o, unsigned cp){
    char b[4]; int k;
    if(cp < 0x80){ b[0] = (char)cp; k = 1; }
    else if(cp < 0x800){ b[0] = (char)(0xC0 | cp >> 6); b[1] = (char)(0x80 | (cp & 63)); k = 2; }
    else if(cp < 0x10000){ b[0] = (char)(0xE0 | cp >> 12); b[1] = (char)(0x80 | (cp >> 6 & 63)); b[2] = (char)(0x80 | (cp & 63)); k = 3; }
    else { b[0] = (char)(0xF0 | cp >> 18); b[1] = (char)(0x80 | (cp >> 12 & 63)); b[2] = (char)(0x80 | (cp >> 6 & 63)); b[3] = (char)(0x80 | (cp & 63)); k = 4; }
    if(*o + k < n){ memcpy(out + *o, b, k); *o += k; }
}
static int hex4(const char *p, unsigned *v){ *v = 0; for(int i = 0; i < 4; i++){ int c = p[i], d = isdigit(c) ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1; if(d < 0) return -1; *v = *v << 4 | (unsigned)d; } return 0; }
/* a string value, unescaped, cut at a UTF-8 character boundary to fit `n` (and to `maxb` bytes when > 0) */
static int js_str(const char *v, char *out, int n, int maxb){
    if(!v || *v != '"' || n < 1) return 0;
    int o = 0; const char *p = v + 1;
    if(maxb > 0 && maxb < n - 1) n = maxb + 1;
    while(*p && *p != '"'){
        unsigned cp;
        if(*p == '\\'){
            p++;
            switch(*p){
                case 'n': cp = '\n'; break; case 't': cp = '\t'; break; case 'r': cp = '\r'; break; case 'b': cp = 8; break; case 'f': cp = 12; break;
                case 'u':
                    if(hex4(p + 1, &cp)) return 0;
                    p += 4;
                    if(cp >= 0xD800 && cp < 0xDC00 && p[1] == '\\' && p[2] == 'u'){
                        unsigned lo; if(!hex4(p + 3, &lo) && lo >= 0xDC00 && lo < 0xE000){ cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00); p += 6; }
                    }
                    if(cp >= 0xD800 && cp < 0xE000) cp = 0xFFFD;
                    break;
                case 0: return 0;
                default: cp = (unsigned char)*p;
            }
            p++;
            if(cp < 0x20) cp = ' ';                    /* plain text: no control characters reach the UI */
            utf8_put(out, n, &o, cp);
            continue;
        }
        int k = 1; unsigned char c = (unsigned char)*p;
        if(c >= 0xF0) k = 4; else if(c >= 0xE0) k = 3; else if(c >= 0xC0) k = 2;
        for(int i = 1; i < k; i++) if(!p[i]) return 0;
        if(o + k < n){ memcpy(out + o, p, k); o += k; } else { p += k; while(*p && *p != '"'){ if(*p == '\\' && p[1]) p++; p++; } break; }
        p += k;
    }
    out[o] = 0; return 1;
}
static int js_int(const char *v, long long *out){
    if(!v || !(*v == '-' || isdigit((unsigned char)*v))) return 0;
    char *e; long long x = strtoll(v, &e, 10); if(e == v || *e == '.' || *e == 'e' || *e == 'E') return 0;
    *out = x; return 1;
}
static int js_bool(const char *v, int *out){
    if(!v) return 0;
    if(!strncmp(v, "true", 4)){ *out = 1; return 1; }
    if(!strncmp(v, "false", 5)){ *out = 0; return 1; }
    return 0;
}
/* array iteration: *it starts at the array's '[', each call returns the next element (NULL at the end or if bad) */
static const char *js_next(const char **it){
    const char *p = js_ws(*it);
    if(*p == '[') p = js_ws(p + 1); else if(*p == ',') p = js_ws(p + 1); else return NULL;
    if(*p == ']') return NULL;
    const char *e = js_skip(p, 0); if(!e) return NULL;
    *it = js_ws(e); return p;
}
static int js_arr_has(const char *arr, const char *s){
    if(!arr || *arr != '[') return 0;
    const char *it = arr, *v; char b[64];
    while((v = js_next(&it))) if(js_str(v, b, sizeof b, 0) && !strcmp(b, s)) return 1;
    return 0;
}
/* JSON string escaping for what we send; `maxb` caps the UTF-8 bytes of the source (64 for names), at a boundary */
static void js_esc(char *out, size_t n, const char *s, int maxb){
    size_t o = 0; int used = 0;
    for(const unsigned char *p = (const unsigned char *)s; *p && o + 7 < n; ){
        int k = *p >= 0xF0 ? 4 : *p >= 0xE0 ? 3 : *p >= 0xC0 ? 2 : 1;
        for(int i = 1; i < k; i++) if(!p[i]){ out[o] = 0; return; }   /* a cut-off character: stop before it */
        if(maxb > 0 && used + k > maxb) break;
        if(*p == '"' || *p == '\\'){ out[o++] = '\\'; out[o++] = (char)*p; }
        else if(*p < 0x20) o += (size_t)snprintf(out + o, n - o, "\\u%04x", *p);
        else { for(int i = 0; i < k; i++) out[o++] = (char)p[i]; }
        used += k; p += k;
    }
    out[o] = 0;
}

/* ---------------------------------------------------------------- WebSocket client */
/* Sizes are size_t and every incoming length is checked against WS_MAX before it is used (1.4.1 security fix: a
 * frame length with the top bit set used to go negative and bypass the checks). */
#define WS_MAX ((size_t)16 << 20)           /* largest frame and largest reassembled message we accept */
typedef struct { int fd; uint8_t *rx; size_t rxn, rxcap; uint8_t *msg; size_t msgn, msgcap; int msgop; } ws_t;
static int ws_send_frame(ws_t *w, int op, const void *data, int len){
    if(w->fd < 0) return -1;
    uint8_t h[14]; int hl = 0; uint8_t mask[4]; ap_rnd(mask, 4);
    h[hl++] = 0x80 | op;
    if(len < 126) h[hl++] = 0x80 | len;
    else if(len < 65536){ h[hl++] = 0x80 | 126; h[hl++] = len >> 8; h[hl++] = len; }
    else { h[hl++] = 0x80 | 127; for(int i = 7; i >= 0; i--) h[hl++] = i < 4 ? (uint8_t)(len >> (8 * i)) : 0; }
    memcpy(h + hl, mask, 4); hl += 4;
    uint8_t *buf = malloc(hl + len); if(!buf) return -1;
    memcpy(buf, h, hl);
    for(int i = 0; i < len; i++) buf[hl + i] = ((const uint8_t *)data)[i] ^ mask[i & 3];
    int tot = hl + len, off = 0;
    while(off < tot){ int r = send(w->fd, buf + off, tot - off, MSG_NOSIGNAL); if(r <= 0){ if(r < 0 && errno == EINTR) continue; free(buf); return -1; } off += r; }
    free(buf); return 0;
}
static int ws_connect(ws_t *w, const char *host, int port, const char *path){
    memset(w, 0, sizeof *w); w->fd = -1;
    char ps[8]; snprintf(ps, sizeof ps, "%d", port);
    struct addrinfo hints = {0}, *res = NULL; hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
    if(getaddrinfo(host, ps, &hints, &res) != 0 || !res){ logf_("can't resolve %s", host); return -1; }
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct timeval tv = { 5, 0 }; setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv); setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    if(connect(fd, res->ai_addr, res->ai_addrlen) < 0){ logf_("can't connect to %s:%d: %s", host, port, strerror(errno)); close(fd); freeaddrinfo(res); return -1; }
    freeaddrinfo(res);
    int one = 1; setsockopt(fd, IPPROTO_TCP, 1 /* TCP_NODELAY */, &one, sizeof one);
    uint8_t k[16]; char k64[32]; ap_rnd(k, 16); b64_enc(k, 16, k64); strcat(k64, "==");
    char req[512]; int n = snprintf(req, sizeof req, "GET %s HTTP/1.1\r\nHost: %s:%d\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                                    "Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n\r\n", path, host, port, k64);
    if(send(fd, req, n, MSG_NOSIGNAL) != n){ close(fd); return -1; }
    char rep[2048]; int got = 0;
    while(got < (int)sizeof rep - 1){ int r = recv(fd, rep + got, 1, 0); if(r <= 0) break; got++; rep[got] = 0; if(got >= 4 && !memcmp(rep + got - 4, "\r\n\r\n", 4)) break; }
    if(strncmp(rep, "HTTP/1.1 101", 12)){ char first[128] = ""; sscanf(rep, "%127[^\r\n]", first); logf_("WebSocket refused: %s", got ? first : "(no reply)"); close(fd); return -1; }
    tv.tv_sec = 0; setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    fcntl(fd, F_SETFL, O_NONBLOCK);
    w->rxn = w->msgn = 0; w->rxcap = 1 << 16; w->rx = malloc(w->rxcap); w->msgcap = 1 << 16; w->msg = malloc(w->msgcap);
    if(!w->rx || !w->msg){ free(w->rx); free(w->msg); w->rx = w->msg = NULL; close(fd); return -1; }
    w->fd = fd;
    return 0;
}
static void ws_close(ws_t *w){
    if(w->fd >= 0){
        uint8_t code[2] = { 1000 >> 8, 1000 & 255 };                  /* normal closure */
        ws_send_frame(w, 8, code, 2);
        shutdown(w->fd, SHUT_WR);
        struct pollfd p = { w->fd, POLLIN, 0 }; char junk[512];           /* read to EOF (briefly), so the close isn't a reset */
        for(int i = 0; i < 5 && poll(&p, 1, 40) == 1; i++) if(recv(w->fd, junk, sizeof junk, 0) <= 0) break;
        close(w->fd);
    }
    w->fd = -1; free(w->rx); free(w->msg); w->rx = w->msg = NULL;
}
/* reads what's there; returns 1 with a complete message in w->msg (op 1 text / 2 binary), 0 none, -1 closed */
static int ws_grow(uint8_t **buf, size_t *cap, size_t need){       /* 0 ok, -1 out of memory (the buffer is kept) */
    if(need <= *cap) return 0;
    size_t ncap = need + 4096;
    uint8_t *n = realloc(*buf, ncap);
    if(!n) return -1;
    *buf = n; *cap = ncap; return 0;
}
static int ws_poll_msg(ws_t *w){
    for(;;){
        /* try to parse one frame from rx */
        if(w->rxn >= 2){
            uint8_t *b = w->rx; int fin = b[0] & 0x80, op = b[0] & 15, masked = b[1] & 0x80;
            uint64_t len = b[1] & 127; size_t hl = 2;
            if(len == 126){ if(w->rxn < 4) goto more; len = (uint64_t)b[2] << 8 | b[3]; hl = 4; }
            else if(len == 127){ if(w->rxn < 10) goto more; len = 0; for(int i = 0; i < 8; i++) len = len << 8 | b[2 + i]; hl = 10; }
            if(masked) hl += 4;
            if(len > WS_MAX) return -1;                                  /* unsigned: no length can slip past this */
            if((op & 8) && len > 125) return -1;                         /* control frames are short by definition */
            size_t flen = (size_t)len, need = hl + flen;
            if(w->rxn < need){
                if(ws_grow(&w->rx, &w->rxcap, need) < 0) return -1;
                goto more;
            }
            uint8_t *pl = b + hl;
            if(masked) for(size_t i = 0; i < flen; i++) pl[i] ^= b[hl - 4 + (i & 3)];
            int done = 0;
            if(op == 8){ return -1; }
            else if(op == 9){ ws_send_frame(w, 10, pl, (int)flen); }
            else if(op == 10){ }
            else if(op == 0 || op == 1 || op == 2){
                if(op){ w->msgn = 0; w->msgop = op; }
                if(flen > WS_MAX - w->msgn) return -1;                   /* continuations can't build a message past the limit */
                if(ws_grow(&w->msg, &w->msgcap, w->msgn + flen + 1) < 0) return -1;
                memcpy(w->msg + w->msgn, pl, flen); w->msgn += flen; w->msg[w->msgn] = 0;
                if(fin) done = 1;
            }
            memmove(w->rx, w->rx + need, w->rxn - need); w->rxn -= need;
            if(done) return 1;
            continue;
        }
    more:
        if(w->rxn >= w->rxcap && ws_grow(&w->rx, &w->rxcap, w->rxcap * 2) < 0) return -1;
        ssize_t r = recv(w->fd, w->rx + w->rxn, w->rxcap - w->rxn, 0);
        if(r == 0) return -1;
        if(r < 0) return (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) ? 0 : -1;
        w->rxn += (size_t)r;
    }
}


/* ---------------------------------------------------------------- time filter (messaging.md "Time filter")
 * offset = server clock - local clock, with drift; the spec's two-dimensional Kalman filter and parameters. */
typedef struct { double offset, drift, poo, pod, pdd; long long last; int count, use_drift; } tfilt_t;
static void tf_reset(tfilt_t *f){ memset(f, 0, sizeof *f); f->poo = INFINITY; }
static void tf_update(tfilt_t *f, long long meas, long long max_err, long long t_added){
    if(t_added <= f->last || max_err <= 0) return;
    double dt = (double)(t_added - f->last); f->last = t_added;
    double R = (0.5 * max_err) * (0.5 * max_err);
    if(f->count == 0){ f->offset = (double)meas; f->poo = R; f->drift = 0; f->count = 1; return; }
    if(f->count == 1){ f->drift = ((double)meas - f->offset) / dt; f->pdd = (f->poo + R) / (dt * dt); f->offset = (double)meas; f->poo = R; f->count = 2; return; }
    const double drift_sd = 1e-11;
    double off_p = f->offset + f->drift * dt;
    double poo_p = f->poo + 2 * f->pod * dt + f->pdd * dt * dt;                  /* process_std_dev = 0 */
    double pod_p = f->pod + f->pdd * dt;
    double pdd_p = f->pdd + drift_sd * drift_sd * dt;
    double y = (double)meas - off_p;
    if(f->count < 100) f->count++;
    else if(fabs(y) > 3.0 * (double)max_err){ poo_p *= 4; pod_p *= 4; pdd_p *= 4; }   /* forget_factor^2 */
    double S = poo_p + R, ko = poo_p / S, kd = pod_p / S;
    f->offset = off_p + ko * y; f->drift = f->drift + kd * y;
    f->poo = poo_p - ko * poo_p; f->pod = pod_p - kd * poo_p; f->pdd = pdd_p - kd * pod_p;
    f->use_drift = f->drift * f->drift > 4.0 * f->pdd;                          /* threshold 2.0, squared */
}
static long long rnd(double x){ return (long long)(x < 0 ? x - 0.5 : x + 0.5); }
static long long tf_to_local(const tfilt_t *f, long long t_server){
    double d = f->use_drift ? f->drift : 0;
    return rnd(((double)t_server - f->offset + d * (double)f->last) / (1 + d));
}

/* ---------------------------------------------------------------- audio FIFO (stereo int16 frames) */
#define FIFO_FRAMES (AP_RATE * 20)
static int16_t *g_fifo; static int g_fh, g_fn;   /* head index, frames held */
static long long g_fifo_ts;                       /* server-clock us of the head frame */
static long long g_fifo_end_ts;                   /* expected server ts of the next frame to arrive */
static void fifo_reset(void){ g_fh = g_fn = 0; g_fifo_ts = g_fifo_end_ts = 0; }
static void fifo_push(const int16_t *pcm, int frames, long long ts){
    if(g_fn == 0){ g_fifo_ts = ts; }
    else {
        long long d = ts - g_fifo_end_ts;
        if(d > 20000 || d < -20000){                 /* a jump in the timeline: start over from this chunk */
            logf_("audio: timeline jump of %lld ms, resyncing", d / 1000);
            fifo_reset(); g_fifo_ts = ts;
        }
    }
    for(int i = 0; i < frames && g_fn < FIFO_FRAMES; i++){
        int at = (g_fh + g_fn) % FIFO_FRAMES;
        g_fifo[2 * at] = pcm[2 * i]; g_fifo[2 * at + 1] = pcm[2 * i + 1]; g_fn++;
    }
    g_fifo_end_ts = ts + (long long)frames * 1000000 / AP_RATE;
}
static void fifo_pop(int16_t *out, int frames){
    for(int i = 0; i < frames; i++){ out[2 * i] = g_fifo[2 * g_fh]; out[2 * i + 1] = g_fifo[2 * g_fh + 1]; g_fh = (g_fh + 1) % FIFO_FRAMES; }
    g_fn -= frames;
    g_fifo_ts += (long long)frames * 1000000 / AP_RATE;
}
/* stream/clear with from_timestamp: keep what plays before `from`, drop the rest */
static void fifo_clear_from(long long from){
    if(g_fn == 0) return;
    if(from <= g_fifo_ts){ fifo_reset(); return; }
    long long keep = (from - g_fifo_ts) * AP_RATE / 1000000;
    if(keep < g_fn){ g_fn = (int)keep; g_fifo_end_ts = g_fifo_ts + keep * 1000000 / AP_RATE; }
}

/* FLAC (sendspin_flac.c): the stream header from stream/start, each chunk decoded behind it */
long ssf_decode(const unsigned char *hdr, size_t hn, const unsigned char *data, size_t n,
                void (*out)(void *ctx, const short *pcm, int frames), void *ctx);
static uint8_t *g_flac_hdr; size_t g_flac_hdr_n;
static int b64std_dec(const char *s, uint8_t *out, size_t max){       /* standard base64, padding allowed; -1 bad */
    uint32_t acc = 0; int bits = 0; size_t o = 0;
    for(; *s && *s != '='; s++){
        int c = (unsigned char)*s, v;
        if(c >= 'A' && c <= 'Z') v = c - 'A'; else if(c >= 'a' && c <= 'z') v = c - 'a' + 26; else if(c >= '0' && c <= '9') v = c - '0' + 52;
        else if(c == '+') v = 62; else if(c == '/') v = 63; else return -1;
        acc = acc << 6 | (uint32_t)v; bits += 6;
        if(bits >= 8){ bits -= 8; if(o >= max) return -1; out[o++] = (uint8_t)(acc >> bits); }
    }
    return (int)o;
}
typedef struct { long long ts; int peak; } flac_ctx;
static void flac_out(void *ctx, const short *pcm, int frames){
    flac_ctx *fc = ctx; fifo_push(pcm, frames, fc->ts); fc->ts += (long long)frames * 1000000 / AP_RATE;
    for(int i = 0; i < 2 * frames; i++){ int a = pcm[i] < 0 ? -pcm[i] : pcm[i]; if(a > fc->peak) fc->peak = a; }
}

/* ---------------------------------------------------------------- client */
static volatile int g_stop;
static void on_sig(int s){ (void)s; g_stop = 1; }

enum { PSK_NONE, PSK_LT, PSK_PR, PSK_SN };                 /* the PSK the Noise handshake matched */
enum { PH_INIT, PH_NOISE, PH_HELLO, PH_ACTIVATE, PH_LIVE };   /* connection phase */
enum { PM_NONE, PM_PSK, PM_DYNAMIC };                     /* pairing attempt method */
#define ART_MAX ((uint32_t)4 << 20)                         /* larger covers are counted, not kept */
typedef struct {
    char title[160], artist[160], album[160];
    long long ts, dur_ms, prog_ms; int speed, has_prog, valid;
} meta_t;
typedef struct {
    const char *host, *name; int port;
    int latency_ms;            /* how long the AirPlay receiver holds audio before it plays */
    int volume, muted, gain;
    ws_t ws; airplay_t ap;
    int streaming, fmt_ok;     /* player stream open / its format is ours */
    int flac;                  /* the stream is FLAC (else PCM) */
    long long dec_frames; int dec_logged;
    long long late_drops, sent_pkts;
    int delay_ms;              /* user Sync Delay: + plays later */
    int unpaired_ok;           /* Settings: Allow Unpaired */
    int cmdfd;                 /* $SS_DIR/cmd FIFO (opened read-write, so reads never see EOF) */
    /* --- the encrypted session */
    ssc_keys keys; int phase; long long phase_until;
    char cinit[256]; int cinit_n;
    char server_id[44], server_name[96];
    uint8_t rs[32], h[32];
    ssc_cipher rx, tx;
    int psk_kind, rec_idx;
    int rehs;                  /* re-handshake done, waiting for the new server/activate: no application messages */
    uint8_t *frag; size_t frag_n, frag_cap; int frag_on, frag_type;
    uint8_t *ptbuf; size_t ptcap;   /* decrypt / encrypt scratch */
    uint8_t *ctbuf; size_t ctcap;
    /* --- activation */
    int activated, act_playback, act_pairing;
    int r_player, r_meta, r_art, r_ctl;
    int state_sent;            /* the client/state for the current roles went out */
    int pairing_index;
    /* --- pairing attempt */
    int pm, p_qr, p_round, p_fails, p_held, p_coded, p_finalizing; long long p_until;
    uint8_t nonce_b[32], nonce_a[32], p_code[24]; char p_digits[7];
    uint8_t new_psk[32]; ssc_cpace cp;
    char pair_show[160];       /* what the UI shows: "digits:123456" or "qr:SP:1..." */
    char pair_note[96]; long long pair_note_until;
    /* --- clock */
    tfilt_t tf; int synced;
    int burst_left; long long t_pending; long long burst_best_err, burst_best_meas, burst_best_at; long long next_burst;
    /* --- what Music Assistant says is playing */
    meta_t md, md_next;
    char title[160], artist[160], album[160];
    long long dur_ms, prog_ms, prog_at_us; int speed;   /* progress at a local monotonic time; speed 0 = paused */
    char ctl_cmds[256];        /* controller supported_commands, as sent */
    int art_stream;            /* artwork stream open */
    int art_fly; uint32_t art_total, art_got; long long art_ts; uint8_t *art_buf;   /* -1 / the channel in flight */
    int art_pend; uint8_t *pend_buf; uint32_t pend_n; long long pend_ts;          /* a complete pending image */
    int art_n; char art_dir[96];
    int ctl_playing;           /* group playback state: playing */
    int status_dirty; long long status_at;
    char server_lbl[96], msg[96]; int fails;
} cl_t;

/* Music Assistant's volume is applied here, in software: the Disc's AirPlay receiver ignores AirPlay volume (its own
 * volume is the Disc's). amplitude = (volume / 100)^1.5, the curve the Sendspin player role asks for; gain in Q15. */
static int vol_gain(int v, int muted){ if(muted || v <= 0) return 0; if(v >= 100) return 32768; double g = v / 100.0; return (int)(pow(g, 1.5) * 32768 + 0.5); }
static int g_debug;

/* ---------------------------------------------------------------- the UI link */
static void status_write(cl_t *c){
    const char *st = c->phase != PH_LIVE ? (c->fails >= 2 ? "error" : "connecting")
                   : !(c->r_player && c->act_playback) ? "waiting"
                   : (c->streaming && c->speed != 0) ? "playing" : "ready";
    char tmp[128], fin[128]; dpath(tmp, sizeof tmp, "status.tmp"); dpath(fin, sizeof fin, "status");
    FILE *f = fopen(tmp, "w"); if(!f) return;
    fprintf(f, "state=%s\nserver=%s\nmsg=%s\n", st, c->server_name[0] ? c->server_name : c->server_lbl, c->msg);
    if(c->phase == PH_LIVE) fprintf(f, "paired=%d\n", c->psk_kind == PSK_LT);
    if(c->pair_show[0]) fprintf(f, "pair=%s\n", c->pair_show);
    if(c->pair_note[0] && now_us() < c->pair_note_until) fprintf(f, "pair_note=%s\n", c->pair_note);
    if(c->phase == PH_LIVE && c->r_meta && c->title[0]){
        fprintf(f, "title=%s\nartist=%s\nalbum=%s\nduration_ms=%lld\nprogress_ms=%lld\nprogress_at_ms=%lld\nspeed=%d\n",
                c->title, c->artist, c->album, c->dur_ms, c->prog_ms, c->prog_at_us / 1000, c->speed);
        char a0[96]; dpath(a0, sizeof a0, "a0");
        fprintf(f, "track=%s/track\n", c->art_dir[0] ? c->art_dir : a0);
    }
    fprintf(f, "pid=%d\n", (int)getpid());
    fclose(f);
    rename(tmp, fin);
    c->status_dirty = 0; c->status_at = now_us();
}
static void art_save(cl_t *c, const uint8_t *img, int n){
    char dir[96], path[128], old[96];
    snprintf(old, sizeof old, "%s", c->art_dir);
    c->art_n++;
    snprintf(dir, sizeof dir, "%s/a%d", g_dir, c->art_n);
    mkdir(dir, 0755);
    if(n > 0){
        snprintf(path, sizeof path, "%s/cover.jpg", dir);
        FILE *f = fopen(path, "wb"); if(f){ if(fwrite(img, 1, n, f) != (size_t)n) logf_("cover: short write"); fclose(f); }
    }
    snprintf(c->art_dir, sizeof c->art_dir, "%s", dir);
    if(old[0]){ snprintf(path, sizeof path, "%s/cover.jpg", old); unlink(path); rmdir(old); }
    c->status_dirty = 1;
}
static void art_clear(cl_t *c, int shown){                   /* drop the transfer and the pending image; `shown`: also the current */
    c->art_fly = -1; c->art_total = c->art_got = 0; free(c->art_buf); c->art_buf = NULL;
    c->art_pend = 0; free(c->pend_buf); c->pend_buf = NULL; c->pend_n = 0;
    if(shown && c->art_dir[0]) art_save(c, NULL, 0);
}

/* ---------------------------------------------------------------- sending */
static int send_plain_json(cl_t *c, const char *json){         /* the cleartext handshake messages */
    if(g_debug) logf_(">> %.300s", json);
    return ws_send_frame(&c->ws, 1, json, (int)strlen(json));
}
static int grow(uint8_t **b, size_t *cap, size_t need){ if(need <= *cap) return 0; uint8_t *n = realloc(*b, need + 1024); if(!n) return -1; *b = n; *cap = need + 1024; return 0; }
static int send_json(cl_t *c, const char *json){
    size_t n = strlen(json);
    if(n + 1 > 65519) return -1;                               /* everything we send fits one Noise message */
    if(grow(&c->ptbuf, &c->ptcap, n + 1) || grow(&c->ctbuf, &c->ctcap, n + 17)) return -1;
    c->ptbuf[0] = 0; memcpy(c->ptbuf + 1, json, n);
    if(ssc_cipher_seal(&c->tx, c->ptbuf, n + 1, c->ctbuf)) return -1;
    if(g_debug && !strstr(json, "client/time")) logf_(">> %.300s", json);
    return ws_send_frame(&c->ws, 2, c->ctbuf, (int)(n + 17));
}
static void send_goodbye(cl_t *c, const char *reason){
    char m[128]; snprintf(m, sizeof m, "{\"type\":\"client/goodbye\",\"payload\":{\"reason\":\"%s\"}}", reason);
    send_json(c, m); logf_("goodbye: %s", reason);
}
static int can_send(cl_t *c){ return c->phase == PH_LIVE && !c->rehs; }
static void send_state(cl_t *c){
    if(!can_send(c) || !c->synced) return;
    char m[1024]; int o = snprintf(m, sizeof m, "{\"type\":\"client/state\",\"payload\":{\"available\":true");
    if(c->r_player){
        int lead = c->latency_ms + 700 + (c->delay_ms < 0 ? -c->delay_ms : 0);       /* an earlier Sync Delay needs the audio sooner */
        int minb = c->latency_ms + 500 + (c->delay_ms < 0 ? -c->delay_ms : 0);
        if(minb > 5000) minb = 5000;
        o += snprintf(m + o, sizeof m - o, ",\"player\":{\"volume\":%d,\"muted\":%s,\"output_delay_ms\":0,\"required_lead_time_ms\":%d,"
                      "\"min_buffer_ms\":%d,\"supported_commands\":[\"volume\",\"mute\"]}", c->volume, c->muted ? "true" : "false", lead, minb);
    }
    if(c->r_art) o += snprintf(m + o, sizeof m - o, ",\"artwork\":{\"channels\":[{\"source\":\"album\",\"format\":\"jpeg\",\"width\":360,\"height\":360}]}");
    snprintf(m + o, sizeof m - o, "}}");
    send_json(c, m); c->state_sent = 1;
}
static int ctl_allows(cl_t *c, const char *cmd){ char q[24]; snprintf(q, sizeof q, "\"%s\"", cmd); return strstr(c->ctl_cmds, q) != NULL; }
static void send_command(cl_t *c, const char *cmd){
    if(!can_send(c) || !c->r_ctl) return;
    if(!ctl_allows(c, cmd)){ logf_("command %s: not offered by the server", cmd); return; }
    char m[160]; snprintf(m, sizeof m, "{\"type\":\"client/command\",\"payload\":{\"controller\":{\"command\":\"%s\"}}}", cmd);
    send_json(c, m);
    logf_("command: %s", cmd);
}
static void pair_end(cl_t *c, const char *note);
static void pair_abort(cl_t *c, const char *reason, const char *note){
    char m[128]; snprintf(m, sizeof m, "{\"type\":\"pair/abort\",\"payload\":{\"reason\":\"%s\"}}", reason);
    send_json(c, m); logf_("pairing: abort (%s)", reason);
    pair_end(c, note);
}
static void cmd_poll(cl_t *c){
    if(c->cmdfd < 0) return;
    static char buf[256]; static int bn;
    int r = read(c->cmdfd, buf + bn, sizeof buf - 1 - bn);
    if(r <= 0) return;
    bn += r; buf[bn] = 0;
    char *nl;
    while((nl = strchr(buf, '\n'))){
        *nl = 0;
        if(can_send(c)){
            if(!strcmp(buf, "toggle")) send_command(c, (c->ctl_playing || (c->streaming && c->speed)) ? "pause" : "play");
            else if(!strcmp(buf, "play") || !strcmp(buf, "pause") || !strcmp(buf, "next") || !strcmp(buf, "previous")) send_command(c, buf);
            else if(!strncmp(buf, "seek ", 5) && c->r_ctl && ctl_allows(c, "seek")){
                char m[192]; snprintf(m, sizeof m, "{\"type\":\"client/command\",\"payload\":{\"controller\":{\"command\":\"seek\",\"position_ms\":%ld}}}", atol(buf + 5));
                send_json(c, m); logf_("command: seek %ld", atol(buf + 5));
            }
            else if(!strcmp(buf, "pair-cancel") && c->pm != PM_NONE && !c->p_finalizing) pair_abort(c, "user_cancelled", "Pairing cancelled");
        }
        int used = (int)(nl - buf) + 1; memmove(buf, buf + used, bn - used + 1); bn -= used;
    }
    if(bn >= (int)sizeof buf - 1) bn = 0;
}
static void send_hello(cl_t *c){
    char m[2048], name[160], mac[32] = "", macj[64] = "";
    js_esc(name, sizeof name, c->name, 64);
    FILE *f = fopen("/sys/class/net/wlan0/address", "r");
    if(f){ if(fgets(mac, sizeof mac, f)){ mac[strcspn(mac, "\r\n")] = 0; if(strlen(mac) == 17) snprintf(macj, sizeof macj, ",\"mac_address\":\"%s\"", mac); } fclose(f); }
    for(char *p = macj; *p; p++) *p = (char)tolower((unsigned char)*p);
    snprintf(m, sizeof m,
        "{\"type\":\"client/hello\",\"payload\":{\"name\":\"%s\","
        "\"device_info\":{\"product_name\":\"Snowsky Disc\",\"manufacturer\":\"FiiO\",\"software_version\":\"" DISCO_NAME " " DISCO_VERSION "\"%s},"
        "\"supported_roles\":[\"player@v1\",\"metadata@v1\",\"artwork@v1\",\"controller@v1\"],"
        "\"player@v1_support\":{\"supported_formats\":[{\"codec\":\"flac\",\"channels\":2,\"sample_rate\":44100,\"bit_depth\":16},"
        "{\"codec\":\"pcm\",\"channels\":2,\"sample_rate\":44100,\"bit_depth\":16}],"
        "\"buffer_capacity\":%d},"
        "\"supported_pair_methods\":{\"pairing_psk\":{\"locations\":[\"device\"]},"
        "\"dynamic_pairing_code\":{\"out_channels\":[\"display\"],\"formats\":[\"digits\",\"qr_code\"]}},"
        "\"unpaired_access\":{\"enabled\":%s}}}",
        name, macj, FIFO_FRAMES * 4 / 2, c->unpaired_ok ? "true" : "false");
    send_json(c, m);
}

/* ---------------------------------------------------------------- clock sync: bursts of 8, the best of each feeds the filter */
static void send_time(cl_t *c){
    char m[128]; c->t_pending = now_us();
    snprintf(m, sizeof m, "{\"type\":\"client/time\",\"payload\":{\"client_transmitted\":%lld}}", c->t_pending);
    send_json(c, m);
}
static void time_tick(cl_t *c, long long now){
    if(!can_send(c)) return;
    if(c->t_pending && now - c->t_pending > 1500000) c->t_pending = 0;     /* a lost reply: go on with the burst */
    if(c->t_pending) return;
    if(c->burst_left == 0 && now >= c->next_burst){ c->burst_left = 8; c->burst_best_err = -1; }
    if(c->burst_left > 0) send_time(c);
}
static void on_time(cl_t *c, const char *pl, long long t4){
    long long t1, t2, t3;
    if(!js_int(js_get(pl, "client_transmitted"), &t1) || !js_int(js_get(pl, "server_received"), &t2) || !js_int(js_get(pl, "server_transmitted"), &t3)) return;
    if(t1 != c->t_pending) return;                                          /* not the reply we wait for */
    c->t_pending = 0;
    long long meas = ((t2 - t1) + (t3 - t4)) / 2, err = ((t4 - t1) - (t3 - t2)) / 2;
    if(c->burst_best_err < 0 || err < c->burst_best_err){ c->burst_best_err = err; c->burst_best_meas = meas; c->burst_best_at = t4; }
    if(--c->burst_left > 0) return;
    tf_update(&c->tf, c->burst_best_meas, c->burst_best_err, c->burst_best_at);
    c->next_burst = t4 + (c->tf.count < 2 ? 300000 : 5000000);
    if(!c->synced && c->tf.count >= 2){
        c->synced = 1; logf_("clock synced (max error %lld us)", c->burst_best_err);
        send_state(c);
    }
}

/* ---------------------------------------------------------------- metadata (scheduled updates) */
static void md_apply(cl_t *c, const meta_t *m){
    snprintf(c->title, sizeof c->title, "%s", m->title); snprintf(c->artist, sizeof c->artist, "%s", m->artist);
    snprintf(c->album, sizeof c->album, "%s", m->album);
    if(m->has_prog){ c->prog_ms = m->prog_ms; c->dur_ms = m->dur_ms; c->speed = m->speed; c->prog_at_us = c->synced ? tf_to_local(&c->tf, m->ts) : now_us(); }
    else { c->prog_ms = c->dur_ms = 0; c->speed = 0; c->prog_at_us = now_us(); }
    if(c->title[0]) logf_("now playing: %s - %s (%lld/%lld ms, speed %d)", c->artist, c->title, c->prog_ms, c->dur_ms, c->speed);
    c->status_dirty = 1;
}
static void md_clear(cl_t *c){ c->md_next.valid = 0; c->title[0] = c->artist[0] = c->album[0] = 0; c->dur_ms = c->prog_ms = 0; c->speed = 0; c->status_dirty = 1; }
static void on_metadata(cl_t *c, const char *md){
    meta_t m; memset(&m, 0, sizeof m); m.valid = 1;
    if(!js_int(js_get(md, "timestamp"), &m.ts)) m.ts = 0;
    js_str(js_get(md, "title"), m.title, sizeof m.title, 0);              /* every message is the full state: absent = none */
    js_str(js_get(md, "artist"), m.artist, sizeof m.artist, 0);
    js_str(js_get(md, "album"), m.album, sizeof m.album, 0);
    const char *pr = js_get(md, "progress");
    if(js_isobj(pr)){
        long long v;
        if(js_int(js_get(pr, "track_progress"), &v)){ m.prog_ms = v; m.has_prog = 1; }
        if(js_int(js_get(pr, "track_duration"), &v)) m.dur_ms = v;
        m.speed = js_int(js_get(pr, "playback_speed"), &v) ? (int)v : 1000;
    }
    if(c->synced && m.ts && tf_to_local(&c->tf, m.ts) > now_us()){ c->md_next = m; return; }   /* scheduled: it waits */
    c->md_next.valid = 0;
    md_apply(c, &m);
}

/* ---------------------------------------------------------------- activation and pairing */
static void persist_keys(cl_t *c){ if(ssc_keys_save(&c->keys)) logf_("keys: could not save %s", ssc_keys_path()); }
static void pair_end(cl_t *c, const char *note){
    c->pm = PM_NONE; c->p_finalizing = 0; c->p_coded = 0; c->pair_show[0] = 0;
    ssc_wipe(c->new_psk, 32); ssc_wipe(&c->cp, sizeof c->cp); ssc_wipe(c->nonce_b, 32);
    if(note){ snprintf(c->pair_note, sizeof c->pair_note, "%s", note); c->pair_note_until = now_us() + 6000000; }
    c->status_dirty = 1;
}
static void pair_start(cl_t *c, const char *method, int qr){
    char m[256];
    if(!strcmp(method, "pairing_psk")){
        c->pm = PM_PSK; c->p_finalizing = 1;
        if(ssc_random(c->new_psk, 32)) return;
        char e[44]; ssc_b64url_enc(c->new_psk, 32, e);
        snprintf(m, sizeof m, "{\"type\":\"client/pair-init\",\"payload\":{\"pairing_index\":%d}}", c->pairing_index); send_json(c, m);
        snprintf(m, sizeof m, "{\"type\":\"client/pair-finalize\",\"payload\":{\"long_term_psk\":\"%s\"}}", e); send_json(c, m);
        ssc_wipe(e, sizeof e);
        logf_("pairing: Pairing PSK (token)");
        return;
    }
    c->pm = PM_DYNAMIC; c->p_qr = qr; c->p_round = 0; c->p_coded = 0; c->p_until = now_us() + 120000000LL;   /* attempt timeout: 2 min */
    if(c->p_held || c->p_fails >= 20){                         /* the round limit: held until Sendspin is restarted on the Disc */
        c->p_held = 1;
        snprintf(m, sizeof m, "{\"type\":\"client/pair-pending\",\"payload\":{\"pairing_index\":%d,\"message\":\"Too many wrong codes. Turn Sendspin off and on on the Disc to pair again.\"}}", c->pairing_index);
        send_json(c, m); c->pm = PM_NONE; logf_("pairing: held back (round limit)");
        return;
    }
    uint8_t commit[32]; char e[44];
    if(ssc_random(c->nonce_b, 32)) return;
    ssc_sha256_ctx h; ssc_sha256_init(&h); ssc_sha256_update(&h, "sendspin-pair-commit-v1", 23); ssc_sha256_update(&h, c->nonce_b, 32); ssc_sha256_final(&h, commit);
    ssc_b64url_enc(commit, 32, e);
    snprintf(m, sizeof m, "{\"type\":\"client/pair-init\",\"payload\":{\"pairing_index\":%d,\"commit_B\":\"%s\"}}", c->pairing_index, e);
    send_json(c, m);
    logf_("pairing: pairing code (%s)", qr ? "QR code" : "digits");
}
static void pair_sid(cl_t *c, uint8_t sid[64], size_t *n){
    size_t o = 0; memcpy(sid, "sendspin-pair-pake-v1", 21); o = 21; memcpy(sid + o, c->h, 32); o += 32;
    uint32_t v[2] = { (uint32_t)c->pairing_index, (uint32_t)c->p_round };
    for(int i = 0; i < 2; i++){ sid[o++] = v[i] >> 24; sid[o++] = v[i] >> 16; sid[o++] = v[i] >> 8; sid[o++] = v[i]; }
    *n = o;
}
static int dec_field(const char *pl, const char *key, uint8_t *out, size_t want){
    char s[128]; if(!js_str(js_get(pl, key), s, sizeof s, 0)) return -1;
    return ssc_b64url_dec(s, strlen(s), out, want) == (int)want ? 0 : -1;
}
/* returns -1 when the connection must close (a pairing protocol error) */
static int on_pairing(cl_t *c, const char *type, const char *pl){
    char m[256], e1[96], e2[96];
    if(!strcmp(type, "server/pair-finalize")){
        if(c->pm == PM_NONE || !c->p_finalizing) return -1;
        ssc_keys_put(&c->keys, c->server_id, c->new_psk); persist_keys(c);
        logf_("pairing: paired with %s", c->server_name[0] ? c->server_name : c->server_id);
        char note[96]; snprintf(note, sizeof note, "Paired with %s", c->server_name[0] ? c->server_name : "Music Assistant");
        c->p_fails = 0; pair_end(c, note);
        return 0;
    }
    if(!strcmp(type, "pair/abort")){
        char r[40] = ""; js_str(js_get(pl, "reason"), r, sizeof r, 0);
        if(c->pm == PM_NONE) return 0;
        logf_("pairing: the server ended it (%s)", r);
        pair_end(c, !strcmp(r, "pairing_code_mismatch") ? "Wrong code" : "Pairing cancelled");
        return 0;
    }
    if(c->pm != PM_DYNAMIC || c->p_finalizing) return c->pm == PM_NONE ? 0 : -1;   /* left over after an abort: dropped */
    if(!strcmp(type, "server/pair-init")){
        if(c->p_round == 0){
            if(dec_field(pl, "nonce_A", c->nonce_a, 32)) return -1;
            uint8_t d[32]; ssc_pair_digest(c->h, c->nonce_a, c->nonce_b, d);
            if(c->p_qr){ memcpy(c->p_code, d, 24); char tok[64]; ssc_token('1', d, 24, tok, sizeof tok); snprintf(c->pair_show, sizeof c->pair_show, "qr:%s", tok); }
            else { ssc_pair_digits(d, c->p_digits); snprintf(c->pair_show, sizeof c->pair_show, "digits:%s", c->p_digits); }
            ssc_wipe(d, 32);
        } else if(js_get(pl, "nonce_A")) return -1;          /* only the first round carries it */
        c->p_round++; c->p_coded = 1; c->status_dirty = 1;
        logf_("pairing: round %d, code on screen", c->p_round);
        return 0;
    }
    if(!c->p_coded) return -1;
    if(!strcmp(type, "server/pair-auth")){
        uint8_t ya[32], sid[64]; size_t sn;
        if(dec_field(pl, "pake_msg_1", ya, 32)) return -1;
        pair_sid(c, sid, &sn);
        int r = c->p_qr ? ssc_cpace_respond(&c->cp, c->p_code, 24, sid, sn, ya)
                        : ssc_cpace_respond(&c->cp, (const uint8_t *)c->p_digits, 6, sid, sn, ya);
        if(r) return -1;                                       /* a bad or low-order share */
        ssc_b64url_enc(c->cp.yb, 32, e1);
        snprintf(m, sizeof m, "{\"type\":\"client/pair-auth\",\"payload\":{\"pake_msg_2\":\"%s\"}}", e1); send_json(c, m);
        return 0;
    }
    if(!strcmp(type, "server/pair-confirm")){
        uint8_t ta[64]; if(dec_field(pl, "server_kc", ta, 64) || !c->cp.ok) return -1;
        if(!ssc_cpace_check_server(&c->cp, ta)){
            c->p_fails++; ssc_wipe(&c->cp, sizeof c->cp);
            logf_("pairing: wrong code (%d)", c->p_fails);
            if(c->p_fails >= 20){ c->p_held = 1; pair_abort(c, "pairing_code_mismatch", "Too many wrong codes"); }
            else { send_json(c, "{\"type\":\"client/pair-retry\",\"payload\":{}}"); snprintf(c->pair_note, sizeof c->pair_note, "Wrong code, try again"); c->pair_note_until = now_us() + 6000000; c->status_dirty = 1; }
            return 0;
        }
        c->p_fails = 0;
        uint8_t tb[64], w[48]; ssc_cpace_client_tag(&c->cp, tb);
        ssc_b64url_enc(tb, 64, e1);
        ssc_wrap(&c->cp, "sendspin-pair-nonce-wrap-v1", c->nonce_b, w); ssc_b64url_enc(w, 48, e2);
        snprintf(m, sizeof m, "{\"type\":\"client/pair-confirm\",\"payload\":{\"client_kc\":\"%s\",\"wrapped_nonce_B\":\"%s\"}}", e1, e2); send_json(c, m);
        if(ssc_random(c->new_psk, 32)) return -1;
        ssc_wrap(&c->cp, "sendspin-pair-psk-wrap-v1", c->new_psk, w); ssc_b64url_enc(w, 48, e2);
        snprintf(m, sizeof m, "{\"type\":\"client/pair-finalize\",\"payload\":{\"wrapped_psk\":\"%s\"}}", e2); send_json(c, m);
        ssc_wipe(e2, sizeof e2); ssc_wipe(w, sizeof w);
        c->p_finalizing = 1; c->pair_show[0] = 0; c->status_dirty = 1;
        logf_("pairing: code confirmed, sending the new key");
        return 0;
    }
    return -1;
}
/* roles going away: their output stops and their state is dropped (messaging.md "server/activate") */
static void stream_stop(cl_t *c, int teardown){
    fifo_reset();
    if(teardown){ ap_close(&c->ap); c->streaming = 0; }
    else ap_flush(&c->ap);
}
/* returns -1 to close */
static int on_activate(cl_t *c, const char *pl){
    const char *acts = js_get(pl, "activities"), *roles = js_get(pl, "active_roles"), *pair = js_get(pl, "pairing");
    if(!acts || *acts != '[') return -1;
    int pb = js_arr_has(acts, "playback"), pa = js_arr_has(acts, "pairing");
    int np = c->r_player, nm = c->r_meta, na = c->r_art, nc = c->r_ctl;
    if(roles){ if(*roles != '[') return -1; np = js_arr_has(roles, "player@v1"); nm = js_arr_has(roles, "metadata@v1"); na = js_arr_has(roles, "artwork@v1"); nc = js_arr_has(roles, "controller@v1"); }
    else if(!c->activated) np = nm = na = nc = 0;
    int lt = c->psk_kind == PSK_LT, unpaired = !lt;
    int capable = lt || c->unpaired_ok;                       /* the connection may carry 'playback' / roles */
    if(!capable && !roles && c->activated) np = nm = na = nc = 0;   /* persisted roles count as empty */
    int any_role = np || nm || na || nc;
    if(pa) c->pairing_index++;                                 /* counts every pairing activation since the handshake */
    if(unpaired && !c->unpaired_ok && (pb || any_role)){ send_goodbye(c, "pairing_required"); snprintf(c->msg, sizeof c->msg, "Pair it in Music Assistant first"); return -2; }
    if((lt && pa) || (pb && !capable) || (any_role && !capable)){ send_goodbye(c, "unauthorized"); return -2; }
    if(pa){
        char meth[32] = "", fmt[16] = "";
        if(!js_isobj(pair) || !js_str(js_get(pair, "method"), meth, sizeof meth, 0)) return -1;
        js_str(js_get(pair, "format"), fmt, sizeof fmt, 0);
        int ok = (!strcmp(meth, "pairing_psk") && c->psk_kind == PSK_PR) ||
                 (!strcmp(meth, "dynamic_pairing_code") && c->psk_kind != PSK_PR && (!strcmp(fmt, "digits") || !strcmp(fmt, "qr_code")));
        if(!ok){
            snprintf(c->msg, sizeof c->msg, "%s", "");
            send_json(c, "{\"type\":\"pair/abort\",\"payload\":{\"reason\":\"method_not_supported\"}}");
            logf_("pairing: method %s/%s not offered on this connection", meth, fmt);
            pa = 0;
        }
        if(c->pm != PM_NONE) pair_end(c, NULL);               /* a new pairing activation supersedes the old attempt */
        if(ok){ c->activated = 1; c->act_pairing = 1; pair_start(c, meth, !strcmp(fmt, "qr_code")); }
    } else if(c->pm != PM_NONE){                               /* activation without pairing: the attempt ends, nothing kept */
        logf_("pairing: ended by the server"); pair_end(c, NULL);
    }
    /* removed roles: stop their output and drop their state */
    if((c->r_player && !np) || (c->act_playback && !pb)){ if(c->streaming) logf_("player: stopped (activation)"); stream_stop(c, 1); c->fmt_ok = 0; }
    if(c->r_art && !na){ c->art_stream = 0; art_clear(c, 1); }
    if(c->r_meta && !nm){ md_clear(c); }
    if(c->r_ctl && !nc){ c->ctl_cmds[0] = 0; }
    int added = (np && !c->r_player) || (na && !c->r_art) || (!c->activated && any_role);
    c->r_player = np; c->r_meta = nm; c->r_art = na; c->r_ctl = nc;
    c->act_playback = pb; c->act_pairing = pa;
    int first = !c->activated; c->activated = 1; c->rehs = 0;
    logf_("activate: activities%s%s, roles%s%s%s%s", pb ? " playback" : "", pa ? " pairing" : "", np ? " player" : "", nm ? " metadata" : "", na ? " artwork" : "", nc ? " controller" : "");
    if(first){ c->burst_left = 0; c->next_burst = 0; }
    if(added || first) c->state_sent = 0;
    if(any_role && !c->state_sent) send_state(c);            /* (once the clock is synced if it isn't yet) */
    if(!pb && !any_role && !pa) snprintf(c->msg, sizeof c->msg, "%s", unpaired ? "Approve or pair it in Music Assistant" : "");
    else c->msg[0] = 0;
    c->status_dirty = 1;
    return 0;
}

/* ---------------------------------------------------------------- the Noise handshake */
static int psk_lookup(cl_t *c, const char *psk_id, const char *cat, int allow_sentinel, const uint8_t **psk){
    char id[44];
    if(!strcmp(cat, "lt")){
        for(int i = 0; i < c->keys.nrec; i++){
            ssc_psk_id(c->keys.rec[i].psk, id);
            if(!strcmp(id, psk_id)){
                if(strcmp(c->keys.rec[i].server_id, c->server_id)){ logf_("noise: a long-term key for another server"); return -1; }
                c->rec_idx = i; *psk = c->keys.rec[i].psk; return PSK_LT;
            }
        }
    } else if(!strcmp(cat, "pr")){
        ssc_psk_id(c->keys.pairing_psk, id); if(!strcmp(id, psk_id)){ *psk = c->keys.pairing_psk; return PSK_PR; }
    } else if(!strcmp(cat, "sn")){
        ssc_psk_id(SSC_SENTINEL_PSK, id); if(!strcmp(id, psk_id)){ *psk = SSC_SENTINEL_PSK; return PSK_SN; }
    } else return -1;
    if(!allow_sentinel){ logf_("noise: unknown key in a re-handshake"); return -1; }
    logf_("noise: no key for psk_id (%s): unpaired session (Sentinel)", cat);   /* a lost or evicted pairing */
    *psk = SSC_SENTINEL_PSK; return PSK_SN;
}
/* a re-handshake inside the encrypted channel (connection.md "Re-handshake"): message 1 in, message 2 out under the
 * old keys, then the new keys; the prologue is the previous handshake hash. -1 to close */
static int noise_rehandshake(cl_t *c, const char *pl){
    char d64[512]; uint8_t msg[400], pt[600], out[256]; size_t pn, on;
    if(!js_str(js_get(pl, "data"), d64, sizeof d64, 0)) return -1;
    int n = ssc_b64url_dec(d64, strlen(d64), msg, sizeof msg); if(n < 48) return -1;
    ssc_noise hs; ssc_noise_init(&hs, c->h, 32, c->keys.s_priv, c->keys.s_pub, c->rs);
    if(ssc_noise_read1(&hs, msg, (size_t)n, pt, sizeof pt - 1, &pn)){ logf_("noise: message 1 does not decrypt"); ssc_wipe(&hs, sizeof hs); return -1; }
    pt[pn] = 0;
    char id[64] = "", cat[8] = "";
    if(!js_str(js_get((char *)pt, "psk_id"), id, sizeof id, 0) || !js_str(js_get((char *)pt, "psk_category"), cat, sizeof cat, 0)){ ssc_wipe(&hs, sizeof hs); return -1; }
    const uint8_t *psk = NULL; int kind = psk_lookup(c, id, cat, 0, &psk);
    if(kind < 0){ ssc_wipe(&hs, sizeof hs); return -1; }
    if(ssc_noise_write2(&hs, psk, (const uint8_t *)"{}", 2, out, &on)){ ssc_wipe(&hs, sizeof hs); return -1; }
    char m[512], o64[400]; ssc_b64url_enc(out, on, o64);
    snprintf(m, sizeof m, "{\"type\":\"noise/handshake\",\"payload\":{\"data\":\"%s\"}}", o64);
    if(send_json(c, m) < 0){ ssc_wipe(&hs, sizeof hs); return -1; }            /* message 2 still under the old keys */
    ssc_noise_split(&hs, &c->rx, &c->tx, c->h);
    c->psk_kind = kind; c->pairing_index = 0; c->rehs = 1;
    logf_("noise: re-handshake done (%s)", kind == PSK_LT ? "paired" : kind == PSK_PR ? "pairing key" : "unpaired");
    c->status_dirty = 1;
    return 0;
}
static int noise_initial(cl_t *c, const uint8_t *sinit, size_t sinit_n, const char *pl){
    char d64[512]; uint8_t msg[400], pt[600], out[256], pro[1024]; size_t pn, on;
    if(!js_str(js_get(pl, "data"), d64, sizeof d64, 0)) return -1;
    int n = ssc_b64url_dec(d64, strlen(d64), msg, sizeof msg); if(n < 48) return -1;
    if((size_t)c->cinit_n + sinit_n > sizeof pro) return -1;
    memcpy(pro, c->cinit, c->cinit_n); memcpy(pro + c->cinit_n, sinit, sinit_n);     /* the exact bytes of both init messages */
    ssc_noise hs; ssc_noise_init(&hs, pro, c->cinit_n + sinit_n, c->keys.s_priv, c->keys.s_pub, c->rs);
    if(ssc_noise_read1(&hs, msg, (size_t)n, pt, sizeof pt - 1, &pn)){ logf_("noise: message 1 does not decrypt"); ssc_wipe(&hs, sizeof hs); return -1; }
    pt[pn] = 0;
    char id[64] = "", cat[8] = "";
    if(!js_str(js_get((char *)pt, "psk_id"), id, sizeof id, 0) || !js_str(js_get((char *)pt, "psk_category"), cat, sizeof cat, 0) ||
       (strcmp(cat, "lt") && strcmp(cat, "pr") && strcmp(cat, "sn"))){ ssc_wipe(&hs, sizeof hs); return -1; }
    const uint8_t *psk = NULL; int kind = psk_lookup(c, id, cat, 1, &psk);
    if(kind < 0){ ssc_wipe(&hs, sizeof hs); return -1; }
    if(ssc_noise_write2(&hs, psk, (const uint8_t *)"{}", 2, out, &on)){ ssc_wipe(&hs, sizeof hs); return -1; }
    char m[512], o64[400]; ssc_b64url_enc(out, on, o64);
    snprintf(m, sizeof m, "{\"type\":\"noise/handshake\",\"payload\":{\"data\":\"%s\"}}", o64);
    if(send_plain_json(c, m) < 0){ ssc_wipe(&hs, sizeof hs); return -1; }
    ssc_noise_split(&hs, &c->rx, &c->tx, c->h);
    c->psk_kind = kind; c->pairing_index = 0;
    if(kind == PSK_LT){                                         /* least-recently-used order, saved at most daily */
        long long t = (long long)time(NULL);
        if(t - c->keys.rec[c->rec_idx].used > 86400){ c->keys.rec[c->rec_idx].used = t; persist_keys(c); }
    }
    logf_("noise: encrypted (%s)", kind == PSK_LT ? "paired" : kind == PSK_PR ? "pairing key" : "unpaired");
    return 0;
}

/* ---------------------------------------------------------------- incoming */
static int on_json(cl_t *c, const char *s, long long rx_at){      /* -1 to close */
    const char *pl = js_get(s, "payload"); char type[48] = "";
    if(!js_str(js_get(s, "type"), type, sizeof type, 0) || !js_isobj(pl)) return -1;
    if(g_debug && strcmp(type, "server/time")) logf_("<< %.300s", s);
    if(c->phase == PH_HELLO){
        if(strcmp(type, "server/hello")) return -1;
        js_str(js_get(pl, "name"), c->server_name, sizeof c->server_name, 64);
        logf_("connected to %s", c->server_name[0] ? c->server_name : "?");
        send_hello(c); c->phase = PH_ACTIVATE; c->phase_until = now_us() + 30000000; c->status_dirty = 1;
        return 0;
    }
    if(c->phase == PH_ACTIVATE){
        if(strcmp(type, "server/activate")) return -1;
        c->phase = PH_LIVE; c->fails = 0; c->msg[0] = 0;
        return on_activate(c, pl);
    }
    if(!strcmp(type, "noise/handshake")) return c->rehs ? -1 : noise_rehandshake(c, pl);
    if(!strcmp(type, "server/activate")) return on_activate(c, pl);
    if(c->rehs) return -1;                                      /* only server/activate may follow a re-handshake */
    if(!strcmp(type, "server/time")){ on_time(c, pl, rx_at); return 0; }
    if(!strncmp(type, "server/pair-", 12) || !strcmp(type, "pair/abort")) return on_pairing(c, type, pl);
    if(!strcmp(type, "server/unpair")){
        if(c->psk_kind != PSK_LT) return 0;                     /* an unpaired session ignores it */
        ssc_keys_drop(&c->keys, c->server_id); persist_keys(c);
        send_goodbye(c, "unpaired"); logf_("unpaired by the server");
        snprintf(c->pair_note, sizeof c->pair_note, "Unpaired"); c->pair_note_until = now_us() + 6000000;
        return -2;
    }
    if(!strcmp(type, "stream/start")){
        const char *pp = js_get(pl, "player"), *ar = js_get(pl, "artwork");
        if(js_isobj(pp) && c->r_player && c->act_playback){
            char codec[16] = ""; long long sr = 0, ch = 0, bd = 0;
            js_str(js_get(pp, "codec"), codec, sizeof codec, 0); js_int(js_get(pp, "sample_rate"), &sr); js_int(js_get(pp, "channels"), &ch); js_int(js_get(pp, "bit_depth"), &bd);
            int isflac = !strcmp(codec, "flac");
            int ok = (isflac || !strcmp(codec, "pcm")) && sr == 44100 && ch == 2 && bd == 16;
            if(ok && isflac){                                   /* fLaC + STREAMINFO, standard base64 */
                char h64[1024]; uint8_t hdr[768]; int hn;
                if(!js_str(js_get(pp, "codec_header"), h64, sizeof h64, 0) || (hn = b64std_dec(h64, hdr, sizeof hdr)) < 42 || memcmp(hdr, "fLaC", 4)){
                    logf_("stream/start: FLAC without a usable codec_header"); ok = 0;
                } else { free(g_flac_hdr); g_flac_hdr = malloc(hn); if(g_flac_hdr){ memcpy(g_flac_hdr, hdr, hn); g_flac_hdr_n = (size_t)hn; } else ok = 0; }
            }
            c->flac = isflac; c->dec_logged = 0;
            int was = c->streaming && c->fmt_ok;
            c->fmt_ok = ok;
            logf_("stream/start %s %lld Hz %lld ch %lld bit%s%s", codec, sr, ch, bd, ok ? "" : "  (unsupported, ignoring)", was ? " (update)" : "");
            if(!was) fifo_reset();                              /* an update keeps the buffered audio */
            if(ok && !c->ap.open){
                if(ap_open(&c->ap) == 0){ c->streaming = 1; c->msg[0] = 0; logf_("airplay: session open"); }
                else { logf_("airplay: could not open a session"); snprintf(c->msg, sizeof c->msg, "AirPlay is off on the Disc"); }
            } else if(ok) c->streaming = 1;
            c->status_dirty = 1;
        }
        if(js_isobj(ar) && c->r_art){ if(!c->art_stream) logf_("artwork: stream open"); c->art_stream = 1; }
        return 0;
    }
    if(!strcmp(type, "stream/clear")){
        const char *roles = js_get(pl, "roles"); long long from;
        if(roles && !js_arr_has(roles, "player")) return 0;
        if(js_int(js_get(pl, "from_timestamp"), &from)){ logf_("stream/clear from %lld", from); fifo_clear_from(from); }
        else { logf_("stream/clear"); stream_stop(c, 0); }
        return 0;
    }
    if(!strcmp(type, "stream/end")){
        const char *roles = js_get(pl, "roles");
        if(!roles || js_arr_has(roles, "player")){
            if(c->streaming) logf_("stream/end (sent %lld packets, %lld dropped late)", c->sent_pkts, c->late_drops);
            stream_stop(c, 1); c->sent_pkts = c->late_drops = 0; c->status_dirty = 1;
        }
        if(!roles || js_arr_has(roles, "artwork")){ c->art_stream = 0; art_clear(c, 1); }
        return 0;
    }
    if(!strcmp(type, "server/command")){
        const char *pp = js_get(pl, "player"); char cmd[24] = ""; long long v; int b;
        if(!js_isobj(pp) || !c->r_player || !js_str(js_get(pp, "command"), cmd, sizeof cmd, 0)) return 0;
        if(!strcmp(cmd, "volume") && js_int(js_get(pp, "volume"), &v)) c->volume = v < 0 ? 0 : v > 100 ? 100 : (int)v;
        else if(!strcmp(cmd, "mute") && js_bool(js_get(pp, "mute"), &b)) c->muted = b;
        else return 0;
        c->gain = vol_gain(c->volume, c->muted);
        logf_("volume %d%s", c->volume, c->muted ? " (muted)" : "");
        send_state(c);
        return 0;
    }
    if(!strcmp(type, "group/update")){
        char st[24] = ""; if(js_str(js_get(pl, "playback_state"), st, sizeof st, 0)){ logf_("group: %s", st); c->ctl_playing = !strcmp(st, "playing"); c->status_dirty = 1; }
        return 0;
    }
    if(!strcmp(type, "server/state")){
        const char *md = js_get(pl, "metadata"), *ct = js_get(pl, "controller");
        if(js_isobj(md) && c->r_meta) on_metadata(c, md);
        if(js_isobj(ct) && c->r_ctl){
            const char *sc = js_get(ct, "supported_commands"), *e = sc ? js_skip(sc, 0) : NULL;
            if(sc && e && e - sc < (long)sizeof c->ctl_cmds){ memcpy(c->ctl_cmds, sc, e - sc); c->ctl_cmds[e - sc] = 0; }
        }
        return 0;
    }
    return 0;                                                   /* anything else: ignored (forward compatibility) */
}
/* artwork (roles/artwork/v1.md): announce / part / cancel, one transfer in flight; -1 to close */
static int on_artwork(cl_t *c, const uint8_t *b, size_t n){
    if(n < 2 || n > 65519) return -1;
    int ch = b[0] - 8, fl = b[1];
    if((fl & 0xFC) || fl == 3) return -1;
    if(!c->r_art || !c->art_stream) return 0;                  /* inactive role / no stream: discarded */
    if(fl == 2){                                                /* announce */
        if(n != 14 || c->art_fly >= 0) return -1;
        long long ts = 0; for(int i = 2; i < 10; i++) ts = ts << 8 | b[i];
        uint32_t tot = (uint32_t)b[10] << 24 | (uint32_t)b[11] << 16 | (uint32_t)b[12] << 8 | b[13];
        if(ch == 0){ c->art_pend = 0; free(c->pend_buf); c->pend_buf = NULL; }   /* an announce replaces the pending image */
        c->art_fly = ch; c->art_total = tot; c->art_got = 0; c->art_ts = ts;
        free(c->art_buf); c->art_buf = (ch == 0 && tot > 0 && tot <= ART_MAX) ? malloc(tot) : NULL;
        if(ch == 0 && tot > ART_MAX) logf_("cover: %u bytes, too large - skipped", tot);
    } else if(fl == 0){                                         /* part */
        if(c->art_fly != ch || (size_t)c->art_got + (n - 2) > c->art_total) return -1;
        if(c->art_buf) memcpy(c->art_buf + c->art_got, b + 2, n - 2);
        c->art_got += (uint32_t)(n - 2);
    } else {                                                    /* cancel */
        if(n != 2) return -1;
        if(c->art_fly == ch){ c->art_fly = -1; free(c->art_buf); c->art_buf = NULL; }
        if(ch == 0){ c->art_pend = 0; free(c->pend_buf); c->pend_buf = NULL; }
        return 0;
    }
    if(c->art_fly >= 0 && c->art_got == c->art_total){        /* complete: it becomes the pending image */
        int fly = c->art_fly; c->art_fly = -1;
        if(fly == 0 && (c->art_buf || c->art_total == 0)){
            free(c->pend_buf); c->pend_buf = c->art_buf; c->art_buf = NULL;
            c->pend_n = c->art_total; c->pend_ts = c->art_ts; c->art_pend = 1;
        } else { free(c->art_buf); c->art_buf = NULL; }
    }
    return 0;
}
static void art_tick(cl_t *c, long long now){                   /* a complete pending image shows at its time (never dropped) */
    if(!c->art_pend || (c->synced && tf_to_local(&c->tf, c->pend_ts) > now)) return;
    art_save(c, c->pend_buf, (int)c->pend_n);
    logf_("cover: %u bytes", c->pend_n);
    c->art_pend = 0; free(c->pend_buf); c->pend_buf = NULL;
}
static int on_binary(cl_t *c, const uint8_t *b, size_t n, long long rx_at){   /* decrypted plaintext; -1 to close */
    if(n < 1) return -1;
    if(c->frag_on && b[0] != 1) return -1;
    switch(b[0]){
        case 0: {
            if(n - 1 > 1u << 20) return -1;
            char *s = malloc(n); if(!s) return -1;
            memcpy(s, b + 1, n - 1); s[n - 1] = 0;
            if(strlen(s) != n - 1){ free(s); return -1; }
            int r = on_json(c, s, rx_at); free(s); return r;
        }
        case 1: {                                               /* fragmentation */
            if(n < 2 || (b[1] & 0xFC)) return -1;
            int first = b[1] & 2, last = b[1] & 1; size_t off = 2;
            if(first){ if(c->frag_on || n < 3 || b[2] == 1) return -1; c->frag_on = 1; c->frag_type = b[2]; c->frag_n = 0; off = 3; }
            else if(!c->frag_on) return -1;
            if(c->frag_n + (n - off) > (1u << 20)) return -1;
            if(grow(&c->frag, &c->frag_cap, c->frag_n + (n - off) + 1)) return -1;
            if(first) c->frag[0] = (uint8_t)c->frag_type, c->frag_n = 1;
            memcpy(c->frag + c->frag_n, b + off, n - off); c->frag_n += n - off;
            if(!last) return 0;
            c->frag_on = 0;
            uint8_t *m = c->frag; size_t mn = c->frag_n; c->frag = NULL; c->frag_cap = 0;
            int r = on_binary(c, m, mn, rx_at); free(m); return r;
        }
        case 4: {                                               /* player audio */
            if(n < 13) return -1;
            if(!c->r_player || !c->act_playback || !c->fmt_ok) return 0;
            long long ts = 0; for(int i = 1; i <= 8; i++) ts = ts << 8 | b[i];
            if(c->flac){
                flac_ctx fc = { ts, 0 };
                long got = ssf_decode(g_flac_hdr, g_flac_hdr_n, b + 13, n - 13, flac_out, &fc);
                if(got < 0){ if(!c->dec_logged++) logf_("flac: a chunk did not decode"); return 0; }
                c->dec_frames += got;
                if(g_debug && c->dec_frames >= AP_RATE && c->dec_logged < 2){ c->dec_logged = 2; logf_("flac: decoding (%lld frames so far, peak %d)", c->dec_frames, fc.peak); }
                return 0;
            }
            int frames = (int)((n - 13) / 4);
            int16_t tmp[4096 * 2];
            const uint8_t *p = b + 13;
            while(frames > 0){
                int k = frames > 4096 ? 4096 : frames;
                for(int i = 0; i < 2 * k; i++) tmp[i] = (int16_t)(p[2 * i] | p[2 * i + 1] << 8);   /* PCM is little-endian */
                fifo_push(tmp, k, ts);
                ts += (long long)k * 1000000 / AP_RATE; p += 4 * k; frames -= k;
            }
            return 0;
        }
        case 8: case 9: case 10: case 11: return on_artwork(c, b, n);
        default: return 0;                                      /* ids we don't implement: ignored */
    }
}
/* hand packets to AirPlay so each plays when the server wants it: send at (local play time - receiver latency) */
static void pump_audio(cl_t *c){
    if(!c->ap.open || !c->synced) return;
    int16_t pkt[AP_FRAMES * 2];
    long long now = now_us();
    while(g_fn >= AP_FRAMES){
        long long play_local = tf_to_local(&c->tf, g_fifo_ts);
        long long send_at = play_local - (long long)c->latency_ms * 1000 + (long long)c->delay_ms * 1000;
        if(now + 40000 < send_at) break;                     /* batches: everything due in the next 40 ms goes now */
        fifo_pop(pkt, AP_FRAMES);
        if(now - send_at > 400000){ c->late_drops++; continue; }   /* hopelessly late: skip it */
        if(c->gain < 32768) for(int i = 0; i < AP_FRAMES * 2; i++) pkt[i] = (int16_t)((pkt[i] * c->gain) >> 15);
        ap_send(&c->ap, pkt); c->sent_pkts++;
    }
}
static void session_reset(cl_t *c){
    stream_stop(c, 1); c->fmt_ok = 0;
    art_clear(c, 0);
    ssc_wipe(&c->rx, sizeof c->rx); ssc_wipe(&c->tx, sizeof c->tx); ssc_wipe(c->h, 32);
    free(c->frag); c->frag = NULL; c->frag_cap = c->frag_n = 0; c->frag_on = 0;
    if(c->pm != PM_NONE) pair_end(c, NULL);
    c->phase = PH_INIT; c->psk_kind = PSK_NONE; c->rehs = 0;
    c->activated = c->act_playback = c->act_pairing = 0; c->r_player = c->r_meta = c->r_art = c->r_ctl = 0;
    c->state_sent = 0; c->pairing_index = 0; c->server_name[0] = 0; c->server_id[0] = 0;
    tf_reset(&c->tf); c->synced = 0; c->burst_left = 0; c->t_pending = 0; c->next_burst = 0;
    md_clear(c); c->ctl_cmds[0] = 0; c->art_stream = 0; c->ctl_playing = 0;
}
static int run_once(cl_t *c){
    session_reset(c);
    if(ws_connect(&c->ws, c->host, c->port, "/sendspin") < 0) return -1;
    logf_("WebSocket open to %s:%d", c->host, c->port);
    c->cinit_n = snprintf(c->cinit, sizeof c->cinit, "{\"type\":\"client/init\",\"payload\":{\"client_id\":\"%s\",\"version\":1,\"suite\":\"" SSC_SUITE "\"}}", c->keys.client_id);
    send_plain_json(c, c->cinit);
    c->phase = PH_INIT; c->phase_until = now_us() + 15000000;
    uint8_t *sinit = NULL; size_t sinit_n = 0; int ret = 0;
    while(!g_stop){
        int r;
        while((r = ws_poll_msg(&c->ws)) == 1){
            long long rx_at = now_us(); int bad = 0;
            if(c->phase == PH_INIT || c->phase == PH_NOISE){
                if(c->ws.msgop != 1){ bad = 1; }
                else {
                    const char *s = (const char *)c->ws.msg, *pl = js_get(s, "payload"); char type[32] = "";
                    if(g_debug) logf_("<< %.300s", s);
                    js_str(js_get(s, "type"), type, sizeof type, 0);
                    if(c->phase == PH_INIT && !strcmp(type, "server/error")){
                        char why[40] = ""; js_str(js_get(pl, "reason"), why, sizeof why, 0);
                        logf_("server refused the connection: %s", why);
                        snprintf(c->msg, sizeof c->msg, "Music Assistant refused it (%s)", why); bad = 1;
                    } else if(c->phase == PH_INIT && !strcmp(type, "server/init")){
                        char sid[64] = ""; long long v = 0;
                        if(!js_str(js_get(pl, "server_id"), sid, sizeof sid, 0) || strlen(sid) != 43 || ssc_b64url_dec(sid, 43, c->rs, 32) != 32 ||
                           !js_int(js_get(pl, "version"), &v) || v != 1){ logf_("server/init: not one we understand"); bad = 1; }
                        else {
                            snprintf(c->server_id, sizeof c->server_id, "%s", sid);
                            free(sinit); sinit = malloc(c->ws.msgn); if(!sinit){ bad = 1; } else { memcpy(sinit, c->ws.msg, c->ws.msgn); sinit_n = c->ws.msgn; }
                            c->phase = PH_NOISE; c->phase_until = now_us() + 30000000;
                        }
                    } else if(c->phase == PH_NOISE && !strcmp(type, "noise/handshake")){
                        if(noise_initial(c, sinit, sinit_n, pl) < 0){ logf_("noise: handshake failed"); bad = 1; }
                        else { c->phase = PH_HELLO; c->phase_until = now_us() + 30000000; }
                    } else bad = 1;
                }
            } else {
                if(c->ws.msgop != 2 || c->ws.msgn < 17 || c->ws.msgn > 65535){ logf_("protocol: unexpected message in the encrypted channel"); bad = 1; }
                else if(grow(&c->ptbuf, &c->ptcap, c->ws.msgn) || ssc_cipher_open(&c->rx, c->ws.msg, c->ws.msgn, c->ptbuf)){ logf_("noise: a message does not decrypt"); bad = 1; }
                else { int r2 = on_binary(c, c->ptbuf, c->ws.msgn - 16, rx_at); if(r2 < 0){ if(r2 == -1) logf_("protocol: closing (bad message from the server)"); bad = 1; } }
            }
            if(bad){ ret = 0; goto out; }
        }
        if(r < 0){ logf_("connection closed"); break; }
        long long now = now_us();
        if(c->phase != PH_LIVE && now > c->phase_until){
            logf_("timed out waiting for the server (%s)", c->phase == PH_INIT ? "server/init" : c->phase == PH_NOISE ? "handshake" : c->phase == PH_HELLO ? "server/hello" : "server/activate");
            if(c->phase == PH_INIT) snprintf(c->msg, sizeof c->msg, "Needs Music Assistant 2.10.7 or newer");
            break;
        }
        if(c->phase == PH_LIVE){
            time_tick(c, now);
            if(c->md_next.valid && c->synced && tf_to_local(&c->tf, c->md_next.ts) <= now){ meta_t m = c->md_next; c->md_next.valid = 0; md_apply(c, &m); }
            art_tick(c, now);
            if(c->pm == PM_DYNAMIC && !c->p_finalizing && now > c->p_until) pair_abort(c, "attempt_timeout", "Pairing timed out");
        }
        pump_audio(c);
        if(c->ap.open) ap_service(&c->ap);
        cmd_poll(c);
        if(c->status_dirty || now - c->status_at > 5000000) status_write(c);
        /* sleep until data arrives or the next batch is due (40 ms) - not a 5 ms spin: this runs on the Disc's battery */
        struct pollfd p[2] = { { c->ws.fd, POLLIN, 0 }, { c->cmdfd, POLLIN, 0 } };
        poll(p, c->cmdfd >= 0 ? 2 : 1, 40);
    }
    if(g_stop && c->phase >= PH_HELLO) send_goodbye(c, "shutdown");
out:
    free(sinit);
    session_reset(c);
    ws_close(&c->ws);
    c->status_dirty = 1;
    return ret;
}

/* ---------------------------------------------------------------- mDNS: find Music Assistant's Sendspin server */
static int dns_name(const uint8_t *m, int n, int o, char *out, int on){   /* returns offset after the name, -1 bad */
    int end = -1, len = 0, hops = 0;
    while(o < n){
        int l = m[o];
        if(l == 0){ o++; break; }
        if((l & 0xC0) == 0xC0){ if(o + 1 >= n || ++hops > 16) return -1; if(end < 0) end = o + 2; o = ((l & 0x3F) << 8) | m[o + 1]; continue; }
        if(o + 1 + l > n) return -1;
        if(len && len < on - 1) out[len++] = '.';
        for(int i = 0; i < l && len < on - 1; i++) out[len++] = (char)m[o + 1 + i];
        o += 1 + l;
    }
    out[len] = 0;
    return end >= 0 ? end : o;
}
static int mdns_find(char *host, int hn, int *port){
    int fd = socket(AF_INET, SOCK_DGRAM, 0); if(fd < 0) return -1;
    struct sockaddr_in me = {0}; me.sin_family = AF_INET;
    bind(fd, (void *)&me, sizeof me);
    uint8_t q[64]; int o = 12; memset(q, 0, 12); q[5] = 1;                         /* one question */
    const char *labels[] = { "_sendspin-server", "_tcp", "local" };
    for(int i = 0; i < 3; i++){ int l = (int)strlen(labels[i]); q[o++] = l; memcpy(q + o, labels[i], l); o += l; }
    q[o++] = 0; q[o++] = 0; q[o++] = 12; q[o++] = 0x80; q[o++] = 1;                /* PTR, IN, unicast response wanted */
    struct sockaddr_in g = {0}; g.sin_family = AF_INET; g.sin_port = htons(5353); g.sin_addr.s_addr = inet_addr("224.0.0.251");
    char target[128] = ""; uint32_t addr = 0; int sport = 0, found = 0;
    long long until = now_us() + 3000000;
    for(int tries = 0; !found && now_us() < until; ){
        if(tries < 3 && (tries == 0 || now_us() > until - 3000000 + tries * 1000000)){ sendto(fd, q, o, 0, (void *)&g, sizeof g); tries++; }
        struct pollfd p = { fd, POLLIN, 0 };
        if(poll(&p, 1, 200) != 1) continue;
        uint8_t m[1500]; struct sockaddr_in from; socklen_t fl = sizeof from;
        int n = recvfrom(fd, m, sizeof m, 0, (void *)&from, &fl); if(n < 12) continue;
        int qd = m[4] << 8 | m[5], an = (m[6] << 8 | m[7]) + (m[8] << 8 | m[9]) + (m[10] << 8 | m[11]);
        int p2 = 12; char nm[256];
        for(int i = 0; i < qd && p2 > 0; i++){ p2 = dns_name(m, n, p2, nm, sizeof nm); if(p2 > 0) p2 += 4; }
        uint32_t a_rec[8]; char a_name[8][128]; int na = 0;
        for(int i = 0; i < an && p2 > 0 && p2 + 10 <= n; i++){
            p2 = dns_name(m, n, p2, nm, sizeof nm); if(p2 < 0 || p2 + 10 > n) break;
            int type = m[p2] << 8 | m[p2 + 1], rl = m[p2 + 8] << 8 | m[p2 + 9]; int rd = p2 + 10;
            if(rd + rl > n) break;
            if(type == 33 && rl >= 7 && strstr(nm, "_sendspin-server._tcp")){          /* SRV */
                sport = m[rd + 4] << 8 | m[rd + 5]; dns_name(m, n, rd + 6, target, sizeof target);
            } else if(type == 1 && rl == 4 && na < 8){ memcpy(&a_rec[na], m + rd, 4); snprintf(a_name[na], 128, "%s", nm); na++; }
            p2 = rd + rl;
        }
        if(sport){
            for(int i = 0; i < na; i++) if(!strcasecmp(a_name[i], target)){ addr = a_rec[i]; break; }
            if(!addr) addr = from.sin_addr.s_addr;                                      /* the responder itself */
            found = 1;
        }
    }
    close(fd);
    if(!found) return -1;
    struct in_addr ia; ia.s_addr = addr; snprintf(host, hn, "%s", inet_ntoa(ia)); *port = sport;
    return 0;
}

int sendspin_main(int argc, char **argv){
    setvbuf(stderr, NULL, _IOLBF, 0);
    g_debug = getenv("SS_DEBUG") != NULL;
    if(getenv("SS_DIR") && getenv("SS_DIR")[0]) snprintf(g_dir, sizeof g_dir, "%s", getenv("SS_DIR"));
    if(argc < 2){ fprintf(stderr, "usage: %s <server[:port]|auto> [name] [client_id] [delay_ms] [airplay_latency_ms] [airplay_port] [unpaired 0|1]\n", argv[0]); return 2; }
    signal(SIGPIPE, SIG_IGN); signal(SIGINT, on_sig); signal(SIGTERM, on_sig); signal(SIGHUP, on_sig);
    static char host[128]; snprintf(host, sizeof host, "%s", argv[1]);
    int autodisc = !strcmp(host, "auto") || !host[0];
    int port = 8927; char *colon = strchr(host, ':'); if(colon){ *colon = 0; port = atoi(colon + 1); }
    static cl_t c; memset(&c, 0, sizeof c);
    c.host = host; c.port = port;
    c.name = argc > 2 && argv[2][0] ? argv[2] : "Snowsky Disc";
    /* argv[3], the old MAC-based client_id, is no longer used: Sendspin 1.0's client_id is the Disc's public key */
    c.delay_ms = argc > 4 ? atoi(argv[4]) : 0;
    c.latency_ms = argc > 5 ? atoi(argv[5]) : 2000;
    c.ap.port = argc > 6 ? atoi(argv[6]) : 5000;
    c.unpaired_ok = argc > 7 ? atoi(argv[7]) != 0 : 1;
    c.ap.rtsp = c.ap.aud = c.ap.ctl = c.ap.tim = -1;
    c.art_fly = -1;
    c.volume = 100; c.gain = vol_gain(c.volume, 0); c.ap.vol_db = 0.0;   /* receiver at full scale; MA volume in software */
    g_fifo = malloc(sizeof(int16_t) * 2 * FIFO_FRAMES);
    if(!g_fifo) return 1;
    mkdir(g_dir, 0755);
    char cmdp[128]; dpath(cmdp, sizeof cmdp, "cmd");
    unlink(cmdp); mkfifo(cmdp, 0600);
    c.cmdfd = open(cmdp, O_RDWR | O_NONBLOCK);
    if(ssc_keys_load(&c.keys, 1)){
        logf_("keys: can't read or create %s", ssc_keys_path());
        snprintf(c.msg, sizeof c.msg, "Can't store the Sendspin keys"); c.fails = 2; status_write(&c);
        for(int i = 0; i < 600 && !g_stop; i++) usleep(100000);
        return 1;
    }
    logf_(DISCO_NAME " " DISCO_VERSION " sendspin: server %s, name \"%s\", id %s, delay %d ms, AirPlay port %d, latency %d ms, unpaired %s, %d pairing(s)",
          autodisc ? "auto" : host, c.name, c.keys.client_id, c.delay_ms, c.ap.port, c.latency_ms, c.unpaired_ok ? "allowed" : "refused", c.keys.nrec);
    while(!g_stop){
        if(autodisc){
            snprintf(c.server_lbl, sizeof c.server_lbl, "On your network"); status_write(&c);
            char h[64]; int pt;
            if(mdns_find(h, sizeof h, &pt) == 0){ snprintf(host, sizeof host, "%s", h); c.port = pt; logf_("found Music Assistant at %s:%d", h, pt); }
            else { c.fails++; snprintf(c.msg, sizeof c.msg, "Not found - set the server in Settings"); status_write(&c);
                   { for(int i = 0; i < 50 && !g_stop; i++) usleep(100000); } continue; }
        }
        snprintf(c.server_lbl, sizeof c.server_lbl, "%s", host); status_write(&c);
        char keep[96]; snprintf(keep, sizeof keep, "%s", c.msg);
        if(run_once(&c) < 0){ c.fails++; snprintf(c.msg, sizeof c.msg, "No answer from %s", host); }
        else if(c.msg[0] && strcmp(c.msg, keep)) c.fails++;            /* refused / too old: shown, not "connecting" */
        status_write(&c);
        if(g_stop) break;
        logf_("reconnecting in 3 s");
        for(int i = 0; i < 30 && !g_stop; i++) usleep(100000);
    }
    char st[128]; dpath(st, sizeof st, "status"); unlink(st);
    ssc_wipe(&c.keys, sizeof c.keys);
    logf_("bye");
    return 0;
}
#ifdef SS_STANDALONE
int main(int argc, char **argv){ return sendspin_main(argc, argv); }
#endif
