/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
/* Power settings and the SD-safe shutdown (see power.h for the stock facts this is built on). */
#include "command_spawn.h"
#include "power.h"
#include <stdio.h>
#include <string.h>
#include "sqlite3.h"

/* ---- option tables and frames ------------------------------------------------------------------------------------ */
/* Stock's Idle poweroff menu (V2.57 mq_ui 0x46e298 jump table): Off, 5, 10, 30, 60, 90, 120 minutes, sent as seconds. */
static const int IDLE_SECS[POWER_IDLE_N] = { 0, 300, 600, 1800, 3600, 5400, 7200 };
static const char *const IDLE_LABEL[POWER_IDLE_N] = { "Off", "5 min", "10 min", "30 min", "60 min", "90 min", "120 min" };

int power_idle_secs(int idx){ return (idx >= 0 && idx < POWER_IDLE_N) ? IDLE_SECS[idx] : -1; }
int power_idle_idx_from_secs(int secs){
    for(int i = 0; i < POWER_IDLE_N; i++) if(IDLE_SECS[i] == secs) return i;
    return -1;
}
const char *power_idle_label(int idx){ return (idx >= 0 && idx < POWER_IDLE_N) ? IDLE_LABEL[idx] : "?"; }

int power_frame_sleep(char *buf, size_t n, int secs){
    if(secs < 0 || secs > 0xFFFF) return -1;
    return snprintf(buf, n, "0806000C%04X", (unsigned)secs) > 0 ? 0 : -1;
}
int power_frame_idle(char *buf, size_t n, int idx){
    int s = power_idle_secs(idx);
    if(s < 0) return -1;
    return snprintf(buf, n, "0664000C%04X", (unsigned)s) > 0 ? 0 : -1;
}
int power_frame_charge(char *buf, size_t n, int on){
    return snprintf(buf, n, "0808000C%04X", on ? 1u : 0u) > 0 ? 0 : -1;
}
int power_apply_charge_via(int (*send)(const char *), int on){
    char f[16];
    if(!send || power_frame_charge(f, sizeof f, on) != 0) return -1;
    return send(f) == 0 ? 0 : -1;
}

/* The player's battery task (0x4e2f9c): raw >= 551 is "too high", a leading '-' is "too low". */
int power_temp_state(int tenths_c){ return tenths_c >= 551 ? 1 : (tenths_c < 0 ? 2 : 0); }
const char *power_temp_msg(int state){
    return state == 1 ? "Temperature too high" : (state == 2 ? "Temperature too low" : NULL);
}

int power_read_sysconfig(const char *db_path, const char *column){
    const char *sql = NULL;
    if(column && !strcmp(column, "POWER_SAVE")) sql = "SELECT POWER_SAVE FROM SYSCONFIG WHERE ID=1;";
    else if(column && !strcmp(column, "CHARGE_PROTECT")) sql = "SELECT CHARGE_PROTECT FROM SYSCONFIG WHERE ID=1;";
    if(!sql || !db_path) return -1;
    sqlite3 *c = NULL; int v = -1;
    if(sqlite3_open_v2(db_path, &c, SQLITE_OPEN_READONLY, NULL) == SQLITE_OK){
        sqlite3_busy_timeout(c, 200);
        sqlite3_stmt *st = NULL;
        if(sqlite3_prepare_v2(c, sql, -1, &st, NULL) == SQLITE_OK){
            if(sqlite3_step(st) == SQLITE_ROW && sqlite3_column_type(st, 0) == SQLITE_INTEGER){
                int x = sqlite3_column_int(st, 0);
                if(x >= 0) v = x;
            }
            sqlite3_finalize(st);
        }
    }
    if(c) sqlite3_close(c);
    return v;
}

/* ---- idle power-off run by diskOS ---- */
static struct { uint32_t since; int started, fired; } g_idle;
void power_idle_reset(void){ memset(&g_idle, 0, sizeof g_idle); }
int power_idle_tick(uint32_t now, uint32_t limit_secs, int playing, uint32_t last_input, int (*request)(void)){
    if(limit_secs == 0 || playing){ g_idle.started = 0; g_idle.fired = 0; return 0; }
    if(!g_idle.started){ g_idle.started = 1; g_idle.fired = 0; g_idle.since = now; }
    if((int32_t)(last_input - g_idle.since) > 0 && (int32_t)(now - last_input) >= 0){ g_idle.since = last_input; g_idle.fired = 0; }   /* input restarts it */
    if(g_idle.fired || now - g_idle.since < limit_secs * 1000u) return 0;
    g_idle.fired = 1;
    if(request && request() != 0){ g_idle.since = now; g_idle.fired = 0; }   /* refused: try again after another period */
    return 1;
}
void power_takeover_reset(pw_take_t *t){ t->sent = 0; }
int power_takeover_step(pw_take_t *t, int (*read_secs)(void), int (*send)(const char *), int *stock_secs){
    if(!t || !read_secs || !send) return -1;
    int s = read_secs();
    if(t->sent){
        if(s == 0) return 1;              /* the player's setter ran: confirmed off */
        if(s > 0) t->sent = 0;            /* still on: send again below */
        else return 0;                    /* unreadable: keep waiting */
    }
    int prev = stock_secs ? *stock_secs : 0;
    if(stock_secs && s > 0) *stock_secs = s;   /* remember stock's value for a later restore; diskOS's own setting is untouched */
    if(send("0664000C0000") != 0){ if(stock_secs) *stock_secs = prev; return -1; }
    t->sent = 1;
    return 0;
}

/* ---- sleep timer expiry ------------------------------------------------------------------------------------------ */
pw_sleep_act_t power_sleep_decide(uint32_t armed_ms, uint32_t elapsed_ms, int guarded, int playing, int action){
    if(!armed_ms || elapsed_ms < armed_ms) return PW_SLEEP_WAIT;
    if(action == POWER_SLEEP_SHUTDOWN) return PW_SLEEP_SHUTDOWN;   /* not a toggle: the seek guard does not apply */
    if(guarded) return PW_SLEEP_WAIT;
    return playing ? PW_SLEEP_PAUSE : PW_SLEEP_DISARM;
}

/* ---- SD-safe shutdown -------------------------------------------------------------------------------------------- */
/* Same order as the Restart action: close SD admission, wait for every admitted writer, a full `sync` that must
 * succeed - and only then tell the player to power off. Any failure reopens the card and says so; nothing is powered off
 * after a failed step. The player's own power-off routine kills mq_ui and calls `poweroff -f` with no sync of its own, so
 * this sequence is what makes the card safe. */
static struct { power_ops_t ops; pw_phase_t phase; uint32_t t0; int cancel_sent; } g_pw;

static uint32_t pw_elapsed(void){ return g_pw.ops.now_ms(g_pw.ops.ctx) - g_pw.t0; }
static void pw_fail(const char *msg){
    if(g_pw.phase == PW_HANDOFF){
        /* Take the player's timer back. 0806 does not persist and has no read-back, and a queued frame proves nothing, so
         * the cancel can never be confirmed: keep the card CLOSED (a stock power-off may still be pending) until the player
         * has restarted or the Disc reboots. */
        g_pw.cancel_sent = g_pw.ops.send(g_pw.ops.ctx, "0806000C0000") == 0;
        g_pw.phase = PW_PROTECTED; g_pw.t0 = g_pw.ops.now_ms(g_pw.ops.ctx);
        g_pw.ops.toast(g_pw.ops.ctx, "Shutdown didn't complete - restart the Disc to use the card");
        return;
    }
    g_pw.ops.quiesce_end(g_pw.ops.ctx);
    g_pw.phase = PW_OFF;
    g_pw.ops.toast(g_pw.ops.ctx, msg);
}
int power_shutdown_begin(const power_ops_t *o){
    if(g_pw.phase != PW_OFF) return -1;
    if(!o || !o->quiesce_begin || !o->drained || !o->quiesce_end || !o->sync_start || !o->sync_poll || !o->sync_kill ||
       !o->send || !o->now_ms || !o->toast) return -1;
    g_pw.ops = *o;
    g_pw.ops.quiesce_begin(g_pw.ops.ctx);
    g_pw.phase = PW_DRAIN;
    g_pw.t0 = g_pw.ops.now_ms(g_pw.ops.ctx);
    g_pw.ops.toast(g_pw.ops.ctx, "Shutting down...");
    return 0;
}
pw_phase_t power_shutdown_phase(void){ return g_pw.phase; }
void power_shutdown_new_generation(void){
    if(g_pw.phase != PW_PROTECTED) return;
    g_pw.ops.quiesce_end(g_pw.ops.ctx);
    g_pw.phase = PW_OFF;
}
pw_phase_t power_shutdown_tick(void){
    switch(g_pw.phase){
    case PW_OFF: break;
    case PW_DRAIN:
        if(g_pw.ops.drained(g_pw.ops.ctx)){
            if(g_pw.ops.sync_start(g_pw.ops.ctx) != 0){ pw_fail("Couldn't shut down - try again"); break; }
            g_pw.phase = PW_SYNC; g_pw.t0 = g_pw.ops.now_ms(g_pw.ops.ctx);
        } else if(pw_elapsed() >= POWER_DRAIN_MS) pw_fail("Couldn't shut down - card busy, it reopens when free");
        break;
    case PW_SYNC: {
        int r = g_pw.ops.sync_poll(g_pw.ops.ctx);
        if(r < 0){ pw_fail("Couldn't shut down - card flush failed"); break; }
        if(r == 1){
            /* 0806 is the stock sleep timer: 1 s should make the player run its own power-off routine within about 2 s.
             * UNVERIFIED: the player resets that counter every second while a separate state word equals 1
             * (mq_player_257.dis 0x4f4558-0x4f4560, 0x4f46a4); its meaning is not identified. The hand-off timeout below
             * covers it. */
            if(g_pw.ops.send(g_pw.ops.ctx, "0806000C0001") != 0){ pw_fail("Couldn't shut down - try again"); break; }
            g_pw.phase = PW_HANDOFF; g_pw.t0 = g_pw.ops.now_ms(g_pw.ops.ctx);
        } else if(pw_elapsed() >= POWER_SYNC_MS){ g_pw.ops.sync_kill(g_pw.ops.ctx); pw_fail("Couldn't shut down - card busy, try again"); }
        break; }
    case PW_PROTECTED:
        if(!g_pw.cancel_sent && pw_elapsed() >= POWER_CANCEL_RETRY_MS){   /* keep trying to queue the cancel; never reopens */
            g_pw.cancel_sent = g_pw.ops.send(g_pw.ops.ctx, "0806000C0000") == 0;
            g_pw.t0 = g_pw.ops.now_ms(g_pw.ops.ctx);
        }
        break;
    case PW_HANDOFF:
        if(pw_elapsed() >= POWER_HANDOFF_MS) pw_fail("Couldn't shut down - try again");   /* still running: the player did not power off */
        break;
    }
    return g_pw.phase;
}

#ifndef POWER_CORE_ONLY
#include "screens.h"
#include "config.h"
#include "fwcaps.h"
#include "ipc.h"
#include "art.h"
#include "scanner.h"
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

void ui_sd_quiesce_begin(void); int ui_sd_quiesce_drained(void); void ui_sd_quiesce_end(void);   /* main.c */

#define POWER_SYSCONFIG "/usr/data/fiio/db/sysconfig.db"

int power_supported(void){ return fw_os_ver() == 257; }
int power_apply_charge(int on){ return power_supported() ? power_apply_charge_via(ipc_send_cmd, on) : -1; }

static uint32_t g_key_ms; static int g_have_key;   /* physical-key input the main loop's touch timestamp misses */
static pw_take_t g_take; static int g_take_ok; static unsigned g_take_gen; static int g_take_gen_set;
static int take_read(void){ return power_read_sysconfig(POWER_SYSCONFIG, "POWER_SAVE"); }
/* diskOS owns idle power-off (see power.h): the player's own must be off, CONFIRMED by a POWER_SAVE re-read, before
 * diskOS's idle timer may run. Retried by power_tick until confirmed, and redone after every player reconnect (new ipc
 * generation). Stock's value is only recorded (cfg stock_power_save, for a restore); it never becomes the diskOS setting. */
static void idle_takeover(void){
    unsigned gen = ipc_generation();
    if(!g_take_gen_set || gen != g_take_gen){ g_take_gen = gen; g_take_gen_set = 1; power_takeover_reset(&g_take); g_take_ok = 0; }
    if(g_take_ok) return;
    int sps = cfg_get_int("stock_power_save", 0), was = sps;
    int r = power_takeover_step(&g_take, take_read, ipc_send_cmd, &sps);
    if(sps != was) cfg_set_int("stock_power_save", sps);
    if(r == 1) g_take_ok = 1;
}
static int idle_request(void){ return power_shutdown_request(); }
void power_note_input(void){ g_key_ms = lv_tick_get(); g_have_key = 1; }
void power_startup_mirror(void){
    if(!power_supported()) return;
    idle_takeover();
    int c = power_read_sysconfig(POWER_SYSCONFIG, "CHARGE_PROTECT");
    if(c >= 0){ c = c ? 1 : 0; if(c != cfg_get_int("charge_protect", 0)) cfg_set_int("charge_protect", c); }
}

static pid_t g_sync_pid;
static void real_quiesce_begin(void *c){ (void)c; ui_sd_quiesce_begin(); scanner_abort(); art_kill_all(); }
static int  real_drained(void *c){ (void)c; return ui_sd_quiesce_drained(); }
static void real_quiesce_end(void *c){ (void)c; ui_sd_quiesce_end(); }
static int  real_sync_start(void *c){
    (void)c;
    pid_t p = 0;                                   /* posix_spawn, not fork(): a fork of the big UI can fail for memory */
    char *argv[] = { "sync", NULL };
    if(command_spawn(&p, argv) != 0 || p <= 0) return -1;
    g_sync_pid = p; return 0;
}
static int  real_sync_poll(void *c){
    (void)c; int st = 0;
    pid_t w = waitpid(g_sync_pid, &st, WNOHANG);
    if(w == 0) return 0;
    g_sync_pid = 0;
    return (w > 0 && WIFEXITED(st) && WEXITSTATUS(st) == 0) ? 1 : -1;
}
static void real_sync_kill(void *c){
    (void)c;
    if(g_sync_pid > 0){ kill(-g_sync_pid, SIGKILL); kill(g_sync_pid, SIGKILL); waitpid(g_sync_pid, NULL, WNOHANG); }
    g_sync_pid = 0;
}
static int  real_send(void *c, const char *f){ (void)c; return ipc_send_cmd(f); }
static uint32_t real_now(void *c){ (void)c; return lv_tick_get(); }
static void real_toast(void *c, const char *m){
    (void)c;
    /* The full-screen overlay already reports startup. Failures must restore the UI
     * before presenting the state machine's error, including a protected card. */
    if(!strcmp(m, "Shutting down...")) return;
    ui_shutdown_hide();
    ui_toast(m);
}

int power_shutdown_request(void){
    if(!power_supported()){ ui_toast("Not available on this firmware"); return -1; }
    if(power_shutdown_phase() == PW_PROTECTED){ ui_toast("Shutdown didn't complete - restart the Disc to use the card"); return -1; }
    if(power_shutdown_phase() != PW_OFF) return -1;
    power_ops_t o = { real_quiesce_begin, real_drained, real_quiesce_end, real_sync_start, real_sync_poll, real_sync_kill,
                      real_send, real_now, real_toast, NULL };
    /* Manual, idle and sleep-timer shutdown all enter here. Flush the themed screen
     * to the panel before saving usage or stopping card work. */
    ui_shutdown_screen();
    usage_save();
    int result = power_shutdown_begin(&o);
    if(result != 0) ui_shutdown_hide();
    return result;
}

static int read_temp_tenths(int *out){
    FILE *f = fopen("/sys/class/power_supply/cw221X-bat/temp", "r");
    if(!f) return -1;
    int t = 0, ok = fscanf(f, "%d", &t) == 1;
    fclose(f);
    if(!ok) return -1;
    *out = t; return 0;
}
void power_tick(uint32_t last_input_ms, int playing){
    static uint32_t last; static int have_last, temp_state;
    power_shutdown_tick();
    { static unsigned pg; static int pg_set; unsigned g = ipc_generation();
      if(pg_set && g != pg) power_shutdown_new_generation();   /* the player restarted: a pending stock power-off is gone */
      pg = g; pg_set = 1; }
    uint32_t now = lv_tick_get();
    if(power_supported()){
        if(g_have_key && (int32_t)(g_key_ms - last_input_ms) > 0) last_input_ms = g_key_ms;
        if(power_shutdown_phase() == PW_OFF && g_take_ok)   /* never before the player's own idle power-off is confirmed off */
            power_idle_tick(now, (uint32_t)power_idle_secs(cfg_get_int("idle_off_idx", 0)), playing, last_input_ms, idle_request);
    }
    if(have_last && now - last < 5000u) return;
    last = now; have_last = 1;
    if(power_supported()) idle_takeover();
    int t;
    if(read_temp_tenths(&t) != 0) return;
    int s = power_temp_state(t);
    if(s != temp_state){ temp_state = s; const char *m = power_temp_msg(s); if(m) ui_toast(m); }
}
#endif
