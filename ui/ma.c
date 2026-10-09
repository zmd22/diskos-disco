/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Music Assistant (Sendspin) - the Disc as a Music Assistant player.
 * The sendspin-disc helper talks to Music Assistant and hands the audio to the Disc's own AirPlay receiver (mq_player
 * owns the audio hardware). This file is the UI side: the on/off switch, the screen that shows the connection while
 * nothing plays (SCR_MA), and Settings > Network > Music Assistant. While music plays, Music shows it like any song.
 * Status from the helper: /tmp/ma/status, "key=value" lines (state=off|connecting|ready|playing|error, server=, msg=). */
#include "screens.h"
#include "theme.h"
#include "config.h"
#include "ma.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <strings.h>
#include "theme_kit.h"
#include "ipc.h"
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>

enum { MA_OFF, MA_CONNECTING, MA_READY, MA_PLAYING, MA_ERROR };
static int g_state = MA_OFF;
static char g_server_seen[64];                             /* the server the helper is connected to */
static char g_msg[96];
static int g_test = -1;                                    /* host renders force a state */

/* ---------------------------------------------------------------- settings values (Settings > Network > Music Assistant) */
char g_ma_server_lbl[48] = "Auto";
char g_ma_name_lbl[48] = "Snowsky Disc";

static void labels_load(void){
    const char *b = cfg_get_str("ma_server", "");
    snprintf(g_ma_server_lbl, sizeof g_ma_server_lbl, "%s", b && b[0] ? b : "Auto");
    b = cfg_get_str("ma_name", "");
    snprintf(g_ma_name_lbl, sizeof g_ma_name_lbl, "%s", b && b[0] ? b : "Snowsky Disc");
}
int ma_is_on(void){ return cfg_get_int("ma_on", 0); }

/* ---------------------------------------------------------------- the helper: this binary, run as "ma-sendspin --sendspin ..." */
static pid_t g_pid;                                        /* the running helper, 0 none */
static uint32_t g_restart_at;                              /* lv_tick to (re)start it at, 0 none */
static int g_leaving;                                      /* ma_set_on(0) is switching the mode itself */
static char g_mt[160], g_martist[160], g_malbum[160], g_track[128];   /* what Music Assistant plays */
static uint32_t g_delay_restart;                           /* Sync Delay slider: restart once it settles */
static long g_dur, g_prog; static long long g_prog_at; static int g_speed;

static void client_id(char *out, size_t n){                /* stable per Disc: from the Wi-Fi MAC */
    char mac[32] = ""; FILE *f = fopen("/sys/class/net/wlan0/address", "r");
    if(f){ if(!fgets(mac, sizeof mac, f)) mac[0] = 0; fclose(f); }
    char hex[16] = ""; int k = 0;
    for(const char *p = mac; *p && k < 12; p++) if(isxdigit((unsigned char)*p)) hex[k++] = (char)tolower((unsigned char)*p);
    hex[k] = 0;
    snprintf(out, n, "snowsky-disc-%s", k ? hex : "0");
}
static void helper_kill_stale(void){                       /* a helper left over from a previous mq_ui */
    FILE *f = fopen("/tmp/ma/status", "r"); if(!f) return;
    char line[96]; int pid = 0;
    while(fgets(line, sizeof line, f)) if(!strncmp(line, "pid=", 4)) pid = atoi(line + 4);
    fclose(f);
    if(pid <= 1) return;
    char p[64], cmd[64] = ""; snprintf(p, sizeof p, "/proc/%d/cmdline", pid);
    FILE *c = fopen(p, "r"); if(!c) return;
    size_t n = fread(cmd, 1, sizeof cmd - 1, c); fclose(c); cmd[n] = 0;
    if(!strcmp(cmd, "ma-sendspin")){ kill(-pid, SIGTERM); kill(pid, SIGTERM); }
    unlink("/tmp/ma/status");
}
static void helper_start(void){
    if(g_pid > 0) return;
    labels_load();
    char srv[64], name[48], id[48], delay[16];
    snprintf(srv, sizeof srv, "%s", strcmp(g_ma_server_lbl, "Auto") ? g_ma_server_lbl : "auto");
    snprintf(name, sizeof name, "%s", g_ma_name_lbl);
    client_id(id, sizeof id);
    snprintf(delay, sizeof delay, "%d", cfg_get_int("ma_delay_ms", 0));
    char *argv[] = { "ma-sendspin", "--sendspin", srv, name, id, delay, NULL };
    pid_t pid = 0;
    /* the running binary itself: the OTA launcher runs mq_ui from a private in-RAM copy, so its path ("/memfd:...
     * (deleted)") can't be opened by name - /proc/self/exe can (in the spawned child it is still this binary) */
    const char *exe = "/proc/self/exe";
    /* posix_spawn the binary by path with our argv[0]: command_spawn uses argv[0] as the program, so go direct */
    posix_spawn_file_actions_t fa; posix_spawnattr_t at;
    posix_spawn_file_actions_init(&fa); posix_spawnattr_init(&at);
    posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&fa, 1, "/dev/null", O_WRONLY, 0);
    mkdir("/tmp/ma", 0755);
    posix_spawn_file_actions_addopen(&fa, 2, "/tmp/ma/log", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    for(int fd = 3; fd < 256; fd++) posix_spawn_file_actions_addclose(&fa, fd);
    sigset_t mask; sigemptyset(&mask);
    posix_spawnattr_setsigmask(&at, &mask); posix_spawnattr_setpgroup(&at, 0);
    posix_spawnattr_setflags(&at, POSIX_SPAWN_SETPGROUP | POSIX_SPAWN_SETSIGMASK);
    extern char **environ;
    int rc = posix_spawn(&pid, exe, &fa, &at, argv, environ);
    if(rc != 0) rc = posix_spawn(&pid, "/usr/data/mq_ui", &fa, &at, argv, environ);   /* the installed copy */
    posix_spawn_file_actions_destroy(&fa); posix_spawnattr_destroy(&at);
    if(rc == 0){ g_pid = pid; fprintf(stderr, "ma: sendspin started (pid %d, server %s)\n", (int)pid, srv); }
    else { fprintf(stderr, "ma: sendspin spawn failed: %s\n", strerror(rc)); g_restart_at = lv_tick_get() + 5000; }
}
/* Never waits on the UI thread: the old helper is asked to quit and reaped by helper_watch (SIGKILL after 2 s).
 * A new one only starts once the old one is gone (they'd share the AirPlay session and /tmp/ma/cmd). */
static pid_t g_dying; static uint32_t g_dying_at;
static void helper_stop(void){
    g_restart_at = 0;
    if(g_pid > 0){
        kill(-g_pid, SIGTERM); kill(g_pid, SIGTERM);
        g_dying = g_pid; g_dying_at = lv_tick_get(); g_pid = 0;
    }
    unlink("/tmp/ma/status");
    ipc_clear_external();
    g_mt[0] = 0;
}
static void helper_watch(void){                            /* reap / restart; called from the timer */
    if(g_dying > 0){
        pid_t r = waitpid(g_dying, NULL, WNOHANG);
        if(r == g_dying || (r < 0 && errno == ECHILD)) g_dying = 0;
        else if(lv_tick_elaps(g_dying_at) > 2000){ kill(-g_dying, SIGKILL); kill(g_dying, SIGKILL); }
        if(g_dying > 0) return;
    }
    if(g_pid > 0){
        int st = 0; pid_t r = waitpid(g_pid, &st, WNOHANG);
        /* gone: reaped here, or already reaped by one of the UI's general child reapers (ECHILD) */
        if(r == g_pid || (r < 0 && errno == ECHILD && kill(g_pid, 0) < 0)){
            fprintf(stderr, "ma: sendspin exited (%d), restarting\n", st); g_pid = 0; g_restart_at = lv_tick_get() + 3000;
        }
    }
    if(!ma_is_on()) return;
    if(g_delay_restart && (int32_t)(lv_tick_get() - g_delay_restart) >= 0){ g_delay_restart = 0; ma_restart(); }
    if(g_pid == 0 && g_restart_at && (int32_t)(lv_tick_get() - g_restart_at) >= 0){ g_restart_at = 0; helper_start(); }
    static int tries;                                        /* AirPlay must be on for the audio (boot, after a player restart): */
    if(ui_get_source_mode() == 5) tries = 0;                 /* three quiet tries 10 s apart, not a toast every 10 s forever */
    else if(!g_leaving && tries < 3){
        static uint32_t last; if(!last || lv_tick_elaps(last) > 10000){ last = lv_tick_get(); tries++; ui_set_source_mode(5); }
    }
}
/* restart with new settings (server / name / delay changed) */
void ma_restart(void){ if(ma_is_on()){ helper_stop(); g_restart_at = lv_tick_get() + 300; } }
void ma_delay_changed(void){ if(ma_is_on()) g_delay_restart = lv_tick_get() + 1000; }   /* called on release, after the value is saved */

static void status_read(void){
    if(g_test >= 0){ g_state = g_test; return; }
    if(!ma_is_on()){ g_state = MA_OFF; return; }
    FILE *f = fopen("/tmp/ma/status", "r");
    if(!f){ g_state = MA_CONNECTING; return; }
    char line[300]; int have = 0;
    g_mt[0] = g_martist[0] = g_malbum[0] = g_track[0] = 0; g_msg[0] = 0;
    while(fgets(line, sizeof line, f)){
        line[strcspn(line, "\r\n")] = 0;
        char *v = strchr(line, '='); if(!v) continue; *v++ = 0;
        if(!strcmp(line, "state")) g_state = !strcmp(v, "ready") ? MA_READY : !strcmp(v, "playing") ? MA_PLAYING : !strcmp(v, "error") ? MA_ERROR : MA_CONNECTING;
        else if(!strcmp(line, "server")) snprintf(g_server_seen, sizeof g_server_seen, "%s", v);
        else if(!strcmp(line, "msg")) snprintf(g_msg, sizeof g_msg, "%s", v);
        else if(!strcmp(line, "title")){ snprintf(g_mt, sizeof g_mt, "%s", v); have = 1; }
        else if(!strcmp(line, "artist")) snprintf(g_martist, sizeof g_martist, "%s", v);
        else if(!strcmp(line, "album")) snprintf(g_malbum, sizeof g_malbum, "%s", v);
        else if(!strcmp(line, "track")) snprintf(g_track, sizeof g_track, "%s", v);
        else if(!strcmp(line, "duration_ms")) g_dur = atol(v);
        else if(!strcmp(line, "progress_ms")) g_prog = atol(v);
        else if(!strcmp(line, "progress_at_ms")) g_prog_at = atoll(v);
        else if(!strcmp(line, "speed")) g_speed = atoi(v);
    }
    fclose(f);
    /* the track goes to Music while the helper is connected and knows one */
    if(have && g_mt[0] && (g_state == MA_READY || g_state == MA_PLAYING))
        ipc_set_external(g_mt, g_martist, g_malbum, g_track, g_dur, g_prog, g_prog_at, g_state == MA_PLAYING ? g_speed : 0);
    else ipc_clear_external();
}
void ma_set_on(int on){
    cfg_set_int("ma_on", on ? 1 : 0);
    if(on && !wifi_radio_live()) ui_toast("Wi-Fi is off - turn it on for Sendspin");   /* it waits, and connects once Wi-Fi is up */
    if(on){ helper_kill_stale(); if(ui_get_source_mode() != 5) ui_set_source_mode(5); if(g_dying > 0) g_restart_at = lv_tick_get() + 300; else helper_start(); }   /* AirPlay carries the audio into mq_player */
    else { helper_stop(); if(ui_get_source_mode() == 5){ g_leaving = 1; ui_set_source_mode(0); g_leaving = 0; } }
    status_read();
    if(screen_current() == SCR_MA) ma_refresh();
}
/* another working mode was picked: Sendspin can't play without AirPlay, so it turns off */
void ma_mode_leaving(void){
    if(g_leaving || !ma_is_on()) return;
    cfg_set_int("ma_on", 0); helper_stop();
}
/* Music's buttons drive Music Assistant while it's connected */
int ma_controls(void){ return ma_is_on() && g_pid > 0 && (g_state == MA_READY || g_state == MA_PLAYING); }
void ma_send(const char *cmd){
    int fd = open("/tmp/ma/cmd", O_WRONLY | O_NONBLOCK); if(fd < 0) return;
    char b[48]; int n = snprintf(b, sizeof b, "%s\n", cmd);
    if(write(fd, b, n) != n) fprintf(stderr, "ma: command '%s' not delivered\n", cmd);
    close(fd);
}
int ma_seek(long ms){ char b[40]; snprintf(b, sizeof b, "seek %ld", ms < 0 ? 0 : ms); ma_send(b); return 0; }
/* boot: if Sendspin was on, bring it back once the UI is up */
static void boot_cb(lv_timer_t *t){ (void)t; if(ma_is_on()){ helper_kill_stale(); helper_start(); } }
void ma_boot(void){ lv_timer_t *t = lv_timer_create(boot_cb, 8000, NULL); lv_timer_set_repeat_count(t, 1); }

/* ---------------------------------------------------------------- SCR_MA */
static lv_obj_t *g_ring, *g_glyph, *g_title, *g_status, *g_d1, *g_d2, *g_pill, *g_pill_lbl;
static lv_timer_t *g_timer;
static int g_spin = -1;

static void spin_exec(void *var, int32_t v){ lv_arc_set_rotation((lv_obj_t *)var, v % 360); }
static void set_spin(int spin, int full){
    int key = spin ? 2 : full;
    if(key == g_spin || !g_ring) return;
    g_spin = key;
    lv_anim_delete(g_ring, spin_exec);
    lv_obj_set_style_arc_color(g_ring, ui_current_accent(), LV_PART_INDICATOR);
    if(spin){
        lv_arc_set_value(g_ring, 260);
        lv_anim_t a; lv_anim_init(&a); lv_anim_set_var(&a, g_ring); lv_anim_set_exec_cb(&a, spin_exec);
        lv_anim_set_values(&a, 270, 270 + 360); lv_anim_set_duration(&a, 1400);
        lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE); lv_anim_start(&a);
    } else { lv_arc_set_rotation(g_ring, 270); lv_arc_set_value(g_ring, full ? 1000 : 0); }
}
/* the stylised "MA": two rounded strokes, drawn (no font has it at this size) */
static const lv_point_precise_t MK_M[] = { {0, 40}, {0, 2}, {17, 26}, {34, 2}, {34, 40} };
static const lv_point_precise_t MK_A[] = { {42, 40}, {57, 2}, {72, 40} };
static const lv_point_precise_t MK_AX[] = { {48, 27}, {66, 27} };
static lv_obj_t *mk_line(lv_obj_t *box, const lv_point_precise_t *p, int n){
    lv_obj_t *l = lv_line_create(box);
    lv_line_set_points(l, p, n);
    lv_obj_set_style_line_width(l, 7, 0); lv_obj_set_style_line_rounded(l, true, 0);
    lv_obj_set_style_line_color(l, TC(TEXT_PRIMARY), 0);
    lv_obj_set_pos(l, 4, 4);
    return l;
}
static lv_obj_t *ma_mark(lv_obj_t *root, int x, int y){
    lv_obj_t *box = lv_obj_create(root);
    lv_obj_remove_style_all(box);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(box, 84, 50); lv_obj_align(box, LV_ALIGN_CENTER, x, y);
    mk_line(box, MK_M, 5); mk_line(box, MK_A, 3); mk_line(box, MK_AX, 2);
    return box;
}
static void txt(lv_obj_t *l, const char *t){ if(l && strcmp(lv_label_get_text(l), t)) lv_label_set_text(l, t); }

void ma_refresh(void){
    if(!g_title) return;
    labels_load(); status_read();
    char st[64] = "", d1[120] = "", d2[120] = "";
    const char *srv = g_server_seen[0] ? g_server_seen : (strcmp(g_ma_server_lbl, "Auto") ? g_ma_server_lbl : "");
    switch(g_state){
        case MA_OFF:
            snprintf(st, sizeof st, "Off");
            snprintf(d1, sizeof d1, "A Sendspin player for");
            snprintf(d2, sizeof d2, "Music Assistant");
            break;
        case MA_CONNECTING:
            if(!wifi_radio_live()){                          /* say why it can't connect, not just that it's trying */
                snprintf(st, sizeof st, "Wi-Fi is off");
                snprintf(d1, sizeof d1, "Turn on Wi-Fi to reach");
                snprintf(d2, sizeof d2, "Music Assistant");
                break;
            }
            snprintf(st, sizeof st, "Looking for Music Assistant");
            snprintf(d1, sizeof d1, "%s", srv[0] ? srv : "On your network");
            break;
        case MA_READY: case MA_PLAYING:
            if(g_mt[0]){                                     /* a track: what it is (Music shows it in full) */
                snprintf(st, sizeof st, g_state == MA_PLAYING ? "Playing" : "Paused");
                snprintf(d1, sizeof d1, "%s", g_mt); snprintf(d2, sizeof d2, "%s", g_martist);
            } else {
                snprintf(st, sizeof st, "Ready");
                snprintf(d1, sizeof d1, "Pick %s there", g_ma_name_lbl);
                snprintf(d2, sizeof d2, "%s", srv[0] ? srv : "Music Assistant");
            }
            break;
        case MA_ERROR:
            if(!wifi_radio_live()){ snprintf(st, sizeof st, "Wi-Fi is off"); snprintf(d1, sizeof d1, "Turn on Wi-Fi to reach");
                                    snprintf(d2, sizeof d2, "Music Assistant"); break; }
            snprintf(st, sizeof st, "Can't reach Music Assistant");
            snprintf(d1, sizeof d1, "%s", g_msg[0] ? g_msg : (srv[0] ? srv : "Check the server in Settings"));
            snprintf(d2, sizeof d2, "Trying again");
            break;
    }
    txt(g_status, st); txt(g_d1, d1); txt(g_d2, d2);
    set_spin(g_state == MA_CONNECTING, g_state >= MA_READY && g_state != MA_ERROR);
    int on = g_state != MA_OFF;
    txt(g_pill_lbl, on ? "Turn off" : "Turn on");
    lv_obj_set_style_bg_color(g_pill, theme_color_from_rgb(on ? TH_SURF1 : TH_ACCENT), 0);
}
void ma_test_state(int s){ g_test = s; ma_refresh(); }      /* host renders */
static void tick(lv_timer_t *t){ (void)t; helper_watch(); if(screen_current() == SCR_MA) ma_refresh(); else status_read(); }
/* playing through Music Assistant: keeps the Disc from its idle power-off (main.c -> power_tick) */
int ma_playing(void){ return ma_is_on() && g_state == MA_PLAYING; }
static void pill_cb(lv_event_t *e){ if(lv_event_get_code(e) == LV_EVENT_CLICKED) ma_set_on(!ma_is_on()); }
static lv_obj_t *label(lv_obj_t *root, const lv_font_t *font, uint32_t col, int y, int w){
    lv_obj_t *l = lv_label_create(root);
    lv_label_set_text(l, "");
    lv_obj_set_style_text_font(l, font, 0); lv_obj_set_style_text_color(l, theme_color_from_rgb(col), 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_set_width(l, w); lv_obj_align(l, LV_ALIGN_CENTER, 0, y);
    return l;
}
void ma_create(lv_obj_t *root){
    lv_obj_set_style_bg_color(root, TC(CANVAS), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    g_ring = lv_arc_create(root);
    lv_obj_remove_style(g_ring, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(g_ring, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(g_ring, 132, 132); lv_obj_align(g_ring, LV_ALIGN_CENTER, 0, -76);
    lv_arc_set_bg_angles(g_ring, 0, 360); lv_arc_set_range(g_ring, 0, 1000); lv_arc_set_rotation(g_ring, 270);
    lv_obj_set_style_arc_width(g_ring, 6, LV_PART_MAIN); lv_obj_set_style_arc_color(g_ring, TC(CONTROL_TRACK), LV_PART_MAIN);
    lv_obj_set_style_arc_width(g_ring, 6, LV_PART_INDICATOR); lv_obj_set_style_arc_rounded(g_ring, true, LV_PART_INDICATOR);
    g_glyph = ma_mark(root, 0, -76);
    g_title  = label(root, TH_F_TITLE, TH_TXT1, 6, 260);
    lv_label_set_text(g_title, "Sendspin");
    g_status = label(root, TF(UI_16), TH_ACCENT, 36, 260);
    g_d1     = label(root, TH_F_DETAIL, TH_TXT2, 64, 250);
    g_d2     = label(root, TH_F_DETAIL, TH_TXT3, 86, 250);
    g_pill = lv_button_create(root);
    lv_obj_remove_style_all(g_pill);
    lv_obj_set_size(g_pill, 112, 40); lv_obj_align(g_pill, LV_ALIGN_CENTER, 0, 128);
    lv_obj_set_ext_click_area(g_pill, 4);
    lv_obj_set_style_radius(g_pill, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(g_pill, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(g_pill, pill_cb, LV_EVENT_CLICKED, NULL);
    g_pill_lbl = lv_label_create(g_pill);
    lv_obj_set_style_text_font(g_pill_lbl, TH_F_DETAIL, 0);
    lv_obj_set_style_text_color(g_pill_lbl, TC(TEXT_PRIMARY), 0);
    lv_label_set_text(g_pill_lbl, "Turn on");
    lv_obj_center(g_pill_lbl);
    g_timer = lv_timer_create(tick, 2000, NULL);
    ma_refresh();
}

/* ---------------------------------------------------------------- Settings > Network > Music Assistant: text rows */
static void server_done(const char *t){
    if(!t) return; /* cancel is distinct from saving an empty field (Auto) */
    cfg_set_str("ma_server", (t && t[0] && strcasecmp(t, "auto")) ? t : "");
    labels_load(); setlist_refresh(); ma_restart();
}
static void name_done(const char *t){
    if(t && t[0]){ cfg_set_str("ma_name", t); labels_load(); setlist_refresh(); ma_restart(); }
}
void ma_edit_server(void){ kbinput_open("Server (empty = Auto)", strcmp(g_ma_server_lbl, "Auto") ? g_ma_server_lbl : "", server_done); }
void ma_edit_name(void){ kbinput_open("Player name", g_ma_name_lbl, name_done); }
void ma_settings_load(void){ labels_load(); }
