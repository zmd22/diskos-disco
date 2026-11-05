/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "crashlog.h"
#include "version.h"
#include <errno.h>
#include <pthread.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>

#ifndef CRASHLOG_PATH
#define CRASHLOG_PATH "/usr/data/diskos_crash.log"
#endif
#define CRASHLOG_MAX (64 * 1024)                 /* larger: start again from empty (the newest lines matter) */

/* ---- inside the signal handler: only async-signal-safe calls (open/write/fstat/ftruncate/close, clock_gettime,
 * syscall, prctl), no stdio, no malloc; numbers are formatted by hand into a stack buffer ---- */
typedef struct { char b[400]; int n; } line_t;
static void put(line_t *l, const char *s){ while(*s && l->n < (int)sizeof l->b - 2) l->b[l->n++] = *s++; }
static void hex(line_t *l, unsigned long v){
    char t[20]; int k = 0;
    do { t[k++] = "0123456789abcdef"[v & 15]; v >>= 4; } while(v && k < 16);
    put(l, "0x"); while(k && l->n < (int)sizeof l->b - 2) l->b[l->n++] = t[--k];
}
static void dec(line_t *l, long v){
    char t[24]; int k = 0; int neg = v < 0; unsigned long u = neg ? (unsigned long)-v : (unsigned long)v;
    do { t[k++] = (char)('0' + u % 10); u /= 10; } while(u && k < 22);
    if(neg) put(l, "-");
    while(k && l->n < (int)sizeof l->b - 2) l->b[l->n++] = t[--k];
}
static void write_line(const line_t *l){
    int fd = open(CRASHLOG_PATH, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    if(fd >= 0){
        struct stat st;
        if(fstat(fd, &st) == 0 && st.st_size > CRASHLOG_MAX && ftruncate(fd, 0) == 0) {}
        ssize_t w = write(fd, l->b, (size_t)l->n); (void)w;
        fsync(fd); close(fd);
    }
    ssize_t w2 = write(2, l->b, (size_t)l->n); (void)w2;   /* and to stderr, wherever that goes */
}
static volatile sig_atomic_t g_in_handler;
static void on_crash(int sig, siginfo_t *si, void *ucv){
    if(g_in_handler) return;                              /* a crash while recording: just let the default act */
    g_in_handler = 1;
    unsigned long pc = 0, ra = 0, sp = 0;
    ucontext_t *uc = ucv;
#if defined(__mips__)
    pc = (unsigned long)uc->uc_mcontext.pc;
    ra = (unsigned long)uc->uc_mcontext.gregs[31];
    sp = (unsigned long)uc->uc_mcontext.gregs[29];
#elif defined(__x86_64__)
    pc = (unsigned long)uc->uc_mcontext.gregs[REG_RIP];
    sp = (unsigned long)uc->uc_mcontext.gregs[REG_RSP];
#else
    (void)uc;
#endif
    struct timespec up; clock_gettime(CLOCK_MONOTONIC, &up);
    char tname[17] = {0}; prctl(PR_GET_NAME, (unsigned long)tname, 0, 0, 0);
    line_t l = { .n = 0 };
    put(&l, "CRASH v="); put(&l, DISCO_VERSION);
    put(&l, " sig="); dec(&l, sig);
    put(&l, " code="); dec(&l, si ? si->si_code : 0);
    put(&l, " addr="); hex(&l, si ? (unsigned long)si->si_addr : 0);
    put(&l, " pc="); hex(&l, pc);
    put(&l, " ra="); hex(&l, ra);
    put(&l, " sp="); hex(&l, sp);
    put(&l, " tid="); dec(&l, (long)syscall(SYS_gettid));
    put(&l, " thread="); put(&l, tname);
    put(&l, " uptime="); dec(&l, (long)up.tv_sec);
    put(&l, "\n");
    write_line(&l);
    /* SA_RESETHAND has put the default action back: returning re-runs the faulting instruction, which now ends the
     * process exactly as before (abort() raises again by itself). The watchdog then does what it always did. */
}
static char g_altstack[16384];                            /* a crash from a stack overflow still gets recorded */
/* ---- stall reporter: a watchdog thread notes a main loop that has been stuck for STALL_MS, while it is stuck,
 * with the screen and the last thing the UI was asked to do. Async-signal-safe formatting only (no stdio, no
 * malloc): the stuck main thread may be holding the allocator's lock. */
#define STALL_MS 3000
static volatile long g_tick_ms;                           /* the main loop's last pass (monotonic ms), 0 = not yet */
static volatile int g_scr = -1;
static char g_where[96];
static volatile unsigned g_where_seq;
static long mono_ms(void);
void crashlog_where(const char *what){
    if(!what) return;
    size_t n = strlen(what); if(n > sizeof g_where - 1) n = sizeof g_where - 1;
    memcpy(g_where, what, n); g_where[n] = 0; g_where_seq++;
}
void crashlog_screen(int scr){ g_scr = scr; }
static void *stall_thread(void *arg){
    (void)arg;
    prctl(PR_SET_NAME, (unsigned long)"stallwatch", 0, 0, 0);
    long reported = 0;                                    /* the stall length last written for this stall */
    for(;;){
        struct timespec ts = { 0, 500 * 1000000L }; nanosleep(&ts, NULL);
        long last = g_tick_ms, now = mono_ms();
        if(!last){ continue; }
        long stuck = now - last;
        if(stuck < STALL_MS){ reported = 0; continue; }
        if(reported && stuck < reported * 3) continue;    /* 3 s, then 9 s, 27 s ... */
        reported = stuck;
        char where[sizeof g_where]; memcpy(where, g_where, sizeof where); where[sizeof where - 1] = 0;
        struct timespec up; clock_gettime(CLOCK_MONOTONIC, &up);
        line_t l = { .n = 0 };
        put(&l, "STALL v="); put(&l, DISCO_VERSION);
        put(&l, " stuck="); dec(&l, stuck); put(&l, "ms screen="); dec(&l, g_scr);
        put(&l, " after=\""); put(&l, where[0] ? where : "?"); put(&l, "\" uptime="); dec(&l, (long)up.tv_sec);
        put(&l, "\n");
        write_line(&l);
    }
    return NULL;
}
static void mem_read(long *avail, long *total, long *rss);
void crashlog_install(void){
    stack_t ss = { .ss_sp = g_altstack, .ss_size = sizeof g_altstack, .ss_flags = 0 };
    sigaltstack(&ss, NULL);
    struct sigaction sa; memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = on_crash;
    sa.sa_flags = SA_SIGINFO | SA_RESETHAND | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    const int sigs[] = { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT };
    for(unsigned i = 0; i < sizeof sigs / sizeof sigs[0]; i++) sigaction(sigs[i], &sa, NULL);
    struct timespec up; clock_gettime(CLOCK_MONOTONIC, &up);
    long avail = 0, total = 0, rss = 0; mem_read(&avail, &total, &rss);
    crashlog_note("start v=%s uptime=%ld mem_free=%ldK of %ldK", DISCO_VERSION, (long)up.tv_sec, avail, total);
    pthread_t th; pthread_attr_t at; pthread_attr_init(&at); pthread_attr_setstacksize(&at, 64 * 1024);
    pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
    if(pthread_create(&th, &at, stall_thread, NULL) != 0) crashlog_note("stall reporter: no thread");
    pthread_attr_destroy(&at);
}
/* ---- diagnosis in the main loop (no behaviour change) ----
 * Freeze detector: the main loop normally comes round every few to 200 ms. A gap over FREEZE_MS means something held
 * it up; it gets a line (at most one a minute), so stalls before a crash show in the log.
 * Memory watch: every 5 s, MemAvailable; under LOWMEM_KB a line (at most one a minute) with Disco's own size, because
 * a kill for lack of memory can't be recorded by Disco itself. */
#define FREEZE_MS 1500
#define LOWMEM_KB 12288
static long mono_ms(void){ struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1000L + t.tv_nsec / 1000000L; }
static void mem_read(long *avail, long *total, long *rss){
    FILE *f = fopen("/proc/meminfo", "r"); char line[128]; long freek = -1, cached = 0, buffers = 0;
    if(f){
        while(fgets(line, sizeof line, f)){
            sscanf(line, "MemTotal: %ld", total); sscanf(line, "MemAvailable: %ld", avail);
            sscanf(line, "MemFree: %ld", &freek); sscanf(line, "Cached: %ld", &cached); sscanf(line, "Buffers: %ld", &buffers);
        }
        fclose(f);
    }
    if(*avail <= 0 && freek >= 0) *avail = freek + cached + buffers;   /* older kernels have no MemAvailable */
    f = fopen("/proc/self/statm", "r");
    if(f){ long size, res; if(fscanf(f, "%ld %ld", &size, &res) == 2) *rss = res * (sysconf(_SC_PAGESIZE) / 1024); fclose(f); }
}
void crashlog_tick(void){
    static long last, last_freeze_note = -60000, last_mem_check, last_mem_note = -60000;
    long now = mono_ms();
    g_tick_ms = now;                                        /* for the stall reporter */
    if(last && now - last > FREEZE_MS && now - last_freeze_note > 60000){
        crashlog_note("main loop stalled %ld ms", now - last); last_freeze_note = now;
    }
    last = now;
    if(now - last_mem_check >= 5000){
        last_mem_check = now;
        long avail = 0, total = 0, rss = 0; mem_read(&avail, &total, &rss);
        if(avail > 0 && avail < LOWMEM_KB && now - last_mem_note > 60000){
            crashlog_note("low memory: %ldK free of %ldK, Disco uses %ldK", avail, total, rss); last_mem_note = now;
        }
    }
}
void crashlog_note(const char *fmt, ...){
    char msg[300]; va_list ap; va_start(ap, fmt); vsnprintf(msg, sizeof msg, fmt, ap); va_end(ap);
    time_t now = time(NULL); struct tm tm; char ts[24] = "?";
    if(now > 1600000000 && localtime_r(&now, &tm)) strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", &tm);
    line_t l = { .n = 0 };
    put(&l, ts); put(&l, " "); put(&l, msg); put(&l, "\n");
    write_line(&l);
}
