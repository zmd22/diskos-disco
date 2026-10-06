/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "sdio.h"
#include "art.h"
int artdec_inproc(const char *mode, const char *input, const char *out);   /* artdec_inproc.c */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <pthread.h>
#include <time.h>
#include <stdint.h>
#include <fcntl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <dirent.h>
#include <sys/syscall.h>

/* int64 so tv_sec*1000 can't overflow a 32-bit long after ~24.9 days of monotonic uptime (which would
 * corrupt every deadline comparison below and kill fresh decodes on sight). */
static int64_t art_now_ms(void){ struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (int64_t)t.tv_sec*1000LL + t.tv_nsec/1000000LL; }

/* Artwork is made by diskos-artdec, a small helper shipped in the image: it extracts the embedded cover (or reads
 * a sibling cover file), decodes it and writes the 148px cover, 42px Home thumb and 360px blurred backdrop as 24-bit
 * BMPs. It replaced the stock `ffmpeg` command, which stock V2.57 ships without any image decoders.
 * Fork: the decoder is linked in (artdec_inproc.c) and runs in the calling worker thread instead of as a separate
 * process - forking the large UI process for every cover stopped working after a few quick skips on the device.
 * Its own size/pixel/memory limits still bound every decode; a skip or SD revocation is honoured before a decode
 * starts and before its result is published. See tools/diskos_artdec.c for its limits and exit statuses. */
#ifndef ARTDEC_PATH
#define ARTDEC_PATH "/opt/diskos/bin/diskos-artdec"
#endif
/* The exact artwork layout the readers (LVGL BMP decoder, cover/accent/Album Wall readers) assume: 14-byte file
 * header + 40-byte BITMAPINFOHEADER, pixel offset 54, n x n (positive height = bottom-up), 1 plane, 24 bpp,
 * BI_RGB, rows padded to 4 bytes, and a file of exactly that length. Every field is checked in full. Used for
 * fresh helper output AND for cache hits. */
static uint32_t le32_(const unsigned char *p){ return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24; }
int art_bmp_valid(const char *path, int n){
    uint32_t stride = ((uint32_t)n * 3 + 3) & ~3u, dsz = stride * (uint32_t)n, want = 54 + dsz;
    unsigned char h[54];
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if(fd < 0) return 0;
    struct stat st;
    int ok = fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && (uint64_t)st.st_size == want && read(fd, h, 54) == 54 &&
             h[0] == 'B' && h[1] == 'M' && le32_(h + 2) == want && le32_(h + 6) == 0 && le32_(h + 10) == 54 &&
             le32_(h + 14) == 40 && le32_(h + 18) == (uint32_t)n && le32_(h + 22) == (uint32_t)n &&
             (h[26] | h[27] << 8) == 1 && (h[28] | h[29] << 8) == 24 && le32_(h + 30) == 0 &&
             (le32_(h + 34) == dsz || le32_(h + 34) == 0);
    close(fd);
    return ok;
}
/* In the CHILD, after fork: close every descriptor >= 3. Lists /proc/self/fd with raw openat/getdents64 (no malloc,
 * no stdio: safe after fork in a threaded process), so no descriptor escapes, however high. Returns 0 only if
 * closing is known to be complete; the child refuses to exec otherwise. If /proc cannot be opened, closes
 * 3..fallback_max, which is complete only when fallback_max covers the whole descriptor limit. */
struct art_dirent64 { uint64_t ino; int64_t off; unsigned short reclen; unsigned char type; char name[]; };
__attribute__((unused)) static int art_child_close_fds(int fallback_max, int fallback_complete){
    long dfd;
    do dfd = syscall(SYS_openat, AT_FDCWD, "/proc/self/fd", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    while(dfd < 0 && errno == EINTR);
    if(dfd < 0){
        for(int fd = 3; fd < fallback_max; fd++) close(fd);
        return fallback_complete ? 0 : -1;
    }
    char buf[2048];
    for(;;){
        long n = syscall(SYS_getdents64, (int)dfd, buf, sizeof buf);
        if(n < 0 && errno == EINTR) continue;
        if(n < 0){ close((int)dfd); return -1; }          /* listing incomplete: do not trust it */
        if(n == 0) break;
        for(long off = 0; off < n;){
            struct art_dirent64 *d = (struct art_dirent64 *)(buf + off);
            if(d->reclen == 0) { close((int)dfd); return -1; }
            int v = 0, ok = d->name[0] != 0;
            for(const char *c = d->name; *c; c++){ if(*c < '0' || *c > '9'){ ok = 0; break; } v = v * 10 + (*c - '0'); }
            if(ok && v >= 3 && v != (int)dfd) close(v);
            off += d->reclen;
        }
    }
    close((int)dfd);
    return 0;
}
/* The LIVE (user-driven) decode registers its child here so art_cancel() can kill
 * it when the user skips ahead. Prewarm / fallback decodes pass cancellable=0 and
 * are never registered, so cancelling a skip only targets the on-screen track. */
static pthread_mutex_t g_pid_mu = PTHREAD_MUTEX_INITIALIZER;
static volatile pid_t  g_live_pid = 0;
/* EVERY decoder child is also recorded here, cancellable or not. art_cancel() deliberately targets only
 * the on-screen track, but losing SD access is different: when the card must be released, a prewarm or
 * fallback decode holding it open has to go too. Small fixed array - the decoders are serialised by
 * g_decode_mu, so one or two entries are live at a time. */
#define ART_MAX_CHILDREN 8
static volatile pid_t g_all_pids[ART_MAX_CHILDREN];
__attribute__((unused)) static int art_child_slot_free(void){       /* caller holds g_pid_mu */
    for(int i = 0; i < ART_MAX_CHILDREN; i++) if(g_all_pids[i] == 0) return 1;
    return 0;
}
__attribute__((unused)) static void art_child_add(pid_t p){            /* caller holds g_pid_mu; a slot was reserved before fork */
    for(int i = 0; i < ART_MAX_CHILDREN; i++) if(g_all_pids[i] == 0){ g_all_pids[i] = p; return; }
}
static void art_child_del_locked(pid_t p){     /* caller holds g_pid_mu */
    for(int i = 0; i < ART_MAX_CHILDREN; i++) if(g_all_pids[i] == p){ g_all_pids[i] = 0; break; }
    if(g_live_pid == p) g_live_pid = 0;
}
/* Collect artwork children that outlived their request (still registered after the bounded reap). Decodes are
 * serialised by the callers' decode lock, so at the start of a new attempt every registered child is such a
 * leftover. The SD fault raised for it stays; this only stops it lingering as a zombie. Caller holds g_pid_mu. */
static void art_reap_leftovers_locked(void){
    for(int i = 0; i < ART_MAX_CHILDREN; i++){
        pid_t p = g_all_pids[i];
        if(p > 0 && waitpid(p, NULL, WNOHANG) == p) art_child_del_locked(p);
    }
}
/* Bumped by art_cancel(). A cancellable decode snapshots it on entry and re-checks it (under
 * g_pid_mu) before starting EACH helper child, so a skip is honored not just by killing the
 * running child but by refusing to launch a follow-up cover-fallback decode for the old track. */
static volatile unsigned g_cancel_gen = 0;
/* Bumped ONLY by art_kill_all() (SD access revoked). Unlike g_cancel_gen - which a track skip bumps and which
 * non-cancellable prewarm/fallback decodes deliberately ignore - EVERY decode request checks this before each
 * attempt, so revocation also stops the sibling-cover fallback from starting a new decoder. */
static volatile unsigned g_kill_gen = 0;
/* The g_kill_gen value a decode REQUEST started under, for this thread. art_run_pipeline compares it under
 * g_pid_mu immediately before every fork, so a revocation landing anywhere between the request's start and a
 * decoder launch - including during the sibling-cover access() probes - stops that launch. */
static __thread int      t_kill_armed = 0;
static __thread unsigned t_kill0 = 0;
/* Set by art_make_all_ex_gen: the token captured BEFORE it acquired SD admission, so a revocation that lands
 * between admission and the request start is still seen. Consumed (once) by the request. */
static __thread int      t_kill_preset_valid = 0;
static __thread unsigned t_kill_preset = 0;

/* Kill the in-flight LIVE decode (process group), if any, and supersede any between-attempt work.
 * Safe to call anytime. */
void art_cancel(void){
    pthread_mutex_lock(&g_pid_mu);
    g_cancel_gen++;
    pid_t p = g_live_pid;
    if(p > 0) kill(-p, SIGKILL);
    pthread_mutex_unlock(&g_pid_mu);
}
/* Kill EVERY decoder child, including the deliberately non-cancellable prewarm and fallback ones. Used
 * when SD access is revoked: a decoder still reading the card is exactly what must not outlive that. */
void art_kill_all(void){
    pthread_mutex_lock(&g_pid_mu);
    g_cancel_gen++;
    g_kill_gen++;
    for(int i = 0; i < ART_MAX_CHILDREN; i++){
        pid_t p = g_all_pids[i];
        if(p > 0){ kill(-p, SIGKILL); kill(p, SIGKILL); }
    }
    pthread_mutex_unlock(&g_pid_mu);
}

/* Run the artwork helper on ONE input as a killable child (execv, own process group).
 * cancellable=1 registers the child for art_cancel(). Returns 0 on success, -1 on error/no-art,
 * -2 if a newer request superseded this one (gen changed) before the child was launched. */
/* One helper run. saver_raw == NULL: "art" mode, publishing cover/thumb/backdrop. saver_raw != NULL: "saver" mode,
 * publishing one 360x360 BGRA file there (the three BMP paths are unused). */
/* The helper's own exit status from this thread's last run (its documented codes: 3 no picture, 4 unsupported,
 * 5 malformed, 6 over a limit, 7 I/O), or -1 when it was killed, cancelled, timed out or never started. Lets a caller
 * tell "this picture cannot be decoded" from "this attempt was interrupted" - the return value can't. */
static __thread int t_art_exit = -1;
int art_last_exit(void){ return t_art_exit; }
static int art_run_helper(const char *input, const char *saver_raw, const char *cover_bmp,
                    const char *thumb_bmp, const char *backdrop_bmp, int cancellable, unsigned gen0,
                    int64_t deadline_ms, int raw_size){
    t_art_exit = -1;
    if(art_now_ms() >= deadline_ms) return -1;     /* request deadline already spent: never start another child */
    /* A fresh directory per attempt, on the same filesystem as the outputs: the helper creates its three files
     * inside it exclusively, and only a complete, validated set is renamed into place. Leftovers from a killed
     * or failed attempt can never be mistaken for a result. */
    char stage[64]; snprintf(stage, sizeof stage, "/tmp/.diskos-artdec-XXXXXX");
    if(!mkdtemp(stage)) return -1;
    char sc[96], st_[96], sb[96], sv[96];
    snprintf(sc, sizeof sc, "%s/c.bmp", stage); snprintf(st_, sizeof st_, "%s/t.bmp", stage); snprintf(sb, sizeof sb, "%s/b.bmp", stage);
    snprintf(sv, sizeof sv, "%s/v.bgra", stage);
    /* the same arguments the standalone diskos-artdec takes */
    char *argv_art[] = { "diskos-artdec", "art", (char *)input, stage, NULL };
    char *argv_sav[] = { "diskos-artdec", raw_size == 364 ? "poster" : "saver", (char *)input, sv, NULL };
    char **argv = saver_raw ? argv_sav : argv_art;
    /* The decoder runs IN this thread (artdec_inproc.c): no child process, no fork, no slot to leak. A skip or an
     * SD revocation before it starts stops it here; one that lands while it runs is honoured just below, before
     * anything is published (the decode itself is short and bounded by its own size/pixel/memory limits). */
    int ret = -1;
    pthread_mutex_lock(&g_pid_mu);
    int stale = (cancellable && g_cancel_gen != gen0) || (t_kill_armed && g_kill_gen != t_kill0);
    pthread_mutex_unlock(&g_pid_mu);
    if(stale){ ret = -2; goto cleanup; }
    t_art_exit = artdec_inproc(argv[1], argv[2], argv[3]);
    if(t_art_exit != 0) goto cleanup;                              /* no picture, unsupported, bad input or limit */
    /* publish only a complete set with the exact expected layout, and only if this request is still current */
    if(saver_raw){
        struct stat vs;
        if(stat(sv, &vs) != 0 || !S_ISREG(vs.st_mode) || vs.st_size != raw_size * raw_size * 4) goto cleanup;
    } else if(!art_bmp_valid(sc, 148) || !art_bmp_valid(st_, 42) || !art_bmp_valid(sb, 360)) goto cleanup;
    /* The final "still wanted?" check and the renames happen under g_pid_mu, the lock every cancellation and
     * revocation takes: no cancel can land between the check and the publish. (Renames are within tmpfs.) */
    pthread_mutex_lock(&g_pid_mu);
    if((cancellable && g_cancel_gen != gen0) || (t_kill_armed && g_kill_gen != t_kill0)) ret = -2;
    else if(art_now_ms() >= deadline_ms) ret = -1;          /* finished past the request deadline: not used */
    else if(saver_raw) ret = rename(sv, saver_raw) == 0 ? 0 : -1;
    else if(rename(sc, cover_bmp) != 0) ret = -1;
    /* all or nothing: a later rename failing takes back the earlier ones */
    else if(rename(st_, thumb_bmp) != 0){ unlink(cover_bmp); ret = -1; }
    else if(rename(sb, backdrop_bmp) != 0){ unlink(cover_bmp); unlink(thumb_bmp); ret = -1; }
    else ret = 0;
    pthread_mutex_unlock(&g_pid_mu);
cleanup:
    unlink(sc); unlink(st_); unlink(sb); unlink(sv);               /* whatever is left of this attempt */
    rmdir(stage);
    return ret;
}

static int art_run_pipeline(const char *input, const char *cover_bmp,
                    const char *thumb_bmp, const char *backdrop_bmp, int cancellable, unsigned gen0,
                    int64_t deadline_ms){
    return art_run_helper(input, NULL, cover_bmp, thumb_bmp, backdrop_bmp, cancellable, gen0, deadline_ms, 360);
}

/* Sibling cover filenames tried when a track has no embedded art, in this order. The helper recognises JPEG,
 * PNG, BMP and GIF by content, so the name only decides precedence. exfat is case-insensitive, so a couple of
 * casings cover the common ones. */
static const char *ART_COVER_NAMES[] = {
    "cover.jpg","folder.jpg","cover.jpeg","folder.jpeg","cover.png","folder.png","cover.bmp","folder.bmp",
    "Cover.jpg","Folder.jpg","AlbumArt.jpg","albumart.jpg",
};
static const unsigned ART_COVER_N = sizeof ART_COVER_NAMES / sizeof ART_COVER_NAMES[0];
/* Mix the identity of the track's sidecar cover files (name, size, mtime of each one present, in lookup order)
 * into *h, so replacing, adding or removing a cover.jpg/png invalidates the cache entry. No inode: it is not
 * stable on exfat/vfat (see artcache.c fingerprint). */
void art_sidecar_signature(const char *track, uint64_t *h){
    const char *slash = strrchr(track, '/');
    if(!slash) return;
    size_t dlen = (size_t)(slash - track);
    if(dlen == 0 || dlen > 900) return;
    for(unsigned i = 0; i < ART_COVER_N; i++){
        char cov[1024]; struct stat st;
        int n = snprintf(cov, sizeof cov, "%.*s/%s", (int)dlen, track, ART_COVER_NAMES[i]);
        if(n <= 0 || n >= (int)sizeof cov || stat(cov, &st) != 0 || !S_ISREG(st.st_mode)) continue;
        uint64_t v[3] = { i + 1u, (uint64_t)st.st_size, (uint64_t)st.st_mtime };
        for(int k = 0; k < 3; k++) for(int b = 0; b < 8; b++){ *h ^= (v[k] >> (b * 8)) & 0xff; *h *= 1099511628211ULL; }
    }
}

/* Render cover/thumb/backdrop for a track. Prefer the track's EMBEDDED art (stock precedence); if
 * there is none, fall back to a sibling cover file in the track's folder so albums that keep their
 * art as cover.jpg/folder.jpg still show artwork instead of the no-art placeholder. A cancellable
 * decode snapshots the cancel generation and stops (returns -1, no wasted work) as soon as a skip
 * supersedes it, whether during the embedded run or between it and the cover fallback. */
/* Read the current cancel generation (for a caller that wants to capture it ATOMICALLY with its own
 * request identity, then pass it to art_make_all_ex_gen). art_cancel() is the only bumper and it's called
 * by the live-art setter while holding that setter's request lock, so a caller holding the same lock across
 * this read gets a token consistent with its request snapshot. */
unsigned art_cancel_gen(void){
    unsigned g; pthread_mutex_lock(&g_pid_mu); g = g_cancel_gen; pthread_mutex_unlock(&g_pid_mu); return g;
}

/* gen0 = the cancel token the CALLER captured with its request. A superseding skip bumps g_cancel_gen (via
 * art_cancel), so the per-child gen check trips and no obsolete decode runs - even if the skip lands after
 * the caller validated its request but before we get here. */
static int art_make_all_ex_gen_leased(const char *track, const char *cover_bmp,
                    const char *thumb_bmp, const char *backdrop_bmp, int cancellable, unsigned gen0){
    /* ONE 15s deadline across the whole request (embedded + every sibling-cover attempt), so a slow/hung
     * album can't hold g_decode_mu for attempts x 15s. Absolute monotonic so every attempt shares it. */
    int64_t deadline = art_now_ms() + 15000;
    /* SD revocation (art_kill_all) stops EVERY request: arm this thread's token, checked before each fork. The
     * token comes from before admission when the public wrapper captured one; direct callers snapshot here. */
    if(t_kill_preset_valid){ t_kill0 = t_kill_preset; t_kill_preset_valid = 0; }
    else { pthread_mutex_lock(&g_pid_mu); t_kill0 = g_kill_gen; pthread_mutex_unlock(&g_pid_mu); }
    t_kill_armed = 1;
    int r = art_run_pipeline(track, cover_bmp, thumb_bmp, backdrop_bmp, cancellable, gen0, deadline);
    if(r == 0){ t_kill_armed = 0; return 0; }     /* embedded art */
    if(r == -2){ t_kill_armed = 0; return -1; }   /* superseded or revoked - stop */

    const char *slash = strrchr(track, '/');       /* else try a sibling cover file */
    if(!slash){ t_kill_armed = 0; return -1; }
    size_t dlen = (size_t)(slash - track);
    if(dlen == 0 || dlen > 900){ t_kill_armed = 0; return -1; }
    for(unsigned i=0; i<sizeof ART_COVER_NAMES/sizeof ART_COVER_NAMES[0]; i++){
        if(art_now_ms() >= deadline) break;        /* request deadline spent -> stop trying */
        unsigned kg; pthread_mutex_lock(&g_pid_mu); kg = g_kill_gen; pthread_mutex_unlock(&g_pid_mu);
        if(kg != t_kill0) break;                   /* SD revoked since this request began - stop probing */
        char cov[1024];
        int n = snprintf(cov, sizeof cov, "%.*s/%s", (int)dlen, track, ART_COVER_NAMES[i]);
        if(n <= 0 || n >= (int)sizeof cov) continue;
        if(access(cov, R_OK) != 0) continue;       /* not present */
        r = art_run_pipeline(cov, cover_bmp, thumb_bmp, backdrop_bmp, cancellable, gen0, deadline);
        if(r == 0)  break;                          /* rendered from the external cover */
        if(r == -2) break;                          /* superseded or revoked between attempts - stop */
    }
    t_kill_armed = 0;
    return r == 0 ? 0 : -1;
}

/* The vinyl screensaver's sharp 360x360 cover for `track`, written to out_raw as top-down BGRA. Same rules as the
 * artwork requests: SD admission for the whole request, the caller's absolute deadline (CLOCK_MONOTONIC ms, set
 * when the request was made, so time spent queued counts), revocation stops every attempt,
 * embedded art first then the sidecar cover files. Not cancellable by track skips (the saver checks its own
 * request generation before using the result). Blocking: call it from a worker thread, never the UI thread. */
static int art_make_raw(const char *track, const char *out_raw, int64_t deadline_ms, int size, int cancellable, unsigned gen0){
    unsigned kill0;
    /* collect abandoned children first: after an unreaped timeout admission stays closed, so this must not
     * depend on getting past sd_io_begin (the SD fault itself is kept) */
    pthread_mutex_lock(&g_pid_mu); art_reap_leftovers_locked(); kill0 = g_kill_gen; pthread_mutex_unlock(&g_pid_mu);
    if(!sd_io_begin()) return -1;
    t_kill0 = kill0; t_kill_armed = 1;
    int64_t deadline = deadline_ms;                           /* fixed when the request was POSTED, not now */
    int r = art_run_helper(track, out_raw, NULL, NULL, NULL, cancellable, gen0, deadline, size);
    if(r == -1){
        const char *slash = strrchr(track, '/');
        size_t dlen = slash ? (size_t)(slash - track) : 0;
        for(unsigned i = 0; dlen > 0 && dlen <= 900 && i < ART_COVER_N; i++){
            if(art_now_ms() >= deadline) break;
            unsigned kg; pthread_mutex_lock(&g_pid_mu); kg = g_kill_gen; pthread_mutex_unlock(&g_pid_mu);
            if(kg != t_kill0) break;
            char cov[1024];
            int n = snprintf(cov, sizeof cov, "%.*s/%s", (int)dlen, track, ART_COVER_NAMES[i]);
            if(n <= 0 || n >= (int)sizeof cov || access(cov, R_OK) != 0) continue;
            r = art_run_helper(cov, out_raw, NULL, NULL, NULL, cancellable, gen0, deadline, size);
            if(r == 0 || r == -2) break;
        }
    }
    t_kill_armed = 0;
    sd_io_end();
    return r == 0 ? 0 : -1;
}

/* Both sharp-cover clients share the same admission, revocation, staging and deadline rules. */
int art_make_saver(const char *track, const char *out_raw, int64_t deadline_ms){
    return art_make_raw(track, out_raw, deadline_ms, 360, 0, 0);
}
/* 360x360 BGRA -> 364x364 BGRA (nearest neighbour), so an older helper's "saver" output can stand in for "poster". */
static int art_upscale_364(const char *in, const char *out){
    int rc = -1; uint32_t *s = malloc(360 * 360 * 4), *row = malloc(364 * 4);
    FILE *fi = NULL, *fo = NULL;
    if(!s || !row) goto done;
    if(!(fi = fopen(in, "rb")) || fread(s, 4, 360 * 360, fi) != 360 * 360) goto done;
    char tmp[300]; snprintf(tmp, sizeof tmp, "%s.up", out);
    if(!(fo = fopen(tmp, "wb"))) goto done;
    for(int y = 0; y < 364 && rc == -1; y++){
        const uint32_t *sr = s + (size_t)(y * 360 / 364) * 360;
        for(int x = 0; x < 364; x++) row[x] = sr[x * 360 / 364];
        if(fwrite(row, 4, 364, fo) != 364) rc = -2;
    }
    if(fclose(fo) != 0) rc = -2;
    fo = NULL;
    if(rc == -1 && rename(tmp, out) == 0) rc = 0; else unlink(tmp);
done:
    if(fi) fclose(fi);
    if(fo) fclose(fo);
    free(s); free(row);
    return rc;
}
int art_make_poster(const char *track, const char *out_raw, unsigned gen0){
    int r = art_make_raw(track, out_raw, art_now_ms() + 8000, 364, 1, gen0);
    if(r == 0 || art_last_exit() != 2) return r;
    /* exit 2 = usage: an older diskos-artdec without "poster" (e.g. the stock 1.2.0 payload). Use its 360px
     * "saver" picture, scaled to the 364px geometry, so the sharp cover and immersive still work. */
    char tmp[300]; snprintf(tmp, sizeof tmp, "%s.360", out_raw);
    r = art_make_raw(track, tmp, art_now_ms() + 8000, 360, 1, gen0);
    if(r == 0) r = art_upscale_364(tmp, out_raw) == 0 ? 0 : -1;
    unlink(tmp);
    return r;
}

/* Snapshotting wrapper: captures the cancel generation internally (fine for callers that don't need to
 * couple it to an external request identity - non-cancellable ones ignore gen0 entirely). */
int art_make_all_ex(const char *track, const char *cover_bmp,
                    const char *thumb_bmp, const char *backdrop_bmp, int cancellable){
    unsigned gen0;
    pthread_mutex_lock(&g_pid_mu); gen0 = g_cancel_gen; pthread_mutex_unlock(&g_pid_mu);
    return art_make_all_ex_gen(track, cover_bmp, thumb_bmp, backdrop_bmp, cancellable, gen0);
}

/* Non-cancellable convenience wrapper (prewarm + synchronous fallback). */
int art_make_all(const char *track, const char *cover_bmp,
                 const char *thumb_bmp, const char *backdrop_bmp){
    return art_make_all_ex(track, cover_bmp, thumb_bmp, backdrop_bmp, 0);   /* NOLINT */
}

int art_make_all_ex_gen(const char *track, const char *cover_bmp, const char *thumb_bmp, const char *backdrop_bmp, int cancellable, unsigned gen0){
    /* Capture the revocation token BEFORE admission: taken after sd_io_begin, a revocation landing in between
     * would be absorbed into the snapshot and the request would still fork a decoder. */
    unsigned kill0;
    /* collect abandoned children first (independent of admission, which an unreaped child keeps closed) */
    pthread_mutex_lock(&g_pid_mu); art_reap_leftovers_locked(); kill0 = g_kill_gen; pthread_mutex_unlock(&g_pid_mu);
    if(!sd_io_begin()) return -1;
    t_kill_preset = kill0; t_kill_preset_valid = 1;
    int result = art_make_all_ex_gen_leased(track, cover_bmp, thumb_bmp, backdrop_bmp, cancellable, gen0);
    t_kill_preset_valid = 0;
    sd_io_end();
    return result;
}
