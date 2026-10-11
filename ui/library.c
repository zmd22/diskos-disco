/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "screens.h"
#include "theme.h"
#include "braun.h"
#include "curvelist.h"
#include "theme_kit.h"
#include "folderbrowser.h"
#include "books.h"
#include "musicdb.h"
#include "fwcaps.h"
#include "artcache.h"
#include "azjump.h"
#include "ipc.h"
#include <stdint.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "config.h"
#include "i18n.h"

enum { VIEW_MENU, VIEW_SONGS, VIEW_ALBUMS, VIEW_ARTISTS, VIEW_PLAYLISTS, VIEW_FAVS, VIEW_GENRES, VIEW_GROUP, VIEW_MOSTPLAYED, VIEW_RECENT, VIEW_HISTORY, VIEW_ARTIST_ALBUMS, VIEW_COUNT };
#define CAT_FOLDERS 1000   /* menu sentinel: opens the folder browser (a screen, not a library view) */
#define CAT_BOOKS   1001   /* menu sentinel: opens the Books (audiobook) screen */

#define ROW_H 58                     /* a size up: 18 px titles, 16 px details */
#define LIST_Y 70
#define LIST_H 252

static lv_obj_t *g_list;
static lv_obj_t *g_title;
static azjump_t g_az;         /* the "A - Z" pill + letter grid (azjump.c) */
static lv_obj_t *g_lhint = NULL, *g_lhint_lbl = NULL;  /* rim-scroll A-Z position hint */
static uint32_t  g_lhint_tick = 0;
static char      g_lhint_ch = 0;
static int g_view = VIEW_MENU;
static int g_drill_kind = 0;  /* 1 album, 2 artist, 3 genre */
/* Artist / Genre drills open on their ALBUMS (parity L23): level 1 = the album list (an "All Songs" row first), level 2 =
 * one album's songs by that artist / in that genre, level 0 = the flat all-songs drill (the old view; also a deep link). */
static int  g_scope_lvl = 0;
static int  g_scope_from_albums = 0;   /* the flat list was opened from the albums list ("All Songs"): Back returns there */
static char g_scope_album[MDB_STR];
static int  g_scope_total = 0;   /* songs in the whole scope (the "All Songs" row's count) */
static int g_deeplink = 0;    /* drill opened from the NP hub -> back leaves the Library */
static int g_np_jump = 0;     /* opened from Now Playing's title: the back SWIPE returns straight there */
static int g_has_header = 0;   /* a Play All / Shuffle row is the first list child */
static int g_hdr_extra_px = 0; /* extra leading height above the rows (the album cover header), for scroll math */
static char g_drill[MDB_STR];
static char g_artist[MDB_STR];   /* artist whose albums VIEW_ARTIST_ALBUMS lists (Artist > Album > Track) */
static char g_focus_artist[MDB_STR];  /* one-shot: highlight this artist when going back to Artists */
static int  g_from_artist = 0;   /* the current VIEW_GROUP drill was opened from that artist's album list */
static library_song_click_cb_t g_song_cb;

/* All per-row buffers are dynamically sized to the library song count (g_alloc_n),
 * so there is NO fixed song/group cap - every view (songs, albums≤N, artists≤N,
 * genres≤N, group-songs≤N) fits. Favourites can exceed N (MY_LOVE keeps a favourite whose song left the library),
 * so g_favs grows to the favourite count on its own (g_fav_cap). Allocated after mdb_load(); regrown after a
 * rescan (library_ensure_capacity) and for favourites (lib_ensure_fav_cap). */
static int  g_alloc_n = 0;              /* song-bounded buffers (g_buf; g_favs for Most-Played/Recent) */
static int  g_fav_cap = 0;              /* g_favs slots: >= g_alloc_n, grown past it for favourites (lib_ensure_fav_cap) */
static int  g_grp_cap = 0;             /* group buffers: artists are tokenized so DISTINCT artists can exceed the
                                        * song count - give the album/artist/genre + g_first buffers headroom */
static char *g_first = NULL;            /* per-row first letter, drives the A-Z scrubber (sized to g_grp_cap) */
static int  g_count;

/* group scratch (album/artist/genre views) */
static char (*g_gnames)[MDB_STR]  = NULL;
static char (*g_gartists)[MDB_STR] = NULL;
static int  *g_gcounts = NULL;
static void lib_ensure_group_cap(int need);   /* grow group buffers to an exact count (artist view) */
static void lib_ensure_fav_cap(int need);     /* grow g_favs (+ g_first) for favourites, which can outnumber songs */
static mdb_song_t *g_favs = NULL;
static char (*g_plnames)[MDB_STR] = NULL;   /* dynamic: grown to the real playlist count (no 64 cap) */
static long *g_plids = NULL;
static int   g_plcap = 0;                    /* allocated slots in g_plnames/g_plids */

/* Streamed list fill: building all song rows at once froze the UI for seconds, so
 * we render the first screenful immediately and stream the rest in via a timer
 * (non-blocking). g_buf holds the song pointers for SONGS/GROUP. */
static const mdb_song_t **g_buf = NULL;
static int g_fill_i, g_fill_n;
static lv_timer_t *g_fill_timer;

static void library_reload(void);
static lv_obj_t *g_position, *g_position_label;
static uint32_t g_position_tick;
static int g_position_fading;
static int g_restoring, g_restore_y;
#define POSITION_KEY (MDB_STR * 3 + 96)
static struct { char key[POSITION_KEY]; int y; } g_positions[12];
static char g_position_key[POSITION_KEY];
static unsigned g_position_next;
static void position_key(char *out){
    const char *drill=g_view==VIEW_GROUP?g_drill:"";
    const char *artist=g_view==VIEW_ARTIST_ALBUMS?g_artist:"";
    const char *album=g_view==VIEW_GROUP && g_scope_lvl==2?g_scope_album:"";
    snprintf(out,POSITION_KEY,"%d/%d/%d/%zu:%s/%zu:%s/%zu:%s",g_view,
        g_view==VIEW_GROUP?g_drill_kind:0,g_view==VIEW_GROUP?g_scope_lvl:0,
        strlen(drill),drill,strlen(artist),artist,strlen(album),album);
}
static void position_tick(void){
    if(g_restoring || !g_position || g_count < 12) return;
    int idx=(lv_obj_get_scroll_y(g_list)-g_hdr_extra_px)/(ROW_H+4)-g_has_header;
    if(idx<0)idx=0; if(idx>=g_count)idx=g_count-1;
    char b[32]; snprintf(b,sizeof b,"%d / %d",idx+1,g_count);
    if(strcmp(lv_label_get_text(g_position_label),b))lv_label_set_text(g_position_label,b);
    lv_anim_delete(g_position,NULL);lv_obj_set_style_opa(g_position,LV_OPA_COVER,0);
    lv_obj_remove_flag(g_position,LV_OBJ_FLAG_HIDDEN);g_position_tick=lv_tick_get();g_position_fading=0;
}
static void focus_clear(void);
/* album order: disc, then track number (untagged tracks after tagged ones), then title */
static int track_cmp(const void *pa, const void *pb){
    const mdb_song_t *a = *(const mdb_song_t *const *)pa, *b = *(const mdb_song_t *const *)pb;
    int da = a->disc > 0 ? a->disc : 1, db = b->disc > 0 ? b->disc : 1;
    if(da != db) return da < db ? -1 : 1;
    int ta = a->track > 0 ? a->track : 1 << 30, tb = b->track > 0 ? b->track : 1 << 30;
    if(ta != tb) return ta < tb ? -1 : 1;
    return strcasecmp(a->title, b->title);
}

static char first_letter(const char *s){
    while(*s==' ') s++;
    char c = toupper((unsigned char)*s);
    return (c>='A'&&c<='Z') ? c : '#';
}
static void fmt_dur(char *b, size_t n, int ms){
    if(ms<=0){ b[0]=0; return; }
    long long t=((long long)ms+500)/1000; snprintf(b,n,"%lld:%02lld", t/60, t%60);   /* no int overflow near INT32_MAX */
}

/* ---- rows --------------------------------------------------------------- */
/* ---- curved list: rows follow the circle ------------------------------------------------------------
 * Visible rows resize continuously while scrolling. Keep the existing width limits, panel clearance,
 * theme treatment and virtual row window; unchanged pixel geometry does no extra label/layout work. */
#define ROW_W_FULL 268
static void curve_apply(lv_obj_t *r, int w, int shift){
    int d = ROW_W_FULL - w;
    lv_obj_set_width(r, w);
    lv_obj_set_style_translate_x(r, shift, 0);                       /* Disco: clear of the menu picker */
    for(uint32_t k = 0; k < lv_obj_get_child_count(r); k++){   /* keep text inside the narrowed row */
        lv_obj_t *c = lv_obj_get_child(r, k);
        if(!lv_obj_check_type(c, &lv_label_class)) continue;
        intptr_t orig = (intptr_t)lv_obj_get_user_data(c);          /* original x<<16 | width, saved once */
        if(!orig){ orig = ((intptr_t)lv_obj_get_x(c) << 16) | (lv_obj_get_width(c) & 0xFFFF); lv_obj_set_user_data(c, (void *)orig); }
        int ox = (int)(orig >> 16), ow = (int)(orig & 0xFFFF);
        if(ox >= 150) lv_obj_set_x(c, ox - d);                       /* right-hand value: slide in */
        else          lv_obj_set_width(c, ow - d > 40 ? ow - d : 40);/* title / subtitle: narrow */
    }
}
static void curve_rows(void){
    if(!g_list) return;
    if(th_braun()){                                                  /* Braun: straight rows, restyled */
        for(uint32_t i = 0; i < lv_obj_get_child_count(g_list); i++){
            lv_obj_t *r = lv_obj_get_child(g_list, i);
            if(lv_obj_check_type(r, &lv_label_class) || lv_obj_has_flag(r, LV_OBJ_FLAG_IGNORE_LAYOUT)) continue;   /* every row (Ring curves only some) */
            lv_area_t a; lv_obj_get_coords(r, &a);
            if(a.y2 < -40 || a.y1 > 400) continue;
            if(lv_obj_has_flag(r, LV_OBJ_FLAG_USER_1) && lv_obj_get_width(r) != ROW_W_FULL) lv_obj_set_width(r, ROW_W_FULL);
            curvelist_braun_row(r);
        }
        return;
    }
    uint32_t n = lv_obj_get_child_count(g_list);
    int mode = curvelist_disco_scroll();
    for(uint32_t i = 0; i < n; i++){
        lv_obj_t *r = lv_obj_get_child(g_list, i);
        if(!lv_obj_has_flag(r, LV_OBJ_FLAG_USER_1)) continue;      /* only plain list rows curve */
        lv_area_t a; lv_obj_get_coords(r, &a);
        if(a.y2 < -40 || a.y1 > 400) continue;                     /* off-screen: nothing to do */
        int w, shift;
        if(!curvelist_disco_geom(mode, a.y1, a.y2, ROW_W_FULL, &w, &shift)){
        int dy = abs((a.y1 + a.y2) / 2 - 180) + ROW_H / 2;          /* the row's far edge sets the limit */
        int half = dy < 176 ? (int)sqrtf((float)(180 * 180 - dy * dy)) - 8 : 0;
        w = 2 * half;
        if(w > ROW_W_FULL) w = ROW_W_FULL;
        if(w < 186) w = 186;
        shift = 0; int L = disco_clear_left(a.y1, a.y2);
        { int R = disco_clear_right(a.y1, a.y2);                              /* Disco: clear of the closed sliver */
          if(R && 180 + w / 2 > R){ int lft = 180 - w / 2; w = R - lft; shift = (lft + R) / 2 - 180; } }
        if(L && 180 - w / 2 < L){ int right = 180 + w / 2; int nw = right - L; if(nw < 120) nw = 120; shift = L + nw / 2 - 180; w = nw; }   /* Disco: start right of the picker (never narrower than 120) */
        }
        intptr_t key = ((intptr_t)w << 16 | (unsigned)(shift + 32768)) + 1; /* exact width and translation */
        if((intptr_t)lv_obj_get_user_data(r) == key) continue;
        lv_obj_set_user_data(r, (void *)key);
        curve_apply(r, w, shift);
    }
}
/* ---- position dots: where you are in the whole list, on the left rim (opposite the A-Z button) ---- */
#define N_PDOTS 11
static lv_obj_t *g_pdot[N_PDOTS];
static int g_pdot_cur = -2;
static void pdots_update(void){
    if(th_braun()) return;                                           /* Braun: no position dots */
    if(!g_pdot[0] || !g_list) return;
    lv_obj_update_layout(g_list);
    int32_t sy = lv_obj_get_scroll_y(g_list), sb = lv_obj_get_scroll_bottom(g_list), tot = sy + sb;
    int idx = (tot <= 24) ? -1 : (int)((sy * (N_PDOTS - 1) + tot / 2) / tot);   /* -1: fits on one screen */
    if(idx > N_PDOTS - 1) idx = N_PDOTS - 1;
    if(idx == g_pdot_cur) return;
    g_pdot_cur = idx;
    for(int k = 0; k < N_PDOTS; k++){
        if(idx < 0){ lv_obj_add_flag(g_pdot[k], LV_OBJ_FLAG_HIDDEN); continue; }
        lv_obj_remove_flag(g_pdot[k], LV_OBJ_FLAG_HIDDEN);
        int on = (k == idx), sz = on ? 8 : 5;
        lv_obj_set_size(g_pdot[k], sz, sz);
        float a = (212.0f - 64.0f * k / (N_PDOTS - 1)) * 3.14159265f / 180.0f;     /* top to bottom */
        int x = (int)(180 + 168 * cosf(a)) - sz / 2;                  /* Disco Left: the dots move to the right rim */
        lv_obj_set_pos(g_pdot[k], disco_mx(x, sz), (int)(180 + 168 * sinf(a)) - sz / 2);
        lv_obj_set_style_bg_color(g_pdot[k], on ? TC(ACCENT_PRIMARY) : TC(PAGE_DOT_QUIET), 0);
    }
}
static void pdots_create(lv_obj_t *root){
    for(int k = 0; k < N_PDOTS; k++){
        g_pdot[k] = lv_obj_create(root);
        lv_obj_remove_style_all(g_pdot[k]);
        lv_obj_set_style_radius(g_pdot[k], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(g_pdot[k], LV_OPA_COVER, 0);
        lv_obj_clear_flag(g_pdot[k], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(g_pdot[k], LV_OBJ_FLAG_HIDDEN | LV_OBJ_FLAG_IGNORE_LAYOUT);
    }
    g_pdot_cur = -2;
}
static void virt_follow(void);
static void curve_scroll_cb(lv_event_t *e){ (void)e; virt_follow(); curve_rows(); pdots_update(); position_tick(); }

static lv_obj_t *base_row(void){
    lv_obj_t *r = lv_obj_create(g_list);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, 268, ROW_H);
    lv_obj_add_flag(r, LV_OBJ_FLAG_USER_1);                         /* eligible for the curve */
    lv_obj_set_style_radius(r, 10, 0);
    lv_obj_set_style_bg_color(r, TC(SURFACE), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(r, LV_OPA_70, LV_STATE_PRESSED);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
    return r;
}
static void row_two(lv_obj_t *r, const char *top, const char *sub, const char *right){
    lv_obj_t *t = lv_label_create(r);
    lv_label_set_text(t, top); lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(t, 12, sub&&sub[0]?6:17); lv_obj_set_size(t, right&&right[0]?186:242, 24);
    lv_obj_set_style_text_font(t, ui_font_user(18), 0);   /* CJK titles render via Source Han Sans fallback */
    lv_obj_set_style_text_color(t, TC(TEXT_PRIMARY), 0);
    if(sub && sub[0]){
        lv_obj_t *s = lv_label_create(r);
        lv_label_set_text(s, sub); lv_label_set_long_mode(s, LV_LABEL_LONG_DOT);
        lv_obj_set_pos(s, 12, 31); lv_obj_set_size(s, right&&right[0]?186:242, 20);   /* clear of the duration */
        lv_obj_set_style_text_font(s, ui_font_user(16), 0);
        lv_obj_set_style_text_color(s, TC(TEXT_LYRICS), 0);
    }
    if(right && right[0]){
        lv_obj_t *rl = lv_label_create(r);
        lv_label_set_text(rl, right);
        lv_obj_set_pos(rl, 200, 19); lv_obj_set_size(rl, 56, 20);
        lv_obj_set_style_text_align(rl, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_style_text_font(rl, TF(UI_16), 0);
        lv_obj_set_style_text_color(rl, TC(TEXT_MUTED), 0);
    }
}

/* ---- callbacks ---------------------------------------------------------- */
/* hold a song: the long-press menu (queue, playlist, favourite, album, artist, info, tags) */
static void song_long_cb(lv_event_t *e){
    if(lv_event_get_code(e)!=LV_EVENT_LONG_PRESSED) return;
    int id = (int)(uintptr_t)lv_event_get_user_data(e);
    char p[300]; if(mdb_song_path(id, p, sizeof p) == 1) songmenu_open_song(p);
}
static void song_cb(lv_event_t *e){
    if(lv_event_get_code(e)!=LV_EVENT_SHORT_CLICKED && lv_event_get_code(e)!=LV_EVENT_CLICKED) return;
    focus_clear();                                  /* the user moved on */
    int id=(int)(uintptr_t)lv_event_get_user_data(e);
    if(!g_song_cb || !g_song_cb(id)) return;   /* refused / not sent: already toasted, stay on the list */
    screen_show(SCR_NOWPLAYING);
}
static void reload_async(void *p){ (void)p; library_reload(); }

/* ---- "Remove from Favourites?" confirm (long-press is easy to trigger by
 * accident, so removal needs an explicit confirm). ------------------------ */
static lv_obj_t *g_fav_modal;
static int       g_fav_pending_id;
static void fav_modal_close(void){
    if(g_fav_modal){ lv_obj_delete_async(g_fav_modal); g_fav_modal = NULL; }
}
static void fav_cancel_cb(lv_event_t *e){ if(lv_event_get_code(e)==LV_EVENT_CLICKED) fav_modal_close(); }
static void fav_confirm_cb(lv_event_t *e){
    if(lv_event_get_code(e)!=LV_EVENT_CLICKED) return;
    mdb_unfavorite(g_fav_pending_id);
    fav_modal_close();
    lv_async_call(reload_async, NULL);
}
static void fav_modal_pill(lv_obj_t *card, int x, const char *txt, theme_color_role_t col, lv_event_cb_t cb){
    lv_obj_t *b = lv_button_create(card);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, 112, 48); lv_obj_align(b, LV_ALIGN_BOTTOM_MID, x, -16);
    lv_obj_set_style_radius(b, 12, 0);
    lv_obj_set_style_bg_color(b, TC(SURFACE_RAISED), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    ui_on(b, cb, LV_EVENT_CLICKED, NULL, "library.cb", UI_CORE);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, TF(UI_18), 0);
    lv_obj_set_style_text_color(l, theme_color(col), 0);
    lv_obj_center(l);
}
static void fav_confirm(int i){   /* i = the row's index into g_favs */
    g_fav_pending_id = g_favs[i].love_id;   /* MY_LOVE.ID: removes exactly this row, even if its song left SONG */
    const char *title = g_favs[i].title[0] ? g_favs[i].title : "this song";
    fav_modal_close();
    g_fav_modal = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(g_fav_modal);
    lv_obj_set_size(g_fav_modal, 360, 360); lv_obj_center(g_fav_modal);
    lv_obj_set_style_bg_color(g_fav_modal, TC(CANVAS), 0);
    lv_obj_set_style_bg_opa(g_fav_modal, LV_OPA_70, 0);
    lv_obj_clear_flag(g_fav_modal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_fav_modal, LV_OBJ_FLAG_CLICKABLE);             /* absorb taps */
    ui_on(g_fav_modal, fav_cancel_cb, LV_EVENT_CLICKED, NULL, "library.fav_cancel", UI_CORE); /* tap outside = cancel */
    lv_obj_t *card = lv_obj_create(g_fav_modal);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, 272, 184); lv_obj_center(card);
    lv_obj_set_style_radius(card, 18, 0);
    lv_obj_set_style_bg_color(card, TC(SURFACE), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *t = lv_label_create(card);
    lv_label_set_text(t, "Remove from Favourites?");
    lv_obj_set_style_text_font(t, TF(UI_18), 0);
    lv_obj_set_style_text_color(t, TC(TEXT_PRIMARY), 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 24);
    lv_obj_t *s = lv_label_create(card);
    lv_label_set_text(s, title);
    lv_label_set_long_mode(s, LV_LABEL_LONG_DOT);
    lv_obj_set_size(s,224,lv_font_get_line_height(ui_font_cjk(16)));
    lv_obj_set_style_text_align(s, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(s, ui_font_cjk(16), 0);   /* song title is user data: Cyrillic/CJK-capable (issue #3) */
    lv_obj_set_style_text_color(s, TC(TEXT_MUTED), 0);
    lv_obj_align(s, LV_ALIGN_TOP_MID, 0, 52);
    fav_modal_pill(card, -58, "Cancel", THEME_CLR_TEXT_SECONDARY, fav_cancel_cb);
    fav_modal_pill(card,  58, "Remove", THEME_CLR_STATUS_DANGER, fav_confirm_cb);
}

/* Favourites rows: short tap plays, long-press asks to remove. (SHORT_CLICKED
 * so a long-press doesn't also fire a play on release.) */
/* Rows carry their index into g_favs: the MY_LOVE.ID removes it and (with the row) starts the V2.57 favourites
 * queue; the SONG.ID plays it on firmware without the stock favourites queue. */
static void fav_row_cb(lv_event_t *e){
    lv_event_code_t c = lv_event_get_code(e);
    int i = (int)(uintptr_t)lv_event_get_user_data(e);
    if(g_view != VIEW_FAVS || i < 0 || i >= g_count) return;
    if(c == LV_EVENT_SHORT_CLICKED){
        int ok = fw_fav_play_by_love_id() ? ui_play_favorite(g_favs[i].love_id, i + 1)
                                          : (g_song_cb && g_song_cb(g_favs[i].id));
        if(ok) screen_show(SCR_NOWPLAYING);   /* refused / not sent: already toasted, stay on the list */
    } else if(c == LV_EVENT_LONG_PRESSED){
        fav_confirm(i);
    }
}
static void playlist_cb(lv_event_t *e){
    if(lv_event_get_code(e)!=LV_EVENT_CLICKED) return;
    int i=(int)(intptr_t)lv_event_get_user_data(e);
    plview_open(g_plids[i], g_plnames[i]);   /* open the playlist (don't auto-play) */
}
static void pl_new_done(const char *name){   /* keyboard finished */
    if(name && name[0] && mdb_playlist_create(name) > 0) library_reload();
}
static void pl_new_cb(lv_event_t *e){
    if(lv_event_get_code(e)!=LV_EVENT_CLICKED) return;
    kbinput_open("Playlist name", "", pl_new_done);   /* create an empty playlist */
}
static void group_cb(lv_event_t *e){
    if(lv_event_get_code(e)!=LV_EVENT_SHORT_CLICKED) return;
    focus_clear();
    int gi=(int)(uintptr_t)lv_event_get_user_data(e);
    if(g_view==VIEW_ARTISTS){                  /* Artist -> that artist's albums */
        snprintf(g_artist, MDB_STR, "%s", g_gnames[gi]);
        g_view=VIEW_ARTIST_ALBUMS; g_drill_kind=0; g_from_artist=0; g_deeplink=0;
        library_reload(); return;
    }
    if(g_view==VIEW_ARTIST_ALBUMS){            /* row 0 = All Songs by the artist, the rest = one album */
        if(gi==0){ g_drill_kind=2; snprintf(g_drill, MDB_STR, "%s", g_artist); }
        else     { g_drill_kind=1; snprintf(g_drill, MDB_STR, "%s", g_gnames[gi]); }
        g_view=VIEW_GROUP; g_from_artist=1;    /* keep g_deeplink: back returns to the album list first */
        library_reload(); return;
    }
    g_drill_kind = (g_view==VIEW_ALBUMS)?1:(g_view==VIEW_GENRES)?3:2;
    snprintf(g_drill, MDB_STR, "%s", g_gnames[gi]);
    g_scope_from_albums = 0;
    g_scope_lvl = (g_drill_kind != 1 && mdb_scope_albums(g_drill_kind==3 ? MDB_SCOPE_GENRE : MDB_SCOPE_ARTIST, g_drill, NULL, NULL, NULL, 0) > 0) ? 1 : 0;
    int keep=g_view; g_view=VIEW_GROUP; (void)keep;
    g_deeplink=0;   /* normal in-library drill: back returns to the category list */
    library_reload();
}
/* Long-press an album/artist/genre row -> play the whole group right away
 * (saves drilling in + tapping Play All). Short tap still drills in. */
/* Play group gi of the current view (the original long-press action; now the menu's Play / Shuffle). */
static int scope_play_album(const char *album, int song_id);
static void group_play(int gi, int shuffle){
    int all  = (g_view==VIEW_ARTIST_ALBUMS && gi==0);   /* "All Songs" row = the whole artist */
    int kind = (g_view==VIEW_ALBUMS || (g_view==VIEW_ARTIST_ALBUMS && !all))?1:(g_view==VIEW_GENRES)?3:2;
    int lt   = (kind==1)?3:(kind==3)?10:2;   /* 3=album, 10=genre, 2=artist */
    ui_set_workmode(shuffle ? 1 : 0);          /* sequential, or shuffle from the menu */
    if(lt == 2 && mdb_artist_mode()){          /* Artists by album artist: through the artist plan, or nothing */
        mdb_plan_t plan; int ok = 0;
        if(!mdb_artist_plan(g_gnames[gi], &plan)){ ui_toast("Couldn't play that list"); return; }
        ok = ui_play_plan(&plan, plan.ids[0]); mdb_plan_free(&plan);
        if(!ok) return;                        /* refused or not sent: ui_play_plan already said why; stay on the list */
    } else if(!ui_play_list(lt, all ? g_artist : g_gnames[gi], 1)) return;
    screen_show(SCR_NOWPLAYING);
}
/* fork: long-press an album / artist / genre row = its action menu (Play, Shuffle, Add to queue, Playlist) */
static int g_menu_gi = -1, g_menu_view = -1, g_menu_scope;
static void group_menu_play(int shuffle){
    if(g_view != g_menu_view || g_menu_gi < 0 || g_menu_gi >= g_count){ ui_toast("The list changed"); return; }
    if(g_menu_scope){
        ui_set_workmode(shuffle ? 1 : 0);
        if(!scope_play_album(g_gnames[g_menu_gi], -1)) return;
        screen_show(SCR_NOWPLAYING);
    } else group_play(g_menu_gi, shuffle);
}
static void group_menu_open(int gi, int scope){
    int all  = (!scope && g_view==VIEW_ARTIST_ALBUMS && gi==0);
    int album = scope || g_view==VIEW_ALBUMS || (g_view==VIEW_ARTIST_ALBUMS && !all);
    const char *col = album ? "ALBUM" : (g_view==VIEW_GENRES) ? "GENRE" : "ARTIST";
    const char *val = all ? g_artist : g_gnames[gi];
    g_menu_gi = gi; g_menu_view = g_view; g_menu_scope = scope;
    songmenu_open_group(col, val, val, group_menu_play);
}
static void group_play_cb(lv_event_t *e){
    if(lv_event_get_code(e)!=LV_EVENT_LONG_PRESSED) return;
    int gi=(int)(uintptr_t)lv_event_get_user_data(e);
    if(gi < 0 || gi >= g_count) return;
    group_menu_open(gi, 0);
}
static void menu_cb(lv_event_t *e){
    if(lv_event_get_code(e)!=LV_EVENT_CLICKED) return;
    int v=(int)(uintptr_t)lv_event_get_user_data(e);
    if(v==CAT_FOLDERS){ folderbrowser_open(); return; }   /* opens a screen, not a library view */
    if(v==CAT_BOOKS){ books_open(); return; }             /* audiobooks: a screen, not a library view */
    if(v==VIEW_ALBUMS && cfg_get_int("album_view", 0)==1){ screen_show(SCR_ALBUMWALL); return; }  /* cover flow */
    g_view=v;
    g_drill_kind=0; library_reload();
}
/* Back to Artists lands on the artist you came from (else the one playing), focused - not the top of the list */
static void back_to_artists(void){
    g_view = VIEW_ARTISTS;
    snprintf(g_focus_artist, sizeof g_focus_artist, "%s", g_artist);
    if(!g_focus_artist[0]){
        track_state_t st; ipc_get_state(&st); mdb_song_t sg;
        if(st.have_track && mdb_song_by_path(st.path, &sg) == 1)
            snprintf(g_focus_artist, sizeof g_focus_artist, "%s", mdb_artist_mode() ? sg.artist_group : sg.artist);
    }
    library_reload();
}
/* Step one level back WITHIN the library (drill-in -> its category list -> the
 * category menu). Returns 1 if it handled an internal step, 0 if already at the
 * top menu (so the caller should leave the Library screen). */
int library_back(void){                    /* the back SWIPE (and any caller that means "go back") */
    int jump = g_np_jump;
    focus_clear();
    if(jump){                                   /* came from Now Playing's title: straight back there */
        g_deeplink = 0; g_from_artist = 0; g_view = VIEW_MENU; g_drill_kind = 0; library_reload(); return 0;
    }
    if(g_view==VIEW_GROUP){
        if(g_drill_kind!=1 && g_scope_lvl==2){ g_scope_lvl=1; library_reload(); return 1; }   /* album songs -> that scope's albums */
        if(g_drill_kind!=1 && g_scope_lvl==0 && g_scope_from_albums){ g_scope_from_albums=0; g_scope_lvl=1; library_reload(); return 1; }   /* All Songs -> albums */
        if(g_deeplink){  /* opened from the hub -> leave Library entirely (back to hub) */
            g_deeplink=0; g_view=VIEW_MENU; g_drill_kind=0; library_reload(); return 0;
        }
        if(g_drill_kind!=1 && g_drill_kind!=3){ g_drill_kind=0; back_to_artists(); return 1; }
        g_view=(g_drill_kind==1)?VIEW_ALBUMS:VIEW_GENRES;
        g_drill_kind=0; library_reload(); return 1;
    }
    if(g_view==VIEW_ARTIST_ALBUMS){
        if(g_deeplink){ g_deeplink=0; g_view=VIEW_MENU; library_reload(); return 0; }   /* opened from the hub */
        back_to_artists(); return 1;
    }
    if(g_view==VIEW_MOSTPLAYED || g_view==VIEW_RECENT){ g_view=VIEW_HISTORY; library_reload(); return 1; }  /* stats -> History */
    if(g_view==VIEW_FAVS && g_deeplink){ g_deeplink=0; g_view=VIEW_MENU; library_reload(); return 0; }
    if(g_view!=VIEW_MENU){ g_view=VIEW_MENU; library_reload(); return 1; }
    return 0;
}
/* the header's back arrow: one level UP the hierarchy, never out of a drill the user was placed in */
static int library_up(void){
    focus_clear();
    if(g_view==VIEW_GROUP){
        if(g_from_artist){ g_from_artist=0; g_deeplink=0; g_view=VIEW_ARTIST_ALBUMS; g_drill_kind=0; library_reload(); return 1; }
        g_deeplink=0;
        if(g_drill_kind!=1 && g_drill_kind!=3){ g_drill_kind=0; back_to_artists(); return 1; }
        g_view=(g_drill_kind==1)?VIEW_ALBUMS:VIEW_GENRES;
        g_drill_kind=0; library_reload(); return 1;
    }
    if(g_view==VIEW_ARTIST_ALBUMS){ g_deeplink=0; back_to_artists(); return 1; }
    return library_back();                      /* elsewhere: the usual step back (Artists -> Library, ...) */
}
static void back_cb(lv_event_t *e){
    if(lv_event_get_code(e)!=LV_EVENT_CLICKED) return;
    if(!library_up()) screen_back();
}

/* ---- artist / genre -> albums -> songs ---------------------------------------------------------------------------- */
static int scope_kind(void){ return g_drill_kind==3 ? MDB_SCOPE_GENRE : MDB_SCOPE_ARTIST; }
static void scope_all_cb(lv_event_t *e){          /* "All Songs": the flat list (Play All / Shuffle on the whole artist / genre) */
    if(lv_event_get_code(e)!=LV_EVENT_SHORT_CLICKED) return;
    g_scope_lvl = 0; g_scope_from_albums = 1; library_reload();
}
static void scope_album_cb(lv_event_t *e){
    if(lv_event_get_code(e)!=LV_EVENT_SHORT_CLICKED) return;
    int ai=(int)(uintptr_t)lv_event_get_user_data(e);
    if(g_view!=VIEW_GROUP || g_scope_lvl!=1 || ai<0 || ai>=g_count-1) return;
    snprintf(g_scope_album, MDB_STR, "%s", g_gnames[ai]);
    g_scope_lvl = 2; library_reload();
}
/* Play song_id through an album plan. Types 2/3/5/7 go through ui_play_plan; the genre+album queue (8) is the same frame
 * and jump sent straight through ui_play_list (ui_play_plan only knows 2/3/5/7). */
static int scope_play_plan(mdb_plan_t *plan, int song_id){
    if(plan->list_type == 8){
        int pos = mdb_plan_pos(plan, song_id);
        if(pos < 1){ ui_toast("Couldn't find that song"); return 0; }
        return ui_play_list(8, plan->name, pos);
    }
    return ui_play_plan(plan, song_id);
}
/* Play one album of the current artist / genre: song_id < 0 = from the start, -2 = a random song (Shuffle) */
static int scope_play_album(const char *album, int song_id){
    mdb_plan_t plan;
    if(!mdb_scope_album_plan(scope_kind(), g_drill, album, &plan)){ ui_toast("Couldn't play that list"); return 0; }
    if(song_id == -2 && plan.count > 1) song_id = plan.ids[rand() % plan.count];
    else if(song_id < 0) song_id = plan.ids[0];
    int ok = scope_play_plan(&plan, song_id);
    mdb_plan_free(&plan);
    return ok;
}
static void scope_song_cb(lv_event_t *e){
    if(lv_event_get_code(e)!=LV_EVENT_SHORT_CLICKED && lv_event_get_code(e)!=LV_EVENT_CLICKED) return;
    int id=(int)(uintptr_t)lv_event_get_user_data(e);
    if(!scope_play_album(g_scope_album, id)) return;   /* refused / not sent: already toasted, stay on the list */
    screen_show(SCR_NOWPLAYING);
}
static void scope_album_play_cb(lv_event_t *e){   /* long-press an album row in an artist/genre scope: its action menu (fork) */
    if(lv_event_get_code(e)!=LV_EVENT_LONG_PRESSED) return;
    int ai=(int)(uintptr_t)lv_event_get_user_data(e);
    if(g_view!=VIEW_GROUP || g_scope_lvl!=1 || ai<0 || ai>=g_count-1) return;
    group_menu_open(ai, 1);
}

/* ---- A-Z grid ----------------------------------------------------------- */
/* build one row for the current view at list index i (data already prepared) */
static char g_focus_path[600];        /* one-shot: highlight this song when its album opens */
static int  g_focus_idx = -1;
static lv_obj_t *g_focus_row;
static char g_focus_album[MDB_STR];   /* one-shot: highlight this album in an artist's album list */
static void focus_resolve(int n){      /* find the focused song among this list's rows */
    g_focus_idx = -1; g_focus_row = NULL;
    if(g_focus_artist[0] && g_view == VIEW_ARTISTS){
        for(int i = 0; i < n; i++) if(!strcasecmp(g_gnames[i], g_focus_artist)){ g_focus_idx = i; break; }
        return;
    }
    if(g_focus_album[0] && g_view == VIEW_ARTIST_ALBUMS){
        for(int i = 1; i < n; i++) if(!strcasecmp(g_gnames[i], g_focus_album)){ g_focus_idx = i; break; }
        return;
    }
    if(!g_focus_path[0] || !(g_view == VIEW_GROUP || g_view == VIEW_SONGS)) return;
    char p[600];
    for(int i = 0; i < n; i++)
        if(mdb_song_path(g_buf[i]->id, p, sizeof p) && !strcmp(p, g_focus_path)){ g_focus_idx = i; break; }   /* returns 1 on success */
    /* NOT cleared here: showing the Library rebuilds the list once more (library_refresh on show), and
     * the focus has to survive that. It is dropped on the user's next move - focus_clear() below. */
}
static void focus_clear(void){ g_focus_album[0] = 0; g_focus_artist[0] = 0; g_focus_path[0] = 0; g_focus_idx = -1; g_focus_row = NULL; g_np_jump = 0; }
static void focus_mark(lv_obj_t *r){   /* lifted row + accent title: "you are here" */
    lv_obj_set_style_bg_color(r, TC(SURFACE_SELECTED), 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(r, 14, 0);
    lv_obj_t *t = lv_obj_get_child(r, 0);
    if(t && lv_obj_check_type(t, &lv_label_class)){
        lv_obj_set_style_text_color(t, TC(ACCENT_PRIMARY), 0);
        ui_reveal_title(t);
    }
    g_focus_row = r;
}
static void add_row(int i){
    int focus_me = (i == g_focus_idx);
    char dur[12];
    lv_obj_t *r = base_row();
    if(g_view==VIEW_GROUP && g_drill_kind!=1 && g_scope_lvl==1){       /* artist/genre -> albums: "All Songs" first */
        char cnt[12];
        if(i==0){
            snprintf(cnt, sizeof cnt, "%d", g_scope_total);
            ui_on(r, scope_all_cb, LV_EVENT_SHORT_CLICKED, NULL, "library.scope_all", UI_CORE);
            row_two(r, tr("All Songs"), NULL, cnt);
        } else {
            snprintf(cnt, sizeof cnt, "%d", g_gcounts[i-1]);
            ui_on(r, scope_album_cb, LV_EVENT_SHORT_CLICKED, (void*)(uintptr_t)(i-1), "library.scope_album", UI_CORE);
            ui_on(r, scope_album_play_cb, LV_EVENT_LONG_PRESSED, (void*)(uintptr_t)(i-1), "library.scope_album_play.long", UI_CORE);
            row_two(r, g_gnames[i-1], g_drill_kind==3 ? g_gartists[i-1] : NULL, cnt);
        }
        theme_list_row(r);
        return;
    }
    switch(g_view){
        case VIEW_SONGS: case VIEW_GROUP:
            if(g_view==VIEW_GROUP && g_drill_kind!=1 && g_scope_lvl==2)
                ui_on(r, scope_song_cb, LV_EVENT_SHORT_CLICKED, (void*)(uintptr_t)g_buf[i]->id, "library.scope_song", UI_CORE);
            else
                ui_on(r, song_cb, LV_EVENT_SHORT_CLICKED, (void*)(uintptr_t)g_buf[i]->id, "library.song", UI_CORE);
            /* fork: hold a track = its action menu (queue, playlist, favourite, album, artist, info, tags);
             * SHORT_CLICKED above so the hold doesn't also start playback on release */
            ui_on(r, song_long_cb, LV_EVENT_LONG_PRESSED, (void*)(uintptr_t)g_buf[i]->id, "library.song.long", UI_CORE);
            fmt_dur(dur,sizeof dur,g_buf[i]->dur_ms);
            if(g_view==VIEW_GROUP && (g_drill_kind==1 || g_scope_lvl==2) && g_buf[i]->track > 0 && cfg_get_int("track_numbers", 0)){
                char num[MDB_STR + 16];                    /* album list + Track Numbers on: "3 Title" (stock "%d %s") */
                snprintf(num, sizeof num, "%d %s", g_buf[i]->track, g_buf[i]->title[0]?g_buf[i]->title:"Untitled");
                row_two(r, num, g_buf[i]->artist, dur);
            } else
            row_two(r, g_buf[i]->title[0]?g_buf[i]->title:"Untitled", g_buf[i]->artist, dur);
            break;
        case VIEW_ALBUMS: {
            ui_on(r, group_cb, LV_EVENT_SHORT_CLICKED, (void*)(uintptr_t)i, "library.group", UI_CORE);
            ui_on(r, group_play_cb, LV_EVENT_LONG_PRESSED, (void*)(uintptr_t)i, "library.group_play.long", UI_CORE);
            char cnt[12]; snprintf(cnt,sizeof cnt,"%d",g_gcounts[i]);
            row_two(r, g_gnames[i], g_gartists[i], cnt);
            break; }
        case VIEW_ARTISTS:
            ui_on(r, group_cb, LV_EVENT_SHORT_CLICKED, (void*)(uintptr_t)i, "library.group", UI_CORE);
            ui_on(r, group_play_cb, LV_EVENT_LONG_PRESSED, (void*)(uintptr_t)i, "library.group_play.long", UI_CORE);
            row_two(r, g_gnames[i], NULL, NULL);
            break;
        case VIEW_ARTIST_ALBUMS: {   /* row 0 is "All Songs"; counts are this artist's tracks per album */
            lv_obj_add_event_cb(r, group_cb,      LV_EVENT_SHORT_CLICKED, (void*)(uintptr_t)i);
            lv_obj_add_event_cb(r, group_play_cb, LV_EVENT_LONG_PRESSED,  (void*)(uintptr_t)i);
            char cnt[12]; snprintf(cnt,sizeof cnt,"%d",g_gcounts[i]);
            row_two(r, g_gnames[i], NULL, cnt);
            break; }
        case VIEW_GENRES: {
            ui_on(r, group_cb, LV_EVENT_SHORT_CLICKED, (void*)(uintptr_t)i, "library.group", UI_CORE);
            ui_on(r, group_play_cb, LV_EVENT_LONG_PRESSED, (void*)(uintptr_t)i, "library.group_play.long", UI_CORE);
            char cnt[12]; snprintf(cnt,sizeof cnt,"%d",g_gcounts[i]);
            row_two(r, g_gnames[i], NULL, cnt);
            break; }
        case VIEW_FAVS:
            ui_on(r, fav_row_cb, LV_EVENT_SHORT_CLICKED, (void*)(uintptr_t)i, "library.fav_row", UI_CORE);
            ui_on(r, fav_row_cb, LV_EVENT_LONG_PRESSED, (void*)(uintptr_t)i, "library.fav_row.long", UI_CORE);
            fmt_dur(dur,sizeof dur,g_favs[i].dur_ms);
            row_two(r, g_favs[i].title, g_favs[i].artist, dur);
            break;
        case VIEW_MOSTPLAYED: case VIEW_RECENT:   /* tap plays the song (via song_cb, by id) */
            lv_obj_add_event_cb(r, song_cb, LV_EVENT_SHORT_CLICKED, (void*)(uintptr_t)g_favs[i].id);
            lv_obj_add_event_cb(r, song_long_cb, LV_EVENT_LONG_PRESSED, (void*)(uintptr_t)g_favs[i].id);
            fmt_dur(dur,sizeof dur,g_favs[i].dur_ms);
            row_two(r, g_favs[i].title[0]?g_favs[i].title:"Untitled", g_favs[i].artist, dur);
            break;
    }
    if(focus_me) focus_mark(r);
}
static void fill_stop(void){ if(g_fill_timer){ lv_timer_del(g_fill_timer); g_fill_timer=NULL; } }

/* ---- long lists: a sliding window of real rows between two spacers ------------------------------------
 * Every row used to be a live object: 30,000 songs meant ~30,000 rows (~50 MB on a 64-bit host, about half
 * that here - most of the free RAM), a fill that ran for minutes in the background, and scrolling that
 * moved every one of them each frame (15 ms per step on a fast PC). Rows all share one pitch
 * (ROW_H + the 4 px flex gap), so long lists now hold ~24 real rows around the viewport; two spacers
 * give the list its full height, so scrolling, momentum, the A-Z jump and the position dots behave exactly
 * as before. Rows are created with the same add_row() as always, so they look and act the same. */
#define VIRT_MIN   80          /* lists at least this long are windowed; shorter ones fill as before */
#define VIRT_AHEAD 9           /* rows kept above and below the visible ones */
#define PITCH      (ROW_H + 4)
static int g_virt;             /* this list is windowed */
static int g_vfirst, g_vlast;  /* rows [g_vfirst, g_vlast) exist */
static lv_obj_t *g_sp_top, *g_sp_bot;
static lv_obj_t *spacer(void){
    lv_obj_t *sp = lv_obj_create(g_list);
    lv_obj_remove_style_all(sp);
    lv_obj_set_size(sp, 1, 1);
    lv_obj_clear_flag(sp, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return sp;
}
static void spacer_set(lv_obj_t *sp, int rows){           /* rows * PITCH, counting the gap after it */
    if(rows <= 0){ lv_obj_add_flag(sp, LV_OBJ_FLAG_HIDDEN); return; }
    lv_obj_remove_flag(sp, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_height(sp, rows * PITCH - 4);
}
static void virt_drop_row(lv_obj_t *r){ if(r == g_focus_row) g_focus_row = NULL; lv_obj_delete(r); }
static void virt_window(int first, int last){              /* make exactly rows [first, last) exist */
    if(first < 0) first = 0;
    if(last > g_fill_n) last = g_fill_n;
    if(first >= last){ first = 0; last = g_fill_n < 2 * VIRT_AHEAD ? g_fill_n : 2 * VIRT_AHEAD; }
    if(last <= g_vfirst || first >= g_vlast){                 /* no overlap (a jump): rebuild */
        while(g_vlast > g_vfirst){ virt_drop_row(lv_obj_get_child(g_list, lv_obj_get_index(g_sp_top) + 1)); g_vlast--; }
        g_vfirst = g_vlast = first;
    }
    while(g_vfirst < first){ virt_drop_row(lv_obj_get_child(g_list, lv_obj_get_index(g_sp_top) + 1)); g_vfirst++; }
    while(g_vlast > last){  virt_drop_row(lv_obj_get_child(g_list, lv_obj_get_index(g_sp_bot) - 1)); g_vlast--; }
    while(g_vfirst > first){                                 /* grow upwards: insert right after the top spacer */
        g_vfirst--; add_row(g_vfirst);
        lv_obj_move_to_index(lv_obj_get_child(g_list, -1), lv_obj_get_index(g_sp_top) + 1);
    }
    while(g_vlast < last){ add_row(g_vlast); g_vlast++; }    /* grow downwards: append, then keep the spacer last */
    lv_obj_move_to_index(g_sp_bot, -1);
    spacer_set(g_sp_top, g_vfirst);
    spacer_set(g_sp_bot, g_fill_n - g_vlast);
}
static int virt_visible(void){                              /* the first row index in view */
    int idx = (lv_obj_get_scroll_y(g_list) - g_hdr_extra_px) / PITCH - g_has_header;
    return idx < 0 ? 0 : idx;
}
static void virt_follow(void){                              /* on scroll: keep the window around the view */
    if(!g_virt) return;
    int v = virt_visible(), vis_rows = LIST_H / PITCH + 1;
    if(v - g_vfirst >= 3 && g_vlast - (v + vis_rows) >= 3) return;        /* comfortably inside */
    virt_window(v - VIRT_AHEAD, v + vis_rows + VIRT_AHEAD);
    lv_obj_update_layout(g_list);          /* new rows need real geometry before the curve measures them */
}
static void fill_cb(lv_timer_t *t){
    int end = g_fill_i + 40; if(end > g_fill_n) end = g_fill_n;
    for(; g_fill_i < end; g_fill_i++) add_row(g_fill_i);
    if(g_fill_i >= g_fill_n){ fill_stop(); lv_obj_update_layout(g_list); curve_rows(); g_pdot_cur = -2; pdots_update(); }
    (void)t;
}
/* From Now Playing's title: the album, this song focused, placed inside its artist so the header walks UP
 * (album -> the artist's albums -> Artists -> Library), while the back swipe returns to Now Playing. */
void library_open_album_focus(const char *album, const char *artist, const char *path){
    library_open_album(album);                     /* (clears any older focus / jump) */
    if(artist && artist[0]){ snprintf(g_artist, MDB_STR, "%s", artist); g_from_artist = 1; }
    snprintf(g_focus_path, sizeof g_focus_path, "%s", path ? path : "");
    g_np_jump = 1;
    library_reload();                              /* build it again with this song focused */
}
static void fill_start(int n){
    fill_stop();
    g_fill_n = n; g_fill_i = 0;
    focus_resolve(n);
    g_virt = (n >= VIRT_MIN);
    if(g_virt){
        g_sp_top = spacer(); g_sp_bot = spacer(); g_vfirst = g_vlast = 0;
        int f = g_focus_idx >= 0 ? g_focus_idx - 2 : 0;       /* around the focused song, if any */
        virt_window(f, f + LIST_H / PITCH + 1 + VIRT_AHEAD);
        g_fill_i = g_fill_n;                                   /* nothing left to fill in the background */
        lv_obj_update_layout(g_list);
        if(g_focus_row){ lv_obj_scroll_to_view(g_focus_row, LV_ANIM_OFF); lv_obj_update_layout(g_list); virt_follow(); }
        curve_rows(); g_pdot_cur = -2; pdots_update();
        return;
    }
    int first = n < 18 ? n : 18;
    if(g_restore_y>0){int need=g_restore_y/PITCH+LIST_H/PITCH+2;if(need>first)first=need<n?need:n;}                 /* first screenful, instantly */
    if(g_focus_idx >= first) first = g_focus_idx + 1 < n ? g_focus_idx + 1 : n;   /* ...through the focused song */
    for(; g_fill_i < first; g_fill_i++) add_row(g_fill_i);
    lv_obj_update_layout(g_list);
    if(g_focus_row){ lv_obj_scroll_to_view(g_focus_row, LV_ANIM_OFF); lv_obj_update_layout(g_list); }
    curve_rows(); g_pdot_cur = -2; pdots_update();
    if(g_fill_i < g_fill_n) g_fill_timer = lv_timer_create(fill_cb, 16, NULL);
}
static void fill_flush(void){                    /* render the rest now (before a jump) */
    for(; g_fill_i < g_fill_n; g_fill_i++) add_row(g_fill_i);
    fill_stop();
}

static void jump_to_letter(char L){
    if(g_count<=0) return;
    if(!g_virt) fill_flush();                    /* short list: ensure the target row exists (long: windowed) */
    int idx=-1;
    for(int i=0;i<g_count;i++) if(g_first[i]==L){ idx=i; break; }
    if(idx<0) for(int i=0;i<g_count;i++) if(g_first[i]>=L){ idx=i; break; } /* nearest after */
    if(idx<0) idx=g_count-1;
    /* row pitch = ROW_H + the list's 4px flex pad_row, so idx*ROW_H alone lands
     * progressively short for later letters. */
    if(g_virt){ virt_window(idx - VIRT_AHEAD, idx + LIST_H / PITCH + 1 + VIRT_AHEAD); lv_obj_update_layout(g_list); }
    lv_obj_scroll_to_y(g_list, g_hdr_extra_px + (idx + g_has_header)*(ROW_H+4), LV_ANIM_OFF);
    curve_rows(); pdots_update();
}
static void az_show(int on){
    azjump_show(&g_az, on);
    /* fork: the list always sits on the centre line, rows centred, so the curve narrows them evenly on both sides
     * (as History and the folder browser do); the A-Z button sits just outside the widest row on the right rim */
    if(g_list){
        lv_obj_set_x(g_list, 37);
        lv_obj_set_flex_align(g_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    }
}

/* ---- populate ----------------------------------------------------------- */
static lv_obj_t *empty_label(const char *txt){
    lv_obj_t *l = lv_label_create(g_list);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, TF(UI_16), 0);
    lv_obj_set_style_text_color(l, TC(TEXT_MUTED), 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(l, 268);
    return l;
}
/* empty music view: the message + a tappable "Scan Library" so a new user isn't dead-ended */
static void scan_action_cb(lv_event_t *e){
    if(lv_event_get_code(e)!=LV_EVENT_CLICKED) return;
    ui_rescan_library();
}
static void empty_scan(const char *txt){
    empty_label(txt);
    lv_obj_t *b = lv_button_create(g_list);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, 200, 46);
    lv_obj_set_style_radius(b, 23, 0);
    lv_obj_set_style_bg_color(b, TC(SURFACE), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, TC(SURFACE_RAISED), LV_STATE_PRESSED);
    lv_obj_add_event_cb(b, scan_action_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, LV_SYMBOL_REFRESH "  Scan Library");
    lv_obj_set_style_text_font(l, TF(UI_16), 0);
    lv_obj_set_style_text_color(l, TC(TEXT_PRIMARY), 0);
    lv_obj_center(l);
}

/* ---- Play All / Shuffle All header for song lists ----------------------- */
/* list_type + filter name for the current song view (0100 jump-table types:
 * 1=all, 2=artist, 3=album, 6=favourites, 10=genre). Returns 1 if playable. */
static int current_list_context(int *lt, char *name, int cap){
    if(g_view==VIEW_SONGS){ if(lt)*lt=1; if(name&&cap>0)name[0]=0; return 1; }
    if(g_view==VIEW_FAVS){  if(lt)*lt=6; if(name&&cap>0)name[0]=0; return 1; }
    if(g_view==VIEW_GROUP)  return library_drill_context(lt, name, cap);
    if(g_view==VIEW_ARTIST_ALBUMS){ if(lt)*lt=2; if(name&&cap>0) snprintf(name, cap, "%s", g_artist); return 1; }
    return 0;
}
/* Play the current list, setting the play-mode first (0=Sequential, 1=Shuffle).
 * Play-mode = 0102 (ground-truth captured 2026-06-25); ui_set_workmode applies it. */
static void play_list_mode(int mode){
    if(g_view==VIEW_GROUP && g_drill_kind!=1 && g_scope_lvl==2){     /* one album of an artist / genre: its own stock queue */
        cfg_set_int("work_mode", mode); ui_set_workmode(mode);
        if(scope_play_album(g_scope_album, mode == 1 ? -2 : -1)) screen_show(SCR_NOWPLAYING);
        return;
    }
    int lt; char nm[160];
    if(!current_list_context(&lt, nm, sizeof nm)) return;
    cfg_set_int("work_mode", mode); ui_set_workmode(mode);
    int pos = 1;
    /* shuffle: start on a random track (not always the first). Skip artist lists (lt==2):
     * the UI's split-artist count can exceed the player's exact ARTIST=? list -> out-of-range pos. */
    if(mode == 1 && g_count > 1 && lt != 2) pos = 1 + (rand() % g_count);
    /* V2.57 favourites start from a MY_LOVE.ID, not a position (index 0 is refused) */
    if(lt == 6 && fw_fav_play_by_love_id()){ if(pos > g_count || !ui_play_favorite(g_favs[pos-1].love_id, pos)) return; }
    else if(lt == 2 && mdb_artist_mode()){          /* Artists by album artist: through the artist plan, or nothing */
        mdb_plan_t plan; int ok = 0;
        if(!mdb_artist_plan(nm, &plan)){ ui_toast("Couldn't play that list"); return; }
        int i = (mode == 1 && plan.count > 1) ? rand() % plan.count : 0;
        ok = ui_play_plan(&plan, plan.ids[i]);
        mdb_plan_free(&plan);
        if(!ok) return;                        /* refused or not sent: ui_play_plan already said why; stay on the list */
    }
    else if(!ui_play_list(lt, nm, pos)) return;
    screen_show(SCR_NOWPLAYING);
}
static void play_all_cb(lv_event_t *e){    if(lv_event_get_code(e)==LV_EVENT_CLICKED) play_list_mode(0); } /* sequential */
static void shuffle_all_cb(lv_event_t *e){ if(lv_event_get_code(e)==LV_EVENT_CLICKED) play_list_mode(1); } /* shuffle */

static void hdr_btn(lv_obj_t *row, int x, const char *txt, lv_event_cb_t cb){
    lv_obj_t *b = lv_button_create(row);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, 128, 44); lv_obj_set_pos(b, x, 4);
    lv_obj_set_style_radius(b, 12, 0);
    int br = th_braun();                                            /* Braun: flat off-white keys, dark type */
    lv_obj_set_style_bg_color(b, br ? TC(SURFACE) : TC(SURFACE), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, br ? TC(SURFACE_PRESSED) : TC(SURFACE_RAISED), LV_STATE_PRESSED);
    lv_obj_add_flag(b, LV_OBJ_FLAG_USER_4);                         /* Braun's row pass leaves this key's colours alone */
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, TF(UI_14), 0);
    lv_obj_set_style_text_color(l, br ? TC(TEXT_PRIMARY) : TC(TEXT_PRIMARY), 0);
    lv_obj_center(l);
}
/* Album detail header: the album cover on top (from the art cache) with the artist beneath it, shown
 * above the Play All / Shuffle row for an album drill - from EITHER the list view or the cover flow. */
static int g_hdr_cover_ctr;
static void add_album_cover_header(const char *album, const char *artist){
    lv_obj_t *r = lv_obj_create(g_list);
    lv_obj_remove_style_all(r);
    int has_artist = artist && artist[0];
    int hdr_h = has_artist ? 196 : 172;
    lv_obj_set_size(r, 286, hdr_h);   /* full list width so the cover screen-centres */
    g_hdr_extra_px = hdr_h + 4;        /* +flex pad_row: the A-Z scroll math subtracts this leading height */
    lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    const int CX = 7;   /* the list sits at x30/w286 (centre 173); +7 puts TOP_MID content at screen-centre 180 */

    /* Resolve the album's cached 148px cover (first track that has one) into a ROTATING /tmp file so
     * LVGL's image cache can't serve a previous album's bitmap for a reused filename. */
    char src[64]; src[0] = 0;
    int ids[6]; int n = mdb_album_track_ids(album, ids, 6);
    for(int i=0;i<n;i++){
        char track[512]; track[0]=0;
        char tmp[48]; snprintf(tmp, sizeof tmp, "/tmp/album_hdr%d.bmp", g_hdr_cover_ctr & 3);
        if(mdb_song_path(ids[i], track, sizeof track) && track[0] && artcache_get_cover(track, tmp)==0){
            snprintf(src, sizeof src, "A:%s", tmp); g_hdr_cover_ctr++;
            break;
        }
    }
    if(src[0]){
        lv_obj_t *img = lv_image_create(r);
        lv_image_set_src(img, src);                         /* native 148x148 BMP (no scale/rotate: SW renderer) */
        lv_obj_align(img, LV_ALIGN_TOP_MID, CX, 10);
    } else {                                                /* art-less album -> dark tile with its initial */
        lv_obj_t *ph = lv_obj_create(r);
        lv_obj_remove_style_all(ph);
        lv_obj_set_size(ph, 148, 148); lv_obj_align(ph, LV_ALIGN_TOP_MID, CX, 10);
        lv_obj_set_style_bg_color(ph, TC(SURFACE_RAISED), 0);
        lv_obj_set_style_bg_opa(ph, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(ph, 12, 0);
        lv_obj_t *in = lv_label_create(ph);
        char init[8]={0};   /* one WHOLE UTF-8 code point (a bare first byte tofus Cyrillic/CJK) */
        if(album && album[0]){ unsigned char c0=(unsigned char)album[0];
            int len=(c0<0x80)?1:((c0>>5)==0x6)?2:((c0>>4)==0xE)?3:((c0>>3)==0x1E)?4:1;
            for(int i=0;i<len && album[i];i++) init[i]=album[i];
            if(len==1 && init[0]>='a'&&init[0]<='z') init[0]-=32;
        } else init[0]='?';
        lv_label_set_text(in, init);
        /* ui_text_font tops out at 20 (would silently shrink a 28 request to 16): use the big latin face
         * for an ASCII initial, else the largest CJK-capable size so a non-Latin initial still renders. */
        lv_obj_set_style_text_font(in, (unsigned char)init[0] < 0x80 ? TF(UI_28) : ui_font_cjk(20), 0);
        lv_obj_set_style_text_color(in, TC(TEXT_MUTED), 0);
        lv_obj_center(in);
    }
    if(has_artist){
        lv_obj_t *a = lv_label_create(r);
        lv_label_set_long_mode(a, LV_LABEL_LONG_DOT); lv_obj_set_width(a, 240);
        lv_obj_set_style_text_align(a, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(a, artist);
        lv_obj_set_style_text_font(a, ui_text_font(15), 0);
        lv_obj_set_style_text_color(a, TC(TEXT_MUTED), 0);
        lv_obj_align(a, LV_ALIGN_TOP_MID, CX, 166);
    }
}

/* the first row of a song list: [ Play All ] [ Shuffle ]. centered=1 screen-centres the pair (used under
 * the album cover card, so it lines up with the centred cover instead of the left-aligned list rows). */
static void add_play_header(int centered){
    lv_obj_t *r = lv_obj_create(g_list);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, centered ? 286 : 268, ROW_H);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    int base = centered ? 16 : 0;                 /* r-local 16 => screen-centre (list is at x30/w286) */
    hdr_btn(r, base,       tr_sym(LV_SYMBOL_PLAY, "Play All"), play_all_cb);
    hdr_btn(r, base + 140, tr_sym(LV_SYMBOL_SHUFFLE, "Shuffle"), shuffle_all_cb);
    g_has_header = 1;
}

static const char *VIEW_TITLE[] = { "Library","Songs","Albums","Artists","Playlists","Favourites","Genres","","Most Played","Recently Played","History","" };

/* Rows for VIEW_ARTIST_ALBUMS: row 0 "All Songs" (every track by the artist), then each distinct
 * non-empty album among those tracks (case-insensitive, like mdb_albums), alphabetically. Tracks with
 * no album tag appear only under All Songs. Returns the row count (0 = no tracks). */
static int artist_albums(const char *artist){
    if(!g_buf || !g_gnames || !g_gcounts || !g_first || g_grp_cap < 1) return 0;
    int ns = mdb_artist_songs(artist, g_buf, g_alloc_n);
    if(ns <= 0) return 0;
    snprintf(g_gnames[0], MDB_STR, "All Songs"); g_gcounts[0] = ns; g_first[0] = '#';
    int n = 1;
    for(int i=0;i<ns;i++){
        const char *al = g_buf[i]->album;
        if(!al[0]) continue;
        int j = 1;
        while(j<n && strcasecmp(g_gnames[j], al) < 0) j++;
        if(j<n && !strcasecmp(g_gnames[j], al)){ g_gcounts[j]++; continue; }
        if(n >= g_grp_cap) break;
        memmove(g_gnames+j+1, g_gnames+j, (size_t)(n-j)*MDB_STR);
        memmove(g_gcounts+j+1, g_gcounts+j, (size_t)(n-j)*sizeof *g_gcounts);
        snprintf(g_gnames[j], MDB_STR, "%s", al); g_gcounts[j] = 1; n++;
    }
    for(int i=1;i<n;i++) g_first[i] = first_letter(g_gnames[i]);
    return n;
}
/* keep VIEW_TITLE[] in lockstep with the view enum so VIEW_TITLE[g_view] can't read out of bounds */
_Static_assert(sizeof(VIEW_TITLE)/sizeof(VIEW_TITLE[0]) == VIEW_COUNT, "VIEW_TITLE must have one entry per view");

static void add_plist_cb(lv_event_t *e){
    if(lv_event_get_code(e)!=LV_EVENT_CLICKED) return;
    if(!g_buf || g_count <= 0 || (g_view!=VIEW_GROUP)){ ui_toast("Nothing to add"); return; }
    int *ids = malloc((size_t)g_count * sizeof *ids);
    if(!ids){ ui_toast("Out of memory"); return; }
    for(int i=0;i<g_count;i++) ids[i] = g_buf[i]->id;
    plpick_set_ids(ids, g_count);
    free(ids);
    screen_show(SCR_PLPICK);
}

static void add_plist_row(int centered){
    lv_obj_t *r = lv_obj_create(g_list);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, centered ? 286 : 268, ROW_H);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    hdr_btn(r, (centered ? 16 : 0) + 70, tr_sym(LV_SYMBOL_PLUS, "Playlist"), add_plist_cb);   /* centred under the pair above */
    g_has_header = 2;                            /* Play All / Shuffle + this row */
}

static void library_reload_inner(void){
    if(!g_list) return;
    if(g_lhint){ lv_obj_add_flag(g_lhint, LV_OBJ_FLAG_HIDDEN); g_lhint_ch = 0; }  /* don't leave a stale A-Z hint across views */
    fill_stop();              /* cancel any in-flight stream before wiping rows */
    g_virt = 0; g_sp_top = g_sp_bot = NULL; g_vfirst = g_vlast = 0;   /* detach the window BEFORE clearing: the clear
                                                                     * clamps the scroll, and that scroll event must not
                                                                     * reach virt_follow() with freed spacers */
    lv_obj_clean(g_list);
    g_count = 0;
    g_has_header = 0;
    g_hdr_extra_px = 0;
    lv_obj_scroll_to_y(g_list, 0, LV_ANIM_OFF);

    const char *ttl = (g_view==VIEW_GROUP) ? g_drill : (g_view==VIEW_ARTIST_ALBUMS) ? g_artist : VIEW_TITLE[g_view];
    if(g_title) lv_label_set_text(g_title, ttl);
    az_show(0);

    /* one-time discoverability hints for the invisible long-press actions */
    static int s_group_hold_hint=0, s_fav_hold_hint=0;
    if((g_view==VIEW_ALBUMS || g_view==VIEW_ARTISTS || g_view==VIEW_GENRES) && !s_group_hold_hint){
        s_group_hold_hint=1; ui_toast("Hold an item for more");
    } else if(g_view==VIEW_FAVS && !s_fav_hold_hint){
        s_fav_hold_hint=1; ui_toast("Hold to remove");
    }

    char dur[12];

    if(g_view==VIEW_MENU){
        /* "Most Played" + "Recently Played" are play STATS, not catalog axes -> grouped under History */
        static const char *CATS[] = { "Songs","Albums","Artists","Genres","Playlists","Favourites","Folders","Books","History" };
        static const int   CATV[] = { VIEW_SONGS,VIEW_ALBUMS,VIEW_ARTISTS,VIEW_GENRES,VIEW_PLAYLISTS,VIEW_FAVS,CAT_FOLDERS,CAT_BOOKS,VIEW_HISTORY };
        for(int i=0;i<(int)(sizeof CATS/sizeof CATS[0]);i++){
            if(!DISKOS_AUDIOBOOKS && CATV[i]==CAT_BOOKS) continue;   /* compile-time gate (DISKOS_AUDIOBOOKS, currently 1): hides the Books row when the audiobook feature is built out */
            lv_obj_t *r = base_row();
            ui_on(r, menu_cb, LV_EVENT_CLICKED, (void*)(uintptr_t)CATV[i], "library.menu", UI_CORE);
            row_two(r, tr(CATS[i]), NULL, NULL);
            lv_obj_t *ch = lv_label_create(r);
            lv_label_set_text(ch, LV_SYMBOL_RIGHT);
            lv_obj_set_pos(ch, 240, 17);
            lv_obj_set_style_text_color(ch, TC(TEXT_DISABLED), 0);
        }
        return;
    }
    if(g_view==VIEW_HISTORY){          /* sub-menu: the two play-stats views */
        static const char *HCATS[] = { "Most Played","Recently Played" };
        static const int   HCATV[] = { VIEW_MOSTPLAYED, VIEW_RECENT };
        for(int i=0;i<2;i++){
            lv_obj_t *r = base_row();
            ui_on(r, menu_cb, LV_EVENT_CLICKED, (void*)(uintptr_t)HCATV[i], "library.menu", UI_CORE);
            row_two(r, tr(HCATS[i]), NULL, NULL);
            lv_obj_t *ch = lv_label_create(r);
            lv_label_set_text(ch, LV_SYMBOL_RIGHT);
            lv_obj_set_pos(ch, 240, 17);
            lv_obj_set_style_text_color(ch, TC(TEXT_DISABLED), 0);
        }
        return;
    }

    (void)dur;
    if(g_view==VIEW_GROUP && g_drill_kind!=1 && g_scope_lvl==1){       /* artist / genre -> its albums */
        int n = mdb_scope_albums(scope_kind(), g_drill, g_gnames, g_gartists, g_gcounts, g_grp_cap);
        if(n > g_grp_cap - 1) n = g_grp_cap - 1;
        if(n <= 0) g_scope_lvl = 0;                                     /* the albums went away (rescan): show the flat list */
        else {
            g_scope_total = (g_drill_kind==3) ? mdb_genre_songs(g_drill, g_buf, g_alloc_n) : mdb_artist_songs(g_drill, g_buf, g_alloc_n);
            g_first[0] = '#';
            for(int i=0;i<n;i++) g_first[i+1]=first_letter(g_gnames[i]);
            g_count = n + 1; fill_start(n + 1);
            if(n > 12) az_show(1);
            return;
        }
    }
    if(g_view==VIEW_SONGS || g_view==VIEW_GROUP){
        int n;
        if(g_view==VIEW_GROUP && g_drill_kind!=1 && g_scope_lvl==2) n=mdb_scope_album_songs(scope_kind(),g_drill,g_scope_album,g_buf,g_alloc_n);
        else if(g_view==VIEW_GROUP) n=(g_drill_kind==1)?mdb_album_songs(g_drill,g_buf,g_alloc_n):(g_drill_kind==3)?mdb_genre_songs(g_drill,g_buf,g_alloc_n):mdb_artist_songs(g_drill,g_buf,g_alloc_n);
        else { n=mdb_song_count(); if(n>g_alloc_n) n=g_alloc_n; for(int i=0;i<n;i++) g_buf[i]=mdb_song(i); }
        if(n<=0){ empty_scan(tr("No songs found")); return; }
        int is_album = (g_view==VIEW_GROUP && (g_drill_kind==1 || g_scope_lvl==2));
        if(is_album) add_album_cover_header(g_scope_lvl==2 && g_drill_kind!=1 ? g_scope_album : g_drill, n>0 ? g_buf[0]->artist : NULL);  /* album drill: cover on top */
        add_play_header(is_album);                                       /* centre the pair under the album cover */
        if(g_view==VIEW_GROUP) add_plist_row(is_album);                  /* album / artist / genre: add these songs to a playlist */
        for(int i=0;i<n;i++) g_first[i]=first_letter(g_buf[i]->title);
        g_count=n; fill_start(n);
        if(g_view==VIEW_SONGS) az_show(1);
    } else if(g_view==VIEW_ALBUMS){
        int n=mdb_albums(g_gnames,g_gartists,g_gcounts,g_grp_cap);
        if(n<=0){ empty_scan(tr("No albums found")); return; }
        for(int i=0;i<n;i++) g_first[i]=first_letter(g_gnames[i]);
        g_count=n; fill_start(n); az_show(1);
    } else if(g_view==VIEW_ARTIST_ALBUMS){
        lib_ensure_group_cap(512);
        int n=artist_albums(g_artist);
        if(n<=0){ empty_scan("No songs found"); return; }
        add_play_header(0);                     /* Play All / Shuffle = everything by this artist */
        g_count=n; fill_start(n);
    } else if(g_view==VIEW_ARTISTS){
        lib_ensure_group_cap(mdb_artist_count());   /* size for every distinct artist so none are clipped */
        int n=mdb_artists(g_gnames,g_grp_cap);
        if(n<=0){ empty_scan(tr("No artists found")); return; }
        for(int i=0;i<n;i++) g_first[i]=first_letter(g_gnames[i]);
        g_count=n; fill_start(n); az_show(1);
    } else if(g_view==VIEW_GENRES){
        int n=mdb_genres(g_gnames,g_gcounts,g_grp_cap);
        if(n<=0){ empty_scan(tr("No genres found")); return; }
        for(int i=0;i<n;i++) g_first[i]=first_letter(g_gnames[i]);
        g_count=n; fill_start(n); az_show(1);
    } else if(g_view==VIEW_FAVS){
        /* favourites can outnumber SONG (a favourite whose song left the library stays in MY_LOVE - and in the V2.57
         * queue), so the song-sized buffers must grow to show every one. The rows were cleaned above. */
        lib_ensure_fav_cap(mdb_favorite_count());
        int n=mdb_favorites(g_favs, g_fav_cap < g_grp_cap ? g_fav_cap : g_grp_cap, fw_fav_play_by_love_id());
        if(n<=0){ empty_label(tr("No Favourites yet")); return; }
        add_play_header(0);
        for(int i=0;i<n;i++) g_first[i]=first_letter(g_favs[i].title);
        g_count=n; fill_start(n); az_show(1);
    } else if(g_view==VIEW_MOSTPLAYED || g_view==VIEW_RECENT){
        int n = (g_view==VIEW_MOSTPLAYED) ? mdb_mostplayed(g_favs, g_alloc_n) : mdb_recent(g_favs, g_alloc_n);
        if(n<=0){ empty_label(tr("Nothing played yet")); return; }
        g_count=n; fill_start(n);   /* ordered by plays / recency -> no Play-All header, no A-Z */
    } else { /* VIEW_PLAYLISTS */
        /* grow the buffers to the real playlist count (kept file-scope: playlist_cb reads
         * g_plids[i]/g_plnames[i] after this returns). A partial realloc keeps the old cap. */
        int num = mdb_playlist_num();
        /* overflow-guard the size_t multiply on 32-bit: MDB_STR is the larger stride so it
         * bounds both arrays. A pathological count just keeps the old capacity. */
        if(num > g_plcap && (size_t)num <= ((size_t)-1) / MDB_STR){
            char (*nn)[MDB_STR] = realloc(g_plnames, (size_t)num*MDB_STR);
            long  *ni           = realloc(g_plids,   (size_t)num*sizeof(long));
            if(nn) g_plnames = nn;
            if(ni) g_plids   = ni;
            if(nn && ni) g_plcap = num;
        }
        int n = (g_plcap > 0 && g_plnames && g_plids) ? mdb_playlists(g_plnames, g_plids, g_plcap) : 0;
        /* "New Playlist" is always first so an empty library can still create one */
        lv_obj_t *nr=base_row();
        ui_on(nr, pl_new_cb, LV_EVENT_CLICKED, NULL, "library.pl_new", UI_CORE);
        row_two(nr, tr_sym(LV_SYMBOL_PLUS, "New Playlist"), NULL, NULL);
        theme_list_row(nr);
        for(int i=0;i<n;i++){
            lv_obj_t *r=base_row();
            lv_obj_add_event_cb(r, playlist_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
            int c = mdb_playlist_count(g_plids[i]); if(c < 0) c = 0;
            char sub[24]; snprintf(sub, sizeof sub, "%d song%s", c, c == 1 ? "" : "s");
            row_two(r, g_plnames[i], sub, NULL);                         /* the name, and how many songs under it */
        }
    }
    if(th_braun()){ lv_obj_update_layout(g_list); curve_rows(); }   /* Braun: every view's rows get the Braun styling */
}

/* Twelve session-local views, no flash writes on scrolling. Restore after rebuilding
 * and after the virtual spacers exist; explicit track focus takes priority. */
static void library_reload(void){
    if(!g_list)return;
    if(g_position_key[0]){
        int slot=-1;for(int i=0;i<12;i++)if(!strcmp(g_positions[i].key,g_position_key)){slot=i;break;}
        if(slot<0)slot=(int)(g_position_next++%12);
        snprintf(g_positions[slot].key,POSITION_KEY,"%s",g_position_key);
        g_positions[slot].y=lv_obj_get_scroll_y(g_list);
    }
    char key[POSITION_KEY];position_key(key);g_restore_y=0;
    for(int i=0;i<12;i++)if(!strcmp(g_positions[i].key,key)){g_restore_y=g_positions[i].y;break;}
    g_restoring=1;
    library_reload_inner();
    position_key(g_position_key);
    if(g_restore_y>0 && !g_np_jump){
        lv_obj_update_layout(g_list);lv_obj_scroll_to_y(g_list,g_restore_y,LV_ANIM_OFF);
        lv_obj_update_layout(g_list);virt_follow();curve_rows();pdots_update();
    }
    g_restoring=0;
    if(g_position)lv_obj_add_flag(g_position,LV_OBJ_FLAG_HIDDEN);
}

void library_set_song_click_cb(library_song_click_cb_t cb){ g_song_cb=cb; }

/* Rebuild the current Library view from the DB - used after an external change
 * (e.g. a playlist deleted from the playlist view) so the list isn't stale. */
void library_refresh(void){ if(g_list) library_reload(); }
/* The Artists grouping changed (Settings): an open artist drill's key may not exist in the new grouping, so step back
 * to the Artists list; whatever Library shows is rebuilt from the new keys. */
void library_artist_mode_changed(void){
    if(g_view == VIEW_GROUP && g_drill_kind == 2){ g_view = VIEW_ARTISTS; g_drill_kind = 0; }
    if(g_list) library_reload();
}
lv_obj_t *library_scroller(void){ return g_list; }

/* If the user is inside an album/artist/genre drill-in, report the player
 * list_type (3=album,2=artist,10=genre) + name so a song tap plays the exact
 * track within that list. Returns 0 when in a flat view (Songs/Search/Favs),
 * where the caller falls back to the song's own album. */
int library_drill_context(int *list_type, char *name, int cap){
    if(g_view != VIEW_GROUP) return 0;
    int lt = (g_drill_kind==1)?3 : (g_drill_kind==3)?10 : 2;
    if(list_type) *list_type = lt;
    if(name && cap>0) snprintf(name, cap, "%s", g_drill);
    return 1;
}

/* deep-link from the Now Playing 3-dot menu: jump straight to an album/artist */
void library_open_artists(void){ focus_clear(); g_view=VIEW_ARTISTS; g_drill_kind=0; g_deeplink=1; g_from_artist=0; library_reload(); }   /* Disco menu */
void library_open_songs(void){ focus_clear(); g_view=VIEW_SONGS; g_drill_kind=0; g_deeplink=1; g_from_artist=0; library_reload(); }
void library_open_favourites(void){
    focus_clear(); g_view=VIEW_FAVS; g_drill_kind=0; g_deeplink=1; g_from_artist=0;
    library_reload();
}
void library_open_album(const char *name){
    focus_clear();
    g_view=VIEW_GROUP; g_drill_kind=1; g_deeplink=1; snprintf(g_drill, MDB_STR, "%s", name); library_reload();
}
void library_open_artist_focus(const char *artist, const char *album){
    library_open_artist(artist);                   /* (clears any older focus) */
    snprintf(g_focus_album, MDB_STR, "%s", album ? album : "");
    g_np_jump = 1;
    library_reload();
}
void library_open_artist(const char *name){
    focus_clear();
    /* lands on the artist's album list (Artist > Album > Track); back from there leaves the Library */
    g_view=VIEW_ARTIST_ALBUMS; g_drill_kind=0; g_deeplink=1; g_from_artist=0;
    snprintf(g_artist, MDB_STR, "%s", name); library_reload();
}

/* ---- rim-scroll alphabet hint: a big centred letter shown while flying through
 * an alphabetical list, auto-hidden ~650ms after scrolling stops. -------------- */
static void lhint_timer_cb(lv_timer_t *t){ (void)t;
    if(g_position && !lv_obj_has_flag(g_position,LV_OBJ_FLAG_HIDDEN)){
        uint32_t elapsed=lv_tick_elaps(g_position_tick);
        if(elapsed>650 && !g_position_fading){g_position_fading=1;lv_obj_fade_out(g_position,120,0);}
        if(elapsed>850)lv_obj_add_flag(g_position,LV_OBJ_FLAG_HIDDEN);
    }
    if(g_lhint && !lv_obj_has_flag(g_lhint, LV_OBJ_FLAG_HIDDEN) && lv_tick_elaps(g_lhint_tick) > 650)
        lv_obj_add_flag(g_lhint, LV_OBJ_FLAG_HIDDEN);
}
/* called from the rim-scroll handler while the Library list is the one scrolling */
void library_scroll_letter_tick(void){
    if(!g_list || !g_lhint || g_count <= 0) return;
    /* Only the views that actually fill g_first[] (alphabetical lists) get the A-Z scrubber hint.
     * MostPlayed/Recent/History (and Menu/Playlists) never populate g_first, so reading it there
     * showed stale letters from a prior view. Allowlist, so any new view defaults to no hint. */
    if(!(g_view==VIEW_SONGS || g_view==VIEW_GROUP || g_view==VIEW_ALBUMS ||
         g_view==VIEW_ARTISTS || g_view==VIEW_GENRES || g_view==VIEW_FAVS)) return;
    int pitch = ROW_H + 4;
    int idx = (lv_obj_get_scroll_y(g_list) - g_hdr_extra_px)/pitch - g_has_header;
    if(idx < 0) idx = 0; else if(idx >= g_count) idx = g_count - 1;
    char ch = g_first[idx];
    if(ch && ch != g_lhint_ch){ g_lhint_ch = ch; char b[2]={ch,0}; lv_label_set_text(g_lhint_lbl, b); }
    g_lhint_tick = lv_tick_get();
    lv_obj_clear_flag(g_lhint, LV_OBJ_FLAG_HIDDEN);
}

/* (Re)allocate all per-row buffers to fit `songs` tracks (min 1). The new set is allocated first and swapped in
 * only when all of it succeeded: a miss keeps the working buffers and capacities (a first allocation that misses
 * leaves 0 capacities -> empty views, never OOB). */
static void lib_alloc_buffers(int songs){
    int n = songs > 0 ? songs : 1, gc = n * 2 + 64;   /* group buffers: headroom for tokenized artists */
    const mdb_song_t **buf = malloc((size_t)n * sizeof *buf);
    mdb_song_t *favs = malloc((size_t)n * sizeof *favs);
    char *first = malloc((size_t)gc);
    char (*gnames)[MDB_STR] = malloc((size_t)gc * sizeof *gnames);
    char (*gartists)[MDB_STR] = malloc((size_t)gc * sizeof *gartists);
    int *gcounts = malloc((size_t)gc * sizeof *gcounts);
    if(!buf || !favs || !first || !gnames || !gartists || !gcounts){
        fprintf(stderr, "library: buffer alloc failed for %d songs (keeping %d)\n", n, g_alloc_n);
        free(buf); free(favs); free(first); free(gnames); free(gartists); free(gcounts);
        return;
    }
    free(g_buf); free(g_favs); free(g_first); free(g_gnames); free(g_gartists); free(g_gcounts);
    g_buf = buf; g_favs = favs; g_first = first; g_gnames = gnames; g_gartists = gartists; g_gcounts = gcounts;
    g_alloc_n = n; g_fav_cap = n; g_grp_cap = gc;
}
/* Grow the buffers if the library outgrew them - e.g. a clean first boot allocated for 1 song
 * and a rescan then added thousands. Call after any mdb_load() that may have added tracks. */
void library_ensure_capacity(void){
    int total = mdb_total_song_count();   /* full catalog: unfiltered Favourites/history also fill these buffers */
    if(total > g_alloc_n) lib_alloc_buffers(total);
}

/* Grow ONLY the group buffers (artist/album/genre rows + A-Z first-letters) to hold `need` entries.
 * The default headroom (2*songs+64) is ample, but tokenized artists have no song-bounded ceiling,
 * so the artist view sizes to the exact distinct count first - no silent clipping. A realloc miss
 * leaves the current (smaller) capacity, which callers still respect via g_grp_cap. */
static void lib_ensure_group_cap(int need){
    if(need <= g_grp_cap) return;
    char (*a)[MDB_STR] = realloc(g_gnames,   (size_t)need * sizeof *g_gnames);   if(!a) return; g_gnames   = a;
    char (*b)[MDB_STR] = realloc(g_gartists, (size_t)need * sizeof *g_gartists); if(!b) return; g_gartists = b;
    int   *c           = realloc(g_gcounts,  (size_t)need * sizeof *g_gcounts);  if(!c) return; g_gcounts  = c;
    char  *f           = realloc(g_first,    (size_t)need);                      if(!f) return; g_first    = f;
    g_grp_cap = need;
}
/* Grow g_favs (and g_first, via the group buffers) to `need` favourite rows. realloc keeps the old buffers on a
 * miss, so the list shows what fits and the other views keep working. */
static void lib_ensure_fav_cap(int need){
    if(need > g_fav_cap){
        mdb_song_t *f = realloc(g_favs, (size_t)need * sizeof *g_favs);
        if(f){ g_favs = f; g_fav_cap = need; }
    }
    lib_ensure_group_cap(need);   /* g_first is indexed per favourite row too; retried every visit (no-op when big enough) */
}

void library_create(lv_obj_t *root){
    lv_obj_set_style_bg_color(root, TC(CANVAS), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);

    g_title = ui_header_cb(root, "Library", back_cb);   /* shared header; back_cb pops the internal view stack */

    g_list = lv_obj_create(root);
    lv_obj_remove_style_all(g_list);
    lv_obj_set_pos(g_list, 30, LIST_Y); lv_obj_set_size(g_list, 286, LIST_H);
    lv_obj_set_style_bg_opa(g_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_bottom(g_list, 44, 0);   /* round bottom bezel */
    lv_obj_set_style_pad_row(g_list, 4, 0);
    lv_obj_set_flex_flow(g_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(g_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);   /* narrowed rows stay centred */
    lv_obj_add_event_cb(g_list, curve_scroll_cb, LV_EVENT_SCROLL, NULL);
    pdots_create(root);                          /* after the list, so they draw on top of it */
    lv_obj_set_scroll_dir(g_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(g_list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(g_list, LV_OBJ_FLAG_SCROLL_MOMENTUM);

    /* the "A - Z" pill and its letter grid (azjump.c, shared with Folders) */
    azjump_create(&g_az, root, jump_to_letter, "library");

    /* alphabet hint overlay (rim-scroll position indicator) - centred, hidden */
    g_lhint = lv_obj_create(root);
    lv_obj_remove_style_all(g_lhint);
    lv_obj_set_size(g_lhint, 96, 96);
    lv_obj_center(g_lhint);
    lv_obj_set_style_radius(g_lhint, 22, 0);
    lv_obj_set_style_bg_color(g_lhint, TC(SURFACE), 0);
    lv_obj_set_style_bg_opa(g_lhint, LV_OPA_80, 0);
    lv_obj_clear_flag(g_lhint, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_lhint, LV_OBJ_FLAG_HIDDEN);
    g_lhint_lbl = lv_label_create(g_lhint);
    lv_obj_set_style_text_font(g_lhint_lbl, TF(UI_40), 0);
    lv_obj_set_style_text_color(g_lhint_lbl, TC(TEXT_PRIMARY), 0);
    lv_label_set_text(g_lhint_lbl, "A");
    lv_obj_center(g_lhint_lbl);
    g_position=lv_obj_create(root);lv_obj_remove_style_all(g_position);
    lv_obj_set_size(g_position,108,24);lv_obj_align(g_position,LV_ALIGN_TOP_MID,0,42);
    lv_obj_set_style_bg_color(g_position,TC(SURFACE),0);lv_obj_set_style_bg_opa(g_position,LV_OPA_COVER,0);
    lv_obj_set_style_radius(g_position,LV_RADIUS_CIRCLE,0);lv_obj_clear_flag(g_position,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
    g_position_label=lv_label_create(g_position);lv_label_set_text(g_position_label,"");
    lv_obj_set_style_text_font(g_position_label,TF(UI_14),0);lv_obj_set_style_text_color(g_position_label,TC(TEXT_PRIMARY),0);
    lv_obj_center(g_position_label);lv_obj_add_flag(g_position,LV_OBJ_FLAG_HIDDEN);
    lv_timer_create(lhint_timer_cb, 150, NULL);

    mdb_load();
    int total = mdb_total_song_count();          /* size for the FULL catalog: Favourites/Most-Played/Recent
                                                  * query unfiltered rows (incl. audiobooks), so music-only
                                                  * count would undersize them */
    if(total < 0) total = mdb_song_count() > 256 ? mdb_song_count() : 256;   /* COUNT failed -> generous floor, don't undersize */
    lib_alloc_buffers(total);
    library_reload();
    if(th_braun() && g_list){                                        /* Braun: the grille, the list on the lower segment */
        br_face(root); lv_obj_update_layout(g_list);
        int y = lv_obj_get_y(g_list) - 6; if(y < 40) y = 40;
        lv_obj_move_to_index(br_segment(root, y), 1);
        curvelist_braun_watch(g_list);
    }
}
