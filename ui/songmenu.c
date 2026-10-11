/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
/* Long-press menus in the orbit style (like Quick Settings and the file operations):
 *   Library song:   Add to queue, Playlist, Favourite, Album, Artist, Info, Tags
 *   folder song:    Add to queue, Playlist, Favourite, Info, Tags, Rename, Copy / Move, Delete
 *   folder:         Add to queue, Playlist, Tags, Rename, Copy / Move, Delete
 *   album (Library, fork): Play, Shuffle, Add to queue, Playlist, Tags; artist / genre: Play, Shuffle, Add to queue, Playlist
 * The name sits at the top; the hub in the middle cancels. Favourite is filled red when it already is one. */
#include "screens.h"
#include "theme.h"
#include "braun.h"
#include "orbit.h"
#include "musicdb.h"
#include "ipc.h"
#include <stdio.h>
#include <string.h>

enum { A_QUEUE, A_PLAYLIST, A_FAV, A_ALBUM, A_ARTIST, A_INFO, A_TAGS, A_RENAME, A_COPYMOVE, A_DELETE, A_PLAY, A_SHUFFLE, A_N };
static lv_obj_t *g_ov;
static orbit_t g_orb;
static int g_act[8], g_nact, g_is_dir, g_fav;
static char g_path[600], g_dir[600], g_name[256];
static mdb_song_t g_song;
static int g_have_song;
static void (*g_done)(void);
static char g_gcol[8], g_gval[256];          /* group menu: SONG column + value */
static void (*g_gplay)(int shuffle);         /* group menu: the list's own play action */

static void close_ov(void){ if(g_ov){ lv_obj_delete(g_ov); g_ov = NULL; } }
static int is_current(const char *path){ track_state_t st; ipc_get_state(&st); return st.have_track && !strcmp(st.path, path); }
static void pick(int i){
    if(i < 0 || i >= g_nact) return;
    int a = g_act[i];
    close_ov();
    switch(a){
        case A_QUEUE: {
            int r = g_gcol[0] ? queue_add_group(g_gcol, g_gval) : g_is_dir ? queue_add_folder(g_path) : queue_add_path(g_path);
            if(g_is_dir || g_gcol[0]){ char t[48]; snprintf(t, sizeof t, r > 0 ? "Added %d songs to the queue" : "Nothing new to add", r); ui_toast(t); }
            else ui_toast_icon(LV_SYMBOL_LIST, ui_current_accent(), r > 0 ? "Added to the queue" : r < 0 ? "The queue is full" : "Can't queue that");
        } break;
        case A_PLAYLIST:
            if(g_gcol[0]) plpick_set_group(g_gcol, g_gval);
            else if(g_is_dir) plpick_set_folder(g_path); else plpick_set_song(g_path, 0, NULL);
            screen_show(SCR_PLPICK); break;
        case A_FAV: {
            int on = !g_fav;
            if(is_current(g_path)) ui_set_favorite(on);                      /* keep the player in step */
            else if(!mdb_set_favorite_path(g_path, on)){ ui_toast("Couldn't change favourites"); break; }
            ui_toast(on ? "Added to Favourites" : "Removed from Favourites");
        } break;
        case A_ALBUM:  if(g_have_song && g_song.album[0]){ library_open_album(g_song.album); screen_show(SCR_LIBRARY); } else ui_toast("No album tag"); break;
        case A_ARTIST: if(g_have_song && g_song.artist[0]){ library_open_artist(g_song.artist); screen_show(SCR_LIBRARY); } else ui_toast("No artist tag"); break;
        case A_INFO: {
            track_state_t st; memset(&st, 0, sizeof st);
            if(is_current(g_path)){ ipc_get_state(&st); songinfo_unpin(); songinfo_set(&st); screen_show(SCR_SONGINFO); break; }
            st.have_track = 1; snprintf(st.path, sizeof st.path, "%.250s", g_path);
            if(g_have_song){ snprintf(st.title, sizeof st.title, "%s", g_song.title); snprintf(st.artist, sizeof st.artist, "%s", g_song.artist);
                             snprintf(st.album, sizeof st.album, "%s", g_song.album); st.duration_ms = g_song.dur_ms; }
            else snprintf(st.title, sizeof st.title, "%.150s", g_name);
            songinfo_show_song(&st);
        } break;
        case A_TAGS:
            if(g_gcol[0]){ if(!strcmp(g_gcol, "ALBUM")) tagfix_album(g_gval); break; }   /* an album: every track of it */
            if(g_is_dir) tagfix_folder(g_path);
            else tagfix_song(g_path, g_have_song ? g_song.title : g_name, g_have_song ? g_song.artist : "", g_have_song ? g_song.album : "", g_have_song ? g_song.dur_ms : 0);
            break;
        case A_RENAME:   fileops_action(g_dir, g_name, g_is_dir, g_done, 0); break;
        case A_COPYMOVE: fileops_action(g_dir, g_name, g_is_dir, g_done, 1); break;
        case A_DELETE:   fileops_action(g_dir, g_name, g_is_dir, g_done, 2); break;
        case A_PLAY:     if(g_gplay) g_gplay(0); break;
        case A_SHUFFLE:  if(g_gplay) g_gplay(1); break;
    }
}
static void hub_cb(lv_event_t *e){ (void)e; close_ov(); }
static void open_menu(const char *title, const int *acts, int n){
    close_ov();
    g_ov = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(g_ov);
    lv_obj_set_size(g_ov, 360, 360);
    lv_obj_set_style_bg_color(g_ov, TC(CANVAS), 0);
    lv_obj_set_style_bg_opa(g_ov, 235, 0);
    lv_obj_add_flag(g_ov, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(g_ov, LV_OBJ_FLAG_SCROLLABLE);
    static const char *const GLYPH[A_N] = { LV_SYMBOL_LIST, LV_SYMBOL_PLUS, TH_IC_HEART, LV_SYMBOL_IMAGE, LV_SYMBOL_HOME, LV_SYMBOL_FILE,
                                           LV_SYMBOL_DOWNLOAD, LV_SYMBOL_EDIT, LV_SYMBOL_COPY, LV_SYMBOL_TRASH, LV_SYMBOL_PLAY, LV_SYMBOL_SHUFFLE };
    static const char *const CAP[A_N] = { "Queue", "Playlist", "Favourite", "Album", "Artist", "Info", "Tags", "Rename", "Copy/Move", "Delete", "Play", "Shuffle" };
    orbit_item_t it[8];
    g_nact = n > 8 ? 8 : n;
    for(int i = 0; i < g_nact; i++){ g_act[i] = acts[i]; it[i].glyph = GLYPH[acts[i]]; it[i].cap = CAP[acts[i]]; }
    orbit_create(&g_orb, g_ov, it, g_nact, -90, pick);
    for(int i = 0; i < g_nact; i++){
        if(g_act[i] == A_FAV){ lv_obj_set_style_text_font(g_orb.icon[i], &font_theme_20, 0); orbit_set_on(&g_orb, i, g_fav, ui_current_accent()); }
        if(g_act[i] == A_DELETE && !th_braun()) lv_obj_set_style_text_color(g_orb.icon[i], ui_current_accent(), 0);
    }
    orbit_hub_create(&g_orb, g_ov, hub_cb, g_gcol[0] ? LV_SYMBOL_LIST : g_is_dir ? LV_SYMBOL_DIRECTORY : LV_SYMBOL_AUDIO, "Cancel");
    lv_obj_t *t = lv_label_create(g_ov);
    lv_label_set_text(t, title);
    lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
    lv_obj_set_width(t, 220);
    lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(t, ui_font_cjk(18), 0);
    lv_obj_set_height(t, lv_font_get_line_height(ui_font_cjk(18)));     /* fork: one line + "...", never wraps into the orbit */
    lv_obj_set_style_text_color(t, TC(TEXT_PRIMARY), 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 10);
    orbit_popup_polish(&g_orb);
    if(g_fav) for(int i=0;i<g_nact;i++) if(g_act[i]==A_FAV) orbit_set_on(&g_orb,i,1,ui_current_accent());
    disco_menu_glass(&g_orb, g_ov);                                   /* Disco: glass over the cover backdrop */
}
static void load_song(const char *path){
    snprintf(g_path, sizeof g_path, "%s", path);
    g_have_song = mdb_song_by_path(path, &g_song);
    if(is_current(path)){ track_state_t st; ipc_get_state(&st); g_fav = st.is_favorite; }   /* the player's own flag */
    else g_fav = mdb_is_favorite_path(path);
}
void songmenu_open_song(const char *path){                   /* Library */
    if(!path || !path[0]) return;
    g_gcol[0] = 0; g_is_dir = 0; g_done = NULL; load_song(path);
    const char *sl = strrchr(path, '/'); snprintf(g_name, sizeof g_name, "%s", sl ? sl + 1 : path);
    static const int A[7] = { A_QUEUE, A_PLAYLIST, A_FAV, A_ALBUM, A_ARTIST, A_INFO, A_TAGS };
    open_menu(g_have_song && g_song.title[0] ? g_song.title : g_name, A, 7);
}
void songmenu_open_file(const char *dir, const char *name, void (*done)(void)){   /* folder browser: a song */
    char p[600]; snprintf(p, sizeof p, "%s/%s", dir, name);
    g_gcol[0] = 0; g_is_dir = 0; g_done = done; load_song(p);
    snprintf(g_dir, sizeof g_dir, "%s", dir); snprintf(g_name, sizeof g_name, "%s", name);
    static const int A[8] = { A_QUEUE, A_PLAYLIST, A_FAV, A_INFO, A_TAGS, A_RENAME, A_COPYMOVE, A_DELETE };
    open_menu(name, A, 8);
}
void songmenu_open_folder(const char *dir, const char *name, void (*done)(void)){ /* folder browser: a folder */
    snprintf(g_path, sizeof g_path, "%s/%s", dir, name);
    snprintf(g_dir, sizeof g_dir, "%s", dir); snprintf(g_name, sizeof g_name, "%s", name);
    g_gcol[0] = 0; g_is_dir = 1; g_done = done; g_have_song = 0; g_fav = 0;
    static const int A[6] = { A_QUEUE, A_PLAYLIST, A_TAGS, A_RENAME, A_COPYMOVE, A_DELETE };
    open_menu(name, A, 6);
}
void songmenu_open_group(const char *col, const char *val, const char *title, void (*play)(int shuffle)){   /* Library: album / artist / genre */
    if(!col || !val || !val[0]) return;
    snprintf(g_gcol, sizeof g_gcol, "%s", col); snprintf(g_gval, sizeof g_gval, "%s", val);
    g_gplay = play; g_is_dir = 0; g_done = NULL; g_have_song = 0; g_fav = 0; g_path[0] = 0;
    static const int A[5] = { A_PLAY, A_SHUFFLE, A_QUEUE, A_PLAYLIST, A_TAGS };   /* Tags: albums only */
    open_menu(title && title[0] ? title : val, A, strcmp(col, "ALBUM") ? 4 : 5);
}
