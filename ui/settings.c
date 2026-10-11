/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "screens.h"
#include "ma.h"
#include "fork_build.h"
#include "braun.h"
#include "theme.h"
#include "curvelist.h"
#include "orbit.h"
#include "theme_kit.h"
#include "config.h"
#include "anim.h"
#include "musicdb.h"
#include "fwcaps.h"
#include "version.h"
#include "md5.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/reboot.h>
#include <pthread.h>
#include <stdatomic.h>
#include "command_spawn.h"
#include "sdio.h"
#include "art.h"
#include "scanner.h"
#include "power.h"
#include "system.h"
#include "ota.h"
#include "controls.h"
#include "i18n.h"

/* Killed children are always collected, so none is left a zombie: kill_and_reap() waits up to 300 ms (a SIGKILLed
 * child normally exits at once), and anything still unreaped (e.g. stuck in kernel I/O) is parked and swept by a
 * small self-deleting timer until it has been collected - also after the restart attempt that killed it has ended. */
#define RST_REAP_MAX 8
static pid_t g_rst_reap[RST_REAP_MAX];
static lv_timer_t *g_rst_reaper;
static int rst_reap_sweep(void){                     /* returns how many are still parked */
    int left = 0;
    for(int i = 0; i < RST_REAP_MAX; i++)
        if(g_rst_reap[i] > 0){
            pid_t w = waitpid(g_rst_reap[i], NULL, WNOHANG);
            if(w == g_rst_reap[i] || (w < 0 && errno == ECHILD)) g_rst_reap[i] = 0; else left++;
        }
    return left;
}
static void rst_reaper_cb(lv_timer_t *t){ if(rst_reap_sweep() == 0){ lv_timer_delete(t); g_rst_reaper = NULL; } }
/* Free parking slots after a sweep. Every caller that may later kill children checks it has enough BEFORE forking,
 * so kill_and_reap() can always park - a killed child is never forgotten unreaped. */
static int rst_reap_free(void){ rst_reap_sweep(); int n = 0; for(int i = 0; i < RST_REAP_MAX; i++) if(g_rst_reap[i] <= 0) n++; return n; }
static void kill_and_reap(pid_t p){
    if(p <= 0) return;
    kill(-p, SIGKILL); kill(p, SIGKILL);
    for(int k = 0; k < 30; k++){
        pid_t w = waitpid(p, NULL, WNOHANG);
        if(w == p || (w < 0 && errno == ECHILD)) return;
        usleep(10000);
    }
    for(int i = 0; i < RST_REAP_MAX; i++) if(g_rst_reap[i] <= 0){ g_rst_reap[i] = p; break; }
    if(!g_rst_reaper) g_rst_reaper = lv_timer_create(rst_reaper_cb, 1000, NULL);
}

/* Run a shell command as a bounded child in its OWN process group, and NEVER blocking-reap it: a child
 * stuck in kernel I/O must not be able to hold the UI thread - or a restart - even for a moment. */
static void bounded_detached(const char *cmd, int ms){
    pid_t p = 0;                                   /* posix_spawn, not fork(): a fork of the big UI can fail for memory */
    char *argv[] = { "/bin/sh", "-c", (char *)cmd, NULL };
    if(command_spawn(&p, argv) != 0 || p <= 0) return;
    for(int i = 0; i < ms/100; i++){
        if(waitpid(p, NULL, WNOHANG) == p) return;
        usleep(100000);
    }
    kill_and_reap(p);
}

/* ---- setting model ------------------------------------------------------ */
typedef enum { ST_TOGGLE, ST_SLIDER, ST_CYCLER, ST_READONLY, ST_ACTION, ST_CHOICE } st_type_t;   /* CHOICE: pick from a list */

typedef struct setting_s {
    const char *group;
    const char *label;
    st_type_t   type;
    const char *cfg_key;
    int         min, max, step;            /* slider */
    const char *const *opts; int nopts;    /* cycler */
    const char *ro_val;                    /* readonly */
    void (*apply)(int v);                  /* optional apply hook / decode seam */
    int  def;
    const char *desc;                      /* detail-page description */
    const char *const *opt_descs;          /* per-option descriptions (cyclers) */
} setting_t;

/* apply hooks (decode seams live in main.c) */
void screen_set_anim(int on);
void ui_set_workmode(int mode);
int ui_apply_eq(int preset);
void ui_clock_refresh(void);
void ui_set_accent_config(int mode, int rgb);   /* accent: 0=dynamic / 1=static(rgb) */
void ui_set_prewarm_mode(int m);                /* art cache: 0=covers only 1=+sweep idle 2=+sweep idle&charging */
void ui_set_dre(int on);                        /* audio cluster (main.c) */
void ui_set_gain(int high);
int  ui_set_artist_class(int album_artist);      /* 0648; 0 sent, -1 not (main.c) */
#include "netart.h"
int  ui_set_folder_jump(int on);                 /* 0687; 0 sent, -1 not (main.c) */
void ui_set_track_display(int on);               /* 064d where the player has it (main.c) */
void ui_set_output(int spdif);
void ui_set_dac_filter(int idx);
void ui_set_gapless(int on);
void ui_set_memory(int mode);
void ui_set_maxvol(int v);
void ui_set_balance(int v);

static void apply_open_colorpick(int v){ (void)v; colorpick_open(); }   /* seed sliders from cfg + open */
static void apply_debug_mode(int v){ (void)v; debug_open(); }           /* Settings -> System -> Debug Mode */
static void apply_artcache(int v){ ui_set_prewarm_mode(v); }   /* every mode preloads album covers; the mode picks the per-track sweep */
void ui_sd_quiesce_begin(void); int ui_sd_quiesce_drained(void); void ui_sd_quiesce_end(void);   /* main.c: Restart card drain */

static void apply_swipe(int v){ ui_apply_swipe_thresh(v); }   /* live; persisted on slider release */
static void apply_anim(int v){ screen_set_anim(v); }
static void apply_weather(int v){ weather_set_enabled(v); }
static void apply_mode(int v){ if(ui_book_active()) return; ui_set_workmode(v); }   /* an active book stays in Single; cfg still updates for later music */
static void apply_eq(int v){ ui_eq_select(v); }   /* central: also records eq_last so the drawer A/B stays in sync */
/* audio cluster: v<0 = "System default" (unmanaged) -> ui_set_* no-ops, leaving the player's
 * own state untouched. A real value sends the live command; persistence is via cfg + the
 * player-ready re-apply in main.c (ui_reapply_audio). */
static void apply_dre(int v){ ui_set_dre(v); }
static void apply_replay_gain(int v){ ui_set_replay_gain(v); }
static void apply_gain(int v){ ui_set_gain(v); }
static void apply_dac_filter(int v){ ui_set_dac_filter(v); }
static void apply_gapless(int v){ ui_set_gapless(v); }
#ifdef DISKOS_TEST_OUTPUTS
static void apply_spdif(int v){ ui_set_output(v); }   /* SPDIF: stock toggle form: pause -> silent -> 0666 -> 0657 8 -> resume (modes.c); the toggle then mirrors the real route */
#endif
static void apply_memory(int v){ ui_set_memory(v); }
static void apply_maxvol(int v){ ui_set_maxvol(v); }
static void apply_balance(int v){ ui_set_balance(v); }
static void apply_time(int v){ (void)v; ui_clock_refresh(); }
static void apply_sleep(int idx){
    static const int M[] = {0,15,30,45,60,90};
    ui_set_sleep_timer((idx>=0 && idx<6) ? M[idx] : 0);
}
static void apply_np_style(int v){ ui_set_np_style(v); }
/* Theme settings apply by re-launching the UI (palette + Outdoor backlight are read at startup). set_val has already
 * saved the new value; if the reload is refused (a library scan is running) the old value goes back, and the detail
 * view is rebuilt after this event finishes (it cannot be deleted from inside its own switch/button callback). */
static int g_set_prev;                                  /* the value set_val replaced */
static void detail_refresh_async(void *u){ (void)u; setting_detail_refresh(); }
static void theme_reload_or_revert(const char *key){
    if(ui_theme_reload("settings") == 0) return;        /* not reached on success: the exec replaced us */
    cfg_set_int(key, g_set_prev);
    lv_async_call(detail_refresh_async, NULL);
}
static void apply_appearance(int v){ (void)v; theme_reload_or_revert("theme_variant"); }
static void apply_theme_auto(int v){ (void)v; theme_reload_or_revert("theme_auto"); }
static void apply_outdoor(int v){ (void)v; theme_reload_or_revert("outdoor"); }
static void apply_theme_preset(int v){ (void)v; theme_reload_or_revert("theme_preset"); }
/* Font Size and Language change every screen's text: like a theme change, the UI re-launches and re-reads them */
static void apply_np_font(int v){(void)v;theme_reload_or_revert("np_font");}
static void apply_font_size(int v){ (void)v; theme_reload_or_revert("font_size"); }
static void apply_language(int v){ (void)v; theme_reload_or_revert("language"); }

static void rescan_go(void){ ui_rescan_library(); }
static void apply_autotag(int v){ (void)v; }
static void apply_upnext(int v){ (void)v; home_shortcuts_refresh(); }

/* ---- Date & Time (parity with stock: automatic time switch, set by hand, time zone) ---------------------------- */
static void apply_auto_time(int v){
    if(mdb_sysconfig_set_auto_time(v) != 0){           /* the player may hold its database: put the switch back */
        cfg_set_int("auto_time", g_set_prev);
        ui_toast("Couldn't change it - try again");
        lv_async_call(detail_refresh_async, NULL);
        return;
    }
    ui_toast("Applies after the next restart");        /* the player reads it only when it starts (see musicdb.c) */
}
/* Artists grouping (stock "Artist / Album Artist", ARTIST_CLASS_TYPE via 0648): the player owns the value (mirrored
 * at startup); a send that fails puts the choice back so the menu never shows a grouping the player isn't using. */
static void apply_artist_class(int v){
    if(ui_set_artist_class(v) != 0){
        cfg_set_int("artist_class", g_set_prev);
        ui_toast("Couldn't change it - try again");
        lv_async_call(detail_refresh_async, NULL);
    }
}
/* Play Through Folders (stock FOLDER_JUMP via 0687): the player owns the value (mirrored at startup); a failed send
 * puts the switch back. */
static void apply_folder_jump(int v){
    if(ui_set_folder_jump(v) != 0){
        cfg_set_int("folder_jump", g_set_prev);
        ui_toast("Couldn't change it - try again");
        lv_async_call(detail_refresh_async, NULL);
    }
}
/* Charging optimization (stock 0808, SYSCONFIG.CHARGE_PROTECT): the player owns it (mirrored at startup, power.c); a
 * failed send puts the choice back. Idle Power-off has NO apply hook on purpose: diskOS runs that timer itself (power.c)
 * and keeps the player's own idle power-off (0664) off, because the player's path powers off without syncing the card. */
/* Screen Rotation and the volume keys' actions (stock 0646 / 0820-0822, controls.c): the player stores them; a refused
 * send puts the choice back so the menu never shows what the player isn't using. */
static void apply_screen_rot(int v){
    if(ui_set_screen_rot(v) != 0){
        cfg_set_int(CTL_CFG_ROT, g_set_prev);
        ui_toast("Couldn't change it - try again");
        lv_async_call(detail_refresh_async, NULL);
    } else if(ctl_rotation_pending())
        cfg_set_int(CTL_CFG_ROT, g_set_prev);   /* stored only when the user taps Keep (controls.c); the row keeps the old one till then */
}
static void key_act_apply(int key, const char *cfg_key, int v){
    if(ui_set_volume_key(key, v) != 0){
        cfg_set_int(cfg_key, g_set_prev);
        ui_toast("Couldn't change it - try again");
        lv_async_call(detail_refresh_async, NULL);
    }
}
static void apply_key_single(int v){ key_act_apply(CTL_KEY_SINGLE, "key_single", v); }
static void apply_key_double(int v){ key_act_apply(CTL_KEY_DOUBLE, "key_double", v); }
static void apply_key_long(int v){   key_act_apply(CTL_KEY_LONG, "key_long", v); }
static void apply_charge_protect(int v){
    if(power_apply_charge(v) != 0){
        cfg_set_int("charge_protect", g_set_prev);
        ui_toast("Couldn't change it - try again");
        lv_async_call(detail_refresh_async, NULL);
    }
}
/* Track Numbers: album song lists show each song's track number (stock "TRACK_DISPLAY", drawn by diskOS) */
static void apply_track_numbers(int v){ ui_set_track_display(v); library_refresh(); }
/* Online Album Art (stock "Online cover", done by diskOS - netart.c): on tries the current song now; off drops any
 * queued lookup and redraws the current cover without an online picture */
static void apply_online_art(int v){ if(!v) netart_cancel(); ui_art_redo(); }
static void apply_open_datetime(int v){ (void)v; screen_show(SCR_DATETIME); }
/* Time Zone: "Automatic" leaves the zone to the player (looked up online when Wi-Fi connects, with Automatic Time
 * on); any other entry copies that zone's rules to /usr/data/localtime (what /etc/localtime points at, for every
 * process), atomically, then re-launches the UI so its clock uses them. */
static const char *const TZ_LABEL[] = { "Automatic", "UTC", "London", "Paris / Berlin", "Athens / Helsinki",
    "Moscow", "Cairo", "Johannesburg", "Dubai", "Karachi", "India", "Bangkok / Jakarta", "China / Singapore",
    "Hong Kong", "Tokyo", "Seoul", "Perth", "Adelaide", "Darwin", "Brisbane", "Sydney / Melbourne", "Hobart",
    "Auckland", "Honolulu", "Anchorage", "Los Angeles", "Denver", "Phoenix", "Chicago", "Mexico City", "New York",
    "Toronto", "Sao Paulo", "Buenos Aires" };
static const char *const TZ_ZONE[] = { NULL, "UTC", "Europe/London", "Europe/Berlin", "Europe/Athens",
    "Europe/Moscow", "Africa/Cairo", "Africa/Johannesburg", "Asia/Dubai", "Asia/Karachi", "Asia/Kolkata",
    "Asia/Bangkok", "Asia/Shanghai", "Asia/Hong_Kong", "Asia/Tokyo", "Asia/Seoul", "Australia/Perth",
    "Australia/Adelaide", "Australia/Darwin", "Australia/Brisbane", "Australia/Sydney", "Australia/Hobart",
    "Pacific/Auckland", "Pacific/Honolulu", "America/Anchorage", "America/Los_Angeles", "America/Denver",
    "America/Phoenix", "America/Chicago", "America/Mexico_City", "America/New_York", "America/Toronto",
    "America/Sao_Paulo", "America/Argentina/Buenos_Aires" };
#define TZ_COUNT ((int)(sizeof TZ_LABEL / sizeof TZ_LABEL[0]))
_Static_assert(sizeof TZ_LABEL / sizeof TZ_LABEL[0] == sizeof TZ_ZONE / sizeof TZ_ZONE[0], "time zone tables");
/* write n bytes over /usr/data/localtime: temp + fsync + rename + directory fsync */
static int tz_write(const unsigned char *buf, size_t n){
    const char *tmp = "/usr/data/localtime.diskos-tmp", *dst = "/usr/data/localtime";
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0644);
    if(fd < 0) return -1;
    int ok = write(fd, buf, n) == (ssize_t)n && fsync(fd) == 0;
    if(close(fd) != 0) ok = 0;
    if(!ok || rename(tmp, dst) != 0){ unlink(tmp); return -1; }
    int dfd = open("/usr/data", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if(dfd >= 0){ fsync(dfd); close(dfd); }
    return 0;
}
static size_t tz_read(const char *path, unsigned char *buf, size_t cap){   /* 0 = unreadable, too big, or not TZif */
    FILE *in = fopen(path, "rb");
    if(!in) return 0;
    size_t n = fread(buf, 1, cap, in);
    int more = fgetc(in) != EOF;
    fclose(in);
    return (more || n < 44 || memcmp(buf, "TZif", 4)) ? 0 : n;
}
/* Install a zone. This firmware keeps the RTC in LOCAL time and every UI start reloads the clock from it (hwclock -s),
 * so the RTC is rewritten in the NEW zone's local time straight away (hwclock -w, bounded); if that fails the old
 * zone goes back - a relaunch would otherwise read the old local time as the new zone's and shift the clock by the
 * zones' difference (device-verified 2026-09-26: +10 h going Sydney -> UTC without it). */
static int tz_install(const char *zone){
    static unsigned char nbuf[65536], obuf[65536];
    char src[160]; snprintf(src, sizeof src, "/usr/share/zoneinfo/%s", zone);
    size_t n = tz_read(src, nbuf, sizeof nbuf);
    if(!n) return -1;
    size_t on = tz_read("/usr/data/localtime", obuf, sizeof obuf);
    if(tz_write(nbuf, n) != 0) return -1;
    char *a[] = { "hwclock", "-w", NULL };
    if(ui_run_bounded(a, 4000) != 0){
        if(on) tz_write(obuf, on);                     /* back to the zone the RTC still matches */
        return -1;
    }
    return 0;
}
static void apply_timezone(int v){
    if(v > 0 && v < TZ_COUNT && tz_install(TZ_ZONE[v]) != 0){
        cfg_set_int("tz_idx", g_set_prev);
        ui_toast("Couldn't set the time zone");
        lv_async_call(detail_refresh_async, NULL);
        return;
    }
    theme_reload_or_revert("tz_idx");                  /* the UI re-reads the zone rules at start */
}
static void apply_rescan(int v){ (void)v;
    if(scanner_active()){ scanner_abort(); return; }
    fileops_confirm("Rescan library?", "Looks for new and changed music", "Rescan", rescan_go);
}
static void do_shutdown(void){ ui_power_off(); }
static void apply_shutdown(int v){ (void)v;
    fileops_confirm("Shut down player?", "Switches the player off", "Shut down", do_shutdown);
}
static void apply_import_m3u(int v){ (void)v;
    int n = mdb_import_m3u_sd("/tmp/sdcard");   /* every folder on the card (bounded walk), like stock V2.57 */
    char b[48];
    if(n <= 0) snprintf(b, sizeof b, "No new playlists found");
    else       snprintf(b, sizeof b, "Imported %d playlist%s", n, n==1 ? "" : "s");
    ui_toast(b);
}
static void apply_wifi(int v){ (void)v; wifi_open(); }   /* opens SCR_WIFI */
static void apply_bt(int v){ (void)v; bt_open(); }       /* opens SCR_BT */
static void apply_volkeys_group(int v);                   /* Settings > System > Volume Keys */
static void apply_ma_group(int v);                        /* Settings > Network > Music Assistant */
static void apply_ma_on(int v){ ma_set_on(v); }
static void apply_ma_server(int v){ (void)v; ma_edit_server(); }
static void apply_ma_name(int v){ (void)v; ma_edit_name(); }

static void apply_workmode(int v){ (void)v; modes_open(); }  /* opens SCR_WORKMODE (source-mode picker) */
static void apply_eq_custom(int v){ (void)v; screen_show(SCR_EQ); }  /* opens Custom EQ */
static void apply_disco_menu(int v){ (void)v; disco_menu_open(); }     /* fork: Disco theme only */
/* Display > Disco Options: the "Disco" rows on the same list screen; back returns to Display (settings_back_consumed) */
static void apply_disco_options(int v);
static void apply_grp_themecolours(int v);
static void apply_grp_textlayout(int v);
static void apply_grp_screenstandby(int v);
static void apply_grp_onlineextras(int v);
static void apply_grp_musicscreen(int v);
static void apply_grp_discocolours(int v);
static void apply_grp_discolayout(int v);
static void apply_grp_power(int v);
static void apply_grp_datetime(int v);
static void apply_grp_library(int v);
static void apply_grp_controls(int v);
static void apply_grp_maintenance(int v);
static void apply_grp_about(int v);
static void apply_disco_clock(int v){ dhome_show_clock(v); }
void disco_progress_apply(void);
static void apply_disco_progress(int v){ (void)v; disco_progress_apply(); }
void dhome_times_apply(void);
static void apply_disco_times(int v){ (void)v; dhome_times_apply(); }
void disco_sheen_apply(void);
static void apply_disco_sheen(int v){ (void)v; disco_sheen_apply(); }
static const char *OPT_DPROG[] = { "Disco", "Accent", "Off" };
static const char *OPT_DEQ[] = { "Disco", "Accent" };
static const char *OPT_DSHAPE[] = { "Linear", "Arc" };
static const char *OPT_DTAL[] = { "Centred", "Left" };
static const char *OPT_DSCROLL[] = { "Classic", "Straight", "Disco" };
static const char *OPT_DSIDE[] = { "Right", "Left" };
/* Side: pick with the arrows, then Apply (on the Side page) - the interface restarts with the hub (and everything around it) on that side */
static void apply_disco_side_ok(int v){
    (void)v;
    int pick = cfg_get_int("disco_side_pick", cfg_get_int("disco_side", 0)), cur = cfg_get_int("disco_side", 0);
    if(pick == cur){ ui_toast(cur ? "Already on the left" : "Already on the right"); return; }
    g_set_prev = cur; cfg_set_int("disco_side", pick);
    theme_reload_or_revert("disco_side");
}
static void side_apply_cb(lv_event_t *e){ (void)e; apply_disco_side_ok(0); }
static const char *OPT_DHOLD[] = { "Immersive", "Favourite", "Nothing" };
void dhome_title_apply(void);
static void apply_disco_title(int v){ (void)v; dhome_title_apply(); }
static void apply_disco_hold(int v){ (void)v; }
static const char *OPT_DIMM[] = { "Vinyl", "CD" };
void ui_imm_style_apply(void);
static void apply_disco_imm(int v){ (void)v; ui_imm_style_apply(); }                       /* read on each hold; marks the row as Disco's */
void eqcustom_style_apply(void);
static void apply_disco_eq(int v){ (void)v; eqcustom_style_apply(); }
static void apply_shortcuts(int v){ (void)v; screen_show(SCR_SCCONFIG); }  /* opens the Shortcuts picker */

/* ---- Default-UI preference + Restart ------------------------------------- *
 * Boot model: the user picks a persistent default UI (diskOS or Stock) here;
 * holding Vol-Up at power-on boots the OTHER one for that boot. The boot hook
 * (fiio_init.sh) reads a flag file: /usr/data/boot_default_stock present => the
 * default is Stock; absent => the default is diskOS. We mirror the cycler value
 * to that flag so the choice persists across reboots. */
/* This is the user's route to the STOCK firmware, so it must not be able to hang. It used to shell out
 * to `touch|rm && sync`: a full filesystem sync can block indefinitely on a stuck device, and it ran
 * after the boot watchdog was already disarmed. Done natively now - create/remove the flag and fsync only
 * the directory holding it, which is what actually makes the choice durable - with no shell and no
 * global sync. Failure is reported rather than silently diverging from the shown setting. */
/* The write runs on its own small thread (no fork() of the big UI, no child that could look like "mq_ui" to the
 * watchdog): the UI waits at most ~3 s for it. A write stuck in storage I/O only parks that thread; until it ends, no
 * second one is started and the change is reported as failed. */
static _Atomic int g_bd_busy;                                       /* a writer thread is running */
static _Atomic int g_bd_res;                                        /* 0 running, 1 recorded, 2 failed */
static void *boot_default_writer(void *arg){
    const char *flag = "/usr/data/boot_default_stock";
    int v = (int)(intptr_t)arg, r = 0;
    if(v == 1){
        int fd = open(flag, O_WRONLY|O_CREAT|O_CLOEXEC, 0644);
        if(fd < 0) r = -1;
        else {
            if(fsync(fd) != 0) r = -1;
            if(close(fd) != 0) r = -1;
        }
    } else {
        if(unlink(flag) != 0 && errno != ENOENT) r = -1;
    }
    if(r == 0){                                                     /* the directory entry durable too */
        int dfd = open("/usr/data", O_RDONLY|O_DIRECTORY|O_CLOEXEC);
        if(dfd < 0) r = -1;
        else {
            if(fsync(dfd) != 0) r = -1;
            if(close(dfd) != 0) r = -1;
        }
    }
    atomic_store(&g_bd_res, r == 0 ? 1 : 2);
    atomic_store(&g_bd_busy, 0);
    return NULL;
}
static void apply_boot_default(int v){
    int ok = 0;
    if(!atomic_exchange(&g_bd_busy, 1)){
        atomic_store(&g_bd_res, 0);
        pthread_t t; pthread_attr_t a; pthread_attr_init(&a);
        pthread_attr_setdetachstate(&a, PTHREAD_CREATE_DETACHED); pthread_attr_setstacksize(&a, 64 * 1024);
        if(pthread_create(&t, &a, boot_default_writer, (void *)(intptr_t)v) != 0) atomic_store(&g_bd_busy, 0);
        else for(int i = 0; i < 30; i++){                           /* ~3 s */
            int r = atomic_load(&g_bd_res);
            if(r){ ok = r == 1; break; }
            usleep(100000);
        }
        pthread_attr_destroy(&a);
    }
    if(!ok) ui_toast("Couldn't change boot UI");
}

static lv_obj_t *g_boot_modal;
static void boot_modal_close(void){ if(g_boot_modal){ lv_obj_delete_async(g_boot_modal); g_boot_modal = NULL; } }
static lv_timer_t *g_rst_timer;
static void boot_cancel_cb(lv_event_t *e){ if(lv_event_get_code(e)==LV_EVENT_CLICKED && !g_rst_timer) boot_modal_close(); }   /* no cancel once a restart is under way */
/* Restart: only reboots once every admitted SD writer has finished AND a full `sync` has SUCCEEDED; otherwise it
 * refuses, reopens card access and says so. (v1.1.3 withheld it: it gave the flush 2 s, then rebooted regardless.)
 * Runs as a timer state machine, so the UI never blocks while it waits. */
#define RESTART_WRITERS_MS 10000   /* admitted writers must drain within this */
#define RESTART_SYNC_MS    15000   /* and the flush must complete within this */
static int         g_rst_phase;    /* 0 = waiting for writers, 1 = waiting for sync */
static uint32_t    g_rst_t0;
static pid_t       g_rst_sync, g_rst_clock;
static pid_t spawn_detached(const char *cmd){      /* own process group; posix_spawn (see bounded_detached) */
    pid_t p = 0;
    char *argv[] = { "/bin/sh", "-c", (char *)cmd, NULL };
    return command_spawn(&p, argv) == 0 ? p : -1;
}
static void stop_child(pid_t *p){ kill_and_reap(*p); *p = 0; }
/* For diagnosis over SSH: cat /tmp/restart_last. tmpfs, so writing it can never block on the card it is about (the
 * restart did not happen, so it is still there); det names what failed with its own code, not a stale errno. */
static void restart_fail(const char *why, const char *det){
    {   FILE *f = fopen("/tmp/restart_last", "w");
        if(f){ fprintf(f, "%s: %s (phase %d, drained %d, %u ms)\n", why, det ? det : "-", g_rst_phase, ui_sd_quiesce_drained(), (unsigned)lv_tick_elaps(g_rst_t0)); fclose(f); }
    }
    stop_child(&g_rst_sync); stop_child(&g_rst_clock);
    if(g_rst_timer){ lv_timer_delete(g_rst_timer); g_rst_timer = NULL; }
    ui_sd_quiesce_end();           /* reopen card access (readers + writers) if the card is still local */
    boot_modal_close();
    ui_toast(why);
}
static void restart_tick(lv_timer_t *t){
    (void)t;
    rst_reap_sweep();
    if(g_rst_phase == 0){
        if(ui_sd_quiesce_drained()){                           /* every admitted reader AND writer has finished */
            g_rst_sync = spawn_detached("sync");
            if(g_rst_sync <= 0){ g_rst_sync = 0; restart_fail("Couldn't restart - try again", "sync could not be started"); return; }
            g_rst_phase = 1; g_rst_t0 = lv_tick_get();
        } else if(lv_tick_elaps(g_rst_t0) >= RESTART_WRITERS_MS){
            restart_fail("Couldn't restart - card busy, try again", "card readers/writers still busy");
        }
        return;
    }
    int st = 0;
    pid_t w = waitpid(g_rst_sync, &st, WNOHANG);
    if(w == g_rst_sync){
        g_rst_sync = 0;
        if(!WIFEXITED(st) || WEXITSTATUS(st) != 0){ char d[48];
            if(WIFEXITED(st)) snprintf(d, sizeof d, "sync exit %d", WEXITSTATUS(st)); else snprintf(d, sizeof d, "sync killed by signal %d", WIFSIGNALED(st) ? WTERMSIG(st) : -1);
            restart_fail("Couldn't restart - card flush failed", d); return; }
        stop_child(&g_rst_clock);   /* the RTC save had the whole wait; a still-running one is stuck */
        reboot(RB_AUTOBOOT);        /* direct syscall: no shell, no PATH, no binary on disk */
        char d[48]; snprintf(d, sizeof d, "reboot() refused, errno %d", errno);   /* read right after the call */
        bounded_detached("reboot", 5000);   /* only reached if the syscall was refused */
        restart_fail("Couldn't restart - try again", d);
        return;
    }
    if(lv_tick_elaps(g_rst_t0) >= RESTART_SYNC_MS) restart_fail("Couldn't restart - card busy, try again", "sync still running after 15 s");
}
static void boot_confirm_cb(lv_event_t *e){
    if(lv_event_get_code(e)!=LV_EVENT_CLICKED) return;
    if(g_rst_timer) return;                         /* already restarting */
    if(rst_reap_free() < 3){ ui_toast("Couldn't restart - try again"); return; }   /* sync + clock + reboot fallback */
    /* Stop our own SD work: closing admission alone is not enough - a scan holds one lease for its whole walk and
     * the prewarm decoder is non-cancellable, so both are stopped explicitly. Then wait for the rest to drain. */
    ui_sd_quiesce_begin();          /* closes sd_io leases AND sd_write_begin writers; storage_tick won't reopen them */
    scanner_abort();
    art_kill_all();
    /* Save the (NTP-corrected) clock to the RTC alongside - the periodic save runs only every 5 min and the stock
     * player saves the RTC only on ITS power-off path. Best effort, its own process group, never waited on. */
    g_rst_clock = spawn_detached("hwclock -w 2>/dev/null");
    if(g_rst_clock < 0) g_rst_clock = 0;
    g_rst_phase = 0; g_rst_t0 = lv_tick_get();
    g_rst_timer = lv_timer_create(restart_tick, 100, NULL);
    if(!g_rst_timer){ restart_fail("Couldn't restart - try again", "no timer"); return; }
    ui_toast("Restarting...");
}
static void boot_modal_pill(lv_obj_t *card, int x, const char *txt, theme_color_role_t col, lv_event_cb_t cb){
    lv_obj_t *b = lv_button_create(card);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, 108, 42); lv_obj_align(b, LV_ALIGN_BOTTOM_MID, x, -16);
    lv_obj_set_ext_click_area(b, 4);   /* 42px pill -> ~50px touch target */
    lv_obj_set_style_radius(b, 12, 0);
    lv_obj_set_style_bg_color(b, TC(SURFACE_RAISED), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    ui_on(b, cb, LV_EVENT_CLICKED, NULL, "settings.cb", UI_CORE);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, TF(UI_16), 0);
    lv_obj_set_style_text_color(l, theme_color(col), 0);
    lv_obj_center(l);
}
static const char *g_restart_note;   /* one-shot subtitle for the next restart modal (settings_restart_prompt) */
static void apply_restart(int v){ (void)v;
    boot_modal_close();
    g_boot_modal = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(g_boot_modal);
    lv_obj_set_size(g_boot_modal, 360, 360); lv_obj_center(g_boot_modal);
    lv_obj_set_style_bg_color(g_boot_modal, TC(CANVAS), 0);
    lv_obj_set_style_bg_opa(g_boot_modal, LV_OPA_70, 0);
    lv_obj_clear_flag(g_boot_modal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_boot_modal, LV_OBJ_FLAG_CLICKABLE);                       /* absorb taps */
    ui_on(g_boot_modal, boot_cancel_cb, LV_EVENT_CLICKED, NULL, "settings.boot_cancel", UI_CORE);  /* tap outside = cancel */
    lv_obj_t *card = lv_obj_create(g_boot_modal);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, 280, 184); lv_obj_center(card);
    lv_obj_set_style_radius(card, 18, 0);
    lv_obj_set_style_bg_color(card, TC(SURFACE), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *t = lv_label_create(card);
    lv_label_set_text(t, "Restart now?");
    lv_obj_set_style_text_font(t, TF(UI_16), 0);
    lv_obj_set_style_text_color(t, TC(TEXT_PRIMARY), 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 22);
    lv_obj_t *s = lv_label_create(card);
    lv_label_set_text(s, g_restart_note ? g_restart_note : "Boots your default UI. Hold Vol-Up at power-on for the other one.");
    g_restart_note = NULL;
    lv_label_set_long_mode(s, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s, 236);
    lv_obj_set_style_text_align(s, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(s, TF(UI_14), 0);
    lv_obj_set_style_text_color(s, TC(TEXT_MUTED), 0);
    lv_obj_align(s, LV_ALIGN_TOP_MID, 0, 50);
    boot_modal_pill(card, -58, "Cancel",  0xC7C7CC, boot_cancel_cb);
    boot_modal_pill(card,  58, "Restart", UI_RED, boot_confirm_cb);
}
/* write the backlight sysfs only (no config change) - used for transient dimming.
 * v==0 fully powers the panel backlight DOWN: writing brightness 0 alone leaves the
 * LED driver enabled at its minimum (screen looks black but the backlight glows), so
 * we also toggle bl_power (0 = FB_BLANK_UNBLANK on, 4 = FB_BLANK_POWERDOWN off). */
void ui_backlight(int v){
    if(v < 0) v = 0; if(v > 40) v = 40;
    if(v > 0){                     /* set target level first, then power on (no flash) */
        FILE *f = fopen("/sys/class/backlight/backlight/brightness", "w");
        if(f){ fprintf(f, "%d", v); fclose(f); }
        else  fprintf(stderr, "backlight brightness open failed: %s\n", strerror(errno));
    }
    FILE *p = fopen("/sys/class/backlight/backlight/bl_power", "w");
    if(p){ fprintf(p, "%d", v ? 0 : 4); fclose(p); }
    else  fprintf(stderr, "backlight bl_power open failed: %s\n", strerror(errno));
}
static void apply_brightness(int v){ if(v < 1) v = 1; ui_backlight(v); }
void settings_apply_startup(void){
    apply_brightness(ui_effective_brightness());
    { int at = mdb_sysconfig_auto_time();              /* mirror the player's switch (it owns the value) */
      if(at >= 0 && at != cfg_get_int("auto_time", 1)) cfg_set_int("auto_time", at); }
    { int ac = mdb_artist_class_type();                 /* the player's Artist / Album Artist grouping (it owns it) */
      if(ac >= 0 && ac != cfg_get_int("artist_class", 0)) cfg_set_int("artist_class", ac);
      mdb_set_artist_mode(ac == 1); }                   /* unknown -> by artist, as the player's queue can't be matched */
    { int fj = mdb_sysconfig_folder_jump();             /* the player's Play Through Folders switch (it owns it) */
      if(fj >= 0 && fj != cfg_get_int("folder_jump", 0)) cfg_set_int("folder_jump", fj); }
    power_startup_mirror();        /* the player's own Idle poweroff / Charging optimization values */
    controls_startup_mirror();     /* the player's Screen rotation and volume-key actions */
    cfg_set_int("sleep_idx", 0);   /* never auto-arm a sleep timer across reboots */
    apply_boot_default(cfg_get_int("boot_default", 0));  /* keep the boot-hook flag in sync */
    /* Supply fork defaults only for absent preferences; upstream and user choices survive. */
    cfg_begin();
    if(!cfg_get_int("np_poster_v1", 0)){          /* this fork's Now Playing is the Poster style, once */
        if(cfg_get_int("np_style", -1)<0) cfg_set_int("np_style", 2);
        cfg_set_int("np_poster_v1", 1);
    }
    if(!cfg_get_int("saver_ring_v1", 0)){             /* the Dim Ring clock becomes the screensaver once */
        if(cfg_get_int("saver_style", -1)<0) cfg_set_int("saver_style", 5);
        cfg_set_int("saver_ring_v1", 1);
    }
    if(!cfg_get_int("accent_red_v1", 0)){
        if(cfg_get_int("accent_mode", -1)<0) cfg_set_int("accent_mode", 1);
        if(cfg_get_int("accent_color", -1)<0) cfg_set_int("accent_color", UI_RED);
        cfg_set_int("accent_red_v1", 1);
    }
    if(cfg_commit()) fprintf(stderr,"settings: fork defaults save failed\n");
    ui_set_accent_config(cfg_get_int("accent_mode", 1), cfg_get_int("accent_color", UI_RED));
    apply_artcache(cfg_get_int("artcache", 0));          /* prewarm off by default */
}

/* shared brightness API (used by Quick Settings too) - persists the value */
void ui_set_brightness(int v){
    if(v < 1) v = 1; if(v > 40) v = 40;
    apply_brightness(v);
    cfg_set_int("brightness", v);
}
int ui_get_brightness(void){
    int v = cfg_get_int("brightness", 16);
    return v < 1 ? 16 : v;   /* never 0/negative: wake paths pass this to ui_backlight,
                              * and 0 would power the panel DOWN while logically awake */
}
/* the level the screen actually runs at: full in Outdoor mode, else the user's. The user's own level stays saved
 * and comes back when Outdoor mode is turned off. */
int ui_effective_brightness(void){ return theme_outdoor() ? 40 : ui_get_brightness(); }
static void apply_brightness_row(int v){ if(!theme_outdoor()) apply_brightness(v); }   /* Outdoor holds full */

static const char *const OPT_MODE[] = { "Sequential", "Shuffle", "Repeat One", "Repeat All", "Single" };
/* 0..10 = built-in presets; 11..20 = the ten user PEQ slots (USER1..USER10), edited in Custom EQ.
 * Index here maps 1:1 to STYLE_PRESET, so the cycler value is exactly what ui_apply_eq() selects. */
static const char *const OPT_EQ[]   = { "Off","Jazz","Rock","R&B","Hip-Hop","Pop","Dance","Classical","Retro","Sibilance 1","Sibilance 2",
                                        "USER1","USER2","USER3","USER4","USER5","USER6","USER7","USER8","USER9","USER10" };
static const char *const OPT_SLEEP[] = { "Off","15 min","30 min","45 min","60 min","90 min" };
static const char *const OPT_SLEEP_ACT[] = { "Pause","Shut down" };
static const char *const OPT_IDLE_OFF[POWER_IDLE_N] = { "Off","5 min","10 min","30 min","60 min","90 min","120 min" };
static const char *const OPT_POWER[] = { "Off","30 sec","1 min","2 min","5 min" };  /* idx->TMAP secs in main.c */
static const char *const OPT_NPSTYLE[] = { "Cover", "Vinyl", "Poster", "Ring" };
static const char *const OPT_APPEARANCE[] = { "Dark", "Light" };
static const char *const OPT_NPFONT[] = {"Original", "Inter Semibold", "Nunito Bold"};
static const char *const OPT_FONTSIZE[] = { "Small", "Medium", "Large" };
static const char *const OPT_ALBUMVIEW[] = { "List", "Cover Flow" };
static const char *const OPT_SAVERSTYLE[] = { "Cover", "Analog", "Minimal", "Digital", "Vinyl", "Ring" };
static const char *const OPT_DISCCOLOUR[] = { "Black", "Turquoise", "Pink" };
static const char *const OPT_BOOTDEF[] = { "diskOS", "Stock" };
static const char *const OPT_ARTCACHE[] = { "Covers only","When Idle","Idle & Charging" };

/* Public EQ preset name lookup (Quick Settings drawer toast, etc.); mirrors OPT_EQ indexing 0..20. */
const char *ui_eq_name(int i){ return (i >= 0 && i < 21) ? OPT_EQ[i] : "Off"; }
/* audio cluster cyclers - all use min=-1 so the value can be "System default" (unmanaged) */
static const char *const OPT_DRE[]    = { "Off", "On" };
static const char *const OPT_AUTOTAG[] = { "Off", "On" };
static const char *const OPT_REPLAYGAIN[] = { "Off", "Track", "Album" };
static const char *const OPT_GAIN[]   = { "Low", "High" };
static const char *const OPT_DFILTER[]= { "Fast LL","Slow LL","Slow PC","Fast PC","NOS","Wideband" };
static const char *const OPT_MEMORY[] = { "Off", "Position", "Song" };
static const char *const OPT_ARTISTCLASS[] = { "By Artist", "By Album Artist" };
static const char *const OPT_ROTATION[] = { "Normal", "Turn 90", "Turn 180", "Turn 270" };   /* stock values 0..3 */
static const char *const OPT_VOLKEY[] = { "Switch Track", "Adjust Volume" };
static const char *const D_ARTCACHE[] = {
    "Album covers preload in the background so browsing stays smooth. Per-track art is decoded only as you play it (default).",
    "Covers preload, plus every track's art is pre-decoded while idle. Decoding can warm the player; it pauses automatically if it gets hot.",
    "Covers preload, plus every track's art is pre-decoded while idle AND charging. Decoding can warm the player; it pauses automatically if it gets hot.",
};

/* per-option descriptions for the cyclers (parallel to the OPT_ arrays) */
static const char *const D_MODE[] = {
    "Play through the list in order, then stop.",
    "Play tracks in a random order.",
    "Repeat the current track over and over.",
    "Loop the whole list when it ends.",
    "Play the current track once, then stop.",
};
static const char *const D_NPSTYLE[] = {
    "Album cover shown as a rounded square.",
    "Spinning vinyl record while a track plays.",
    "Full-cover Poster with the existing immersive record and lyrics view.",
};

static const setting_t TABLE[] = {
    { "Playback", "Play Mode",   ST_CYCLER, "work_mode", 0,0,0, OPT_MODE, 5, NULL, apply_mode, 0,
      "How the player advances through tracks.", D_MODE },
    { "Playback", "Equalizer",   ST_CYCLER, "eq_preset", 0,0,0, OPT_EQ,  21, NULL, apply_eq,   0,
      "Tone preset sent to the player; audible effect is still being verified.", NULL },
    { "Playback", "Up Next", ST_TOGGLE, "up_next", 0,1,0, NULL,0, NULL, apply_upnext, 0,
      "Show Up Next beside Home Favourites. Hold the Queue shortcut on Now Playing to view it; a tap still opens your editable Queue.", NULL },
    { "Playback", "Auto-tag",    ST_CYCLER, "autotag", 0,0,0, OPT_AUTOTAG, 2, NULL, apply_autotag, 0,
      "On Wi-Fi, a track that plays for 10 s and lacks synced lyrics or artwork gets them added to its tags. Not found: tried again on a later play, at most once a day.", NULL },
    { "Playback", "Custom EQ",    ST_ACTION, NULL, 0,0,0, NULL,0, LV_SYMBOL_RIGHT, apply_eq_custom, 0,
      "View the custom curve. Editing is available only on firmware with a verified custom EQ protocol.", NULL },
    { "Playback", "DSD Output",  ST_READONLY, NULL, 0,0,0, NULL,0, "Auto", NULL, 0,
      "The player auto-selects the DSD mode; this control isn't user-adjustable yet.", NULL },
    { "Playback", "Resume Playback", ST_CYCLER, "memory_play", 0,0,0, OPT_MEMORY, 3, NULL, apply_memory, 0,
      "On power-on: Off = start fresh, Position = resume the exact spot, Song = reopen the last track.", NULL },
    { "Playback", "Artists",     ST_CYCLER, "artist_class", 0,0,0, OPT_ARTISTCLASS, 2, NULL, apply_artist_class, 0,
      "Group the Artists list by each song's artist, or by its album artist - the same choice as the stock player.", NULL },
    { "Playback", "Play Through Folders", ST_TOGGLE, "folder_jump", 0,1,1, NULL, 0, NULL, apply_folder_jump, 0,
      "When an album, artist, genre or folder finishes, keep playing the next one. Works with Sequential, Shuffle and Loop All.", NULL },
    /* Audio/DAC cluster - cyclers with min=-1 so they can read "System default" (unmanaged):
     * until you pick a value diskOS sends nothing + the player keeps its own setting. */
    { "Audio",    "Working Mode", ST_ACTION, NULL, 0,0,0, NULL,0, LV_SYMBOL_RIGHT, apply_workmode, 0,
      "Switch the audio source: local playback, USB DAC, Bluetooth receiving, or USB storage.", NULL },
    { "Audio",    "Gain",        ST_CYCLER, "audio_gain",   0,0,0, OPT_GAIN, 2, NULL, apply_gain, 0,
      "Headphone output gain. High drives demanding headphones louder.", NULL },
    { "Audio",    "DAC Filter",  ST_CYCLER, "audio_filter", 0,0,0, OPT_DFILTER, 6, NULL, apply_dac_filter, 1,
      "CS43131 digital filter roll-off. A subtle tone/transient tradeoff.", NULL },
    { "Audio",    "ReplayGain",  ST_CYCLER, "replay_gain",  0,0,0, OPT_REPLAYGAIN, 3, NULL, apply_replay_gain, 0,
      "Level the volume across tracks. Track uses each song's gain; Album keeps an album's relative dynamics.", NULL },
    { "Audio",    "DRE",         ST_CYCLER, "audio_dre",    0,0,0, OPT_DRE, 2, NULL, apply_dre, 1,
      "Dynamic Range Enhancement (CS43131) for quieter listening.", NULL },
    { "Audio",    "Gapless",     ST_CYCLER, "gapless",      0,0,0, OPT_DRE, 2, NULL, apply_gapless, 0,
      "Play tracks with no silent gap between them.", NULL },
    { "Audio",    "Max Volume",  ST_SLIDER, "max_vol",      10,120,5, NULL,0, NULL, apply_maxvol, 120,
      "Cap the maximum volume level (protects your ears / headphones).", NULL },
    { "Audio",    "Balance",     ST_SLIDER, "balance",      -10,10,1, NULL,0, NULL, apply_balance, 0,
      "Left/right channel balance. 0 = centred; negative = left, positive = right.", NULL },
#ifdef DISKOS_TEST_OUTPUTS   /* device-unverified: test builds only */
    { "Audio",    "SPDIF Output", ST_TOGGLE, "spdif",   0,1,1, NULL,0, NULL, apply_spdif, 0,
      "Send audio to the digital SPDIF output instead of the internal DAC. Playback pauses for a moment while it switches.", NULL },
#endif
    { "Display",  "Brightness",  ST_SLIDER, "brightness", 4,40,2, NULL,0, NULL, apply_brightness_row, 16,
      "Screen backlight level. While Outdoor Mode is on the screen stays at full brightness.", NULL },
    { "Display", "Theme", ST_ACTION, NULL, 0,0,0, NULL,0, LV_SYMBOL_RIGHT, apply_grp_themecolours, 0,
      "Theme, Appearance (dark or light), Automatic Appearance, Outdoor Mode and the Accent colour.", NULL },
    { "Display",  "Disco Options", ST_ACTION, NULL, 0,0,0, NULL,0, LV_SYMBOL_RIGHT, apply_disco_options, 0,
      "The Disco theme's own settings: the menu circle, clock, track times, rim sheen, equalizer and progress bar colours.", NULL },
    { "Display", "Text & Layout", ST_ACTION, NULL, 0,0,0, NULL,0, LV_SYMBOL_RIGHT, apply_grp_textlayout, 0,
      "Font size, screen rotation, the clock format, track numbers, animations and the back-swipe distance.", NULL },
    { "Display", "Standby", ST_ACTION, NULL, 0,0,0, NULL,0, LV_SYMBOL_RIGHT, apply_grp_screenstandby, 0,
      "What the screen does when you leave it: screensaver and when the screen switches off.", NULL },
    { "Display", "Online & Extras", ST_ACTION, NULL, 0,0,0, NULL,0, LV_SYMBOL_RIGHT, apply_grp_onlineextras, 0,
      "Online album art and lyrics, the art cache, weather on Home, Shortcuts and the album view.", NULL },
    { "Theme Colours",  "Theme",       ST_CHOICE, "theme_preset", 0,0,0, theme_preset_names, THEME_PRESET_COUNT, NULL, apply_theme_preset, THEME_PRESET_BRAUN,
      "Colour theme for the whole interface. Each has dark and light screens (see Appearance).", NULL },
    { "Theme Colours",  "Appearance",  ST_CYCLER, "theme_variant", 0,0,0, OPT_APPEARANCE, 2, NULL, apply_appearance, 1,
      "Dark or light screens. Changing it redraws the screen in a moment; music keeps playing.", NULL },
    { "Theme Colours",  "Automatic Appearance", ST_TOGGLE, "theme_auto", 0,1,1, NULL,0,NULL,apply_theme_auto,0,
      "Ring, Braun and Disco use Light from 07:00 to 20:00 and Dark overnight. Changes apply when the screen turns off. Outdoor Mode keeps Light on.",NULL },
    { "Theme Colours",  "Outdoor Mode", ST_TOGGLE, "outdoor",  0,1,1, NULL, 0, NULL, apply_outdoor, 0,
      "Light screens at full brightness, easier to read in sunlight. Uses more battery. Turning it off returns your own brightness.", NULL },
    { "Theme Colours",  "Accent Colour", ST_ACTION, NULL, 0,0,0, NULL,0, LV_SYMBOL_RIGHT, apply_open_colorpick, 0,
      "Now Playing accent: derived from album art, or any fixed colour you pick.", NULL },
    { "Text & Layout",  "Font Size",   ST_CYCLER, "font_size", 0,0,0, OPT_FONTSIZE, 3, NULL, apply_font_size, 1,
      "Size of list and body text: Small, Medium or Large. Titles and clocks keep their size. The screen redraws in a moment.", NULL },
    { "Text & Layout",  "Screen Rotation", ST_CYCLER, CTL_CFG_ROT, 0,0,0, OPT_ROTATION, CTL_ROT_N, NULL, apply_screen_rot, 0,
      "Turn the picture and touch clockwise: Normal, 90, 180 or 270 degrees. Stored by the player, as in the stock menu.", NULL },
    { "Text & Layout",  "24-Hour Time", ST_TOGGLE, "time_24h",  0,1,1, NULL, 0, NULL, apply_time, 1,
      "Use a 24-hour clock instead of AM/PM.", NULL },
    { "Text & Layout",  "Track Numbers", ST_TOGGLE, "track_numbers", 0,1,1, NULL, 0, NULL, apply_track_numbers, 0,
      "Show each song's track number in album lists.", NULL },
    { "Text & Layout",  "Animations",  ST_TOGGLE, "anim",      0,1,1, NULL, 0, NULL, apply_anim, 1,
      "Slide animations between screens.", NULL },
    { "Text & Layout",  "Back-swipe",  ST_SLIDER, "swipe_thresh", 30,120,5, NULL,0, NULL, apply_swipe, 60,
      "Swipe distance needed to go back. Lower is more sensitive.", NULL },
    { "Text & Layout",  "Now Playing", ST_CYCLER, "np_style",   0,0,0, OPT_NPSTYLE, 4, NULL, apply_np_style, 0,
      "Album art style on the Now Playing screen.", D_NPSTYLE },
    { "Text & Layout",  "Disc Colour", ST_CYCLER, "disc_colour", 0,0,0, OPT_DISCCOLOUR, 3, NULL, NULL, 0,
      "The colour of the Disc drawn on the startup screen. Set it to match yours.", NULL },
    { "Standby",  "Screensaver", ST_CYCLER, "saver_idx",  0,0,0, OPT_POWER, 5, NULL, NULL, 2,
      "Idle time before the clock screensaver appears and the screen dims.", NULL },
    { "Standby",  "Saver Style", ST_CYCLER, "saver_style", 0,0,0, OPT_SAVERSTYLE, 6, NULL, NULL, 0,
      "Screensaver look: Cover art, Analog clock, Minimal, Digital, spinning Vinyl, or the Ring clock.", NULL },
    { "Standby",  "Screen Off",  ST_CYCLER, "screenoff_idx", 0,0,0, OPT_POWER, 5, NULL, NULL, 3,
      "How long after the screensaver the screen turns fully off.", NULL },
    { "Online & Extras",  "Online Album Art", ST_TOGGLE, "online_art", 0,1,1, NULL, 0, NULL, apply_online_art, 0,
      "Songs with no cover of their own get one from the internet (Wi-Fi). Each album is looked up once; playback is never touched.", NULL },
    { "Online & Extras",  "Online Lyrics", ST_TOGGLE, "online_lyrics", 0,1,1, NULL, 0, NULL, NULL, 1,
      "Songs with no lyrics of their own (no .lrc file, none in the song's tags) are looked up on lrclib.net (Wi-Fi). Off stops diskOS's own lookup; lyrics from the SD card or the song's tags still show.", NULL },
    { "Online & Extras",  "Album Art Cache", ST_CYCLER, "artcache", 0,0,0, OPT_ARTCACHE, 3, NULL, apply_artcache, 0,
      "Pre-decode album art so covers load instantly. Background decoding can warm the player; it throttles on heat.", D_ARTCACHE },
    { "Online & Extras",  "Weather on Home", ST_TOGGLE, "weather_on", 0,1,1, NULL, 0, NULL, apply_weather, 1,
      "Show the weather glance on the home screen and screensaver (tap it to open the full forecast). Off skips the background fetch to save battery.", NULL },
    { "Online & Extras",  "Shortcuts", ST_ACTION, NULL, 0,0,0, NULL,0, LV_SYMBOL_RIGHT, apply_shortcuts, 0,
      "Choose up to five shortcuts for the panel opened by swiping left from Home.", NULL },
    { "Online & Extras",  "Album View",  ST_CYCLER, "album_view", 0,0,0, OPT_ALBUMVIEW, 2, NULL, NULL, 0,
      "How the Albums list looks: a text list, or a cover flow you flick through.", NULL },
    { "Disco Options", "Disco Menu", ST_ACTION, NULL, 0,0,0, NULL,0, LV_SYMBOL_RIGHT, apply_disco_menu, 0,
      "The sections of the black menu disc, in order. Add, remove and move them; Music and Settings always stay.", NULL },
    { "Disco Options", "Music Screen", ST_ACTION, NULL, 0,0,0, NULL,0, LV_SYMBOL_RIGHT, apply_grp_musicscreen, 0,
      "The Music screen: clock, track times, track number, where the title sits and what holding it does.", NULL },
    { "Disco Options", "Colours", ST_ACTION, NULL, 0,0,0, NULL,0, LV_SYMBOL_RIGHT, apply_grp_discocolours, 0,
      "Rim sheen and the colours of the equalizer, volume and progress, plus the progress shape.", NULL },
    { "Disco Options", "Layout", ST_ACTION, NULL, 0,0,0, NULL,0, LV_SYMBOL_RIGHT, apply_grp_discolayout, 0,
      "Which side the navigation circle sits on, and how lists scroll.", NULL },
    { "Disco Options", "Immersive", ST_CYCLER, "disco_imm", 0,0,0, OPT_DIMM, 2, NULL, apply_disco_imm, 0,
      "The full-screen spinning cover: Vinyl (grooves and a dark label) or CD (a clear hub, a still rainbow sheen and faint spokes, like light on a spinning disc). The lyrics shade stays the same.", NULL },
    { "Music Screen", "Show Clock", ST_TOGGLE, "disco_clock", 0,1,1, NULL,0, NULL, apply_disco_clock, 1,
      "Show the clock, weather and temperature over the cover on Music.", NULL },
    { "Music Screen", "Track Times", ST_TOGGLE, "disco_times", 0,1,1, NULL,0, NULL, apply_disco_times, 1,
      "Small elapsed and remaining times under the ends of the progress line on Music.", NULL },
    { "Music Screen", "Track Number", ST_TOGGLE, "disco_trackno", 0,1,1, NULL,0, NULL, NULL, 0,
      "Show the song's track number before its title on Music, as in \"3. Northern Lights\" (from the song's tags).", NULL },
    { "Music Screen", "Track Font", ST_CYCLER, "np_font", 0,0,0, OPT_NPFONT, 3, NULL, apply_np_font, 0,
      "Original, Inter Semibold or Nunito Bold for title and artist. Alternatives use larger, heavier text. Applying redraws the interface.", NULL },
    { "Music Screen", "Title Position", ST_CYCLER, "disco_title_al", 0,0,0, OPT_DTAL, 2, NULL, apply_disco_title, 0,
      "Where the title and artist sit on Music: centred, or from the left.", NULL },
    { "Music Screen", "Title Hold", ST_CYCLER, "disco_title_hold", 0,0,0, OPT_DHOLD, 3, NULL, apply_disco_hold, 0,
      "What holding the title on Music does: open the cover full screen, or favourite (or unfavourite) the song. A tap still opens the track menu.", NULL },
    { "Disco Colours", "Rim Sheen", ST_TOGGLE, "disco_sheen", 0,1,1, NULL,0, NULL, apply_disco_sheen, 1,
      "The faint CD rainbow around the edge of Music and playlists.", NULL },
    { "Disco Colours", "Equalizer", ST_CYCLER, "disco_eq", 0,0,0, OPT_DEQ, 2, NULL, apply_disco_eq, 0,
      "The colours of the equalizer's faders: Disco gives every band its own CD colour, Accent uses the album's accent (or your picked one).", NULL },
    { "Disco Colours", "Volume", ST_CYCLER, "disco_vol", 0,0,0, OPT_DEQ, 2, NULL, apply_disco_eq, 0,
      "The colour of the volume arc: Disco is the CD rainbow, Accent uses the album's accent (or your picked one).", NULL },
    { "Disco Colours", "Progress Bar", ST_CYCLER, "disco_progress", 0,0,0, OPT_DPROG, 3, NULL, apply_disco_progress, 0,
      "The progress on Music (line or arc, see Progress Shape): Disco is the rainbow, Accent follows the cover colour (or your picked accent), Off hides it.", NULL },
    { "Disco Colours", "Progress Shape", ST_CYCLER, "disco_prog_shape", 0,0,0, OPT_DSHAPE, 2, NULL, apply_disco_progress, 0,
      "Linear is the line under the title. Arc runs the progress round the edge of Music instead, from just below the menu, clockwise, to just above it. Drag or tap either to seek.", NULL },
    { "Disco Layout", "Side", ST_CYCLER, "disco_side_pick", 0,0,0, OPT_DSIDE, 2, NULL, NULL, 0,
      "Pick a side, then Apply. The interface restarts.", NULL },
    { "Disco Layout", "Scroll", ST_CYCLER, "disco_scroll", 0,0,0, OPT_DSCROLL, 3, NULL, NULL, 0,
      "How list rows move as they scroll: Classic follows the screen's edge, Straight keeps them flat, Disco swings them round the navigation circle like spokes of a CD.", NULL },
    { "System", "Power", ST_ACTION, NULL, 0,0,0, NULL,0, LV_SYMBOL_RIGHT, apply_grp_power, 0,
      "Sleep timer, idle power-off, the charging limit and shutting down.", NULL },
    { "System", "Date & Time", ST_ACTION, NULL, 0,0,0, NULL,0, LV_SYMBOL_RIGHT, apply_grp_datetime, 0,
      "Automatic time, setting the date and time by hand, and the time zone.", NULL },
    { "System", "Library", ST_ACTION, NULL, 0,0,0, NULL,0, LV_SYMBOL_RIGHT, apply_grp_library, 0,
      "Rescan the library and import playlists from the card.", NULL },
    { "System", "Controls", ST_ACTION, NULL, 0,0,0, NULL,0, LV_SYMBOL_RIGHT, apply_grp_controls, 0,
      "Volume keys, the interface the Disc starts in, and the language.", NULL },
    { "System", "Maintenance", ST_ACTION, NULL, 0,0,0, NULL,0, LV_SYMBOL_RIGHT, apply_grp_maintenance, 0,
      "Update from the SD card, restart, reset settings and debug tools.", NULL },
    { "System", "About", ST_ACTION, NULL, 0,0,0, NULL,0, LV_SYMBOL_RIGHT, apply_grp_about, 0,
      "Version, device information and temperature.", NULL },
    { "Power",   "Sleep Timer", ST_CYCLER, "sleep_idx", 0,0,0, OPT_SLEEP, 6, NULL, apply_sleep, 0,
      "Pause playback, or shut the device down (see When Sleep Ends), after this long. Resets on restart.", NULL },
    { "Power",   "When Sleep Ends", ST_CYCLER, "sleep_action", 0,0,0, OPT_SLEEP_ACT, 2, NULL, NULL, POWER_SLEEP_PAUSE,
      "What the Sleep Timer does. Shut down closes the card safely first, then powers the device off.", NULL },
    { "Power",   "Idle Power-off", ST_CYCLER, "idle_off_idx", 0,0,0, OPT_IDLE_OFF, POWER_IDLE_N, NULL, NULL, 0,
      "Power the device off after this long with nothing playing and no touch or key press. Closes the card safely first.", NULL },
    { "Power",   "Charging Limit", ST_TOGGLE, "charge_protect", 0,1,1, NULL, 0, NULL, apply_charge_protect, 0,
      "The player's Charging optimization: stops charging at about 80% to slow battery wear.", NULL },
    { "Power",   "Shut down player", ST_ACTION, NULL, 0,0,0, NULL,0, LV_SYMBOL_POWER, apply_shutdown, 0,
      "Switch the player off.", NULL },
    { "Date & Time",   "Automatic Time", ST_TOGGLE, "auto_time", 0,1,1, NULL, 0, NULL, apply_auto_time, 1,
      "Set the time and time zone from the internet whenever Wi-Fi connects. A change applies after the next restart.", NULL },
    { "Date & Time",   "Set Date & Time", ST_ACTION, NULL, 0,0,0, NULL,0, LV_SYMBOL_RIGHT, apply_open_datetime, 0,
      "Set the clock by hand. With Automatic Time on, Wi-Fi may correct it later.", NULL },
    { "Date & Time",   "Time Zone",   ST_CHOICE, "tz_idx", 0,0,0, TZ_LABEL, TZ_COUNT, NULL, apply_timezone, 0,
      "Automatic follows your location when Wi-Fi connects (with Automatic Time on). Pick a zone to set it yourself.", NULL },
    { "Library",   "Rescan Library", ST_ACTION, NULL, 0,0,0, NULL,0, "Scan", apply_rescan, 0,
      "Re-scan the SD card for new or removed music.", NULL },
    { "Library",   "Import Playlists", ST_ACTION, NULL, 0,0,0, NULL,0, "Import", apply_import_m3u, 0,
      "Import .m3u / .m3u8 playlists found on the SD card.", NULL },
    { "Controls",   "Volume Keys", ST_ACTION, NULL, 0,0,0, NULL,0, LV_SYMBOL_RIGHT, apply_volkeys_group, 0,
      "What the volume keys do on a press, a double press and when held: adjust the volume or switch track.", NULL },
    { "Controls",   "Default UI",  ST_CYCLER, "boot_default", 0,0,0, OPT_BOOTDEF, 2, NULL, apply_boot_default, 0,
      "Which UI boots by default. To boot the other one, hold Vol-Up from power-on until it appears.", NULL },
    { "Controls",   "Language",    ST_CHOICE, "language", 0,0,0, i18n_lang_names, LANG_COUNT, NULL, apply_language, LANG_EN,
      "Interface language. Song, artist and album names are always shown as they are tagged.", NULL },
    { "Maintenance",   "Update from SD Card", ST_ACTION, NULL, 0,0,0, NULL,0, LV_SYMBOL_RIGHT, system_update_sd_open, 0,
      "Install a signed diskOS update from the SD card folder diskos-update (made with your signing script). Otherwise shows how a stock update file is used.", NULL },
    /* Disco: no online updates (Update diskOS / Automatic Updates are gone); updates come from the SD card or a flash */
    { "Maintenance",   "Restart",     ST_ACTION, NULL, 0,0,0, NULL,0, "Restart", apply_restart, 0,
      "Restart the device. Boots your default UI; hold Vol-Up for the other one.", NULL },
    { "Maintenance",   "Reset diskOS Settings", ST_ACTION, NULL, 0,0,0, NULL,0, "Reset", system_reset_open, 0,
      "Put diskOS's look and behaviour settings back to defaults. Music, Wi-Fi, Bluetooth, Last.fm, EQ and audio settings are kept.", NULL },
    { "Maintenance",   "Debug Mode",  ST_ACTION, NULL, 0,0,0, NULL,0, LV_SYMBOL_RIGHT, apply_debug_mode, 0,
      "Enable temporary SSH over Wi-Fi (fresh random password) for debugging. Off by default.", NULL },
    { "About",   "About",       ST_READONLY, NULL, 0,0,0, NULL,0, "diskOS beta", NULL, 0,
      "diskOS - a custom music player UI.", NULL },
    { "About",   "Device Info", ST_ACTION, NULL, 0,0,0, NULL,0, LV_SYMBOL_RIGHT, system_about_open, 0,
      "Model, stock firmware, diskOS build, MAC addresses, storage and battery.", NULL },
    { "About",   "Temperature", ST_READONLY, NULL, 0,0,0, NULL,0, "@temp", NULL, 0,
      "Battery/board temperature from the fuel gauge (this SoC exposes no core sensor).", NULL },
    { "Volume Keys", "Press", ST_CYCLER, "key_single", 0,0,0, OPT_VOLKEY, 2, NULL, apply_key_single, CTL_ACT_VOLUME,
      "What the volume keys do on a single press: adjust the volume or switch track. Stored by the player, as in the stock menu.", NULL },
    { "Volume Keys", "Double Press", ST_CYCLER, "key_double", 0,0,0, OPT_VOLKEY, 2, NULL, apply_key_double, CTL_ACT_VOLUME,
      "What the volume keys do on a double press: adjust the volume or switch track.", NULL },
    { "Volume Keys", "Long Press", ST_CYCLER, "key_long", 0,0,0, OPT_VOLKEY, 2, NULL, apply_key_long, CTL_ACT_VOLUME,
      "What the volume keys do when held: adjust the volume or switch track.", NULL },
    { "Network",  "Wi-Fi",       ST_ACTION, NULL, 0,0,0, NULL,0, LV_SYMBOL_RIGHT, apply_wifi, 0,
      "Scan for and connect to Wi-Fi networks.", NULL },
    { "Network",  "MA Sendspin", ST_ACTION, NULL, 0,0,0, NULL,0, LV_SYMBOL_RIGHT, apply_ma_group, 0,
      "A Sendspin player for Music Assistant: the Disc shows up there as a speaker. Needs Wi-Fi; uses AirPlay while it is on.", NULL },
    { "MA Sendspin", "Sendspin", ST_TOGGLE, "ma_on", 0,1,1, NULL,0, NULL, apply_ma_on, 0,
      "A Sendspin player for Music Assistant. On: the Disc connects and waits for music, using AirPlay while it is on. Off goes back to Playback.", NULL },
    { "MA Sendspin", "Server", ST_ACTION, NULL, 0,0,0, NULL,0, g_ma_server_lbl, apply_ma_server, 0,
      "Auto finds Music Assistant on your network. Tap to type its address instead, for example 192.168.1.20 (port 8927 is added for you). Clear it for Auto.", NULL },
    { "MA Sendspin", "Player Name", ST_ACTION, NULL, 0,0,0, NULL,0, g_ma_name_lbl, apply_ma_name, 0,
      "How the Disc is called in Music Assistant.", NULL },
    { "MA Sendspin", "Sync Delay", ST_SLIDER, "ma_delay_ms", -2000,2000,10, NULL,0, NULL, NULL, 0,
      "Moves the Disc's sound later (+) or earlier (-), in milliseconds, to line it up with other speakers playing the same music.", NULL },
    { "Network",  "Bluetooth", ST_ACTION, NULL, 0,0,0, NULL,0, LV_SYMBOL_RIGHT, apply_bt, 0,
      "Pair Bluetooth devices. Audio routes to connected headphones or speakers (SBC, beta).", NULL },
    { "Network",  "Bluetooth Codec", ST_CHOICE, "bt_codec", 0,0,0, BT_CODEC_LABEL, BT_CODEC_N, NULL, NULL, 0,
      "Codec for Bluetooth headphones, as in the stock player. Applies the next time the headphones connect. If they lack the codec, SBC is used. AAC and LDAC are heavier for this player and may stutter.", NULL },
};
#define N_SETTINGS ((int)(sizeof(TABLE)/sizeof(TABLE[0])))

static int  get_val(const setting_t *s){ return s->cfg_key ? cfg_get_int(s->cfg_key, s->def) : 0; }
static void set_val(const setting_t *s, int v){
    /* EQ persists itself only on a successful send (apply_eq -> ui_eq_select), so a failed send never
     * leaves a stale eq_preset; every other setting persists up front here. */
    g_set_prev = get_val(s);
    if(s->cfg_key && strcmp(s->cfg_key, "eq_preset")) cfg_set_int(s->cfg_key, v);
    if(s->apply) s->apply(v);
}
/* battery/board temp from the fuel gauge; power-supply ABI = tenths of a degree C */
/* "1.2.0 (8c29ecbf)": the release plus the first 8 hex of the RUNNING binary's md5 - the build id used in every
 * test record, and true even for a hand-deployed build. Hashed once (the binary is ~2.5 MB), then cached. */
static void read_about(char *buf, int n){
    static char id[9];
    if(!id[0]){
        snprintf(id, sizeof id, "?");
        int fd = open("/proc/self/exe", O_RDONLY | O_CLOEXEC);
        struct stat st;
        if(fd >= 0 && fstat(fd, &st) == 0 && st.st_size > 0 && st.st_size < 64*1024*1024){
            unsigned char *b = malloc((size_t)st.st_size);
            size_t got = 0;
            while(b && got < (size_t)st.st_size){
                ssize_t r = read(fd, b + got, (size_t)st.st_size - got);
                if(r < 0 && errno == EINTR) continue;
                if(r <= 0) break;
                got += (size_t)r;
            }
            if(b && got == (size_t)st.st_size){ char hex[33]; md5_hex(b, got, hex); snprintf(id, sizeof id, "%.8s", hex); }
            free(b);
        }
        if(fd >= 0) close(fd);
    }
    snprintf(buf, n, "Disco! %s (%s)", DISCO_VERSION, id);   /* "Disco! 0.9 (8c29ecbf)" - on diskOS DISKOS_VERSION */
}
void settings_diskos_id(char *buf, int n){ read_about(buf, n); }   /* system.c About */
void settings_restart_prompt(const char *note){ g_restart_note = note; apply_restart(0); }
static void read_batt_temp(char *buf, int n){
    FILE *f = fopen("/sys/class/power_supply/cw221X-bat/temp", "r");
    int t;
    if(f && fscanf(f, "%d", &t) == 1){ fclose(f);
        unsigned at = t<0 ? 0u-(unsigned)t : (unsigned)t;   /* unsigned negate: INT_MIN-safe; sign explicit */
        snprintf(buf, n, "%s%u.%u\xC2\xB0""C", t<0?"-":"", at/10, at%10);   /* 305->"30.5°C", -5->"-0.5°C" */
    } else { if(f) fclose(f); snprintf(buf, n, "--"); }
}
/* Format a slider value WITH its unit - shared by val_text (committed value) and the live drag
 * callback (uncommitted value) so brightness shows "%" and swipe-thresh "px" while dragging too. */
static void fmt_slider(const setting_t *s, int v, char *buf, int n){
    if(s->cfg_key && !strcmp(s->cfg_key, "swipe_thresh"))    snprintf(buf,n, "%d px", v);
    else if(s->cfg_key && !strcmp(s->cfg_key, "brightness")) snprintf(buf,n, "%d%%", v*100/40);  /* 4..40 -> 10..100% */
    else if(s->cfg_key && !strcmp(s->cfg_key, "ma_delay_ms")) snprintf(buf,n, v ? "%+d ms" : "0 ms", v);
    else                                                    snprintf(buf,n, "%d", v);
}
/* A setting the running firmware cannot apply (only Gain today: its player command is mapped per verified
 * firmware in fwcaps.c, and an unmapped firmware gets no command). The row stays visible so it is not a mystery,
 * but says so and is not editable - otherwise the menu would show a choice that silently does nothing. */
static int setting_unavailable(const setting_t *s){
    return s->cfg_key && ((!strcmp(s->cfg_key, "audio_gain") && fw_gain_tag() == NULL)
                       || (!strcmp(s->cfg_key, "artist_class") && !fw_artist_class_settable())
                       || (!strcmp(s->cfg_key, "folder_jump") && !fw_folder_jump_settable())
                       || (!strcmp(s->cfg_key, "bt_codec") && fw_os_ver() != 257)
                       || (!strcmp(s->cfg_key, "ota_allow") && !ota_supported(&OTA_DEV))
                       || ((!strcmp(s->cfg_key, "idle_off_idx") || !strcmp(s->cfg_key, "charge_protect")
                            || !strcmp(s->cfg_key, "sleep_action")) && !power_supported())
                       || ((!strcmp(s->cfg_key, CTL_CFG_ROT) || !strcmp(s->cfg_key, "key_single")
                            || !strcmp(s->cfg_key, "key_double") || !strcmp(s->cfg_key, "key_long")) && !ctl_supported()));
}
static const char *val_text_value(const setting_t *s, char *buf, int n){
    if(s->cfg_key && !strcmp(s->cfg_key, "bt_codec")) return LV_SYMBOL_RIGHT;   /* the codec's name doesn't fit beside the label: shown inside */
    if(setting_unavailable(s)) return tr(s->cfg_key && !strcmp(s->cfg_key, "ota_allow") ? "Not supported on this install" : "Not on this firmware");
    /* Outdoor Mode overrides these two while it is on: show what is really in effect, not the saved choice (which
     * stays untouched and comes back when Outdoor Mode is turned off) */
    if(theme_outdoor() && s->cfg_key && !strcmp(s->cfg_key, "theme_variant")) return "Light (Outdoor)";
    if(theme_auto_supported() && cfg_get_int("theme_auto",0) && s->cfg_key && !strcmp(s->cfg_key,"theme_variant"))
        return theme_variant() ? "Light (Auto)" : "Dark (Auto)";
    if(theme_outdoor() && s->cfg_key && !strcmp(s->cfg_key, "brightness")) return "Full (Outdoor)";
    int v = get_val(s);
    switch(s->type){
        case ST_TOGGLE:   return tr(v?"On":"Off");
        case ST_SLIDER:   fmt_slider(s, v, buf, n); break;
        case ST_CYCLER: case ST_CHOICE: return (v>=0&&v<s->nopts)?tr(s->opts[v]):"?";
        case ST_READONLY:
            if(s->ro_val && !strcmp(s->ro_val, "@temp")) read_batt_temp(buf, n);
            else if(s->ro_val && !strcmp(s->ro_val, "@about")) read_about(buf, n);
            else return tr(s->ro_val?s->ro_val:"");
            break;
        case ST_ACTION:   return tr((s->apply==apply_rescan && scanner_active()) ? "Stop" : (s->ro_val?s->ro_val:""));
    }
    return buf;
}

static const char *val_text(const setting_t *s,char *buf,int n){
    if(n<=0)return "";
    buf[0]=0;
    const char *v=val_text_value(s,buf,n);
    if(v!=buf)snprintf(buf,n,"%s",v?v:"");
    return buf;
}

/* ---- shared widgets ----------------------------------------------------- */
#define ACCENT UI_RED

static lv_obj_t *g_list_rows[N_SETTINGS];   /* value labels, to refresh in place */

/* Rescan Library row: title shows "Scanning... N songs" and the action word reads "Stop" ("Cancel" is cut off in the 96 px box) while the scanner thread
 * runs. The timer lives and dies with the row, so leaving the screen and coming back rebuilds it from the counter. */
typedef struct { lv_obj_t *lbl, *vl; lv_timer_t *tm; } rescan_row_t;
static void rescan_row_tick(lv_timer_t *t){
    rescan_row_t *rr = lv_timer_get_user_data(t);
    char b[40];
    if(scanner_active()){
        int done = 0; scanner_progress(&done, NULL);
        if(done >= 1000) snprintf(b, sizeof b, "Scanning... %d,%03d", done / 1000, done % 1000);
        else             snprintf(b, sizeof b, "Scanning... %d", done);
        lv_label_set_text(rr->lbl, b);
        lv_label_set_text(rr->vl, tr("Stop"));
    } else {
        lv_label_set_text(rr->lbl, tr("Rescan Library"));
        lv_label_set_text(rr->vl, tr("Scan"));
    }
}
static void rescan_row_del_cb(lv_event_t *e){
    rescan_row_t *rr = lv_event_get_user_data(e);
    lv_timer_delete(rr->tm);
    free(rr);
}
static const setting_t *g_active;           /* setting shown in the detail screen */
static lv_obj_t *g_detail_root;
static lv_obj_t *g_setlist_root;            /* SCR_SETLIST root: one category's rows, rebuilt per entry */
static const char *g_active_group;          /* which category SCR_SETLIST is currently showing */
static void apply_disco_options(int v){ (void)v; g_active_group = "Disco Options"; setlist_refresh(); }
/* Settings sub-groups (Display, Disco Options and System are split into short pages; back steps up one level) */
static void apply_grp_themecolours(int v){ (void)v; g_active_group = "Theme Colours"; setlist_refresh(); }
static void apply_grp_textlayout(int v){ (void)v; g_active_group = "Text & Layout"; setlist_refresh(); }
static void apply_grp_screenstandby(int v){ (void)v; g_active_group = "Standby"; setlist_refresh(); }
static void apply_grp_onlineextras(int v){ (void)v; g_active_group = "Online & Extras"; setlist_refresh(); }
static void apply_grp_musicscreen(int v){ (void)v; g_active_group = "Music Screen"; setlist_refresh(); }
static void apply_grp_discocolours(int v){ (void)v; g_active_group = "Disco Colours"; setlist_refresh(); }
static void apply_grp_discolayout(int v){ (void)v; g_active_group = "Disco Layout"; setlist_refresh(); }
static void apply_grp_power(int v){ (void)v; g_active_group = "Power"; setlist_refresh(); }
static void apply_grp_datetime(int v){ (void)v; g_active_group = "Date & Time"; setlist_refresh(); }
static void apply_grp_library(int v){ (void)v; g_active_group = "Library"; setlist_refresh(); }
static void apply_grp_controls(int v){ (void)v; g_active_group = "Controls"; setlist_refresh(); }
static void apply_grp_maintenance(int v){ (void)v; g_active_group = "Maintenance"; setlist_refresh(); }
static void apply_grp_about(int v){ (void)v; g_active_group = "About"; setlist_refresh(); }
static void apply_volkeys_group(int v){ (void)v; g_active_group = "Volume Keys"; setlist_refresh(); }
static void apply_ma_group(int v){ (void)v; ma_settings_load(); g_active_group = "MA Sendspin"; setlist_refresh(); }
void settings_open_ma(void){ apply_ma_group(0); }      /* host renders */
void settings_open_volkeys(void){ apply_volkeys_group(0); }   /* host renders */
void settings_open_ma_delay(void){ for(int i = 0; i < (int)(sizeof TABLE / sizeof TABLE[0]); i++) if(TABLE[i].cfg_key && !strcmp(TABLE[i].cfg_key, "ma_delay_ms")){ settings_open_detail(i); return; } }
/* screen_back() on SCR_SETLIST: a sub-group (Disco Options, Music Assistant) steps back to its parent on the same screen */
static const struct { const char *child, *parent; } SUBGROUP[] = {
    { "Theme Colours", "Display" },
    { "Text & Layout", "Display" },
    { "Standby", "Display" },
    { "Online & Extras", "Display" },
    { "Music Screen", "Disco Options" },
    { "Disco Colours", "Disco Options" },
    { "Disco Layout", "Disco Options" },
    { "Power", "System" },
    { "Date & Time", "System" },
    { "Library", "System" },
    { "Controls", "System" },
    { "Maintenance", "System" },
    { "About", "System" },
    { "Disco Options", "Display" },
    { "MA Sendspin", "Network" },
    { "Volume Keys", "Controls" } };
int settings_back_consumed(void){
    if(!g_active_group) return 0;
    for(size_t i = 0; i < sizeof SUBGROUP / sizeof SUBGROUP[0]; i++)
        if(!strcmp(g_active_group, SUBGROUP[i].child)){ g_active_group = SUBGROUP[i].parent; setlist_refresh(); return 1; }
    return 0;
}
/* Category order for the top-level Settings screen (must match the group strings used in TABLE). */
static const char *const GROUPS[] = { "Playback", "Audio", "Display", "Network", "System" };
#define N_GROUPS ((int)(sizeof(GROUPS)/sizeof(GROUPS[0])))



/* ---- detail screen ------------------------------------------------------ */
static void detail_back_cb(lv_event_t *e){ if(lv_event_get_code(e)==LV_EVENT_CLICKED) screen_back(); }

/* VALUE_CHANGED: apply live only (no flash write); RELEASED persists once. This
 * avoids rewriting the whole config file on every pixel of a slider drag. */
static void detail_slider_cb(lv_event_t *e){
    lv_obj_t *sl = lv_event_get_target(e);
    int v = lv_slider_get_value(sl);
    if(g_active->apply) g_active->apply(v);
    lv_obj_t *vl = lv_event_get_user_data(e);
    if(vl){ char b[16]; fmt_slider(g_active, v, b, sizeof b); lv_label_set_text(vl, b); }   /* keep %/px unit while dragging */
}
static int outdoor_holds(const setting_t *s){ return theme_outdoor() && s->cfg_key && !strcmp(s->cfg_key, "brightness"); }
static void detail_slider_release_cb(lv_event_t *e){
    lv_obj_t *sl = lv_event_get_target(e);
    if(outdoor_holds(g_active)) return;                 /* Outdoor keeps the user's own level untouched */
    if(g_active->cfg_key) cfg_set_int(g_active->cfg_key, lv_slider_get_value(sl));
    if(g_active->cfg_key && !strcmp(g_active->cfg_key, "ma_delay_ms")) ma_delay_changed();   /* restart with the saved value */
}
static void detail_cycle_cb(lv_event_t *e){
    if(theme_outdoor() && g_active->cfg_key && !strcmp(g_active->cfg_key, "theme_variant")){
        ui_toast("Outdoor Mode keeps Light on");   /* a Dark/Light change could not show until Outdoor Mode is off */
        return;
    }
    if(theme_auto_supported() && cfg_get_int("theme_auto",0) && g_active->cfg_key && !strcmp(g_active->cfg_key,"theme_variant")){
        ui_toast("Turn Automatic Appearance off first"); return;
    }
    int dir = (int)(uintptr_t)lv_event_get_user_data(e);
    int v = get_val(g_active) + dir;
    if(v < 0) v = g_active->nopts-1;
    if(v >= g_active->nopts) v = 0;
    set_val(g_active, v);
    /* rebuild detail body to reflect new value */
    void setting_detail_refresh(void); setting_detail_refresh();
}
static void detail_toggle_cb(lv_event_t *e){
    lv_obj_t *sw = lv_event_get_target(e);
    set_val(g_active, lv_obj_has_state(sw, LV_STATE_CHECKED) ? 1 : 0);
}
static lv_obj_t *make_list(lv_obj_t *root);
static lv_obj_t *setting_card(lv_obj_t *list);
static void detail_choice_async(void *u){ (void)u; setting_detail_refresh(); }
static void detail_choice_cb(lv_event_t *e){
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if(!g_active || i == get_val(g_active)) return;
    set_val(g_active, i);                               /* a theme re-launches the UI from here */
    lv_async_call(detail_choice_async, NULL);           /* the tick moves after this event (rows can't delete themselves) */
}

void setting_detail_refresh(void){
    if(!g_detail_root || !g_active) return;
    lv_obj_clean(g_detail_root);
    lv_obj_set_style_bg_color(g_detail_root, TC(CANVAS), 0);
    lv_obj_set_style_bg_opa(g_detail_root, LV_OPA_COVER, 0);
    ui_header_cb(g_detail_root, g_active->label, detail_back_cb);   /* shared header */

    const setting_t *s = g_active;
    int v = get_val(s);

    if(s->type == ST_SLIDER){
        lv_obj_t *sl = lv_slider_create(g_detail_root);
        lv_obj_set_size(sl, 220, 12);
        lv_obj_set_ext_click_area(sl, 14);
        lv_obj_align(sl, LV_ALIGN_CENTER, 0, -6);
        lv_slider_set_range(sl, s->min, s->max);
        if(outdoor_holds(s)){ v = ui_effective_brightness(); lv_obj_add_state(sl, LV_STATE_DISABLED); }
        lv_slider_set_value(sl, v, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(sl, TC(SURFACE_RAISED), LV_PART_MAIN);
        lv_obj_set_style_bg_color(sl, TC(ACCENT_PRIMARY), LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(sl, TC(TEXT_PRIMARY), LV_PART_KNOB);
        lv_obj_t *vl = lv_label_create(g_detail_root);
        char b[16]; lv_label_set_text(vl, val_text(s,b,sizeof b));
        lv_obj_set_width(vl, 360); lv_obj_align(vl, LV_ALIGN_CENTER, 0, 40);
        lv_obj_set_style_text_align(vl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(vl, ui_current_accent(), 0);
        lv_obj_set_style_text_font(vl, TF(UI_20), 0);
        ui_on(sl, detail_slider_cb, LV_EVENT_VALUE_CHANGED, vl, "settings.detail_slider.value", UI_CORE);
        lv_obj_add_event_cb(sl, detail_slider_release_cb, LV_EVENT_RELEASED, NULL);
        lv_obj_add_event_cb(sl, detail_slider_release_cb, LV_EVENT_PRESS_LOST, NULL);
    } else if(s->type == ST_TOGGLE){
        lv_obj_t *sw = lv_switch_create(g_detail_root);
        lv_obj_align(sw, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_bg_color(sw, ui_current_accent(), LV_PART_INDICATOR | LV_STATE_CHECKED);
        if(v) lv_obj_add_state(sw, LV_STATE_CHECKED);
        ui_on(sw, detail_toggle_cb, LV_EVENT_VALUE_CHANGED, NULL, "settings.detail_toggle.value", UI_CORE);
    } else if(s->type == ST_CHOICE){
        /* every option as a row; the current one ticked. Themes also show three colour dots (background, surface,
         * accent) in the current Dark/Light variant, so each can be judged before picking. */
        lv_obj_t *list = make_list(g_detail_root);
        if(s->opts == i18n_lang_names) lv_obj_set_height(list, 215);
        int is_theme = !strcmp(s->cfg_key, "theme_preset");
        /* Themes: our own first (Ring, Braun), then Stone; the other presets stay in the code and keep working if
         * already selected, but are not offered here for now. Rows still carry the real preset index. */
        int THEME_SHOWN[4] = { THEME_PRESET_RING, THEME_PRESET_BRAUN, THEME_PRESET_DISCO, -1 }, nshown = 3;
        for(int k = 0; k < THEME_PRESET_COUNT; k++) if(!strcmp(theme_preset_names[k], "stone")){ THEME_SHOWN[nshown++] = k; break; }
        int nrows = is_theme ? nshown : s->nopts;
        for(int r = 0; r < nrows; r++){
            int i = is_theme ? THEME_SHOWN[r] : r;
            lv_obj_t *row = setting_card(list);
            int cur = (i == v);
            if(cur) lv_obj_set_style_bg_color(row, TC(SURFACE_SELECTED), 0);
            ui_on(row, detail_choice_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i, "settings.detail_choice", UI_CORE);
            int x = 18;
            if(is_theme){
                static const theme_color_role_t DOTS[3] = { THEME_CLR_CANVAS, THEME_CLR_SURFACE_RAISED, THEME_CLR_ACCENT_PRIMARY };
                for(int k = 0; k < 3; k++){
                    lv_obj_t *dot = lv_obj_create(row);
                    lv_obj_remove_style_all(dot);
                    lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);
                    lv_obj_set_size(dot, 16, 16);
                    lv_obj_align(dot, LV_ALIGN_LEFT_MID, x + k * 12, 0);
                    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
                    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
                    lv_obj_set_style_bg_color(dot, theme_preset_color(i, DOTS[k]), 0);
                    lv_obj_set_style_border_width(dot, 1, 0);
                    lv_obj_set_style_border_color(dot, TC(BORDER_STRONG), 0);
                }
                x += 3 * 12 + 14;
            }
            lv_obj_t *lbl = lv_label_create(row);
            if(is_theme){ char nm[32]; snprintf(nm, sizeof nm, "%s", tr(s->opts[i]));      /* "stone" -> "Stone" */
                          if(nm[0] >= 'a' && nm[0] <= 'z') nm[0] -= 'a' - 'A'; lv_label_set_text(lbl, nm); }
            else lv_label_set_text(lbl, tr(s->opts[i]));
            lv_obj_align(lbl, LV_ALIGN_LEFT_MID, x, 0);
            lv_obj_set_style_text_font(lbl, s->opts == i18n_lang_names ? TF(USER_16) : TF(UI_16), 0);   /* native names: any script */
            lv_obj_set_style_text_color(lbl, TC(TEXT_PRIMARY), 0);
            if(cur){
                lv_obj_t *ck = lv_label_create(row);
                lv_label_set_text(ck, LV_SYMBOL_OK);
                lv_obj_align(ck, LV_ALIGN_RIGHT_MID, -16, 0);
                lv_obj_set_style_text_color(ck, ui_current_accent(), 0);
            }
            theme_list_row(row);
        }
    } else if(s->type == ST_CYCLER){
        /* < value > stepper */
        lv_obj_t *vl = lv_label_create(g_detail_root);
        char b[24]; lv_label_set_text(vl, val_text(s,b,sizeof b));
        lv_obj_set_size(vl, 220, 32); lv_label_set_long_mode(vl, LV_LABEL_LONG_DOT);
        lv_obj_align(vl, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_text_align(vl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(vl, ui_current_accent(), 0);
        lv_obj_set_style_text_font(vl, TF(UI_24), 0);
        lv_obj_t *lb = lv_button_create(g_detail_root);
        lv_obj_remove_style_all(lb); lv_obj_set_size(lb, 48, 48);
        lv_obj_align(lb, LV_ALIGN_CENTER, -110, 0);
        lv_obj_t *li = lv_label_create(lb); lv_label_set_text(li, LV_SYMBOL_LEFT);
        lv_obj_set_style_text_color(li, TC(TEXT_PRIMARY), 0); lv_obj_center(li);
        lv_obj_add_event_cb(lb, detail_cycle_cb, LV_EVENT_CLICKED, (void*)(uintptr_t)-1);
        lv_obj_t *rb = lv_button_create(g_detail_root);
        lv_obj_remove_style_all(rb); lv_obj_set_size(rb, 48, 48);
        lv_obj_align(rb, LV_ALIGN_CENTER, 110, 0);
        lv_obj_t *ri = lv_label_create(rb); lv_label_set_text(ri, LV_SYMBOL_RIGHT);
        lv_obj_set_style_text_color(ri, TC(TEXT_PRIMARY), 0); lv_obj_center(ri);
        lv_obj_add_event_cb(rb, detail_cycle_cb, LV_EVENT_CLICKED, (void*)(uintptr_t)1);
        if(s->cfg_key && !strcmp(s->cfg_key, "disco_side_pick")){       /* Disco Layout > Side: Apply under the choice */
            lv_obj_t *ab = lv_button_create(g_detail_root); lv_obj_remove_style_all(ab);
            lv_obj_set_size(ab, 132, 46); lv_obj_align(ab, LV_ALIGN_CENTER, 0, 58);
            lv_obj_set_style_radius(ab, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(ab, ui_current_accent(), 0); lv_obj_set_style_bg_opa(ab, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_opa(ab, LV_OPA_70, LV_STATE_PRESSED);
            lv_obj_add_flag(ab, LV_OBJ_FLAG_USER_2);                        /* the kit leaves its colours alone */
            lv_obj_t *al = lv_label_create(ab); lv_label_set_text(al, "Apply"); lv_obj_center(al);
            lv_obj_set_style_text_font(al, TF(UI_20), 0); lv_obj_set_style_text_color(al, theme_on_color(ui_current_accent()), 0);
            lv_obj_add_event_cb(ab, side_apply_cb, LV_EVENT_CLICKED, NULL);
        }
    } else { /* readonly */
        lv_obj_t *vl = lv_label_create(g_detail_root);
        char b[128];lv_label_set_text(vl,val_text(s,b,sizeof b));
        lv_obj_set_width(vl, 360); lv_obj_align(vl, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_text_align(vl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(vl, TC(TEXT_LYRICS), 0);
        lv_obj_set_style_text_font(vl, TF(UI_20), 0);
    }

    /* description at the bottom - per-option for cyclers (refreshes on change) */
    const char *desc = s->desc;
    if(s->type==ST_CYCLER && s->opt_descs && v>=0 && v<s->nopts) desc = s->opt_descs[v];
    if(s->type == ST_CHOICE) desc = (s->opts == i18n_lang_names) ? "Screen redraws after selection."
                                  : (s->cfg_key && !strcmp(s->cfg_key, "bt_codec")) ? "Best effort: SBC if the headphones lack it." : NULL;
    if(desc && desc[0]){
        lv_obj_t *d = lv_label_create(g_detail_root);
        lv_label_set_text(d, tr(desc));
        lv_label_set_long_mode(d, s->type == ST_CHOICE ? LV_LABEL_LONG_DOT : LV_LABEL_LONG_WRAP);
        lv_obj_set_width(d, 240);
        if(s->type == ST_CHOICE) lv_obj_set_height(d, 22);
        lv_obj_align(d, LV_ALIGN_BOTTOM_MID, 0, -40);
        lv_obj_set_style_text_align(d, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(d, TF(UI_14), 0);
        lv_obj_set_style_text_color(d, TC(TEXT_MUTED), 0);
    }
}

void setting_detail_create(lv_obj_t *root){ g_detail_root = root; }

/* open a specific setting's detail page directly (verification / deep-link) */
void settings_open_detail(int idx){
    if(idx<0 || idx>=N_SETTINGS) return;
    g_active = &TABLE[idx];
    screen_show(SCR_SETTING_DETAIL);
    setting_detail_refresh();
}

/* open a setting's detail page by its cfg key (robust to TABLE reordering). No-op if not found. */
void settings_open_key(const char *key){
    if(!key) return;
    for(int i=0;i<N_SETTINGS;i++)
        if(TABLE[i].cfg_key && !strcmp(TABLE[i].cfg_key, key)){
            if(setting_unavailable(&TABLE[i])){ ui_toast("Not supported on this firmware yet"); return; }
            settings_open_detail(i); return;
        }
}

/* ---- master list -------------------------------------------------------- */
static void list_back_cb(lv_event_t *e){ if(lv_event_get_code(e)==LV_EVENT_CLICKED){ if(settings_back_consumed()) return; screen_back(); } }

static void row_cb(lv_event_t *e){
    if(lv_event_get_code(e)!=LV_EVENT_CLICKED) return;
    int idx = (int)(uintptr_t)lv_event_get_user_data(e);
    const setting_t *s = &TABLE[idx];
    if(s->type == ST_TOGGLE){
        if(setting_unavailable(s)){ ui_toast(tr("Not supported on this install")); return; }   /* only Allow diskOS Updates is a toggle that can be unavailable */
        int nv = !get_val(s); set_val(s, nv);
        char b[16];
        if(g_list_rows[idx]) lv_label_set_text(g_list_rows[idx], val_text(s,b,sizeof b));
        return;
    }
    if(s->type == ST_ACTION){
        /* Actions provide their own completion feedback via ui_toast (in-place
         * actions like Rescan/Import) or by navigating to a screen (Wi-Fi/BT).
         * No permanent "Working..." label (it never resolved). */
        if(s->apply) s->apply(0);
        return;
    }
    if(s->type == ST_READONLY) return;   /* info rows (Temperature/About) aren't tappable */
    if(setting_unavailable(s)){ ui_toast("Not supported on this firmware yet"); return; }
    g_active = s;
    screen_show(SCR_SETTING_DETAIL);
    setting_detail_refresh();
}

/* the scrolling column that holds setting rows, positioned in the round screen's safe area */
static lv_obj_t *make_list(lv_obj_t *root){
    lv_obj_t *list = lv_obj_create(root);
    lv_obj_remove_style_all(list);
    lv_obj_set_pos(list, 30, 70);
    lv_obj_set_size(list, 300, 250);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_row(list, 8, 0);       /* small gap between cards (matches the design) */
    lv_obj_set_style_pad_bottom(list, 44, 0);   /* round bottom bezel: last row must scroll fully clear */
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);   /* rows on the centre line */
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    return list;
}

/* one row card: a plain dark rounded pill (no accent bar - kept clean). */
static lv_obj_t *setting_card(lv_obj_t *list){
    lv_obj_t *row = lv_button_create(list);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, 288, TH_ROW_H);
    lv_obj_set_style_radius(row, th_disco() ? LV_RADIUS_CIRCLE : TH_R_ROW, 0);   /* Disco: rounded line containers */
    lv_obj_set_style_bg_color(row, TC(SURFACE), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_70, 0);
    lv_obj_set_style_bg_color(row, TC(SURFACE_RAISED), LV_STATE_PRESSED);
    lv_obj_add_flag(row, LV_OBJ_FLAG_USER_1);                         /* curves with the circle */
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    return row;
}

/* ---- top-level category list (SCR_SETTINGS): Playback / Audio / Display / Network / System ---- */
__attribute__((unused)) static void cat_cb(lv_event_t *e){
    if(lv_event_get_code(e)!=LV_EVENT_CLICKED) return;
    int gi = (int)(uintptr_t)lv_event_get_user_data(e);
    if(gi<0 || gi>=N_GROUPS) return;
    g_active_group = GROUPS[gi];
    screen_show(SCR_SETLIST);   /* transition() rebuilds the rows via setlist_refresh */
}

static orbit_t g_set_orb;
static void cat_pick(int gi){
    if(gi == N_GROUPS){ screen_show(SCR_USAGE); return; }   /* Battery */
    if(gi < 0 || gi >= N_GROUPS) return; g_active_group = GROUPS[gi]; screen_show(SCR_SETLIST); }
void setlist_open(const char *group){
 if(!group)return;
 for(int i=0;i<N_GROUPS;i++)if(!strcmp(group,GROUPS[i])){g_active_group=GROUPS[i];screen_show(SCR_SETLIST);return;}
}
static void hub_cb(lv_event_t *e){ list_back_cb(e); }
/* Disco: the categories as a curved list of rounded rows (as Quick Settings), clear of the navigation sliver */
static void disco_cat_cb(lv_event_t *e){ if(lv_event_get_code(e) == LV_EVENT_CLICKED) cat_pick((int)(intptr_t)lv_event_get_user_data(e)); }
static void settings_disco_list(lv_obj_t *root){          /* styled like the submenus: curved rows, 4 in view, scroll dots */
    lv_obj_t *t = lv_label_create(root); lv_label_set_text(t, "Settings");
    lv_obj_set_style_text_font(t, TF(UI_20), 0); lv_obj_set_style_text_color(t, TC(TEXT_PRIMARY), 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 28);
    static const char *GICON[] = { LV_SYMBOL_PLAY, LV_SYMBOL_AUDIO, LV_SYMBOL_IMAGE, LV_SYMBOL_WIFI, LV_SYMBOL_SETTINGS, LV_SYMBOL_BATTERY_3 };
    lv_obj_t *list = make_list(root);
    static curvelist_t cl; curvelist_attach(&cl, list, root, 288);
    for(int g = 0; g <= N_GROUPS; g++){
        lv_obj_t *r = setting_card(list);
        lv_obj_add_event_cb(r, disco_cat_cb, LV_EVENT_CLICKED, (void *)(intptr_t)g);
        lv_obj_t *ic = lv_label_create(r); lv_label_set_text(ic, GICON[g]); lv_obj_set_style_text_font(ic, TH_F_LIST, 0);
        lv_obj_set_style_text_color(ic, ui_current_accent(), 0); lv_obj_set_pos(ic, 18, 16);
        lv_obj_t *n = lv_label_create(r); lv_label_set_text(n, g < N_GROUPS ? GROUPS[g] : "Battery");
        lv_obj_set_style_text_font(n, TH_F_LIST, 0); lv_obj_set_style_text_color(n, TC(TEXT_PRIMARY), 0);
        lv_obj_set_pos(n, 50, 16);
        lv_obj_t *c = lv_label_create(r); lv_label_set_text(c, LV_SYMBOL_RIGHT); lv_obj_set_style_text_font(c, TH_F_DETAIL, 0);
        lv_obj_set_style_text_color(c, TC(TEXT_SECONDARY), 0); lv_obj_set_size(c, 120, 22); lv_obj_set_pos(c, 150, 18);
        lv_obj_set_style_text_align(c, LV_TEXT_ALIGN_RIGHT, 0);
        theme_list_row(r);
    }
    curvelist_update(&cl);
}
void settings_create(lv_obj_t *root){
    lv_obj_set_style_bg_color(root, TC(CANVAS), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    if(th_disco()){ settings_disco_list(root); return; }
    /* orbit: the five categories circle a grey-ringed hub; one sits at the bottom, the top holds the title */
    orbit_title(root, "Settings");
    static const char *GICON[N_GROUPS] = { LV_SYMBOL_PLAY, LV_SYMBOL_AUDIO, LV_SYMBOL_IMAGE,
                                           LV_SYMBOL_WIFI, LV_SYMBOL_SETTINGS };
    orbit_item_t it[N_GROUPS + 1];                          /* + Battery: the usage dial */
    for(int g = 0; g < N_GROUPS; g++){ it[g].glyph = GICON[g]; it[g].cap = GROUPS[g]; }
    it[N_GROUPS].glyph = LV_SYMBOL_BATTERY_3; it[N_GROUPS].cap = "Battery";
    orbit_create(&g_set_orb, root, it, N_GROUPS + 1, -60, cat_pick);
    for(int g = 0; g <= N_GROUPS; g++)                                  /* category icons in the accent */
        lv_obj_set_style_text_color(g_set_orb.icon[g], TC(ACCENT_PRIMARY), 0);
    orbit_hub_create(&g_set_orb, root, hub_cb, LV_SYMBOL_SETTINGS, "Back");
    if(th_braun()){ br_face(root); orbit_braun_icons(&g_set_orb); }   /* Braun: the grille face; knob icons white */
}

/* Category list is static; nothing to re-sync when SCR_SETTINGS is (re)shown. */
void settings_refresh_list(void){ }
void settings_row_values_refresh(void){
    char b[40];
    for(int i=0;i<N_SETTINGS;i++) if(g_list_rows[i] && lv_obj_is_valid(g_list_rows[i])) lv_label_set_text(g_list_rows[i], val_text(&TABLE[i],b,sizeof b));
}

/* ---- one category's rows (SCR_SETLIST), rebuilt on every entry so values are always live ---- */
void setlist_create(lv_obj_t *root){ g_setlist_root = root; }

void setlist_refresh(void){
    if(!g_setlist_root || !g_active_group) return;
    lv_obj_clean(g_setlist_root);
    lv_obj_set_style_bg_color(g_setlist_root, TC(CANVAS), 0);
    lv_obj_set_style_bg_opa(g_setlist_root, LV_OPA_COVER, 0);
    ui_header_cb(g_setlist_root, g_active_group, list_back_cb);   /* title = category; back pops to categories */

    lv_obj_t *list = make_list(g_setlist_root);
    static curvelist_t cl; curvelist_attach(&cl, list, g_setlist_root, 288);   /* theme: every list curves + dots */
    for(int i=0;i<N_SETTINGS;i++){
        const setting_t *s = &TABLE[i];
        if(strcmp(s->group, g_active_group)) continue;   /* only rows in this category */
        if(th_disco() && (s->apply == apply_np_style || !strcmp(s->label, "Disc Colour") || !strcmp(s->label, "Saver Style"))) continue;   /* Disco has its own Music + standby: these don't apply */
        if(!th_disco() && s->apply == apply_disco_options) continue;
        if(!th_disco() && (s->apply == apply_disco_menu || s->apply == apply_disco_clock || s->apply == apply_disco_progress || s->apply == apply_disco_sheen || s->apply == apply_disco_times || s->apply == apply_disco_eq || s->apply == apply_disco_title || s->apply == apply_disco_hold || s->apply == apply_disco_imm)) continue;   /* fork: Disco rows */

        lv_obj_t *row = setting_card(list);
        ui_on(row, row_cb, LV_EVENT_CLICKED, (void*)(uintptr_t)i, "settings.row", UI_CORE);

        lv_obj_t *lbl = lv_label_create(row);
        lv_label_set_text(lbl, s->label);
        lv_obj_set_pos(lbl, 18, 16);
        lv_obj_set_style_text_font(lbl, TH_F_LIST, 0);
        lv_obj_set_style_text_color(lbl, TC(TEXT_PRIMARY), 0);

        char b[24]; val_text(s, b, sizeof b);
        lv_obj_t *vl = lv_label_create(row);
        lv_label_set_text(vl, b);
        lv_obj_set_pos(vl, 150, 18);
        lv_obj_set_size(vl, 120, 22);
        lv_obj_set_style_text_align(vl, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_style_text_font(vl, TH_F_DETAIL, 0);
        lv_obj_set_style_text_color(vl, TC(TEXT_SECONDARY), 0);
        lv_obj_set_style_text_color(vl, s->type==ST_READONLY? TC(TEXT_MUTED) : TC(TEXT_LYRICS), 0);
        g_list_rows[i] = vl;
        if(s->apply == apply_rescan){                  /* live scan progress on this row while a scan runs */
            rescan_row_t *rr = malloc(sizeof *rr);
            if(rr){
                rr->lbl = lbl; rr->vl = vl;
                rr->tm = lv_timer_create(rescan_row_tick, 400, rr);
                lv_obj_add_event_cb(row, rescan_row_del_cb, LV_EVENT_DELETE, rr);
                rescan_row_tick(rr->tm);
            }
        }
        theme_list_row(row);
    }
    curvelist_update(&cl);
}
void settings_open_disco(void){ apply_disco_options(0); }   /* host renders */
void settings_open_group(const char *g){ for(size_t i = 0; i < sizeof SUBGROUP / sizeof SUBGROUP[0]; i++) if(!strcmp(g, SUBGROUP[i].child)){ g_active_group = SUBGROUP[i].child; setlist_refresh(); return; } }   /* host renders */
void settings_test_scroll(int y){                                     /* host renders: scroll the rows by y px */
    if(!g_setlist_root) return;
    lv_obj_t *st[64]; int n = 0; st[n++] = g_setlist_root;
    while(n){ lv_obj_t *c = st[--n];
        if(lv_obj_get_scroll_bottom(c) > 0){ lv_obj_scroll_to_y(c, y, LV_ANIM_OFF); return; }
        for(uint32_t i = 0; i < lv_obj_get_child_count(c) && n < 64; i++) st[n++] = lv_obj_get_child(c, i); } }
