/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "command_spawn.h"
#include <fcntl.h>
#include "fwcaps.h"
#include "screens.h"
#include "zune_dial.h"
#include "braun.h"
#include "classic.h"
#include "theme.h"
#include "null_theme.h"
#include "simple_theme.h"
#include "curvelist.h"
#include "theme_kit.h"
#include "config.h"       /* cfg_get_int/cfg_set_int: persist the BT on/off intent */
#include "aptx_blob.h"    /* the aptX BlueALSA files carried inside mq_ui */
#include <sys/statvfs.h>
#include <stdio.h>
#include <spawn.h>
#include <dirent.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <time.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <time.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <sys/wait.h>
#include <poll.h>
#include <signal.h>

/* Bluetooth settings (SCR_BT) + a details screen (SCR_BT_INFO).
 * The BT chip (BCM43438 / AP6212, 2.4GHz-only) sits on UART /dev/ttyS0. RE of the
 * stock mq_player (NOT BSA - bsa_server is dead code for the wrong chip) shows the
 * stock production stack is ALSO bluez, but it FIRST downloads the chip firmware
 * patch with `brcm_patchram_plus ... --patchram <BCM4343A1...hcd> /dev/ttyS0` @3Mbaud
 * to create a working hci0 - nothing attaches hci0 at boot (bcmdhd.ko only loads the
 * driver/GPIOs). Without that patchram step inquiry/scan is broken, which is why our
 * earlier bluez-only bring-up scanned poorly. bt_enable() now mirrors stock exactly:
 * patchram -> /usr/project/bluetoothd (a2dp,avrcp,source) -> hciconfig up/piscan/class
 * -> bluetoothctl agent. `bluetoothctl` drives scan/pair/connect; `bluealsa` is the
 * a2dp-source audio engine (SBC/LDAC).  Layout matches the Wi-Fi page (control row in the
 * centre band; the list carries on/scanning/empty state; connected ✓ row taps
 * through to details).  NOTE: routing the player's audio onto a connected BT
 * sink is the stock mq_player's job (it has bluealsa support) - building the
 * profile here is stage 1; confirm real headphone playback with the user. */

static lv_obj_t *g_sw, *g_list, *g_info_list;
static lv_obj_t *g_hring, *g_hglyph, *g_hname, *g_hsub, *g_hknob;   /* g_hknob: Braun's status knob */   /* the state ring + name + line at the top */
static curvelist_t g_bcl;                                  /* curved device rows */
static lv_timer_t *g_scan_timer;
static lv_timer_t *g_scanwait_timer;   /* bt_open's non-destructive "wait for adapter then scan" poll */
static uint32_t    g_scanwait_start;
static lv_timer_t *g_bt_autoroute_timer;
static char g_sel_mac[20];     /* device selected for the details screen */
static char g_bt_autorouted[20];

static void start_scan(void);             /* fwd */
static void scan_timer_cb(lv_timer_t *t);  /* fwd */
static void scan_abort(void);             /* fwd */
static void scan_kick(void);              /* fwd - off-thread re-enumerate (no discovery window) */
static void bt_announce(const char *mac, const char *codec);   /* fwd - "Connected: <name> - <codec>" */
static void bt_announce_reset(void);      /* fwd */
static void bt_autoroute_start(void);      /* fwd */
static void bt_autoroute_stop(void);       /* fwd */
static void bt_remember_last(const char *mac);   /* fwd */
static void bt_reap_bounded(pid_t pid);          /* fwd */
static pid_t g_conn_pid;                          /* the running pair/connect attempt, 0 = none */
static char g_disc_mac[20]; static uint32_t g_disc_until;   /* N03: just disconnected: don't auto-route back to it */
static void conn_proc_stop(void){
    if(g_conn_pid > 0){
        if(waitpid(g_conn_pid, NULL, WNOHANG) == 0){ kill(-g_conn_pid, SIGTERM); kill(g_conn_pid, SIGTERM); bt_reap_bounded(g_conn_pid); }
        g_conn_pid = 0;
    }
}
static pid_t g_scan_pid;                          /* the running device search (bluetoothctl scan), 0 = none */
static void scan_proc_stop(void){                 /* end our device search: its BlueZ session goes with it */
    if(g_scan_pid > 0){
        if(waitpid(g_scan_pid, NULL, WNOHANG) == 0){ kill(-g_scan_pid, SIGTERM); kill(g_scan_pid, SIGTERM); bt_reap_bounded(g_scan_pid); }
        g_scan_pid = 0;
    }
}

/* The refresh glyph INSIDE the "Scanning" message spins while a scan runs. It lives
 * in g_list, so it must be stopped before g_list is cleaned (else the anim references
 * a freed object). scan_stop() is called at the top of every g_list-clearing path. */
static lv_obj_t *g_scan_icon;
static void spin_anim_cb(void *o, int32_t v){ lv_obj_set_style_transform_rotation((lv_obj_t*)o, v, 0); }
/* Squash and Disco ("B2" header): the icon on a disc, its shade says on (the theme's ink) or off (grey); the ring
 * around the disc is a faint track that spins an ink arc while Bluetooth turns on or scans. No state text. */
static int g_b2, g_b2_scanning, g_hstate;
static lv_obj_t *g_cdisc;
static void b2_ring(void);
static lv_color_t b2_accent(void){ return th_simple() ? TC(ACCENT_PRIMARY) : ui_current_accent(); }   /* Squash orange / Disco's accent */
static void scan_stop(void){
    if(g_scan_icon){ lv_anim_delete(g_scan_icon, spin_anim_cb); g_scan_icon = NULL; }
    if(g_b2 && g_b2_scanning){ g_b2_scanning = 0; b2_ring(); }
}

/* ---- header helpers (local copies) -------------------------------------- */

/* Children we killed but could not reap within ~100 ms (stuck in uninterruptible sleep) are parked here and
 * retried by later calls, never waited on. (Same idea as upstream diskOS 1.1.3.) */
#define BT_STUCK_MAX 8
static pid_t g_bt_stuck[BT_STUCK_MAX];
static pthread_mutex_t g_bt_stuck_mu = PTHREAD_MUTEX_INITIALIZER;
static void bt_reap_stuck(void){
    pthread_mutex_lock(&g_bt_stuck_mu);
    for(int i = 0; i < BT_STUCK_MAX; i++) if(g_bt_stuck[i] > 0 && waitpid(g_bt_stuck[i], NULL, WNOHANG) != 0) g_bt_stuck[i] = 0;
    pthread_mutex_unlock(&g_bt_stuck_mu);
}
static __thread int g_last_exit = -1;                    /* exit status of this thread's last run_cap_to command (-1: unknown) */
static void bt_reap_bounded(pid_t pid){
    for(int pass = 0; pass < 2; pass++){
        for(int i = 0; i < 20; i++){                         /* ~100 ms */
            int st = 0; pid_t r = waitpid(pid, &st, WNOHANG);
            if(r == pid){ g_last_exit = WIFEXITED(st) ? WEXITSTATUS(st) : -1; return; }
            if(r != 0) return;
            usleep(5000);
        }
        kill(-pid, SIGKILL); kill(pid, SIGKILL);            /* still there: make sure it is really being killed */
    }
    pthread_mutex_lock(&g_bt_stuck_mu);
    for(int i = 0; i < BT_STUCK_MAX; i++) if(g_bt_stuck[i] <= 0){ g_bt_stuck[i] = pid; break; }
    pthread_mutex_unlock(&g_bt_stuck_mu);
}
/* Run a shell command and capture its output, but never wait longer than `ms`: bluetoothctl blocks
 * indefinitely when bluetoothd is wedged, and an unbounded popen() here froze the UI (called from
 * timers) or left the device-list worker stuck forever, so the list never refreshed again. The child
 * gets its own process group so a timeout kills the whole pipeline. Returns bytes read, -1 on timeout. */
static int run_cap_to(const char *cmd, char *out, int cap, int ms){
    out[0] = 0;
    g_last_exit = -1;
    bt_reap_stuck();
    /* close-on-exec: a helper started at the same moment by another thread must not inherit this pipe's writer, or
     * this reader sees no end-of-file until that unrelated helper exits (false time-outs). The child's own copy is
     * the dup2() onto stdout below, which posix_spawn makes without the flag. */
    int fd[2]; if(pipe2(fd, O_CLOEXEC) != 0) return 0;
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
    if(rc != 0){ close(fd[0]); fprintf(stderr, "bt: spawn failed (%s): %s\n", strerror(rc), cmd); return 0; }
    int n = 0, timed_out = 0; uint32_t t0 = lv_tick_get();
    for(;;){
        int left = ms - (int)lv_tick_elaps(t0);
        if(left <= 0){ timed_out = 1; break; }
        struct pollfd p = { .fd = fd[0], .events = POLLIN };
        int pr = poll(&p, 1, left);
        if(pr < 0){ if(errno == EINTR) continue; break; }
        if(pr == 0){ timed_out = 1; break; }
        ssize_t r = read(fd[0], out + n, (size_t)(cap - 1 - n));
        if(r <= 0) break;                                   /* EOF: the command finished */
        n += (int)r; if(n >= cap - 1) break;
    }
    close(fd[0]);
    if(timed_out){ kill(-pid, SIGKILL); kill(pid, SIGKILL); fprintf(stderr, "bt: timed out after %d ms: %s\n", ms, cmd); }
    bt_reap_bounded(pid);                                   /* never an unbounded waitpid(): a child stuck in the kernel is parked, not waited on */
    out[n] = 0;
    return timed_out ? -1 : n;
}
static int run_cap(const char *cmd, char *out, int cap){ int n = run_cap_to(cmd, out, cap, 4000); return n < 0 ? 0 : n; }

/* ---- the state header: the ring says it at a glance - grey = off, a short spinning arc = turning on, a full
 * accent ring = on - with the connected device (or the state) under it ---------------------------------- */
static void hring_spin_exec(void *var, int32_t v){ lv_arc_set_rotation((lv_obj_t *)var, v % 360); }
static void b2_ring(void){
    if(!g_b2 || !g_hring) return;
    int spin = g_hstate == BT_TURNING_ON || g_b2_scanning;
    lv_anim_delete(g_hring, hring_spin_exec);
    lv_obj_set_style_arc_color(g_hring, b2_accent(), LV_PART_INDICATOR);
    lv_arc_set_rotation(g_hring, 270); lv_arc_set_value(g_hring, spin ? 220 : 0);
    if(spin){
        lv_anim_t a; lv_anim_init(&a); lv_anim_set_var(&a, g_hring); lv_anim_set_exec_cb(&a, hring_spin_exec);
        lv_anim_set_values(&a, 270, 270 + 360); lv_anim_set_duration(&a, 1100);
        lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE); lv_anim_start(&a);
    }
}
static void hdr_set(int state, const char *name, const char *sub){
    if(!g_hring) return;
    g_hstate = state;
    if(g_b2){
        lv_obj_set_style_text_color(g_hglyph, state == BT_ON ? TC(TEXT_PRIMARY) : TC(TEXT_DISABLED), 0);
        if(g_cdisc) lv_obj_set_style_bg_color(g_cdisc, state == BT_OFF ? TC(SURFACE) : TC(SURFACE_RAISED), 0);
        lv_label_set_text(g_hname, ""); lv_label_set_text(g_hsub, (sub && state == BT_OFF) ? sub : "");   /* only an error line */
        b2_ring();
        return;
    }
    if(g_hknob){                                                   /* Braun: the knob's pointer + lamp say it */
        br_knob_set_on(g_hknob, state == BT_ON);
        lv_label_set_text(g_hname, name ? name : ""); lv_label_set_text(g_hsub, sub ? sub : "");
        lv_obj_set_style_text_color(g_hname, state == BT_OFF ? TC(TEXT_SECONDARY) : TC(TEXT_PRIMARY), 0);
        return;
    }
    lv_anim_delete(g_hring, hring_spin_exec);
    lv_arc_set_rotation(g_hring, 270);
    lv_arc_set_value(g_hring, state == BT_ON ? 1000 : state == BT_TURNING_ON ? 260 : 0);
    lv_obj_set_style_arc_color(g_hring, ui_current_accent(), LV_PART_INDICATOR);
    if(state == BT_TURNING_ON){
        lv_anim_t a; lv_anim_init(&a); lv_anim_set_var(&a, g_hring); lv_anim_set_exec_cb(&a, hring_spin_exec);
        lv_anim_set_values(&a, 270, 270 + 360); lv_anim_set_duration(&a, 1000);
        lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE); lv_anim_start(&a);
    }
    lv_obj_set_style_text_color(g_hglyph, state == BT_OFF ? TC(TEXT_DISABLED) : TC(TEXT_PRIMARY), 0);
    lv_label_set_text(g_hname, name ? name : "");
    lv_obj_set_style_text_color(g_hname, state == BT_OFF ? TC(TEXT_SECONDARY) : TC(TEXT_PRIMARY), 0);
    lv_label_set_text(g_hsub, sub ? sub : "");
}

/* Main-thread probes share the posix_spawn runner used by device enumeration.
 * fork() duplicates the large UI address space and can fail on the player while
 * the same command succeeds from a small SSH shell. Keep the existing deadline
 * and any output captured before it; never turn a timeout into an unbounded wait. */
static int run_cap_bounded(const char *cmd, char *out, int cap, int timeout_ms){
    if(cap < 1) return 0;
    int n = run_cap_to(cmd, out, cap, timeout_ms);
    return n < 0 ? (int)strlen(out) : n;
}
/* Shared entry for other modules (wifi.c radio reconciliation): the same bounded capture - the command runs in its
 * own process group and is killed at timeout_ms, so a wedged tool can never block the UI thread. */
int ui_run_cap_bounded(const char *cmd, char *out, int cap, int timeout_ms){ return run_cap_bounded(cmd, out, cap, timeout_ms); }

/* a single status message (Scanning / off / empty) - centered in the list area */
static void list_msg(const char *m){
    if(!g_list) return;
    scan_stop();
    lv_obj_clean(g_list);
    lv_obj_set_flex_align(g_list, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *e = lv_label_create(g_list);
    lv_label_set_text(e, m);
    lv_obj_set_style_text_color(e, TC(TEXT_MUTED), 0);
    lv_obj_set_style_text_font(e, g_b2 ? TF(UI_18) : TF(UI_14), 0);
    if(g_b2){ lv_label_set_long_mode(e, LV_LABEL_LONG_WRAP); lv_obj_set_width(e, 240); lv_obj_set_style_text_align(e, LV_TEXT_ALIGN_CENTER, 0); }
}
/* "Scanning" + a spinning refresh glyph, centered in the list area */
static void list_msg_scanning(void){
    if(!g_list) return;
    scan_stop();
    lv_obj_clean(g_list);
    lv_obj_set_flex_align(g_list, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    if(g_b2){                                                   /* the ring spins; one large word in the middle */
        lv_obj_t *t = lv_label_create(g_list);
        lv_label_set_text(t, "Scanning...");
        lv_obj_set_style_text_color(t, TC(TEXT_SECONDARY), 0);
        lv_obj_set_style_text_font(t, TF(UI_28), 0);
        g_b2_scanning = 1; b2_ring();
        return;
    }
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

/* A bring-up in flight leaves /tmp/bt_enabling; one older than this is a dead bring-up (its subshell
 * was killed or hung), and a stale marker used to make every later "turn on" silently do nothing. */
#define BT_ENABLE_MAX_S 45
static int bt_enabling(void){
    struct stat st;
    if(stat("/tmp/bt_enabling", &st) != 0) return 0;
    if(time(NULL) - st.st_mtime > BT_ENABLE_MAX_S){ unlink("/tmp/bt_enabling"); return 0; }
    return 1;
}
static int proc_running(const char *name){                  /* cheap pidof: /proc/<pid>/comm, no spawn */
    DIR *d = opendir("/proc"); if(!d) return 0;
    struct dirent *e; int found = 0;
    while(!found && (e = readdir(d))){
        if(e->d_name[0] < '0' || e->d_name[0] > '9') continue;
        char p[64], c[32]; snprintf(p, sizeof p, "/proc/%.40s/comm", e->d_name);
        FILE *f = fopen(p, "r"); if(!f) continue;
        if(fgets(c, sizeof c, f)){ c[strcspn(c, "\n")] = 0; found = !strcmp(c, name); }
        fclose(f);
    }
    closedir(d);
    return found;
}
static int rfkill_bt_unblocked(void){
    int found = 0;
    for(int i = 0; i < 12; i++){
        char p[64]; snprintf(p, sizeof p, "/sys/class/rfkill/rfkill%d/type", i);
        FILE *f = fopen(p, "r"); if(!f) continue;
        char t[16] = {0}; char *r = fgets(t, sizeof t, f); fclose(f);
        if(!r || strncmp(t, "bluetooth", 9) != 0) continue;
        found = 1;
        snprintf(p, sizeof p, "/sys/class/rfkill/rfkill%d/soft", i);
        f = fopen(p, "r"); if(!f) continue;
        int soft = 1; if(fscanf(f, "%d", &soft) != 1) soft = 1; fclose(f);
        if(soft != 0) return 0;
    }
    return found ? 1 : cfg_get_int("bt_on", 0);
}
/* BT_OFF / BT_TURNING_ON / BT_ON - what every surface shows (Home icon, Quick Settings, the BT screen).
 * "On" means the radio is unblocked AND bluetoothd runs: an unblocked radio after a failed bring-up is
 * not on. Cheap (/proc + /sys reads, no process spawns), so it can be polled once a second. */
int bt_state(void){
    if(bt_enabling()) return BT_TURNING_ON;
    return (rfkill_bt_unblocked() && proc_running("bluetoothd")) ? BT_ON : BT_OFF;
}

/* powered = bluetoothd up AND adapter Powered: yes */
static int bt_on(void){
    if(!proc_running("bluetoothd")) return 0;   /* /proc read; no child process needed */
    /* bluetoothctl talks to bluetoothd over D-Bus and can stall if the daemon is wedged - bound it so
     * this probe (called from UI poll timers) never freezes the main thread. */
    char s[2048]; run_cap_bounded("bluetoothctl show 2>/dev/null", s, sizeof s, 250);
    return strstr(s, "Powered: yes") != NULL;
}

/* Cheap "is the BT radio enabled?" for the status icon + QS tile: reads the bluetooth rfkill soft-block
 * from /sys (no process spawn). Reflects reality regardless of who set it - diskOS bt_disable() rfkill-
 * blocks, and the stock boot can bring BT up per its own SYSCONFIG - unlike the persisted cfg intent,
 * which goes stale when the stock firmware enables BT out from under us. Falls back to the intent if no
 * bluetooth rfkill node exists. */
int bt_radio_on(void){ return bt_state() == BT_ON; }
__attribute__((unused)) static int bt_radio_on_legacy(void){
    int found = 0;
    for(int i = 0; i < 12; i++){
        char p[64]; snprintf(p, sizeof p, "/sys/class/rfkill/rfkill%d/type", i);
        FILE *f = fopen(p, "r"); if(!f) continue;
        char t[16] = {0}; char *r = fgets(t, sizeof t, f); fclose(f);
        if(!r || strncmp(t, "bluetooth", 9) != 0) continue;
        found = 1;
        snprintf(p, sizeof p, "/sys/class/rfkill/rfkill%d/soft", i);
        f = fopen(p, "r"); if(!f) continue;
        int soft = 1; if(fscanf(f, "%d", &soft) != 1) soft = 1; fclose(f);
        if(soft != 0) return 0;   /* any bluetooth rfkill soft-blocked -> BT off */
    }
    return found ? 1 : cfg_get_int("bt_on", 0);
}

/* ---- enable / disable --------------------------------------------------- */
/* idempotent: ensure the pairing agent + a2dp-source audio endpoint are up.
 * Must run whenever the radio is on - without bt-agent pairing fails, and
 * without bluealsa there is no audio sink for the player to route to. */
/* a2dp-source audio engine. --sbc-quality=medium (bit-pool ~33) keeps SBC encode load
 * under this X2000 CPU's ceiling: stock's default-quality stereo SBC stutters, but medium
 * quality plays clean STEREO with headroom (on-device: ~86% idle). bluetoothctl's own agent
 * (set up in bt_enable) handles just-works pairing, so no separate bt-agent. */
/* ---- Born on the right codec ------------------------------------------------------------------------
 * At connection BlueALSA (v4.0.0 on the Disc, started by Disco) takes the first enabled codec in its own order, and
 * SBC comes first - so every link used to start on SBC and then switch, which rebuilds the link (a short silence).
 * Tested on the Disc: started with --codec=-sbc, headphones connected straight on LDAC and stayed on it through
 * Disco's route and playback. So when the last-used headphones are known to support the chosen codec, BlueALSA is
 * started preferring it: AAC chosen -> SBC and LDAC off at connection; LDAC chosen -> SBC off, with the chosen LDAC
 * quality. The later codec step then finds it "already" on that codec: no switch, no pause. Unknown headphones and
 * other choices start as before. Connecting to headphones not known to support it first restarts BlueALSA with the
 * normal settings (bt_codec_prepare_steps). Each headphone's codec list is remembered from its first connection. */
int bt_codec_pick(int choice, unsigned avail, int avail_known);   /* below */
static unsigned bt_codec_need(int v);                              /* below */
/* Codec bits, as remembered per headphones and read from BlueALSA's "Available codecs" */
#define BTC_SBC    1u           /* mandatory for every A2DP sink: a list without it is incomplete, never "full" */
#define BTC_AAC    2u
#define BTC_LDAC   4u
#define BTC_APTX   8u
#define BTC_APTXHD 16u
static void bt_mac_cfg_key(const char *prefix, const char *mac, char *k, int cap){
    char m[20] = {0}; int j = 0;
    for(int i = 0; mac && mac[i] && j < 12; i++) if(mac[i] != ':') m[j++] = mac[i];
    snprintf(k, cap, "%s%s", prefix, m);
}
static void bt_caps_key(const char *mac, char *k, int cap){ bt_mac_cfg_key("btcaps_", mac, k, cap); }   /* codecs offered */
static int bt_caps_get(const char *mac){              /* remembered codec bits (BTC_*), -1 unknown */
    if(!mac || !mac[0]) return -1;
    char k[32]; bt_caps_key(mac, k, sizeof k); return cfg_get_int(k, -1);
}
/* Whether these headphones' list was seen with every codec enabled (a normal start). A BlueALSA started on one codec
 * lists only what it allowed, so its list is partial: it may add to the memory but never replace it - otherwise one
 * AAC-only connection would remember "AAC only" and keep every later start on AAC (seen on the Disc, Nov 2026). */
#define BT_FULL_APTX 2   /* the whole list, seen by the aptX build (1 = an earlier rule that also trusted stock's list) */
static int bt_caps_full(const char *mac){ char k[32]; bt_mac_cfg_key("btfull_", mac, k, sizeof k); return cfg_get_int(k, 0); }
static void bt_caps_write(const char *mac, unsigned avail){       /* 3.3.1's: replace the list as read */
    char k[32]; bt_caps_key(mac, k, sizeof k);
    if(cfg_get_int(k, -1) != (int)avail) cfg_set_int(k, (int)avail);
}
static void bt_caps_set(const char *mac, unsigned avail){        /* the full list, from a normal start */
    char k[32]; bt_caps_write(mac, avail);
    bt_mac_cfg_key("btfull_", mac, k, sizeof k);
    if(cfg_get_int(k, 0) != BT_FULL_APTX) cfg_set_int(k, BT_FULL_APTX);
}
static void bt_caps_add(const char *mac, unsigned bits){          /* seen working or listed under a restricted start */
    int caps = bt_caps_get(mac); unsigned nb = (unsigned)(caps < 0 ? 0 : caps) | bits;
    if(caps < 0 || (unsigned)caps != nb){ char k[32]; bt_caps_key(mac, k, sizeof k); cfg_set_int(k, (int)nb); }
}
/* Known-good: a codec that has PLAYED on these headphones (about 20 s of music, see the crash guard below). Only a
 * known-good codec is ever started on its own (born); anything else starts with the normal order and switches.
 * A codec that merely didn't confirm in time is NOT remembered as bad any more: it only steps down for that one
 * connection, and the next connection tries the best again (3.3.x's persistent "bad" marks kept pairs on AAC). */
static int bt_good_get(const char *mac){ char k[32]; bt_mac_cfg_key("btgood_", mac, k, sizeof k); return cfg_get_int(k, 0); }
static void bt_good_add(const char *mac, unsigned bit){
    if(!mac || !mac[0] || !bit) return;
    char k[32]; bt_mac_cfg_key("btgood_", mac, k, sizeof k);
    int g = cfg_get_int(k, 0); if(!(g & (int)bit)) cfg_set_int(k, g | (int)bit);
    bt_caps_add(mac, bit);
}
/* Skipped: a codec the Disc restarted on while it started playing on these headphones (the crash guard). Skipped
 * until the user chooses Bluetooth Codec again (bt_skip_gen), so a reboot loop can't happen and nothing stays off
 * for good. Stored as bits | generation << 8. */
static unsigned bt_skip_get(const char *mac){
    char k[32]; bt_mac_cfg_key("btskip_", mac, k, sizeof k);
    int v = cfg_get_int(k, 0);
    return (v >> 8) == cfg_get_int("bt_skip_gen", 0) ? (unsigned)(v & 0xff) : 0u;
}
static void bt_skip_add(const char *mac, unsigned bit){
    char k[32]; bt_mac_cfg_key("btskip_", mac, k, sizeof k);
    cfg_set_int(k, (int)((bt_skip_get(mac) | bit) & 0xff) | cfg_get_int("bt_skip_gen", 0) << 8);
}
/* Two BlueALSAs (with Settings > aptX on): FiiO's own (kind 0) for SBC, AAC and LDAC - LDAC ONLY ever runs on it -
 * and Disco's aptX build (kind 1) for aptX / aptX HD, always started with LDAC off: LDAC audio through it hung the
 * Disc until the watchdog rebooted it (Nov 2026). Its patched codec list still shows every codec a pair has, so a
 * pair's whole list is learned on it. aptX off: FiiO's only, as 3.3.1. */
static const char *bt_alsa_aptx_bin(void);   /* below: the aptX BlueALSA, NULL when not installed */
/* The saved codec choice: 0 SBC, 1 AAC, 2-4 aptX/LDAC (LDAC / aptX HD / aptX, whichever the headphones have; the number is
 * the LDAC quality). 5 and 6 (aptX / aptX HD, from 3.4.0 test builds) are no choice any more: read as aptX/LDAC Standard. */
static int bt_codec_choice(void){
    int c = cfg_get_int("bt_codec", 0);
    return c == 5 || c == 6 ? 3 : c < 0 || c > 6 ? 0 : c;
}
/* Codecs these headphones can be given: aptX only with the aptX BlueALSA, never one the crash guard skipped. */
static unsigned bt_usable(const char *mac, unsigned caps){
    if(!bt_alsa_aptx_bin()) caps &= ~(BTC_APTX | BTC_APTXHD);
    return caps & ~bt_skip_get(mac);
}
/* What each BlueALSA can run: FiiO's no aptX, ours no LDAC. */
static unsigned bt_kind_can(int kind){ return kind == 1 ? ~BTC_LDAC : ~(BTC_APTX | BTC_APTXHD); }
static int bt_kind_for_v(int v){ return v == 5 || v == 6; }     /* the BlueALSA a codec value needs */
/* The BlueALSA for these headphones: ours only when aptX is what they will get, or to learn their whole list. */
static int bt_kind_for(const char *mac){
    if(!bt_alsa_aptx_bin() || fw_os_ver() != 257) return 0;
    int choice = bt_codec_choice();
    if(choice < 2) return 0;                                       /* SBC / AAC chosen: FiiO's, as 3.3.1 */
    int caps = bt_caps_get(mac);
    if(caps >= 0 && (bt_usable(mac, (unsigned)caps) & BTC_LDAC)) return 0;   /* LDAC tops everything: FiiO's */
    if(caps < 0 || bt_caps_full(mac) != BT_FULL_APTX || !(caps & BTC_SBC)) return 1;   /* list not whole: learn it */
    return bt_kind_for_v(bt_codec_pick(choice, bt_usable(mac, (unsigned)caps), 1));
}
/* The codec BlueALSA should be born on for these headphones (0 normal start, 1 AAC, 2 LDAC, 3 aptX, 4 aptX HD): only
 * a codec that has already played on them; anything else starts normally and switches after connecting. */
static int bt_born_for(const char *mac){
    if(fw_os_ver() != 257) return 0;                   /* the codec setting only applies on V2.57 */
    int caps = bt_caps_get(mac); if(caps < 0) return 0;
    int kind = bt_kind_for(mac);
    if(kind == 1 && (bt_caps_full(mac) != BT_FULL_APTX || !(caps & BTC_SBC))) return 0;
    int v = bt_codec_pick(bt_codec_choice(), bt_usable(mac, (unsigned)caps) & bt_kind_can(kind), 1);
    unsigned bit = bt_codec_need(v);
    if(!bit || !((unsigned)bt_good_get(mac) & bit)) return 0;
    return v == 1 ? 1 : v >= 2 && v <= 4 ? 2 : v == 5 ? 3 : v == 6 ? 4 : 0;
}
#ifndef BT_PROC_DIR
#define BT_PROC_DIR "/proc"
#endif
#ifndef BT_SYSBT_DIR
#define BT_SYSBT_DIR "/sys/class/bluetooth"
#endif
static const char *bt_ldac_q(void){                    /* LDAC quality for the chosen LDAC level */
    int c = bt_codec_choice(); return c == 2 ? "mobile" : c == 4 ? "high" : "standard";
}
/* ---- The aptX BlueALSA (opt-in) ----------------------------------------------------------------------------------
 * Stock BlueALSA 4.0.0 on the Disc is built with SBC, AAC and LDAC only. The aptX build is the same upstream 4.0.0
 * (MIT) with aptX and aptX HD enabled through libfreeaptx (LGPL-2.1, shipped next to it as its own shared library;
 * the Disc's libaptx / libaptxHD wrap FFmpeg, which has no aptX encoder there). LDAC and fdk-aac are the Disc's own.
 * The file is called "bluealsa" in its own folder, so killall/pidof and the /proc reading below treat
 * it exactly like stock. Not installed -> everything below is byte-for-byte the stock behaviour. */
#ifndef BT_APTX_BINS
#define BT_APTX_BINS "/opt/diskos/libexec/aptx/bluealsa", "/usr/data/diskos-aptx/bluealsa"
#endif
#ifndef BT_APTX_DIR
#define BT_APTX_DIR "/usr/data/diskos-aptx"
#endif
static const char *bt_alsa_aptx_bin(void){
    if(!cfg_get_int("bt_aptx", 0)) return NULL;          /* Settings > aptX (experimental): off = stock, as 3.3.1 */
    if(fw_os_ver() != 257) return NULL;                    /* built against V2.57's libraries */
    static const char *const bins[] = { BT_APTX_BINS };
    for(size_t i = 0; i < sizeof bins / sizeof *bins; i++) if(access(bins[i], X_OK) == 0) return bins[i];
    return NULL;
}
static void bt_alsa_args_for(char *out, int cap, int born, int aptx){
    static const char *const stock[] = { "--codec=sbc", "--codec=-sbc --codec=-ldac", "--codec=-sbc" };
    static const char *const ax[] = {                                                    /* LDAC always off on ours */
        "--codec=sbc --codec=-ldac --codec=aptX --codec=aptX-HD",                        /* 0 normal */
        "--codec=-sbc --codec=-ldac --codec=-aptX --codec=-aptX-HD",                     /* 1 AAC */
        "--codec=sbc --codec=-ldac --codec=aptX --codec=aptX-HD",                        /* (2 LDAC: never here) */
        "--codec=-sbc --codec=-aac --codec=-ldac --codec=aptX --codec=-aptX-HD",         /* 3 aptX */
        "--codec=-sbc --codec=-aac --codec=-ldac --codec=-aptX --codec=aptX-HD" };      /* 4 aptX HD */
    if(born < 0 || born > 4 || (!aptx && born > 2) || (aptx && born == 2)) born = 0;
    snprintf(out, cap, "-S --device=hci0 --profile=a2dp-source --sbc-quality=medium --ldac-abr --ldac-quality=%s "
             "%s --initial-volume=48", bt_ldac_q(), aptx ? ax[born] : stock[born]);
}
static void bt_alsa_args(char *out, int cap, int born){ bt_alsa_args_for(out, cap, born, 0); }   /* stock BlueALSA */
/* The shell command that starts BlueALSA `kind` born as `born` (output redirection is the caller's): FiiO's exactly as
 * 3.3.1, or the aptX build (kind 1, only with Settings > aptX (experimental) on and the files in place). */
static void bt_alsa_cmd(char *out, int cap, int born, int kind){
    char a[256]; const char *bin = kind == 1 ? bt_alsa_aptx_bin() : NULL;
    if(!bin){ bt_alsa_args(a, sizeof a, born); snprintf(out, cap, "bluealsa %s", a); return; }
    bt_alsa_args_for(a, sizeof a, born, 1);
    snprintf(out, cap, "%s %s", bin, a);
}
/* What the RUNNING BlueALSA was started with, read from its command line in /proc (no process started): 0 normal,
 * 1 born on AAC, 2 born on LDAC, -1 none running; q gets its --ldac-quality. The truth, whoever started it and
 * whatever Disco intended (review 2.6.2 b2, finding 2). */
static int bt_alsa_actual(char *q, int qcap){
    if(q && qcap > 0) q[0] = 0;
    DIR *d = opendir(BT_PROC_DIR); if(!d) return -1;
    struct dirent *e; int born = -1;
    while(born < 0 && (e = readdir(d))){
        if(e->d_name[0] < '1' || e->d_name[0] > '9') continue;
        char p[320], comm[32] = ""; snprintf(p, sizeof p, BT_PROC_DIR "/%s/comm", e->d_name);
        FILE *f = fopen(p, "r"); if(!f) continue;
        if(!fgets(comm, sizeof comm, f)) comm[0] = 0;
        fclose(f);
        if(strncmp(comm, "bluealsa\n", 9) && strcmp(comm, "bluealsa")) continue;
        snprintf(p, sizeof p, BT_PROC_DIR "/%s/cmdline", e->d_name);
        char cl[1024]; size_t n = 0;
        if((f = fopen(p, "r"))){ n = fread(cl, 1, sizeof cl - 2, f); fclose(f); }
        cl[n] = 0; for(size_t i = 0; i < n; i++) if(!cl[i]) cl[i] = ' ';
        strcat(cl, " ");                                   /* every argument ends in a space: whole-word matches */
        int no_sbc = strstr(cl, "--codec=-sbc ") != NULL, no_ldac = strstr(cl, "--codec=-ldac ") != NULL;
        born = !no_sbc ? 0 : strstr(cl, "--codec=aptX-HD ") ? 4 : strstr(cl, "--codec=aptX ") ? 3 : no_ldac ? 1 : 2;
        const char *lq = strstr(cl, "--ldac-quality=");
        if(lq && q){ lq += 15; int k = 0; while(lq[k] && lq[k] != ' ' && k < qcap - 1){ q[k] = lq[k]; k++; } q[k] = 0; }
    }
    closedir(d);
    return born;
}
/* Which BlueALSA is running: 1 the aptX build, 0 FiiO's, -1 can't tell (none running, or its /proc exe unreadable) -
 * "can't tell" never triggers anything. The stock player starts FiiO's itself at boot. */
static int bt_alsa_kind_running(void){
    DIR *d = opendir(BT_PROC_DIR); if(!d) return -1;
    struct dirent *e; int r = -1;
    while((e = readdir(d))){
        if(e->d_name[0] < '1' || e->d_name[0] > '9') continue;
        char p[320], comm[32] = ""; snprintf(p, sizeof p, BT_PROC_DIR "/%s/comm", e->d_name);
        FILE *f = fopen(p, "r"); if(!f) continue;
        if(!fgets(comm, sizeof comm, f)) comm[0] = 0;
        fclose(f);
        if(strncmp(comm, "bluealsa\n", 9) && strcmp(comm, "bluealsa")) continue;
        char exe[256]; snprintf(p, sizeof p, BT_PROC_DIR "/%s/exe", e->d_name);
        ssize_t n = readlink(p, exe, sizeof exe - 1);
        if(n > 0){
            exe[n] = 0;
            char *del = strstr(exe, " (deleted)"); if(del) *del = 0;   /* replaced on disk while running: same binary */
            r = strstr(exe, "aptx/bluealsa") != NULL;
        }
        break;
    }
    closedir(d);
    return r;
}
/* Is the running BlueALSA started the way `want` asks (incl. the LDAC quality)? A missing daemon counts as "no". */
static int bt_alsa_is(int want){
    char q[24]; int born = bt_alsa_actual(q, sizeof q);
    return born == want && (want != 2 || !strcmp(q, bt_ldac_q()));
}
/* Bluetooth connections the kernel has right now (/sys/class/bluetooth/hci0:<handle>, one per link; no process):
 * evidence independent of BlueALSA. -1 when the kernel doesn't show them (then nothing is restarted on a guess). */
static int bt_acl_links(void){
    DIR *d = opendir(BT_SYSBT_DIR); if(!d) return -1;
    struct dirent *e; int n = 0, hci = 0;
    while((e = readdir(d))){ if(!strncmp(e->d_name, "hci0", 4)){ hci = 1; if(e->d_name[4] == ':') n++; } }
    closedir(d);
    return hci ? n : -1;
}
/* Shell steps that restart BlueALSA as `born` and wait (at most 3 s) until it answers, for use inside a background
 * script: the UI never waits for it (review finding 3). */
static void bt_alsa_restart_steps(char *out, int cap, int born, int kind){
    char a[768]; bt_alsa_cmd(a, sizeof a, born, kind);
    snprintf(out, cap, "killall bluealsa 2>/dev/null; sleep 0.3; (%s >/tmp/bluealsa.log 2>&1 &); "
             "i=0; while [ $i -lt 15 ] && ! bluealsa-cli list-pcms >/dev/null 2>&1; do sleep 0.2; i=$((i+1)); done; ", a);
}
static void bt_alsa_restart_async(int born, int kind, const char *why){
    char steps[1024], cmd[1100]; bt_alsa_restart_steps(steps, sizeof steps, born, kind);
    snprintf(cmd, sizeof cmd, "( %s) >/dev/null 2>&1 &", steps);
    system(cmd);
    static const char *const first[] = { "SBC", "AAC", "LDAC", "aptX", "aptX HD" };
    fprintf(stderr, "bt codec: %s BlueALSA restarting (%s first): %s\n", kind == 1 ? "aptX" : "FiiO's",
            first[born >= 0 && born <= 4 ? born : 0], why); fflush(stderr);
}
static void bt_ensure_services(void){
    const char *last = cfg_get_str("bt_last", "");
    int kind = bt_kind_for(last);
    if(bt_alsa_actual(NULL, 0) >= 0){                    /* running: leave it, its real arguments are read when needed */
        int kr = bt_alsa_kind_running();
        if(kr >= 0 && kr != kind && bt_acl_links() == 0)   /* ...unless it is the other binary and nothing is connected */
            bt_alsa_restart_async(bt_born_for(last), kind, "the BlueALSA for the usual headphones");
        return;
    }
    char a[768], cmd[900];
    bt_alsa_cmd(a, sizeof a, bt_born_for(last), kind);
    snprintf(cmd, sizeof cmd, "pidof bluealsa >/dev/null 2>&1 || %s >/tmp/bluealsa.log 2>&1 &", a);
    system(cmd);
}
/* The headphones have really gone: only when the kernel shows NO Bluetooth connection, and the running BlueALSA isn't
 * started the way the usual headphones (and the chosen LDAC quality) want, restart it in the background so their next
 * connection is born on the right codec. "Can't tell" never restarts anything (review findings 3 and 4). */
static void bt_alsa_rearm_born(void){
    if(bt_acl_links() != 0) return;
    const char *last = cfg_get_str("bt_last", "");
    int want = bt_born_for(last), kind = bt_kind_for(last), kr = bt_alsa_kind_running();
    if(bt_alsa_actual(NULL, 0) < 0 || (bt_alsa_is(want) && (kr < 0 || kr == kind))) return;
    bt_alsa_restart_async(want, kind, "ready for the usual headphones");
}
/* Before connecting to these headphones: if the running BlueALSA is the other binary, or restricted to a codec that
 * isn't theirs, the connect script first restarts the right one and waits until it answers (fills `steps`, returns 1). */
static int bt_codec_prepare_steps(const char *mac, char *steps, int cap){
    if(cap > 0) steps[0] = 0;
    int born = bt_alsa_actual(NULL, 0), kind = bt_kind_for(mac), kr = bt_alsa_kind_running(), want = bt_born_for(mac);
    if(born < 0 || ((born == 0 || want == born) && (kr < 0 || kr == kind))) return 0;
    bt_alsa_restart_steps(steps, cap, want, kind);
    fprintf(stderr, "bt codec: %s BlueALSA before connecting %s\n", kind == 1 ? "aptX" : "FiiO's", mac); fflush(stderr);
    return 1;
}
/* ---- A2DP codec choice (parity C17/C18) ----------------------------------------------------------------
 * Stock V2.57 picks the codec with the player command 06b3 (VALUE1: 0 SBC, 1 AAC, 2/3/4 LDAC mobile/standard/high;
 * mq_player handler 0x4f0980 maps it to the worker 0x4112b4, which reads /usr/data/bt_codec, then calls BlueALSA
 * SelectCodec over D-Bus; LDAC quality goes through /usr/data/bt_pipe_recv). BlueALSA itself is left as stock
 * launches it - on the device its status advertises SBC AAC LDAC endpoints whatever --codec says - so a choice needs
 * no daemon restart, and this only ever sends the frame diskOS already sent, with another VALUE1. The choice is
 * read on the next route to a Bluetooth device (connect/reconnect), once BlueALSA has that device's PCM. DEVICE-UNVERIFIED with headphones. */
/* Settings offers SBC, AAC and aptX/LDAC (2-4, by LDAC quality): LDAC and aptX are never chosen between, both always beat
 * AAC and SBC. Values 5 and 6 (aptX, aptX HD) are what aptX/LDAC applies on aptX headphones, never a choice: they are not
 * stock VALUE1s (mq_player has no aptX), so they are asked of BlueALSA directly (bluealsa-cli codec: the same
 * SelectCodec call mq_player makes) and need the aptX BlueALSA. */
/* "aptX/LDAC <quality>": the best of LDAC / aptX HD / aptX the headphones have; the quality is LDAC's (aptX has none).
 * Plain ASCII: some themes' Settings fonts have no middle dot (it showed as a box). */
const char *const BT_CODEC_LABEL[BT_CODEC_N] = { "SBC", "AAC", "aptX/LDAC Mobile", "aptX/LDAC Standard",
                                                 "aptX/LDAC High", "aptX", "aptX HD" };
static int bt_mac_valid(const char *mac);
static int bt_codec_frame_value(const char *mac, int v, char *out, int cap);
static unsigned bt_codec_need(int v){
    return v == 1 ? BTC_AAC : (v >= 2 && v <= 4) ? BTC_LDAC : v == 5 ? BTC_APTX : v == 6 ? BTC_APTXHD : 0u;
}
/* How many codec choices Settings offers: SBC, AAC and the three aptX/LDAC (aptX / aptX HD are applied, never chosen). */
int bt_codec_choices(void){ return BT_CODEC_N - 2; }
/* Settings' hint for a choice: what it ends up as, highest first (the same order as bt_codec_pick). */
const char *bt_codec_hint(int v){
    static char b[160];
    if(v == 5 || v == 6) v = 3;                                   /* old aptX choices: aptX/LDAC Standard */
    if(v >= 2 && v <= 4 && !bt_alsa_aptx_bin())
        snprintf(b, sizeof b, "LDAC if the headphones have it, then AAC, then SBC. aptX: turn on aptX (experimental).");
    else if(v >= 2 && v <= 4)
        snprintf(b, sizeof b, "LDAC or aptX HD / aptX, whichever the headphones have, then AAC, then SBC. The quality is LDAC's.");
    else if(v == 1) snprintf(b, sizeof b, "Best effort: AAC, then SBC.");
    else snprintf(b, sizeof b, "SBC works with all headphones.");
    return b;
}
/* A choice's codec as BlueALSA spells it, upper case (as "Selected codec:" is compared), and its BTC_ bit. */
static const char *bt_codec_word(int v){ return v == 1 ? "AAC" : (v >= 2 && v <= 4) ? "LDAC" : v == 5 ? "APTX" : v == 6 ? "APTX-HD" : "SBC"; }
/* The codec to apply: the best one the sink offers, never above the choice. Highest first:
 *   aptX/LDAC: LDAC (the chosen quality) -> aptX HD -> aptX -> AAC -> SBC
 *   AAC:   AAC -> SBC          SBC: SBC
 * so headphones with LDAC get LDAC and aptX headphones get aptX, whatever the LDAC quality, and neither ever ends on
 * AAC while it has one of them. SBC when the sink's list is unknown. aptX is only listed with the aptX BlueALSA.
 * 5 / 6 passed as the choice (a step down from aptX HD / aptX) count as aptX/LDAC. */
int bt_codec_pick(int choice, unsigned avail, int avail_known){
    if(choice <= 0 || choice >= BT_CODEC_N) return 0;         /* SBC chosen */
    if(!avail_known) return 0;                                /* unknown: SBC */
    if(choice >= 2){                                          /* aptX/LDAC: LDAC or aptX, whichever is there */
        if(avail & BTC_LDAC) return choice <= 4 ? choice : 3;
        if(avail & BTC_APTXHD) return 6;
        if(avail & BTC_APTX) return 5;
    }
    return (avail & BTC_AAC) ? 1 : 0;                         /* then AAC, then SBC */
}
/* The codecs below v in that order: what is left to try once v did not get through. */
static unsigned bt_codec_below(int v){
    unsigned n = bt_codec_need(v);
    return n == BTC_LDAC ? BTC_APTXHD | BTC_APTX | BTC_AAC | BTC_SBC : n == BTC_APTXHD ? BTC_APTX | BTC_AAC | BTC_SBC
         : n == BTC_APTX ? BTC_AAC | BTC_SBC : n == BTC_AAC ? BTC_SBC : 0u;
}
/* Codecs a sink offers, from `bluealsa-cli info <pcm>`: the words on the "Available codecs:" line and on the lines
 * after it that carry no ':'. *known = 0 when that header is missing (unparseable/failed query). */
unsigned bt_codec_parse_avail(const char *info, int *known){
    unsigned m = 0;
    if(known) *known = 0;
    const char *h = info ? strstr(info, "Available codecs:") : NULL;
    if(!h) return 0;
    if(known) *known = 1;
    const char *p = h + 17;
    for(int first = 1; *p;){
        const char *e = strchr(p, '\n'); size_t n = e ? (size_t)(e - p) : strlen(p);
        if(!first && memchr(p, ':', n)) break;
        first = 0;
        for(size_t i = 0; i < n;){
            size_t j = i;
            while(j < n && ((p[j]>='A'&&p[j]<='Z')||(p[j]>='a'&&p[j]<='z')||(p[j]>='0'&&p[j]<='9')||p[j]=='-')) j++;
            if(j - i == 3 && !strncasecmp(p + i, "SBC", 3)) m |= BTC_SBC;
            else if(j - i == 3 && !strncasecmp(p + i, "AAC", 3)) m |= BTC_AAC;
            else if(j - i == 4 && !strncasecmp(p + i, "LDAC", 4)) m |= BTC_LDAC;
            else if(j - i == 4 && !strncasecmp(p + i, "APTX", 4)) m |= BTC_APTX;
            else if(j - i == 7 && !strncasecmp(p + i, "APTX-HD", 7)) m |= BTC_APTXHD;
            i = j > i ? j : i + 1;
        }
        if(!e) break;
        p = e + 1;
    }
    return m;
}
/* Shell command that prints "PCM=<path>" then the A2DP source PCM's info for a device (MAC already validated);
 * it prints nothing while BlueALSA has no such PCM. */
static int bt_codec_query_cmd(const char *mac, char *out, int cap){
    if(!bt_mac_valid(mac)) return -1;
    char d[24]; int n = 0;
    for(int i = 0; i < 17; i++) d[n++] = mac[i] == ':' ? '_' : mac[i];
    d[n] = 0;
    int r = snprintf(out, cap, "p=$(bluealsa-cli list-pcms 2>/dev/null | grep -i 'dev_%s/a2dpsrc' | head -n 1); "
                               "[ -n \"$p\" ] && { echo \"PCM=$p\"; bluealsa-cli info \"$p\" 2>/dev/null; }", d);
    return (r > 0 && r < cap) ? 0 : -1;
}
/* Thread-safe bounded capture for the probe worker (the shared run_cap_bounded is main-thread only). */
static int bt_probe_run(const char *cmd, char *out, int cap, int timeout_ms){
    out[0] = 0;
    int fds[2];
    if(pipe2(fds, O_CLOEXEC) != 0) return 0;                      /* no other helper may inherit the write end */
    pid_t pid;
    char *argv[] = { "/bin/sh", "-c", (char *)cmd, NULL };
    if(command_spawn_io(&pid, argv, fds[1], -1) != 0){ close(fds[0]); close(fds[1]); return 0; }   /* posix_spawn: no fork() of the UI */
    close(fds[1]);
    int n = 0, left = timeout_ms;
    while(left > 0 && n < cap - 1){
        struct pollfd pf = { fds[0], POLLIN, 0 };
        struct timespec t0, t1; clock_gettime(CLOCK_MONOTONIC, &t0);
        if(poll(&pf, 1, left) <= 0) break;
        int r = read(fds[0], out + n, cap - 1 - n);
        if(r <= 0) break;
        n += r;
        clock_gettime(CLOCK_MONOTONIC, &t1);
        left -= (int)((t1.tv_sec - t0.tv_sec) * 1000 + (t1.tv_nsec - t0.tv_nsec) / 1000000);
    }
    out[n] = 0;
    close(fds[0]);
    kill(-pid, SIGKILL); kill(pid, SIGKILL);
    waitpid(pid, NULL, 0);                        /* worker thread: blocking here never touches the UI */
    return n;
}
/* Result of the last probe, shared between the worker threads and the UI thread. Every route change (a route
 * to a device, back to analog, or a lost link) bumps gen; a probe result carries the gen it was started under and is
 * dropped if it is no longer current, so a stale answer can never trigger a codec switch on a later connection. */
static struct { char mac[18]; int state; /* 0 idle, 1 running, 2 done */ int pcm, known; unsigned avail;
                int choice, misses, armed, applied; unsigned gen;
                int verifying, want_v, verify_tries; char sel[16]; char pcm_path[96]; unsigned allow; } g_cq;
    /* sel: BlueALSA's "Selected codec"; pcm_path: its PCM; allow: codecs still worth trying on this connection (a
     * codec that did not get through, and everything above it, is dropped: no stepping back up) */
#define BT_VERIFY_TRIES 8      /* after sending: probe ticks (0.7 s apart, ~5.6 s) for BlueALSA to report the codec */
int ui_codec_switch_gate(const char *mac);   /* main.c: 1 = switch now (nothing playing, or held), 0 = wait */
static int bt_info_prop(const char *buf, const char *key, char *out, int cap);   /* fwd */
static pthread_mutex_t g_cq_mu = PTHREAD_MUTEX_INITIALIZER;
#define BT_PCM_TRIES 20        /* probe ticks (0.7 s apart, ~14 s) to wait for the PCM before giving up (stays on SBC) */
struct bt_probe_arg { unsigned gen; char mac[18]; };
static void *bt_codec_probe_thread(void *arg){
    struct bt_probe_arg *pa = arg;
    char cmd[240], info[1536];
    info[0] = 0;
    if(bt_codec_query_cmd(pa->mac, cmd, sizeof cmd) == 0) bt_probe_run(cmd, info, sizeof info, 700);
    int known = 0; unsigned avail = bt_codec_parse_avail(info, &known);
    char sel[16] = "", v[64];
    if(bt_info_prop(info, "Selected codec:", v, sizeof v)){          /* first word, upper case: "AAC", "LDAC", "SBC" */
        int k = 0; while(v[k] && v[k] != ' ' && v[k] != ':' && k < (int)sizeof sel - 1){ sel[k] = (v[k] >= 'a' && v[k] <= 'z') ? (char)(v[k] - 32) : v[k]; k++; }
        sel[k] = 0;
    }
    pthread_mutex_lock(&g_cq_mu);
    if(pa->gen == g_cq.gen){                       /* still the route this probe was started for */
        g_cq.pcm = strstr(info, "PCM=") != NULL; g_cq.known = known; g_cq.avail = avail; g_cq.state = 2;
        snprintf(g_cq.sel, sizeof g_cq.sel, "%s", sel);
        const char *pp = strstr(info, "PCM="); int k = 0;   /* "PCM=/org/bluealsa/hci0/dev_XX_.../a2dpsrc/sink" */
        if(pp) for(pp += 4; k < (int)sizeof g_cq.pcm_path - 1 && (isalnum((unsigned char)pp[k]) || pp[k] == '/' || pp[k] == '_'); k++) g_cq.pcm_path[k] = pp[k];
        g_cq.pcm_path[k] = 0;
    }
    pthread_mutex_unlock(&g_cq_mu);
    free(pa);
    return NULL;
}
/* The route frame: 06b3 + length + VALUE1 0 (SBC) + MAC - byte-for-byte what diskOS has always sent, so routing
 * itself never waits on codec state. Returns 0, or -1 for a bad MAC / short buffer. */
int ui_bt_codec_frame(const char *mac, char *out, int cap){
    return bt_codec_frame_value(mac, 0, out, cap);
}
static int bt_codec_frame_value(const char *mac, int v, char *out, int cap){
    if(!bt_mac_valid(mac)) return -1;
    int r = snprintf(out, cap, "06b3%04X%04X%s", (unsigned)(12 + 17), (unsigned)v, mac);
    return (r > 0 && r < cap) ? 0 : -1;
}
/* AAC/LDAC. (2.6.2 note: on the owner's Disc the running BlueALSA is Disco's own, with its own arguments and an early
 * PID, so the relaunch below doesn't seem to happen in practice; see "Born on the right codec".) The route's 06c1 makes
 * the player (re)start bluetoothd and BlueALSA (mq_player_257.dis: bt_source_control killall + relaunch), so the device's A2DP PCM appears only AFTER the route, and the player's codec worker drops a
 * 06b3 sent while it is missing. So the route goes out with SBC as always (audio starts at once) and this then waits,
 * off the UI thread, for the PCM: when it shows and lists the chosen codec, the choice is sent as a second 06b3.
 * One step per call (UI timer tick); returns 1 when finished (sent, unavailable, gave up, or the route changed). */
/* Switching BlueALSA under connected headphones (their best codec needs the other one): disconnect, restart the right
 * BlueALSA, reconnect, all in the background. The music is paused and stays paused (the hold is dropped, so nothing
 * resumes on the Disc's own output while the link is down). Once per headphones per 2 minutes, so it can't loop. */
#ifndef BT_SWITCH_MARK
#define BT_SWITCH_MARK "/tmp/bt_switch_done"   /* left by the switch script once it reconnected: check the codec again */
#endif
static char g_sw_mac[18]; static uint32_t g_sw_at;
static int bt_daemon_switch_allowed(const char *mac){
    return strcmp(g_sw_mac, mac) || !g_sw_at || lv_tick_elaps(g_sw_at) > 120000;
}
static void bt_daemon_switch(const char *mac, int kind, int v){
    snprintf(g_sw_mac, sizeof g_sw_mac, "%s", mac); g_sw_at = lv_tick_get() | 1;
    char steps[1024], cmd[1400];
    bt_alsa_restart_steps(steps, sizeof steps, 0, kind);       /* normal order: the connection then switches to v */
    /* the reconnect can be too quick for the auto-route to see the link gone, so the script leaves BT_SWITCH_MARK when
     * done and the auto-route then treats it as a new connection (the codec step runs again, on the right BlueALSA) */
    unlink(BT_SWITCH_MARK);
    snprintf(cmd, sizeof cmd, "( bluetoothctl disconnect %s; sleep 1; %s sleep 0.5; bluetoothctl connect %s; touch " BT_SWITCH_MARK
             " ) >/dev/null 2>&1 &", mac, steps, mac);
    void ui_route_hold_cancel(void); ui_route_hold_cancel();
    system(cmd);
    fprintf(stderr, "bt codec: %s needs %s BlueALSA: reconnecting %s\n", bt_codec_word(v), kind == 1 ? "the aptX" : "FiiO's", mac);
    fflush(stderr);
    ui_toast(kind == 1 ? "Reconnecting for aptX..." : "Reconnecting for LDAC...");
}
/* aptX / aptX HD: ask BlueALSA itself for the codec on the device's PCM (mq_player's 06b3 has no aptX value), in
 * the background. 1 = asked; the usual verify step then waits for "Selected codec" to say so. */
static int bt_codec_select_direct(int v){
    char pcm[96], cmd[200];
    pthread_mutex_lock(&g_cq_mu); snprintf(pcm, sizeof pcm, "%s", g_cq.pcm_path); pthread_mutex_unlock(&g_cq_mu);
    if(strncmp(pcm, "/org/bluealsa/", 14)) return 0;                   /* no PCM read yet: the next tick tries again */
    snprintf(cmd, sizeof cmd, "bluealsa-cli codec %s %s >/dev/null 2>&1 &", pcm, v == 6 ? "aptX-HD" : "aptX");
    return system(cmd) == 0;
}
/* ---- Crash guard ------------------------------------------------------------------------------------------------
 * A codec that hasn't played on these headphones yet is noted ("bt_try", synced to flash) while music starts on it,
 * and the note is cleared when the music stops or after 20 s of playing - then the codec is known-good for them. If
 * the Disc restarts with the note still there, that codec froze it: it is skipped on those headphones (bt_skip_add)
 * until Bluetooth Codec is chosen again, and a message says so. Nothing else ever takes a codec away. */
#define BT_GUARD_SECS 20
static char g_gd_mac[18]; static unsigned g_gd_bit; static int g_gd_secs, g_gd_armed; static lv_timer_t *g_gd_timer;
static unsigned bt_word_bit(const char *w){
    return !strcmp(w, "AAC") ? BTC_AAC : !strcmp(w, "LDAC") ? BTC_LDAC : !strcmp(w, "APTX") ? BTC_APTX
         : !strcmp(w, "APTX-HD") ? BTC_APTXHD : 0u;
}
static void bt_guard_stop(void){
    if(g_gd_armed){ cfg_set_str("bt_try", ""); g_gd_armed = 0; }
    g_gd_bit = 0; g_gd_secs = 0;
    if(g_gd_timer){ lv_timer_del(g_gd_timer); g_gd_timer = NULL; }
}
static void bt_guard_tick(lv_timer_t *t){
    (void)t;
    if(!g_gd_bit || strcmp(ui_route_mac(), g_gd_mac)){ bt_guard_stop(); return; }   /* routed away: no crash happened */
    int playing = ui_is_playing();
    if(playing && !g_gd_armed){
        char v[40]; snprintf(v, sizeof v, "%s,%u", g_gd_mac, g_gd_bit);
        cfg_set_str("bt_try", v); g_gd_armed = 1;
    }
    if(playing && ++g_gd_secs >= BT_GUARD_SECS){
        bt_good_add(g_gd_mac, g_gd_bit);
        fprintf(stderr, "bt codec: 0x%x known-good on %s\n", g_gd_bit, g_gd_mac); fflush(stderr);
        bt_guard_stop();
    }
    else if(!playing && g_gd_armed){ cfg_set_str("bt_try", ""); g_gd_armed = 0; }
}
static void bt_guard_settled(const char *mac, const char *word){
    unsigned bit = bt_word_bit(word);
    if(!bit || ((unsigned)bt_good_get(mac) & bit)){ bt_guard_stop(); return; }   /* SBC, or played here before */
    if(g_gd_bit == bit && !strcmp(g_gd_mac, mac)) return;
    bt_guard_stop();
    snprintf(g_gd_mac, sizeof g_gd_mac, "%s", mac); g_gd_bit = bit;
    g_gd_timer = lv_timer_create(bt_guard_tick, 1000, NULL);
}
/* At start-up: a note left behind means the Disc restarted while that codec was starting to play. */
static char g_gd_msg[160];
static void bt_guard_msg_cb(lv_timer_t *t){ lv_timer_del(t); ui_toast(g_gd_msg); }
static int bt_bad_key(const char *k){ return !strncmp(k, "btbad_", 6); }
static void bt_guard_boot(void){
    const char *t = cfg_get_str("bt_try", "");
    if(t[0]){
        char mac[18] = ""; unsigned bit = 0;
        if(sscanf(t, "%17[0-9A-Fa-f:],%u", mac, &bit) == 2 && bt_mac_valid(mac) && bit && bit != BTC_SBC){
            bt_skip_add(mac, bit);
            static const struct { unsigned b; const char *n; } nm[] = { {BTC_AAC,"AAC"}, {BTC_LDAC,"LDAC"}, {BTC_APTX,"aptX"}, {BTC_APTXHD,"aptX HD"} };
            const char *n = "A codec";
            for(size_t i = 0; i < sizeof nm / sizeof *nm; i++) if(nm[i].b == bit) n = nm[i].n;
            snprintf(g_gd_msg, sizeof g_gd_msg, "%s restarted the Disc with these headphones, so it's skipped for them.\n"
                     "Choose Bluetooth Codec again to retry.", n);
            fprintf(stderr, "bt codec: crash guard: 0x%x skipped on %s (the Disc restarted while it started playing)\n", bit, mac); fflush(stderr);
            lv_timer_create(bt_guard_msg_cb, 12000, NULL);
        }
        cfg_set_str("bt_try", "");
    }
    cfg_remove_if(bt_bad_key);           /* 3.3.x's "failed to confirm" marks: no longer used, and they kept pairs on AAC */
}
/* Settings > Quiet Wi-Fi for Bluetooth changed: applied on the next auto-route poll (a few seconds). */
/* Settings > Bluetooth Codec chosen (again): every skipped codec gets another chance. */
void bt_codec_setting_changed(void){ cfg_set_int("bt_skip_gen", (cfg_get_int("bt_skip_gen", 0) + 1) & 0xffffff); }
void ui_resume_guard_arm(int reason, const char *mac);
int bt_codec_upgrade_step(void){
    char mac[18], f[48], settled[16] = ""; int send = -1, done = 0, switch_v = 0; struct bt_probe_arg *spawn = NULL;
    pthread_mutex_lock(&g_cq_mu);
    snprintf(mac, sizeof mac, "%s", g_cq.mac);
    if(!g_cq.armed || strcmp(ui_route_mac(), mac)) done = 1;                 /* cancelled or routed away: forget it */
    else if(g_cq.verifying && g_cq.state == 2){
        /* sent: wait for BlueALSA to report the new codec before calling it done (the playback hold waits on this) */
        const char *want = bt_codec_word(g_cq.want_v);
        if(!strcmp(g_cq.sel, want)){ done = 1; g_cq.applied = 1; snprintf(settled, sizeof settled, "%s", want);
            fprintf(stderr, "bt codec: %s confirmed\n", want); fflush(stderr); }
        else if(++g_cq.verify_tries >= BT_VERIFY_TRIES){
            /* not confirmed: the next one down for THIS connection only (nothing is remembered as bad). aptX off and
             * LDAC: as 3.3.1 (it stays where it is) */
            int kr = bt_alsa_kind_running();
            int next = g_cq.want_v >= 5 || (g_cq.want_v >= 2 && bt_alsa_aptx_bin())
                     ? bt_codec_pick(g_cq.choice, bt_usable(g_cq.mac, g_cq.avail) & g_cq.allow & bt_codec_below(g_cq.want_v)
                                     & bt_kind_can(kr < 0 ? 0 : kr), 1) : 0;
            if(next > 0){
                g_cq.verifying = 0; g_cq.allow &= bt_codec_below(g_cq.want_v); g_cq.misses = 0;
                fprintf(stderr, "bt codec: %s not confirmed, trying %s\n", want, bt_codec_word(next)); fflush(stderr);
            } else {                                        /* AAC, or LDAC on stock BlueALSA: as 3.3.1 */
                done = 1; g_cq.applied = 1;
                snprintf(settled, sizeof settled, "%s", g_cq.sel[0] ? g_cq.sel : "SBC");
                fprintf(stderr, "bt codec: %s not confirmed (selected '%s'), going on\n", want, g_cq.sel); fflush(stderr);
            }
        }
        g_cq.state = 0;
    }
    else if(g_cq.verifying){ /* probe still running */ }
    else if(g_cq.state == 2){
        int kr = bt_alsa_kind_running();
        if(g_cq.pcm && g_cq.known){                                      /* remember these headphones' codecs */
            /* The aptX build lists every codec the pair has (patched), so with the mandatory SBC in it the list is
             * whole and replaces the memory. Anything else only adds: the memory never shrinks. */
            if(kr == 1 && (g_cq.avail & BTC_SBC)) bt_caps_set(mac, g_cq.avail);
            else bt_caps_add(mac, g_cq.avail);
        }
        if(g_cq.pcm && g_cq.known){                                      /* the link is there and says what it can do */
            int c = bt_caps_get(mac);
            unsigned all = bt_usable(mac, g_cq.avail | (c > 0 ? (unsigned)c : 0u)) & g_cq.allow;
            int v = bt_codec_pick(g_cq.choice, all, g_cq.known);         /* unlisted -> SBC: nothing to do */
            if(v && kr >= 0 && !(bt_codec_need(v) & bt_kind_can(kr)) && bt_alsa_aptx_bin()){   /* this BlueALSA can't run their best */
                if(bt_daemon_switch_allowed(mac)) switch_v = v;            /* (the gate is asked after unlocking) */
                else v = bt_codec_pick(g_cq.choice, all & bt_kind_can(kr) & g_cq.avail, g_cq.known);   /* best on this one */
            }
            else if(v) v = bt_codec_pick(g_cq.choice, all & bt_kind_can(kr < 0 ? 0 : kr) & g_cq.avail, g_cq.known);
            const char *want = v ? bt_codec_word(v) : "";
            if(switch_v){ /* decided below, outside the lock */ }
            else if(v != 0 && !strcmp(g_cq.sel, want)){                  /* already on it: no switch, no pause */
                done = 1; g_cq.applied = 1;
                fprintf(stderr, "bt codec: already %s, nothing to switch\n", want); fflush(stderr);
                snprintf(settled, sizeof settled, "%s", want);
            }
            else if(v != 0) send = v;                                    /* applied only once confirmed (verifying) */
            else { done = 1; g_cq.applied = 1; snprintf(settled, sizeof settled, "%s", g_cq.sel[0] ? g_cq.sel : "SBC"); }
        } else if(++g_cq.misses >= BT_PCM_TRIES){ done = 1; g_cq.applied = 1; }   /* no link, or its codecs never known */
        g_cq.state = 0;
    }
    if(!done && !switch_v && g_cq.state == 0 && (spawn = malloc(sizeof *spawn)) != NULL){
        spawn->gen = g_cq.gen; snprintf(spawn->mac, sizeof spawn->mac, "%s", mac); g_cq.state = 1;
    }
    if(done) g_cq.armed = 0;
    unsigned gen_now = g_cq.gen;
    pthread_mutex_unlock(&g_cq_mu);
    if(settled[0]){ bt_announce(mac, settled); bt_guard_settled(mac, settled); }   /* "Connected: ..."; crash guard */
    if(switch_v){
        if(ui_codec_switch_gate(mac)){                                   /* nothing playing, or paused and held */
            pthread_mutex_lock(&g_cq_mu); if(g_cq.gen == gen_now){ g_cq.armed = 0; g_cq.applied = 1; } pthread_mutex_unlock(&g_cq_mu);
            bt_daemon_switch(mac, bt_kind_for_v(switch_v), switch_v);       /* the reconnect brings a fresh route */
            return 1;
        }
        pthread_mutex_lock(&g_cq_mu); if(g_cq.gen == gen_now) g_cq.state = 0; pthread_mutex_unlock(&g_cq_mu);   /* a song is starting: next tick */
    }
    if(send > 0 && !ui_codec_switch_gate(mac)){              /* a song is starting: switch once that settles */
        pthread_mutex_lock(&g_cq_mu); if(g_cq.gen == gen_now) g_cq.state = 0; pthread_mutex_unlock(&g_cq_mu);
        send = 0;
    }
    if(send > 0){
        int sent = send <= 4 ? bt_codec_frame_value(mac, send, f, sizeof f) == 0 && ipc_send_cmd(f) == 0   /* stock 06b3 */
                             : bt_codec_select_direct(send);                                              /* aptX: BlueALSA */
        pthread_mutex_lock(&g_cq_mu);
        if(g_cq.gen == gen_now && g_cq.armed){
            if(sent){ g_cq.verifying = 1; g_cq.want_v = send; g_cq.verify_tries = 0; g_cq.state = 0; done = 0;
                      fprintf(stderr, "bt codec: %s requested, waiting for it\n", bt_codec_word(send)); fflush(stderr); }
            else if(++g_cq.misses >= BT_PCM_TRIES){
                /* this codec never got through: the next one down the same order (LDAC -> aptX HD -> aptX -> AAC) */
                int kr = bt_alsa_kind_running();
                int next = bt_codec_pick(g_cq.choice, bt_usable(mac, g_cq.avail) & g_cq.allow & bt_codec_below(send)
                                         & bt_kind_can(kr < 0 ? 0 : kr), 1);
                if(next > 0){
                    g_cq.allow &= bt_codec_below(send); g_cq.misses = 0;
                    fprintf(stderr, "bt codec: %s request failed, trying %s\n", bt_codec_word(send), bt_codec_word(next)); fflush(stderr);
                } else { g_cq.armed = 0; g_cq.applied = 1; done = 1; }   /* gave up: SBC */
            }
            /* else: still armed, the next tick probes and tries again */
        }
        pthread_mutex_unlock(&g_cq_mu);
        /* (no guard here any more: a switch is only sent with nothing playing or under the playback hold) */
    }
    if(spawn){ pthread_t t;
               if(pthread_create(&t, NULL, bt_codec_probe_thread, spawn) == 0) pthread_detach(t);
               else { pthread_mutex_lock(&g_cq_mu); if(spawn->gen == g_cq.gen) g_cq.state = 0; pthread_mutex_unlock(&g_cq_mu); free(spawn); } }
    return done;
}
static lv_timer_t *g_cq_timer;
static void bt_codec_upgrade_cb(lv_timer_t *t){
    if(bt_codec_upgrade_step()){ lv_timer_del(t); g_cq_timer = NULL; }
}
int bt_codec_upgrade_pending(void){
    pthread_mutex_lock(&g_cq_mu); int p = g_cq.armed; pthread_mutex_unlock(&g_cq_mu); return p;
}
/* Any route change invalidates a pending upgrade and every probe still running for it. */
void ui_bt_codec_upgrade_cancel(void){
    pthread_mutex_lock(&g_cq_mu);
    g_cq.gen++; g_cq.armed = 0; g_cq.applied = 0; g_cq.state = 0; g_cq.verifying = 0;
    pthread_mutex_unlock(&g_cq_mu);
    /* N01: an AAC/LDAC request still in line goes too - only those (VALUE1 1-4), never the route's own SBC frame
     * (VALUE1 0), which the auto-route poll's routine cancels right after a route must not withdraw */
    for(int v = 1; v <= 4; v++){ char pre[16]; snprintf(pre, sizeof pre, "06b3%04X%04X", 12u + 17u, (unsigned)v); ipc_drop_queued(pre); }
}
/* Called after a route to `mac`. fresh = a full route sequence just re-sent the SBC frame: it cancels whatever was
 * pending and, if AAC/LDAC is chosen, arms a new upgrade. fresh = 0 (already routed there, e.g. a tap) keeps a
 * pending or finished upgrade as it is. SBC (the default) leaves nothing armed: no timer, probe or extra frame. */
void ui_bt_codec_upgrade_arm(const char *mac, int fresh){
    if(!bt_mac_valid(mac)) return;
    int choice = bt_codec_choice();
    if(fw_os_ver() != 257) choice = 0;   /* the 06b3 codec mapping is only decoded for V2.57: elsewhere stay on SBC */
    pthread_mutex_lock(&g_cq_mu);
    int same = !strcmp(g_cq.mac, mac) && (g_cq.armed || g_cq.applied);
    if(!fresh && (same || choice == 0)){ pthread_mutex_unlock(&g_cq_mu); return; }
    g_cq.gen++; g_cq.armed = 0; g_cq.applied = 0; g_cq.state = 0; g_cq.misses = 0; g_cq.verifying = 0; g_cq.pcm_path[0] = 0;
    snprintf(g_cq.mac, sizeof g_cq.mac, "%s", mac);
    g_cq.allow = ~0u;
    if(choice > 0){ g_cq.choice = choice; g_cq.armed = 1; }
    pthread_mutex_unlock(&g_cq_mu);
    if(choice > 0 && !g_cq_timer) g_cq_timer = lv_timer_create(bt_codec_upgrade_cb, 700, NULL);   /* music may be waiting */
}
/* ---- Live codec (Song Info's Output row) -------------------------------------------------------------------------
 * What BlueALSA says the routed headphones are actually running, not what was asked for. Read with the same
 * bluealsa-cli probe as the codec switch, off the UI thread, at most every BT_LIVE_EVERY ms and only while someone
 * asks (Song Info open, routed to Bluetooth). The answer is kept per MAC, so a route change can never show the old
 * headphones' codec. */
#define BT_LIVE_EVERY 4000
static struct { char mac[18]; char codec[16]; int busy; uint32_t at; } g_live;
static pthread_mutex_t g_live_mu = PTHREAD_MUTEX_INITIALIZER;
/* BlueALSA's codec word -> the name people know. Unknown words pass through; "" stays "". */
const char *bt_codec_display(const char *sel){
    static const struct { const char *k, *v; } m[] = { {"SBC","SBC"}, {"AAC","AAC"}, {"LDAC","LDAC"},
        {"APTX","aptX"}, {"APTX-HD","aptX HD"}, {"APTXHD","aptX HD"}, {"APTX-LL","aptX LL"}, {"FASTSTREAM","FastStream"} };
    if(!sel) return "";
    for(size_t i = 0; i < sizeof m / sizeof *m; i++) if(!strcasecmp(sel, m[i].k)) return m[i].v;
    return sel;
}
static void *bt_live_thread(void *arg){
    char mac[18]; snprintf(mac, sizeof mac, "%s", (const char *)arg); free(arg);
    char cmd[240], info[1536] = "", v[64], sel[16] = ""; int got = 0;
    if(bt_codec_query_cmd(mac, cmd, sizeof cmd) == 0) got = bt_probe_run(cmd, info, sizeof info, 700);
    if(strstr(info, "PCM=") && bt_info_prop(info, "Selected codec:", v, sizeof v)){
        int k = 0; while(v[k] && v[k] != ' ' && v[k] != ':' && k < (int)sizeof sel - 1){ sel[k] = (char)toupper((unsigned char)v[k]); k++; }
        sel[k] = 0;
    }
    pthread_mutex_lock(&g_live_mu);
    if(!strcmp(g_live.mac, mac) && got > 0) snprintf(g_live.codec, sizeof g_live.codec, "%s", sel);   /* same headphones;
                                                    * nothing read (a timeout) keeps the last value: no flicker */
    g_live.busy = 0;
    pthread_mutex_unlock(&g_live_mu);
    return NULL;
}
/* UI thread. Fills out with the live codec's display name and returns 1 when routed to Bluetooth and it is known;
 * returns 0 otherwise (local output, or not read yet). Each call may start one background read. */
int ui_bt_live_codec(char *out, int cap){
    const char *mac = ui_route_mac();
    if(cap > 0) out[0] = 0;
    if(!bt_mac_valid(mac)) return 0;
    char *arg = NULL; int have;
    pthread_mutex_lock(&g_live_mu);
    if(strcmp(g_live.mac, mac)){ snprintf(g_live.mac, sizeof g_live.mac, "%s", mac); g_live.codec[0] = 0; g_live.at = 0; }
    uint32_t now = lv_tick_get();
    if(!g_live.busy && (!g_live.at || lv_tick_elaps(g_live.at) >= BT_LIVE_EVERY) && (arg = strdup(mac)) != NULL){
        g_live.busy = 1; g_live.at = now | 1;
    }
    have = g_live.codec[0] != 0;
    if(have) snprintf(out, cap, "%s", bt_codec_display(g_live.codec));
    pthread_mutex_unlock(&g_live_mu);
    if(arg){ pthread_t t;
             if(pthread_create(&t, NULL, bt_live_thread, arg) == 0) pthread_detach(t);
             else { free(arg); pthread_mutex_lock(&g_live_mu); g_live.busy = 0; pthread_mutex_unlock(&g_live_mu); } }
    return have;
}
/* Full stock bring-up: download the chip firmware patch over /dev/ttyS0 (creates a
 * working hci0 - the step our old bluez-only path lacked), then bluetoothd + agent. */
static void bt_enable(void){
    /* Guard against a SECOND concurrent bring-up: if the marker is already present a bring-up is
     * in flight, and starting another detached subshell would have the two kill each other's
     * patchram/daemons (e.g. boot-restore + a user toggle firing close together) -> BT left broken.
     * bt_disable() removes the marker, so a real off->on still re-enables. */
    if(bt_enabling()) return;                                   /* a live bring-up (a stale marker is cleared) */
    /* Create the cancel marker SYNCHRONOUSLY before the backgrounded subshell, so it exists by
     * the time we return. bt_enable()/bt_disable() are both main-thread + serialized, so a
     * later bt_disable() `rm` always beats the async subshell - no touch-vs-rm race. */
    /* Each bring-up writes its own token into the marker and checks for THAT token: a newer bring-up (after an
     * off -> on) makes an older one stop at its next check instead of both running. */
    static unsigned bt_gen;
    unsigned tok = (unsigned)time(NULL) * 1000u + (++bt_gen % 1000u);
    { FILE *m = fopen("/tmp/bt_enabling", "w"); if(m){ fprintf(m, "%u\n", tok); fclose(m); } }
    char script[3584], alsa_args[768];
    bt_alsa_cmd(alsa_args, sizeof alsa_args, bt_born_for(cfg_get_str("bt_last", "")),
                bt_kind_for(cfg_get_str("bt_last", "")));                                    /* born on the right codec */
    snprintf(script, sizeof script,
        "( A(){ [ \"$(cat /tmp/bt_enabling 2>/dev/null)\" = %u ]; }; "                 /* still the current bring-up? */
        "  T(){ \"$@\" & p=$!; ( sleep 6; kill -9 $p 2>/dev/null ) & w=$!; wait $p; kill $w 2>/dev/null; }; "   /* 6 s cap per bluetoothctl */
        "  i=0; while [ -e /tmp/bt_disabling ] && [ \"$i\" -lt 40 ]; do sleep 0.25; i=$((i+1)); done; "   /* let a just-started teardown finish first (it would kill this bring-up) */
        "  killall -9 fiio_bluetoothctl brcm_patchram_plus bluetoothd bluealsa 2>/dev/null; "
        "  hciconfig hci0 down 2>/dev/null; "
        /* power-cycle the BT core (BT_REG_ON via rfkill) BEFORE patchram, so the chip
         * re-syncs whether this is a fresh boot OR a re-enable after a prior patchram
         * (without the block/unblock cycle, re-patchram on an already-firmwared chip
         * hangs and hci0 never appears - verified on-device). */
        "  rfkill block bluetooth; sleep 1; "
        "  A || exit; "     /* disabled or superseded during the block? leave it to the newer state */
        "  rfkill unblock bluetooth; sleep 1; "
        "  A || exit; "
        "  brcm_patchram_plus --enable_lpm --enable_hci --no2bytes --tosleep 200000 --baudrate 3000000 "
        "    --patchram /lib/firmware/bt_bcm/BCM4343A1_001.002.009.1026.1055.hcd /dev/ttyS0 >/tmp/patchram.log 2>&1 & "
        /* patchram can take well over 5s to attach hci0; keep retrying `up` until it's
         * RUNNING (up to ~20s) so we never give up before the chip is ready and leave hci0
         * DOWN (which made bt_on() report "off" and the toggle time out). Each pass re-checks
         * the marker so a toggle-off aborts the loop promptly. */
        "  i=0; while [ \"$i\" -lt 40 ]; do A || exit; hciconfig hci0 up 2>/dev/null; "
        "    hciconfig hci0 2>/dev/null | grep -q RUNNING && break; sleep 0.5; i=$((i+1)); done; "
        "  A || exit; "     /* disabled during bring-up -> don't start the daemons */
        "  /usr/project/bluetoothd --noplugin=sap --plugin=a2dp,avrcp --mode=source >/tmp/btd.log 2>&1 & "
        "  %s >/tmp/bluealsa.log 2>&1 & "
        /* hci0 UP alone leaves bluez Powered:no - power the controller on explicitly (needs
         * bluetoothd, hence after its start) so bt_on()'s "Powered: yes" check passes. */
        "  sleep 1; A || exit; "
        "  T bluetoothctl power on; hciconfig hci0 piscan; hciconfig hci0 class 0x200414; "
        "  T bluetoothctl agent on; T bluetoothctl default-agent; T bluetoothctl pairable on; "
        "  A && rm -f /tmp/bt_enabling "     /* enable finished: drop the marker (only if still ours) */
        ") >/dev/null 2>&1 &", tok, alsa_args);
    system(script);
    /* NB: bluealsa is started INSIDE the subshell above (after hci0 is RUNNING +
     * bluetoothd), not here, so it never races the patchram attach. */
}
static void bt_wifi_quiet_tick(int off);   /* fwd */
static void bt_disable(void){
    bt_announce_reset();   /* the next connection after Bluetooth comes back is announced ("Connected: ...") */
    bt_wifi_quiet_tick(1);   /* Wi-Fi back to normal */
    scan_abort();          /* cancel any pending/active scan + the bt_open observer (covers the radio-timeout
                            * OFF path, which reaches here without a caller-side scan_abort). LVGL-touching,
                            * so it MUST stay on the main thread. */
    bt_autoroute_stop();
    /* Tear the stack down in a BACKGROUNDED subshell so the UI never blocks: `bluetoothctl power off`
     * stalls for the D-Bus timeout if bluetoothd is wedged, and running that synchronously on the main
     * thread froze the whole UI on a BT-off tap (Reddit report 2026-09-21). The device has no `timeout`
     * binary, so instead of a graceful daemon power-off (which could hang the subshell before the kill)
     * we KILL the daemons FIRST - direct, fast, no D-Bus round-trip - then drop the interface and
     * rfkill-block. rm the enable marker first so any in-flight bt_enable() subshell aborts. */
    /* Synchronously: drop the bring-up marker (an in-flight bring-up stops at its next check, and a following "on"
     * is not mistaken for one already running) and set the teardown marker, which a following bring-up waits for. */
    unlink("/tmp/bt_enabling");
    { FILE *m = fopen("/tmp/bt_disabling", "w"); if(m) fclose(m); }
    system("( killall -9 bluealsa bluetoothd brcm_patchram_plus fiio_bluetoothctl bt-agent 2>/dev/null; "
           "hciconfig hci0 down >/dev/null 2>&1; "
           "rfkill block bluetooth >/dev/null 2>&1; rm -f /tmp/bt_disabling ) >/dev/null 2>&1 &");
}

/* A Bluetooth MAC must be exactly AA:BB:CC:DD:EE:FF (hex + colons) before it is ever
 * interpolated into a bluetoothctl shell command. bluez scan output is already this
 * format, so this is defensive: a malformed/hostile address can carry no shell
 * metacharacters past this gate. */
static int bt_mac_valid(const char *mac){
    if(!mac) return 0;
    for(int i = 0; i < 17; i++){
        char c = mac[i];
        if((i % 3) == 2){ if(c != ':') return 0; }
        else if(!((c>='0'&&c<='9')||(c>='A'&&c<='F')||(c>='a'&&c<='f'))) return 0;
    }
    return mac[17] == 0;
}

/* Route once per connected bluealsa A2DP sink. Keeping the MAC latched while the
 * PCM exists preserves a manual switch back to analog until the sink reconnects. */
/* ---- Quiet Wi-Fi while listening over Bluetooth -------------------------------------------------------------------
 * The Disc's AP6212 (BCM43438) does Wi-Fi and Bluetooth on ONE 2.4 GHz radio and antenna, taking turns. Stock leaves
 * Wi-Fi power-save off (wl PM 0: awake and listening all the time), and an unconnected wpa_supplicant keeps searching
 * for networks (off-channel scans) - both take airtime from the headphones (dropouts, LDAC first). While audio is
 * routed to Bluetooth (Settings > Quiet Wi-Fi for Bluetooth, on by default):
 *  - Wi-Fi power-save on (wl PM 2, "fast": it wakes for its own traffic, so downloads still run at full speed);
 *  - not connected: wpa_supplicant's searching is paused (wpa_cli disconnect) - never while the Wi-Fi screen is open
 *    or a connection is under way.
 * Undone as soon as audio leaves Bluetooth, Bluetooth goes off or the setting is turned off. Each change leaves a
 * marker in /tmp, so a restarted mq_ui undoes a leftover one (bt_wifi_quiet_boot). Runs in the background. */
#ifndef BT_WQ_DIR
#define BT_WQ_DIR "/tmp"
#endif
#define BT_WQ_PM     BT_WQ_DIR "/bt_wifi_pm"        /* the power-save mode before ours (restored after) */
#define BT_WQ_PAUSED BT_WQ_DIR "/bt_wifi_paused"    /* searching paused by us */
#define BT_WQ_EVERY  60000                          /* re-applied while routed: a Wi-Fi reconnect resets PM */
static int g_wq_on; static uint32_t g_wq_at;
static void bt_wifi_quiet_apply(int on, int may_pause){
    char cmd[640];
    if(on)
        snprintf(cmd, sizeof cmd, "( [ -f " BT_WQ_PM " ] || { m=$(wl PM 2>/dev/null) && [ -n \"$m\" ] && echo \"$m\" > " BT_WQ_PM "; }; "
                 "wl PM 2; %s ) >/dev/null 2>&1 &",
                 may_pause ? "case \"$(wpa_cli -i wlan0 status 2>/dev/null | grep '^wpa_state=')\" in "
                             "*=SCANNING|*=DISCONNECTED) wpa_cli -i wlan0 disconnect && touch " BT_WQ_PAUSED ";; esac" : ":");
    else
        snprintf(cmd, sizeof cmd, "( [ -f " BT_WQ_PM " ] && wl PM \"$(cat " BT_WQ_PM ")\"; rm -f " BT_WQ_PM "; "
                 "[ -f " BT_WQ_PAUSED " ] && wpa_cli -i wlan0 reconnect; rm -f " BT_WQ_PAUSED " ) >/dev/null 2>&1 &");
    system(cmd);
}
/* Called on every auto-route poll (and with off=1 when Bluetooth goes off). */
static void bt_wifi_quiet_tick(int off){
    int want = !off && cfg_get_int("bt_wifi_quiet", 1) && ui_route_mac()[0];
    if(want){
        if(!g_wq_on || lv_tick_elaps(g_wq_at) >= BT_WQ_EVERY){
            bt_wifi_quiet_apply(1, screen_current() != SCR_WIFI && screen_current() != SCR_WIFI_INFO);
            if(!g_wq_on){ fprintf(stderr, "bt: Wi-Fi quiet while on Bluetooth (power-save on, no searching)\n"); fflush(stderr); }
            g_wq_on = 1; g_wq_at = lv_tick_get();
        }
    } else if(g_wq_on){
        bt_wifi_quiet_apply(0, 0); g_wq_on = 0;
        fprintf(stderr, "bt: Wi-Fi back to normal\n"); fflush(stderr);
    }
}
/* Start-up: a previous mq_ui may have left Wi-Fi quiet. */
static void bt_wifi_quiet_boot(void){
    if(access(BT_WQ_PM, F_OK) == 0 || access(BT_WQ_PAUSED, F_OK) == 0) bt_wifi_quiet_apply(0, 0);
}
static void bt_autoroute_poll_cb(lv_timer_t *t){
    (void)t;
    bt_wifi_quiet_tick(0);
    /* The post-restart settle guard lives in ui_route_bt (covers every routing path); this poll keeps
     * firing during the window and routes on the first tick past it (ui_route_bt returns "not routed"
     * meanwhile, so g_bt_autorouted is not latched and the retry stands). */
    char path[256], mac[20];
    int found = 0;
    /* bounded: bluealsa-cli talks to bluealsa and can stall if it is wedged; this runs on a 3s poll
     * timer (main thread), so a hang here would freeze the UI. No `timeout` binary on the device. */
    /* A probe that TIMED OUT (the Disc busy, or slower with the screen off) knows nothing: it must not count as "the
     * link is gone". Taking it for that made 2.6.0 forget the route, and the re-route's 06c1 restarts the player's
     * Bluetooth service - dropping the headphones mid-song. Only a probe that completed and found no A2DP PCM is
     * evidence. */
    int got = run_cap_to("bluealsa-cli list-pcms 2>/dev/null | grep -m1 a2dpsrc", path, sizeof path, 250);
    if(got < 0) return;                                   /* no answer in time: change nothing, ask again next poll */
    if(got > 0){
        char *dev = strstr(path, "dev_");
        if(dev){
            dev += 4;
            char *slash = strchr(dev, '/');
            if(slash && slash - dev == 17){
                memcpy(mac, dev, 17); mac[17] = 0;
                for(int i = 0; i < 17; i++) if(mac[i] == '_') mac[i] = ':';
                found = bt_mac_valid(mac);
            }
        }
    }
    /* B01: the link is really gone only after THREE completed probes in a row found no A2DP PCM (9 s); then the
     * route record goes too, so the same headphones coming back get the full routing sequence, not "already routed".
     * Fewer than that changes nothing at all (not even the latch or a pending codec switch): a single empty answer
     * during a codec switch or a busy moment must never lead to a re-route. */
    /* The Bluetooth screen only lists devices when opened; when the audio link appears or goes (headphones switched
     * on or off) while it's showing, re-read the devices so "Connected - LDAC" doesn't linger. A quiet re-read of the
     * known devices (no discovery window), only on a change. */
    /* A link counts as gone after two completed empty probes, and as back when it shows again after that (review:
     * debounce, so a flaky link doesn't re-trigger work on every single empty answer). Gone: the next connection is
     * announced again and an open Bluetooth screen re-reads its list. Back: its codec is checked again (headphones
     * switched off and on reconnect on SBC otherwise), the list re-read, and an SBC link announced. */
    static int missing, seen, orphan;
    if(!found){
        missing++;
        if(missing == 2){ bt_announce_reset(); if(screen_current() == SCR_BT) scan_kick(); }
        /* Other headphones reconnecting by themselves while BlueALSA is restricted (born on AAC/LDAC) can't get an
         * audio link if they lack that codec, so no PCM would ever show them. The kernel still lists the connection:
         * a link with no PCM for three probes in a row, while restricted, gets BlueALSA back to the normal order
         * (review finding 1). The usual headphones get their PCM within seconds and never trigger this. */
        if(bt_acl_links() > 0){
            int born = bt_alsa_actual(NULL, 0);
            if(born > 0 && ++orphan >= 3){ orphan = 0; int kr = bt_alsa_kind_running();
                                           bt_alsa_restart_async(0, kr > 0 ? kr : 0, "a connection got no audio link"); }
            else if(born <= 0) orphan = 0;
        } else orphan = 0;
        if(missing == 3){
            g_bt_autorouted[0] = 0; ui_bt_codec_upgrade_cancel();
            bt_alsa_rearm_born();                            /* really gone: be ready to be born on the right codec */
            if(ui_route_mac()[0]){ void ui_route_forget(const char *why); ui_route_forget("link gone"); }
        }
        return;
    }
    int back = !seen || missing >= 2;                   /* a new connection, or a real return */
    if(unlink(BT_SWITCH_MARK) == 0) back = 1;           /* reconnected through the other BlueALSA (bt_daemon_switch) */
    seen = 1; missing = 0; orphan = 0;
    if(back){
        ui_bt_codec_upgrade_arm(mac, 1);                 /* check its codec again */
        if(screen_current() == SCR_BT) scan_kick();
        if(!bt_codec_upgrade_pending()) bt_announce(mac, "SBC");   /* SBC chosen: nothing to wait for */
    }
    if(g_disc_mac[0] && !strcmp(mac, g_disc_mac) && (int32_t)(lv_tick_get() - g_disc_until) < 0) return;   /* N03 */
    if(strcmp(mac, g_bt_autorouted)){
        if(ui_route_bt(mac) == 0){    /* latch only on a successful route, else retry on the next poll */
            snprintf(g_bt_autorouted, sizeof g_bt_autorouted, "%s", mac);
            bt_remember_last(mac);
        }
    }
}

/* After Bluetooth comes on (boot or a toggle) the Disc calls the headphones it last played to, a few times: many
 * headphones try to reconnect only once, right at power-on, often before the Disc's Bluetooth is up. Each try is
 * capped at 8 s; nothing happens if they're off or already connected. Runs in the background. */
static void bt_reconnect_last(void){
    const char *mac = cfg_get_str("bt_last", "");
    if(!bt_mac_valid(mac)) return;
    char cmd[512];
    snprintf(cmd, sizeof cmd,
             "( T(){ \"$@\" & p=$!; ( sleep 8; kill -9 $p 2>/dev/null ) & w=$!; wait $p; r=$?; kill $w 2>/dev/null; return $r; }; "
             "for i in 1 2 3; do bluetoothctl info %s 2>/dev/null | grep -q 'Connected: yes' && exit; "
             "T bluetoothctl connect %s && exit; sleep 5; done ) >/dev/null 2>&1 &", mac, mac);
    system(cmd);
}
/* Is BlueALSA's A2DP stream running right now? 1 yes, 0 no (answered), -1 unknown (no answer in time, or this
 * BlueALSA doesn't report it). Evidence that sound is going to the headphones, independent of Disco's own
 * position-based guess. Spawns a short command: only for rare decisions (shutdown, route, hold end). */
int bt_audio_running(void){
    char out[256];
    int n = run_cap_to("for p in $(bluealsa-cli list-pcms 2>/dev/null | grep a2dpsrc); do bluealsa-cli info \"$p\" 2>/dev/null; done"
                       " | grep -i -m1 '^ *Running:'", out, sizeof out, 1000);
    if(n < 0) return -1;
    if(!n) return -1;                                   /* no "Running:" line: this BlueALSA doesn't say */
    return strstr(out, "true") || strstr(out, "yes") ? 1 : 0;
}
/* A source change (USB DAC, Bluetooth receiving, back to playback...) owns the output: forget the auto-route latch so
 * returning to playback routes to the headphones afresh (B02). */
void bt_autoroute_reset(void){ g_bt_autorouted[0] = 0; ui_bt_codec_upgrade_cancel(); }
static void bt_autoroute_start(void){
    if(g_bt_autoroute_timer) return;
    g_bt_autoroute_timer = lv_timer_create(bt_autoroute_poll_cb, 3000, NULL);
}
static void bt_autoroute_stop(void){
    if(g_bt_autoroute_timer){ lv_timer_del(g_bt_autoroute_timer); g_bt_autoroute_timer = NULL; }
    g_bt_autorouted[0] = 0;
}
/* The player restarted: a fresh mq_player defaults to local/analog output, so any "already routed to X"
 * memory is stale. Forget it so the auto-route poll re-routes the still-connected speaker (and so the
 * local re-init isn't wrongly suppressed). If BT is on but the poll timer died, re-arm it. */
void bt_notify_player_restart(void){
    g_bt_autorouted[0] = 0;
    if(bt_on() && !g_bt_autoroute_timer) bt_autoroute_start();
}

/* ---- pair + connect ----------------------------------------------------- */
/* Extract a `bluetoothctl info` property value: the line whose first non-blank token is `key`
 * (e.g. "Connected:" / "Icon:"), returning its trimmed value. Line-anchored so a Name:/Alias:
 * value that merely CONTAINS the text can't spoof it. Fills out + returns 1 on match, else out="",0. */
static int bt_info_prop(const char *buf, const char *key, char *out, int cap){
    if(cap <= 0) return 0;
    size_t klen = strlen(key);
    for(const char *l = buf; l && *l; ){
        const char *p = l; while(*p==' '||*p=='\t') p++;
        if(!strncmp(p, key, klen)){
            p += klen; while(*p==' '||*p=='\t') p++;
            int i=0; while(p[i] && p[i]!='\n' && p[i]!='\r' && i<cap-1){ out[i]=p[i]; i++; }
            while(i>0 && (out[i-1]==' '||out[i-1]=='\t')) i--;   /* trim trailing whitespace so "yes " compares == "yes" */
            out[i]=0; return 1;
        }
        const char *nl = strchr(l, '\n'); l = nl ? nl+1 : NULL;
    }
    out[0]=0;
    return 0;
}
/* a device is connected when `bluetoothctl info <mac>` reports "Connected: yes" */
static int bt_dev_connected(const char *mac){
    if(!bt_mac_valid(mac)) return 0;
    char cmd[160], buf[2048];
    snprintf(cmd, sizeof cmd, "bluetoothctl info %s 2>/dev/null", mac);
    if(run_cap_to(cmd, buf, sizeof buf, 1000) < 0) return 0;   /* UI thread, polled every 1.5 s: no long stalls; the next tick asks again */
    /* match the "Connected:" PROPERTY line (after indentation), not a Name:/Alias:
     * that merely contains the text "Connected: yes". */
    for(char *l = buf; l && *l; ){
        char *nl = strchr(l, '\n'); if(nl) *nl = 0;
        char *p = l; while(*p == ' ' || *p == '\t') p++;
        if(!strncmp(p, "Connected:", 10)) return strstr(p, "yes") != NULL;
        if(!nl) break; l = nl + 1;
    }
    return 0;
}

/* honest completion: pair+connect is async (and slow), so poll for the real
 * result for up to 25s and toast it, instead of silently firing-and-forgetting. */
static lv_timer_t *g_bt_conn_timer;
static uint32_t    g_bt_conn_start;
static char        g_bt_conn_mac[32];
/* The "is it connected yet?" question runs on a short worker thread: bluetoothctl can take up to a second to
 * answer, and asking it on the UI thread every 1.5 s made the screen stutter while connecting. */
static pthread_mutex_t g_cc_mu = PTHREAD_MUTEX_INITIALIZER;
static int g_cc_busy, g_cc_yes; static char g_cc_mac[20];
static void *cc_worker(void *a){
    (void)a; char m[20];
    pthread_mutex_lock(&g_cc_mu); snprintf(m, sizeof m, "%s", g_cc_mac); pthread_mutex_unlock(&g_cc_mu);
    int yes = bt_dev_connected(m);
    pthread_mutex_lock(&g_cc_mu); if(!strcmp(m, g_cc_mac)) g_cc_yes = yes; g_cc_busy = 0; pthread_mutex_unlock(&g_cc_mu);
    return NULL;
}
static int bt_conn_check(const char *mac){            /* 1 = the last finished check said connected; starts the next */
    int yes, start = 0;
    pthread_mutex_lock(&g_cc_mu);
    if(strcmp(mac, g_cc_mac)){ snprintf(g_cc_mac, sizeof g_cc_mac, "%s", mac); g_cc_yes = 0; }
    yes = g_cc_yes;
    if(!g_cc_busy){ g_cc_busy = 1; start = 1; }
    pthread_mutex_unlock(&g_cc_mu);
    if(start){ pthread_t th; if(pthread_create(&th, NULL, cc_worker, NULL) == 0) pthread_detach(th);
               else { pthread_mutex_lock(&g_cc_mu); g_cc_busy = 0; pthread_mutex_unlock(&g_cc_mu); } }
    return yes;
}
static void bt_remember_last(const char *mac){
    if(bt_mac_valid(mac) && strcmp(cfg_get_str("bt_last", ""), mac)) cfg_set_str("bt_last", mac);
}
static void bt_conn_poll_cb(lv_timer_t *t){
    (void)t;
    if(bt_conn_check(g_bt_conn_mac)){
        pthread_mutex_lock(&g_cc_mu); g_cc_yes = 0; pthread_mutex_unlock(&g_cc_mu);
        bt_remember_last(g_bt_conn_mac);
        lv_timer_del(g_bt_conn_timer); g_bt_conn_timer = NULL;
        /* Connected at the BT layer. Only claim "Connected" (audio routed) if the route sequence
         * actually went through; otherwise stay honest and leave g_bt_autorouted unset so the
         * autoroute poll keeps retrying the route. */
        int routed = ui_route_bt(g_bt_conn_mac);
        if(routed == 0){
            snprintf(g_bt_autorouted, sizeof g_bt_autorouted, "%s", g_bt_conn_mac);
            if(!bt_codec_upgrade_pending()) bt_announce(g_bt_conn_mac, "SBC");   /* else: announced once the codec is known */
        } else if(routed == -2){            /* deferred while a song starts: the auto-route poll routes it shortly */
            ui_toast("Connecting...");
        } else {
            ui_toast("Paired - audio stays on player");
        }
        scan_kick();                        /* instant re-list (device already known) -> ✓, no 13s re-scan */
        return;
    }
    if(lv_tick_elaps(g_bt_conn_start) > 25000){   /* pairing can be slow */
        lv_timer_del(g_bt_conn_timer); g_bt_conn_timer = NULL;
        ui_toast("Couldn't connect");
        scan_kick();                        /* re-list the current devices, no fresh discovery */
    }
}

static void bt_connect(const char *mac){
    if(!bt_mac_valid(mac)){ ui_toast("Bad device address"); return; }
    scan_proc_stop();                                   /* stop our search first: pairing during discovery fails often */
    char prep[1024]; bt_codec_prepare_steps(mac, prep, sizeof prep);   /* restricted BlueALSA: normal order first */
    char cmd[1600];
    /* Stop discovery first (pairing/connecting during an inquiry fails often on this chip), and pair only a device
     * that isn't paired yet (re-pairing confuses some headphones). */
    snprintf(cmd, sizeof cmd,
             "%sbluetoothctl scan off; bluetoothctl info %s 2>/dev/null | grep -q 'Paired: yes' || bluetoothctl pair %s; "
             "bluetoothctl trust %s; bluetoothctl connect %s",
             prep, mac, mac, mac, mac);
    conn_proc_stop();                                   /* a previous attempt still running: this one replaces it */
    {   /* our own child in its own process group, so Disconnect can stop it (N03) */
        posix_spawn_file_actions_t fa; posix_spawn_file_actions_init(&fa);
        posix_spawn_file_actions_addopen(&fa, 1, "/dev/null", O_WRONLY, 0);
        posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
        posix_spawnattr_t at; posix_spawnattr_init(&at);
        posix_spawnattr_setflags(&at, POSIX_SPAWN_SETPGROUP); posix_spawnattr_setpgroup(&at, 0);
        char *argv[] = { "sh", "-c", cmd, NULL }; extern char **environ; pid_t pid;
        if(posix_spawn(&pid, "/bin/sh", &fa, &at, argv, environ) == 0) g_conn_pid = pid;
        posix_spawn_file_actions_destroy(&fa); posix_spawnattr_destroy(&at);
    }
    if(!strcmp(g_disc_mac, mac)) g_disc_mac[0] = 0;   /* connecting again lifts an earlier Disconnect's block */
    snprintf(g_bt_conn_mac, sizeof g_bt_conn_mac, "%s", mac);
    ui_toast("Connecting...");
    g_bt_conn_start = lv_tick_get();
    if(g_bt_conn_timer) lv_timer_del(g_bt_conn_timer);
    g_bt_conn_timer = lv_timer_create(bt_conn_poll_cb, 1500, NULL);
}
static void bt_disconnect(const char *mac){
    if(!bt_mac_valid(mac)) return;
    /* N03: a Disconnect supersedes the connection attempt at every layer: its watcher, its cached "connected" result,
     * its pair/connect process; and for a few seconds the auto-route won't route to these headphones while their link
     * is still going down. */
    if(!strcmp(mac, g_bt_conn_mac)){
        if(g_bt_conn_timer){ lv_timer_del(g_bt_conn_timer); g_bt_conn_timer = NULL; }
        g_bt_conn_mac[0] = 0; conn_proc_stop();
        pthread_mutex_lock(&g_cc_mu); g_cc_mac[0] = 0; g_cc_yes = 0; pthread_mutex_unlock(&g_cc_mu);
    }
    snprintf(g_disc_mac, sizeof g_disc_mac, "%s", mac); g_disc_until = lv_tick_get() + 8000;
    if(!strcmp(mac, g_bt_autorouted)) g_bt_autorouted[0] = 0;
    ui_route_analog();          /* return audio to the DAC before dropping the A2DP link */
    char cmd[128];
    snprintf(cmd, sizeof cmd, "bluetoothctl disconnect %s >/dev/null 2>&1 &", mac);
    system(cmd);
    ui_toast("Disconnecting...");
}

/* The connected device (any kind: a phone in BT-receiving mode, a speaker when streaming), found by a bounded
 * background probe so the UI thread never waits on bluetoothctl. Cached; re-probed at most every 4 s. */
static pthread_mutex_t g_pr_mu = PTHREAD_MUTEX_INITIALIZER;
static char g_pr_name[64]; static int g_pr_conn = 0, g_pr_running = 0; static uint32_t g_pr_at = 0, g_pr_have = 0;
/* N04 (Bluetooth findings, 21 Oct): this BlueZ (5.54) can't list only connected devices, so each pass asks about at
 * most 8 of the known ones - first the one found connected last time, then 8 more starting where the previous pass
 * stopped - so a connected device anywhere in a long list is found within a few passes. */
static int g_pr_offset; static char g_pr_last[18];          /* [g_pr_mu] */
static void *peer_worker(void *a){
    (void)a; char buf[4096], name[64] = {0}; int conn = 0;
    if(run_cap_to("bluetoothctl devices 2>/dev/null", buf, sizeof buf, 2500) > 0){
        char macs[64][18], names[64][64]; int n = 0;
        for(char *l = buf; l && *l && n < 64; ){
            char *nl = strchr(l, '\n'); if(nl) *nl = 0;
            if(!strncmp(l, "Device ", 7) && strlen(l) > 7 + 17){
                char mac[20]; memcpy(mac, l + 7, 17); mac[17] = 0;
                if(bt_mac_valid(mac)){ snprintf(macs[n], 18, "%s", mac); snprintf(names[n], 64, "%s", l + 7 + 17 + 1); n++; }
            }
            if(!nl) break; l = nl + 1;
        }
        pthread_mutex_lock(&g_pr_mu); int off = n ? g_pr_offset % n : 0; char last[18]; snprintf(last, sizeof last, "%s", g_pr_last); pthread_mutex_unlock(&g_pr_mu);
        int order[9], m = 0;
        for(int i = 0; i < n; i++) if(last[0] && !strcmp(macs[i], last)){ order[m++] = i; break; }
        for(int k = 0; k < n && m < 9; k++){ int i = (off + k) % n; if(m && i == order[0]) continue; order[m++] = i; }
        int asked = 0;
        for(int j = 0; j < m && !conn; j++){
            char cmd[128], info[2048], cv[16];
            snprintf(cmd, sizeof cmd, "bluetoothctl info %s 2>/dev/null", macs[order[j]]);
            if(run_cap_to(cmd, info, sizeof info, 1500) < 0) break;
            asked++;
            if(bt_info_prop(info, "Connected:", cv, sizeof cv) && !strcmp(cv, "yes")){
                conn = 1; snprintf(name, sizeof name, "%s", names[order[j]]);
                pthread_mutex_lock(&g_pr_mu); snprintf(g_pr_last, sizeof g_pr_last, "%s", macs[order[j]]); pthread_mutex_unlock(&g_pr_mu);
            }
        }
        pthread_mutex_lock(&g_pr_mu);
        if(!conn && n) g_pr_offset = (off + (asked > 8 ? 8 : asked)) % n;   /* next pass continues after these */
        pthread_mutex_unlock(&g_pr_mu);
    }
    pthread_mutex_lock(&g_pr_mu);
    g_pr_conn = conn; snprintf(g_pr_name, sizeof g_pr_name, "%s", name); g_pr_have = 1; g_pr_running = 0;
    pthread_mutex_unlock(&g_pr_mu);
    return NULL;
}
int bt_peer_name(char *out, int cap){
    if(cap > 0) out[0] = 0;
    if(bt_state() != BT_ON) return 0;
    int spawn = 0, conn;
    pthread_mutex_lock(&g_pr_mu);
    if(!g_pr_running && (!g_pr_have || lv_tick_elaps(g_pr_at) > 8000)){ g_pr_running = 1; spawn = 1; }
    conn = g_pr_conn; if(cap > 0) snprintf(out, (size_t)cap, "%s", g_pr_name);
    pthread_mutex_unlock(&g_pr_mu);
    if(spawn){
        g_pr_at = lv_tick_get();
        pthread_t th;
        if(pthread_create(&th, NULL, peer_worker, NULL) == 0) pthread_detach(th);
        else { pthread_mutex_lock(&g_pr_mu); g_pr_running = 0; pthread_mutex_unlock(&g_pr_mu); }
    }
    return conn;
}

/* ---- details screen (SCR_BT_INFO) --------------------------------------- */
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
    lv_label_set_long_mode(v, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_pos(v, 96, 11); lv_obj_set_size(v, 142, 18);
    lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_font(v, ui_font_cjk(14), 0);   /* "Name" value = device name: Cyrillic/CJK-capable (issue #3) */
    lv_obj_set_style_text_color(v, TC(TEXT_PRIMARY), 0);
}
/* Forget (unpair + untrust) the selected device, then return to the list + rescan (C13). */
static void info_forget_cb(lv_event_t *e){
    if(lv_event_get_code(e)!=LV_EVENT_CLICKED) return;
    if(bt_mac_valid(g_sel_mac)){
        bt_disconnect(g_sel_mac);   /* route audio back to analog + clear g_route/autoroute BEFORE removing the sink */
        char cmd[128];
        snprintf(cmd, sizeof cmd, "bluetoothctl remove %s >/dev/null 2>&1", g_sel_mac);
        {   char o[256], chk[96], info[512];
            int ok = run_cap_to(cmd, o, sizeof o, 5000) >= 0 && g_last_exit == 0;     /* the command really succeeded */
            if(ok){ snprintf(chk, sizeof chk, "bluetoothctl info %s 2>/dev/null", g_sel_mac);   /* and it's really gone */
                    if(run_cap_to(chk, info, sizeof info, 3000) > 0 && strstr(info, g_sel_mac)) ok = 0; }
            if(ok && !strcmp(cfg_get_str("bt_last", ""), g_sel_mac)) cfg_set_str("bt_last", "");   /* don't call it back */
            ui_toast(ok ? "Device forgotten" : "Couldn't forget device"); }
    }
    screen_back();
    if(g_scan_timer) lv_timer_del(g_scan_timer);
    g_scan_timer = lv_timer_create(scan_timer_cb, 3000, NULL);
    lv_timer_set_repeat_count(g_scan_timer, 1);
}
static void info_action_row(const char *label, lv_event_cb_t cb){
    lv_obj_t *r = lv_button_create(g_info_list);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, 250, 44);
    lv_obj_set_style_radius(r, 8, 0);
    lv_obj_set_style_bg_color(r, TC(DANGER_SURFACE), 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(r, TC(DANGER_SURFACE_PRESSED), LV_STATE_PRESSED);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    ui_on(r, cb, LV_EVENT_CLICKED, NULL, "bt.cb", UI_CORE);
    lv_obj_t *t = lv_label_create(r);
    lv_label_set_text(t, label); lv_obj_center(t);
    lv_obj_set_style_text_font(t, TF(UI_14), 0);
    lv_obj_set_style_text_color(t, TC(STATUS_DANGER), 0);
}
static void info_disc_cb(lv_event_t *e){
    if(lv_event_get_code(e)!=LV_EVENT_CLICKED) return;
    bt_disconnect(g_sel_mac);
    screen_back();
    /* re-list shortly so the now-disconnected device loses its ✓ / blue styling
     * (mirrors the connect path; disconnect is async via bluetoothctl). */
    if(g_scan_timer) lv_timer_del(g_scan_timer);
    g_scan_timer = lv_timer_create(scan_timer_cb, 4000, NULL);
    lv_timer_set_repeat_count(g_scan_timer, 1);
}
static void info_back_cb(lv_event_t *e){ if(lv_event_get_code(e)==LV_EVENT_CLICKED) screen_back(); }

void bt_info_create(lv_obj_t *root){
    lv_obj_set_style_bg_color(root, TC(CANVAS), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    ui_header_cb(root, "Device", info_back_cb);   /* shared header */
    g_info_list = lv_obj_create(root);
    lv_obj_remove_style_all(g_info_list);
    lv_obj_set_pos(g_info_list, 55, 78); lv_obj_set_size(g_info_list, 250, 190);
    lv_obj_set_style_bg_opa(g_info_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_row(g_info_list, 8, 0);
    lv_obj_set_flex_flow(g_info_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(g_info_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scroll_dir(g_info_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(g_info_list, LV_SCROLLBAR_MODE_OFF);

    /* Disconnect button under the info rows */
    lv_obj_t *db = lv_button_create(root);
    lv_obj_remove_style_all(db);
    lv_obj_set_pos(db, 110, 286); lv_obj_set_size(db, 140, 38);
    lv_obj_set_style_radius(db, 12, 0);
    lv_obj_set_style_bg_color(db, TC(DANGER_PANEL_SURFACE), 0);
    lv_obj_set_style_bg_opa(db, LV_OPA_COVER, 0);
    ui_on(db, info_disc_cb, LV_EVENT_CLICKED, NULL, "bt.info_disc", UI_CORE);
    lv_obj_t *dl = lv_label_create(db);
    lv_label_set_text(dl, "Disconnect");
    lv_obj_center(dl);
    lv_obj_set_style_text_font(dl, TF(UI_16), 0);
    lv_obj_set_style_text_color(dl, TC(STATUS_DANGER_STRONG), 0);
}

void bt_info_open(void){
    if(!g_info_list || !bt_mac_valid(g_sel_mac)) return;
    lv_obj_clean(g_info_list);
    char cmd[128], buf[2048], val[96];
    snprintf(cmd, sizeof cmd, "bluetoothctl info %s 2>/dev/null", g_sel_mac);
    run_cap(cmd, buf, sizeof buf);
    char *p = strstr(buf, "Name: ");
    if(p){ sscanf(p+6, "%95[^\n]", val); info_row("Name", val); }
    info_row("Address", g_sel_mac);
    { char v[96];   /* line-anchored property reads: a device NAME can't spoof Connected/Icon */
      info_row("Connected", (bt_info_prop(buf,"Connected:",v,sizeof v) && !strcmp(v,"yes")) ? "Yes" : "No");
      info_row("Paired",    (bt_info_prop(buf,"Paired:",   v,sizeof v) && !strcmp(v,"yes")) ? "Yes" : "No");
      if(bt_info_prop(buf,"Icon:",v,sizeof v)) info_row("Type", v); }
    info_row("Audio", "On (beta)");   /* routing works; SBC over this CPU can be rough. short: value label is 142px */
    info_action_row("Forget This Device", info_forget_cb);   /* C13: unpair + untrust */
    screen_show(SCR_BT_INFO);
}

/* ---- device list -------------------------------------------------------- */
/* A row's data rides in its event callbacks, NOT in lv_obj user data (the curved-list helper owns that slot). */
typedef struct { int connected; char mac[20]; } devrow_t;
static void row_free_cb(lv_event_t *e){ free(lv_event_get_user_data(e)); }
static void dev_cb(lv_event_t *e){
    if(lv_event_get_code(e)!=LV_EVENT_CLICKED) return;
    const devrow_t *dr = lv_event_get_user_data(e);
    if(!dr) return;
    const char *mac = dr->mac;
    int connected = dr->connected;
    if(!mac[0]) return;
    if(connected){
        snprintf(g_sel_mac, sizeof g_sel_mac, "%s", mac);
        ui_route_bt(mac);        /* tapping a connected audio device routes playback to it (no-op if already) */
        bt_info_open();
    } else {
        bt_connect(mac);
        list_msg("Connecting " LV_SYMBOL_BLUETOOTH);
        if(g_hsub) lv_label_set_text(g_hsub, "Connecting...");
        /* re-list shortly so the new connection picks up its ✓ */
        if(g_scan_timer) lv_timer_del(g_scan_timer);
        g_scan_timer = lv_timer_create(scan_timer_cb, 4000, NULL);
        lv_timer_set_repeat_count(g_scan_timer, 1);
    }
}

#define BROW_W 268
static void add_dev_row(const char *mac, const char *name, int connected, const char *codec){
    lv_obj_t *r = lv_button_create(g_list);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, g_b2 ? 280 : BROW_W, g_b2 ? 66 : 64);                       /* taller rows */
    lv_obj_add_flag(r, LV_OBJ_FLAG_USER_1);                 /* curves with the circle */
    lv_obj_set_style_radius(r, TH_R_ROW, 0);
    lv_obj_set_style_bg_color(r, connected ? TC(SURFACE_RAISED) : TC(SURFACE), 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(r, TC(SURFACE_RAISED), LV_STATE_PRESSED);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    devrow_t *dr = malloc(sizeof *dr);
    if(dr){ dr->connected = connected; snprintf(dr->mac, sizeof dr->mac, "%s", mac); }
    lv_obj_add_event_cb(r, dev_cb, LV_EVENT_CLICKED, dr);
    lv_obj_add_event_cb(r, row_free_cb, LV_EVENT_DELETE, dr);

    lv_obj_t *ic = lv_label_create(r);                       /* headphones: accent when connected */
    lv_label_set_text(ic, LV_SYMBOL_AUDIO);
    lv_obj_set_pos(ic, 14, 22);
    lv_obj_set_style_text_font(ic, TF(UI_16), 0);
    lv_obj_set_style_text_color(ic, connected ? (th_null()?TC(TEXT_PRIMARY):g_b2?b2_accent():ui_current_accent()) : TC(TEXT_SECONDARY), 0);
    lv_obj_t *t = lv_label_create(r);
    lv_label_set_text(t, name);
    lv_label_set_long_mode(t, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_pos(t, 42, 7); lv_obj_set_size(t, BROW_W - 42 - (connected ? 40 : 16), 28);
    lv_obj_set_style_text_font(t, th_simple()?TF(UI_22):th_null()?TF(UI_20):ui_font_cjk(20), 0);       /* BT device names are user data: Cyrillic/CJK-capable (issue #3) */
    lv_obj_set_style_text_color(t, TC(TEXT_PRIMARY), 0);
    lv_obj_t *sl = lv_label_create(r);
    char st[40]; snprintf(st, sizeof st, "Connected%s%s", (codec && codec[0]) ? " - " : "", (codec && codec[0]) ? codec : "");
    lv_label_set_text(sl, connected ? st : "Tap to connect");
    lv_obj_set_pos(sl, 42, 37);
    lv_obj_set_style_text_font(sl, TF(UI_18), 0);           /* the hint line, a bit larger (was 16) */
    lv_obj_set_style_text_color(sl, connected ? (th_null()?TC(TEXT_PRIMARY):g_b2?b2_accent():ui_current_accent()) : TC(TEXT_SECONDARY), 0);
    if(g_b2){ lv_obj_set_pos(ic, 14, 23); lv_obj_set_pos(t, 42, 6); lv_obj_set_width(t, 280 - 42 - (connected ? 40 : 16)); lv_obj_set_pos(sl, 42, 36); }
    if(connected){ lv_obj_t *ch = lv_label_create(r); lv_label_set_text(ch, LV_SYMBOL_RIGHT);   /* opens its details */
                   lv_obj_align(ch, LV_ALIGN_RIGHT_MID, -16, 0); lv_obj_set_style_text_color(ch, TC(TEXT_DISABLED), 0);
                   if(th_classic()){ lv_obj_set_style_text_font(ch, TF(UI_14), 0); lv_obj_set_style_text_color(ch, TC(TEXT_PRIMARY), 0); lv_obj_align(ch, LV_ALIGN_RIGHT_MID, -10, 0); } }
    if(th_classic()){   /* Graphite: a 34 px menu row - the name, and the codec (or "On") when connected */
        lv_obj_set_size(r, 260, 34); lv_obj_set_style_bg_opa(r, LV_OPA_TRANSP, 0);
        lv_obj_add_flag(ic, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(sl, connected ? ((codec && codec[0]) ? codec : "On") : "");
        lv_obj_set_style_text_font(sl, TF(UI_16), 0); lv_obj_set_style_text_color(sl, TC(TEXT_SECONDARY), 0);
        lv_obj_align(sl, LV_ALIGN_RIGHT_MID, connected ? -28 : -10, 0);
        lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
        lv_obj_set_size(t, connected ? 150 : 236, lv_font_get_line_height(ui_font_cjk(20)));
        lv_obj_align(t, LV_ALIGN_LEFT_MID, 10, 0);
        classic_row_style(r);
    }
}

/* Show only real AUDIO devices (bluez Icon = audio-card / audio-headset / ...),
 * so BLE gadgets and the "LE-" advertisement entries (which carry no A2DP) are
 * hidden and the user can only tap a connectable sink.  Connected ✓ comes from
 * bluez's own "Connected: yes" (not a raw ACL link, which over-reports). */
/* Enumeration runs OFF the main thread: `bluetoothctl devices` plus a per-device
 * `bluetoothctl info` is up to ~60 blocking popen() spawns - running that inline froze
 * the LVGL loop for seconds (H5) and risked the fiio_init hardware watchdog. The worker
 * builds ONLY plain data; the main thread renders rows from it (LVGL is main-thread-only).
 * A generation counter drops a superseded worker's result if a newer scan started. */
/* The A2DP codec in use for a connected device, e.g. "SBC": read from bluealsa's PCM for that device (the line
 * "Codec:" / "Selected codec:" of `bluealsa-cli info`). Runs on the scan worker thread, bounded; "" when unknown. */
static void bt_codec_of(const char *mac, char *out, int cap){
    if(cap < 1) return;
    out[0] = 0;
    char m[20]; int i = 0;
    for(; mac[i] && i < 17; i++) m[i] = (mac[i] == ':') ? '_' : mac[i];
    m[i] = 0;
    char cmd[200], buf[160];
    snprintf(cmd, sizeof cmd, "bluealsa-cli info /org/bluealsa/hci0/dev_%s/a2dpsrc/sink 2>/dev/null | grep -i -E '^[[:space:]]*(selected )?codec:' | head -1", m);
    if(run_cap_to(cmd, buf, sizeof buf, 1500) <= 0) return;
    char *c = strchr(buf, ':'); if(!c) return;
    c++; while(*c == ' ' || *c == '\t') c++;
    int k = 0;
    while(*c && k < cap - 1 && ((*c >= 'A' && *c <= 'Z') || (*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '-')){
        out[k++] = (*c >= 'a' && *c <= 'z') ? (char)(*c - 32) : *c; c++;
    }
    out[k] = 0;
}
typedef struct { char mac[20]; char name[128]; int connected; char codec[16]; } bt_scan_dev_t;
static pthread_mutex_t g_scan_mu = PTHREAD_MUTEX_INITIALIZER;
static bt_scan_dev_t   g_scan_res[40];
static int             g_scan_n = 0;         /* [g_scan_mu] result count */
static int             g_scan_done = 0;      /* [g_scan_mu] worker finished -> main renders */
static int             g_scan_running = 0;   /* [g_scan_mu] a worker is in flight */
static unsigned        g_scan_gen = 0;       /* [g_scan_mu] bumped per scan; worker tags its result */
static lv_timer_t     *g_scanpoll_timer;
/* "Connected: <headphones> - <codec>", once per connection, when the codec is settled: after a tap and when
 * headphones reconnect by themselves. The codec is what BlueALSA reports running; the name comes from the device list,
 * or bluetoothctl if it isn't there. bt_announce_reset() when the link goes, so the next connection is announced. */
static char g_announced[20];
static void bt_announce_reset(void){ g_announced[0] = 0; }
static void bt_announce(const char *mac, const char *codec){
    if(!bt_mac_valid(mac) || !strcmp(g_announced, mac)) return;
    snprintf(g_announced, sizeof g_announced, "%s", mac);
    char name[64] = "";
    pthread_mutex_lock(&g_scan_mu);
    for(int i = 0; i < g_scan_n && i < 40; i++) if(!strcasecmp(g_scan_res[i].mac, mac)){ snprintf(name, sizeof name, "%s", g_scan_res[i].name); break; }
    pthread_mutex_unlock(&g_scan_mu);
    if(!name[0]){ char cmd[96], info[1024]; snprintf(cmd, sizeof cmd, "bluetoothctl info %s 2>/dev/null", mac);
                  if(run_cap_to(cmd, info, sizeof info, 1500) > 0) bt_info_prop(info, "Name:", name, sizeof name); }
    char msg[128];
    if(name[0]) snprintf(msg, sizeof msg, "Connected: %s\n%s", name, codec && codec[0] ? codec : "SBC");   /* name, then codec */
    else snprintf(msg, sizeof msg, "Connected\n%s", codec && codec[0] ? codec : "SBC");
    ui_toast(msg);
}
static int             g_scanpoll_n = 0;
static int             g_scan_pending = 0;   /* main-thread: a re-enumerate was requested while a worker ran */

static void *scan_worker(void *arg){
    unsigned my_gen = (unsigned)(intptr_t)arg;
    char buf[8192]; if(run_cap_to("bluetoothctl devices 2>/dev/null", buf, sizeof buf, 4000) < 0) buf[0] = 0;
    bt_scan_dev_t res[40]; int n = 0;
    char *l = buf;
    while(l && *l && n < 40){
        char *nl = strchr(l, '\n'); if(nl) *nl = 0;
        /* line: "Device AA:BB:CC:DD:EE:FF Friendly Name" */
        if(!strncmp(l, "Device ", 7) && strlen(l) > 7+17){
            char mac[20]; memcpy(mac, l+7, 17); mac[17]=0;
            const char *name = l + 7 + 17 + 1;
            if(name[0] && bt_mac_valid(mac)){
                char cmd[128], info[2048];
                snprintf(cmd, sizeof cmd, "bluetoothctl info %s 2>/dev/null", mac);
                if(run_cap_to(cmd, info, sizeof info, 3000) < 0) break;   /* bluetoothd not answering: stop, publish what we have */
                char icon[64], cv[16];   /* line-anchored: a spoofed device name can't fake Icon/Connected */
                if(bt_info_prop(info, "Icon:", icon, sizeof icon) && !strncmp(icon, "audio", 5)){  /* audio sinks only */
                    snprintf(res[n].mac,  sizeof res[n].mac,  "%s", mac);
                    snprintf(res[n].name, sizeof res[n].name, "%s", name);
                    res[n].connected = (bt_info_prop(info,"Connected:",cv,sizeof cv) && !strcmp(cv,"yes"));
                    res[n].codec[0] = 0;
                    if(res[n].connected) bt_codec_of(mac, res[n].codec, sizeof res[n].codec);
                    n++;
                }
            }
        }
        if(!nl) break;
        l = nl + 1;
    }
    pthread_mutex_lock(&g_scan_mu);
    if(my_gen == g_scan_gen){        /* still current (not abandoned): publish results + free the latch.
                                      * Only the current-gen worker clears g_scan_running, so an
                                      * abandoned worker can't stomp a newer scan's state. */
        memcpy(g_scan_res, res, (size_t)n * sizeof(bt_scan_dev_t));
        g_scan_n = n; g_scan_done = 1;
        g_scan_running = 0;
    }
    pthread_mutex_unlock(&g_scan_mu);
    return NULL;
}

/* main thread only: render the worker's results into g_list (LVGL touched only here) */
static void scan_render(void){
    if(!g_list) return;
    scan_stop();
    lv_obj_clean(g_list);
    lv_obj_set_flex_align(g_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);  /* rows top-aligned */
    static bt_scan_dev_t local[40]; int n;
    pthread_mutex_lock(&g_scan_mu);
    n = g_scan_n; if(n > 40) n = 40;
    memcpy(local, g_scan_res, (size_t)n * sizeof(bt_scan_dev_t));
    pthread_mutex_unlock(&g_scan_mu);
    for(int i = 0; i < n; i++) if(local[i].connected)  add_dev_row(local[i].mac, local[i].name, 1, local[i].codec);  /* connected first */
    for(int i = 0; i < n; i++) if(!local[i].connected) add_dev_row(local[i].mac, local[i].name, 0, NULL);
    if(n == 0) list_msg("No audio devices found");
    else { lv_obj_update_layout(g_list); curvelist_update(&g_bcl); }
    {   const char *cn = NULL, *cc = NULL;
        for(int i = 0; i < n; i++) if(local[i].connected){ cn = local[i].name; cc = local[i].codec; break; }
        char hs[200];   /* one line, scrolling when too long: "Connected to FiiO EH11 - LDAC" (2.6.2 polish) */
        if(cn) snprintf(hs, sizeof hs, "Connected to %s%s%s", cn, (cc && cc[0]) ? " - " : "", (cc && cc[0]) ? cc : "");
        hdr_set(BT_ON, cn ? hs : "Bluetooth is on", cn ? "" : "Connect a speaker to play audio (beta)"); }
    /* NOTE: audio output routing to the BT sink is intentionally DISABLED.
     * bluez+bluealsa does SBC encoding in software, which is too heavy for this
     * MIPS CPU (laggy audio + watchdog reboots), and the 0666 output-mux command
     * destabilises mq_player's local audio engine.  The page still pairs/connects
     * devices, but the player always uses its native DAC output.  Smooth BT audio
     * would require the stock Broadcom BSA (hardware a2dp), a separate effort. */
}

/* main thread: poll until the worker finishes, then render. Keeps waiting (does NOT
 * render stale results early); only a wedged worker (~30s) is abandoned - and then the
 * generation is bumped + the latch freed so its late write is dropped and new scans work. */
static void scanpoll_cb(lv_timer_t *t){
    (void)t;
    int done;
    pthread_mutex_lock(&g_scan_mu); done = g_scan_done; pthread_mutex_unlock(&g_scan_mu);
    if(!done){
        if(++g_scanpoll_n < 100) return;   /* keep waiting (~30s) for the worker */
        pthread_mutex_lock(&g_scan_mu); g_scan_gen++; g_scan_running = 0; pthread_mutex_unlock(&g_scan_mu);
    }
    if(g_scanpoll_timer){ lv_timer_del(g_scanpoll_timer); g_scanpoll_timer = NULL; }
    if(g_scan_pending){ g_scan_pending = 0; scan_kick(); return; }   /* a refresh was queued mid-run -> re-enumerate fresh instead of rendering the stale result */
    scan_render();
}

/* (re)start the render poll timer (main thread) */
static void scan_ensure_poll(void){
    g_scanpoll_n = 0;
    if(g_scanpoll_timer) lv_timer_del(g_scanpoll_timer);
    g_scanpoll_timer = lv_timer_create(scanpoll_cb, 300, NULL);
}

/* main thread: spawn the off-thread enumeration. If one is already running, just make
 * sure the poll is live so its result still renders (don't pile up a second worker). */
static void scan_kick(void){
    pthread_mutex_lock(&g_scan_mu);
    if(g_scan_running){   /* a worker is mid-run: queue a fresh re-enumerate for when it finishes,
                           * so a stale (e.g. pre-connect) result isn't the final render */
        pthread_mutex_unlock(&g_scan_mu); g_scan_pending = 1; scan_ensure_poll(); return;
    }
    g_scan_running = 1; g_scan_done = 0; g_scan_n = 0; g_scan_gen++;   /* clear stale results: a timed-out/failed scan then renders honestly, not the last list */
    unsigned gen = g_scan_gen;
    pthread_mutex_unlock(&g_scan_mu);
    g_scan_pending = 0;   /* this fresh worker satisfies any queued refresh */
    pthread_t th;
    if(pthread_create(&th, NULL, scan_worker, (void*)(intptr_t)gen) == 0){
        pthread_detach(th);
        scan_ensure_poll();     /* render when the worker signals done */
    } else {   /* couldn't spawn -> drop the latch, kill any stale poll, render what we have */
        pthread_mutex_lock(&g_scan_mu); g_scan_running = 0; pthread_mutex_unlock(&g_scan_mu);
        if(g_scanpoll_timer){ lv_timer_del(g_scanpoll_timer); g_scanpoll_timer = NULL; }
        scan_render();
    }
}

static void scan_timer_cb(lv_timer_t *t){
    (void)t;
    if(g_scan_timer){ lv_timer_del(g_scan_timer); g_scan_timer = NULL; }
    scan_kick();   /* enumerate off-thread; scanpoll_cb renders when done (no main-thread freeze) */
}
/* Cancel every pending scan artifact and abandon any in-flight worker (bump gen so its
 * late result is dropped). Used when Bluetooth is turned off so a delayed scan can't
 * render device rows over the "Bluetooth is off" message. */
static void scan_abort(void){
    if(g_scan_timer){ lv_timer_del(g_scan_timer); g_scan_timer = NULL; }
    if(g_scanpoll_timer){ lv_timer_del(g_scanpoll_timer); g_scanpoll_timer = NULL; }
    if(g_scanwait_timer){ lv_timer_del(g_scanwait_timer); g_scanwait_timer = NULL; }  /* cancel the bt_open observer too */
    pthread_mutex_lock(&g_scan_mu);
    g_scan_gen++; g_scan_running = 0; g_scan_n = 0; g_scan_done = 0;
    pthread_mutex_unlock(&g_scan_mu);
}
static void start_scan(void){
    if(!bt_on()){ list_msg("Bluetooth is off"); return; }
    list_msg_scanning();
    /* Classic BR/EDR inquiry - what BT speakers/headphones use - needs ~10-12s to find and
     * resolve a device's name/class; the old 6s window quit before speakers ever appeared. */
    scan_proc_stop();                                       /* never two searches at once */
    {   /* the search is our own child (own process group): stopping it ends its BlueZ discovery session, which a
         * separate "bluetoothctl scan off" cannot (BlueZ ties a discovery session to the client that started it) */
        posix_spawn_file_actions_t fa; posix_spawn_file_actions_init(&fa);
        posix_spawn_file_actions_addopen(&fa, 1, "/dev/null", O_WRONLY, 0);
        posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
        posix_spawnattr_t at; posix_spawnattr_init(&at);
        posix_spawnattr_setflags(&at, POSIX_SPAWN_SETPGROUP); posix_spawnattr_setpgroup(&at, 0);
        char *argv[] = { "bluetoothctl", "--timeout", "13", "scan", "on", NULL };
        extern char **environ;
        pid_t pid;
        if(posix_spawnp(&pid, "bluetoothctl", &fa, &at, argv, environ) == 0) g_scan_pid = pid;
        posix_spawn_file_actions_destroy(&fa); posix_spawnattr_destroy(&at);
    }
    if(g_scan_timer) lv_timer_del(g_scan_timer);
    g_scan_timer = lv_timer_create(scan_timer_cb, 13500, NULL);
    lv_timer_set_repeat_count(g_scan_timer, 1);
}

/* ---- radio toggle ------------------------------------------------------- */
/* bt_enable() is slow (rfkill cycle + patchram firmware download + hci0 bring-up,
 * several seconds). A fixed settle timer would either fire too early ("No audio
 * devices found" before the radio is up) or never actively scan. Poll bt_on()
 * until the adapter is Powered, then run a real scan. */
static lv_timer_t *g_radio_timer;
static uint32_t    g_radio_start;
/* A device search shares the one small radio with Bluetooth audio and caused split-second dropouts during LDAC
 * playback (#13). While sound goes to headphones, the page lists the known devices instead; the user can still
 * search on request (the search button). */
static void search_or_list(void){
    if(ui_route_mac()[0]) scan_kick();                    /* known devices, no discovery window */
    else start_scan();
}
static void delayed_scan_cb(lv_timer_t *t){ (void)t; if(bt_on() && screen_current() == SCR_BT) search_or_list(); }
static void radio_on_poll_cb(lv_timer_t *t){
    (void)t;
    if(bt_on()){
        lv_timer_del(g_radio_timer); g_radio_timer = NULL;
        bt_ensure_services();
        bt_autoroute_start();
        bt_reconnect_last();           /* first the last headphones; a search would get in the way of connecting */
        if(screen_current() == SCR_BT){
            if(cfg_get_str("bt_last", "")[0]){ list_msg("Looking for your headphones...");
                                               lv_timer_t *st = lv_timer_create(delayed_scan_cb, 9000, NULL);
                                               lv_timer_set_repeat_count(st, 1); }
            else start_scan();
        }
        return;
    }
    if(lv_tick_elaps(g_radio_start) > 28000){   /* 2s rfkill + ~20s up-loop + daemon/power settle */
        lv_timer_del(g_radio_timer); g_radio_timer = NULL;
        /* bring-up failed: drop the persisted intent so the QS tile + boot-restore don't keep
         * showing/enforcing "on" for a radio that never came up. Tear the stack down so the cleared
         * intent matches a real off state (a late/half-started enable can't linger powered), and
         * sync the screen switch. */
        cfg_set_int("bt_on", 0);
        if(g_sw) lv_obj_clear_state(g_sw, LV_STATE_CHECKED);
        bt_disable();
        list_msg("Couldn't turn on Bluetooth"); hdr_set(BT_OFF, "Bluetooth is off", "Couldn't turn it on");
    }
}
static void sw_cb(lv_event_t *e){
    if(lv_event_get_code(e)!=LV_EVENT_VALUE_CHANGED) return;
    int on = lv_obj_has_state(g_sw, LV_STATE_CHECKED);
    if(on){
        cfg_set_int("bt_on", 1);   /* persist intent so boot restores it (mirror WiFi) */
        bt_enable();
        list_msg("Please wait..."); hdr_set(BT_TURNING_ON, "Turning on...", "");
        g_radio_start = lv_tick_get();
        if(g_scanwait_timer){ lv_timer_del(g_scanwait_timer); g_scanwait_timer = NULL; }  /* radio poll owns bring-up now - keep the two mutually exclusive */
        if(g_radio_timer) lv_timer_del(g_radio_timer);
        g_radio_timer = lv_timer_create(radio_on_poll_cb, 1000, NULL);
    } else {
        cfg_set_int("bt_on", 0);   /* OFF before teardown so boot-restore won't resurrect it */
        if(g_radio_timer){ lv_timer_del(g_radio_timer); g_radio_timer = NULL; }
        if(g_bt_conn_timer){ lv_timer_del(g_bt_conn_timer); g_bt_conn_timer = NULL; }  /* else its 25s timeout scan_kick()s while BT is off */
        scan_abort();          /* cancel any pending/active scan so it can't render over "off" */
        ui_route_analog();     /* return audio to the DAC before killing the BT stack */
        bt_disable();
        list_msg("Switch it on to find devices"); hdr_set(BT_OFF, "Bluetooth is off", "");
    }
}

/* Quick Settings tile short-press: flip BT + persist intent, no screen-specific UI.
 * Mirrors sw_cb's actions (bt_boot_restore/keepalive enforce the intent). Returns new state. */
int bt_toggle(void){
    int on = (bt_state() == BT_OFF);   /* on or mid-bring-up -> turn off; off -> turn on */
    cfg_set_int("bt_on", on);
    if(on){
        bt_enable();
        g_radio_start = lv_tick_get();
        if(g_scanwait_timer){ lv_timer_del(g_scanwait_timer); g_scanwait_timer = NULL; }
        if(g_radio_timer) lv_timer_del(g_radio_timer);
        g_radio_timer = lv_timer_create(radio_on_poll_cb, 1000, NULL);
    } else {
        if(g_radio_timer){ lv_timer_del(g_radio_timer); g_radio_timer = NULL; }        /* cancel the bring-up poll too */
        if(g_bt_conn_timer){ lv_timer_del(g_bt_conn_timer); g_bt_conn_timer = NULL; }  /* don't let it scan_kick() after BT off */
        scan_abort();          /* cancel any pending/active scan */
        ui_route_analog();     /* return audio to the DAC before killing the BT stack */
        bt_disable();
    }
    if(g_hring){ if(on) hdr_set(BT_TURNING_ON, "Turning on...", ""); else hdr_set(BT_OFF, "Bluetooth is off", ""); }
    /* keep the BT screen's switch in sync so it reflects reality when opened later */
    if(g_sw){ if(on) lv_obj_add_state(g_sw, LV_STATE_CHECKED); else lv_obj_clear_state(g_sw, LV_STATE_CHECKED); }
    ui_status_refresh();   /* update the home BT icon immediately (don't wait for the next status poll) */
    return on;
}

/* ---- Bluetooth persistence: restore the radio at boot if it was on (mirrors WiFi) -------
 * diskOS owns a "bt_on" intent (cfg), which starts OFF on a fresh install (bt_init_intent below - it is NOT
 * seeded from stock SYSCONFIG.BT_STATUS); the toggle above persists every change. bt_boot_restore() (called at startup) brings the stack up if
 * the intent is on - bluez then auto-reconnects trusted speakers and bt_autoroute picks them
 * up, so BT audio survives reboots with no BT-screen visit. Uses its OWN poll timer (not
 * g_radio_timer) and never scans (no BT screen at boot). */
/* ---- Putting the aptX files in place (Settings > aptX (experimental) on) ---------------------------------------------
 * The files ride inside the signed mq_ui (aptx_blob.c), so an SD update brings them: no reflash. Written only when
 * missing or different, the library first; each goes to <name>.new, is flushed, then renamed over the old one, so a
 * running BlueALSA keeps its copy and the next Bluetooth start gets the new one. Runs on its own thread. */
static int aptx_same(const char *path, const unsigned char *d, size_t n){
    int fd = open(path, O_RDONLY | O_CLOEXEC); if(fd < 0) return 0;
    struct stat st; int same = fstat(fd, &st) == 0 && (size_t)st.st_size == n;
    unsigned char buf[4096]; size_t off = 0;
    while(same && off < n){ ssize_t r = read(fd, buf, sizeof buf); if(r <= 0 || memcmp(buf, d + off, (size_t)r)){ same = 0; break; } off += (size_t)r; }
    close(fd);
    return same;
}
/* 0 = in place (nothing written), 1 = files written, -1 = failed (no space, write error, unsupported firmware). */
static int bt_aptx_install(void){
    if(fw_os_ver() != 257) return -1;
    int changed = 0; size_t need = 0;
    char p[96], tmp[104];
    for(int i = 0; i < APTX_NFILES; i++){
        snprintf(p, sizeof p, BT_APTX_DIR "/%s", APTX_FILES[i].name);
        if(!aptx_same(p, APTX_FILES[i].data, aptx_file_size(i))) need += aptx_file_size(i);
    }
    if(!need) return 0;
    if(mkdir(BT_APTX_DIR, 0755) != 0 && errno != EEXIST) return -1;
    struct statvfs vf;
    if(statvfs(BT_APTX_DIR, &vf) == 0 && (unsigned long long)vf.f_bavail * vf.f_frsize < need + 512 * 1024ull) return -1;
    for(int i = 0; i < APTX_NFILES; i++){
        size_t n = aptx_file_size(i);
        snprintf(p, sizeof p, BT_APTX_DIR "/%s", APTX_FILES[i].name);
        if(aptx_same(p, APTX_FILES[i].data, n)) continue;
        snprintf(tmp, sizeof tmp, "%s.new", p);
        int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, APTX_FILES[i].mode);
        if(fd < 0) return -1;
        size_t off = 0; int ok = 1;
        while(off < n){ ssize_t w = write(fd, APTX_FILES[i].data + off, n - off); if(w <= 0){ ok = 0; break; } off += (size_t)w; }
        if(ok && fsync(fd) != 0) ok = 0;
        close(fd);
        if(!ok || chmod(tmp, APTX_FILES[i].mode) != 0 || rename(tmp, p) != 0){ unlink(tmp); return -1; }
        changed = 1;
    }
    sync();
    return changed;
}
static pthread_mutex_t g_ax_mu = PTHREAD_MUTEX_INITIALIZER;
static int g_ax_state, g_ax_result, g_ax_tell;   /* [g_ax_mu] state 0 idle, 1 running, 2 done; tell: toast the result */
static lv_timer_t *g_ax_timer;
static void *bt_aptx_thread(void *a){
    (void)a; int r = bt_aptx_install();
    pthread_mutex_lock(&g_ax_mu); g_ax_result = r; g_ax_state = 2; pthread_mutex_unlock(&g_ax_mu);
    fprintf(stderr, "bt aptx: files %s\n", r < 0 ? "NOT written (space, write error or firmware)" : r ? "written" : "in place"); fflush(stderr);
    return NULL;
}
static void bt_aptx_poll_cb(lv_timer_t *t){
    pthread_mutex_lock(&g_ax_mu); int st = g_ax_state, r = g_ax_result, tell = g_ax_tell; if(st == 2) g_ax_state = 0; pthread_mutex_unlock(&g_ax_mu);
    if(st != 2) return;
    lv_timer_del(t); g_ax_timer = NULL;
    if(r < 0) ui_toast("aptX couldn't be set up (storage full?) - Bluetooth stays as it was");
    else if(tell) ui_toast("aptX ready - turn Bluetooth off and on to use it");
}
/* Put the files in place if aptX is on (tell = the user just switched it on: say when it's ready). */
void bt_aptx_sync(int tell){
    if(!cfg_get_int("bt_aptx", 0) || fw_os_ver() != 257) return;
    pthread_mutex_lock(&g_ax_mu);
    if(g_ax_state == 1){ g_ax_tell |= tell; pthread_mutex_unlock(&g_ax_mu); return; }
    g_ax_state = 1; g_ax_tell = tell;
    pthread_mutex_unlock(&g_ax_mu);
    pthread_t th;
    if(pthread_create(&th, NULL, bt_aptx_thread, NULL) != 0){ pthread_mutex_lock(&g_ax_mu); g_ax_state = 0; pthread_mutex_unlock(&g_ax_mu); return; }
    pthread_detach(th);
    if(!g_ax_timer) g_ax_timer = lv_timer_create(bt_aptx_poll_cb, 400, NULL);
}
/* Settings > aptX (experimental) changed. */
void bt_aptx_setting_changed(int on){
    if(on) bt_aptx_sync(1);
    else ui_toast("aptX off - the stock Bluetooth is used from the next Bluetooth start");
}
void bt_init_intent(void){
    { int c = cfg_get_int("bt_codec", 0); if(c == 5 || c == 6) cfg_set_int("bt_codec", 3); }   /* old aptX choice: aptX/LDAC Standard */
    bt_guard_boot();
    bt_wifi_quiet_boot();                                 /* undo a Wi-Fi quiet a previous mq_ui left behind */
    bt_aptx_sync(0);                                       /* aptX on: make sure this mq_ui's files are in place */
    if(cfg_get_int("bt_on", -1) >= 0) return;             /* already owned by diskOS - keep the user's choice */
    /* Default OFF on a fresh diskOS: do NOT inherit stock's SYSCONFIG.BT_STATUS. BT on-by-default put
     * users one tap from the freeze bug (now fixed), and most listening here is wired - so a new install
     * starts with Bluetooth off and the user enables it when they want it. */
    cfg_set_int("bt_on", 0);
}
static lv_timer_t *g_bootrestore_timer;
static uint32_t    g_bootrestore_start;
static void bootrestore_poll_cb(lv_timer_t *t){
    (void)t;
    if(bt_on()){
        lv_timer_del(g_bootrestore_timer); g_bootrestore_timer = NULL;
        bt_ensure_services();
        bt_autoroute_start();          /* speaker auto-reconnects -> auto-route routes it */
        bt_reconnect_last();           /* and the Disc calls the last headphones itself */
        return;
    }
    if(lv_tick_elaps(g_bootrestore_start) > 28000){       /* patchram + power-on settle */
        lv_timer_del(g_bootrestore_timer); g_bootrestore_timer = NULL;
    }
}
void bt_boot_restore(void){
    bt_init_intent();
    if(cfg_get_int("bt_on", 0) != 1) return;              /* was off -> stay off */
    if(bt_on()){ bt_ensure_services(); bt_autoroute_start(); return; }   /* already up (UI restart) */
    bt_enable();
    g_bootrestore_start = lv_tick_get();
    if(g_bootrestore_timer) lv_timer_del(g_bootrestore_timer);
    g_bootrestore_timer = lv_timer_create(bootrestore_poll_cb, 1000, NULL);
}

static void rescan_cb(lv_event_t *e){ if(lv_event_get_code(e)==LV_EVENT_CLICKED) start_scan(); }
__attribute__((unused)) static void back_cb(lv_event_t *e){ if(lv_event_get_code(e)==LV_EVENT_CLICKED) screen_back(); }

void bt_create(lv_obj_t *root){
    lv_obj_set_style_bg_color(root, TC(CANVAS), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    g_hring = lv_arc_create(root);
    lv_obj_remove_style(g_hring, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(g_hring, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(g_hring, 84, 84); lv_obj_align(g_hring, LV_ALIGN_TOP_MID, 0, 34);
    lv_arc_set_rotation(g_hring, 270); lv_arc_set_bg_angles(g_hring, 0, 360); lv_arc_set_range(g_hring, 0, 1000); lv_arc_set_value(g_hring, 0);
    lv_obj_set_style_arc_width(g_hring, 4, LV_PART_MAIN); lv_obj_set_style_arc_color(g_hring, TC(CONTROL_TRACK), LV_PART_MAIN);
    lv_obj_set_style_arc_width(g_hring, 4, LV_PART_INDICATOR); lv_obj_set_style_arc_rounded(g_hring, true, LV_PART_INDICATOR);
    g_hglyph = lv_label_create(root);
    lv_label_set_text(g_hglyph, LV_SYMBOL_BLUETOOTH);
    lv_obj_set_style_text_font(g_hglyph, TF(UI_28), 0);
    lv_obj_align(g_hglyph, LV_ALIGN_TOP_MID, 0, 62);
    g_hname = lv_label_create(root);
    lv_label_set_long_mode(g_hname, LV_LABEL_LONG_SCROLL_CIRCULAR); lv_obj_set_width(g_hname, 230);
    lv_obj_set_style_text_align(g_hname, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(g_hname, ui_font_cjk(18), 0);
    lv_obj_align(g_hname, LV_ALIGN_TOP_MID, 0, 128);
    g_hsub = lv_label_create(root);
    lv_label_set_long_mode(g_hsub, LV_LABEL_LONG_SCROLL_CIRCULAR); lv_obj_set_width(g_hsub, 250);
    lv_obj_set_style_text_align(g_hsub, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(g_hsub, ui_font_cjk(14), 0); lv_obj_set_style_text_color(g_hsub, TC(TEXT_SECONDARY), 0);
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

    g_list = lv_obj_create(root);
    lv_obj_remove_style_all(g_list);
    lv_obj_set_pos(g_list, (360 - BROW_W) / 2, 182); lv_obj_set_size(g_list, BROW_W, 170);
    lv_obj_set_style_pad_bottom(g_list, 30, 0);
    lv_obj_set_style_bg_opa(g_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_row(g_list, 4, 0);
    lv_obj_set_flex_flow(g_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(g_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scroll_dir(g_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(g_list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(g_list, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    curvelist_attach(&g_bcl, g_list, root, BROW_W);
    hdr_set(BT_OFF, "Bluetooth", "");
    /* less empty space at the top, one legible "Connected to ..." line, a larger hint, a taller list.
     * Shared layout (Disco, Ring, Stone) here; Squash, Null and Braun after their skins below. */
    lv_obj_align(g_hring, LV_ALIGN_TOP_MID, 0, 20); lv_obj_align(g_hglyph, LV_ALIGN_TOP_MID, 0, 48);
    lv_obj_align(g_sw, LV_ALIGN_TOP_MID, 106, 50); lv_obj_align(rb, LV_ALIGN_TOP_MID, -106, 45);
    lv_obj_set_width(g_hname, 296); lv_obj_set_style_text_font(g_hname, ui_font_cjk(18), 0);   /* fits a typical name; longer ones scroll */
    lv_obj_align(g_hname, LV_ALIGN_TOP_MID, 0, 110);
    lv_obj_set_style_text_font(g_hsub, ui_font_cjk(16), 0); lv_obj_align(g_hsub, LV_ALIGN_TOP_MID, 0, 138);
    lv_obj_set_pos(g_list, (360 - BROW_W) / 2, 162); lv_obj_set_size(g_list, BROW_W, 190);
    if(th_simple())simple_radio_skin(g_hring,g_hglyph,g_hname,g_hsub,g_sw,rb,g_list);
    if(th_simple()){                                    /* Squash: the same, in its proportions */
        lv_obj_align(g_hring, LV_ALIGN_TOP_MID, 0, 26); lv_obj_align(g_hglyph, LV_ALIGN_TOP_MID, 0, 38);
        lv_obj_align(rb, LV_ALIGN_TOP_MID, -86, 44); lv_obj_align(g_sw, LV_ALIGN_TOP_MID, 92, 52);
        lv_obj_set_size(g_hname, 308, 28); lv_obj_set_style_text_font(g_hname, TF(UI_20), 0); lv_obj_align(g_hname, LV_ALIGN_TOP_MID, 0, 116);
        lv_obj_set_style_text_font(g_hsub, TF(UI_20), 0); lv_obj_align(g_hsub, LV_ALIGN_TOP_MID, 0, 146);
        lv_obj_set_pos(g_list, 30, 170); lv_obj_set_size(g_list, 300, 176);
        lv_label_set_long_mode(g_hname, LV_LABEL_LONG_SCROLL_CIRCULAR);
    }
    if(th_null()){null_radio_skin(root,"BLUETOOTH",g_hring,g_hglyph,g_hname,g_hsub,g_sw,rb,g_list);
        lv_obj_set_style_text_font(g_hsub, TF(UI_18), 0);              /* Null keeps its layout; the hint a bit larger */
        lv_label_set_long_mode(g_hname, LV_LABEL_LONG_SCROLL_CIRCULAR); }
    if(th_zune())zune_radio_skin(root,"bluetooth","bluetooth on","scan for devices",g_hring,g_hglyph,g_hname,g_hsub,g_sw,rb,g_list);
    if(th_classic())classic_radio_skin(root,"Bluetooth",g_hring,g_hglyph,g_hname,g_hsub,g_sw,rb,g_list);
    g_b2 = th_simple() || th_disco();
    if(g_b2){                                                  /* Squash / Disco: the B2 header (see b2_ring) */
        const int cy = 52;                                       /* the control row's centre */
        lv_obj_add_flag(g_hname, LV_OBJ_FLAG_HIDDEN);
        g_cdisc = lv_obj_create(root); lv_obj_remove_style_all(g_cdisc); kit_keep(g_cdisc);
        lv_obj_set_size(g_cdisc, 72, 72); lv_obj_set_style_radius(g_cdisc, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(g_cdisc, LV_OPA_COVER, 0); lv_obj_remove_flag(g_cdisc, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_align(g_cdisc, LV_ALIGN_TOP_MID, 0, cy - 36); lv_obj_move_to_index(g_cdisc, 0);
        lv_obj_set_size(g_hring, 86, 86); lv_obj_align(g_hring, LV_ALIGN_TOP_MID, 0, cy - 43);
        lv_obj_set_style_arc_width(g_hring, 3, LV_PART_MAIN); lv_obj_set_style_arc_width(g_hring, 3, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(g_hring, TC(SURFACE_RAISED), LV_PART_MAIN);
        lv_obj_set_style_text_font(g_hglyph, TF(UI_40), 0); lv_obj_set_size(g_hglyph, 60, LV_SIZE_CONTENT);
        lv_obj_set_style_pad_top(g_hglyph, 0, 0); lv_obj_set_style_text_align(g_hglyph, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_update_layout(g_hglyph); lv_obj_align(g_hglyph, LV_ALIGN_TOP_MID, 0, cy - lv_obj_get_height(g_hglyph) / 2);
        /* refresh: a bare grey icon. A plain object, not a button, so the theme kit doesn't dress it up again */
        lv_obj_add_flag(rb, LV_OBJ_FLAG_HIDDEN);
        lv_obj_t *rx = lv_obj_create(root); lv_obj_remove_style_all(rx); kit_bare(rx);
        lv_obj_set_size(rx, 40, 40); lv_obj_align(rx, LV_ALIGN_TOP_MID, -88, cy - 20);
        lv_obj_add_flag(rx, LV_OBJ_FLAG_CLICKABLE); lv_obj_set_ext_click_area(rx, 10); lv_obj_remove_flag(rx, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(rx, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(rx, TC(SURFACE), LV_STATE_PRESSED); lv_obj_set_style_bg_opa(rx, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_add_event_cb(rx, rescan_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_t *rxl = lv_label_create(rx); kit_keep(rxl); lv_label_set_text(rxl, LV_SYMBOL_REFRESH);
        lv_obj_set_style_text_font(rxl, TF(UI_18), 0); lv_obj_set_style_text_color(rxl, TC(TEXT_DISABLED), 0); lv_obj_center(rxl);
        /* the switch: smaller, a soft track; on = the theme's accent */
        lv_obj_set_size(g_sw, 42, 22); lv_obj_align(g_sw, LV_ALIGN_TOP_MID, 88, cy - 11); lv_obj_set_ext_click_area(g_sw, 14);
        lv_obj_set_style_bg_color(g_sw, TC(SURFACE_RAISED), LV_PART_MAIN);
        lv_obj_set_style_bg_color(g_sw, b2_accent(), (lv_style_selector_t)LV_PART_INDICATOR | LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(g_sw, TC(TEXT_DISABLED), LV_PART_KNOB);
        lv_obj_set_style_bg_color(g_sw, TC(TEXT_PRIMARY), (lv_style_selector_t)LV_PART_KNOB | LV_STATE_CHECKED);
        kit_keep(g_sw);
        lv_obj_set_style_text_font(g_hsub, TF(UI_16), 0); lv_obj_align(g_hsub, LV_ALIGN_TOP_MID, 0, 102);
        lv_obj_set_pos(g_list, (360 - 280) / 2, 94); lv_obj_set_size(g_list, 280, 254);
        lv_obj_set_style_pad_bottom(g_list, 24, 0); lv_obj_set_style_pad_row(g_list, 6, 0);
        hdr_set(BT_OFF, "", "");
    }
    if(th_braun_family()){
        lv_obj_add_flag(g_hring, LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(g_hglyph, LV_OBJ_FLAG_HIDDEN);
        lv_obj_t *pd = br_disc(root, 180, 80, 44, BR_PANEL); (void)pd;
        g_hknob = br_knob(root, 180, 76, 30, LV_SYMBOL_BLUETOOTH, TF(UI_20));
        br_style_switch(g_sw);
        lv_obj_set_style_text_color(g_hname, TC(TEXT_PRIMARY), 0); lv_obj_set_style_text_font(g_hname, br_font(18, 1), 0);
        lv_obj_set_style_text_color(g_hsub, TC(TEXT_SECONDARY), 0); lv_obj_set_style_text_font(g_hsub, br_font(14, 0), 0);
        lv_obj_set_width(g_hname, 276); lv_obj_align(g_hname, LV_ALIGN_TOP_MID, 0, 120); lv_obj_align(g_hsub, LV_ALIGN_TOP_MID, 0, 146);   /* Braun: knob stays */
        lv_obj_set_pos(g_list, (360 - BROW_W) / 2, 168); lv_obj_set_size(g_list, BROW_W, 184);
        for(uint32_t k = 0; k < lv_obj_get_child_count(root); k++){      /* the rescan button: a flat disc */
            lv_obj_t *o = lv_obj_get_child(root, k);
            if(lv_obj_check_type(o, &lv_button_class) && lv_obj_get_width(o) == 34){
                lv_obj_set_style_bg_color(o, TC(SURFACE), 0);
                lv_obj_t *l = lv_obj_get_child(o, 0); if(l) lv_obj_set_style_text_color(l, TC(TEXT_PRIMARY), 0);
            }
        }
        hdr_set(BT_OFF, "Bluetooth", "");
    }
}

/* Non-destructive "wait for the adapter, then scan" poll used when the BT screen is opened while the
 * radio is on/coming up (from a QS toggle, or brought up externally). Unlike radio_on_poll_cb it NEVER
 * powers BT off on a timeout - it just stops trying, so a flaky bt_on() probe can't kill a live radio. */
static void scanwait_poll_cb(lv_timer_t *t){
    (void)t;
    if(screen_current() != SCR_BT){                 /* user left the BT screen -> stop waiting */
        lv_timer_del(g_scanwait_timer); g_scanwait_timer = NULL; return;
    }
    if(bt_on()){
        lv_timer_del(g_scanwait_timer); g_scanwait_timer = NULL;
        bt_ensure_services(); bt_autoroute_start(); search_or_list();
        return;
    }
    if(lv_tick_elaps(g_scanwait_start) > 28000){    /* gave up waiting - do NOT tear the radio down */
        lv_timer_del(g_scanwait_timer); g_scanwait_timer = NULL;
        list_msg(bt_radio_on() ? "Bluetooth is starting up" : "Bluetooth is off");
        if(bt_radio_on()) hdr_set(BT_ON, "Bluetooth is on", "Starting up..."); else hdr_set(BT_OFF, "Bluetooth is off", "");
    }
}

void bt_open(void){
    if(g_sw) lv_obj_set_style_bg_color(g_sw, g_b2 ? b2_accent() : ui_current_accent(), (lv_style_selector_t)LV_PART_INDICATOR | LV_STATE_CHECKED);   /* follow an accent change */
    /* "should BT be on" = the intent OR the live radio. Intent is the reliable signal right after a QS
     * toggle, because bt_enable() rfkill-blocks-then-unblocks, so bt_radio_on() reads 0 for a moment. */
    int want = cfg_get_int("bt_on", 0) || bt_radio_on();
    if(g_sw){
        if(want) lv_obj_add_state(g_sw, LV_STATE_CHECKED);
        else     lv_obj_clear_state(g_sw, LV_STATE_CHECKED);
    }
    screen_show(SCR_BT);
    /* bt_open is authoritative for this screen entry: cancel any in-flight bring-up observer (sw_cb's radio
     * poll or a prior scanwait) BEFORE deciding a path, so none of the branches below can race a pending
     * timer into a second start_scan() (e.g. reopen after the radio powered up but before g_radio_timer
     * fired would otherwise scan here AND on the next poll tick). */
    if(g_radio_timer){ lv_timer_del(g_radio_timer); g_radio_timer = NULL; }
    if(g_scanwait_timer){ lv_timer_del(g_scanwait_timer); g_scanwait_timer = NULL; }
    if(bt_on()){                                   /* fully up -> scan immediately */
        hdr_set(BT_ON, "Bluetooth is on", "");
        bt_ensure_services(); bt_autoroute_start(); search_or_list();
    } else if(want){                                /* on/coming up (e.g. just toggled from Quick Settings)
                                                     * -> poll until Powered, then scan. NON-destructive:
                                                     * unlike radio_on_poll_cb, this never powers BT off on a
                                                     * timeout, so observing an externally-enabled radio (or a
                                                     * flaky bt_on() probe) can't kill a working controller. */
        list_msg_scanning();
        hdr_set(BT_TURNING_ON, "Turning on...", "");
        g_scanwait_start = lv_tick_get();
        if(g_scanwait_timer) lv_timer_del(g_scanwait_timer);
        g_scanwait_timer = lv_timer_create(scanwait_poll_cb, 1000, NULL);
    } else {
        list_msg("Switch it on to find devices");
        hdr_set(BT_OFF, "Bluetooth is off", "");
    }
}

/* Host preview fixture: not called by device code. */
void bt_preview_null(void){
    if(getenv("BT_PREVIEW_OFF")){ hdr_set(BT_OFF,"Bluetooth is off",""); lv_obj_clear_state(g_sw,LV_STATE_CHECKED); list_msg("Switch it on to find devices"); return; }
    if(getenv("BT_PREVIEW_SCAN")){ hdr_set(BT_ON,"Bluetooth is on",""); lv_obj_add_state(g_sw,LV_STATE_CHECKED); list_msg_scanning(); return; }
    lv_obj_clean(g_list);hdr_set(BT_ON,getenv("BT_PREVIEW_HDR")?getenv("BT_PREVIEW_HDR"):"Connected to FiiO EH11 - LDAC","");lv_obj_add_state(g_sw,LV_STATE_CHECKED);add_dev_row("00:11:22:33:44:55","FiiO EH11",1,"LDAC");add_dev_row("00:11:22:33:44:66","Living room",0,"");add_dev_row("00:11:22:33:44:77","Studio headphones",0,"");curvelist_update(&g_bcl);}
