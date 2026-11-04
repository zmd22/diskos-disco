/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef SCREENS_H
#define SCREENS_H
#include "lvgl/lvgl.h"
#include "ipc.h"

/* diskOS red: the single UI accent (switches, sliders, "on" states, Settings row bars, default
 * accent colour). Ported from our fork's theme. */
#define UI_RED 0xE4122C

/* synced lyrics for the immersive view: one line per timestamp, sorted by time */
typedef struct { long ms; char text[120]; } lyr_line_t;
int lyrics_timed_load(const char *path, lyr_line_t *out, int cap);
int lyrics_immersive_load(lyr_line_t *out,int cap,unsigned *revision); /* main thread, asynchronous sources */
void lyrics_invalidate_current(void);
void npmenu_refresh_art(void);       /* NP menu shown: cover + wash */
void usage_create(lv_obj_t *root);   /* Settings > Battery: the 24-hour usage dial */
void usage_refresh(void);
void usage_tick(int screen_on, int playing);        /* main loop: one sample a minute */
void usage_note_battery(int pct, int charging);     /* status poll */
int  usage_charging(void);                           /* battery charging now (charge current > 0) */
int  usage_plugged_in(void);                         /* 1 on external power, 0 not, -1 unknown */
void ui_shutdown_screen(void);   /* the themed "Shutting down" screen (top layer) */
void ui_shutdown_hide(void);
void ui_power_off(void);         /* shutdown screen, save, sync, poweroff */
void usage_save(void);                              /* before a power-off */
long queue_pid(int create);          /* the "Queue" playlist */
int  queue_count(void);
int  queue_add_ids(const int *ids, int n);   /* exact songs, saved once; -1 if they don't fit, -2 database busy (nothing added), else added */
int  queue_add_path(const char *path);   /* 1 added, 0 already there, -1 failed */
int  queue_add_group(const char *col, const char *val);
int  queue_add_folder(const char *dir);
void queue_clear(void);
void queue_touched(void);
void queue_open(void);
void queue_tick(const track_state_t *st, int playing);
void queue_note_external_play(void);
int  queue_next(void);
void queue_note_prev(void);                  /* the Previous button was pressed */                       /* Next with songs queued: play the first of them (1 = handled) */        /* the user started something: the queue re-anchors after it */
int  queue_remove_at(int i); int queue_move(int from, int to); void queue_shuffle(void); void queue_jump(int i);
void queue_path_moved(const char *oldp, const char *newp); void queue_path_removed(const char *p);
void queue_create(lv_obj_t *root);
/* stored in cfg disco_nav_double / disco_nav_hold; 1.4.1 added four before Nothing (older saves of 6 = Nothing are
 * moved to the new value once, see disco_nav_migrate) */
enum { DISCO_NAV_MUSIC, DISCO_NAV_SEARCH, DISCO_NAV_QUEUE, DISCO_NAV_UPNEXT, DISCO_NAV_SHORTCUTS, DISCO_NAV_QUICK,
       DISCO_NAV_VOLUME, DISCO_NAV_SCREENOFF, DISCO_NAV_PLAYPAUSE, DISCO_NAV_NEXT, DISCO_NAV_NONE, DISCO_NAV_SHUFFLE, DISCO_NAV_COUNT };
/* Append new actions: existing saved action numbers must not change. */
int library_shuffle_all(void);
void disco_nav_migrate(void);
void disco_nav_action_run(int action);
int disco_track_overlay_visible(void);
void dhome_overlay_close(void);
void shortcuts_config_create(lv_obj_t *root); void shortcuts_config_refresh(void); void shortcut_run(const char *key); void queue_refresh(void);
int ui_play_slot(int pos);
int ui_play_restore(int type, const char *name, long pid, int pos);
int  ui_play_context(int *type, char *name, int cap, long *pid);
long ui_playing_playlist(void);           /* the custom playlist being played, 0 = none */
void ui_queue_changed(void);              /* the Queue's size changed: refresh the NP badge */
void songmenu_open_song(const char *path);   /* Library long-press on a song */
void songmenu_open_file(const char *dir, const char *name, void (*done)(void));   /* folder browser: a song file */
void songmenu_open_folder(const char *dir, const char *name, void (*done)(void)); /* folder browser: a folder */
void songmenu_open_group(const char *col, const char *val, const char *title, void (*play)(int shuffle));   /* Library: album / artist / genre (fork) */
void songmenu_offer_select(void (*fn)(void));   /* Zune: the next hold menu offers "select" (start picking) */
void fileops_action(const char *dir, const char *name, int is_dir, void (*done)(void), int action);  /* 0 rename, 1 copy/move, 2 delete */
void songinfo_show_song(const track_state_t *st);
void songinfo_unpin(void);
void plpick_set_folder(const char *dir);
void tagfix_song(const char *path, const char *title, const char *artist, const char *album, long dur_ms);
void tagfix_folder(const char *dir);
void ui_toast_icon(const char *icon, lv_color_t icol, const char *msg);   /* toast with its own icon */
void ui_scan_orbit(int on);            /* the rescan dot orbiting the rim */
void ui_invalidate_play_scope(void);
void ui_restart(void);
int  th_want_dark(void);   /* Braun: the variant the settings and the clock ask for */
int  th_night_now(void);                       /* re-exec the UI (theme change) */
void fileops_confirm(const char *title, const char *detail, const char *yes, void (*on_yes)(void));   /* themed Cancel / <yes> dialog on the top layer */
void fileops_open(const char *dir, const char *name, int is_dir, void (*done)(void));  /* folder browser long-press */
void tagfix_current_track(void);      /* NP menu: add synced lyrics + artwork the current track is missing */
void tagfix_album(const char *album);  /* Library album hold menu: tag every track of that album */
void tagfix_current_album(void);      /* NP menu: the same for every track of its album */
void tagfix_auto_tick(const track_state_t *st, int playing);   /* Auto-tag (Settings > Playback) */
void ui_np_tags_changed(void);        /* tags were rewritten: lyrics views reload */
#include <stdbool.h>
enum { SCR_HOME, SCR_LIBRARY, SCR_NOWPLAYING, SCR_SETTINGS, SCR_SETTING_DETAIL, SCR_SEARCH, SCR_SAVER, SCR_QUICK, SCR_SONGINFO, SCR_NPMENU, SCR_TUNE, SCR_EQ, SCR_APPS, SCR_NPHUB, SCR_PLPICK, SCR_PLVIEW, SCR_WIFI, SCR_WIFI_INFO, SCR_BT, SCR_BT_INFO, SCR_WEATHER, SCR_LYRICS, SCR_COLORPICK, SCR_LASTFM, SCR_WORKMODE, SCR_DEBUG, SCR_FOLDER, SCR_BOOKS, SCR_CHAPTERS, SCR_SETLIST, SCR_QSCONFIG, SCR_ALBUMWALL, SCR_USAGE, SCR_QUEUE, SCR_SCCONFIG, SCR_MODEINFO, SCR_UPNEXT, SCR_DATETIME, SCR_EQ_EDITOR, SCR_MA, SCR_ROULETTE, SCR_APPLAND, SCR_STATS, SCR_APPSHELF, SCR_APPPICK, SCR_SPHERE, SCR_ROULETTE_SPIN, SCR_SHELF, SCR_COUNT };
void screens_init(void);
void screen_show(int which);
void screen_back(void);
void screen_home(void);              /* straight to Home, clearing the back stack */
void zhome_focus_now_playing(void);   /* Zune Home always returns to the Now Playing item */
int  screen_press_was_long(void);    /* in a click handler: the press was a long press */
void screen_set_anim(int on);
int  screen_get_anim(void);
void screen_skip_anim_once(void);          /* the next screen change without the slide (the caller animates it) */
int  screen_current(void);
int  screen_back_leads_nowhere(void);   /* Home with nothing to go back to: no back-swipe arrow */
void ui_toast(const char *msg);
void splash_start(void);
void splash_cover(void);         /* a plain cover in the splash colour until splash_start (main.c, before the first frame) */
void splash_test_frame(int ms);   /* host renders: the splash at ms */
/* the "something" block-matrix clock (home.c): handle 0 = Home, 1 = screensaver */
int  mclock_new(int h, lv_obj_t *parent, int y);
void mclock_set(int h, const char *time_text);
void mclock_place(int h, int y, int shown);   /* the boot splash (splash.c): the Disc loads a disc, then hands off to Home */
int  ui_edge_nav(void);
int  ui_round_width(int y, int margin);   /* the round screen's width at height y, less margin each side */
void ui_back_glyph(lv_obj_t *label);   /* style an edge Back's label the theme's way */   /* 1 = the theme puts Back on the left edge (screens with their own header follow it) */
int  ui_toast_hint(const char *msg); /* show nonessential help only when no toast is active */   /* transient completion-feedback message */
void ui_status_refresh(void);     /* re-poll the home status row (battery/wifi/bt) now, e.g. after a radio toggle */
lv_obj_t *screen_get_root(int which);
/* screensaver (saver.c) */
void saver_create(lv_obj_t *root);
void saver_set_clock(const char *t, const char *date);
void saver_set_weather(const char *text);
void saver_set_track(const char *title, const char *artist, const void *backdrop_src, const char *path);
const char *ui_current_backdrop_src(void);
/* quick settings (quicksettings.c) */
void ui_publish_art_surfaces(const track_state_t *st,int playing); /* shared decoder-completion publication */
void quicksettings_create(lv_obj_t *root);
void quicksettings_build(void);            /* rebuild the drawer from config (called on every open) */
void qsconfig_create(lv_obj_t *root);      /* SCR_QSCONFIG: pick which drawer tiles appear */
void qsconfig_refresh(void);
void quicksettings_refresh(int playing);
void quicksettings_set_now_playing(const char *title, const char *artist, int playing);
void quicksettings_set_volume(int vol);
void quicksettings_set_art(const void *cover_dsc, const void *backdrop);   /* orbit hub cover + wash (path or RAM image) */
void quicksettings_set_battery(int pct, int charging);                          /* bottom-rim battery arc */
/* song info (songinfo.c) */
void songinfo_create(lv_obj_t *root);
void songinfo_set(const track_state_t *st);
void songinfo_output_tick(void);                /* Output row (Bluetooth codec): refresh while Song Info is visible */
/* Now Playing side menus (npmenus.c) + custom EQ (eqcustom.c) */
void npmenu_create(lv_obj_t *root);
void nphub_create(lv_obj_t *root);   /* right-swipe hub: Playback + Options buttons */
void nphub_refresh(void);            /* rebuild the hub for the current track (book-aware) */
void upnext_create(lv_obj_t *root);   /* SCR_UPNEXT: the player queue from the current song on (upnext.c) */
void upnext_open(void);                /* show SCR_UPNEXT */
void upnext_refresh(void);             /* read the queue fresh (on each SCR_UPNEXT show) */
void datetime_create(lv_obj_t *root);  /* SCR_DATETIME: set the date + time by hand (datetime.c) */
void datetime_refresh(void);           /* rebuilt from the current time on each show */
void ui_rtc_save_now(void);            /* save the system clock to the RTC now (hwclock -w, off the UI thread) */
void ui_note_transport_sent(void);      /* a play/pause/next/prev went to the player: this ipc generation needs no local-init one-shot */
void ui_art_redo(void);                /* re-request the cover without same-album reuse (Online Album Art switched) */
void ui_art_retry(void);               /* re-request the current song's cover (after the SD gate opens) */
int  ui_take_art_force(void);          /* 1 once when a cover redo is pending (main loop runs ui_update) */
void chapters_open(void);            /* load the current book's chapters + show SCR_CHAPTERS (books.c) */
void tune_create(lv_obj_t *root);
void eqcustom_create(lv_obj_t *root);
void colorpick_create(lv_obj_t *root);    /* accent colour picker screen */
void colorpick_open(void);                /* seed sliders from cfg + show */
void modes_create(lv_obj_t *root);        /* Working Mode (audio source) picker screen */
void modeinfo_create(lv_obj_t *root);     /* SCR_MODEINFO: the active working mode, big icon + live status */
void modeinfo_refresh(void);
int  bt_peer_name(char *out, int cap);    /* connected BT device (cached, non-blocking): 1 + its name, or 0 */
void modes_open(void);                     /* refresh selection + show SCR_WORKMODE */
/* source/working mode: 0=Local 1=USB-DAC 2=BT-Receiving 3=USB-Storage */
int  ui_set_source_mode(int mode);         /* replay the stock V2.28 switch sequence; 0=ok -1=bad arg */
int  ui_get_source_mode(void);
int  ui_source_switch_pending(void);
int  ui_source_switch_failed(void);
int ui_local_playback_allowed(void);
int  ui_detect_source_mode(void);          /* M17: the ACTUAL mode from the USB gadget state (0/1/3; BT reads as 0) */
void npmenu_set(const track_state_t *st, int playing, const void *thumb_src);
void npmenu_close_transients(void);   /* dismiss lv_layer_top popups on navigation */
void songmenu_close(void);            /* hold menu (songmenu.c) */
void fileops_close(void);             /* file confirm / destination picker (fileops.c) */
void eqcustom_pad_close(void);        /* EQ number pad (eqcustom.c) */
void ui_set_favorite(int on);   /* love/unlove the current song (0104) */
/* add-to-playlist picker (npmenus.c) */
void plpick_create(lv_obj_t *root);
void plpick_set_song(const char *path, int pos_id, const char *title);   /* song to add when a playlist is tapped; pos_id (a2 pos_id, 0 = unknown) picks a CUE/ISO sub-track */
int  library_picking(void);              /* several library items are being picked (multi-select) */
void library_pick_demo(const char *csv);   /* renderer/tests: tick these item indices */
void plpick_set_ids(const int *ids, int n);   /* instead: add exactly these SONG.IDs (a list as shown); then screen_show(SCR_PLPICK) */
void plpick_set_group(const char *col, const char *val);   /* instead: add every song of an ALBUM/ARTIST/GENRE (col) equal to val; then screen_show(SCR_PLPICK) */
/* playlist detail (playlistview.c) */
void plview_create(lv_obj_t *root);
void plview_open(long pid, const char *name);
void plview_refresh(void);   /* rebuild the song list from the DB (called on every entry) */
void plview_release(void);   /* the hidden playlist screen drops its song list (rebuilt by plview_refresh) */
/* wifi settings (wifi.c) */
void wifi_create(lv_obj_t *root);
void wifi_open(void);
int  wifi_toggle(void);        /* Quick Settings tile short-press: flip radio + persist, returns new state */
int  wifi_radio_live(void);   /* Wi-Fi radio really on (wpa_supplicant running) */
void lastfm_open(void);                   /* Settings -> Last.fm (SCR_LASTFM) */
void debug_open(void);                     /* Settings -> System -> Debug Mode (SCR_DEBUG) */
void debug_create(lv_obj_t *root);
void lastfm_create(lv_obj_t *parent);
void wifi_init_intent(void);   /* seed wifi_on intent from stock WIFI_STATUS (first run only) */
void wifi_supervise(void);     /* keepalive: (re)start supplicant if Wi-Fi should be on but isn't */
void wifi_info_create(lv_obj_t *root);
void wifi_info_open(void);
/* bluetooth settings (bt.c) */
void bt_create(lv_obj_t *root);
void bt_open(void);
int  bt_toggle(void);          /* Quick Settings tile short-press: flip BT + persist, returns new state */
int  bt_radio_on(void);        /* cheap actual BT-enabled state (rfkill), for the status icon + QS tile */
enum { BT_OFF, BT_ON, BT_TURNING_ON };
int  bt_state(void);          /* off / on / turning on: cheap (/proc + /sys), safe to poll every second */
void bt_info_create(lv_obj_t *root);
void bt_info_open(void);
#define BT_CODEC_N 7                   /* A2DP codec values (bt.c): SBC, AAC, Best (LDAC Mobile/Standard/High), aptX, aptX HD (applied, not chosen) */
extern const char *const BT_CODEC_LABEL[BT_CODEC_N];
void bt_aptx_setting_changed(int on);           /* bt.c: Settings > aptX (experimental) changed */
void bt_aptx_sync(int tell);                     /* bt.c: aptX on -> put mq_ui's aptX files in place (background) */
int  bt_codec_choices(void);                    /* bt.c: codec choices Settings shows (aptX ones only when installed) */
const char *bt_codec_hint(int v);               /* bt.c: Settings hint for a codec choice (fallback order) */
void bt_codec_setting_changed(void);            /* bt.c: Bluetooth Codec chosen (again): skipped codecs get another chance */
int  ui_bt_codec_frame(const char *mac, char *out, int cap);   /* the 06b3 frame that selects SBC for a route to mac */
void ui_bt_codec_upgrade_arm(const char *mac, int fresh);   /* after a route to mac (fresh = full route sequence): SBC cancels any pending switch; AAC/LDAC arms one for once BlueALSA has the PCM */
void ui_bt_codec_upgrade_cancel(void);           /* any route change / link loss: drop the pending switch and stale probe results */
const char *ui_route_mac(void);                  /* main.c: MAC the player is routed to, "" when local */
int  ui_bt_live_codec(char *out, int cap);      /* bt.c: the routed headphones' running codec ("LDAC", "aptX HD"), 1 = known; may start a background read */
const char *bt_codec_display(const char *sel);  /* bt.c: BlueALSA codec word -> display name */
void bt_boot_restore(void);   /* at startup: re-enable BT + arm auto-route if it was on */
void bt_notify_player_restart(void);   /* player restarted: forget stale auto-route so the poll re-routes */
int  ui_player_settling(void);         /* 1 while the player is still in its post-restart late-init settle window */
void library_open_favourites(void);
void library_open_album(const char *name);
void library_open_album_focus(const char *album, const char *artist, const char *path);   /* album, current song highlighted */
void library_open_artist(const char *name);
void albumwall_create(lv_obj_t *root);     /* SCR_ALBUMWALL: cover-flow album browser */
void albumwall_refresh(void);              /* rebuilt per entry from the album list */
void albumwall_prewarm_seed(void);         /* MAIN thread: (re)start the incremental album-cover prewarm enqueue */
void albumwall_step(int dir);              /* +1 next / -1 prev album (discrete: keys / demo) */
void albumwall_drag_begin(int px);         /* finger down: start a continuous drag from press x */
void albumwall_drag(int px);               /* finger move: flow follows the finger 1:1 */
void albumwall_drag_end(void);             /* finger up: inertial fling + snap to nearest album */
void albumwall_drag_cancel(void);          /* abandon a drag without a fling (tap path) */
void albumwall_scroll_rel(float d_alb);    /* rim scroll: nudge the flow by d_alb albums (continuous) */
void albumwall_settle(void);               /* rim release: snap to the nearest album */
void albumwall_play(void);                 /* play the centred album (cover tap) */
void albumwall_open(void);                 /* open the centred album's track list (cover long-press) */
const lv_font_t *ui_text_font(int px);     /* fallback-chained user-text font (14/16/18/20) */
/* apps launcher (apps.c) + launch hook (main.c) */
void apps_create(lv_obj_t *root);
void apps_reload(void);
void app_launch(const char *exec);
void app_launch_direct(const char *exec); /* landing-page confirmation only */
/* App landing page (roulette.c): Boomshine's, or one made from an installed app's app.conf */
void applanding_create(lv_obj_t *root);
void applanding_show(const char *name, const char *tagline, const char *about, const char *button, const char *exec);
void boomshine_landing(void);
#define APP_BUILTIN_BOOMSHINE "builtin:boomshine"   /* app_launch() target for the game built into mq_ui */
#define APP_BUILTIN_SPHERE "builtin:songsphere"     /* SongSphere: built in since 2.2.0 (sphere_ui.c) */
#define APP_BUILTIN_STATS "builtin:stats"           /* Listening Stats: a screen of mq_ui, opened from its landing page */
void stats_create(lv_obj_t *root);
void stats_open(void);                              /* read the numbers, then show them (the landing page's button) */
void stats_landing(void);                           /* its landing page */
lv_obj_t *stats_scroller(void);
void appshelf_create(lv_obj_t *root);       /* SCR_APPSHELF: Apps, the five chosen apps (appshelf.c) */
void appshelf_pick_create(lv_obj_t *root);  /* SCR_APPPICK: Choose apps */
void appshelf_open(void);                   /* refresh, then show Apps (the Disco menu) */
void appshelf_reload(void);
void appshelf_pick_open(void);
lv_obj_t *appshelf_scroller(void);
lv_image_dsc_t *app_icon_load(const char *bmp_path, int size);   /* appshelf.c: a BMP icon in RAM at size x size (free() it) */
void applanding_icon(const char *bmp_path);  /* the next applanding_show() draws this picture ("" / NULL: none) */
int  stats_back_consumed(void);
/* SongSphere, built in (sphere_ui.c). kind: 0 song (key = path), 1 album (album name), 2 artist */
void sphere_ui_create(lv_obj_t *root);
void sphere_ui_landing(void);
void sphere_ui_open(void);
void sphere_ui_tag(int kind, const char *key, const char *name);
int  sphere_ui_enabled(void);                /* Settings > Playback > SongSphere */
int  sphere_ui_back_consumed(void);
int  sphere_ui_owns_point(int x, int y);
int  sphere_ui_song_spot(const char *path, const char *album, const char *artist, float *x, float *y);   /* 1 own, 2 album, 3 artist */
const char *sphere_ui_mood_name(float x, float y);
lv_color_t sphere_ui_mood_colour(float x, float y);
void sphere_ui_test(int page, float x, float y);   /* host renders */
void ui_app_play_ids(int *ids, int n, int shuffle, const char *name, int play, int save);   /* main.c: play / keep SONG ids (takes ids) */                      /* Back closes its Wrapped / top-15 view first */
void stats_test_sub(int k); void stats_test_wrap(int k, int page);   /* host renders */
/* settings (master list + drill-in detail) */
void settings_create(lv_obj_t *root);      /* SCR_SETTINGS: the category list (Playback/Audio/...) */
void settings_refresh_list(void);
void setlist_create(lv_obj_t *root);       /* SCR_SETLIST: one category's rows (built per entry) */
void setlist_open(const char *group);      /* open one category ("Playback", "Audio", ...) */
void setlist_refresh(void);
void settings_apply_startup(void);

/* accent plumbing: the live accent + per-screen repaint so no surface shows a stale colour */
lv_color_t ui_current_accent(void);
lv_color_t ui_disco_fit_accent(lv_color_t c);   /* Disco: readable accent on the current variant */
/* Standard screen header: back-chevron (tucked out of the clipped top-left) + centred title, clear of
 * the chevron. Back taps call screen_back(). Returns the title label so screens with a DYNAMIC title
 * (e.g. Library retitling to Songs/Albums) can update it. Use on every detail/list screen for one
 * consistent header instead of per-screen copies. */
lv_obj_t *ui_header(lv_obj_t *root, const char *title);
lv_obj_t *ui_header_cb(lv_obj_t *root, const char *title, lv_event_cb_t back_cb);  /* header with custom back */
void home_set_accent(lv_color_t accent);
void saver_set_accent(lv_color_t accent);
void saver_show_sync(void);   /* apply saver-style + accent immediately on saver show */
int  saver_wants_bright(void);/* 1 = keep full brightness (vinyl art showcase), don't dim */
int  ui_run_bounded(char *const argv[], int timeout_ms);  /* external cmd as a killable child w/ hard timeout */
void ui_decode_lock(void);    /* serialize an ffmpeg artwork decode against the other decoders (OOM guard) */
void ui_decode_unlock(void);
void library_open_letter_picker(void);   /* library.c: open the A - Z picker (a hold on the rim scrollbar) */
void ui_pill_style(lv_obj_t *b, int primary);   /* ui.c: the Play / Shuffle pill look (Disco, Squash, Null) */
int  library_lift(void);              /* library.c: px the Library headers (and the track sheet) sit higher; 0 = theme unchanged */
void library_header_lift(lv_obj_t *root, uint32_t from, lv_obj_t *title);   /* library.c: raise header parts added since child `from` */
void ui_art_set_pending(int pending); /* temporarily faster delivery while artwork is in flight */
int  ui_art_settling(void);           /* ui.c: a cover is waiting out its settle time */
void ui_art_wake(void);               /* main.c: a cover started settling - poll at the settle cadence */
void ui_loop_wake(void);              /* main.c: wake a resting main loop (safe from any thread) */
int  ui_net_up(void);
int  ui_battery(int *charging);       /* main.c: battery percent (-1 unknown) and charging, from the last status poll */                 /* main.c: Wi-Fi has a link/address (last status poll) */
void ui_perf_note(const char *stage); /* opt-in DISKOS_PERF diagnostics, no idle polling */
void tune_refresh(void);      /* re-sync Tune panel Play Mode/EQ labels on show */

/* Audio/DAC cluster: send live command (no-op if value <0 = unmanaged). main.c. */
void ui_set_dre(int on);
void ui_set_gain(int high);
void ui_set_output(int spdif);
int  ui_route_bt(const char *mac);   /* route player audio to a connected BT speaker (by MAC) */
int  ui_route_analog(void);          /* route player audio back to the local DAC */
void ui_set_dac_filter(int idx);
void ui_set_replay_gain(int v);   /* 0=Off 1=Track 2=Album */
void ui_set_gapless(int on);
void ui_set_memory(int mode);
void ui_set_maxvol(int v);
void ui_set_balance(int v);
void ui_reapply_audio(void);  /* resend managed audio settings (DRE/filter/etc.) on player-ready / reconnect */
void ui_request_sleep(void);  /* Quick Settings "Sleep": manual screen-off request (works with saver Off) */
void ui_set_brightness(int v);   /* persist + apply */
int  ui_get_brightness(void);
int  ui_effective_brightness(void);   /* what the backlight runs at (Outdoor mode = full) */
void ui_backlight(int v);        /* transient backlight write, no persist */
void ui_set_sleep_timer(int minutes);   /* 0 = off; pauses playback when it elapses */
void ui_set_sleep_eoc(long target_ms, const char *path, int at_book_end);  /* pause when THAT book's position reaches target_ms (end of chapter); at_book_end=1 if the target is the file end (last/chapterless chapter) so a rollover can fulfil it */
void ui_disarm_book_eoc(void);  /* disarm any end-of-chapter sleep; call on explicit track navigation */
int  ui_book_active(void);      /* 1 if an audiobook is the active playback context (suppress play-mode changes) */
int  ui_sleep_state(int *secs_left);    /* 0 off, 1 duration (fills secs_left), 2 end-of-chapter */
void ui_np_close_overlays(void);        /* dismiss the Now Playing sleep-timer popover (on screen change) */
int  ui_np_overlay_active(void);        /* 1 while the sleep popover is up (suppress ring seek/nav) */
long ui_smart_rewind_ms(long idle_seconds);  /* how far to back up on resume, by idle time */
void ui_defer_sleep(void);                    /* hold the sleep pause off briefly after a transport tap */
void setting_detail_create(lv_obj_t *root);
void setting_detail_refresh(void);
int  dhome_owns_point(int x, int y);     /* Disco Music: the progress line / arc (no swipe to Apps from there) */
int  eqcustom_owns_point(int x, int y);   /* the round EQ's dial: no back-swipe starts there */
void eqpreset_create(lv_obj_t *root);
void eqpreset_refresh(void);
void eqcustom_refresh(void);   /* re-resolve the edited USER slot on SCR_EQ_EDITOR entry (display-only) */
void settings_open_detail(int idx);
void settings_open_key(const char *key);   /* open a setting detail by cfg key (drawer tiles) */
/* reusable on-screen keyboard modal (kbinput.c) */
typedef void (*kbinput_done_cb_t)(const char *text);  /* NULL if cancelled; "" if saved empty */
void kbinput_open(const char *title, const char *initial, kbinput_done_cb_t cb);
/* same modal, but the text field is masked (dots) - for secrets like Wi-Fi passwords. */
void kbinput_open_password(const char *title, const char *initial, kbinput_done_cb_t cb);
int  kbinput_active(void);   /* 1 while the modal keyboard is up (suppress gestures) */
/* search */
void search_create(lv_obj_t *root);
void ui_clock_refresh(void);
/* IPC decode seams (frames wired in once decoded) */
int ui_seek_to(long ms);   /* returns 0 if the seek frame was queued, -1 if the send failed */
int  ui_play_book(const char *path, long resume_ms);  /* 1 = sent */   /* play an audiobook file + resume at resume_ms (0 = start) */
void ui_cancel_book_resume(void);                      /* end the book session (call on any explicit track change) */
void ui_book_user_seeked(long target_ms);              /* manual seek in a book: drop pending resume, persist target_ms to the bookmark now, keep session */
int  ui_set_volume(int vol);   /* set absolute volume 0..VOL_MAX; returns 0=queued, -1=failed */
void ui_set_workmode(int mode);
void ui_np_set_playmode(int wm);   /* fork (Zune): set the play mode and repaint (its play-mode list) */
int ui_apply_eq(int preset);
int ui_eq_select(int preset);    /* central EQ apply+persist (eq_preset + eq_last); 0 ok, -1 send failed */
int ui_is_playing(void);         /* authoritative normalized play state (for the drawer transport glyph) */
int  ui_transport_command(const char *cmd); /* shared play/pause and music/book skip behavior */
void ui_pp_tap_hint(void);       /* call on a play/pause tap: show the predicted state instantly (no ~1s lag) */
void ui_pp_glyph(lv_obj_t *label, int playing);   /* set a play/pause glyph (play optically centred) */
int  ui_glyph_ink_dy(const lv_font_t *f, uint32_t cp);   /* offset that centres a glyph's ink in its line */
void ui_glyph_center_ink(lv_obj_t *label);                /* a one-icon label centred by its ink */
int  ui_pp_icon_playing(int real_playing);   /* returns the play/pause glyph state: prediction if held, else real */
const char *ui_eq_name(int i);   /* EQ preset name for index 0..20 (drawer toast) */
void ui_rescan_library(void);
int  ui_theme_reload(const char *screen);   /* re-launch the UI to apply a theme change (main.c) */
int  ui_play_mode_now(void);           /* the play mode the player is using (0..4), -1 unknown (main.c) */
int  ui_queue_jump(int ord1);          /* Up Next: type-0 jump to 1-based position in the current queue (0 = sent) */
/* play a library list (0100): list_type 0=all,2=artist,3=album,10=genre;
 * name = artist/album/genre (NULL/"" for all); pos1 = 1-based start track. */
int  ui_play_list(int list_type, const char *name, int pos1);   /* 1 = the play was sent (else refused/failed, toasted) */
int  ui_play_favorite(int love_id, int pos1);   /* V2.57 favourites queue from this MY_LOVE.ID (row pos1) */
int  ui_press_travel(void);   /* px the current/last touch moved; tap handlers under swipes ignore a moved press */
int  ui_play_song_by_path(const char *path);   /* folder browser: play a track by absolute path (1=ok) */
int  ui_play_playlist(long pid, int pos);
/* swipe sensitivity (px of horizontal travel needed for a back-swipe); lower = more sensitive */
void ui_set_swipe_thresh(int px);
void ui_apply_swipe_thresh(int px);   /* live apply, no persist (for slider drag) */
int  ui_get_swipe_thresh(void);
/* now playing (ui.c) */
void ui_create(lv_obj_t *root);
void ui_history_tick(const track_state_t *st);
void ui_update(const track_state_t *st);
void ui_art_poll(lv_timer_t *t);          /* apply a finished album-art decode (main thread) */
void ui_start_art_prewarm(void);          /* spawn the background cover/accent prewarm sweep */
int  ui_prewarm_enqueue(const char *path); /* MAIN thread: queue a track path for background cover decode; 1=queued/dup, 0=full */
void ui_set_accent_config(int mode, int rgb);  /* 0=dynamic / 1=static(rgb); applies immediately */
int  ui_accent_is_static(void);
lv_color_t ring_border_color(void);   /* Ring button rings: the theme accent, or the picked Accent colour */
void ui_set_prewarm_mode(int m);          /* 0=covers only 1=+per-track sweep when idle 2=+sweep when idle & charging */
int  ui_run_cap_bounded(const char *cmd, char *out, int cap, int timeout_ms);   /* bt.c: popen-like capture, killed at timeout */
int  bt_radio_on(void);              /* current mode; 0 = Album Art Cache off */
int  ui_display_is_off(void);            /* fully-off panel; visual callbacks may skip work */
int  ui_main_is_idle(void);               /* main.c: 1 when the screen is dimmed/off (no active use) */
const lv_font_t *ui_font_cjk(int size);   /* international/CJK user-text chain */
void ui_reveal_title(lv_obj_t *label);
void ui_music_scroll(lv_obj_t *label);
void ui_library_list_watch(lv_obj_t *list);
void ui_library_row(lv_obj_t *row);
const lv_font_t *ui_np_font(int size);    /* optional Now Playing face, with the same fallbacks */
int  ui_take_art_applied(void);           /* 1 once after art (re)applied -> re-push surfaces */
int  ui_np_seek_press(int x, int y);
int  ui_np_cover_hit(int x, int y);      /* 1 = (x,y) is on the visible NP cover */
void ui_np_seek_line(lv_obj_t *bar);     /* theme: seek on this straight bar/slider instead of the ring */
void ui_np_pp_filled(int on);            /* theme: the play key is filled with the accent (recolour its fill) */
void ui_np_backdrop_enabled(int on);     /* theme: 0 = no blurred cover behind Now Playing */
int  ui_np_seek_move(int x, int y);
int  ui_np_seek_release(int x, int y);
/* full-screen album-art view (stock cover.png reuse): tap the NP cover to open */
void ui_np_fsart_open(void);
void ui_np_fsart_close(void);
int  ui_np_fsart_active(void);   /* 1 while the full-screen art is up (suppress NP seek/nav) */
const char *ui_current_cover_src(void);
const void *ui_current_backdrop_img(void);  /* the blurred backdrop decoded in RAM, else its file path */
void ui_np_rescroll(void);                  /* Now Playing shown: let a long title scroll again */
const void *ui_current_cover_dsc(void);   /* rotatable cover for vinyl saver */
void saver_vinyl_spin(int want);          /* drive saver vinyl spin (from main loop) */
const char *ui_current_thumb_src(void);
void ui_set_np_style(int vinyl);   /* 0 = cover (rounded square), 1 = vinyl (disc) */
void ui_vinyl_spin(int want);      /* drive the vinyl spin from the main loop */
void ui_imm_control_apply(void);
void ui_imm_control_cancel(void);
int ui_imm_volume_mode(void);
void ui_seek_preview(long position,long duration,int on);
int ui_seek_preview_active(void);
void ui_show_volume(int vol);      /* show the on-screen volume bar (auto-hides) */
/* home */
void home_create(lv_obj_t *root);
void home_shortcuts_create(lv_obj_t *root);
void home_shortcuts_refresh(void);
void home_set_clock(const char *t, const char *s);
void home_set_art_enabled(int on);   /* 0 = the theme's Home pill shows no album art */
void home_set_backdrop_enabled(int on);   /* 0 = no blurred cover behind Home (the theme's own background) */
void home_set_status(int batt, int charging, int wifi, int bt);
void home_set_weather(const char *text);
/* weather.c */
void weather_fetch_async(void);
void weather_poll(lv_timer_t *t);
void weather_set_enabled(int on);   /* Settings: on/off passive weather + background fetch */
void weather_app_create(lv_obj_t *root);   /* Weather app: set location */
void weather_app_open(void);               /* refresh + show the weather app */
/* lyrics.c */
void lyrics_create(lv_obj_t *root);
void lyrics_open(void);                     /* fetch current track lyrics + show */
void lyrics_poll(lv_timer_t *t);            /* apply a finished fetch (main thread) */
void home_set_now_playing(const char *title, const char *artist, lv_color_t accent, bool playing);
void home_set_art_src(const void *src);
void home_set_backdrop(const void *src);   /* full-screen blurred album backdrop on Home (path or RAM image) */
typedef void (*home_settings_click_cb_t)(void);
void home_set_settings_click_cb(home_settings_click_cb_t cb);
/* library */
void library_create(lv_obj_t *root);
typedef int (*library_song_click_cb_t)(int index);   /* index = song id; returns 1 if a play was sent */
void library_set_song_click_cb(library_song_click_cb_t cb);
void library_refresh(void);   /* rebuild the current Library view (after external DB changes) */
void library_artist_mode_changed(void);   /* Settings changed the Artists grouping: leave an artist drill + rebuild */
void library_ensure_capacity(void);  /* grow row buffers to fit the library (after a rescan adds tracks) */
/* primary scrollable list per long-list screen (for rim-scroll); NULL if none */
lv_obj_t *library_scroller(void);
void library_scroll_letter_tick(void);   /* flash the current A-Z position while rim-scrolling */
lv_obj_t *playlistview_scroller(void);
lv_obj_t *search_scroller(void);
int search_back_consumed(void);
int search_back_to_landing(void);   /* Disco: keyboard -> landing page */
int settings_back_consumed(void);   /* Display > Disco Options: back to Display */
/* ---- fork: the Disco theme (disco.c) ---- */
void screen_section(int which, int dir);          /* menu picker: jump to a section's top screen (stack = Music -> it) */
void disco_init(lv_obj_t *parent);                /* after every screen exists (screens_init) */
void disco_screen_entered(int which, lv_obj_t *root);
void disco_screen_entering(int which);         /* before the screen refreshes: show / hide the picker */   /* every transition: backdrop, glass rows, picker */
int  disco_np_wanted(void);                       /* 1 while Disco itself opens the Now Playing screen (immersive) */
void disco_art_changed(void);                     /* new cover decoded (ui_publish_art_surfaces) */
void disco_menu_open(void);                       /* Settings > Display > Disco Menu */
void dhome_create(lv_obj_t *root);                /* the Disco Music/Home screen (disco_home.c) */
void dhome_set_clock(const char *t, const char *sub);
void dhome_set_weather(const char *s);
void dhome_set_status(int batt, int charging, int wifi, int bt);
void dhome_set_now_playing(const char *title, const char *artist, lv_color_t accent, bool playing);
void dhome_art_changed(void);
void dhome_set_accent(lv_color_t accent);
void dhome_show_clock(int on);
void disco_open_np_immersive(void);
int  disco_mirror(void);
const void *disco_idle_art(void);         /* disco_home.c: the generated CD shown while nothing plays */
int  dhome_idle(void);                    /* Music is idle (nothing playing): "Go to Library" + the CD */                   /* Disco > Side = Left: the hub and everything around it on the left (read once; OK restarts the UI) */
static inline int disco_mx(int x, int w){ return disco_mirror() ? 360 - x - w : x; }   /* an x laid out for the right-hand side, mirrored when Left */
int  disco_clear_left(int y1, int y2);
int  disco_clear_right(int y1, int y2);     /* lists: the x a row must end at to clear the closed sliver (0 = none) */
void disco_nav_set_open(int open);
void ui_back_hint(int y, int dx);
void ui_back_hint_left(int y,int dx);
void ui_shortcuts_hint(int y, int dx);
void ui_home_hint(int x, int dy);   /* swipe-up-to-Home feedback (main.c) */
const lv_font_t *ui_font_user(int size);   /* ui.c: list user text, sized by Font Size */           /* main.c: back-swipe feedback (dx <= 6 hides it) */         /* the navigation circle: 1 popped in, 0 back to the tab */      /* lists: the x a row must start at to clear the picker (0 = no limit) */         /* the title overlay: Now Playing + immersive */
const void *ui_current_sharp_img(void);           /* ui.c: the 364 px sharp cover (RGB888) or NULL */
int  ui_np_fav_state(void);                       /* -1 no track, else 0/1 */
void ui_np_fav_toggle(void);
int  ui_np_mode_next(void);       /* fork: 1 = Back closed the search results page */
lv_obj_t *apps_scroller(void);
void search_set_song_click_cb(library_song_click_cb_t cb);  /* tap a search result -> play it */
/* current album/artist/genre drill context (player list_type 3/2/10); 0 if flat */
int  library_drill_context(int *list_type, char *name, int cap);
/* step one level back within the library; 0 if already at the top menu */
int  library_back(void);
/* Disco: hand-built list rows (disco.c) - children [0] icon, [1] name, [2] value */
lv_obj_t *disco_row(lv_obj_t *root, int y, int h, const char *glyph, const lv_font_t *gfont, const char *name, lv_event_cb_t cb, void *ud);
lv_obj_t *disco_title(lv_obj_t *root, const char *t);
lv_obj_t *squash_menu_row(lv_obj_t *root,int slot,const char *glyph,const lv_font_t *font,const char *name,lv_event_cb_t cb,void *ud);
void ui_np_open_album(void);    /* the playing song's album in the Library, the song focused */
void ui_np_open_artist(void);   /* the playing song's artist, its album focused */
void library_open_artist_focus(const char *artist, const char *album);
void library_open_artists(void); void library_open_songs(void);   /* Library at Artists / Songs (Disco menu) */
void albumwall_visible(int visible);

#endif

void roulette_create(lv_obj_t *root);
/* Album Roulette, built in: the spin behind the vinyl landing page (roulette_spin.c, roulette_backend.c) */
#define APP_BUILTIN_ROULETTE "builtin:roulette"
void roulette_spin_create(lv_obj_t *root);
void roulette_spin_open(void);              /* a fresh sample, the wheel while covers are prepared, then the spin */

#define APP_BUILTIN_SHELF "builtin:disc-shelf"
