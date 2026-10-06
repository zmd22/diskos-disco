/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Idle-CPU profiling (measurement build only: -DDISKOS_PROFILE). Every 60 s it writes one report to stderr: loop
 * wake-ups, how often the loop ran "busy" (5 ms cap), time inside lv_timer_handler and the rest of the loop, display
 * flushes and pixels, and every LVGL timer's call count + total time (callbacks are wrapped in a timing trampoline,
 * keyed by the original callback address - map it to a name with nm on the unstripped binary). */
#ifdef DISKOS_PROFILE
#include "prof.h"
#include "lvgl/lvgl.h"
#include "lvgl/src/misc/lv_timer_private.h"
#include "lvgl/src/core/lv_global.h"
#include "lvgl/src/misc/lv_anim_private.h"
#include <stdio.h>
#include <time.h>
static unsigned long long now_ns(void){ struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (unsigned long long)t.tv_sec * 1000000000ULL + (unsigned long long)t.tv_nsec; }
#define PT_MAX 96
static struct { lv_timer_t *t; lv_timer_cb_t orig; unsigned calls; unsigned long long ns; } pt[PT_MAX];
static int npt;
static unsigned long long g_t0, g_handler_ns, g_body_ns;
static unsigned g_loops, g_busy, g_bl[3], g_flushes, g_anim_loops; static unsigned long long g_px;
void prof_flush(int px){ g_flushes++; g_px += (unsigned long long)px; }
static void tramp(lv_timer_t *t){
    for(int i = 0; i < npt; i++) if(pt[i].t == t){
        lv_timer_cb_t cb = pt[i].orig; unsigned long long a = now_ns();
        cb(t);                                   /* may delete t: only the table entry is touched afterwards */
        pt[i].ns += now_ns() - a; pt[i].calls++;
        return;
    }
}
static void wrap_new_timers(void){
    for(lv_timer_t *t = lv_timer_get_next(NULL); t; t = lv_timer_get_next(t)){
        if(t->timer_cb == tramp) continue;
        int slot = -1;
        for(int i = 0; i < npt; i++) if(pt[i].t == t){ slot = i; break; }   /* address reused by a new timer */
        if(slot < 0){ if(npt >= PT_MAX) continue; slot = npt++; }
        pt[slot].t = t; pt[slot].orig = t->timer_cb;
        t->timer_cb = tramp;
    }
}
void prof_loop(int busy, int bl_state, int anims, unsigned long long handler_ns, unsigned long long body_ns){
    unsigned long long n = now_ns();
    if(!g_t0) g_t0 = n;
    wrap_new_timers();
    g_loops++; g_busy += busy ? 1 : 0; if(bl_state >= 0 && bl_state < 3) g_bl[bl_state]++; if(anims) g_anim_loops++;
    g_handler_ns += handler_ns; g_body_ns += body_ns;
    if(n - g_t0 < 60000000000ULL) return;
    fprintf(stderr, "PROF window=60s loops=%u busy=%u anim_loops=%u bl=%u/%u/%u handler_ms=%llu body_ms=%llu flushes=%u px=%llu\n",
            g_loops, g_busy, g_anim_loops, g_bl[0], g_bl[1], g_bl[2], g_handler_ns / 1000000, g_body_ns / 1000000, g_flushes, g_px);
    for(int i = 0; i < npt; i++) if(pt[i].calls)
        fprintf(stderr, "PROF timer cb=%p calls=%u ms=%llu\n", (void *)pt[i].orig, pt[i].calls, pt[i].ns / 1000000);
    for(lv_anim_t *an = lv_ll_get_head(&LV_GLOBAL_DEFAULT()->anim_state.anim_ll); an; an = lv_ll_get_next(&LV_GLOBAL_DEFAULT()->anim_state.anim_ll, an))
        fprintf(stderr, "PROF anim exec=%p custom=%p var=%p dur=%u repeat=%u\n", (void *)an->exec_cb, (void *)an->custom_exec_cb,
                an->var, (unsigned)an->duration, (unsigned)an->repeat_cnt);
    fflush(stderr);
    g_t0 = n; g_handler_ns = g_body_ns = g_px = 0; g_loops = g_busy = g_flushes = g_anim_loops = 0; g_bl[0] = g_bl[1] = g_bl[2] = 0;
    for(int i = 0; i < npt; i++){ pt[i].calls = 0; pt[i].ns = 0; }
}
#endif
