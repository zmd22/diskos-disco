/* Loopback AirPlay 1 sender: hands PCM to the Disc's own AirPlay receiver (and through it to mq_player).
 * Encrypted (AES key wrapped with the AirPort Express RSA key), uncompressed ALAC, 44.1 kHz / 16 bit / stereo.
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <poll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "ss_crypto.h"

#define AP_FRAMES 352
#define AP_RATE   44100

static const char AP_APPLE_MOD[] = "59dE8qLieItsH1WgjrcFRKj6eUWqi+bGLOX1HL3U3GhC/j0Qg90u3sG/1CUtwC5vOYvfDmFI6oSFXi5ELabWJmT2dKHzBJKa3k9ok+8t9ucRqMd6DZHJ2YCCLlDRKSKv6kDqnw4UwPdpOMXziC/AMj3Z/lUVX1G7WSHCAWKf1zNS1eLvqr+boEjXuBOitnZ/bDzPHrTOZz0Dew0uowxf/+sG+NCK3eQJVxqcaJ/vEHKIVd2M+5qL71yJQ+87X6oV3eaYvt3zWZYD6z5vYTcrtij2VZ9Zmni/UAaHqn9JdsBWLUEpVviYnhimNVvYFZeCXg/IdTQ+x4IRdiXNv5hEew==";

typedef struct {
    int port;                    /* receiver RTSP port (127.0.0.1) */
    int rtsp, aud, ctl, tim;     /* sockets */
    int cseq, open;
    char session[128], uri[96];
    struct sockaddr_in dst, cdst;
    uint16_t seq; uint32_t rtpt; int first, npkt;
    aes_t aes; uint8_t iv[16];
    double vol_db;
} airplay_t;

static void ap_rnd(uint8_t *b, int n){ FILE *f = fopen("/dev/urandom", "rb"); if(!f || fread(b, 1, n, f) != (size_t)n){ for(int i = 0; i < n; i++) b[i] = rand(); } if(f) fclose(f); }
static uint64_t ap_ntp(void){
    struct timespec t; clock_gettime(CLOCK_REALTIME, &t);
    return ((uint64_t)(t.tv_sec + 0x83AA7E80u) << 32) | (uint64_t)(((uint64_t)t.tv_nsec << 32) / 1000000000u);
}
static void ap_put32(uint8_t *b, uint32_t v){ b[0] = v >> 24; b[1] = v >> 16; b[2] = v >> 8; b[3] = v; }
static void ap_put64(uint8_t *b, uint64_t v){ ap_put32(b, v >> 32); ap_put32(b + 4, (uint32_t)v); }

static int ap_tcp(int port){
    int fd = socket(AF_INET, SOCK_STREAM, 0); if(fd < 0) return -1;
    struct sockaddr_in a = {0}; a.sin_family = AF_INET; a.sin_port = htons(port); a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    struct timeval tv = { 2, 0 }; setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv); setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    if(connect(fd, (void *)&a, sizeof a) < 0){ close(fd); return -1; }
    return fd;
}
static int ap_udp(int *port){
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in a = {0}; a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_ANY);
    bind(fd, (void *)&a, sizeof a);
    socklen_t l = sizeof a; getsockname(fd, (void *)&a, &l); if(port) *port = ntohs(a.sin_port);
    fcntl(fd, F_SETFL, O_NONBLOCK);
    return fd;
}
static int ap_read_reply(int fd, char *buf, int n){
    int got = 0;
    while(got < n - 1){
        int r = recv(fd, buf + got, n - 1 - got, 0); if(r <= 0) break;
        got += r; buf[got] = 0;
        char *e = strstr(buf, "\r\n\r\n");
        if(e){ char *cl = strcasestr(buf, "Content-Length:"); int bl = cl ? atoi(cl + 15) : 0;
               if(got >= (e - buf) + 4 + bl) break; }
    }
    buf[got > 0 ? got : 0] = 0;
    return got;
}
static int ap_rtsp(airplay_t *a, const char *method, const char *extra, const char *ctype, const char *body, char *rep, int rn){
    char req[4096];
    int bl = body ? (int)strlen(body) : 0;
    int n = snprintf(req, sizeof req, "%s %s RTSP/1.0\r\nCSeq: %d\r\nUser-Agent: iTunes/7.6.2 (Windows; N;)\r\n"
                     "Client-Instance: 56B29BB6CB904862\r\nDACP-ID: 56B29BB6CB904862\r\nActive-Remote: 1986535575\r\n",
                     method, !strcmp(method, "OPTIONS") ? "*" : a->uri, ++a->cseq);
    if(a->session[0]) n += snprintf(req + n, sizeof req - n, "Session: %s\r\n", a->session);
    if(extra) n += snprintf(req + n, sizeof req - n, "%s", extra);
    if(bl) n += snprintf(req + n, sizeof req - n, "Content-Type: %s\r\nContent-Length: %d\r\n", ctype, bl);
    n += snprintf(req + n, sizeof req - n, "\r\n%s", bl ? body : "");
    if(send(a->rtsp, req, n, MSG_NOSIGNAL) != n) return -1;
    int r = ap_read_reply(a->rtsp, rep, rn);
    int code = r > 12 ? atoi(rep + 9) : -1;
    if(code != 200){ char first[160] = ""; sscanf(rep, "%159[^\r\n]", first); fprintf(stderr, "airplay: %s -> %s\n", method, r > 0 ? first : "(no reply)"); }
    return code;
}
static int ap_hdr_int(const char *s, const char *key){ const char *p = strstr(s, key); return p ? atoi(p + strlen(key)) : 0; }

static void ap_close(airplay_t *a){
    char rep[1024];
    if(a->open && a->rtsp >= 0) ap_rtsp(a, "TEARDOWN", NULL, NULL, NULL, rep, sizeof rep);
    if(a->rtsp >= 0) close(a->rtsp);
    if(a->aud >= 0) close(a->aud);
    if(a->ctl >= 0) close(a->ctl);
    if(a->tim >= 0) close(a->tim);
    a->rtsp = a->aud = a->ctl = a->tim = -1; a->open = 0; a->session[0] = 0;
}
static int ap_set_volume(airplay_t *a, double db){
    a->vol_db = db;
    if(!a->open) return 0;
    char rep[1024], body[64]; snprintf(body, sizeof body, "volume: %.6f\r\n", db);
    return ap_rtsp(a, "SET_PARAMETER", NULL, "text/parameters", body, rep, sizeof rep) == 200 ? 0 : -1;
}
static int ap_open(airplay_t *a){
    char rep[4096], buf[2048];
    a->rtsp = a->aud = a->ctl = a->tim = -1; a->cseq = 0; a->session[0] = 0; a->open = 0;
    a->rtsp = ap_tcp(a->port);
    if(a->rtsp < 0){ fprintf(stderr, "airplay: no receiver on port %d (is the Disc in AirPlay mode?)\n", a->port); return -1; }
    struct sockaddr_in me; socklen_t ml = sizeof me; getsockname(a->rtsp, (void *)&me, &ml);
    char ip[32]; inet_ntop(AF_INET, &me.sin_addr, ip, sizeof ip);
    uint32_t sid; ap_rnd((uint8_t *)&sid, 4);
    snprintf(a->uri, sizeof a->uri, "rtsp://%s/%u", ip, sid);
    if(ap_rtsp(a, "OPTIONS", NULL, NULL, NULL, rep, sizeof rep) != 200){ ap_close(a); return -1; }
    int bl = snprintf(buf, sizeof buf, "v=0\r\no=iTunes %u 0 IN IP4 %s\r\ns=iTunes\r\nc=IN IP4 127.0.0.1\r\nt=0 0\r\n"
             "m=audio 0 RTP/AVP 96\r\na=rtpmap:96 AppleLossless\r\na=fmtp:96 %d 0 16 40 10 14 2 255 0 0 %d\r\n", sid, ip, AP_FRAMES, AP_RATE);
    uint8_t key[16], seed[20], mod[260], wrapped[256]; char k64[400], iv64[40];
    ap_rnd(key, 16); ap_rnd(a->iv, 16); ap_rnd(seed, 20);
    b64_dec(AP_APPLE_MOD, mod);
    rsa_oaep(wrapped, key, 16, mod, seed); aes_init(&a->aes, key);
    b64_enc(wrapped, 256, k64); b64_enc(a->iv, 16, iv64);
    snprintf(buf + bl, sizeof buf - bl, "a=rsaaeskey:%s\r\na=aesiv:%s\r\n", k64, iv64);
    if(ap_rtsp(a, "ANNOUNCE", NULL, "application/sdp", buf, rep, sizeof rep) != 200){ ap_close(a); return -1; }
    int cport, tport; a->ctl = ap_udp(&cport); a->tim = ap_udp(&tport); a->aud = ap_udp(NULL);
    snprintf(buf, sizeof buf, "Transport: RTP/AVP/UDP;unicast;interleaved=0-1;mode=record;control_port=%d;timing_port=%d\r\n", cport, tport);
    if(ap_rtsp(a, "SETUP", buf, NULL, NULL, rep, sizeof rep) != 200){ ap_close(a); return -1; }
    char *s = strcasestr(rep, "Session:"); if(s){ s += 8; while(*s == ' ') s++; sscanf(s, "%127[^;\r\n]", a->session); }
    int sport = ap_hdr_int(rep, "server_port="), rc = ap_hdr_int(rep, "control_port=");
    memset(&a->dst, 0, sizeof a->dst); a->dst.sin_family = AF_INET; a->dst.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a->cdst = a->dst; a->dst.sin_port = htons(sport); a->cdst.sin_port = htons(rc ? rc : sport);
    ap_rnd((uint8_t *)&a->seq, 2); ap_rnd((uint8_t *)&a->rtpt, 4);
    snprintf(buf, sizeof buf, "Range: npt=0-\r\nRTP-Info: seq=%u;rtptime=%u\r\n", a->seq, a->rtpt);
    if(ap_rtsp(a, "RECORD", buf, NULL, NULL, rep, sizeof rep) != 200){ ap_close(a); return -1; }
    a->open = 1; a->first = 1; a->npkt = 0;
    ap_set_volume(a, a->vol_db);
    return 0;
}
/* drop what the receiver buffered (seek / skip) */
static void ap_flush(airplay_t *a){
    if(!a->open) return;
    char rep[1024], buf[128];
    snprintf(buf, sizeof buf, "RTP-Info: seq=%u;rtptime=%u\r\n", a->seq, a->rtpt);
    ap_rtsp(a, "FLUSH", buf, NULL, NULL, rep, sizeof rep);
    a->first = 1;
}

typedef struct { uint8_t *p; int bit; } ap_bw_t;
static void ap_bw(ap_bw_t *w, unsigned v, int nbits){
    for(int i = nbits - 1; i >= 0; i--){ if(v >> i & 1) w->p[w->bit >> 3] |= 0x80 >> (w->bit & 7); w->bit++; }
}
/* answer the receiver's timing requests (newer receivers ask; the Disc's doesn't) */
static void ap_service(airplay_t *a){
    if(a->tim < 0) return;
    uint8_t q[64]; struct sockaddr_in from; socklen_t fl = sizeof from;
    while(recvfrom(a->tim, q, sizeof q, 0, (void *)&from, &fl) >= 32){
        if((q[1] & 0x7f) != 0x52){ fl = sizeof from; continue; }
        uint8_t r[32] = { 0x80, 0xd3, 0x00, 0x07 };
        memcpy(r + 8, q + 24, 8); ap_put64(r + 16, ap_ntp()); ap_put64(r + 24, ap_ntp());
        sendto(a->tim, r, 32, 0, (void *)&from, fl); fl = sizeof from;
    }
}
/* one packet: AP_FRAMES stereo frames of native-endian int16 */
static int ap_send(airplay_t *a, const int16_t *pcm){
    if(!a->open) return -1;
    ap_service(a);
    if(a->npkt % 125 == 0){
        uint8_t sp[20] = { a->first ? 0x90 : 0x80, 0xd4, 0x00, 0x07 };
        ap_put32(sp + 4, a->rtpt - 11025); ap_put64(sp + 8, ap_ntp()); ap_put32(sp + 16, a->rtpt);
        sendto(a->ctl, sp, 20, 0, (void *)&a->cdst, sizeof a->cdst);
    }
    uint8_t pkt[12 + 4 + AP_FRAMES * 4 + 8];
    memset(pkt, 0, sizeof pkt);
    /* RTP v2, payload type 96 (marker bit on the first packet) */
    pkt[0] = 128; pkt[1] = a->first ? 224 : 96; pkt[2] = a->seq >> 8; pkt[3] = a->seq; ap_put32(pkt + 4, a->rtpt); ap_put32(pkt + 8, 0x5d15c0);
    /* uncompressed ALAC frame: a 23-bit header (stereo, escape flag), then the samples MSB-first, then the 3-bit end tag.
     * 23 bits = 2 bytes + 7 bits, so every 16-bit sample lands 7 bits into a byte: packed a byte at a time. */
    uint8_t *o = pkt + 12;
    o[0] = 32; o[1] = 0;                                    /* 001 0000 0000 0000 0... */
    uint32_t acc = 1; int nb = 7;                           /* ...0 00 1 : the last 7 header bits (value 1) */
    int k = 2;
    for(int i = 0; i < AP_FRAMES * 2; i++){
        acc = acc << 16 | (uint16_t)pcm[i]; nb += 16;
        while(nb >= 8){ nb -= 8; o[k++] = (uint8_t)(acc >> nb); }
    }
    acc = acc << 3 | 7; nb += 3;                            /* end tag */
    while(nb >= 8){ nb -= 8; o[k++] = (uint8_t)(acc >> nb); }
    if(nb) o[k++] = (uint8_t)(acc << (8 - nb));
    int n = k;
    aes_cbc_packet(&a->aes, a->iv, pkt + 12, n);
    sendto(a->aud, pkt, 12 + n, 0, (void *)&a->dst, sizeof a->dst);
    a->seq++; a->rtpt += AP_FRAMES; a->first = 0; a->npkt++;
    return 0;
}
