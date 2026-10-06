/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
/* "Add lyrics & artwork": fills in what a track is missing, inside its own tags.
 *   - synced lyrics from lrclib.net (replacing plain, unsynced ones - synced ones are kept);
 *   - 600x600 front cover from Apple's iTunes search, only when the file has no picture yet.
 * FLAC: LYRICS= Vorbis comment + a PICTURE block. MP3: ID3v2 USLT (LRC text) + APIC.
 * Other formats: the lyrics go to a sibling .lrc (the player reads those); no artwork.
 *
 * Safety first - these are the user's music files:
 *   the new file is written to a hidden temp file beside the original, re-read and verified (the audio
 *   part must be byte-for-byte identical, and what was added must read back), fsync'd, and only then
 *   renamed over the original. Anything unusual (ID3v2.2, unsynchronised or extended-header tags, a
 *   damaged FLAC header, too little free space) is skipped, never "fixed". Writes go through the SD
 *   write gate, so nothing is written while the card is handed to a PC.
 * All network + file work runs on one worker thread; the UI polls a status block and shows toasts. */
#include "screens.h"
#include "sdio.h"
#include "theme.h"
#include "musicdb.h"
#include "artcache.h"
#include "ipc.h"
#include "config.h"
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <time.h>
#include <ctype.h>

#ifndef TAGFIX_HOST_TEST
#define TF_TMP "/tmp"
#else
#define TF_TMP "/tmp/tagfix_test"
#endif

/* ================================ small helpers ================================ */
static uint32_t crc_tab[256];
static void crc_init(void){
    for(uint32_t i = 0; i < 256; i++){ uint32_t c = i; for(int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1; crc_tab[i] = c; }
}
static uint32_t crc_upd(uint32_t c, const uint8_t *p, size_t n){ c = ~c; while(n--) c = crc_tab[(c ^ *p++) & 0xFF] ^ (c >> 8); return ~c; }

static int read_file(const char *p, uint8_t **out, size_t *len, size_t maxlen){
    FILE *f = fopen(p, "rb"); if(!f) return -1;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    if(n < 0 || (size_t)n > maxlen){ fclose(f); return -1; }
    uint8_t *b = malloc((size_t)n + 1); if(!b){ fclose(f); return -1; }
    if(fread(b, 1, (size_t)n, f) != (size_t)n){ free(b); fclose(f); return -1; }
    fclose(f); b[n] = 0; *out = b; *len = (size_t)n; return 0;
}
/* copy [off, EOF) of `in` to `out`, returning bytes copied and a CRC of them */
static long copy_tail(FILE *in, long off, FILE *out, uint32_t *crc){
    static uint8_t buf[64 * 1024];
    if(fseek(in, off, SEEK_SET) != 0) return -1;
    long tot = 0; size_t r; *crc = 0;
    while((r = fread(buf, 1, sizeof buf, in)) > 0){
        if(out && fwrite(buf, 1, r, out) != r) return -1;
        *crc = crc_upd(*crc, buf, r); tot += (long)r;
    }
    return ferror(in) ? -1 : tot;
}
static void tmp_name(const char *path, char *out, size_t cap){   /* hidden, same folder, same extension */
    const char *sl = strrchr(path, '/');
    if(sl) snprintf(out, cap, "%.*s/.tagtmp_%s", (int)(sl - path), path, sl + 1);
    else   snprintf(out, cap, ".tagtmp_%s", path);
}
static int enough_space(const char *path, long need){
    char dir[600]; snprintf(dir, sizeof dir, "%s", path);
    char *sl = strrchr(dir, '/'); if(sl) *sl = 0; else snprintf(dir, sizeof dir, ".");
    struct statvfs sv; if(statvfs(dir, &sv) != 0) return 0;
    return (unsigned long long)sv.f_bavail * sv.f_frsize > (unsigned long long)need + 2 * 1024 * 1024;
}
static void be32(uint8_t *p, uint32_t v){ p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }
static void le32(uint8_t *p, uint32_t v){ p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
static uint32_t rbe32(const uint8_t *p){ return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static uint32_t rle32(const uint8_t *p){ return (uint32_t)p[3] << 24 | p[2] << 16 | p[1] << 8 | p[0]; }
static uint32_t synchsafe_r(const uint8_t *p){ return (uint32_t)(p[0] & 0x7F) << 21 | (p[1] & 0x7F) << 14 | (p[2] & 0x7F) << 7 | (p[3] & 0x7F); }
static void synchsafe_w(uint8_t *p, uint32_t v){ p[0] = (v >> 21) & 0x7F; p[1] = (v >> 14) & 0x7F; p[2] = (v >> 7) & 0x7F; p[3] = v & 0x7F; }

/* growable byte buffer */
typedef struct { uint8_t *p; size_t n, cap; int oom; } bb_t;
static void bb_add(bb_t *b, const void *d, size_t n){
    if(b->oom) return;
    if(b->n + n > b->cap){ size_t nc = (b->cap ? b->cap * 2 : 4096); while(nc < b->n + n) nc *= 2;
        uint8_t *q = realloc(b->p, nc); if(!q){ b->oom = 1; return; } b->p = q; b->cap = nc; }
    memcpy(b->p + b->n, d, n); b->n += n;
}

/* ================================ JPEG ================================ */
static int jpeg_dims(const uint8_t *j, size_t n, int *w, int *h){
    if(n < 4 || j[0] != 0xFF || j[1] != 0xD8) return 0;
    size_t i = 2;
    while(i + 9 < n){
        if(j[i] != 0xFF){ i++; continue; }
        uint8_t m = j[i + 1];
        if(m == 0xD8 || m == 0x01 || (m >= 0xD0 && m <= 0xD7)){ i += 2; continue; }
        size_t len = (size_t)j[i + 2] << 8 | j[i + 3];
        if(m >= 0xC0 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC){
            *h = j[i + 5] << 8 | j[i + 6]; *w = j[i + 7] << 8 | j[i + 8]; return 1;
        }
        i += 2 + len;
    }
    return 0;
}

/* ================================ FLAC ================================ */
typedef struct { uint8_t type; uint32_t len; uint8_t *data; } fblk_t;
typedef struct { fblk_t b[64]; int n; long audio_off; uint32_t total; } flac_t;
static void flac_free(flac_t *f){ for(int i = 0; i < f->n; i++) free(f->b[i].data); f->n = 0; }
static int flac_parse(FILE *in, flac_t *f){
    uint8_t h[4]; f->n = 0; f->total = 0;
    if(fread(h, 1, 4, in) != 4 || memcmp(h, "fLaC", 4)) return -1;
    for(;;){
        if(fread(h, 1, 4, in) != 4) return -1;
        int last = h[0] & 0x80; uint8_t type = h[0] & 0x7F; uint32_t len = (uint32_t)h[1] << 16 | h[2] << 8 | h[3];
        if(type == 127 || f->n >= 64 || len > 16u * 1024 * 1024) return -1;
        f->total += len; if(f->total > 24u * 1024 * 1024) return -1;   /* a corrupt header can't exhaust RAM */
        uint8_t *d = malloc(len ? len : 1); if(!d) return -1;
        if(len && fread(d, 1, len, in) != len){ free(d); return -1; }
        f->b[f->n].type = type; f->b[f->n].len = len; f->b[f->n].data = d; f->n++;
        if(last) break;
    }
    if(f->n == 0 || f->b[0].type != 0) return -1;             /* STREAMINFO must come first */
    f->audio_off = ftell(in);
    return 0;
}
static int flac_has_picture(const flac_t *f){ for(int i = 0; i < f->n; i++) if(f->b[i].type == 6) return 1; return 0; }
/* new VORBIS_COMMENT body: all comments kept except (when lyrics given) LYRICS/UNSYNCEDLYRICS, plus LYRICS=lrc */
static int vc_rebuild(const uint8_t *d, uint32_t len, const char *lrc, bb_t *out){
    static const char VENDOR[] = "diskOS";
    uint32_t vlen = 0, cnt = 0; const uint8_t *p = d, *e = d + len;
    if(d && len >= 8){ vlen = rle32(p); if(vlen > len - 8) return -1; }
    uint8_t t[4];
    if(d && len >= 8){ le32(t, vlen); bb_add(out, t, 4); bb_add(out, p + 4, vlen); p += 4 + vlen; cnt = rle32(p); p += 4; }
    else { le32(t, sizeof VENDOR - 1); bb_add(out, t, 4); bb_add(out, VENDOR, sizeof VENDOR - 1); }
    size_t cnt_pos = out->n; uint32_t kept = 0; bb_add(out, "\0\0\0\0", 4);
    for(uint32_t i = 0; i < cnt; i++){
        if(p + 4 > e) return -1;
        uint32_t cl = rle32(p); if(cl > (uint32_t)(e - p - 4)) return -1;
        const char *c = (const char *)p + 4;
        int drop = lrc && ((cl > 7 && !strncasecmp(c, "LYRICS=", 7)) || (cl > 15 && !strncasecmp(c, "UNSYNCEDLYRICS=", 15)));
        if(!drop){ le32(t, cl); bb_add(out, t, 4); bb_add(out, c, cl); kept++; }
        p += 4 + cl;
    }
    if(lrc){ size_t L = strlen(lrc); le32(t, (uint32_t)(7 + L)); bb_add(out, t, 4); bb_add(out, "LYRICS=", 7); bb_add(out, lrc, L); kept++; }
    le32(out->p + cnt_pos, kept);
    return out->oom ? -1 : 0;
}
static int flac_write(const char *path, const char *tmp, const char *lrc, const uint8_t *jpg, size_t jlen, int jw, int jh, uint32_t *crc_out, long *audio_len){
    FILE *in = fopen(path, "rb"); if(!in) return -1;
    flac_t f; if(flac_parse(in, &f) != 0){ flac_free(&f); fclose(in); return -2; }
    bb_t meta = {0};
    int have_vc = 0;
    for(int i = 0; i < f.n; i++) if(f.b[i].type == 4) have_vc = 1;
    /* blocks in order, PADDING dropped, VORBIS_COMMENT rebuilt; a new one after STREAMINFO if missing */
    for(int i = 0; i < f.n; i++){
        fblk_t *b = &f.b[i]; if(b->type == 1) continue;
        uint8_t hdr[4];
        if(b->type == 4 && lrc){
            bb_t vc = {0}; if(vc_rebuild(b->data, b->len, lrc, &vc) != 0){ free(vc.p); goto bad; }
            hdr[0] = 4; hdr[1] = vc.n >> 16; hdr[2] = vc.n >> 8; hdr[3] = vc.n; bb_add(&meta, hdr, 4); bb_add(&meta, vc.p, vc.n); free(vc.p);
        } else { hdr[0] = b->type; hdr[1] = b->len >> 16; hdr[2] = b->len >> 8; hdr[3] = b->len; bb_add(&meta, hdr, 4); bb_add(&meta, b->data, b->len); }
        if(i == 0 && lrc && !have_vc){
            bb_t vc = {0}; if(vc_rebuild(NULL, 0, lrc, &vc) != 0){ free(vc.p); goto bad; }
            hdr[0] = 4; hdr[1] = vc.n >> 16; hdr[2] = vc.n >> 8; hdr[3] = vc.n; bb_add(&meta, hdr, 4); bb_add(&meta, vc.p, vc.n); free(vc.p);
        }
    }
    if(jpg){
        static const char MIME[] = "image/jpeg";
        uint32_t plen = 4 + 4 + (sizeof MIME - 1) + 4 + 4 * 4 + 4 + (uint32_t)jlen;
        uint8_t hdr[4] = { 6, plen >> 16, plen >> 8, plen }, t[4];
        bb_add(&meta, hdr, 4);
        be32(t, 3); bb_add(&meta, t, 4);                                      /* front cover */
        be32(t, sizeof MIME - 1); bb_add(&meta, t, 4); bb_add(&meta, MIME, sizeof MIME - 1);
        be32(t, 0); bb_add(&meta, t, 4);                                      /* no description */
        be32(t, (uint32_t)jw); bb_add(&meta, t, 4); be32(t, (uint32_t)jh); bb_add(&meta, t, 4);
        be32(t, 24); bb_add(&meta, t, 4); be32(t, 0); bb_add(&meta, t, 4);
        be32(t, (uint32_t)jlen); bb_add(&meta, t, 4); bb_add(&meta, jpg, jlen);
    }
    { uint8_t hdr[4] = { 1 | 0x80, 0, 0x10, 0 }; bb_add(&meta, hdr, 4);     /* 4 KB padding, last block */
      static uint8_t z[4096]; bb_add(&meta, z, sizeof z); }
    if(meta.oom) goto bad;
    /* the old "last block" bits are cleared: only our padding carries the flag */
    for(size_t off = 0; off < meta.n; ){ uint32_t l = (uint32_t)meta.p[off + 1] << 16 | meta.p[off + 2] << 8 | meta.p[off + 3];
        if(off + 4 + l < meta.n) meta.p[off] &= 0x7F;
        off += 4 + l; }
    FILE *out = fopen(tmp, "wb"); if(!out) goto bad;
    if(fwrite("fLaC", 1, 4, out) != 4 || fwrite(meta.p, 1, meta.n, out) != meta.n){ fclose(out); unlink(tmp); goto bad; }
    *audio_len = copy_tail(in, f.audio_off, out, crc_out);
    int ok = *audio_len >= 0 && fflush(out) == 0 && fsync(fileno(out)) == 0;
    if(fclose(out) != 0) ok = 0;
    fclose(in); free(meta.p); flac_free(&f);
    if(!ok){ unlink(tmp); return -1; }
    return 0;
bad:
    fclose(in); free(meta.p); flac_free(&f); return -1;
}
static int flac_audio_crc(const char *p, uint32_t *crc, long *len, int *has_pic){
    FILE *in = fopen(p, "rb"); if(!in) return -1;
    flac_t f; int r = flac_parse(in, &f);
    if(r == 0){ *has_pic = flac_has_picture(&f); *len = copy_tail(in, f.audio_off, NULL, crc); if(*len < 0) r = -1; }
    flac_free(&f); fclose(in); return r;
}

/* ================================ MP3 / ID3v2 ================================ */
typedef struct { int ver; long tag_len; uint8_t *frames; size_t flen; } id3_t;   /* tag_len = whole tag incl. header */
/* 1 = tag parsed, 0 = no tag, -2 = a tag we don't rewrite (v2.2, unsynchronisation, extended header, footer) */
static int id3_read(FILE *in, id3_t *t){
    uint8_t h[10]; memset(t, 0, sizeof *t);
    if(fread(h, 1, 10, in) != 10 || memcmp(h, "ID3", 3)) return 0;
    if(h[3] != 3 && h[3] != 4) return -2;
    if(h[5] & 0xF0) return -2;                              /* unsync / ext header / experimental / footer */
    uint32_t sz = synchsafe_r(h + 6);
    t->ver = h[3]; t->tag_len = 10 + (long)sz;
    t->frames = malloc(sz ? sz : 1); if(!t->frames) return -1;
    if(sz && fread(t->frames, 1, sz, in) != sz){ free(t->frames); return -1; }
    t->flen = sz; return 1;
}
/* walk frames; cb(id, data, len, ud) for each */
typedef void (*id3_cb)(const char *id, const uint8_t *d, uint32_t len, const uint8_t *hdr, void *ud);
static int id3_walk(const id3_t *t, id3_cb cb, void *ud){
    size_t i = 0;
    while(i + 10 <= t->flen){
        const uint8_t *f = t->frames + i;
        if(f[0] == 0) break;                                 /* padding */
        for(int k = 0; k < 4; k++) if(!((f[k] >= 'A' && f[k] <= 'Z') || (f[k] >= '0' && f[k] <= '9'))) return -1;
        uint32_t len = t->ver == 4 ? synchsafe_r(f + 4) : rbe32(f + 4);
        if(len > t->flen - i - 10) return -1;
        char id[5] = { f[0], f[1], f[2], f[3], 0 };
        cb(id, f + 10, len, f, ud);
        i += 10 + len;
    }
    return 0;
}
typedef struct { int apic; bb_t *out; int drop_lyrics; } id3_copy_t;
static void id3_scan_cb(const char *id, const uint8_t *d, uint32_t len, const uint8_t *hdr, void *ud){
    (void)d; (void)len; (void)hdr; id3_copy_t *c = ud; if(!strcmp(id, "APIC")) c->apic = 1;
}
static void id3_copy_cb(const char *id, const uint8_t *d, uint32_t len, const uint8_t *hdr, void *ud){
    id3_copy_t *c = ud;
    if(c->drop_lyrics && !strcmp(id, "USLT")) return;        /* replaced by the synced lyrics */
    bb_add(c->out, hdr, 10); bb_add(c->out, d, len);
}
static void id3_frame(bb_t *out, int ver, const char *id, const bb_t *body){
    uint8_t h[10]; memcpy(h, id, 4);
    if(ver == 4) synchsafe_w(h + 4, (uint32_t)body->n); else be32(h + 4, (uint32_t)body->n);
    h[8] = h[9] = 0; bb_add(out, h, 10); bb_add(out, body->p, body->n);
}
/* UTF-8 -> UTF-16LE (with BOM) for ID3v2.3, which predates UTF-8 frames */
static void utf16_add(bb_t *b, const char *s){
    bb_add(b, "\xFF\xFE", 2);
    const unsigned char *p = (const unsigned char *)s;
    while(*p){
        uint32_t cp; int n;
        if(*p < 0x80){ cp = *p; n = 1; }
        else if((*p & 0xE0) == 0xC0){ cp = *p & 0x1F; n = 2; }
        else if((*p & 0xF0) == 0xE0){ cp = *p & 0x0F; n = 3; }
        else if((*p & 0xF8) == 0xF0){ cp = *p & 0x07; n = 4; }
        else { p++; continue; }
        for(int k = 1; k < n; k++){ if((p[k] & 0xC0) != 0x80){ n = k; cp = 0xFFFD; break; } cp = cp << 6 | (p[k] & 0x3F); }
        p += n;
        if(cp >= 0x10000){ cp -= 0x10000; uint16_t hi = 0xD800 | (cp >> 10), lo = 0xDC00 | (cp & 0x3FF);
            uint8_t u[4] = { hi & 0xFF, hi >> 8, lo & 0xFF, lo >> 8 }; bb_add(b, u, 4); }
        else { uint8_t u[2] = { cp & 0xFF, cp >> 8 }; bb_add(b, u, 2); }
    }
}
static int mp3_write(const char *path, const char *tmp, const char *lrc, const uint8_t *jpg, size_t jlen, uint32_t *crc_out, long *audio_len){
    FILE *in = fopen(path, "rb"); if(!in) return -1;
    id3_t t; int r = id3_read(in, &t);
    if(r < 0){ fclose(in); return r == -2 ? -2 : -1; }
    int ver = r == 1 ? t.ver : 3;
    long audio_off = r == 1 ? t.tag_len : 0;
    bb_t fr = {0};
    if(r == 1){ id3_copy_t c = { 0, &fr, lrc != NULL }; if(id3_walk(&t, id3_copy_cb, &c) != 0){ free(t.frames); free(fr.p); fclose(in); return -2; } }
    if(lrc){
        bb_t b = {0};
        if(ver == 4){ bb_add(&b, "\x03" "eng", 4); bb_add(&b, "\0", 1); bb_add(&b, lrc, strlen(lrc)); }           /* UTF-8 */
        else { bb_add(&b, "\x01" "eng", 4); bb_add(&b, "\xFF\xFE\0\0", 4); utf16_add(&b, lrc); }               /* UTF-16 */
        id3_frame(&fr, ver, "USLT", &b); free(b.p);
    }
    if(jpg){
        bb_t b = {0};
        bb_add(&b, "\0" "image/jpeg", 11); bb_add(&b, "\0", 1);     /* latin-1, mime + NUL */
        bb_add(&b, "\x03", 1); bb_add(&b, "\0", 1);                 /* front cover, empty description */
        bb_add(&b, jpg, jlen);
        id3_frame(&fr, ver, "APIC", &b); free(b.p);
    }
    static uint8_t pad[2048]; bb_add(&fr, pad, sizeof pad);
    if(fr.oom){ free(t.frames); free(fr.p); fclose(in); return -1; }
    uint8_t h[10] = { 'I', 'D', '3', (uint8_t)ver, 0, 0 }; synchsafe_w(h + 6, (uint32_t)fr.n);
    FILE *out = fopen(tmp, "wb");
    int ok = out && fwrite(h, 1, 10, out) == 10 && fwrite(fr.p, 1, fr.n, out) == fr.n;
    if(ok){ *audio_len = copy_tail(in, audio_off, out, crc_out); ok = *audio_len >= 0 && fflush(out) == 0 && fsync(fileno(out)) == 0; }
    if(out && fclose(out) != 0) ok = 0;
    fclose(in); free(t.frames); free(fr.p);
    if(!ok){ unlink(tmp); return -1; }
    return 0;
}
static int mp3_audio_crc(const char *p, uint32_t *crc, long *len, int *has_pic){
    FILE *in = fopen(p, "rb"); if(!in) return -1;
    id3_t t; int r = id3_read(in, &t); long off = 0; *has_pic = 0;
    if(r == 1){ id3_copy_t c = { 0, NULL, 0 }; id3_walk(&t, id3_scan_cb, &c); *has_pic = c.apic; off = t.tag_len; free(t.frames); }
    else if(r < 0){ fclose(in); return -1; }
    *len = copy_tail(in, off, NULL, crc); fclose(in);
    return *len < 0 ? -1 : 0;
}

/* ================================ what a file needs ================================ */
static int file_has_picture(const char *p, int fmt);
static int file_has_picture_(const char *p, int fmt){         /* headers only - no audio read */
    FILE *in = fopen(p, "rb"); if(!in) return 1;              /* unreadable: don't try to add */
    int pic = 1;
    if(fmt == 1){ flac_t f; if(flac_parse(in, &f) == 0) pic = flac_has_picture(&f); flac_free(&f); }
    else { id3_t t; int r = id3_read(in, &t); pic = 0;
           if(r == 1){ id3_copy_t c = { 0, NULL, 0 }; id3_walk(&t, id3_scan_cb, &c); pic = c.apic; free(t.frames); }
           else if(r < 0) pic = 1; }
    fclose(in); return pic;
}
enum { FMT_OTHER, FMT_FLAC, FMT_MP3 };
static int file_fmt(const char *p){
    FILE *f = fopen(p, "rb"); if(!f) return -1;
    uint8_t h[4] = {0}; size_t n = fread(h, 1, 4, f); fclose(f);
    if(n == 4 && !memcmp(h, "fLaC", 4)) return FMT_FLAC;
    const char *e = strrchr(p, '.');
    if((n >= 3 && !memcmp(h, "ID3", 3)) || (e && !strcasecmp(e, ".mp3"))) return FMT_MP3;
    return FMT_OTHER;
}
static int file_has_picture(const char *p, int fmt){ return file_has_picture_(p, fmt); }
static int has_synced_lyrics(const char *p){
    lyr_line_t *l = malloc(sizeof(lyr_line_t) * 8); if(!l) return 0;
    int n = lyrics_timed_load(p, l, 8); free(l); return n > 0;
}

/* The core, independent of the network so it can be tested: add `lrc` (NULL = none) and `jpg` (NULL = none)
 * to `path`, verify, and swap in. Returns bit 1 = lyrics added, bit 2 = artwork added, 0 = nothing to do,
 * negative = skipped / failed (the original untouched). */
enum { TF_ERR_IO = -1, TF_ERR_FORMAT = -2, TF_ERR_SPACE = -3, TF_ERR_VERIFY = -4, TF_ERR_BUSY = -5 };
static int tagfix_apply_leased(const char *path, const char *lrc, const uint8_t *jpg, size_t jlen){
    if(!crc_tab[1]) crc_init();
    int fmt = file_fmt(path); if(fmt < 0) return TF_ERR_IO;
    int want_lyr = lrc && lrc[0] && !has_synced_lyrics(path);
    if(fmt == FMT_OTHER){                                    /* lyrics only, as a sibling .lrc */
        if(!want_lyr) return 0;
        char lp[640], tp[660]; snprintf(lp, sizeof lp, "%s", path);
        char *dot = strrchr(lp, '.'), *sl = strrchr(lp, '/');
        if(dot && (!sl || dot > sl)) strcpy(dot, ".lrc"); else strncat(lp, ".lrc", sizeof lp - strlen(lp) - 1);
        tmp_name(lp, tp, sizeof tp);
        if(!sd_write_begin()) return TF_ERR_BUSY;
        FILE *f = fopen(tp, "wb"); int ok = f && fputs(lrc, f) >= 0 && fflush(f) == 0 && fsync(fileno(f)) == 0;
        if(f && fclose(f) != 0) ok = 0;
        if(ok) ok = rename(tp, lp) == 0; else unlink(tp);
        sd_write_end();
        return ok ? 1 : TF_ERR_IO;
    }
    uint32_t crc0; long len0; int pic0;
    if((fmt == FMT_FLAC ? flac_audio_crc(path, &crc0, &len0, &pic0) : mp3_audio_crc(path, &crc0, &len0, &pic0)) != 0) return TF_ERR_FORMAT;
    int want_art = jpg && jlen > 4 && !pic0;
    if(!want_lyr && !want_art) return 0;
    struct stat st; if(stat(path, &st) != 0) return TF_ERR_IO;
    if(!enough_space(path, (long)st.st_size + (long)jlen)) return TF_ERR_SPACE;
    int jw = 600, jh = 600; if(want_art && !jpeg_dims(jpg, jlen, &jw, &jh)) want_art = 0;
    if(!want_lyr && !want_art) return 0;
    char tmp[660]; tmp_name(path, tmp, sizeof tmp);
    if(!sd_write_begin()) return TF_ERR_BUSY;
    uint32_t crcw; long lenw; int r;
    if(fmt == FMT_FLAC) r = flac_write(path, tmp, want_lyr ? lrc : NULL, want_art ? jpg : NULL, jlen, jw, jh, &crcw, &lenw);
    else                r = mp3_write (path, tmp, want_lyr ? lrc : NULL, want_art ? jpg : NULL, jlen, &crcw, &lenw);
    if(r != 0){ sd_write_end(); return r == -2 ? TF_ERR_FORMAT : TF_ERR_IO; }
    /* verify the new file on disk: identical audio, and what we added reads back */
    uint32_t crc1; long len1; int pic1 = 0;
    int v = (fmt == FMT_FLAC ? flac_audio_crc(tmp, &crc1, &len1, &pic1) : mp3_audio_crc(tmp, &crc1, &len1, &pic1)) == 0
            && crc1 == crc0 && len1 == len0 && crcw == crc0 && lenw == len0
            && (!want_art || pic1) && (!want_lyr || has_synced_lyrics(tmp));
    if(!v){ unlink(tmp); sd_write_end(); fprintf(stderr, "tagfix: verify failed, original kept: %s\n", path); return TF_ERR_VERIFY; }
    int ok = rename(tmp, path) == 0;
    if(!ok) unlink(tmp);
    sd_write_end();
    if(!ok) return TF_ERR_IO;
    return (want_lyr ? 1 : 0) | (want_art ? 2 : 0);
}

int tagfix_apply(const char *path, const char *lrc, const uint8_t *jpg, size_t jlen){
    if(!sd_io_begin()) return TF_ERR_BUSY;
    int result=tagfix_apply_leased(path,lrc,jpg,jlen);
    sd_io_end(); return result;
}

/* ================================ network ================================ */
static void url_enc(const char *s, char *o, size_t cap){
    static const char hx[] = "0123456789ABCDEF"; size_t k = 0;
    for(; *s && k + 4 < cap; s++){
        unsigned char c = (unsigned char)*s;
        if((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.') o[k++] = (char)c;
        else { o[k++] = '%'; o[k++] = hx[c >> 4]; o[k++] = hx[c & 15]; }
    }
    o[k] = 0;
}
/* fetch a URL (only %-encoded characters inside, so the quotes can't be broken) into a file */
static int http_get(const char *url, const char *file, size_t maxlen){
    char cmd[2600]; snprintf(cmd, sizeof cmd, "wget -qO '%s' -T 15 '%s' 2>/dev/null", file, url);
    unlink(file);
    if(system(cmd) != 0) return -1;
    struct stat st; if(stat(file, &st) != 0 || st.st_size <= 0 || (size_t)st.st_size > maxlen) return -1;
    return 0;
}
/* the JSON string value after "key": at or after `from`; decoded to UTF-8 into out. NULL when absent/null */
const char *tf_json_str(const char *from, const char *key, char *out, size_t cap){
    char pat[64]; snprintf(pat, sizeof pat, "\"%s\":", key);
    const char *p = strstr(from, pat); if(!p) return NULL;
    p += strlen(pat); while(*p == ' ') p++;
    if(*p != '"') return NULL;                               /* null / number: nothing */
    p++; size_t k = 0;
    while(*p && *p != '"' && k + 5 < cap){
        if(*p != '\\'){ out[k++] = *p++; continue; }
        p++;
        if(!*p) break;                                        /* truncated after a backslash */
        switch(*p){
            case 'n': out[k++] = '\n'; p++; break; case 'r': p++; break; case 't': out[k++] = '\t'; p++; break;
            case 'b': case 'f': p++; break;
            case 'u': { unsigned cp = 0; int i = 1;
                for(; i <= 4 && isxdigit((unsigned char)p[i]); i++){ char c = p[i]; cp = cp * 16 + (unsigned)(c <= '9' ? c - '0' : (c | 32) - 'a' + 10); }
                if(i != 5){ p += i; continue; }                      /* malformed or truncated escape: skip it */
                p += 5;
                if(cp >= 0xD800 && cp <= 0xDBFF && p[0] == '\\' && p[1] == 'u'){ unsigned lo = 0; int j = 2;
                    for(; j <= 5 && isxdigit((unsigned char)p[j]); j++){ char c = p[j]; lo = lo * 16 + (unsigned)(c <= '9' ? c - '0' : (c | 32) - 'a' + 10); }
                    if(j == 6 && lo >= 0xDC00 && lo <= 0xDFFF){ cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00); p += 6; }
                    else cp = 0xFFFD; }
                else if(cp >= 0xD800 && cp <= 0xDFFF) cp = 0xFFFD;   /* a lone surrogate */
                if(cp < 0x80) out[k++] = (char)cp;
                else if(cp < 0x800){ out[k++] = (char)(0xC0 | cp >> 6); out[k++] = (char)(0x80 | (cp & 0x3F)); }
                else if(cp < 0x10000){ out[k++] = (char)(0xE0 | cp >> 12); out[k++] = (char)(0x80 | ((cp >> 6) & 0x3F)); out[k++] = (char)(0x80 | (cp & 0x3F)); }
                else { out[k++] = (char)(0xF0 | cp >> 18); out[k++] = (char)(0x80 | ((cp >> 12) & 0x3F)); out[k++] = (char)(0x80 | ((cp >> 6) & 0x3F)); out[k++] = (char)(0x80 | (cp & 0x3F)); }
            } break;
            default: out[k++] = *p++; break;                  /* \" \\ \/ */
        }
    }
    out[k] = 0;
    return k ? p : NULL;
}
/* synced lyrics: exact match (title, artist, album, duration) first, then a search */
static char *fetch_lyrics(const char *title, const char *artist, const char *album, long dur_ms){
    char t[600], a[600], al[600], url[2200], *res = NULL;
    url_enc(title, t, sizeof t); url_enc(artist, a, sizeof a); url_enc(album, al, sizeof al);
    uint8_t *j; size_t jl; char *out = malloc(64 * 1024); if(!out) return NULL;
    snprintf(url, sizeof url, "https://lrclib.net/api/get?track_name=%s&artist_name=%s&album_name=%s&duration=%ld", t, a, al, dur_ms / 1000);
    if(http_get(url, TF_TMP "/tagfix.json", 512 * 1024) == 0 && read_file(TF_TMP "/tagfix.json", &j, &jl, 512 * 1024) == 0){
        if(tf_json_str((char *)j, "syncedLyrics", out, 64 * 1024)) res = out;
        free(j);
    }
    if(!res){
        snprintf(url, sizeof url, "https://lrclib.net/api/search?track_name=%s&artist_name=%s", t, a);
        if(http_get(url, TF_TMP "/tagfix.json", 1024 * 1024) == 0 && read_file(TF_TMP "/tagfix.json", &j, &jl, 1024 * 1024) == 0){
            const char *p = (char *)j;
            while(p && (p = strstr(p, "\"syncedLyrics\":"))){        /* the first result that has synced lyrics */
                if(tf_json_str(p, "syncedLyrics", out, 64 * 1024)){ res = out; break; }
                p += 15;
            }
            free(j);
        }
    }
    if(!res) free(out);
    return res;
}
/* front cover, 600x600, from the iTunes search (album match, else the song) */
static uint8_t *fetch_art(const char *title, const char *artist, const char *album, size_t *len){
    char term[1300], q[1300], url[2200], art[1024]; uint8_t *j; size_t jl; int found = 0;
    for(int pass = 0; pass < 2 && !found; pass++){
        if(pass == 0 && !album[0]) continue;
        snprintf(term, sizeof term, "%s %s", artist, pass == 0 ? album : title);
        url_enc(term, q, sizeof q);
        snprintf(url, sizeof url, "https://itunes.apple.com/search?term=%s&entity=%s&limit=3", q, pass == 0 ? "album" : "song");
        if(http_get(url, TF_TMP "/tagfix.json", 512 * 1024) == 0 && read_file(TF_TMP "/tagfix.json", &j, &jl, 512 * 1024) == 0){
            if(tf_json_str((char *)j, "artworkUrl100", art, sizeof art)) found = 1;
            free(j);
        }
    }
    if(!found) return NULL;
    char *s = strstr(art, "100x100bb");
    if(s){ char big[1100]; snprintf(big, sizeof big, "%.*s600x600bb%.900s", (int)(s - art), art, s + 9);
           if(strlen(big) < sizeof art) memcpy(art, big, strlen(big) + 1); }
    for(char *c = art; *c; c++) if(*c == '\'' || *c == ' ') return NULL;   /* never let a URL break the shell quotes */
    uint8_t *img; size_t il;
    if(http_get(art, TF_TMP "/tagfix_art.jpg", 3 * 1024 * 1024) != 0 || read_file(TF_TMP "/tagfix_art.jpg", &img, &il, 3 * 1024 * 1024) != 0) return NULL;
    int w, h; if(!jpeg_dims(img, il, &w, &h)){ free(img); return NULL; }
    *len = il; return img;
}

/* ================================ the worker ================================ */
typedef struct { char path[600], title[256], artist[256], album[256]; long dur_ms; } tf_item_t;
static struct {
    pthread_mutex_t mu;
    int busy, done, total, lyr, art, complete, notfound, failed, finished, album, autom;
    tf_item_t *items; int n;
} S = { .mu = PTHREAD_MUTEX_INITIALIZER };
static lv_timer_t *g_poll;
/* ---- Auto-tag bookkeeping: files where nothing was found, retried at most once a day ---- */
#ifndef TAGFIX_HOST_TEST
#define AT_LOG "/usr/data/autotag.log"
#else
#define AT_LOG TF_TMP "/autotag.log"
#endif
static int at_recently_tried(const char *path){
    FILE *f = fopen(AT_LOG, "r"); if(!f) return 0;
    char line[700]; long now = (long)time(NULL); int hit = 0;
    while(fgets(line, sizeof line, f)){
        char *tab = strchr(line, '\t'); if(!tab) continue;
        line[strcspn(line, "\n")] = 0;
        if(!strcmp(tab + 1, path) && now - atol(line) < 86400) hit = 1;
    }
    fclose(f); return hit;
}
static void at_log_attempt(const char *path){
    struct stat st;
    if(stat(AT_LOG, &st) == 0 && st.st_size > 64 * 1024){    /* keep it small: the newest ~200 entries */
        uint8_t *b; size_t n;
        if(read_file(AT_LOG, &b, &n, 256 * 1024) == 0){
            size_t keep = n / 3, i = n - keep; while(i < n && b[i - 1] != '\n') i++;
            FILE *w = fopen(AT_LOG ".new", "w"); if(w){ fwrite(b + i, 1, n - i, w); fclose(w); rename(AT_LOG ".new", AT_LOG); }
            free(b);
        }
    }
    FILE *f = fopen(AT_LOG, "a"); if(!f) return;
    fprintf(f, "%ld\t%s\n", (long)time(NULL), path); fclose(f);
}
static int net_online(void){                                 /* a default route over Wi-Fi (cheap /proc read) */
    FILE *f = fopen("/proc/net/route", "r"); if(!f) return 0;
    char ifc[32], dst[16]; char line[256]; int ok = 0;
    if(!fgets(line, sizeof line, f)){ fclose(f); return 0; }
    while(fgets(line, sizeof line, f)) if(sscanf(line, "%31s %15s", ifc, dst) == 2 && !strcmp(dst, "00000000") && !strncmp(ifc, "wlan", 4)) ok = 1;
    fclose(f); return ok;
}
static int g_last_done = -1;

static void *worker(void *arg){
    (void)arg;
    uint8_t *art = NULL; size_t alen = 0; int art_tried = 0;
    for(int i = 0; i < S.n; i++){
        tf_item_t *it = &S.items[i];
        if(!sd_io_begin()){
            pthread_mutex_lock(&S.mu); S.failed += S.n-i; S.done=S.n; pthread_mutex_unlock(&S.mu);
            break;
        }
        int fmt = file_fmt(it->path), need_lyr = !has_synced_lyrics(it->path), need_art = 0;
        if(fmt == FMT_FLAC || fmt == FMT_MP3) need_art = !file_has_picture(it->path, fmt);
        sd_io_end();
        char *lrc = need_lyr ? fetch_lyrics(it->title, it->artist, it->album, it->dur_ms) : NULL;
        if(need_art && !art_tried){ art = fetch_art(it->title, it->artist, it->album, &alen); art_tried = 1; }   /* once (per album) */
        int r = (lrc || (need_art && art)) ? tagfix_apply(it->path, lrc, need_art ? art : NULL, alen) : 0;
        pthread_mutex_lock(&S.mu);
        if(r > 0){ if(r & 1) S.lyr++; if(r & 2) S.art++; }
        else if(r == 0){ if(!need_lyr && !need_art) S.complete++; else S.notfound++; }
        else S.failed++;
        S.done = i + 1;
        pthread_mutex_unlock(&S.mu);
        free(lrc);
        if(!S.album){ free(art); art = NULL; art_tried = 0; }
    }
    free(art);
    pthread_mutex_lock(&S.mu); S.finished = 1; pthread_mutex_unlock(&S.mu);
    return NULL;
}
static void poll_cb(lv_timer_t *t){
    (void)t;
    pthread_mutex_lock(&S.mu);
    int done = S.done, total = S.total, fin = S.finished, lyr = S.lyr, art = S.art, comp = S.complete, nf = S.notfound, fail = S.failed, album = S.album;
    pthread_mutex_unlock(&S.mu);
    if(!fin){
        if(album && done != g_last_done){ char b[64]; snprintf(b, sizeof b, "Tagging %d / %d...", done + 1 > total ? total : done + 1, total); ui_toast(b); g_last_done = done; }
        return;
    }
    char b[160];
    pthread_mutex_lock(&S.mu); int autom = S.autom; pthread_mutex_unlock(&S.mu);
    if(autom){                                               /* Auto-tag: quiet unless something was added */
        if(lyr || art){ snprintf(b, sizeof b, "Auto-tag: added %s%s%s", lyr ? "lyrics" : "", lyr && art ? " & " : "", art ? "artwork" : ""); ui_toast(b); ui_np_tags_changed(); }
        else if(nf || fail) at_log_attempt(S.items[0].path);  /* try again on a later play, at most daily */
        lv_timer_delete(g_poll); g_poll = NULL;
        free(S.items); S.items = NULL;
        pthread_mutex_lock(&S.mu); S.busy = 0; pthread_mutex_unlock(&S.mu);
        return;
    }
    if(!album){
        if(lyr || art) snprintf(b, sizeof b, "Added %s%s%s", lyr ? "synced lyrics" : "", lyr && art ? " and " : "", art ? "artwork" : "");
        else if(comp) snprintf(b, sizeof b, "Already has synced lyrics and artwork");
        else if(fail) snprintf(b, sizeof b, "Couldn't update this file (left unchanged)");
        else snprintf(b, sizeof b, "Nothing found online for this track");
    } else snprintf(b, sizeof b, "Album done: lyrics %d, artwork %d, complete %d, not found %d%s", lyr, art, comp, nf, fail ? ", some skipped" : "");
    ui_toast(b);
    if(lyr || art) ui_np_tags_changed();                     /* let the lyrics / immersive views reload */
    lv_timer_delete(g_poll); g_poll = NULL;
    free(S.items); S.items = NULL;
    pthread_mutex_lock(&S.mu); S.busy = 0; pthread_mutex_unlock(&S.mu);
}
static int start_ex(tf_item_t *items, int n, int album, int autom);
static int start(tf_item_t *items, int n, int album){ return start_ex(items, n, album, 0); }
static int start_ex(tf_item_t *items, int n, int album, int autom){
    if(!sd_io_allowed()){ free(items); if(!autom) ui_toast("SD unavailable - tagging is paused"); return -1; }
    pthread_mutex_lock(&S.mu);
    if(S.busy){ pthread_mutex_unlock(&S.mu); free(items); if(!autom) ui_toast("Already tagging - one moment"); return -1; }
    S.busy = 1; S.done = S.lyr = S.art = S.complete = S.notfound = S.failed = S.finished = 0;
    S.total = n; S.n = n; S.items = items; S.album = album; S.autom = autom;
    pthread_mutex_unlock(&S.mu);
    g_last_done = -1;
    pthread_t th; pthread_attr_t at; pthread_attr_init(&at); pthread_attr_setstacksize(&at, 512 * 1024);
    if(pthread_create(&th, &at, worker, NULL) != 0){ pthread_attr_destroy(&at); free(items); S.items = NULL; S.busy = 0; ui_toast("Couldn't start tagging"); return -1; }
    pthread_detach(th); pthread_attr_destroy(&at);
    g_poll = lv_timer_create(poll_cb, 400, NULL);
    if(!autom) ui_toast(album ? "Tagging album..." : "Looking up lyrics & artwork...");
    return 0;
}
/* Now Playing menu: the current track */
void tagfix_current_track(void){
    track_state_t st; ipc_get_state(&st);
    if(!st.have_track || !st.path[0]){ ui_toast("Nothing playing"); return; }
    tf_item_t *it = calloc(1, sizeof *it); if(!it) return;
    snprintf(it->path, sizeof it->path, "%s", st.path); snprintf(it->title, sizeof it->title, "%s", st.title);
    snprintf(it->artist, sizeof it->artist, "%s", st.artist); snprintf(it->album, sizeof it->album, "%s", st.album);
    it->dur_ms = st.duration_ms;
    start(it, 1, 0);
}
/* Now Playing menu: every track of the current track's album (paths gathered here, on the UI thread) */
void tagfix_current_album(void){
    track_state_t st; ipc_get_state(&st);
    if(!st.have_track || !st.album[0]){ ui_toast("This track has no album tag"); return; }
    static const mdb_song_t *songs[400];
    int n = mdb_album_songs(st.album, songs, 400);
    if(n <= 0){ ui_toast("Album not found in the library"); return; }
    tf_item_t *items = calloc((size_t)n, sizeof *items); if(!items) return;
    int k = 0;
    for(int i = 0; i < n; i++){
        if(mdb_song_path(songs[i]->id, items[k].path, sizeof items[k].path) != 1) continue;
        snprintf(items[k].title, sizeof items[k].title, "%s", songs[i]->title);
        snprintf(items[k].artist, sizeof items[k].artist, "%s", songs[i]->artist);
        snprintf(items[k].album, sizeof items[k].album, "%s", songs[i]->album);
        items[k].dur_ms = songs[i]->dur_ms; k++;
    }
    if(!k){ free(items); ui_toast("Album not found in the library"); return; }
    start(items, k, 1);
}

/* ================================ Auto-tag ================================ */
/* Called every main-loop pass with the normalised play state. Cheap early-outs; once a track has
 * played 10 s it's considered once: on Wi-Fi, not recently tried, then the tagger checks what it
 * lacks (headers only) and looks up only that. */
static char g_at_last[600];
void tagfix_auto_tick(const track_state_t *st, int playing){
    if(!sd_io_allowed() || !cfg_get_int("autotag", 0) || !playing || !st || !st->have_track || !st->path[0]) return;
    if(st->position_ms < 10000 || !strcmp(st->path, g_at_last)) return;
    snprintf(g_at_last, sizeof g_at_last, "%s", st->path);  /* once per play of this track */
    pthread_mutex_lock(&S.mu); int busy = S.busy; pthread_mutex_unlock(&S.mu);
    if(busy || !net_online() || at_recently_tried(st->path)) return;
    if(!sd_io_begin()) return;
    int complete=0;
    if(has_synced_lyrics(st->path)){                         /* the common case: quick check before a thread */
        int fmt = file_fmt(st->path);
        complete=(fmt != FMT_FLAC && fmt != FMT_MP3) || file_has_picture(st->path,fmt);
    }
    sd_io_end();
    if(complete) return;
    tf_item_t *it = calloc(1, sizeof *it); if(!it) return;
    snprintf(it->path, sizeof it->path, "%s", st->path); snprintf(it->title, sizeof it->title, "%s", st->title);
    snprintf(it->artist, sizeof it->artist, "%s", st->artist); snprintf(it->album, sizeof it->album, "%s", st->album);
    it->dur_ms = st->duration_ms;
    start_ex(it, 1, 0, 1);
}

/* Library / folder long-press: one song */
void tagfix_song(const char *path, const char *title, const char *artist, const char *album, long dur_ms){
    tf_item_t *it = calloc(1, sizeof *it); if(!it) return;
    snprintf(it->path, sizeof it->path, "%s", path); snprintf(it->title, sizeof it->title, "%s", title ? title : "");
    snprintf(it->artist, sizeof it->artist, "%s", artist ? artist : ""); snprintf(it->album, sizeof it->album, "%s", album ? album : "");
    it->dur_ms = dur_ms;
    start(it, 1, 0);
}
/* folder long-press: every song under it (one artwork lookup per album run) */
typedef struct { tf_item_t *items; int n, cap; } tf_col_t;
static void col_cb(void *ud, const char *path, const char *title, const char *artist, const char *album, long dur){
    tf_col_t *c = ud; if(c->n >= c->cap) return;
    tf_item_t *it = &c->items[c->n++];
    snprintf(it->path, sizeof it->path, "%s", path); snprintf(it->title, sizeof it->title, "%s", title);
    snprintf(it->artist, sizeof it->artist, "%s", artist); snprintf(it->album, sizeof it->album, "%s", album); it->dur_ms = dur;
}
void tagfix_folder(const char *dir){
    tf_col_t c = { calloc(400, sizeof(tf_item_t)), 0, 400 };
    if(!c.items) return;
    mdb_folder_rows(dir, col_cb, &c);
    if(!c.n){ free(c.items); ui_toast("No songs in this folder"); return; }
    start(c.items, c.n, 1);
}
