#include "onboarding.h"
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
/* Long-press menus in the orbit style (like Quick Settings and the file operations):
 *   Library song:   Add to queue, Playlist, Favourite, Album, Artist, Info, Tags
 *   folder song:    Add to queue, Playlist, Favourite, Info, Tags, Rename, Copy / Move, Delete
 *   folder:         Add to queue, Playlist, Tags, Rename, Copy / Move, Delete
 *   album (Library, fork): Play, Shuffle, Add to queue, Playlist, Tags, Mood; artist: Play, Shuffle, Add to queue, Playlist, Mood;
 *   genre: Play, Shuffle, Add to queue, Playlist (Mood: SongSphere's tag page, when SongSphere is on)
 * The name sits at the top; the hub in the middle cancels. Favourite is filled red when it already is one. */
#include "screens.h"
#include "anim.h"
#include "zune_thumbs.h"
#include "theme.h"
#include "braun.h"
#include "orbit.h"
#include "musicdb.h"
#include "ipc.h"
#include <stdio.h>
#include <string.h>

enum { A_QUEUE, A_PLAYLIST, A_FAV, A_ALBUM, A_ARTIST, A_INFO, A_TAGS, A_RENAME, A_COPYMOVE, A_DELETE, A_PLAY, A_SHUFFLE, A_MOOD, A_SELECT, A_LYRICS, A_VQUEUE, A_N };   /* LYRICS, VQUEUE: the song ring only */
static void (*g_sel_next)(void), (*g_sel)(void);   /* Zune: "Select" starts picking from this item (the Library sets it per hold) */
void songmenu_offer_select(void (*fn)(void)){ g_sel_next = fn; }
static lv_obj_t *g_ov;
static orbit_t g_orb;
static int g_act[8], g_nact, g_is_dir, g_fav;
static char g_path[600], g_dir[600], g_name[256];
static mdb_song_t g_song;
static int g_have_song;
static void (*g_done)(void);
static char g_gcol[8], g_gval[256];          /* group menu: SONG column + value */
static void (*g_gplay)(int shuffle);         /* group menu: the list's own play action */

static int g_song_ring;   /* this menu is a song's round menu (Disco, Squash, Null) */
static void close_ov(void){ if(g_ov){ lv_obj_delete(g_ov); g_ov = NULL; } zthumb_force(0); }
void songmenu_close(void){ if(g_ov){ lv_obj_delete_async(g_ov); g_ov = NULL; } }   /* a screen change: never act on the old row */
static int is_current(const char *path){ track_state_t st; ipc_get_state(&st); return st.have_track && !strcmp(st.path, path); }
static void pick(int i){
    if(i < 0 || i >= g_nact) return;
    int a = g_act[i];
    close_ov();
    switch(a){
        case A_QUEUE: {
            int r = g_gcol[0] ? queue_add_group(g_gcol, g_gval) : g_is_dir ? queue_add_folder(g_path) : queue_add_path(g_path);
            if(g_is_dir || g_gcol[0]){ char t[48];
                snprintf(t, sizeof t, r > 0 ? "Added %d songs to the queue" : r == -2 ? "Library busy - try again" : r == -1 ? "The queue is full" : "Nothing new to add", r);
                ui_toast(t); }
            else ui_toast_icon(LV_SYMBOL_LIST, ui_current_accent(), r > 0 ? "Added to the queue" : r < 0 ? "The queue is full" : "Can't queue that");
        } break;
        case A_LYRICS:
            if(is_current(g_path)) lyrics_open(); else ui_toast("Lyrics show for the song that's playing");
            break;
        case A_VQUEUE: queue_open(); break;
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
        case A_SELECT:   if(g_sel) g_sel(); break;
        case A_MOOD:     sphere_ui_tag(!strcmp(g_gcol, "ALBUM") ? 1 : 2, g_gval, g_gval); break;   /* SongSphere: where it sits */
    }
}
static void hub_cb(lv_event_t *e){ (void)e; close_ov(); }
/* Zune (Figma 32): the item small at the top (its cover beside it), the actions as a dial; tap off the dial = close */
#include "zune_dial.h"
#include "theme_kit.h"
LV_FONT_DECLARE(font_zune_regular_16)
static void zpick_async(void *u){ pick((int)(intptr_t)u); }
static void zpick(int i){ lv_async_call(zpick_async, (void *)(intptr_t)i); }   /* the dial goes with the overlay: not from its own click */
static void zune_menu(const char *title, const int *acts, int n){
    g_ov = lv_obj_create(lv_layer_top()); lv_obj_remove_style_all(g_ov); kit_keep(g_ov);
    lv_obj_set_size(g_ov, 360, 360);
    lv_obj_set_style_bg_color(g_ov, TC(CANVAS), 0); lv_obj_set_style_bg_opa(g_ov, LV_OPA_COVER, 0);
    lv_obj_add_flag(g_ov, LV_OBJ_FLAG_CLICKABLE); lv_obj_clear_flag(g_ov, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(g_ov, hub_cb, LV_EVENT_CLICKED, NULL);
    static const char *const ZCAP[A_N] = { "add to queue", "add to playlist", "favourite", "go to album", "go to artist", "info", "edit tags",
                                           "rename", "copy / move", "delete", "play", "shuffle", "mood", "select" };
    static char lab[10][24]; const char *lb[10];
    g_nact = n > 9 ? 9 : n;
    for(int i = 0; i < g_nact; i++) g_act[i] = acts[i];
    g_sel = g_sel_next; g_sel_next = NULL;
    if(g_sel && g_nact < 9) g_act[g_nact++] = A_SELECT;
    int focus = 0;
    for(int i = 0; i < g_nact; i++){
        snprintf(lab[i], sizeof lab[i], "%s", g_act[i] == A_FAV && g_fav ? "unfavourite" : ZCAP[g_act[i]]); lb[i] = lab[i];
        if(g_act[i] == A_QUEUE && !focus) focus = i;
    }
    zdial_build(g_ov, lb, g_nact, focus, zpick, 56, "songmenu.zune");
    for(int i = 0; i < g_nact; i++) if(g_act[i] == A_DELETE) zdial_set_color(i, TC(ACCENT_PRIMARY));
    lv_obj_t *t = lv_label_create(g_ov); kit_keep(t);
    char b[300]; snprintf(b, sizeof b, "%s%s%s", title, g_have_song && g_song.artist[0] ? " \xC2\xB7 " : "", g_have_song ? g_song.artist : "");
    lv_label_set_text(t, b); lv_label_set_long_mode(t, LV_LABEL_LONG_SCROLL_CIRCULAR); lv_obj_set_width(t, 200);
    lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(t, zune_font(&font_zune_regular_16), 0); lv_obj_set_style_text_color(t, TC(TEXT_MUTED), 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 26);
    if(screen_current()==SCR_LIBRARY)onboarding_discovered(HINT_LIBRARY);
}
#include "track_sheet.h"
static void sheet_pick(lv_event_t *e){pick((int)(intptr_t)lv_event_get_user_data(e));}
static void open_menu(const char *title, const int *acts, int n){
    close_ov();
    if(th_zune()){ zune_menu(title, acts, n); return; }
    if(!g_song_ring && !g_is_dir && !g_gcol[0] && (th_disco()||th_simple()||th_null())){
        static const int order[]={A_FAV,A_ALBUM,A_ARTIST,A_PLAYLIST,A_QUEUE,A_INFO,A_TAGS,A_RENAME,A_COPYMOVE,A_DELETE};
        static const char *const names[]={"Favourite","Go to album","Go to artist","Add to playlist","Add to queue","Song info","Tags","Rename","Copy/Move","Delete"};
        static const char *const icons[]={TH_IC_HEART,LV_SYMBOL_IMAGE,LV_SYMBOL_HOME,LV_SYMBOL_PLUS,LV_SYMBOL_LIST,LV_SYMBOL_FILE,LV_SYMBOL_EDIT,LV_SYMBOL_EDIT,LV_SYMBOL_COPY,LV_SYMBOL_TRASH};
        const char *labels[10],*glyphs[10];g_nact=0;
        for(int k=0;k<10;k++)for(int j=0;j<n;j++)if(acts[j]==order[k]&&g_nact<8){g_act[g_nact]=order[k];labels[g_nact]=names[k];glyphs[g_nact++]=icons[k];}
        g_sel_next=NULL;g_sel=NULL;
        g_ov=lv_obj_create(lv_layer_top());lv_obj_remove_style_all(g_ov);kit_keep(g_ov);lv_obj_set_size(g_ov,360,360);
        const void *cover=is_current(g_path)?ui_current_cover_dsc():NULL;
        track_sheet_build(g_ov,title,g_have_song?g_song.artist:"",cover,labels,glyphs,g_nact,sheet_pick,hub_cb);
        anim_pop_in(g_ov);                       /* the sheet fades and rises in */
        return;
    }
    g_ov = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(g_ov);
    lv_obj_set_size(g_ov, 360, 360);
    lv_obj_set_style_bg_color(g_ov, TC(CANVAS), 0);
    lv_obj_set_style_bg_opa(g_ov, 235, 0);
    lv_obj_add_flag(g_ov, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(g_ov, LV_OBJ_FLAG_SCROLLABLE);
    static const char *const GLYPH[A_N] = { LV_SYMBOL_LIST, LV_SYMBOL_PLUS, TH_IC_HEART, LV_SYMBOL_IMAGE, LV_SYMBOL_HOME, LV_SYMBOL_FILE,
                                           LV_SYMBOL_DOWNLOAD, LV_SYMBOL_EDIT, LV_SYMBOL_COPY, LV_SYMBOL_TRASH, LV_SYMBOL_PLAY, LV_SYMBOL_SHUFFLE, LV_SYMBOL_GPS, LV_SYMBOL_OK };
    static const char *const CAP[A_N] = { "Queue", "Playlist", "Favourite", "Album", "Artist", "Info", "Tags", "Rename", "Copy/Move", "Delete", "Play", "Shuffle", "Mood", "Select" };
    orbit_item_t it[8];
    g_nact = n > 8 ? 8 : n;
    for(int i = 0; i < g_nact; i++){ g_act[i] = acts[i]; it[i].glyph = GLYPH[acts[i]]; it[i].cap = CAP[acts[i]]; }
    if(g_song_ring){   /* the song menu's own wording */
        static const char *const SCAP[A_N] = { [A_QUEUE] = "+Queue", [A_PLAYLIST] = "+Playlist", [A_FAV] = "Favourite", [A_ALBUM] = "Album",
                                               [A_ARTIST] = "Artist", [A_INFO] = "Info", [A_TAGS] = "Tags", [A_LYRICS] = "Lyrics", [A_VQUEUE] = "View queue" };
        static const char *const NCAP[A_N] = { [A_QUEUE] = "+Queue", [A_PLAYLIST] = "+List", [A_FAV] = "Fav", [A_ALBUM] = "Album",
                                               [A_ARTIST] = "Artist", [A_INFO] = "Info", [A_TAGS] = "Tags", [A_LYRICS] = "Lyrics", [A_VQUEUE] = "Queue" };   /* Null: its wide face */
        for(int i = 0; i < g_nact; i++){ const char *const *c = th_null() ? NCAP : SCAP; if(c[g_act[i]]) it[i].cap = c[g_act[i]];
            if(g_act[i] == A_LYRICS) it[i].glyph = LV_SYMBOL_AUDIO;
            if(g_act[i] == A_VQUEUE) it[i].glyph = th_null() ? LV_SYMBOL_RIGHT : LV_SYMBOL_BARS; }
    }
    g_sel = g_sel_next; g_sel_next = NULL;                                   /* offered for this menu only */
    if(g_sel && th_zune() && g_nact < 8){ g_act[g_nact] = A_SELECT; it[g_nact].glyph = GLYPH[A_SELECT]; it[g_nact].cap = CAP[A_SELECT]; g_nact++; }
    orbit_create(&g_orb, g_ov, it, g_nact, -90, pick);
    for(int i = 0; i < g_nact; i++){
        if(g_act[i] == A_FAV){ lv_obj_set_style_text_font(g_orb.icon[i], &font_theme_20, 0); orbit_set_on(&g_orb, i, g_fav, ui_current_accent()); }
        if(g_act[i] == A_DELETE && !th_braun_family()) lv_obj_set_style_text_color(g_orb.icon[i], ui_current_accent(), 0);
    }
    if(library_lift() || g_song_ring) orbit_hub_create(&g_orb, g_ov, hub_cb, LV_SYMBOL_LEFT, "Back");   /* Back, not Cancel */
    else orbit_hub_create(&g_orb, g_ov, hub_cb, g_gcol[0] ? LV_SYMBOL_LIST : g_is_dir ? LV_SYMBOL_DIRECTORY : LV_SYMBOL_AUDIO, "Cancel");
    if(g_song_ring){   /* the hub: the song's cover with a back arrow */
        lv_obj_t *img = orbit_hub_cover_back(&g_orb);
        if(is_current(g_path)) orbit_hub_cover_set(img, ui_current_cover_dsc());
        else if(g_have_song && g_song.id > 0){ lv_obj_add_flag(img, LV_OBJ_FLAG_HIDDEN); zthumb_force(1); zthumb_bind(img, g_song.id, ORBIT_HUB); }
        else lv_obj_add_flag(img, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_t *t = lv_label_create(g_ov);
    lv_label_set_text(t, title);
    lv_label_set_long_mode(t, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(t, 220);
    lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
    const lv_font_t *tf = th_braun2() ? br_font(16, 1) : ui_font_cjk(18);   /* Braun 2.0: Inter, international fallback kept */
    lv_obj_set_style_text_font(t, tf, 0);
    lv_obj_set_height(t, lv_font_get_line_height(tf));     /* fork: one line + "...", never wraps into the orbit */
    lv_obj_set_style_text_color(t, TC(TEXT_PRIMARY), 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 10);
    if(g_song_ring) lv_obj_add_flag(t, LV_OBJ_FLAG_HIDDEN);   /* the wider ring reaches the top: the cover says which song */
    if(g_song_ring) orbit_spread();
    orbit_popup_polish(&g_orb);
    if(g_song_ring){ void disco_vec_icon(lv_obj_t *, const char *, int);   /* album: a record; artist: a microphone */
        for(int i = 0; i < g_nact; i++){ if(g_act[i] == A_ALBUM) disco_vec_icon(g_orb.icon[i], "@vinyl", 24);
                                         if(g_act[i] == A_ARTIST) disco_vec_icon(g_orb.icon[i], "@mic", 24); } }
    if(g_fav) for(int i=0;i<g_nact;i++) if(g_act[i]==A_FAV) orbit_set_on(&g_orb,i,1,ui_current_accent());
    if(screen_current()==SCR_LIBRARY)onboarding_discovered(HINT_LIBRARY);
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
    static const int R[8] = { A_FAV, A_LYRICS, A_ALBUM, A_ARTIST, A_VQUEUE, A_INFO, A_QUEUE, A_PLAYLIST };   /* as the Music pop-up, clockwise from 12 */
    g_song_ring = library_lift() > 0 || th_braun2();   /* Braun 2.0: the full song set (Lyrics, View queue) as Squash / Disco / Null, in its own keys */
    open_menu(g_have_song && g_song.title[0] ? g_song.title : g_name, g_song_ring ? R : A, g_song_ring ? 8 : 7);
    g_song_ring = 0;
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
    int A[6] = { A_PLAY, A_SHUFFLE, A_QUEUE, A_PLAYLIST }, n = 4;
    int album = !strcmp(col, "ALBUM");
    if(album) A[n++] = A_TAGS;                                                    /* Tags: albums only */
    if(sphere_ui_enabled() && (album || !strcmp(col, "ARTIST"))) A[n++] = A_MOOD; /* Mood: albums and artists */
    open_menu(title && title[0] ? title : val, A, n);
}
