/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
/* Over-the-air diskOS UI updates, core (no LVGL; host-tested). Contract: update-system/OTA_CONTRACT.md.
 * Flow: releases/latest -> tag + "are the six bundle assets listed" -> wget each file into <updates>/incoming (bounded,
 * cancellable, stall-guarded) -> the read-only rootfs verifier (the same sh file boot runs) -> fsync -> ONE atomic
 * rename(incoming, pending). Nothing here installs a binary or touches /usr/data/mq_ui. */
#include <spawn.h>
#include <signal.h>
extern char **environ;
#include "ota.h"
#include "command_spawn.h"
#include "fork_build.h"
#include "system.h"
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>
#include <glob.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <poll.h>
#include <sys/statvfs.h>
#include <sys/wait.h>

const ota_paths_t OTA_DEV = {
    "/usr/data/updates", "/etc/diskos-ota/root.pub.pem", "/etc/diskos-ota/epochs", "/etc/diskos-ota/diskos-verify.sh",
    SYS_GITHUB_LATEST, SYS_GITHUB_DOWNLOAD, "/usr/data", "/sys/class/net/wlan0/operstate", "/tmp/.diskos_run", "/tmp/.diskos_launched", "/proc/asound/card*/pcm*p/sub*/status", "/proc/self/exe"
};

/* the release asset (all under one tag), the name it is saved as, and its size cap (0 = the manifest's size) */
static const struct { const char *asset, *local; long cap; } OTA_FILES[OTA_NFILES] = {
    { "diskos-ota-keyauthz",              "keyauthz",             8192 },
    { "diskos-ota-keyauthz.sig",          "keyauthz.sig",         4096 },
    { "diskos-ota-leafpub.pem",           "leafpub.pem",          4096 },
    { "diskos-ota-update.manifest",       "update.manifest",      8192 },
    { "diskos-ota-update.manifest.sig",   "update.manifest.sig",  4096 },
    { "diskos-ota-mq_ui",                 "mq_ui",                0 },
};
#define OTA_MANIFEST_IDX 3
#define OTA_UI_IDX 5
#define OTA_JSON_CAP (256 * 1024)
#define OTA_MARGIN (1024 * 1024)

int ota_asset_name(int i, const char **asset, const char **local){
    if(i < 0 || i >= OTA_NFILES) return -1;
    if(asset) *asset = OTA_FILES[i].asset;
    if(local) *local = OTA_FILES[i].local;
    return 0;
}

/* ---------------------------------------------------------------- release JSON */
/* Minimal JSON walk: only the "assets" array of the top-level release object counts, and only each element's own
 * top-level "name" string. A release title or a nested "name" is never an asset. Malformed input = no assets. */
static const char *jws(const char *s){ while(*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++; return s; }
static const char *jstr_end(const char *s){                       /* s at an opening quote; returns the closing quote or NULL */
    for(s++; *s; s++){ if(*s == '\\'){ if(!*++s) return NULL; continue; } if(*s == '"') return s; }
    return NULL;
}
static const char *jskip(const char *s, int depth){               /* skip one value; returns the char after it, or NULL */
    s = jws(s);
    if(depth > 12 || !*s) return NULL;
    if(*s == '"'){ const char *e = jstr_end(s); return e ? e + 1 : NULL; }
    if(*s == '{' || *s == '['){
        char close = *s == '{' ? '}' : ']';
        s = jws(s + 1);
        if(*s == close) return s + 1;
        for(;;){
            if(close == '}'){ if(*s != '"') return NULL; const char *e = jstr_end(s); if(!e) return NULL; s = jws(e + 1); if(*s != ':') return NULL; s++; }
            s = jskip(s, depth + 1); if(!s) return NULL;
            s = jws(s);
            if(*s == ','){ s = jws(s + 1); continue; }
            return *s == close ? s + 1 : NULL;
        }
    }
    while(*s && *s != ',' && *s != '}' && *s != ']' && *s != ' ' && *s != '\n' && *s != '\r' && *s != '\t') s++;   /* number/true/null */
    return s;
}
int ota_assets_ok(const char *json){
    if(!json) return 0;
    int seen[OTA_NFILES] = {0}, have_assets = 0;
    const char *s = jws(json);
    if(*s != '{') return 0;
    s = jws(s + 1);
    while(*s == '"'){
        const char *ke = jstr_end(s); if(!ke) return 0;
        int is_assets = (ke - s - 1 == 6 && !strncmp(s + 1, "assets", 6));
        s = jws(ke + 1); if(*s != ':') return 0;
        s = jws(s + 1);
        if(is_assets){
            if(have_assets || *s != '[') return 0;                    /* duplicate or non-array "assets" */
            have_assets = 1;
            s = jws(s + 1);
            while(*s && *s != ']'){
                if(*s != '{'){ s = jskip(s, 1); if(!s) return 0; }
                else {
                    s = jws(s + 1);
                    while(*s == '"'){
                        const char *ne = jstr_end(s); if(!ne) return 0;
                        int is_name = (ne - s - 1 == 4 && !strncmp(s + 1, "name", 4));
                        s = jws(ne + 1); if(*s != ':') return 0;
                        s = jws(s + 1);
                        if(is_name && *s == '"'){
                            const char *ve = jstr_end(s); if(!ve) return 0;
                            for(int i = 0; i < OTA_NFILES; i++){
                                size_t l = strlen(OTA_FILES[i].asset);
                                if((size_t)(ve - s - 1) == l && !strncmp(s + 1, OTA_FILES[i].asset, l)) seen[i]++;
                            }
                        }
                        s = jskip(s, 2); if(!s) return 0;
                        s = jws(s);
                        if(*s == ','){ s = jws(s + 1); continue; }
                        break;
                    }
                    if(*s != '}') return 0;
                    s++;
                }
                s = jws(s);
                if(*s == ','){ s = jws(s + 1); continue; }
                break;
            }
            if(*s != ']') return 0;
            s++;
        } else { s = jskip(s, 1); if(!s) return 0; }
        s = jws(s);
        if(*s == ','){ s = jws(s + 1); continue; }
        break;
    }
    if(*s != '}' || !have_assets) return 0;
    for(int i = 0; i < OTA_NFILES; i++) if(seen[i] != 1) return 0;    /* every asset exactly once */
    return 1;
}

/* Strict version grammar: [v]N.N.N, each 1-5 digits, nothing before or after. */
int ota_ver_parse(const char *s, int allow_v, int v[3]){
    if(!s) return -1;
    if(allow_v && *s == 'v') s++;
    for(int i = 0; i < 3; i++){
        int n = 0, d = 0;
        while(isdigit((unsigned char)*s) && d < 5){ n = n * 10 + (*s - '0'); s++; d++; }
        if(!d || isdigit((unsigned char)*s)) return -1;
        v[i] = n;
        if(i < 2){ if(*s != '.') return -1; s++; }
    }
    return *s ? -1 : 0;
}
int ota_ver_cmp(const char *a, int a_v, const char *b, int b_v){    /* -1 a<b, 0, 1, or -2 if either is not strict */
    int x[3], y[3];
    if(ota_ver_parse(a, a_v, x) != 0 || ota_ver_parse(b, b_v, y) != 0) return -2;
    for(int i = 0; i < 3; i++) if(x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
    return 0;
}

/* ---------------------------------------------------------------- child processes */
/* Run argv, capture stdout (and stderr when merge_err) into buf (cap bytes, NUL-terminated). Returns the exit status,
 * or -1 for a signal/spawn failure. *capped = the output hit the cap (child killed; the caller treats it as failure). */
/* Keep a descriptor above 0..2. mq_ui is started without stdin/stdout (only stderr is reopened, to the boot log), so a
 * fresh descriptor can BE fd 0 or 1. A child then dup2()s /dev/null and the output pipe over 0..2 and silently replaces
 * it: the verifier was handed /proc/self/fd/1 (the pipe) as the update folder and rejected every SD update as
 * "damaged". Moving them to 3+ keeps the child's stdio and the descriptors it is given apart. */
static int fd_hi(int fd){
    if(fd < 0 || fd > 2) return fd;
    int fl = fcntl(fd, F_GETFD);
    int n = fcntl(fd, (fl >= 0 && (fl & FD_CLOEXEC)) ? F_DUPFD_CLOEXEC : F_DUPFD, 3);
    close(fd);
    return n;
}
static char g_why[700];                                                   /* the detail of the last staging failure */
static void why(const char *fmt, const char *a, int b){ snprintf(g_why, sizeof g_why, fmt, a, b); }
/* Runs argv with stdout (and stderr if merge_err) into buf. posix_spawn, not fork(): mq_ui is big, and a fork() of it
 * can fail for memory on the Disc (the rest of the UI spawns this way too). On a failure before the child ran,
 * buf says which step failed and why. */
#define RC_SIGNAL  (-1)                                                   /* the child was killed by a signal */
#define RC_NOSTART (-2)                                                   /* pipe / spawn / wait failed: it never ran properly */
#define RC_TIMEOUT (-3)                                                   /* no end within the deadline: killed */
static int run_capture(char *const argv[], int use_path, int merge_err, char *buf, size_t cap, size_t *got, int *capped, int timeout_ms){
    int pfd[2], e;
    *got = 0; *capped = 0; if(cap) buf[0] = 0;
    /* both ends close-on-exec: a helper started by another thread meanwhile must not inherit the write end (the read
     * below would then wait for THAT process to end). The child's own copy is made by dup2, which clears the flag. */
    if(pipe2(pfd, O_CLOEXEC) != 0){ snprintf(buf, cap, "pipe failed (errno %d)", errno); return RC_NOSTART; }
    pfd[0] = fd_hi(pfd[0]); pfd[1] = fd_hi(pfd[1]);
    if(pfd[0] < 0 || pfd[1] < 0){ snprintf(buf, cap, "fd move failed (errno %d)", errno);
                                  if(pfd[0] >= 0) close(pfd[0]);
                                  if(pfd[1] >= 0) close(pfd[1]);
                                  return RC_NOSTART; }
    posix_spawn_file_actions_t fa; posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_adddup2(&fa, pfd[1], 1);
    if(merge_err) posix_spawn_file_actions_adddup2(&fa, pfd[1], 2);
    else posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
    posix_spawnattr_t at; posix_spawnattr_init(&at);
    sigset_t def; sigemptyset(&def); sigaddset(&def, SIGCHLD); sigaddset(&def, SIGPIPE);
    sigset_t none; sigemptyset(&none);
    posix_spawnattr_setsigdefault(&at, &def); posix_spawnattr_setsigmask(&at, &none);
    posix_spawnattr_setflags(&at, POSIX_SPAWN_SETPGROUP | POSIX_SPAWN_SETSIGDEF | POSIX_SPAWN_SETSIGMASK);
    posix_spawnattr_setpgroup(&at, 0);
    pid_t p;
    e = use_path ? posix_spawnp(&p, argv[0], &fa, &at, argv, environ) : posix_spawn(&p, argv[0], &fa, &at, argv, environ);
    posix_spawn_file_actions_destroy(&fa); posix_spawnattr_destroy(&at);
    close(pfd[1]);
    if(e != 0){ close(pfd[0]); snprintf(buf, cap, "spawn %s failed (errno %d)", argv[0], e); return RC_NOSTART; }
    struct timespec t0; clock_gettime(CLOCK_MONOTONIC, &t0);
    int timed_out = 0;
    while(*got < cap){                                                    /* read until EOF, the cap or the deadline */
        struct timespec now; clock_gettime(CLOCK_MONOTONIC, &now);
        long el = (now.tv_sec - t0.tv_sec) * 1000L + (now.tv_nsec - t0.tv_nsec) / 1000000L;
        if(el >= timeout_ms){ timed_out = 1; break; }
        struct pollfd pf = { pfd[0], POLLIN, 0 };
        int pr = poll(&pf, 1, (int)(timeout_ms - el));
        if(pr < 0){ if(errno == EINTR) continue; break; }
        if(pr == 0){ timed_out = 1; break; }
        ssize_t r = read(pfd[0], buf + *got, cap - *got);
        if(r < 0 && (errno == EINTR || errno == EAGAIN)) continue;
        if(r <= 0) break;
        *got += (size_t)r;
    }
    if(*got >= cap) *capped = 1;
    if(*capped || timed_out){ killpg(p, SIGKILL); kill(p, SIGKILL); }   /* the whole group: a shell's children too */
    close(pfd[0]);
    buf[*got < cap ? *got : cap - 1] = 0;
    int st = 0, waited = 0;
    while(!waited){ if(waitpid(p, &st, 0) >= 0) waited = 1; else if(errno != EINTR) break; }
    if(timed_out){ size_t n = strlen(buf); snprintf(buf + n, cap - n, "%sno result after %d s", n ? " / " : "", timeout_ms / 1000); return RC_TIMEOUT; }
    if(!waited){ size_t n = strlen(buf); snprintf(buf + n, cap - n, "%swait failed (errno %d)", n ? " / " : "", errno); return RC_NOSTART; }
    if(WIFSIGNALED(st)){ size_t n = strlen(buf); snprintf(buf + n, cap - n, "%skilled by signal %d", n ? " / " : "", WTERMSIG(st)); return RC_SIGNAL; }
    return WIFEXITED(st) ? WEXITSTATUS(st) : RC_SIGNAL;
}

int ota_fetch_release(const ota_paths_t *p, char *tag, size_t n, int *has_bundle){
    if(has_bundle) *has_bundle = 0;
    char *buf = malloc(OTA_JSON_CAP + 1);
    if(!buf) return -1;
    char *argv[] = { "wget", "-qO-", "-T", "12", (char *)p->latest_url, NULL };
    size_t got; int capped;
    int rc = run_capture(argv, 1, 0, buf, OTA_JSON_CAP + 1, &got, &capped, 60000);
    int ok = -1;
    if(rc == 0 && !capped && sys_parse_tag(buf, tag, n) == 0){
        ok = 0;
        int tv[3];
        if(has_bundle) *has_bundle = ota_ver_parse(tag, 1, tv) == 0 && ota_assets_ok(buf);   /* a tag outside the grammar is never offered */
    }
    free(buf);
    return ok;
}

int ota_wifi_down(const ota_paths_t *p){
    char b[24] = "";
    int fd = open(p->operstate, O_RDONLY | O_CLOEXEC);
    if(fd < 0) return 0;                                  /* unknown state is not "down": let the fetch decide */
    ssize_t r = read(fd, b, sizeof b - 1);
    close(fd);
    if(r <= 0) return 0;
    b[r] = 0;
    while(r > 0 && (b[r - 1] == '\n' || b[r - 1] == '\r' || b[r - 1] == ' ')) b[--r] = 0;
    return !strcmp(b, "down");
}

/* ---------------------------------------------------------------- small file helpers */
static int read_file(const char *path, char *buf, size_t n){          /* regular non-symlink file, NUL-terminated; length or -1 */
    struct stat st;
    if(lstat(path, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 0 || (size_t)st.st_size >= n) return -1;
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if(fd < 0) return -1;
    ssize_t r = read(fd, buf, n - 1);
    close(fd);
    if(r < 0) return -1;
    buf[r] = 0;
    return (int)r;
}
static int read_at(int dfd, const char *name, char *buf, size_t n){   /* same, relative to a directory fd (no path re-resolution) */
    int fd = openat(dfd, name, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if(fd < 0) return -1;
    struct stat st;
    if(fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 0 || (size_t)st.st_size >= n){ close(fd); return -1; }
    ssize_t r = read(fd, buf, n - 1);
    close(fd);
    if(r < 0) return -1;
    buf[r] = 0;
    return (int)r;
}
static int all_digits(const char *s, size_t max){ size_t l = strlen(s); if(!l || l > max) return 0; for(; *s; s++) if(!isdigit((unsigned char)*s)) return 0; return 1; }
static void rm_at(int dfd, const char *name, int depth){                /* unlinkat; recurse into real directories only (never follows links) */
    struct stat st;
    if(fstatat(dfd, name, &st, AT_SYMLINK_NOFOLLOW) != 0) return;
    if(S_ISDIR(st.st_mode)){
        if(depth < 4){
            int fd = openat(dfd, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            if(fd >= 0){
                DIR *d = fdopendir(fd);
                if(d){
                    struct dirent *e;
                    while((e = readdir(d))) if(strcmp(e->d_name, ".") && strcmp(e->d_name, "..")) rm_at(dirfd(d), e->d_name, depth + 1);
                    closedir(d);
                } else close(fd);
            }
        }
        unlinkat(dfd, name, AT_REMOVEDIR);
    } else unlinkat(dfd, name, 0);
}
static int fsync_at(int dfd, const char *name){                        /* 0 = durable; the error is propagated, never dropped */
    int fd = openat(dfd, name, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if(fd < 0) return -1;
    int r = fsync(fd);
    close(fd);
    return r;
}
static int64_t ota_free_bytes(const char *dir){
    struct statvfs sv;
    if(statvfs(dir, &sv) != 0) return -1;
    return (int64_t)((unsigned long long)sv.f_bavail * (unsigned long long)sv.f_frsize);
}
/* the staging root: created 0700, must be a real directory (not a link) owned by the running user with no group/other bits */
static int open_updates(const ota_paths_t *p){
    if(mkdir(p->updates, 0700) != 0 && errno != EEXIST) return -1;
    int fd = open(p->updates, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if(fd < 0) return -1;
    struct stat st;
    if(fstat(fd, &st) != 0 || !S_ISDIR(st.st_mode) || st.st_uid != geteuid()){ close(fd); return -1; }
    if((st.st_mode & 077) != 0 && fchmod(fd, 0700) != 0){ close(fd); return -1; }
    return fd;
}

/* ---------------------------------------------------------------- SHA-256 (health binding: hash of the running executable) */
typedef struct { uint32_t h[8]; uint64_t len; unsigned char buf[64]; size_t n; } sha_t;
static const uint32_t SHA_K[64] = {
0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2 };
#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
static void sha_block(sha_t *c, const unsigned char *b){
    uint32_t w[64], a[8];
    for(int i = 0; i < 16; i++) w[i] = (uint32_t)b[i*4] << 24 | (uint32_t)b[i*4+1] << 16 | (uint32_t)b[i*4+2] << 8 | b[i*4+3];
    for(int i = 16; i < 64; i++){
        uint32_t s0 = ROR(w[i-15], 7) ^ ROR(w[i-15], 18) ^ (w[i-15] >> 3), s1 = ROR(w[i-2], 17) ^ ROR(w[i-2], 19) ^ (w[i-2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    memcpy(a, c->h, sizeof a);
    for(int i = 0; i < 64; i++){
        uint32_t t1 = a[7] + (ROR(a[4], 6) ^ ROR(a[4], 11) ^ ROR(a[4], 25)) + ((a[4] & a[5]) ^ (~a[4] & a[6])) + SHA_K[i] + w[i];
        uint32_t t2 = (ROR(a[0], 2) ^ ROR(a[0], 13) ^ ROR(a[0], 22)) + ((a[0] & a[1]) ^ (a[0] & a[2]) ^ (a[1] & a[2]));
        a[7] = a[6]; a[6] = a[5]; a[5] = a[4]; a[4] = a[3] + t1; a[3] = a[2]; a[2] = a[1]; a[1] = a[0]; a[0] = t1 + t2;
    }
    for(int i = 0; i < 8; i++) c->h[i] += a[i];
}
static void sha_add(sha_t *c, const unsigned char *d, size_t l){
    c->len += l;
    while(l){ size_t k = 64 - c->n < l ? 64 - c->n : l; memcpy(c->buf + c->n, d, k); c->n += k; d += k; l -= k; if(c->n == 64){ sha_block(c, c->buf); c->n = 0; } }
}
static int sha256_file(const char *path, char hex[65]){
    static const uint32_t init[8] = { 0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19 };
    sha_t c; memcpy(c.h, init, sizeof init); c.len = 0; c.n = 0;
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if(fd < 0) return -1;
    unsigned char b[4096]; ssize_t r;
    while((r = read(fd, b, sizeof b)) > 0) sha_add(&c, b, (size_t)r);
    close(fd);
    if(r < 0) return -1;
    uint64_t bits = c.len * 8;
    unsigned char pad = 0x80; sha_add(&c, &pad, 1);
    unsigned char z = 0; while(c.n != 56) sha_add(&c, &z, 1);
    unsigned char lb[8]; for(int i = 0; i < 8; i++) lb[i] = (unsigned char)(bits >> (56 - 8 * i));
    sha_add(&c, lb, 8);
    for(int i = 0; i < 8; i++) snprintf(hex + i * 8, 9, "%08x", c.h[i]);
    return 0;
}

int ota_supported(const ota_paths_t *p){
    return access(p->rootpub, R_OK) == 0 && access(p->epochs, R_OK) == 0 && access(p->verifier, R_OK) == 0;
}
/* MIN_KEY_EPOCH / MIN_UI_EPOCH from the RO epochs file; both strict digits or the update is refused (fail closed). */
static int read_epochs(const ota_paths_t *p, char *key, char *ui, size_t n){
    char t[256];
    if(read_file(p->epochs, t, sizeof t) < 0) return -1;
    if(sys_kv(t, "MIN_KEY_EPOCH", key, n) != 0 || sys_kv(t, "MIN_UI_EPOCH", ui, n) != 0) return -1;
    if(!all_digits(key, 9) || !all_digits(ui, 9)) return -1;
    return 0;
}

/* ---------------------------------------------------------------- download one file */
/* wget writes into the incoming directory THROUGH ITS FD (/proc/self/fd/N/...), so a swapped pathname cannot redirect it;
 * a file-size limit just above cap stops it on the device even if this poller is slow (an over-cap file stays detectable). */
static int dl_file(const ota_paths_t *p, const char *tag, const char *asset, int ifd, const char *local, long long cap, ota_prog_t *pg, long long base){
    char url[400], part[80], target[160];
    if(snprintf(url, sizeof url, "%s/%s/%s", p->dl_base, tag, asset) >= (int)sizeof url) return OTA_E_BAD;
    if(snprintf(part, sizeof part, "%s.part", local) >= (int)sizeof part) return OTA_E_BAD;
    snprintf(target, sizeof target, "/proc/self/fd/3/%s", part);       /* the incoming directory is the child's fd 3 */
    unlinkat(ifd, part, 0);
    /* posix_spawn, not fork() (a fork of the big UI can fail for memory). The file-size limit is set by a small shell
     * (ulimit -f counts 512-byte blocks: the cap rounded up, +1 block, so an over-cap file is still detectable below);
     * the arguments go in as $1..$3, never into the command text. */
    char blocks[24]; snprintf(blocks, sizeof blocks, "%lld", cap > 0 ? cap / 512 + 2 : 0LL);
    char *argv[] = { "/bin/sh", "-c", cap > 0 ? "ulimit -f \"$1\" || exit 126; exec wget -q -T 15 -t 1 -O \"$2\" \"$3\""
                                              : "exec wget -q -T 15 -t 1 -O \"$2\" \"$3\"",
                     "sh", blocks, target, url, NULL };
    pid_t pid;
    if(command_spawn_io(&pid, argv, -1, ifd) != 0) return OTA_E_IO;
    struct timespec now; clock_gettime(CLOCK_MONOTONIC, &now);
    time_t last = now.tv_sec; long long lastsz = -1;
    int st = 0, rc = OTA_OK;
    for(;;){
        pid_t r = waitpid(pid, &st, WNOHANG);
        struct stat sb; long long sz = fstatat(ifd, part, &sb, AT_SYMLINK_NOFOLLOW) == 0 ? (long long)sb.st_size : 0;
        atomic_store(&pg->done, (long)(base + sz));
        if(r == pid) break;
        if(r < 0 && errno != EINTR){ rc = OTA_E_IO; break; }
        int kill_it = 0;
        clock_gettime(CLOCK_MONOTONIC, &now);
        if(atomic_load(&pg->cancel)){ rc = OTA_E_CANCEL; kill_it = 1; }
        else if(cap > 0 && sz > cap){ rc = OTA_E_BAD; kill_it = 1; }                    /* bigger than the signed size: stop now */
        else if(sz != lastsz){ lastsz = sz; last = now.tv_sec; }
        else if(now.tv_sec - last >= OTA_STALL_S){ rc = OTA_E_NET; kill_it = 1; }
        if(kill_it){
            killpg(pid, SIGKILL); kill(pid, SIGKILL);
            while(waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
            unlinkat(ifd, part, 0);
            return rc;
        }
        struct timespec ts = { 0, 50 * 1000 * 1000 };
        nanosleep(&ts, NULL);
    }
    if(rc != OTA_OK){ unlinkat(ifd, part, 0); return rc; }
    struct stat sb;
    int have = fstatat(ifd, part, &sb, AT_SYMLINK_NOFOLLOW) == 0 && S_ISREG(sb.st_mode);
    if(have && cap > 0 && (long long)sb.st_size > cap){ unlinkat(ifd, part, 0); return OTA_E_BAD; }   /* incl. wget stopped by the rlimit */
    if(!WIFEXITED(st) || WEXITSTATUS(st) != 0 || !have || sb.st_size <= 0){ unlinkat(ifd, part, 0); return OTA_E_NET; }
    if(renameat(ifd, part, ifd, local) != 0){ unlinkat(ifd, part, 0); return OTA_E_IO; }
    return OTA_OK;
}

/* ---------------------------------------------------------------- verify */
static int reject_class(const char *out){
    if(strstr(out, "signature invalid") || strstr(out, "substituted key") || strstr(out, "bad magic") || strstr(out, "role '")) return OTA_E_SIG;
    if(strstr(out, "downgrade") || strstr(out, "revoked key")) return OTA_E_OLDER;
    return OTA_E_BAD;
}
static int tok(const char *line, const char *key, char *out, size_t n){    /* " key=value" token of the verifier's OK line */
    char pat[24]; snprintf(pat, sizeof pat, " %s=", key);
    const char *k = strstr(line, pat);
    if(!k) return -1;
    k += strlen(pat);
    size_t i = 0;
    while(*k && *k != ' ' && *k != '\n'){
        if(!(isalnum((unsigned char)*k) || *k == '.' || *k == '-' || *k == '_') || i + 1 >= n) return -1;
        out[i++] = *k++;
    }
    if(!i) return -1;
    out[i] = 0;
    return 0;
}

static int fin_at(int ufd, int ifd, int rc, int line){                    /* failure exit: drop the work dir, release the fds */
    if(rc != OTA_OK && ufd >= 0) rm_at(ufd, "incoming", 0);
    if(ufd >= 0){                                                          /* /usr/data/updates/last_result: why, for the next look */
        int fd = openat(ufd, "last_result", O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0600);
        if(fd >= 0){ char b[900]; int n = snprintf(b, sizeof b, "rc=%d at ota.c:%d\n%s\n", rc, line, rc == OTA_OK ? "" : g_why);
                     if(n > (int)sizeof b - 1) n = (int)sizeof b - 1;
                     if(write(fd, b, (size_t)n) < 0){ /* best effort */ }
                     close(fd); }
    }
    g_why[0] = 0;
    if(ifd >= 0) close(ifd);
    if(ufd >= 0) close(ufd);
    return rc;
}
#define fin(u, i, r) fin_at((u), (i), (r), __LINE__)

int ota_stage(const ota_paths_t *p, const char *running, const char *tag, ota_prog_t *pg){
    char kep[16], uep[16], path[300];
    atomic_store(&pg->done, 0); atomic_store(&pg->total, 0); atomic_store(&pg->phase, 1); pg->need_free = 0; pg->version[0] = 0;

    if(!ota_allowed()) return OTA_E_OFF;                                       /* turned off in Settings: nothing is downloaded or staged */
    if(!ota_supported(p)) return OTA_E_UNSUPPORTED;
    if(read_epochs(p, kep, uep, sizeof kep) != 0) return OTA_E_UNSUPPORTED;
    int tv[3];
    if(ota_ver_parse(running, 0, tv) != 0) return OTA_E_OLDER;
    if(ota_ver_parse(tag, 1, tv) != 0) return OTA_E_BAD;                       /* the whole tag must be vN.N.N */
    if(ota_ver_cmp(running, 0, tag, 1) != -1) return OTA_E_OLDER;              /* only strictly newer */
    int64_t fr = ota_free_bytes(p->data_root);
    if(fr < OTA_MARGIN){ pg->need_free = OTA_MARGIN; return OTA_E_SPACE; }

    int ufd = open_updates(p), ifd = -1;
    if(ufd < 0) return OTA_E_IO;                                               /* a link, a foreign owner or an unfixable mode: refuse */
    /* defence in depth: the last accepted epoch (wipeable, so never lower than the baked floor); same floor boot uses: accepted + 1 */
    char acc[128], av[16];
    if(read_at(ufd, "accepted", acc, sizeof acc) > 0 && sys_kv(acc, "epoch", av, sizeof av) == 0 && all_digits(av, 9) && atol(av) + 1 > atol(uep))
        snprintf(uep, sizeof uep, "%ld", atol(av) + 1);
    /* an interrupted pending replacement: put the aside copy back, or drop a stale one */
    struct stat sb;
    if(fstatat(ufd, "pending", &sb, AT_SYMLINK_NOFOLLOW) != 0 && fstatat(ufd, "pending.old", &sb, AT_SYMLINK_NOFOLLOW) == 0) renameat(ufd, "pending.old", ufd, "pending");
    else rm_at(ufd, "pending.old", 0);
    rm_at(ufd, "incoming", 0);
    if(mkdirat(ufd, "incoming", 0700) != 0) return fin(ufd, -1, OTA_E_IO);
    ifd = fd_hi(openat(ufd, "incoming", O_RDONLY | O_DIRECTORY | O_NOFOLLOW));        /* NOT close-on-exec: wget/the verifier reach it as /proc/self/fd/N */
    if(ifd < 0) return fin(ufd, -1, OTA_E_IO);
    char ipath[48]; snprintf(ipath, sizeof ipath, "/proc/self/fd/%d", ifd);

    long long base = 0, ui_size = 0;
    for(int i = 0; i < OTA_NFILES; i++){
        long long cap = OTA_FILES[i].cap;
        if(i == OTA_UI_IDX){
            char mt[8192 + 1], sz[16];                                         /* the manifest is downloaded but NOT trusted yet: size only bounds work */
            if(read_at(ifd, OTA_FILES[OTA_MANIFEST_IDX].local, mt, sizeof mt) < 0 || sys_kv(mt, "size", sz, sizeof sz) != 0 || !all_digits(sz, 9)) return fin(ufd, ifd, OTA_E_BAD);
            ui_size = atoll(sz);
            if(ui_size <= 0 || ui_size > OTA_MAX_UI) return fin(ufd, ifd, OTA_E_BAD);
            int64_t need = 2 * ui_size + OTA_MARGIN;                            /* boot copies pending/mq_ui next to the live one */
            fr = ota_free_bytes(p->data_root);
            if(fr < need){ pg->need_free = need; return fin(ufd, ifd, OTA_E_SPACE); }
            atomic_store(&pg->total, (long)(base + ui_size));
            cap = ui_size;
        }
        int rc = dl_file(p, tag, OTA_FILES[i].asset, ifd, OTA_FILES[i].local, cap, pg, base);
        if(rc != OTA_OK) return fin(ufd, ifd, rc);
        if(fstatat(ifd, OTA_FILES[i].local, &sb, AT_SYMLINK_NOFOLLOW) == 0) base += (long long)sb.st_size;
    }

    /* verify BEFORE staging, with the RO root key and floors */
    atomic_store(&pg->phase, 2);
    if(atomic_load(&pg->cancel)) return fin(ufd, ifd, OTA_E_CANCEL);
    char ui[80], out[512]; size_t got; int capped;
    snprintf(ui, sizeof ui, "%s/mq_ui", ipath);
    char *vargv[] = { "/bin/sh", (char *)p->verifier, (char *)p->rootpub, kep, uep, ipath, ui, NULL };
    int vrc = run_capture(vargv, 0, 1, out, sizeof out, &got, &capped, 180000);
    if(vrc != 0 || capped){ snprintf(g_why, sizeof g_why, "verifier exit %d%s: %s", vrc, capped ? " (output cut)" : "", out);
                            return fin(ufd, ifd, vrc < 0 ? OTA_E_RUN : (vrc == 1 && !capped) ? reject_class(out) : OTA_E_BAD); }   /* couldn't run != damaged */
    char ver[40], ep[16];
    if(strncmp(out, "OK ", 3) != 0 || tok(out, "version", ver, sizeof ver) != 0 || tok(out, "epoch", ep, sizeof ep) != 0 || !all_digits(ep, 18)) return fin(ufd, ifd, OTA_E_BAD);
    if(ota_ver_parse(ver, 0, tv) != 0) return fin(ufd, ifd, OTA_E_BAD);                  /* signed version must be strict N.N.N too */
    if(ota_ver_cmp(running, 0, ver, 0) != -1) return fin(ufd, ifd, OTA_E_OLDER);         /* a validly signed OLD bundle replayed under a new tag */
    if(ota_ver_cmp(tag, 1, ver, 0) != 0) return fin(ufd, ifd, OTA_E_BAD);                /* signed version must be the release's */

    /* commit: ready last, fsync everything (errors are failures), keep the old pending until the new one is durable */
    if(atomic_load(&pg->cancel)) return fin(ufd, ifd, OTA_E_CANCEL);
    atomic_store(&pg->phase, 3);
    int wfd = openat(ifd, "ready.tmp", O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    if(wfd < 0) return fin(ufd, ifd, OTA_E_IO);
    char rt[80]; int rl = snprintf(rt, sizeof rt, "version=%s\nepoch=%s\n", ver, ep);
    int okw = write(wfd, rt, (size_t)rl) == rl && fsync(wfd) == 0;
    okw = (close(wfd) == 0) && okw;
    if(!okw || renameat(ifd, "ready.tmp", ifd, "ready") != 0) return fin(ufd, ifd, OTA_E_IO);
    for(int i = 0; i < OTA_NFILES; i++) if(fsync_at(ifd, OTA_FILES[i].local) != 0) return fin(ufd, ifd, OTA_E_IO);
    if(fsync_at(ifd, "ready") != 0 || fsync(ifd) != 0) return fin(ufd, ifd, OTA_E_IO);
    int had = fstatat(ufd, "pending", &sb, AT_SYMLINK_NOFOLLOW) == 0;
    if(had && renameat(ufd, "pending", ufd, "pending.old") != 0) return fin(ufd, ifd, OTA_E_IO);
    if(renameat(ufd, "incoming", ufd, "pending") != 0){
        if(had) renameat(ufd, "pending.old", ufd, "pending");
        return fin(ufd, ifd, OTA_E_IO);
    }
    if(fsync(ufd) != 0){                                                        /* not durable: put things back exactly as they were */
        renameat(ufd, "pending", ufd, "incoming");
        if(had) renameat(ufd, "pending.old", ufd, "pending");
        return fin(ufd, ifd, OTA_E_IO);
    }
    if(had){ rm_at(ufd, "pending.old", 0); fsync(ufd); }                        /* the old bundle goes only now (best effort: a leftover is swept next time) */
    snprintf(pg->version, sizeof pg->version, "%s", ver);
    (void)path;
    return fin(ufd, ifd, OTA_OK);
}

/* ---------------------------------------------------------------- stage a signed bundle from a local folder (fork)
 * The same checks and commit as ota_stage, but the six files are COPIED from <src> (a folder on the SD card made by the
 * owner's signing script) instead of downloaded. No release tag: the signed epoch (boot floor: accepted + 1) is the only
 * ordering, so any newer signed build - including a re-test of the same version number - can be staged. Never installs
 * anything: boot verifies again and runs it as a trial (auto rollback if it never proves healthy). */
static int copy_in(const char *src, const char *name, int ifd, const char *local, long long cap, ota_prog_t *pg, long long base){
    char path[400]; snprintf(path, sizeof path, "%s/%s", src, name);
    int in = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if(in < 0){ why("can't open %s (errno %d)", path, errno); return OTA_E_BAD; }
    struct stat st;
    if(fstat(in, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0 || (cap > 0 && st.st_size > cap)){ why("%s: empty, not a file or too big (%d)", path, (int)st.st_size); close(in); return OTA_E_BAD; }
    int out = openat(ifd, local, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if(out < 0){ close(in); return OTA_E_IO; }
    static char buf[64 * 1024]; long long done = 0; int rc = OTA_OK;
    for(;;){
        if(atomic_load(&pg->cancel)){ rc = OTA_E_CANCEL; break; }
        ssize_t n = read(in, buf, sizeof buf);
        if(n == 0) break;
        if(n < 0){ if(errno == EINTR) continue; rc = OTA_E_IO; break; }
        for(ssize_t w = 0; w < n; ){ ssize_t k = write(out, buf + w, (size_t)(n - w)); if(k < 0){ if(errno == EINTR) continue; rc = OTA_E_IO; break; } w += k; }
        if(rc != OTA_OK) break;
        done += n; atomic_store(&pg->done, (long)(base + done));
        if(cap > 0 && done > cap){ rc = OTA_E_BAD; break; }
    }
    close(in);
    if(close(out) != 0 && rc == OTA_OK) rc = OTA_E_IO;
    if(rc == OTA_OK && done != (long long)st.st_size) rc = OTA_E_IO;     /* the file changed while it was read */
    return rc;
}
int ota_stage_dir(const ota_paths_t *p, const char *src, ota_prog_t *pg){
    char kep[16], uep[16];
    atomic_store(&pg->done, 0); atomic_store(&pg->total, 0); atomic_store(&pg->phase, 1); pg->need_free = 0; pg->version[0] = 0;
    if(!ota_supported(p)) return OTA_E_UNSUPPORTED;                            /* no baked root key: this install can't verify anything */
    if(read_epochs(p, kep, uep, sizeof kep) != 0) return OTA_E_UNSUPPORTED;
    int64_t fr = ota_free_bytes(p->data_root);
    if(fr < OTA_MARGIN){ pg->need_free = OTA_MARGIN; return OTA_E_SPACE; }
    int ufd = open_updates(p), ifd = -1;
    if(ufd < 0) return OTA_E_IO;
    char acc[128], av[16];
    if(read_at(ufd, "accepted", acc, sizeof acc) > 0 && sys_kv(acc, "epoch", av, sizeof av) == 0 && all_digits(av, 9) && atol(av) + 1 > atol(uep))
        snprintf(uep, sizeof uep, "%ld", atol(av) + 1);
    struct stat sb;
    if(fstatat(ufd, "pending", &sb, AT_SYMLINK_NOFOLLOW) != 0 && fstatat(ufd, "pending.old", &sb, AT_SYMLINK_NOFOLLOW) == 0) renameat(ufd, "pending.old", ufd, "pending");
    else rm_at(ufd, "pending.old", 0);
    rm_at(ufd, "incoming", 0);
    if(mkdirat(ufd, "incoming", 0700) != 0) return fin(ufd, -1, OTA_E_IO);
    ifd = fd_hi(openat(ufd, "incoming", O_RDONLY | O_DIRECTORY | O_NOFOLLOW));        /* NOT close-on-exec: the verifier reaches it as /proc/self/fd/N */
    if(ifd < 0) return fin(ufd, -1, OTA_E_IO);
    char ipath[48]; snprintf(ipath, sizeof ipath, "/proc/self/fd/%d", ifd);

    long long base = 0;
    for(int i = 0; i < OTA_NFILES; i++){
        long long cap = OTA_FILES[i].cap;
        if(i == OTA_UI_IDX){
            char mt[8192 + 1], sz[16];
            if(read_at(ifd, OTA_FILES[OTA_MANIFEST_IDX].local, mt, sizeof mt) < 0 || sys_kv(mt, "size", sz, sizeof sz) != 0 || !all_digits(sz, 9)) return fin(ufd, ifd, OTA_E_BAD);
            long long ui_size = atoll(sz);
            if(ui_size <= 0 || ui_size > OTA_MAX_UI) return fin(ufd, ifd, OTA_E_BAD);
            int64_t need = 2 * ui_size + OTA_MARGIN;
            fr = ota_free_bytes(p->data_root);
            if(fr < need){ pg->need_free = need; return fin(ufd, ifd, OTA_E_SPACE); }
            atomic_store(&pg->total, (long)(base + ui_size));
            cap = ui_size;
        }
        int rc = copy_in(src, OTA_FILES[i].local, ifd, OTA_FILES[i].local, cap, pg, base);
        if(rc != OTA_OK) return fin(ufd, ifd, rc);
        if(fstatat(ifd, OTA_FILES[i].local, &sb, AT_SYMLINK_NOFOLLOW) == 0) base += (long long)sb.st_size;
    }

    atomic_store(&pg->phase, 2);
    if(atomic_load(&pg->cancel)) return fin(ufd, ifd, OTA_E_CANCEL);
    char ui[80], out[512]; size_t got; int capped;
    snprintf(ui, sizeof ui, "%s/mq_ui", ipath);
    char *vargv[] = { "/bin/sh", (char *)p->verifier, (char *)p->rootpub, kep, uep, ipath, ui, NULL };
    int vrc = run_capture(vargv, 0, 1, out, sizeof out, &got, &capped, 180000);
    if(vrc != 0 || capped){ snprintf(g_why, sizeof g_why, "verifier exit %d%s: %s", vrc, capped ? " (output cut)" : "", out);
                            return fin(ufd, ifd, vrc < 0 ? OTA_E_RUN : (vrc == 1 && !capped) ? reject_class(out) : OTA_E_BAD); }   /* couldn't run != damaged */
    char ver[40], ep[16];
    if(strncmp(out, "OK ", 3) != 0 || tok(out, "version", ver, sizeof ver) != 0 || tok(out, "epoch", ep, sizeof ep) != 0 || !all_digits(ep, 18)) return fin(ufd, ifd, OTA_E_BAD);

    if(atomic_load(&pg->cancel)) return fin(ufd, ifd, OTA_E_CANCEL);
    atomic_store(&pg->phase, 3);
    int wfd = openat(ifd, "ready.tmp", O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    if(wfd < 0) return fin(ufd, ifd, OTA_E_IO);
    char rt[80]; int rl = snprintf(rt, sizeof rt, "version=%s\nepoch=%s\n", ver, ep);
    int okw = write(wfd, rt, (size_t)rl) == rl && fsync(wfd) == 0;
    okw = (close(wfd) == 0) && okw;
    if(!okw || renameat(ifd, "ready.tmp", ifd, "ready") != 0) return fin(ufd, ifd, OTA_E_IO);
    for(int i = 0; i < OTA_NFILES; i++) if(fsync_at(ifd, OTA_FILES[i].local) != 0) return fin(ufd, ifd, OTA_E_IO);
    if(fsync_at(ifd, "ready") != 0 || fsync(ifd) != 0) return fin(ufd, ifd, OTA_E_IO);
    int had = fstatat(ufd, "pending", &sb, AT_SYMLINK_NOFOLLOW) == 0;
    if(had && renameat(ufd, "pending", ufd, "pending.old") != 0) return fin(ufd, ifd, OTA_E_IO);
    if(renameat(ufd, "incoming", ufd, "pending") != 0){
        if(had) renameat(ufd, "pending.old", ufd, "pending");
        return fin(ufd, ifd, OTA_E_IO);
    }
    if(fsync(ufd) != 0){
        renameat(ufd, "pending", ufd, "incoming");
        if(had) renameat(ufd, "pending.old", ufd, "pending");
        return fin(ufd, ifd, OTA_E_IO);
    }
    if(had){ rm_at(ufd, "pending.old", 0); fsync(ufd); }
    snprintf(pg->version, sizeof pg->version, "%s", ver);
    return fin(ufd, ifd, OTA_OK);
}

int ota_allowed(void){ return DISKOS_FORK_OTA_ENABLED && cfg_get_int("ota_allow", 1) != 0; }

/* Drop a staged bundle the user no longer wants. Only pending/ and its aside copy pending.old/ (which recovery would
 * otherwise rename back to pending/) are removed, by name relative to the updates directory fd, never following links:
 * active/, good/, good.prev/, accepted, rejected, trial* and the running build are not touched. pending.old goes first so
 * a power cut in between leaves a pending/ that can simply be discarded again. */
int ota_discard_pending(const ota_paths_t *p){
    int ufd = open_updates(p);
    if(ufd < 0) return 0;
    rm_at(ufd, "pending.old", 0);
    rm_at(ufd, "pending", 0);
    int ok = fsync(ufd) == 0;
    struct stat sb;
    if(fstatat(ufd, "pending", &sb, AT_SYMLINK_NOFOLLOW) == 0 || fstatat(ufd, "pending.old", &sb, AT_SYMLINK_NOFOLLOW) == 0) ok = 0;
    close(ufd);
    return ok;
}

int ota_pending(const ota_paths_t *p, char *ver, size_t n){
    char path[300], t[128];
    snprintf(path, sizeof path, "%s/pending/ready", p->updates);
    if(read_file(path, t, sizeof t) <= 0) return 0;
    if(sys_kv(t, "version", ver, n) != 0) return 0;
    return 1;
}

static int hex64(const char *s){ if(strlen(s) != 64) return 0; for(int i = 0; i < 64; i++) if(!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f'))) return 0; return 1; }
/* Trial boot = the launcher's intent (/tmp/.diskos_run) AND its statement of what it really started (/tmp/.diskos_launched)
 * both say BUILD=trial with the same 64-hex SHA256. No hashing here (cheap enough for the UI thread). */
int ota_trial_version(const ota_paths_t *p, char *ver, size_t n){
    char t[256], u[256], b1[16], b2[16], s1[80], s2[80];
    if(read_file(p->run_file, t, sizeof t) <= 0 || read_file(p->launched_file, u, sizeof u) <= 0) return 0;
    if(sys_kv(t, "BUILD", b1, sizeof b1) != 0 || strcmp(b1, "trial") != 0) return 0;
    if(sys_kv(u, "BUILD", b2, sizeof b2) != 0 || strcmp(b2, "trial") != 0) return 0;
    if(sys_kv(t, "SHA256", s1, sizeof s1) != 0 || sys_kv(u, "SHA256", s2, sizeof s2) != 0) return 0;
    if(!hex64(s1) || strcmp(s1, s2) != 0) return 0;
    if(ver){ if(sys_kv(t, "VERSION", ver, n) != 0) snprintf(ver, n, "%s", "update"); }
    return 1;
}
/* the strict form: as above, and the executable that is RUNNING hashes to that SHA256 (a fallback/flashed UI never vouches
 * for a trial it did not run, even with identical-looking run files). Fills sha. */
static int trial_is_me(const ota_paths_t *p, char sha[80]){
    char t[256], self[65];
    if(!ota_trial_version(p, NULL, 0)) return 0;
    if(read_file(p->run_file, t, sizeof t) <= 0 || sys_kv(t, "SHA256", sha, 80) != 0) return 0;
    if(sha256_file(p->self_exe, self) != 0 || strcmp(self, sha) != 0) return 0;
    return 1;
}
int ota_pcm_running(const ota_paths_t *p){
    glob_t g; int found = 0;
    if(glob(p->pcm_glob, 0, NULL, &g) != 0) return 0;
    for(size_t i = 0; i < g.gl_pathc && !found; i++){
        char t[512];
        if(read_file(g.gl_pathv[i], t, sizeof t) > 0 && strstr(t, "state: RUNNING")) found = 1;
    }
    globfree(&g);
    return found;
}
static int write_marker(const ota_paths_t *p, const char *name, const char *sha){          /* SHA256=<hex>\n, tmp + fsync + rename + dir fsync */
    char tmp[300], dst[300], b[80];
    snprintf(tmp, sizeof tmp, "%s/%s.tmp", p->updates, name);
    snprintf(dst, sizeof dst, "%s/%s", p->updates, name);
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0600);
    if(fd < 0) return 0;
    int l = snprintf(b, sizeof b, "SHA256=%s\n", sha);
    int ok = write(fd, b, (size_t)l) == l && fsync(fd) == 0;
    ok = (close(fd) == 0) && ok;
    if(!ok || rename(tmp, dst) != 0){ unlink(tmp); return 0; }
    int dfd = open(p->updates, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if(dfd < 0) return 0;
    ok = fsync(dfd) == 0;
    close(dfd);
    return ok;
}
/* Health proof for the trial build (contract 4a): only the RUNNING trial may write exactly SHA256=<its hash>. */
int ota_health_write(const ota_paths_t *p){
    char sha[80];
    return trial_is_me(p, sha) && write_marker(p, "healthy", sha);
}
/* "Go back": the user asked to drop the trial; boot honours <updates>/revert only when it names the trial's sha. */
int ota_revert_write(const ota_paths_t *p){
    char sha[80];
    return trial_is_me(p, sha) && write_marker(p, "revert", sha);
}
int ota_rollback_take(const ota_paths_t *p){
    char path[300], t[512];
    snprintf(path, sizeof path, "%s/rollback", p->updates);
    if(read_file(path, t, sizeof t) < 0) return 0;              /* absent, not a regular file, or oversize: not a marker */
    unlink(path);
    return 1;
}

const char *ota_err_text(int code){
    switch(code){
    case OTA_OK: return NULL;
    case OTA_E_CANCEL: return "Update cancelled";
    case OTA_E_NET: return "Couldn't reach GitHub. Check that Wi-Fi is connected.";
    case OTA_E_SPACE: return "Not enough space on the device";
    case OTA_E_UNSUPPORTED: return "Updates are not supported on this install";
    case OTA_E_OFF: return "Updates are turned off in Settings";
    case OTA_E_SIG: return "Update rejected - not signed by diskOS";
    case OTA_E_OLDER: return "Update refused - older than what you have";
    case OTA_E_BAD: return "Update rejected - the download is damaged";
    case OTA_E_RUN: return "Couldn't check the update. Restart the player and try again.";
    default: return "Couldn't save the update. Try again.";
    }
}
