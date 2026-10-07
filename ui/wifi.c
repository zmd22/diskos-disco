/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "screens.h"
#include "braun.h"
#include "theme.h"
#include "curvelist.h"
#include <spawn.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>
#include <unistd.h>
#include "theme_kit.h"
#include "config.h"
#include "fwcaps.h"
#include "ipc.h"
#include <stdio.h>
#include <dirent.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

/* Wi-Fi settings (SCR_WIFI) + a details screen (SCR_WIFI_INFO).
 * Radio on/off reuses the stock /usr/bin/wifi_up.sh / wifi_down.sh (rfkill +
 * wpa_supplicant + udhcpc).  Scan/connect drive the running supplicant via
 * `wpa_cli -i wlan0`.  Connecting writes the network in and `save_config`s it
 * to /usr/data/wpa_supplicant.conf (update_config=1), so it persists.
 * Round-screen-aware: the top corners stay clear (only back chevron + centred
 * title); the on/off switch + scan button share one row in the wide centre
 * band.  The list itself carries state (connected ✓ row / "Wi-Fi is off" /
 * "Scanning" / "No networks") so there is no redundant status line.
 * NB: connecting to a DIFFERENT AP drops the laptop deploy link - recover over
 * serial if needed. */

#define WCLI "/usr/sbin/wpa_cli -i wlan0 "

static lv_obj_t *g_sw, *g_list;
static lv_obj_t *g_hring, *g_hglyph, *g_hname, *g_hsub, *g_hknob;   /* g_hknob: Braun's status knob */   /* the status ring + name + line at the top */
static curvelist_t g_wcl;                                  /* curved network rows */
static int g_cur_sig = -100;                               /* signal of the joined network (dBm), from the last scan */
static lv_timer_t *g_scan_timer;
static lv_timer_t *g_conn_timer;      /* non-NULL while a user connect is being polled */
static uint32_t    g_wifi_last_start = 0;   /* last wifi_up.sh launch (shared: toggle + keepalive) */
static int         g_sanitize_pending = 1;  /* sanitize saved nets once the supplicant is up (set on 1st run + each (re)launch) */
static unsigned    g_intent_epoch = 0;      /* bumped by every explicit user Wi-Fi choice (switch or QS tile) */
static char g_pending_ssid[64];   /* SSID awaiting a password from the keyboard */
static char g_cur_ssid[64];       /* currently-connected SSID (to mark the list) */
static lv_obj_t *g_info_list;     /* details screen */

static void start_scan(void);     /* fwd */

/* The refresh glyph INSIDE the "Scanning" message spins while a scan runs. It lives
 * in g_list, so it must be stopped before g_list is cleaned (else the anim references
 * a freed object). scan_stop() is called at the top of every g_list-clearing path. */
static lv_obj_t *g_scan_icon;
static void spin_anim_cb(void *o, int32_t v){ lv_obj_set_style_transform_rotation((lv_obj_t*)o, v, 0); }
static void scan_stop(void){
    if(g_scan_icon){ lv_anim_delete(g_scan_icon, spin_anim_cb); g_scan_icon = NULL; }
}

/* ---- tiny local copies of settings.c's header helpers ------------------- */

/* ---- shell helpers ------------------------------------------------------ */
/* Run cmd and return ALL of its output (malloc'd, NUL-terminated; caller frees), read to EOF, capped at
 * `max` bytes (a bigger output is cut there, which the row parser treats as a partial last row). */
static char *run_cap_all(const char *cmd, size_t max){
    FILE *p = popen(cmd, "r");
    if(!p) return NULL;
    size_t cap = 4096, n = 0; char *b = malloc(cap);
    if(!b){ pclose(p); return NULL; }
    for(;;){
        if(n + 1 >= cap){
            if(cap >= max) break;
            size_t nc = cap * 2 > max ? max : cap * 2; char *t = realloc(b, nc);
            if(!t) break;
            b = t; cap = nc;
        }
        size_t r = fread(b + n, 1, cap - 1 - n, p);
        if(r == 0) break;
        n += r;
    }
    b[n] = 0;
    pclose(p);
    return b;
}
static int run_cap_to(const char *cmd, char *out, int cap, int ms){
    out[0] = 0;
    int fd[2]; if(pipe(fd) != 0) return 0;
    posix_spawn_file_actions_t fa; posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, fd[1], 1);
    posix_spawn_file_actions_addclose(&fa, fd[0]);
    posix_spawnattr_t at; posix_spawnattr_init(&at);
    posix_spawnattr_setflags(&at, POSIX_SPAWN_SETPGROUP); posix_spawnattr_setpgroup(&at, 0);
    char *argv[] = { "sh", "-c", (char *)cmd, NULL };
    extern char **environ;
    pid_t pid; int rc = posix_spawn(&pid, "/bin/sh", &fa, &at, argv, environ);
    posix_spawn_file_actions_destroy(&fa); posix_spawnattr_destroy(&at);
    close(fd[1]);
    if(rc != 0){ close(fd[0]); return 0; }
    int n = 0, timed_out = 0; uint32_t t0 = lv_tick_get();
    for(;;){
        int left = ms - (int)lv_tick_elaps(t0);
        if(left <= 0){ timed_out = 1; break; }
        struct pollfd p = { .fd = fd[0], .events = POLLIN };
        int pr = poll(&p, 1, left);
        if(pr < 0){ if(errno == EINTR) continue; break; }
        if(pr == 0){ timed_out = 1; break; }
        ssize_t r = read(fd[0], out + n, (size_t)(cap - 1 - n));
        if(r <= 0) break;
        n += (int)r; if(n >= cap - 1) break;
    }
    close(fd[0]);
    if(timed_out){ kill(-pid, SIGKILL); fprintf(stderr, "wifi: timed out after %d ms: %s\n", ms, cmd); }
    waitpid(pid, NULL, 0);
    out[n] = 0;
    return timed_out ? 0 : n;
}
static int run_cap(const char *cmd, char *out, int cap){ return run_cap_to(cmd, out, cap, 4000); }

/* wpa_cli prints SSIDs (scan_results, list_networks, status) printf-encoded: \\ \" \n \r \t \e and \xNN
 * for every non-printable / non-ASCII byte. Decode to the real bytes into out[cap]. Returns the byte
 * count, or -1 if the SSID is longer than 32 bytes (the 802.11 limit), does not fit, has a malformed
 * escape, or holds a control byte / NUL (unusable as a label and unsafe in a wpa_cli command). */
static int wifi_ssid_decode(const char *in, int inlen, char *out, int cap){
    int o = 0;
    for(int i=0;i<inlen;i++){
        unsigned char c = (unsigned char)in[i], v = c;
        if(c == '\\'){
            if(++i >= inlen) return -1;
            char e = in[i];
            if(e == '\\' || e == '"') v = (unsigned char)e;
            else if(e == 'x'){
                int h = 0;
                for(int k=1;k<=2;k++){
                    if(i + k >= inlen) return -1;
                    char d = in[i+k]; int dv;
                    if(d >= '0' && d <= '9') dv = d - '0';
                    else if(d >= 'a' && d <= 'f') dv = d - 'a' + 10;
                    else if(d >= 'A' && d <= 'F') dv = d - 'A' + 10;
                    else return -1;
                    h = h * 16 + dv;
                }
                v = (unsigned char)h; i += 2;
            }
            else if(e == 'n') v = '\n';
            else if(e == 'r') v = '\r';
            else if(e == 't') v = '\t';
            else if(e == 'e') v = 27;
            else return -1;
        }
        if(v < 0x20 || v == 0x7f) return -1;
        if(o >= 32 || o >= cap - 1) return -1;
        out[o++] = (char)v;
    }
    out[o] = 0;
    return o;
}
/* does the list_networks ssid field fld[0..flen) name exactly the real SSID `ssid`? */
static int wifi_ssid_field_eq(const char *fld, int flen, const char *ssid){
    char d[64]; int n = wifi_ssid_decode(fld, flen, d, sizeof d);
    return n >= 0 && (size_t)n == strlen(ssid) && memcmp(d, ssid, (size_t)n) == 0;
}

static int wifi_status(char *ssid, int scap, char *ip, int icap){
    (void)scap; (void)icap;
    ssid[0] = 0; ip[0] = 0;
    char buf[2048];
    if(!run_cap(WCLI "status 2>/dev/null", buf, sizeof buf)) return 0;
    int connected = (strstr(buf, "wpa_state=COMPLETED") != NULL);
    char *l = buf;
    while(l && *l){
        if(!strncmp(l, "ssid=", 5)){
            char *e = strchr(l+5, '\n'); int n = e ? (int)(e - (l+5)) : (int)strlen(l+5);
            if(wifi_ssid_decode(l+5, n, ssid, 64) < 0) ssid[0] = 0;
        }
        else if(!strncmp(l, "ip_address=", 11)){ sscanf(l+11, "%31[^\n]", ip); }
        l = strchr(l, '\n'); if(l) l++;
    }
    return connected;
}

static int wifi_radio_on(void){
    char buf[256];
    run_cap("pidof wpa_supplicant 2>/dev/null", buf, sizeof buf);
    return buf[0] ? 1 : 0;
}

/* The radio as it really is (wpa_supplicant running), not the saved "wifi_on" intent: a /proc walk, no shell, cheap
 * enough for the Quick Settings tile's once-a-second paint. Both the tile and its tap use this. */
static int g_live_cached = -1; static uint32_t g_live_at;   /* the tile repaints every second: walk /proc at most every 1.5 s */
int wifi_radio_live(void){
    if(g_live_cached >= 0 && lv_tick_elaps(g_live_at) < 1500) return g_live_cached;
    DIR *d = opendir("/proc"); if(!d) return cfg_get_int("wifi_on", 1);
    struct dirent *e; int found = 0;
    while(!found && (e = readdir(d))){
        if(e->d_name[0] < '1' || e->d_name[0] > '9') continue;
        char p[64], comm[32] = ""; snprintf(p, sizeof p, "/proc/%.20s/comm", e->d_name);
        FILE *f = fopen(p, "r"); if(!f) continue;
        if(fgets(comm, sizeof comm, f) && !strncmp(comm, "wpa_supplicant", 14)) found = 1;
        fclose(f);
    }
    closedir(d);
    g_live_cached = found; g_live_at = lv_tick_get();
    return found;
}

/* ---- persistence / auto-reconnect hardening ----------------------------- */
/* Make the running supplicant's config writable, keep EVERY saved network a candidate
 * for auto-join (not just the last-selected), and drop stale per-network channel pins
 * (a network locked to a frequency the AP has since left never associates). Run on each
 * supplicant bring-up and after a user connect. */
/* wpa_cli only responds once the control interface is up - pidof can be true well before
 * that, so a ping (PONG) is the real "ready" signal. */
static int wpa_ready(void){
    char buf[64];
    run_cap(WCLI "ping 2>/dev/null", buf, sizeof buf);
    return strstr(buf, "PONG") != NULL;
}
/* returns 1 if it actually ran (ctrl interface ready), 0 if skipped so the caller keeps
 * g_sanitize_pending set until a live, responsive supplicant can be sanitized. */
static int wifi_sanitize_savenets(void){
    if(!wpa_ready()) return 0;                            /* supplicant not (yet) accepting wpa_cli */
    system(WCLI "set update_config 1 >/dev/null 2>&1");   /* else save_config silently fails */
    system(WCLI "enable_network all >/dev/null 2>&1");    /* any known net can auto-join */
    char buf[4096];
    run_cap(WCLI "list_networks 2>/dev/null", buf, sizeof buf);
    char *l = strchr(buf, '\n'); if(l) l++;               /* skip header row */
    while(l && *l){
        int nid;
        if(sscanf(l, "%d", &nid) == 1){
            char c[96];
            snprintf(c, sizeof c, WCLI "set_network %d frequency 0 >/dev/null 2>&1", nid);
            system(c);                                     /* clear stale channel lock */
        }
        l = strchr(l, '\n'); if(l) l++;
    }
    system(WCLI "save_config >/dev/null 2>&1");
    return 1;
}

/* Seed diskOS's own Wi-Fi on/off intent from stock SYSCONFIG.WIFI_STATUS the FIRST time
 * only; thereafter diskOS owns "wifi_on" (the toggle persists every change), so the
 * keepalive can distinguish "supplicant died" from "user turned Wi-Fi off". */
void wifi_init_intent(void){
    if(cfg_get_int("wifi_on", -1) >= 0) return;           /* already owned */
    char buf[32];
    run_cap("sqlite3 /usr/data/fiio/db/sysconfig.db \"SELECT WIFI_STATUS FROM SYSCONFIG WHERE ID=1\" 2>/dev/null",
            buf, sizeof buf);
    /* only persist a VALID read (0/1); a failed/empty query leaves it unseeded so it
     * retries next boot AND the keepalive's default (on) applies meanwhile - never
     * silently latch Wi-Fi off because sqlite hiccuped. */
    if(buf[0]=='0' || buf[0]=='1') cfg_set_int("wifi_on", buf[0]-'0');
}

/* Keepalive: whenever Wi-Fi should be on but wpa_supplicant isn't running (died, never
 * started, radio glitch) (re)start it, so a known network returning in range auto-joins.
 * Sanitize saved nets once the supplicant is up after each (re)launch - driven by a pending
 * flag (set on first run + every launch we initiate, cleared after one run), so it's
 * deterministic and idempotent regardless of pid reuse or a transient pidof miss. Enforce a
 * deliberate OFF against a late in-flight wifi_up. Self-rate-limited: safe to call each loop. */
/* ---- V2.57 radio reconciliation ---------------------------------------------------------------------
 * V2.57's player turns Wi-Fi off at boot when both radios were saved on (keeps Bluetooth, saves WIFI_STATUS=0).
 * diskOS keeps its own "wifi_on" intent, and the keepalive below would restart Wi-Fi at once, undoing stock's
 * decision and leaving both radios fighting. So on firmware with that arbitration, each player generation (boot or
 * player restart) holds the keepalive until the player has answered and settled, then accepts stock's decision
 * ONLY for its exact signature. An explicit user choice at any point ends the hold and always wins; a player that
 * never answers releases the hold unchanged after RECO_GIVEUP_MS, so Wi-Fi cannot get stuck off. */
#define RECO_SETTLE_MS 9000      /* same settle as the local-init handshake (past the ~7 s mode-control window) */
#define RECO_GIVEUP_MS 30000     /* silent player: release unchanged this long after arming */
#define RECO_WINDOW_MS 30000     /* answered player: keep re-checking this long after its first frame, since stock's
                                  * radio work can finish well after the settle (our own BT restore allows 28 s) */
#define RECO_PROBE_MS  1000      /* every probe (sqlite read, pidof) is killed at this bound: never blocks the UI */
static int      g_reco_hold = 0;           /* 1 = keepalive passive while this generation is being reconciled */
static unsigned g_reco_gen  = 0xFFFFFFFFu; /* generation armed */
static unsigned g_reco_rx0, g_reco_epoch0;
static uint32_t g_reco_armed_at, g_reco_first_rx_at, g_reco_probe_at, g_reco_eval_at;
static int      g_reco_seen_rx;

/* The decision, pure: 1 = adopt stock's "Wi-Fi off" into diskOS's intent. Every input must be a clean 0/1 read
 * (callers pass -1 for anything unknown, which never adopts). */
static int wifi_reco_decide(int db_wifi, int db_bt, int eff_wifi, int eff_bt, int want_wifi, int user_changed){
    return db_wifi == 0 && db_bt == 1 && eff_wifi == 0 && eff_bt == 1 && want_wifi == 1 && !user_changed;
}

/* Saved radio state from stock SYSCONFIG: 1 and fills both on a clean "d|d" read, else 0. Bounded. */
static int read_saved_radios(int *wifi, int *bt){
    char buf[32] = {0};
    ui_run_cap_bounded("sqlite3 /usr/data/fiio/db/sysconfig.db \"SELECT WIFI_STATUS||'|'||BT_STATUS FROM SYSCONFIG WHERE ID=1\" 2>/dev/null",
                       buf, sizeof buf, RECO_PROBE_MS);
    if((buf[0] == '0' || buf[0] == '1') && buf[1] == '|' && (buf[2] == '0' || buf[2] == '1') && (buf[3] == '\n' || buf[3] == 0)){
        *wifi = buf[0] - '0'; *bt = buf[2] - '0'; return 1;
    }
    return 0;
}
/* Wi-Fi rfkill soft state: 1 blocked, 0 unblocked, -1 unknown (no wlan switch / unreadable). */
static int wifi_rfkill_blocked(void){
    for(int i = 0; i < 12; i++){
        char p[64]; snprintf(p, sizeof p, "/sys/class/rfkill/rfkill%d/type", i);
        FILE *f = fopen(p, "r"); if(!f) continue;
        char t[16] = {0}; char *r = fgets(t, sizeof t, f); fclose(f);
        if(!r || strncmp(t, "wlan", 4) != 0) continue;
        snprintf(p, sizeof p, "/sys/class/rfkill/rfkill%d/soft", i);
        f = fopen(p, "r"); if(!f) return -1;
        int soft = -1; if(fscanf(f, "%d", &soft) != 1) soft = -1; fclose(f);
        return (soft == 0 || soft == 1) ? soft : -1;
    }
    return -1;
}
/* Effective Wi-Fi the way stock's wifi_down.sh leaves it: 0 = no supplicant AND radio rfkill-blocked, 1 = either one
 * says on, -1 = could not tell (probe failed / no rfkill switch). */
static int wifi_effective(void){
    char buf[64] = {0};
    int rc = ui_run_cap_bounded("pidof wpa_supplicant 2>/dev/null; echo done", buf, sizeof buf, RECO_PROBE_MS);
    if(rc <= 0 || !strstr(buf, "done")) return -1;              /* probe did not finish */
    int daemon = (buf[0] >= '0' && buf[0] <= '9');
    int blocked = wifi_rfkill_blocked();
    if(daemon || blocked == 0) return 1;
    return blocked == 1 ? 0 : -1;
}

static void wifi_reconcile_poll(void){
    if(!fw_has_radio_boot_arbitration()){ g_reco_hold = 0; return; }
    unsigned gen = ipc_generation(), rx = ipc_rx_frames();
    uint32_t now = lv_tick_get();
    if(gen != g_reco_gen){                          /* new player generation: arm */
        g_reco_gen = gen; g_reco_hold = 1; g_reco_rx0 = rx; g_reco_epoch0 = g_intent_epoch;
        g_reco_armed_at = now; g_reco_seen_rx = 0; g_reco_probe_at = now - 1000; g_reco_eval_at = now - RECO_PROBE_MS;
    }
    if(!g_reco_hold) return;
    if(g_intent_epoch != g_reco_epoch0){ g_reco_hold = 0; return; }        /* the user chose: their choice wins */
    if(cfg_get_int("wifi_on", 1) == 0){ g_reco_hold = 0; return; }         /* Wi-Fi intent already off: nothing to undo */
    if(!g_reco_seen_rx){
        if(rx == g_reco_rx0){
            if(now - g_reco_armed_at >= RECO_GIVEUP_MS){ g_reco_hold = 0; return; }   /* silent player: unchanged */
            if(now - g_reco_probe_at >= 1000){ g_reco_probe_at = now; ipc_send_probe("02020008"); }   /* solicit, 1/s */
            return;
        }
        g_reco_seen_rx = 1; g_reco_first_rx_at = now;
    }
    if(now - g_reco_first_rx_at < RECO_SETTLE_MS) return;
    if(now - g_reco_eval_at < 1000) return;         /* re-check about once a second */
    g_reco_eval_at = now;
    int dbw = -1, dbb = -1;
    if(!read_saved_radios(&dbw, &dbb)) dbw = dbb = -1;                       /* unknown: never adopts */
    int effw = wifi_effective(), effb = bt_radio_on();
    /* the probes themselves take time (each is bounded, not instant): judge the window on the clock AFTER them, so a
     * check that started just inside it can never adopt once it has closed */
    if(lv_tick_get() - g_reco_first_rx_at >= RECO_WINDOW_MS){ g_reco_hold = 0; return; }
    if(g_intent_epoch != g_reco_epoch0){ g_reco_hold = 0; return; }        /* the user chose while we probed */
    if(wifi_reco_decide(dbw, dbb, effw, effb, cfg_get_int("wifi_on", 1), 0)){
        cfg_set_int("wifi_on", 0);
        ui_toast("Wi-Fi off: Bluetooth was on");
        fprintf(stderr, "radio reconcile (gen %u): accepted stock's Bluetooth-priority boot, wifi_on=0\n", gen);
        g_reco_hold = 0;
        return;
    }
    if(now - g_reco_first_rx_at >= RECO_WINDOW_MS) g_reco_hold = 0;       /* no conflict appeared: release unchanged */
}

void wifi_supervise(void){
    wifi_reconcile_poll();
    if(g_reco_hold) return;                         /* stock's radio decision is still being read: stay passive */
    static uint32_t last_check = 0;
    static int      first = 1;
    if(!first && lv_tick_elaps(last_check) < 5000) return;   /* check ~every 5s */
    first = 0; last_check = lv_tick_get();

    int up   = wifi_radio_on();
    int want = cfg_get_int("wifi_on", 1);

    /* re-enable all nets + clear freq locks + persist once the supplicant is up, unless a
     * user connect is mid-flight (conn_poll_cb sanitizes on its outcome). */
    if(up && g_sanitize_pending && !g_conn_timer && wifi_sanitize_savenets()) g_sanitize_pending = 0;

    if(!up && want){                        /* should be on but isn't -> (re)start */
        if(!g_wifi_last_start || lv_tick_elaps(g_wifi_last_start) > 30000){  /* 30s backoff */
            g_wifi_last_start = lv_tick_get();
            g_sanitize_pending = 1;         /* sanitize once this new instance is up */
            system("/usr/bin/wifi_up.sh >/dev/null 2>&1 &");
        }
    } else if(up && !want){                 /* should be off but a late wifi_up won -> enforce off */
        system("/usr/bin/wifi_down.sh >/dev/null 2>&1");
    }
}

/* centred grey placeholder shown in the list area for non-network states */
/* a single status message (Scanning / off / empty) - centered in the list area */
/* ---- the status header: a ring showing the state (grey = off, a short spinning arc = turning on, a ring
 * that fills with the signal strength once connected), the network name and a line under it ----------- */
static void hring_spin_exec(void *var, int32_t v){ lv_arc_set_rotation((lv_obj_t *)var, v % 360); }
enum { WS_OFF, WS_TURNING, WS_IDLE, WS_CONNECTED };
static void hdr_set(int state, const char *name, const char *sub, int sig_dbm){
    if(!g_hring) return;
    if(g_hknob){                                                   /* Braun: the knob's pointer + lamp say it */
        br_knob_set_on(g_hknob, state == WS_CONNECTED);
        lv_label_set_text(g_hname, name ? name : ""); lv_label_set_text(g_hsub, sub ? sub : "");
        lv_obj_set_style_text_color(g_hname, state == WS_OFF ? TC(TEXT_SECONDARY) : TC(TEXT_PRIMARY), 0);
        return;
    }
    lv_anim_delete(g_hring, hring_spin_exec);
    lv_arc_set_rotation(g_hring, 270);
    lv_color_t acc = ui_current_accent();
    int v = 0;
    if(state == WS_CONNECTED){ v = sig_dbm >= -45 ? 1000 : sig_dbm <= -90 ? 150 : 150 + (sig_dbm + 90) * 850 / 45; }   /* -90..-45 dBm -> 15..100% */
    lv_arc_set_value(g_hring, state == WS_TURNING ? 260 : v);
    lv_obj_set_style_arc_color(g_hring, acc, LV_PART_INDICATOR);
    if(state == WS_TURNING){
        lv_anim_t a; lv_anim_init(&a); lv_anim_set_var(&a, g_hring); lv_anim_set_exec_cb(&a, hring_spin_exec);
        lv_anim_set_values(&a, 270, 270 + 360); lv_anim_set_duration(&a, 1000);
        lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE); lv_anim_start(&a);
    }
    lv_obj_set_style_text_color(g_hglyph, state == WS_OFF ? TC(TEXT_DISABLED) : TC(TEXT_PRIMARY), 0);
    lv_label_set_text(g_hname, name ? name : "");
    lv_obj_set_style_text_color(g_hname, state == WS_OFF ? TC(TEXT_SECONDARY) : TC(TEXT_PRIMARY), 0);
    lv_label_set_text(g_hsub, sub ? sub : "");
}

static void list_msg(const char *m){
    if(!g_list) return;
    scan_stop();
    lv_obj_clean(g_list);
    lv_obj_set_flex_align(g_list, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *e = lv_label_create(g_list);
    lv_label_set_text(e, m);
    lv_obj_set_style_text_color(e, TC(TEXT_MUTED), 0);
    lv_obj_set_style_text_font(e, TF(UI_14), 0);
}
/* "Scanning" + a spinning refresh glyph, centered in the list area */
static void list_msg_scanning(void){
    if(!g_list) return;
    scan_stop();
    lv_obj_clean(g_list);
    lv_obj_set_flex_align(g_list, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *row = lv_obj_create(g_list);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 8, 0);
    lv_obj_t *t = lv_label_create(row);
    lv_label_set_text(t, "Scanning");
    lv_obj_set_style_text_color(t, TC(TEXT_MUTED), 0);
    lv_obj_set_style_text_font(t, TF(UI_14), 0);
    lv_obj_t *ic = lv_label_create(row);
    lv_label_set_text(ic, LV_SYMBOL_REFRESH);
    lv_obj_set_size(ic, 24, 24);
    lv_obj_set_style_text_align(ic, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(ic, TC(TEXT_MUTED), 0);
    lv_obj_set_style_text_font(ic, TF(UI_14), 0);
    g_scan_icon = ic;
    lv_obj_set_style_transform_pivot_x(ic, lv_pct(50), 0);
    lv_obj_set_style_transform_pivot_y(ic, lv_pct(50), 0);
    lv_anim_t a; lv_anim_init(&a); lv_anim_set_var(&a, ic);
    lv_anim_set_exec_cb(&a, spin_anim_cb);
    lv_anim_set_values(&a, 0, 3600); lv_anim_set_time(&a, 900);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);
}

/* default gateway from /proc/net/route (no external tools). */
static int get_gateway(char *out, int cap){
    out[0] = 0;
    FILE *f = fopen("/proc/net/route", "r");
    if(!f) return 0;
    char line[256]; int got = 0;
    if(!fgets(line, sizeof line, f)){ fclose(f); return 0; }   /* header */
    while(fgets(line, sizeof line, f)){
        char iface[32]; unsigned long dest, gw;
        if(sscanf(line, "%31s %lx %lx", iface, &dest, &gw) == 3 && dest == 0 && gw != 0){
            snprintf(out, cap, "%lu.%lu.%lu.%lu",
                     gw & 0xFF, (gw>>8)&0xFF, (gw>>16)&0xFF, (gw>>24)&0xFF);
            got = 1; break;
        }
    }
    fclose(f);
    return got;
}

/* Shell-safe token for a password: wpa_supplicant's quoted-string parser (wpa_config_parse_psk, 2.9)
 * takes the bytes between the first and LAST double quote literally, with no unescaping, and the
 * SET_NETWORK control command hands the rest of the line over untouched. So the value goes in as
 * "<bytes>" with NO backslash escaping (escaping " or \ would put a real backslash in the key); the
 * whole token is wrapped in shell single-quotes with any ' emitted as '\''. Output e.g. '"pa"ss\'  */
static void wpa_q(const char *v, char *out, int cap){
    int o = 0;
    #define WQ_PUT(ch) do{ if(o < cap-1) out[o++] = (char)(ch); }while(0)
    WQ_PUT('\'');                 /* open shell single-quote   */
    WQ_PUT('"');                  /* open wpa quoted-string    */
    for(; *v && o < cap-8; v++){
        if(*v == '\'') { WQ_PUT('\''); WQ_PUT('\\'); WQ_PUT('\''); WQ_PUT('\''); } /* '\'' */
        else WQ_PUT(*v);
    }
    WQ_PUT('"');                  /* close wpa quoted-string   */
    WQ_PUT('\'');                 /* close shell single-quote  */
    out[o] = 0;
    #undef WQ_PUT
}
/* SSID as unquoted hex (wpa_supplicant reads an unquoted ssid value as hex bytes): any byte sequence,
 * including " and \, arrives exactly. */
static void wpa_hex(const char *v, char *out, int cap){
    int o = 0;
    for(; *v && o + 2 < cap; v++){ snprintf(out + o, 3, "%02x", (unsigned char)*v); o += 2; }
    out[o] = 0;
}

/* ---- connect completion poll (honest feedback) -------------------------
 * A connect is async (association + DHCP take seconds). Instead of re-listing
 * immediately and implying success, poll wpa_state + IP for up to 15s and toast
 * the real outcome. */
static uint32_t    g_conn_start;
static char        g_conn_ssid[64];
static int         g_conn_id = -1;

/* Did the supplicant give up on network `id` after auth failures? `list_networks` rows are
 * "id \t ssid \t bssid \t flags"; flags holds [TEMP-DISABLED] once it has temp-disabled the network
 * (the string is in this wpa_supplicant 2.9 build's flag table next to [CURRENT]/[DISABLED]). Only the
 * 4th field is checked; the ssid field is printf-encoded by wpa_cli but is never looked at here. */
static int wifi_net_temp_disabled(const char *list, int id){
    const char *l = strchr(list, '\n');           /* skip the header row */
    l = l ? l + 1 : "";
    while(*l){
        const char *nl = strchr(l, '\n'); size_t len = nl ? (size_t)(nl - l) : strlen(l);
        char *end; long v = strtol(l, &end, 10);
        if(end != l && *end == '\t' && v == id && (size_t)(end - l) < len){
            const char *f = end;                  /* skip ssid and bssid fields: the 3rd tab starts flags */
            int tabs = 0;
            while(f < l + len && tabs < 3){ if(*f == '\t') tabs++; if(tabs < 3) f++; }
            if(tabs == 3){
                f++;
                size_t rest = (size_t)(l + len - f);
                for(size_t i = 0; i + 15 <= rest; i++)
                    if(!strncmp(f + i, "[TEMP-DISABLED]", 15)) return 1;
            }
        }
        if(!nl) break;
        l = nl + 1;
    }
    return 0;
}

static void conn_fail(const char *why){
    lv_timer_del(g_conn_timer); g_conn_timer = NULL;
    /* connect failed: re-enable other nets IF the supplicant is still up; if it died,
     * leave g_sanitize_pending set so supervise sanitizes the fresh instance when it's up. */
    if(wifi_sanitize_savenets()) g_sanitize_pending = 0;
    ui_toast(why);
    start_scan();
}
static void conn_poll_cb(lv_timer_t *t){
    (void)t;
    char ss[64], ip[32];
    /* require COMPLETED + an IP + the associated SSID == the one we asked for, so a
     * stale old-AP "COMPLETED" (with the previous IP) during an AP switch can't be
     * mistaken for success. */
    if(wifi_status(ss, sizeof ss, ip, sizeof ip) && ip[0] && !strcmp(ss, g_conn_ssid)){
        lv_timer_del(g_conn_timer); g_conn_timer = NULL;
        snprintf(g_cur_ssid, sizeof g_cur_ssid, "%s", g_conn_ssid);
        if(wifi_sanitize_savenets()) g_sanitize_pending = 0;   /* re-enable all + persist (supplicant is up here) */
        ui_toast("Connected");
        start_scan();                        /* refresh list so the joined net gets its ✓ */
        return;
    }
    char nl[2048]; run_cap(WCLI "list_networks 2>/dev/null", nl, sizeof nl);
    if(wifi_net_temp_disabled(nl, g_conn_id)){ conn_fail("Couldn't connect - check the password"); return; }
    if(lv_tick_elaps(g_conn_start) > 15000)   /* gave it 15s to associate + DHCP */
        conn_fail("Couldn't connect");
}

/* wpa_cli 2.9 prints FAIL and still exits 0, so a step succeeded only if its reply is exactly "OK". */
static int wifi_reply_ok(const char *out){
    return out[0] == 'O' && out[1] == 'K' && (out[2] == 0 || out[2] == '\n' || out[2] == '\r');
}
static int wifi_step(const char *cmd, const char *step){
    char out[64]; run_cap(cmd, out, sizeof out);
    if(wifi_reply_ok(out)) return 0;
    fprintf(stderr, "wifi connect: %s failed (reply '%.20s')\n", step, out);
    return -1;
}
/* WPA passphrase: 8..63 bytes (wpa_supplicant's own limit; a 64-char value would have to be a hex PSK) */
static int wifi_psk_len_ok(const char *key){ size_t n = strlen(key); return n >= 8 && n <= 63; }

/* Write the profile into the supplicant and select it. Every step must reply OK; the first that
 * does not aborts BEFORE anything later runs (a half-built profile or an old saved key must never be
 * selected). Returns NULL on success, else the toast text. */
static const char *wifi_apply_net(int id, const char *ssid, const char *key){
    char cmd[512], qssid[160], qkey[300];
    wpa_hex(ssid, qssid, sizeof qssid);
    snprintf(cmd, sizeof cmd, WCLI "set_network %d ssid %s 2>/dev/null", id, qssid);
    if(wifi_step(cmd, "set ssid")) return "Couldn't configure Wi-Fi";
    if(key && key[0]){
        wpa_q(key, qkey, sizeof qkey);
        /* set key_mgmt WPA-PSK BEFORE psk: a REUSED profile that was previously open still has
         * key_mgmt NONE, and setting only psk would leave it open-auth (connect fails w/ right pass). */
        snprintf(cmd, sizeof cmd, WCLI "set_network %d key_mgmt WPA-PSK 2>/dev/null", id);
        if(wifi_step(cmd, "set key_mgmt")) return "Couldn't configure Wi-Fi";
        snprintf(cmd, sizeof cmd, WCLI "set_network %d psk %s 2>/dev/null", id, qkey);
        if(wifi_step(cmd, "set psk")) return "Couldn't set Wi-Fi password";
    } else {
        snprintf(cmd, sizeof cmd, WCLI "set_network %d key_mgmt NONE 2>/dev/null", id);
        if(wifi_step(cmd, "set open")) return "Couldn't configure Wi-Fi";
    }
    snprintf(cmd, sizeof cmd, WCLI "enable_network %d 2>/dev/null", id);
    if(wifi_step(cmd, "enable_network")) return "Couldn't configure Wi-Fi";
    snprintf(cmd, sizeof cmd, WCLI "select_network %d 2>/dev/null", id);
    if(wifi_step(cmd, "select_network")) return "Couldn't connect to Wi-Fi";
    return NULL;
}

/* ---- connect ------------------------------------------------------------ */
static void wifi_connect(const char *ssid, const char *key){
    char buf[2048];
    if(key && key[0] && !wifi_psk_len_ok(key)){ ui_toast("Password must be 8 to 63 characters"); return; }
    int id = -1;
    run_cap(WCLI "list_networks 2>/dev/null", buf, sizeof buf);
    char *l = strchr(buf, '\n'); if(l) l++;               /* skip the header row */
    while(l && *l){
        /* exact SSID match by literal-tab field split. sscanf("%d\t%63[^\t]") swallowed leading
         * whitespace in the SSID, so " Home" and "Home" compared equal - selecting one could
         * reconfigure the other saved profile. Split on the real tabs instead. */
        char *nl = strchr(l, '\n');
        char *t1 = strchr(l, '\t');                       /* end of id field */
        if(t1 && (!nl || t1 < nl)){
            char *sfld = t1 + 1;                          /* ssid field */
            char *t2 = strchr(sfld, '\t');                /* the SSID is always followed by a tab (bssid) */
            /* require a COMPLETE ssid field: a truncated capture ("17\tHom") must NOT match a shorter
             * prefix of a longer real SSID (e.g. select "Home" and reconfigure saved "HomeOffice"). */
            if(t2 && (!nl || t2 < nl) && wifi_ssid_field_eq(sfld, (int)(t2 - sfld), ssid)){
                id = atoi(l); break;
            }
        }
        l = nl ? nl + 1 : NULL;
    }
    if(id < 0){
        char idbuf[32];
        run_cap(WCLI "add_network 2>/dev/null", idbuf, sizeof idbuf);
        char *end = idbuf;
        long nid = strtol(idbuf, &end, 10);
        if(end == idbuf || nid < 0){   /* empty / "FAIL\n" -> id would be 0 and clobber network 0 */
            fprintf(stderr, "wifi add_network failed: '%s'\n", idbuf);
            ui_toast("Couldn't add network");
            return;
        }
        id = (int)nid;
    }
    const char *why = wifi_apply_net(id, ssid, key);
    if(why){ ui_toast(why); return; }
    if(g_conn_timer){ lv_timer_del(g_conn_timer); g_conn_timer = NULL; }
    if(system(WCLI "save_config >/dev/null 2>&1")) fprintf(stderr, "wifi connect: save_config failed\n");   /* non-fatal: connection still works unsaved */
    system("/sbin/udhcpc -i wlan0 -n -q >/dev/null 2>&1 &");   /* backgrounded; outcome via conn_poll_cb */
    /* don't claim success yet - poll for the real outcome (assoc + DHCP). */
    snprintf(g_conn_ssid, sizeof g_conn_ssid, "%s", ssid);
    g_conn_id = id;
    ui_toast("Connecting...");
    g_conn_start = lv_tick_get();
    g_conn_timer = lv_timer_create(conn_poll_cb, 1000, NULL);
}

static void psk_done(const char *text){
    if(text && text[0]) wifi_connect(g_pending_ssid, text);
}

/* ---- details screen (SCR_WIFI_INFO) ------------------------------------- */
static void info_row(const char *key, const char *val){
    lv_obj_t *r = lv_obj_create(g_info_list);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, 250, 40);
    lv_obj_set_style_radius(r, 8, 0);
    lv_obj_set_style_bg_color(r, TC(SURFACE), 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_50, 0);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *k = lv_label_create(r);
    lv_label_set_text(k, key);
    lv_obj_set_pos(k, 12, 11);
    lv_obj_set_style_text_font(k, TF(UI_14), 0);
    lv_obj_set_style_text_color(k, TC(TEXT_MUTED), 0);
    lv_obj_t *v = lv_label_create(r);
    lv_label_set_text(v, val && val[0] ? val : "-");
    lv_label_set_long_mode(v, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(v, 110, 11); lv_obj_set_size(v, 128, 18);
    lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_font(v, ui_font_cjk(14), 0);   /* "Network" value = SSID: Cyrillic/CJK-capable (issue #3) */
    lv_obj_set_style_text_color(v, TC(TEXT_PRIMARY), 0);
}
static void info_back_cb(lv_event_t *e){ if(lv_event_get_code(e)==LV_EVENT_CLICKED) screen_back(); }

void wifi_info_create(lv_obj_t *root){
    lv_obj_set_style_bg_color(root, TC(CANVAS), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    ui_header_cb(root, "Network", info_back_cb);   /* shared header */
    g_info_list = lv_obj_create(root);
    lv_obj_remove_style_all(g_info_list);
    lv_obj_set_pos(g_info_list, 55, 84); lv_obj_set_size(g_info_list, 250, 230);
    lv_obj_set_style_pad_bottom(g_info_list, 44, 0);   /* last row scrolls clear of the round bottom bezel */
    lv_obj_set_style_bg_opa(g_info_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_row(g_info_list, 8, 0);
    lv_obj_set_flex_flow(g_info_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(g_info_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scroll_dir(g_info_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(g_info_list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(g_info_list, LV_OBJ_FLAG_SCROLL_MOMENTUM);
}

/* A tappable destructive-action row appended to the details list (Forget). */
static void info_action_row(const char *label, lv_event_cb_t cb){
    lv_obj_t *r = lv_button_create(g_info_list);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, 250, 44);
    lv_obj_set_style_radius(r, 8, 0);
    lv_obj_set_style_bg_color(r, TC(DANGER_SURFACE), 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(r, TC(DANGER_SURFACE_PRESSED), LV_STATE_PRESSED);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    ui_on(r, cb, LV_EVENT_CLICKED, NULL, "wifi.cb", UI_CORE);
    lv_obj_t *t = lv_label_create(r);
    lv_label_set_text(t, label); lv_obj_center(t);
    lv_obj_set_style_text_font(t, TF(UI_14), 0);
    lv_obj_set_style_text_color(t, TC(STATUS_DANGER), 0);
}
static char g_info_ssid[64];   /* the exact SSID shown on the details screen (retained for Forget) */
/* Remove EVERY saved network whose SSID EXACTLY equals `ssid`, then persist. 1 if any removed.
 * list_networks is TAB-separated (id\tssid\tbssid\tflags); we split on literal tabs and compare the
 * exact ssid field - NOT sscanf "%[^\t]" after a "\t", which would swallow a leading space so " Home"
 * and "Home" collide and Forget could delete the wrong network. */
static int wifi_remove_ssid(const char *ssid){
    if(!wpa_ready() || !ssid || !ssid[0]) return 0;
    char buf[4096]; run_cap(WCLI "list_networks 2>/dev/null", buf, sizeof buf);
    char *l = strchr(buf, '\n'); if(l) l++;               /* skip the header row */
    int removed = 0, failed = 0;
    while(l && *l){
        char *nl = strchr(l, '\n');
        char *t1 = strchr(l, '\t');                       /* end of id field */
        if(t1 && (!nl || t1 < nl)){
            char *sfld = t1 + 1;                           /* ssid field start */
            char *t2 = strchr(sfld, '\t');                 /* end of ssid field (always a tab: bssid follows) */
            /* require a COMPLETE ssid field so a truncated capture can't match a shorter prefix of a longer real SSID. */
            if(t2 && (!nl || t2 < nl) && wifi_ssid_field_eq(sfld, (int)(t2 - sfld), ssid)){
                int nid = atoi(l);
                /* wpa_cli prints OK/FAIL and exits 0 either way, so check its output, not the
                 * shell rc - otherwise a failed remove would still report "forgotten". */
                char cmd[80]; snprintf(cmd, sizeof cmd, WCLI "remove_network %d 2>/dev/null", nid);
                char out[64]; run_cap(cmd, out, sizeof out);
                if(strstr(out, "OK")) removed = 1; else failed = 1;   /* a duplicate-SSID profile we could not remove -> report failure */
            }
        }
        l = nl ? nl + 1 : NULL;
    }
    if(removed){
        char so[64]; run_cap(WCLI "save_config 2>/dev/null", so, sizeof so);
        if(!strstr(so, "OK")) failed = 1;   /* removed from the running config but not persisted (it returns on reboot) */
    }
    return removed && !failed;   /* success only when something was removed AND nothing failed */
}
static void wifi_forget_cb(lv_event_t *e){
    if(lv_event_get_code(e)!=LV_EVENT_CLICKED) return;
    ui_toast(wifi_remove_ssid(g_info_ssid) ? "Network forgotten" : "Couldn't forget network");
    wifi_open();   /* back to the list + fresh scan */
}
void wifi_info_open(void){
    if(!g_info_list) return;
    lv_obj_clean(g_info_list);
    char ssid[64], ip[32], buf[2048], val[64], gw[32];
    wifi_status(ssid, sizeof ssid, ip, sizeof ip);
    snprintf(g_info_ssid, sizeof g_info_ssid, "%s", ssid);   /* retain for Forget (don't re-read on click) */
    info_row("Network", ssid);
    info_row("IP Address", ip);
    if(get_gateway(gw, sizeof gw)) info_row("Router", gw);
    run_cap(WCLI "signal_poll 2>/dev/null", buf, sizeof buf);
    char *p = strstr(buf, "RSSI=");
    if(p){ int r; if(sscanf(p+5, "%d", &r)==1){ snprintf(val,sizeof val,"%d dBm", r); info_row("Signal", val);} }
    run_cap(WCLI "status 2>/dev/null", buf, sizeof buf);
    p = strstr(buf, "key_mgmt=");
    if(p && sscanf(p+9, "%63[^\n]", val)==1){ info_row("Security", strstr(val,"NONE")?"Open":val); }   /* check sscanf: val was read uninitialized on no-match */
    p = strstr(buf, "\nfreq=");
    if(p){ int fr; if(sscanf(p+6,"%d",&fr)==1){ snprintf(val,sizeof val,"%d MHz", fr); info_row("Frequency", val);} }
    p = strstr(buf, "bssid=");
    if(p && sscanf(p+6, "%63[^\n]", val)==1){ info_row("BSSID", val); }   /* check sscanf: val was read uninitialized on no-match */
    info_action_row("Forget This Network", wifi_forget_cb);   /* C05: remove the saved network */
    screen_show(SCR_WIFI_INFO);
}

/* ---- scan list ---------------------------------------------------------- */
/* A row's data rides in its event callbacks, NOT in lv_obj user data: the curved-list helper keeps its own
 * bookkeeping in every row's user data, which would overwrite it. */
typedef struct { int flags; char ssid[64]; } netrow_t;
static void row_free_cb(lv_event_t *e){ free(lv_event_get_user_data(e)); }
static void net_cb(lv_event_t *e){
    if(lv_event_get_code(e)!=LV_EVENT_CLICKED) return;
    const netrow_t *nr = lv_event_get_user_data(e);
    if(!nr) return;
    const char *ssid = nr->ssid;
    intptr_t flags = nr->flags;
    int secured   = flags & 1;
    int connected = flags & 2;
    if(!ssid || !ssid[0]) return;
    if(connected){
        wifi_info_open();                 /* tap the joined network -> details */
    } else if(secured){
        snprintf(g_pending_ssid, sizeof g_pending_ssid, "%s", ssid);
        char prompt[80]; snprintf(prompt, sizeof prompt, "Password for %s", ssid);
        kbinput_open_password(prompt, "", psk_done);   /* masked entry; prompt, not the bare SSID */
    } else {
        wifi_connect(ssid, NULL);
    }
}

/* signal-strength bars: RSSI(dBm) -> 1..4 filled bars, bottom-aligned. */
static void add_signal_bars(lv_obj_t *parent, int x, int y, int sig){
    int bars = sig >= -55 ? 4 : sig >= -67 ? 3 : sig >= -78 ? 2 : 1;
    static const int h[4] = {5, 9, 13, 17};
    for(int i=0;i<4;i++){
        lv_obj_t *b = lv_obj_create(parent);
        lv_obj_remove_style_all(b);
        lv_obj_clear_flag(b, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_size(b, 4, h[i]);
        lv_obj_set_pos(b, x + i*6, y + (17 - h[i]));
        lv_obj_set_style_radius(b, 1, 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(b, i < bars ? TC(TEXT_PRIMARY) : TC(SURFACE_STRONG), 0);
    }
}

#define WROW_W 268
static void add_net_row(const char *ssid, int signal, int secured, int connected){
    lv_obj_t *r = lv_button_create(g_list);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, WROW_W, 50);
    lv_obj_add_flag(r, LV_OBJ_FLAG_USER_1);                 /* curves with the circle */
    lv_obj_set_style_radius(r, TH_R_ROW, 0);
    lv_obj_set_style_bg_color(r, connected ? TC(SURFACE_RAISED) : TC(SURFACE), 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(r, TC(SURFACE_RAISED), LV_STATE_PRESSED);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    netrow_t *nr = malloc(sizeof *nr);
    if(nr){ nr->flags = (secured?1:0) | (connected?2:0); snprintf(nr->ssid, sizeof nr->ssid, "%s", ssid); }
    lv_obj_add_event_cb(r, net_cb, LV_EVENT_CLICKED, nr);
    lv_obj_add_event_cb(r, row_free_cb, LV_EVENT_DELETE, nr);

    int tx = 16;
    if(connected){
        lv_obj_t *ck = lv_label_create(r);
        lv_label_set_text(ck, LV_SYMBOL_OK);
        lv_obj_set_pos(ck, 14, 16);
        lv_obj_set_style_text_font(ck, TF(UI_14), 0);
        lv_obj_set_style_text_color(ck, ui_current_accent(), 0);
        tx = 38;
    }
    lv_obj_t *t = lv_label_create(r);
    lv_label_set_text(t, ssid);
    lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(t, tx, 13); lv_obj_set_size(t, WROW_W - 78 - tx, 24);
    lv_obj_set_style_text_font(t, ui_font_cjk(18), 0);       /* SSIDs are user data: Cyrillic/CJK-capable (issue #3) */
    lv_obj_set_style_text_color(t, TC(TEXT_PRIMARY), 0);

    if(secured){
        /* drawn padlock (no lock glyph exists in the fonts): a shackle loop with the body covering
         * its lower half, muted grey, in the clear lane left of the signal bars. */
        lv_obj_t *shk = lv_obj_create(r);
        lv_obj_remove_style_all(shk);
        lv_obj_clear_flag(shk, LV_OBJ_FLAG_CLICKABLE);   /* decorative: don't steal taps from the row */
        lv_obj_set_size(shk, 8, 8); lv_obj_set_pos(shk, 217, 15);
        lv_obj_set_style_radius(shk, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(shk, 2, 0);
        lv_obj_set_style_border_color(shk, TC(TEXT_MUTED), 0);
        lv_obj_t *body = lv_obj_create(r);           /* body: rounded rect */
        lv_obj_remove_style_all(body);
        lv_obj_clear_flag(body, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_size(body, 10, 8); lv_obj_set_pos(body, 216, 20);
        lv_obj_set_style_radius(body, 2, 0);
        lv_obj_set_style_bg_color(body, TC(TEXT_MUTED), 0);
        lv_obj_set_style_bg_opa(body, LV_OPA_COVER, 0);
    }
    add_signal_bars(r, 232, 14, signal);
}

/* one de-duplicated network of a scan */
struct wnet { char ssid[64]; int sig; int secured; int connected; };
#define WNET_MAX 40

/* Parse `wpa_cli scan_results` (bssid \t freq \t signal \t flags \t ssid) into out[] (max WNET_MAX).
 * SSIDs are printf-encoded by wpa_cli and are decoded to real bytes first (wifi_ssid_decode), so names
 * compare, dedupe, display and connect as what they are; undecodable/over-long/hidden rows are skipped.
 * Only newline-terminated rows count: a capture cut off mid-row (buffer full) loses its last row rather
 * than showing a partial name. Access points with the same SSID collapse into one row at the first
 * one's position, showing the strongest signal (duplicates are merged BEFORE the row cap applies).
 * The connected network (cur, may be "") is always row 0; the rest keep scan order. If cur is not in the
 * scan it is still shown first, using cur_rssi (dBm or WNET_NO_RSSI -> weakest bars) and cur_secured. */
#define WNET_NO_RSSI (-1000)
static int wifi_scan_order(char *buf, const char *cur, int cur_rssi, int cur_secured, struct wnet *out){
    int n = 0;
    char *l = strchr(buf, '\n'); if(l) l++;   /* skip header */
    while(l && *l){
        char *nl = strchr(l, '\n');
        if(!nl) break;                         /* unterminated final row: truncated capture, discard */
        *nl = 0;
        char *f[5] = {0,0,0,0,0}; int nf = 0; char *p = l;
        f[nf++] = p;
        while(nf < 5 && (p = strchr(p, '\t'))){ *p++ = 0; f[nf++] = p; }
        char ssid[64];
        if(nf == 5 && f[4][0] && wifi_ssid_decode(f[4], (int)strlen(f[4]), ssid, sizeof ssid) > 0){
            int sig = atoi(f[2]), dup = -1;
            for(int i=0;i<n;i++) if(!strcmp(out[i].ssid, ssid)){ dup = i; break; }
            if(dup >= 0){
                if(sig > out[dup].sig) out[dup].sig = sig;
            } else if(n < WNET_MAX){
                snprintf(out[n].ssid, sizeof out[n].ssid, "%s", ssid);
                out[n].sig = sig;
                out[n].secured = (strstr(f[3], "WPA") || strstr(f[3], "PSK") || strstr(f[3], "WEP")) != 0;
                out[n].connected = 0;
                n++;
            }
        }
        l = nl + 1;
    }
    if(!cur || !cur[0]) return n;
    int at = -1;
    for(int i=0;i<n;i++) if(!strcmp(out[i].ssid, cur)){ at = i; break; }
    struct wnet c;
    if(at >= 0){ c = out[at]; memmove(&out[1], &out[0], sizeof(struct wnet) * at); }
    else{
        memset(&c, 0, sizeof c);
        snprintf(c.ssid, sizeof c.ssid, "%s", cur);
        c.sig = (cur_rssi == WNET_NO_RSSI) ? -100 : cur_rssi;
        c.secured = cur_secured;
        if(n >= WNET_MAX) n--;                 /* keep the cap: drop the last row */
        memmove(&out[1], &out[0], sizeof(struct wnet) * n);
        n++;
    }
    c.connected = 1;
    out[0] = c;
    return n;
}

/* RSSI from `signal_poll` output: a complete signed integer after "RSSI=", else WNET_NO_RSSI */
static int wifi_parse_rssi(const char *b){
    const char *r = strstr(b, "RSSI=");
    if(!r) return WNET_NO_RSSI;
    r += 5;
    char *end; long v = strtol(r, &end, 10);
    if(end == r || (*end && *end != '\n' && *end != '\r') || v > 0 || v < -200) return WNET_NO_RSSI;
    return (int)v;
}
/* is the current link encrypted, from `status` output? (key_mgmt=NONE -> open; absent -> unknown, treated open) */
static int wifi_status_secured(const char *st){
    for(const char *l = st; l && *l; ){
        if(!strncmp(l, "key_mgmt=", 9)){
            const char *k = l + 9;
            return !(!strncmp(k, "NONE", 4) && (k[4] == 0 || k[4] == '\n' || k[4] == '\r'));
        }
        l = strchr(l, '\n'); if(l) l++;
    }
    return 0;
}
static int wifi_cur_rssi(void){
    char b[512];
    if(!run_cap(WCLI "signal_poll 2>/dev/null", b, sizeof b)) return WNET_NO_RSSI;
    return wifi_parse_rssi(b);
}

static void scan_fill(void){
    if(!g_list) return;
    scan_stop();
    lv_obj_clean(g_list);
    lv_obj_set_flex_align(g_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);  /* rows top-aligned */
    char cs[64], cip[32];
    if(!(wifi_radio_on() && wifi_status(cs, sizeof cs, cip, sizeof cip))) cs[0]=0;
    snprintf(g_cur_ssid, sizeof g_cur_ssid, "%s", cs);
    g_cur_sig = -100;

    char *buf = run_cap_all(WCLI "scan_results 2>/dev/null", 1 << 20);
    if(!buf){ list_msg("Couldn't scan Wi-Fi"); return; }
    struct wnet nets[WNET_MAX];
    int rssi = WNET_NO_RSSI, sec = 0;
    if(g_cur_ssid[0]){
        char st[2048]; run_cap(WCLI "status 2>/dev/null", st, sizeof st);
        sec = wifi_status_secured(st);
        rssi = wifi_cur_rssi();
    }
    int n = wifi_scan_order(buf, g_cur_ssid, rssi, sec, nets);
    free(buf);
    for(int i=0;i<n;i++) add_net_row(nets[i].ssid, nets[i].sig, nets[i].secured, nets[i].connected);
    if(n == 0) list_msg("No networks found");
}

static void scan_timer_cb(lv_timer_t *t){
    (void)t;
    scan_fill();   /* scan_fill() calls scan_stop() before clearing the list */
    if(g_scan_timer){ lv_timer_del(g_scan_timer); g_scan_timer = NULL; }
}
static void start_scan(void){
    if(!wifi_radio_on()){ list_msg("Switch it on to see networks"); hdr_set(WS_OFF, "Wi-Fi is off", "", 0); return; }
    /* wpa_cli returns non-zero when it can't reach the control socket / issue the scan; surface
     * that honestly instead of letting scan_fill() report a false "No networks found". */
    if(system(WCLI "scan >/dev/null 2>&1") != 0){ list_msg("Couldn't scan Wi-Fi"); return; }
    list_msg_scanning();
    if(g_scan_timer) lv_timer_del(g_scan_timer);
    g_scan_timer = lv_timer_create(scan_timer_cb, 2500, NULL);
    lv_timer_set_repeat_count(g_scan_timer, 1);
}

/* ---- radio toggle ------------------------------------------------------- */
/* wifi_up.sh runs async, so wpa_supplicant isn't up the instant the switch flips.
 * Show "Turning on Wi-Fi..." and poll until the radio is actually up before
 * scanning (otherwise start_scan() would immediately report "Wi-Fi is off"). */
static lv_timer_t *g_radio_timer;
static uint32_t    g_radio_start;
static void radio_on_poll_cb(lv_timer_t *t){
    (void)t;
    if(wifi_radio_on()){
        lv_timer_del(g_radio_timer); g_radio_timer = NULL;
        start_scan();
        return;
    }
    if(lv_tick_elaps(g_radio_start) > 8000){
        lv_timer_del(g_radio_timer); g_radio_timer = NULL;
        list_msg("Couldn't turn on Wi-Fi"); hdr_set(WS_OFF, "Wi-Fi is off", "Couldn't turn it on", 0);
    }
}
static void sw_cb(lv_event_t *e){
    if(lv_event_get_code(e)!=LV_EVENT_VALUE_CHANGED) return;
    int on = lv_obj_has_state(g_sw, LV_STATE_CHECKED);
    g_intent_epoch++;                            /* an explicit choice: ends any radio reconciliation hold */
    if(on){
        cfg_set_int("wifi_on", 1);               /* persist intent -> keepalive keeps it up */
        system("/usr/bin/wifi_up.sh >/dev/null 2>&1 &");
        g_wifi_last_start = lv_tick_get();       /* let the keepalive back off (no duplicate launch) */
        g_sanitize_pending = 1;                  /* sanitize this instance once it's up */
        list_msg("Please wait..."); hdr_set(WS_TURNING, "Turning on...", "", 0);
        g_radio_start = lv_tick_get();
        if(g_radio_timer) lv_timer_del(g_radio_timer);
        g_radio_timer = lv_timer_create(radio_on_poll_cb, 700, NULL);
    } else {
        cfg_set_int("wifi_on", 0);               /* set OFF before down so keepalive won't resurrect */
        if(g_radio_timer){ lv_timer_del(g_radio_timer); g_radio_timer = NULL; }
        if(g_scan_timer){ lv_timer_del(g_scan_timer); g_scan_timer = NULL; }  /* cancel a pending scan, else scan_fill overwrites "Wi-Fi is off" with stale networks */
        if(g_conn_timer){ lv_timer_del(g_conn_timer); g_conn_timer = NULL; }
        system("/usr/bin/wifi_down.sh >/dev/null 2>&1");
        list_msg("Switch it on to see networks"); hdr_set(WS_OFF, "Wi-Fi is off", "", 0);
    }
}

/* Quick Settings tile short-press: flip the radio + persist intent, no screen-specific UI.
 * Mirrors sw_cb's radio actions (keepalive enforces the intent). Returns the new state. */
int wifi_toggle(void){
    g_intent_epoch++;                        /* an explicit choice: ends any radio reconciliation hold */
    int on = !wifi_radio_on();               /* flip what the radio IS (checked now), not what was last asked for */
    g_live_cached = -1;                      /* the tile reads the radio afresh after a flip */
    cfg_set_int("wifi_on", on);
    if(on){
        system("/usr/bin/wifi_up.sh >/dev/null 2>&1 &");
        g_wifi_last_start = lv_tick_get();   /* let the keepalive back off (no duplicate launch) */
        g_sanitize_pending = 1;              /* sanitize this instance once it's up */
    } else {
        if(g_radio_timer){ lv_timer_del(g_radio_timer); g_radio_timer = NULL; }
        if(g_scan_timer){ lv_timer_del(g_scan_timer); g_scan_timer = NULL; }  /* cancel any pending radio/scan work on the open Wi-Fi screen */
        if(g_conn_timer){ lv_timer_del(g_conn_timer); g_conn_timer = NULL; }
        system("/usr/bin/wifi_down.sh >/dev/null 2>&1");
    }
    if(g_hring){ if(on) hdr_set(WS_TURNING, "Turning on...", "", 0); else hdr_set(WS_OFF, "Wi-Fi is off", "", 0); }
    /* keep the Wi-Fi screen's switch in sync so it reflects reality when opened later */
    if(g_sw){ if(on) lv_obj_add_state(g_sw, LV_STATE_CHECKED); else lv_obj_clear_state(g_sw, LV_STATE_CHECKED); }
    return on;
}

static void rescan_cb(lv_event_t *e){ if(lv_event_get_code(e)==LV_EVENT_CLICKED) start_scan(); }
__attribute__((unused)) static void back_cb(lv_event_t *e){ if(lv_event_get_code(e)==LV_EVENT_CLICKED) screen_back(); }

void wifi_create(lv_obj_t *root){
    lv_obj_set_style_bg_color(root, TC(CANVAS), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    /* the status ring (centre), the switch on its right and the rescan button on its left */
    g_hring = lv_arc_create(root);
    lv_obj_remove_style(g_hring, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(g_hring, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(g_hring, 84, 84); lv_obj_align(g_hring, LV_ALIGN_TOP_MID, 0, 34);
    lv_arc_set_rotation(g_hring, 270); lv_arc_set_bg_angles(g_hring, 0, 360); lv_arc_set_range(g_hring, 0, 1000); lv_arc_set_value(g_hring, 0);
    lv_obj_set_style_arc_width(g_hring, 4, LV_PART_MAIN); lv_obj_set_style_arc_color(g_hring, TC(CONTROL_TRACK), LV_PART_MAIN);
    lv_obj_set_style_arc_width(g_hring, 4, LV_PART_INDICATOR); lv_obj_set_style_arc_rounded(g_hring, true, LV_PART_INDICATOR);
    g_hglyph = lv_label_create(root);
    lv_label_set_text(g_hglyph, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_font(g_hglyph, TF(UI_28), 0);
    lv_obj_align(g_hglyph, LV_ALIGN_TOP_MID, 0, 62);
    g_hname = lv_label_create(root);
    lv_label_set_long_mode(g_hname, LV_LABEL_LONG_DOT); lv_obj_set_width(g_hname, 230);
    lv_obj_set_style_text_align(g_hname, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(g_hname, ui_font_cjk(18), 0);
    lv_obj_align(g_hname, LV_ALIGN_TOP_MID, 0, 128);
    g_hsub = lv_label_create(root);
    lv_label_set_long_mode(g_hsub, LV_LABEL_LONG_DOT); lv_obj_set_width(g_hsub, 230);
    lv_obj_set_style_text_align(g_hsub, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(g_hsub, ui_font_cjk(14), 0); lv_obj_set_style_text_color(g_hsub, TC(TEXT_SECONDARY), 0);   /* has the middle dot */
    lv_obj_align(g_hsub, LV_ALIGN_TOP_MID, 0, 152);

    g_sw = lv_switch_create(root);
    lv_obj_set_size(g_sw, 46, 24);
    lv_obj_set_ext_click_area(g_sw, 12);
    lv_obj_align(g_sw, LV_ALIGN_TOP_MID, 106, 64);
    lv_obj_set_style_bg_color(g_sw, TC(CONTROL_TRACK), LV_PART_MAIN);
    lv_obj_set_style_bg_color(g_sw, ui_current_accent(), (lv_style_selector_t)LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_add_event_cb(g_sw, sw_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *rb = lv_button_create(root);
    lv_obj_remove_style_all(rb);
    lv_obj_set_size(rb, 34, 34); lv_obj_align(rb, LV_ALIGN_TOP_MID, -106, 59);
    lv_obj_set_ext_click_area(rb, 8);
    lv_obj_set_style_radius(rb, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(rb, TC(SURFACE), 0);
    lv_obj_set_style_bg_opa(rb, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(rb, TC(SURFACE_RAISED), LV_STATE_PRESSED);
    lv_obj_add_event_cb(rb, rescan_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *rl = lv_label_create(rb);
    lv_label_set_text(rl, LV_SYMBOL_REFRESH);
    lv_obj_set_style_text_font(rl, TF(UI_14), 0);
    lv_obj_set_style_text_color(rl, TC(TEXT_SECONDARY), 0);
    lv_obj_center(rl);

    /* network list (carries connected/off/scanning state itself), curved with the circle */
    g_list = lv_obj_create(root);
    lv_obj_remove_style_all(g_list);
    lv_obj_set_pos(g_list, (360 - WROW_W) / 2, 182); lv_obj_set_size(g_list, WROW_W, 170);
    lv_obj_set_style_pad_bottom(g_list, 30, 0);
    lv_obj_set_style_bg_opa(g_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_row(g_list, 4, 0);
    lv_obj_set_flex_flow(g_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(g_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scroll_dir(g_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(g_list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(g_list, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    curvelist_attach(&g_wcl, g_list, root, WROW_W);
    hdr_set(WS_OFF, "Wi-Fi", "", 0);
    if(th_braun()){
        lv_obj_add_flag(g_hring, LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(g_hglyph, LV_OBJ_FLAG_HIDDEN);
        lv_obj_t *pd = br_disc(root, 180, 80, 44, BR_PANEL); (void)pd;
        g_hknob = br_knob(root, 180, 76, 30, LV_SYMBOL_WIFI, TF(UI_20));
        br_style_switch(g_sw);
        lv_obj_set_style_text_color(g_hname, TC(TEXT_PRIMARY), 0); lv_obj_set_style_text_font(g_hname, br_font(18, 1), 0);
        lv_obj_set_style_text_color(g_hsub, TC(TEXT_SECONDARY), 0); lv_obj_set_style_text_font(g_hsub, br_font(12, 0), 0);
        for(uint32_t k = 0; k < lv_obj_get_child_count(root); k++){      /* the rescan button: a flat disc */
            lv_obj_t *o = lv_obj_get_child(root, k);
            if(lv_obj_check_type(o, &lv_button_class) && lv_obj_get_width(o) == 34){
                lv_obj_set_style_bg_color(o, TC(SURFACE), 0);
                lv_obj_t *l = lv_obj_get_child(o, 0); if(l) lv_obj_set_style_text_color(l, TC(TEXT_PRIMARY), 0);
            }
        }
        hdr_set(WS_OFF, "Wi-Fi", "", 0);
    }
}

/* called from settings when the Wi-Fi row is tapped */
void wifi_open(void){
    if(g_sw) lv_obj_set_style_bg_color(g_sw, ui_current_accent(), (lv_style_selector_t)LV_PART_INDICATOR | LV_STATE_CHECKED);   /* follow an accent change */
    if(g_sw){
        if(wifi_radio_on()) lv_obj_add_state(g_sw, LV_STATE_CHECKED);
        else                lv_obj_clear_state(g_sw, LV_STATE_CHECKED);
    }
    screen_show(SCR_WIFI);
    if(wifi_radio_on()){
        char cs[64], cip[32];
        if(wifi_status(cs, sizeof cs, cip, sizeof cip) && cs[0]){ char sub[64]; snprintf(sub, sizeof sub, "Connected%s%s", cip[0] ? " \xC2\xB7 " : "", cip); hdr_set(WS_CONNECTED, cs, sub, g_cur_sig); }
        else hdr_set(WS_IDLE, "Not connected", "", 0);
    } else hdr_set(WS_OFF, "Wi-Fi is off", "", 0);
    start_scan();
}
