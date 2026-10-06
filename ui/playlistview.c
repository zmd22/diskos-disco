/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "screens.h"
#include "braun.h"
#include "curvelist.h"
#include "ipc.h"
#include "theme.h"
#include "vlist.h"
#include "i18n.h"
#include "theme_kit.h"
#include "musicdb.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Playlist detail (SCR_PLVIEW): opened by tapping a playlist in the Library.
 * Header name + Play / Shuffle + a horizontal 3-dot menu (Edit name / Delete).
 * Deleting removes only the playlist + its membership rows; the SONG table and
 * the files are never touched. */

static long g_pid;
static char g_name[160];
static lv_obj_t *g_title_lbl, *g_song_list;
static lv_obj_t *g_play_btn, *g_shuffle_btn;   /* LV_STATE_DISABLED when the playlist is empty */
static lv_obj_t *g_menu, *g_dialog;   /* transient popups on lv_layer_top */

/* Deletes async: callers run inside LV_EVENT_CLICKED handlers on descendants of *p, so a
 * synchronous lv_obj_del here would free the very button whose click is still being
 * dispatched (UAF once the event returns). *p is cleared immediately so no path can
 * double-close while the async delete is pending. */
static void close_pop(lv_obj_t **p){ if(*p){ lv_obj_delete_async(*p); *p = NULL; } }

/* Enable/disable the transport buttons for an empty playlist: at 0 songs Play/Shuffle would only
 * toast, so show them disabled. LV_STATE_DISABLED (not clearing CLICKABLE) so the theme kit still
 * classifies and styles them like a populated playlist's, and LVGL sends no press/click to them.
 * The disabled look (one neutral colour for both labels, faded button) is set up in plview_create. */
static void pl_set_transport_enabled(int on){
    lv_obj_t *btns[2] = { g_play_btn, g_shuffle_btn };
    for(int i=0;i<2;i++){
        if(!btns[i]) continue;
        lv_obj_t *l = lv_obj_get_child(btns[i], 0);
        if(on){ lv_obj_remove_state(btns[i], LV_STATE_DISABLED); if(l) lv_obj_remove_state(l, LV_STATE_DISABLED); }
        else  { lv_obj_add_state(btns[i], LV_STATE_DISABLED);    if(l) lv_obj_add_state(l, LV_STATE_DISABLED); }
    }
}

/* ---- song list ---------------------------------------------------------- */
static mdb_song_t *g_pl_songs; static int g_pl_n;
static lv_obj_t *g_menu_btn, *g_clear_btn; static int g_cur_id = -1; static uint32_t g_clear_armed;
static void plv_reload(void);
/* the Queue's Clear: tap, then tap again within 3 s ("Sure?") */
static void clear_cb(lv_event_t *e){
    if(lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    lv_obj_t *l = lv_obj_get_child(g_clear_btn, 0);
    if(!g_clear_armed || lv_tick_elaps(g_clear_armed) > 3000){ g_clear_armed = lv_tick_get(); if(l) lv_label_set_text(l, "Sure?"); return; }
    g_clear_armed = 0; if(l) lv_label_set_text(l, "Clear");
    queue_clear(); ui_toast("Queue cleared"); plv_reload();
}
static vlist_t g_plv;
static void plv_song_cb(lv_event_t *e);
static void plv_remove_cb(lv_event_t *e);
static void plv_add_row(int i){
        if(i < 0 || i >= g_pl_n) return;
        lv_obj_t *r = lv_button_create(g_song_list);
        lv_obj_remove_style_all(r);
        lv_obj_set_size(r, 280, 46);
        lv_obj_set_style_radius(r, 8, 0);
        lv_obj_set_style_bg_color(r, TC(SURFACE), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(r, LV_OPA_70, LV_STATE_PRESSED);
        lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(r, plv_song_cb, LV_EVENT_SHORT_CLICKED, (void*)(intptr_t)(i+1));
        lv_obj_add_event_cb(r, plv_remove_cb, LV_EVENT_LONG_PRESSED, (void*)(intptr_t)(i+1));  /* L32: hold to remove */
        lv_obj_t *t = lv_label_create(r);
        lv_label_set_text(t, g_pl_songs[i].title);
        if(g_pl_songs[i].id == g_cur_id) lv_obj_set_style_text_color(t, ui_current_accent(), 0);   /* the playing song */
        lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
        lv_obj_set_pos(t, 12, 5); lv_obj_set_size(t, 256, 19);
        lv_obj_set_style_text_font(t, ui_font_user(16), 0);   /* CJK titles like Library/Search */
        lv_obj_set_style_text_color(t, TC(TEXT_PRIMARY), 0);
        lv_obj_t *a = lv_label_create(r);
        lv_label_set_text(a, g_pl_songs[i].artist);
        lv_label_set_long_mode(a, LV_LABEL_LONG_DOT);
        lv_obj_set_pos(a, 12, 25); lv_obj_set_size(a, 256, 16);
        lv_obj_set_style_text_font(a, ui_font_user(14), 0);
        lv_obj_set_style_text_color(a, TC(TEXT_LYRICS), 0);
}
static void plv_scroll_cb(lv_event_t *e){ (void)e; vlist_follow(&g_plv); }
static void plv_reload(void);   /* fwd */
/* Long-press a song row -> remove it from the playlist (L32). Uses the 1-based display ordinal
 * bound to the row, matching mdb_playlist_songs()'s order. */
static void plv_remove_cb(lv_event_t *e){
    if(lv_event_get_code(e)!=LV_EVENT_LONG_PRESSED) return;
    int pos = (int)(intptr_t)lv_event_get_user_data(e);
    if(mdb_playlist_remove_at(g_pid, pos)){
        ui_toast("Removed from playlist");
        plv_reload();                 /* rebuild so ordinals stay correct */
        library_refresh();            /* playlist row count / Now-Playing add-state */
    }
}
static void plv_song_cb(lv_event_t *e){
    if(lv_event_get_code(e)!=LV_EVENT_SHORT_CLICKED) return;   /* SHORT_CLICKED so a long-press-to-remove never also plays */
    int pos = (int)(intptr_t)lv_event_get_user_data(e);   /* 1-based */
    if(ui_play_playlist(g_pid, pos)) screen_show(SCR_NOWPLAYING);   /* refused / not sent: stay (toasted) */
}
static void plv_reload(void){
    {   /* the Queue gets Clear instead of the ... menu; the playing song is highlighted */
        int q = 0;                                  /* (the queue has its own screen now) */
        if(g_clear_btn){ if(q) lv_obj_remove_flag(g_clear_btn, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(g_clear_btn, LV_OBJ_FLAG_HIDDEN);
                         lv_obj_t *l = lv_obj_get_child(g_clear_btn, 0); if(l) lv_label_set_text(l, "Clear"); g_clear_armed = 0; }
        if(g_menu_btn){ if(q) lv_obj_add_flag(g_menu_btn, LV_OBJ_FLAG_HIDDEN); else lv_obj_remove_flag(g_menu_btn, LV_OBJ_FLAG_HIDDEN); }
        track_state_t cs; ipc_get_state(&cs); mdb_song_t ms; g_cur_id = (cs.have_track && mdb_song_by_path(cs.path, &ms)) ? ms.id : -1;
    }
    if(g_title_lbl) lv_label_set_text(g_title_lbl, g_name);
    if(!g_song_list) return;
    vlist_end(&g_plv);
    lv_obj_clean(g_song_list);
    free(g_pl_songs); g_pl_songs = NULL; g_pl_n = 0;
    /* size to the real song count (no 300 cap). songs[] is only read while building the rows
     * (plv_song_cb replays by id+position, not a retained pointer) -> malloc + free here. */
    int cnt = mdb_playlist_count(g_pid);
    mdb_song_t *songs = (cnt>0 && (size_t)cnt <= ((size_t)-1) / sizeof(mdb_song_t))   /* 32-bit overflow guard */
                        ? malloc((size_t)cnt*sizeof(mdb_song_t)) : NULL;
    int n = songs ? mdb_playlist_songs(g_pid, songs, cnt) : 0;
    /* Cap RENDERED rows well under LVGL's uint16 child count (65535, where child_cnt wraps and corrupts the
     * child array). 8192 is far above any realistic playlist yet memory-safe; a pathological larger playlist
     * simply shows its first 8192. (Playback still uses the full playlist via id+position replay.) */
    if(n > 8192) n = 8192;
    pl_set_transport_enabled(n > 0);
    if(n<=0){
        free(songs);   /* NULL-safe */
        lv_obj_t *l = lv_label_create(g_song_list);
        lv_label_set_text(l, "Empty playlist\nAdd songs from Now Playing");
        lv_obj_set_style_text_color(l, TC(TEXT_MUTED), 0);
        lv_obj_set_style_text_font(l, TF(UI_14), 0);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        return;
    }
    g_pl_songs = songs; g_pl_n = n;                  /* kept: rows are created as they scroll into view */
    lv_obj_scroll_to_y(g_song_list, 0, LV_ANIM_OFF);
    if(n >= VLIST_MIN) vlist_begin(&g_plv, g_song_list, n, 46, 6, 0, 226, plv_add_row);
    else for(int i = 0; i < n; i++) plv_add_row(i);
    { static int hinted = 0; if(!hinted){ hinted = 1; ui_toast("Hold a song to remove it"); } }  /* L32 discoverability, once/run */
}
/* Public: rebuild the list from the DB. Called by the screen manager on EVERY entry
 * to SCR_PLVIEW (incl. back-nav), so tap positions can't go stale after the playlist's
 * membership changes elsewhere (e.g. add-to-playlist from Now Playing). */
void plview_refresh(void){ plv_reload(); }

void plview_open(long pid, const char *name){
    g_pid = pid;
    snprintf(g_name, sizeof g_name, "%s", name ? name : "Playlist");
    screen_show(SCR_PLVIEW);   /* transition() calls plview_refresh() -> plv_reload() */
}

/* ---- play / shuffle ----------------------------------------------------- */
static void play_cb(lv_event_t *e){
    if(lv_event_get_code(e)!=LV_EVENT_CLICKED) return;
    if(mdb_playlist_count(g_pid) < 1){ ui_toast("Playlist is empty"); return; }
    ui_set_workmode(0);
    if(ui_play_playlist(g_pid, 1)) screen_show(SCR_NOWPLAYING);
}
static void shuffle_cb(lv_event_t *e){
    if(lv_event_get_code(e)!=LV_EVENT_CLICKED) return;
    int c = mdb_playlist_count(g_pid);
    if(c < 1){ ui_toast("Playlist is empty"); return; }
    srand(lv_tick_get());
    ui_set_workmode(1);                       /* shuffle play-mode */
    if(ui_play_playlist(g_pid, rand()%c + 1)) screen_show(SCR_NOWPLAYING);   /* + random start track */
}

/* ---- delete confirm ----------------------------------------------------- */
static void del_yes_cb(lv_event_t *e){
    if(lv_event_get_code(e)!=LV_EVENT_CLICKED) return;
    close_pop(&g_dialog);
    int ok = mdb_playlist_delete(g_pid);
    if(ok){
        ui_toast("Playlist deleted");
        screen_back();                        /* back to the Playlists list */
        library_refresh();                    /* rebuild it so the deleted playlist is gone */
    } else {
        ui_toast("Delete failed");            /* stay put on failure */
    }
}
static void del_no_cb(lv_event_t *e){ if(lv_event_get_code(e)==LV_EVENT_CLICKED) close_pop(&g_dialog); }

static lv_obj_t *card(int w, int h){
    /* full-screen modal backdrop on the top layer: CATCHES taps so they can't reach the playlist
     * controls beneath (this confirm was a bare floating card before). Stored as g_dialog so
     * close_pop removes the backdrop AND the card child together. No dismiss-on-tap - use the buttons. */
    lv_obj_t *bg = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(bg);
    lv_obj_set_size(bg, 360, 360);
    lv_obj_center(bg);
    lv_obj_set_style_bg_color(bg, TC(CANVAS), 0);
    lv_obj_set_style_bg_opa(bg, LV_OPA_50, 0);
    lv_obj_clear_flag(bg, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(bg, LV_OBJ_FLAG_CLICKABLE);
    g_dialog = bg;
    lv_obj_t *c = lv_obj_create(bg);
    lv_obj_remove_style_all(c);
    lv_obj_set_size(c, w, h);
    lv_obj_center(c);
    lv_obj_set_style_radius(c, 16, 0);
    lv_obj_set_style_bg_color(c, TC(SURFACE), 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(c, 1, 0);
    lv_obj_set_style_border_color(c, TC(SURFACE_RAISED), 0);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    return c;
}
static lv_obj_t *card_btn(lv_obj_t *p, int y, int w, const char *txt, lv_color_t bg, lv_color_t fg, lv_event_cb_t cb){
    lv_obj_t *b = lv_button_create(p);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, w, 40);
    lv_obj_set_ext_click_area(b, 4);   /* 40px pill -> ~48px touch target on the capacitive panel */
    lv_obj_align(b, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_set_style_radius(b, 12, 0);
    lv_obj_set_style_bg_color(b, bg, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    ui_on(b, cb, LV_EVENT_CLICKED, NULL, "playlistview.cb", UI_CORE);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, TF(UI_16), 0);
    lv_obj_set_style_text_color(l, fg, 0);
    lv_obj_center(l);
    return b;
}
static void show_delete_confirm(void){
    close_pop(&g_menu);
    lv_obj_t *dc = card(264, 200);   /* card() sets g_dialog = the full-screen backdrop; content goes on dc */
    lv_obj_t *t = lv_label_create(dc);
    lv_label_set_text(t, "Delete playlist?");
    lv_obj_set_style_text_font(t, TF(UI_20), 0);
    lv_obj_set_style_text_color(t, TC(TEXT_PRIMARY), 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 26);
    lv_obj_t *s = lv_label_create(dc);
    lv_label_set_text(s, "The songs are kept.");
    lv_obj_set_style_text_font(s, TF(UI_14), 0);
    lv_obj_set_style_text_color(s, TC(TEXT_MUTED), 0);
    lv_obj_align(s, LV_ALIGN_TOP_MID, 0, 54);
    card_btn(dc, 88,  200, "Delete", TC(DANGER_DIALOG), TC(STATUS_DANGER), del_yes_cb);
    card_btn(dc, 134, 200, "Cancel", TC(SURFACE_RAISED), TC(TEXT_PRIMARY), del_no_cb);
}

/* ---- rename ------------------------------------------------------------- */
static void rename_done(const char *name){
    if(!name) return;
    if(mdb_playlist_rename(g_pid, name)){
        snprintf(g_name, sizeof g_name, "%s", name);
        theme_title_text(g_title_lbl, g_name);
        library_refresh();   /* rebuild the Playlists list so its row shows the new name (delete already does this) */
        ui_toast("Renamed");
    } else {
        ui_toast("Rename failed");
    }
}
static void rename_cb(lv_event_t *e){
    if(lv_event_get_code(e)!=LV_EVENT_CLICKED) return;
    close_pop(&g_menu);
    kbinput_open("Playlist name", g_name, rename_done);
}

/* ---- 3-dot menu --------------------------------------------------------- */
static void menu_dismiss_cb(lv_event_t *e){ if(lv_event_get_code(e)==LV_EVENT_CLICKED) close_pop(&g_menu); }
static void del_menu_cb(lv_event_t *e){ if(lv_event_get_code(e)==LV_EVENT_CLICKED) show_delete_confirm(); }
static void export_cb(lv_event_t *e){   /* L37: write the playlist to /tmp/sdcard/<name>.m3u */
    if(lv_event_get_code(e)!=LV_EVENT_CLICKED) return;
    close_pop(&g_menu);
    char fn[112];
    if(mdb_playlist_export(g_pid, g_name, fn, sizeof fn)){
        char msg[160]; snprintf(msg, sizeof msg, "Exported to %s", fn);
        ui_toast(msg);
    } else ui_toast("Couldn't export playlist");
}
static void open_menu_cb(lv_event_t *e){
    if(lv_event_get_code(e)!=LV_EVENT_CLICKED) return;
    close_pop(&g_menu);
    /* full-screen catcher so a tap outside dismisses */
    g_menu = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(g_menu);
    lv_obj_set_size(g_menu, 360, 360);
    lv_obj_center(g_menu);
    lv_obj_set_style_bg_color(g_menu, TC(CANVAS), 0);
    lv_obj_set_style_bg_opa(g_menu, LV_OPA_50, 0);
    lv_obj_add_flag(g_menu, LV_OBJ_FLAG_CLICKABLE);
    ui_on(g_menu, menu_dismiss_cb, LV_EVENT_CLICKED, NULL, "playlistview.menu_dismiss", UI_CORE);
    lv_obj_t *c = lv_obj_create(g_menu);
    lv_obj_remove_style_all(c);
    lv_obj_set_size(c, 240, 176);
    lv_obj_center(c);
    lv_obj_set_style_radius(c, 16, 0);
    lv_obj_set_style_bg_color(c, TC(SURFACE), 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    card_btn(c, 12,  216, "Edit Name",       TC(SURFACE_RAISED), TC(TEXT_PRIMARY), rename_cb);
    card_btn(c, 64,  216, "Export to SD",    TC(SURFACE_RAISED), TC(ACCENT_PRIMARY), export_cb);
    card_btn(c, 116, 216, "Delete Playlist", TC(SURFACE_RAISED), TC(STATUS_DANGER), del_menu_cb);
}

static void back_cb(lv_event_t *e){ if(lv_event_get_code(e)==LV_EVENT_CLICKED) screen_back(); }

/* ---- screen ------------------------------------------------------------- */
static lv_obj_t *icon_btn(lv_obj_t *root, int x, int y, int w, const char *sym,
                          lv_color_t fg, lv_event_cb_t cb){
    lv_obj_t *b = lv_button_create(root);
    lv_obj_remove_style_all(b);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, w, 40);
    lv_obj_set_ext_click_area(b, 6);   /* easier hit, esp. the 36px menu button */
    lv_obj_set_style_radius(b, 14, 0);
    lv_obj_set_style_bg_color(b, TC(SURFACE), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, TC(SURFACE_RAISED), LV_STATE_PRESSED);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, sym);
    lv_obj_set_width(l, w - 8);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(l, fg, 0);
    lv_obj_center(l);
    return b;
}
void plview_create(lv_obj_t *root){
    lv_obj_set_style_bg_color(root, TC(CANVAS), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);

    g_title_lbl = ui_header_cb(root, "Playlist", back_cb);   /* shared header; dynamic title = playlist name */
    lv_obj_set_style_text_align(g_title_lbl, LV_TEXT_ALIGN_CENTER, 0);
    if(!ui_edge_nav()) lv_obj_align(g_title_lbl, LV_ALIGN_TOP_MID, 0, 30);   /* edge-nav themes place their own */

    /* Play / Shuffle / 3-dots row */
    g_play_btn    = icon_btn(root, 62,  68, 96, LV_SYMBOL_PLAY "  Play",   TC(STATUS_SUCCESS), play_cb);
    g_shuffle_btn = icon_btn(root, 164, 68, 96, LV_SYMBOL_SHUFFLE "  Shuffle", TC(TEXT_PRIMARY), shuffle_cb);
    /* "more/options" (edit/rename/delete). Drawn "•••" - Apple-Music-style More - because the
     * bullet glyph isn't in this tree's montserrat; LV_SYMBOL_LIST misread as a track list. */
    lv_obj_t *menu_btn = icon_btn(root, 266, 68, 36, "", TC(TEXT_LYRICS), open_menu_cb);
    g_menu_btn = menu_btn;
    g_clear_btn = icon_btn(root, 246, 68, 56, "Clear", TC(TEXT_PRIMARY), clear_cb);   /* the Queue only */
    lv_obj_add_flag(g_clear_btn, LV_OBJ_FLAG_HIDDEN);
    for(int i=0;i<3;i++){
        lv_obj_t *d = lv_obj_create(menu_btn);
        lv_obj_remove_style_all(d);
        lv_obj_clear_flag(d, LV_OBJ_FLAG_CLICKABLE);   /* decorative: let taps fall through to the button */
        lv_obj_set_size(d, 4, 4);
        lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(d, TC(TEXT_LYRICS), 0);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
        lv_obj_align(d, LV_ALIGN_CENTER, (i-1)*7, 0);   /* -7 / 0 / +7 px */
    }

    g_song_list = lv_obj_create(root);
    lv_obj_remove_style_all(g_song_list);
    lv_obj_set_pos(g_song_list, 41, 118);   /* its 268 px rows (5 px in) land centred on the screen */
    lv_obj_set_size(g_song_list, 290, 226);
    lv_obj_set_style_pad_bottom(g_song_list, 44, 0);   /* last row scrolls clear of the round bottom bezel */
    lv_obj_set_style_bg_opa(g_song_list, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(g_song_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(g_song_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(g_song_list, 6, 0);
    lv_obj_set_scroll_dir(g_song_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(g_song_list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(g_song_list, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_add_event_cb(g_song_list, plv_scroll_cb, LV_EVENT_SCROLL, NULL);
    if(th_braun() && g_song_list){                                  /* Braun: the grille, the list on the segment, styled rows */
        br_face(root); lv_obj_update_layout(g_song_list);
        int y = lv_obj_get_y(g_song_list) - 6; if(y < 40) y = 40;
        lv_obj_move_to_index(br_segment(root, y), 1);
        curvelist_braun_watch(g_song_list);
    }
}

lv_obj_t *playlistview_scroller(void){ return g_song_list; }
