/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Sendspin player for Music Assistant ("MA Sendspin"), run by mq_ui as a child: mq_ui --sendspin ...
 * mq_player owns the audio hardware, so the audio goes to the Disc's own AirPlay receiver (loopback, ss_airplay.h).
 * Legacy (unencrypted) Sendspin as accepted by Music Assistant 2.10: player@v1 (raw PCM 44.1/16/2, clock sync,
 * volume/mute in software), metadata@v1, artwork@v1 (JPEG cover), controller@v1 (play/pause/next/previous).
 * UI link: /tmp/ma/status (key=value, rewritten atomically), /tmp/ma/cmd (FIFO, one command per line),
 *          /tmp/ma/a<N>/cover.jpg (the current cover; the UI shows <dir>/track as the "song", its sidecar cover).
 * Usage: --sendspin <server[:port]|auto> <name> <client_id> <delay_ms> [airplay_latency_ms] [airplay_port] */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "ss_airplay.h"
#include "version.h"
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
/* tiny JSON readers: find "key" anywhere after `from` (good enough for Sendspin's flat-ish messages) */
static const char *jkey(const char *s, const char *key){
    char pat[80]; snprintf(pat, sizeof pat, "\"%s\"", key);
    for(const char *p = strstr(s, pat); p; p = strstr(p + 1, pat)){   /* a key is followed by ':', a string value isn't */
        const char *q = p + strlen(pat); while(*q == ' ') q++;
        if(*q != ':') continue;
        q++; while(*q == ' ') q++;
        return q;
    }
    return NULL;
}
static int jstr(const char *s, const char *key, char *out, int n){
    const char *p = jkey(s, key); if(!p || *p != '"') return 0;
    p++; int o = 0;
    while(*p && *p != '"' && o < n - 1){ if(*p == '\\' && p[1]) p++; out[o++] = *p++; }
    out[o] = 0; return 1;
}
static int jnum(const char *s, const char *key, long long *v){
    const char *p = jkey(s, key); if(!p || !(isdigit((unsigned char)*p) || *p == '-')) return 0;
    *v = strtoll(p, NULL, 10); return 1;
}
static int jbool(const char *s, const char *key, int *v){
    const char *p = jkey(s, key); if(!p) return 0;
    if(!strncmp(p, "true", 4)){ *v = 1; return 1; } if(!strncmp(p, "false", 5)){ *v = 0; return 1; } return 0;
}

/* ---------------------------------------------------------------- WebSocket client */
typedef struct { int fd; uint8_t *rx; int rxn, rxcap; uint8_t *msg; int msgn, msgcap, msgop; } ws_t;
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
static int ws_text(ws_t *w, const char *s){ return ws_send_frame(w, 1, s, (int)strlen(s)); }
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
    w->fd = fd; w->rxcap = 1 << 16; w->rx = malloc(w->rxcap); w->msgcap = 1 << 16; w->msg = malloc(w->msgcap);
    return 0;
}
static void ws_close(ws_t *w){ if(w->fd >= 0){ ws_send_frame(w, 8, "", 0); close(w->fd); } w->fd = -1; free(w->rx); free(w->msg); w->rx = w->msg = NULL; }
/* reads what's there; returns 1 with a complete message in w->msg (op 1 text / 2 binary), 0 none, -1 closed */
static int ws_poll_msg(ws_t *w){
    for(;;){
        /* try to parse one frame from rx */
        if(w->rxn >= 2){
            uint8_t *b = w->rx; int fin = b[0] & 0x80, op = b[0] & 15, masked = b[1] & 0x80;
            long long len = b[1] & 127; int hl = 2;
            if(len == 126){ if(w->rxn < 4) goto more; len = b[2] << 8 | b[3]; hl = 4; }
            else if(len == 127){ if(w->rxn < 10) goto more; len = 0; for(int i = 0; i < 8; i++) len = len << 8 | b[2 + i]; hl = 10; }
            if(masked) hl += 4;
            if(len > (64 << 20)) return -1;
            if(w->rxn < hl + len){
                if(hl + len > w->rxcap){ w->rxcap = (int)(hl + len) + 4096; w->rx = realloc(w->rx, w->rxcap); }
                goto more;
            }
            uint8_t *pl = b + hl;
            if(masked) for(long long i = 0; i < len; i++) pl[i] ^= b[hl - 4 + (i & 3)];
            int done = 0;
            if(op == 8){ return -1; }
            else if(op == 9){ ws_send_frame(w, 10, pl, (int)len); }
            else if(op == 10){ }
            else if(op == 0 || op == 1 || op == 2){
                if(op){ w->msgn = 0; w->msgop = op; }
                if(w->msgn + len + 1 > w->msgcap){ w->msgcap = (int)(w->msgn + len) + 4096; w->msg = realloc(w->msg, w->msgcap); }
                memcpy(w->msg + w->msgn, pl, len); w->msgn += (int)len; w->msg[w->msgn] = 0;
                if(fin) done = 1;
            }
            int used = hl + (int)len; memmove(w->rx, w->rx + used, w->rxn - used); w->rxn -= used;
            if(done) return 1;
            continue;
        }
    more:
        if(w->rxn >= w->rxcap){ w->rxcap *= 2; w->rx = realloc(w->rx, w->rxcap); }
        int r = recv(w->fd, w->rx + w->rxn, w->rxcap - w->rxn, 0);
        if(r == 0) return -1;
        if(r < 0) return (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) ? 0 : -1;
        w->rxn += r;
    }
}

/* ---------------------------------------------------------------- clock sync (min-RTT over a window) */
#define TS_N 8
typedef struct { long long off[TS_N], rtt[TS_N]; int n, i; long long offset; int synced; } tsync_t;
static void ts_add(tsync_t *t, long long t0, long long srv_rx, long long srv_tx, long long t3){
    long long rtt = (t3 - t0) - (srv_tx - srv_rx);
    long long off = ((srv_rx - t0) + (srv_tx - t3)) / 2;          /* server = local + off */
    t->off[t->i] = off; t->rtt[t->i] = rtt; t->i = (t->i + 1) % TS_N; if(t->n < TS_N) t->n++;
    int best = 0; for(int k = 1; k < t->n; k++) if(t->rtt[k] < t->rtt[best]) best = k;
    t->offset = t->off[best]; t->synced = t->n >= 3;
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

/* ---------------------------------------------------------------- client */
static volatile int g_stop;
static void on_sig(int s){ (void)s; g_stop = 1; }

typedef struct {
    const char *host, *name, *id; int port;
    int latency_ms;            /* how long the AirPlay receiver holds audio before it plays */
    int volume, muted, gain;
    ws_t ws; tsync_t ts; airplay_t ap;
    int hello_ok, state_sent, streaming, fmt_ok;
    long long next_time, pending_t0;
    long long late_drops, sent_pkts;
    int delay_ms;              /* user Sync Delay: + plays later */
    int cmdfd;                 /* /tmp/ma/cmd FIFO (opened read-write, so reads never see EOF) */
    /* what Music Assistant says is playing */
    char title[160], artist[160], album[160];
    long long dur_ms, prog_ms, prog_at_us; int speed;   /* progress at a local monotonic time; speed 0 = paused */
    int art_n; char art_dir[64];
    int ctl_playing;           /* group playback state: playing */
    int status_dirty; long long status_at;
    char server_lbl[96], msg[96]; int fails;
} cl_t;

/* Music Assistant's volume is applied here, in software: the Disc's AirPlay receiver ignores AirPlay volume (its own
 * volume is the Disc's). Cubic curve, the usual feel for 0-100 sliders; gain in Q15. */
static int vol_gain(int v, int muted){ if(muted || v <= 0) return 0; if(v >= 100) return 32768; double g = v / 100.0; return (int)(g * g * g * 32768 + 0.5); }

/* ---------------------------------------------------------------- the UI link */
static void status_write(cl_t *c){
    const char *st = !c->hello_ok ? (c->fails >= 2 ? "error" : "connecting")
                   : (c->streaming && c->speed != 0) ? "playing" : "ready";
    FILE *f = fopen("/tmp/ma/status.tmp", "w"); if(!f) return;
    fprintf(f, "state=%s\nserver=%s\nmsg=%s\n", st, c->server_lbl, c->msg);
    if(c->hello_ok && c->title[0]){
        fprintf(f, "title=%s\nartist=%s\nalbum=%s\nduration_ms=%lld\nprogress_ms=%lld\nprogress_at_ms=%lld\nspeed=%d\n",
                c->title, c->artist, c->album, c->dur_ms, c->prog_ms, c->prog_at_us / 1000, c->speed);
        fprintf(f, "track=%s/track\n", c->art_dir[0] ? c->art_dir : "/tmp/ma/a0");
    }
    fprintf(f, "pid=%d\n", (int)getpid());
    fclose(f);
    rename("/tmp/ma/status.tmp", "/tmp/ma/status");
    c->status_dirty = 0; c->status_at = now_us();
}
static void art_save(cl_t *c, const uint8_t *img, int n){
    char dir[64], path[96], old[64];
    snprintf(old, sizeof old, "%s", c->art_dir);
    c->art_n++;
    snprintf(dir, sizeof dir, "/tmp/ma/a%d", c->art_n);
    mkdir(dir, 0755);
    if(n > 0){
        snprintf(path, sizeof path, "%s/cover.jpg", dir);
        FILE *f = fopen(path, "wb"); if(f){ if(fwrite(img, 1, n, f) != (size_t)n) logf_("cover: short write"); fclose(f); }
    }
    snprintf(c->art_dir, sizeof c->art_dir, "%s", dir);
    if(old[0]){ snprintf(path, sizeof path, "%s/cover.jpg", old); unlink(path); rmdir(old); }
    c->status_dirty = 1;
}
static void send_command(cl_t *c, const char *cmd){
    char m[160]; snprintf(m, sizeof m, "{\"type\":\"client/command\",\"payload\":{\"controller\":{\"command\":\"%s\"}}}", cmd);
    ws_text(&c->ws, m);
    logf_("command: %s", cmd);
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
        if(c->hello_ok){
            if(!strcmp(buf, "toggle")) send_command(c, (c->ctl_playing || (c->streaming && c->speed)) ? "pause" : "play");
            else if(!strcmp(buf, "play") || !strcmp(buf, "pause") || !strcmp(buf, "next") || !strcmp(buf, "previous")) send_command(c, buf);
            else if(!strncmp(buf, "seek ", 5)){
                char m[192]; snprintf(m, sizeof m, "{\"type\":\"client/command\",\"payload\":{\"controller\":{\"command\":\"seek\",\"position_ms\":%ld}}}", atol(buf + 5));
                ws_text(&c->ws, m); logf_("command: seek %ld", atol(buf + 5));
            }
        }
        int used = (int)(nl - buf) + 1; memmove(buf, buf + used, bn - used + 1); bn -= used;
    }
    if(bn >= (int)sizeof buf - 1) bn = 0;
}

static void send_hello(cl_t *c){
    char m[1600];
    snprintf(m, sizeof m,
        "{\"type\":\"client/hello\",\"payload\":{\"client_id\":\"%s\",\"name\":\"%s\",\"version\":1,"
        "\"supported_roles\":[\"player@v1\",\"metadata@v1\",\"artwork@v1\",\"controller@v1\"],"
        "\"artwork@v1_support\":{\"channels\":[{\"source\":\"album\",\"format\":\"jpeg\",\"media_width\":360,\"media_height\":360}]},"
        "\"device_info\":{\"product_name\":\"Snowsky Disc\",\"manufacturer\":\"FiiO\",\"software_version\":\"" DISCO_NAME " " DISCO_VERSION "\"},"
        "\"player@v1_support\":{\"supported_formats\":[{\"codec\":\"pcm\",\"channels\":2,\"sample_rate\":44100,\"bit_depth\":16}],"
        "\"buffer_capacity\":%d,\"supported_commands\":[\"volume\",\"mute\"]}}}",
        c->id, c->name, FIFO_FRAMES * 4 / 2);
    ws_text(&c->ws, m);
}
static void send_state(cl_t *c, int initial){
    char m[400];
    if(initial)
        snprintf(m, sizeof m, "{\"type\":\"client/state\",\"payload\":{\"available\":true,\"player\":{\"volume\":%d,\"muted\":%s,"
                 "\"static_delay_ms\":0,\"required_lead_time_ms\":%d,\"min_buffer_ms\":%d}}}", c->volume, c->muted ? "true" : "false",
                 c->latency_ms + 700 + (c->delay_ms < 0 ? -c->delay_ms : 0),     /* an earlier Sync Delay needs the audio sooner */
                 c->latency_ms + 500 + (c->delay_ms < 0 ? -c->delay_ms : 0));
    else
        snprintf(m, sizeof m, "{\"type\":\"client/state\",\"payload\":{\"player\":{\"volume\":%d,\"muted\":%s}}}", c->volume, c->muted ? "true" : "false");
    ws_text(&c->ws, m);
}
static void send_time(cl_t *c){
    char m[128]; c->pending_t0 = now_us();
    snprintf(m, sizeof m, "{\"type\":\"client/time\",\"payload\":{\"client_transmitted\":%lld}}", c->pending_t0);
    ws_text(&c->ws, m);
}
static void stream_stop(cl_t *c, int teardown){
    fifo_reset();
    if(teardown){ ap_close(&c->ap); c->streaming = 0; }
    else ap_flush(&c->ap);
}
static int g_debug;
static void on_text(cl_t *c, const char *s){
    char type[48] = ""; jstr(s, "type", type, sizeof type);
    if(g_debug && strcmp(type, "server/time")) logf_("<< %.300s", s);
    if(!strcmp(type, "server/hello")){
        char nm[96] = "?"; jstr(s, "name", nm, sizeof nm);
        logf_("connected to %s", nm);
        if(strstr(s, "\"active_roles\"") && !strstr(s, "player@v1")) logf_("warning: the server did not activate the player role");
        c->hello_ok = 1; c->next_time = 0; c->fails = 0; c->msg[0] = 0; c->status_dirty = 1;
    } else if(!strcmp(type, "server/time")){
        long long t3 = now_us(), t0, rx, tx;
        if(jnum(s, "client_transmitted", &t0) && jnum(s, "server_received", &rx) && jnum(s, "server_transmitted", &tx)){
            ts_add(&c->ts, t0, rx, tx, t3);
            if(c->ts.synced && !c->state_sent){ send_state(c, 1); c->state_sent = 1; logf_("clock synced (rtt %lld us), ready", (t3 - t0)); }
        }
    } else if(!strcmp(type, "stream/start")){
        if(!strstr(s, "\"player\"")) return;                /* an artwork-only start: the audio is untouched */
        char codec[16] = ""; long long sr = 0, ch = 0, bd = 0;
        jstr(s, "codec", codec, sizeof codec); jnum(s, "sample_rate", &sr); jnum(s, "channels", &ch); jnum(s, "bit_depth", &bd);
        c->fmt_ok = !strcmp(codec, "pcm") && sr == 44100 && ch == 2 && bd == 16;
        logf_("stream/start %s %lld Hz %lld ch %lld bit%s", codec, sr, ch, bd, c->fmt_ok ? "" : "  (unsupported, ignoring)");
        fifo_reset();
        if(c->fmt_ok && !c->ap.open){
            if(ap_open(&c->ap) == 0){ c->streaming = 1; c->msg[0] = 0; logf_("airplay: session open"); }
            else { logf_("airplay: could not open a session"); snprintf(c->msg, sizeof c->msg, "AirPlay is off on the Disc"); }
            c->status_dirty = 1;
        } else if(c->ap.open) ap_flush(&c->ap);
    } else if(!strcmp(type, "stream/clear")){
        if(strstr(s, "\"roles\"") && !strstr(s, "\"player")) return;   /* clears another role only */
        logf_("stream/clear"); stream_stop(c, 0);
    } else if(!strcmp(type, "stream/end")){
        if(strstr(s, "\"roles\"") && !strstr(s, "\"player")) return;   /* ends another role only */
        logf_("stream/end (sent %lld packets, %lld dropped late)", c->sent_pkts, c->late_drops); stream_stop(c, 1);
        c->sent_pkts = c->late_drops = 0; c->status_dirty = 1;
    } else if(!strcmp(type, "server/command")){
        char cmd[16] = ""; long long v; int b;
        jstr(s, "command", cmd, sizeof cmd);
        if(!strcmp(cmd, "volume") && jnum(s, "volume", &v)){ c->volume = (int)v; }
        else if(!strcmp(cmd, "mute") && jbool(s, "mute", &b)){ c->muted = b; }
        else return;
        c->gain = vol_gain(c->volume, c->muted);
        logf_("volume %d%s", c->volume, c->muted ? " (muted)" : "");
        send_state(c, 0);
    } else if(!strcmp(type, "group/update")){
        char st[24] = ""; if(jstr(s, "playback_state", st, sizeof st)){ logf_("group: %s", st); c->ctl_playing = !strcmp(st, "playing"); c->status_dirty = 1; }
    } else if(!strcmp(type, "server/state")){
        const char *md = strstr(s, "\"metadata\"");
        if(md){
            char v[160]; const char *k;
            /* delta updates: a key present sets (null clears), an absent key keeps its value */
            #define MD_STR(key, dst) if((k = jkey(md, key))){ if(!strncmp(k, "null", 4)) dst[0] = 0; else if(jstr(md, key, v, sizeof v)) snprintf(dst, sizeof dst, "%s", v); }
            MD_STR("title", c->title) MD_STR("artist", c->artist) MD_STR("album", c->album)
            #undef MD_STR
            long long ts, pr, du, sp;
            if((k = jkey(md, "progress"))){
                if(!strncmp(k, "null", 4)){ c->prog_ms = c->dur_ms = 0; c->speed = 0; c->prog_at_us = now_us(); }
                else if(jnum(md, "track_progress", &pr) && jnum(md, "track_duration", &du)){
                    c->prog_ms = pr; c->dur_ms = du; c->speed = jnum(md, "playback_speed", &sp) ? (int)sp : 1000;
                    c->prog_at_us = (jnum(md, "timestamp", &ts) && c->ts.synced) ? ts - c->ts.offset : now_us();
                }
            }
            if(c->title[0]) logf_("now playing: %s - %s (%lld/%lld ms, speed %d)", c->artist, c->title, c->prog_ms, c->dur_ms, c->speed);
            c->status_dirty = 1;
        }
    }
}
static void on_binary(cl_t *c, const uint8_t *b, int n){
    if(n >= 9 && b[0] == 8){ art_save(c, b + 9, n - 9); logf_("cover: %d bytes", n - 9); return; }   /* 8 = artwork channel 0 */
    if(n < 9 || b[0] != 4 || !c->fmt_ok) return;              /* 4 = player audio chunk */
    long long ts = 0; for(int i = 1; i <= 8; i++) ts = ts << 8 | b[i];
    int frames = (n - 9) / 4;
    int16_t tmp[4096 * 2];
    const uint8_t *p = b + 9;
    while(frames > 0){
        int k = frames > 4096 ? 4096 : frames;
        for(int i = 0; i < 2 * k; i++) tmp[i] = (int16_t)(p[2 * i] | p[2 * i + 1] << 8);   /* PCM is little-endian */
        fifo_push(tmp, k, ts);
        ts += (long long)k * 1000000 / AP_RATE; p += 4 * k; frames -= k;
    }
}
/* hand packets to AirPlay so each plays when the server wants it: send at (local play time - receiver latency) */
static void pump_audio(cl_t *c){
    if(!c->ap.open || !c->ts.synced) return;
    int16_t pkt[AP_FRAMES * 2];
    long long now = now_us();
    while(g_fn >= AP_FRAMES){
        long long play_local = g_fifo_ts - c->ts.offset;
        long long send_at = play_local - (long long)c->latency_ms * 1000 + (long long)c->delay_ms * 1000;
        if(now + 40000 < send_at) break;                     /* batches: everything due in the next 40 ms goes now */
        fifo_pop(pkt, AP_FRAMES);
        if(now - send_at > 400000){ c->late_drops++; continue; }   /* hopelessly late: skip it */
        if(c->gain < 32768) for(int i = 0; i < AP_FRAMES * 2; i++) pkt[i] = (int16_t)((pkt[i] * c->gain) >> 15);
        ap_send(&c->ap, pkt); c->sent_pkts++;
    }
}
static int run_once(cl_t *c){
    if(ws_connect(&c->ws, c->host, c->port, "/sendspin") < 0) return -1;
    logf_("WebSocket open to %s:%d", c->host, c->port);
    memset(&c->ts, 0, sizeof c->ts); c->hello_ok = c->state_sent = 0; c->next_time = 0;
    send_hello(c);
    int ntime = 0;
    while(!g_stop){
        int r;
        while((r = ws_poll_msg(&c->ws)) == 1){
            if(c->ws.msgop == 1) on_text(c, (const char *)c->ws.msg);
            else on_binary(c, c->ws.msg, c->ws.msgn);
        }
        if(r < 0){ logf_("connection closed"); break; }
        long long now = now_us();
        if(c->hello_ok && now >= c->next_time){
            send_time(c); ntime++;
            c->next_time = now + (ntime < 8 ? 250000 : 2000000);
        }
        pump_audio(c);
        if(c->ap.open) ap_service(&c->ap);
        cmd_poll(c);
        if(c->status_dirty || now - c->status_at > 5000000) status_write(c);
        /* sleep until data arrives or the next batch is due (40 ms) - not a 5 ms spin: this runs on the Disc's battery */
        struct pollfd p[2] = { { c->ws.fd, POLLIN, 0 }, { c->cmdfd, POLLIN, 0 } };
        poll(p, c->cmdfd >= 0 ? 2 : 1, 40);
    }
    stream_stop(c, 1);
    ws_close(&c->ws);
    c->hello_ok = 0; c->streaming = 0; c->title[0] = 0; c->status_dirty = 1;
    return 0;
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
    if(argc < 2){ fprintf(stderr, "usage: %s <server[:port]|auto> [name] [client_id] [delay_ms] [airplay_latency_ms] [airplay_port]\n", argv[0]); return 2; }
    signal(SIGPIPE, SIG_IGN); signal(SIGINT, on_sig); signal(SIGTERM, on_sig); signal(SIGHUP, on_sig);
    static char host[128]; snprintf(host, sizeof host, "%s", argv[1]);
    int autodisc = !strcmp(host, "auto") || !host[0];
    int port = 8927; char *colon = strchr(host, ':'); if(colon){ *colon = 0; port = atoi(colon + 1); }
    static cl_t c; memset(&c, 0, sizeof c);
    c.host = host; c.port = port;
    c.name = argc > 2 && argv[2][0] ? argv[2] : "Snowsky Disc";
    c.id = argc > 3 && argv[3][0] ? argv[3] : "snowsky-disc";
    c.delay_ms = argc > 4 ? atoi(argv[4]) : 0;
    c.latency_ms = argc > 5 ? atoi(argv[5]) : 2000;
    c.ap.port = argc > 6 ? atoi(argv[6]) : 5000;
    c.ap.rtsp = c.ap.aud = c.ap.ctl = c.ap.tim = -1;
    c.volume = 100; c.gain = vol_gain(c.volume, 0); c.ap.vol_db = 0.0;   /* receiver at full scale; MA volume in software */
    g_fifo = malloc(sizeof(int16_t) * 2 * FIFO_FRAMES);
    if(!g_fifo) return 1;
    mkdir("/tmp/ma", 0755);
    unlink("/tmp/ma/cmd"); mkfifo("/tmp/ma/cmd", 0600);
    c.cmdfd = open("/tmp/ma/cmd", O_RDWR | O_NONBLOCK);
    logf_(DISCO_NAME " " DISCO_VERSION " sendspin: server %s, name \"%s\", id %s, delay %d ms, AirPlay port %d, latency %d ms",
          autodisc ? "auto" : host, c.name, c.id, c.delay_ms, c.ap.port, c.latency_ms);
    while(!g_stop){
        if(autodisc){
            snprintf(c.server_lbl, sizeof c.server_lbl, "On your network"); status_write(&c);
            char h[64]; int pt;
            if(mdns_find(h, sizeof h, &pt) == 0){ snprintf(host, sizeof host, "%s", h); c.port = pt; logf_("found Music Assistant at %s:%d", h, pt); }
            else { c.fails++; snprintf(c.msg, sizeof c.msg, "Not found - set the server in Settings"); status_write(&c);
                   { for(int i = 0; i < 50 && !g_stop; i++) usleep(100000); } continue; }
        }
        snprintf(c.server_lbl, sizeof c.server_lbl, "%s", host); status_write(&c);
        if(run_once(&c) < 0){ c.fails++; snprintf(c.msg, sizeof c.msg, "No answer from %s", host); }
        status_write(&c);
        if(g_stop) break;
        logf_("reconnecting in 3 s");
        for(int i = 0; i < 30 && !g_stop; i++) usleep(100000);
    }
    unlink("/tmp/ma/status");
    logf_("bye");
    return 0;
}
#ifdef SS_STANDALONE
int main(int argc, char **argv){ return sendspin_main(argc, argv); }
#endif
