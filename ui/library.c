#include "simple_theme.h"
#include "onboarding.h"
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "screens.h"
#include "theme.h"
#include "braun.h"
#include "braun2.h"
#include "curvelist.h"
#include "theme_kit.h"
#include "multipick.h"
#include "folderbrowser.h"
#include "books.h"
#include "musicdb.h"
#include "fwcaps.h"
#include "artcache.h"
#include "azjump.h"
#include "null_theme.h"
#include "ipc.h"
#include "zune_dial.h"
#include "zune_thumbs.h"
#include <stdint.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include "config.h"
#include "i18n.h"

enum { VIEW_MENU, VIEW_SONGS, VIEW_ALBUMS, VIEW_ARTISTS, VIEW_PLAYLISTS, VIEW_FAVS, VIEW_GENRES, VIEW_GROUP, VIEW_MOSTPLAYED, VIEW_RECENT, VIEW_HISTORY, VIEW_ARTIST_ALBUMS, VIEW_COUNT };
#define CAT_FOLDERS 1000   /* menu sentinel: opens the folder browser (a screen, not a library view) */
#define CAT_BOOKS   1001   /* menu sentinel: opens the Books (audiobook) screen */

static int lib_row_h(void);
#define ROW_H lib_row_h()                           /* 52, packed tighter, with larger text (see lib_font); Zune: per view */
#define LIST_Y_BASE 70
#define LIST_H_BASE 252
/* Disco, Squash and Null: the Library's headers sit LIB_LIFT px higher than the shared header, and the lists start
 * that much higher and reach further down, into the room the round bottom used to keep empty. The track sheet uses
 * the same lift so its header lines up. Ring, Braun and Zune keep their layouts. */
#define LIB_LIFT 12
#define LIB_GROW 12   /* extra list length at the bottom */
#define LIB_LIFT_NULL 6   /* Null's wide pixel title needs a longer line than the top 12 px leave beside the arrow */
int library_lift(void){
    if(th_zune() || !(th_disco_family() || th_null())) return 0;
    return th_null() ? LIB_LIFT_NULL : LIB_LIFT;
}
static int lib_grow(void){ return library_lift() ? LIB_GROW : 0; }
#define LIST_Y (LIST_Y_BASE - library_lift())
#define LIST_H (LIST_H_BASE + library_lift() + lib_grow())
/* Move what a header call just added to root (children from index `from` on) up by the lift. Translation, not
 * position, so aligned and absolutely placed header parts move alike; their touch areas move with them. */
/* The raised header line (Disco, Squash, Null): the title centred on the screen, the back arrow on the same line,
 * as far out as the round edge allows with HDR_PAD px to spare, so it is always whole and in view. Squash draws its
 * back arrow without the ring up here - the ring cannot fit beside a centred title this close to the top. */
#define HDR_PAD 8
#define HDR_GAP 8
static int g_arrow_edge = -1;   /* Library header: the arrow's inner edge (x), for the category headers; -1 = unset */
static int hdr_half(int y1, int y2){   /* half the screen's width across rows y1..y2, at the row nearest the rim */
    int dy = abs(y1 - 180) > abs(y2 - 180) ? abs(y1 - 180) : abs(y2 - 180);
    return dy >= 180 ? 0 : curvelist_circle_half(180, dy);
}
/* The header title always fits its line: from the theme's own size down through 22, 20, 18, 16 and 14 px. */
static const lv_font_t *g_title_font0;   /* the Library title's own size, for refitting when its text changes */
static void title_fit(lv_obj_t *title, const char *text, const lv_font_t *f0, int w);
static void title_fit(lv_obj_t *title, const char *text, const lv_font_t *f0, int w){   /* text: the full title (a "..." label reads back shortened) */
    const lv_font_t *steps[6] = { f0, TF(UI_22), TF(UI_20), TF(UI_18), TF(UI_16), TF(UI_14) };
    const lv_font_t *f = f0, *best = f0; int best_w = 1 << 30;
    for(int k = 0; k < 6; k++){   /* the first size (largest first) that fits; failing that, the narrowest */
        lv_point_t ts; lv_text_get_size(&ts, text, steps[k], 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        if(k && lv_font_get_line_height(steps[k]) > lv_font_get_line_height(f0)) continue;   /* never larger than the theme's own */
        if(ts.x <= w - 6){ f = steps[k]; best = NULL; break; }
        if(ts.x < best_w){ best_w = ts.x; best = steps[k]; }
    }
    if(best){   /* too long even small: give it the whole line right of the arrow (left-aligned), then fit again */
        int x = lv_obj_get_x(title), wide = 272 - x;
        if(wide > w + 8){
            lv_obj_set_width(title, wide); lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_LEFT, 0);
            title_fit(title, text, f0, wide); return;
        }
        f = best;
    }
    lv_obj_set_style_text_font(title, f, 0);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);              /* one line, always (a theme may have set wrapping) */
    lv_obj_set_height(title, lv_font_get_line_height(f));
    lv_obj_set_style_translate_y(title, -library_lift() + (lv_font_get_line_height(f0) - lv_font_get_line_height(f)) / 2, 0);   /* stays centred on the line */
}
static int g_hdr_x, g_hdr_w;   /* the centred title box header_align set last */
static int g_title_x0 = -1, g_title_w0;   /* the Library title's centred box, restored before each refit */
void library_title_refit(lv_obj_t *title, const char *text){
    if(!title || !text || !g_title_font0 || !library_lift()) return;
    if(g_title_x0 >= 0){ lv_obj_set_x(title, g_title_x0); lv_obj_set_width(title, g_title_w0); lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0); }
    char up[160]; if(th_null()){ theme_upper(up, sizeof up, text); text = up; }   /* Null: titles in capitals */
    title_fit(title, text, g_title_font0, lv_obj_get_width(title));
    lv_label_set_text(title, text);                                /* re-shorten (if at all) at the new size */
}
static int header_align(lv_obj_t *back, lv_obj_t *title){
    if(!back || !title || back == title) return -1;
    if(th_simple()){ lv_obj_set_style_border_width(back, 0, 0); lv_obj_set_style_bg_opa(back, LV_OPA_TRANSP, 0);
                     lv_obj_set_style_shadow_width(back, 0, 0); }
    lv_obj_t *icon = lv_obj_get_child_count(back) ? lv_obj_get_child(back, 0) : back;
    lv_obj_update_layout(back);
    lv_area_t g; lv_obj_get_coords(icon, &g);                  /* the arrow as drawn (translation included) */
    { lv_area_t ra; lv_obj_get_coords(lv_obj_get_parent(back), &ra);   /* relative to its screen, even mid-slide */
      g.x1 -= ra.x1; g.x2 -= ra.x1; g.y1 -= ra.y1; g.y2 -= ra.y1; }
    lv_point_t gs; lv_text_get_size(&gs, lv_label_get_text(icon), lv_obj_get_style_text_font(icon, 0), 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    int gx1 = (g.x1 + g.x2) / 2 - gs.x / 2, gx2 = gx1 + gs.x;  /* the glyph itself, not its label box */
    int cy = (g.y1 + g.y2) / 2, gh = gs.y * 3 / 5;                /* the arrow's ink: about 3/5 of the line height */
    int half = hdr_half(cy - gh / 2, cy + gh / 2), right_side = (gx1 + gx2) / 2 > 180, edge;
    int dx = right_side ? (180 + half - HDR_PAD) - gx2 : (180 - half + HDR_PAD) - gx1;
    lv_obj_set_x(back, lv_obj_get_x(back) + dx);
    edge = right_side ? gx1 + dx - HDR_GAP : gx2 + dx + HDR_GAP;
    int w = right_side ? 2 * (edge - 180) : 2 * (180 - edge);   /* centred, clear of the arrow on both sides */
    if(w > 220) w = 220;
    if(w < 60) w = 60;
    lv_obj_set_x(title, 180 - w / 2); lv_obj_set_width(title, w); g_hdr_x = 180 - w / 2; g_hdr_w = w;
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    title_fit(title, lv_label_get_text(title), lv_obj_get_style_text_font(title, 0), w);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);              /* a title too long for the line ends in ... */
    return edge;
}
static lv_obj_t *g_title;   /* (defined again below; the Library header title) */
void library_header_lift(lv_obj_t *root, uint32_t from, lv_obj_t *title){
    int dy = library_lift(); if(!dy) return;
    for(uint32_t i = from; i < lv_obj_get_child_count(root); i++) lv_obj_set_style_translate_y(lv_obj_get_child(root, i), -dy, 0);
    if(title && lv_obj_get_child_count(root) > from && lv_obj_check_type(title, &lv_label_class)){
        int edge = header_align(lv_obj_get_child(root, from), title);
        if(title == g_title){ g_arrow_edge = edge; g_title_font0 = (th_simple() || th_null()) ? TF(HEADER) : TF(UI_20);
            g_title_x0 = g_hdr_x; g_title_w0 = g_hdr_w; }
    }
}

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

/* ---- Zune (Figma, Library page): state ---- */
LV_FONT_DECLARE(font_zune_light_18) LV_FONT_DECLARE(font_zune_light_21) LV_FONT_DECLARE(font_zune_light_24)
LV_FONT_DECLARE(font_zune_light_50) LV_FONT_DECLARE(font_zune_regular_13) LV_FONT_DECLARE(font_zune_regular_14)
LV_FONT_DECLARE(font_zune_regular_15) LV_FONT_DECLARE(font_zune_regular_19) LV_FONT_DECLARE(font_zune_semibold_11)
LV_FONT_DECLARE(font_zune_semibold_22)
static lv_obj_t *g_zpiv, *g_zmusic, *g_zmenu, *g_zfade[2], *g_zarc;
static int g_znow_id;                  /* the playing song (its row in the accent) */
static int g_zmenu_focus = 1; /* last Music category */
static int zpiv_index(void);
static void zpiv_go(void *p);
static const char *zlow(const char *t);
static void zpiv_attach(void);
int library_wall_active(void);
static mp_host_t g_mp;
static lv_obj_t *g_category_strip, *g_category_next;
static int category_modern(void){return th_zune()||th_disco_family()||th_null()||th_braun2();}
static void category_click(lv_event_t *e){if(lv_event_get_code(e)==LV_EVENT_CLICKED)lv_async_call(zpiv_go,lv_event_get_user_data(e));}
void library_category_nav(lv_obj_t *root,int index,lv_obj_t *original){
    static const char *const cats[]={"Songs","Albums","Artists","Genres"};
    if(category_modern()){
        int x=th_zune()?100:th_simple()?124:116;
        if(library_lift()&&g_arrow_edge>0&&g_arrow_edge<180)x=g_arrow_edge+4;   /* raised: just right of the back arrow */
        const lv_font_t*f=th_zune()?zune_font(&font_zune_light_50):th_disco()?TF(UI_28):th_braun2()?br_font(22,1):TF(HEADER);
        int cy=32-library_lift();
        if(th_braun2()){x=150;cy=46-lv_font_get_line_height(f)/2-2;}   /* Braun 2.0: on the header's own line, after the back knob */
        lv_obj_t*clip=lv_obj_create(root);lv_obj_remove_style_all(clip);kit_keep(clip);lv_obj_set_pos(clip,x,cy);lv_obj_set_size(clip,360-x,lv_font_get_line_height(f)+4);lv_obj_remove_flag(clip,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t*active=lv_label_create(clip);kit_keep(active);theme_title_text(active,cats[index]);lv_obj_set_style_text_font(active,f,0);lv_obj_set_style_text_color(active,TC(TEXT_PRIMARY),0);lv_obj_set_style_text_letter_space(active,th_zune()?-2:0,0);
        lv_point_t size;lv_text_get_size(&size,lv_label_get_text(active),f,th_zune()?-2:0,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);
        lv_obj_t*next=lv_label_create(clip);kit_keep(next);theme_title_text(next,cats[(index+1)%4]);lv_obj_set_style_text_font(next,f,0);lv_obj_set_style_text_color(next,TC(TEXT_PRIMARY),0);lv_obj_set_style_text_opa(next,LV_OPA_50,0);lv_obj_set_style_text_letter_space(next,th_zune()?-2:0,0);lv_obj_set_pos(next,size.x+24,0);lv_obj_add_flag(next,LV_OBJ_FLAG_CLICKABLE);
        ui_on(next,category_click,LV_EVENT_CLICKED,(void*)(intptr_t)1,"library.category.next",UI_CORE);
        if(original)lv_obj_add_flag(original,LV_OBJ_FLAG_HIDDEN);
        if(root==lv_obj_get_parent(g_list)){g_category_strip=clip;g_category_next=next;g_mp.title=active;}
        zpiv_attach();return;
    }
    static const char *const names[]={"Songs","Albums","Artists","Genres"};
    lv_obj_t *row=lv_obj_create(root);lv_obj_remove_style_all(row);kit_keep(row);lv_obj_set_pos(row,24,76);lv_obj_set_size(row,312,26);lv_obj_remove_flag(row,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
    for(int d=-1;d<=1;d+=2){lv_obj_t*l=lv_label_create(row);kit_keep(l);char b[64];
        snprintf(b,sizeof b,d<0?LV_SYMBOL_LEFT " %s":"%s " LV_SYMBOL_RIGHT,tr(names[(index+d+4)%4]));lv_label_set_text(l,th_zune()?zlow(b):b);lv_label_set_long_mode(l,LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_font(l,TF(UI_14),0);lv_obj_set_style_text_color(l,TC(TEXT_SECONDARY),0);lv_obj_set_style_text_align(l,d<0?LV_TEXT_ALIGN_LEFT:LV_TEXT_ALIGN_RIGHT,0);lv_obj_set_size(l,130,24);lv_obj_set_pos(l,d<0?0:182,0);
        lv_obj_add_flag(l,LV_OBJ_FLAG_CLICKABLE);ui_on(l,category_click,LV_EVENT_CLICKED,(void*)(intptr_t)d,d<0?"library.category.prev":"library.category.next",UI_CORE);
    }
    if(root==lv_obj_get_parent(g_list))g_category_strip=row;
    zpiv_attach();
}
static void category_refresh(void){
    int k=zpiv_index();
    if(g_category_strip){lv_obj_delete(g_category_strip);g_category_strip=NULL;g_category_next=NULL;}
    g_mp.title=g_title;if(g_title)lv_obj_remove_flag(g_title,LV_OBJ_FLAG_HIDDEN);
    if(k>=0)library_category_nav(lv_obj_get_parent(g_list),k,g_title);
    if(!th_zune()){
        lv_obj_set_pos(g_list,library_wall_active()?0:22,k>=0?(category_modern()?96-library_lift()-2:106):LIST_Y);lv_obj_set_size(g_list,library_wall_active()?360:316,k>=0?(category_modern()?226+library_lift()+2+lib_grow():216):LIST_H);
        lv_obj_set_flex_align(g_list,LV_FLEX_ALIGN_START,library_wall_active()?LV_FLEX_ALIGN_START:LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_CENTER);
    }
    if(library_wall_active()){lv_obj_set_width(g_list,360);lv_obj_set_x(g_list,0);}
}

static int *g_zrep, *g_zac, *g_zsc;    /* album tile covers (a song per album); artist album / song counts (-1 = not yet) */
static int g_znalb;                    /* albums on the tile wall (rows hold three) */
static int lib_zalbum(void){ return g_view==VIEW_GROUP && (g_drill_kind==1 || g_scope_lvl==2); }   /* one album's songs */
int library_wall_active(void){return g_view==VIEW_ALBUMS && simple_cfg_get_int("album_view",0)==1;}
static int lib_row_h(void){
    if(th_braun2())return 56;
    if(library_wall_active())return 98;
    if(!th_zune()) return th_null()?60:52;
    if(lib_zalbum()) return theme_font_size()==2?36:30;                             /* compact track rows under the album header */
    return theme_font_size()==2?64:54;
}
static void zrow_two(lv_obj_t *r, const char *top, const char *sub, const char *right, int now);
static void zcurve_rows(void);
static void zscroll_update(void);
static const char *zlow(const char *t);

/* All per-row buffers are dynamically sized to the library song count (g_alloc_n),
 * so there is NO fixed song/group cap - every view (songs, albums≤N, artists≤N,
 * genres≤N, group-songs≤N) fits. Favourites can exceed N (MY_LOVE keeps a favourite whose song left the library),
 * so g_favs grows to the favourite count on its own (g_fav_cap). Allocated after mdb_load(); regrown after a
 * rescan (library_ensure_capacity) and for favourites (lib_ensure_fav_cap). */
static int  g_alloc_n = 0;              /* song-bounded buffers (g_buf; g_favs for Most-Played/Recent) */
static int  g_fav_cap = 0;              /* history/favourites slots, allocated lazily (lib_ensure_fav_cap) */
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
/* the "12 / 80" pill sits on the header line, in the title's place - the title steps aside while it shows,
 * so the pill never covers a row or the title (Disco, Squash, Null; elsewhere it keeps its old place) */
static void pos_header(int pill_on){
    if(!library_lift()) return;
    lv_obj_t *o[2] = { g_title, g_category_strip };
    for(int k = 0; k < 2; k++) if(o[k]){
        if(pill_on) lv_obj_set_style_opa(o[k], LV_OPA_TRANSP, 0);
        else lv_obj_remove_local_style_prop(o[k], LV_STYLE_OPA, 0);
    }
}
static void position_tick(void){
    if(g_restoring || !g_position || g_count < 12 || th_zune() || th_braun2()) return;   /* Braun 2.0: no "12 / 340" readout while scrolling */
    int idx=(lv_obj_get_scroll_y(g_list)-g_hdr_extra_px)/(ROW_H+4)-g_has_header;
    if(library_wall_active())idx*=3;
    if(idx<0)idx=0; if(idx>=g_count)idx=g_count-1;
    char b[32]; snprintf(b,sizeof b,"%d / %d",idx+1,g_count);
    if(strcmp(lv_label_get_text(g_position_label),b))lv_label_set_text(g_position_label,b);
    lv_anim_delete(g_position,NULL);lv_obj_set_style_opa(g_position,LV_OPA_COVER,0);
    lv_obj_remove_flag(g_position,LV_OBJ_FLAG_HIDDEN);g_position_tick=lv_tick_get();g_position_fading=0;
    pos_header(1);
}
static void focus_clear(void);
static void sel_exit(void);           /* stop picking several items (multi-select, below) */
/* g_mp is declared with the category header, whose title follows selection. */
static void az_show(int on);
static void focus_scroll_begin(lv_event_t*);
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
#define LEAN 12                /* Disco: rows reach this much further into the free edge (the back swipe needs less) */
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
/* Disco: every row reaches LEAN px further into the free edge (the back swipe needs less): the same curve, moved over,
 * so the rows keep their shape relative to each other while scrolling; the right ends stay where they were */
static void lean_free_edge(int y1, int y2, int *w, int *shift){
    (void)y1; (void)y2;
    *w += LEAN; *shift += disco_mirror() ? LEAN / 2 : -LEAN / 2;
}
int disco_mirror(void);
static void curve_rows(void);
/* Wheel: the space above the first row (it waits in the middle) holds the list's size - "80 songs" - large and
 * quiet, and scrolls away with the list. Only on the main lists; drilled views have their own first rows. */
static void wheel_lead(void){
    static const char *NOUN[][2] = { [VIEW_SONGS]={"song","songs"}, [VIEW_ALBUMS]={"album","albums"}, [VIEW_ARTISTS]={"artist","artists"},
                                     [VIEW_GENRES]={"genre","genres"}, [VIEW_PLAYLISTS]={"playlist","playlists"}, [VIEW_FAVS]={"favourite","favourites"} };
    int n = g_count;
    if(g_view == VIEW_PLAYLISTS){   /* the playlists' rows, less "New Playlist" */
        n = 0; for(uint32_t i = 0; i < lv_obj_get_child_count(g_list); i++) if(lv_obj_has_flag(lv_obj_get_child(g_list, i), LV_OBJ_FLAG_USER_1)) n++;
        n--;
    }
    int ok = (g_view==VIEW_SONGS||g_view==VIEW_ALBUMS||g_view==VIEW_ARTISTS||g_view==VIEW_GENRES||g_view==VIEW_PLAYLISTS||g_view==VIEW_FAVS) && n > 0;
    lv_obj_t *first = lv_obj_get_child_count(g_list) ? lv_obj_get_child(g_list, 0) : NULL;
    int has = first && lv_obj_has_flag(first, LV_OBJ_FLAG_USER_3) && !lv_obj_has_flag(first, LV_OBJ_FLAG_USER_1);
    if(!ok){ if(has) lv_obj_delete(first); return; }
    lv_obj_t *lead = first, *num, *noun;
    if(!has){
        lead = lv_obj_create(g_list); lv_obj_remove_style_all(lead); kit_keep(lead);
        lv_obj_set_size(lead, lv_pct(100), 80); lv_obj_move_to_index(lead, 0);
        lv_obj_add_flag(lead, LV_OBJ_FLAG_USER_3); lv_obj_remove_flag(lead, LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        num = lv_label_create(lead); kit_keep(num); lv_obj_set_style_text_font(num, TF(UI_28), 0);
        lv_obj_set_style_text_color(num, theme_color_mix(TC(CANVAS), TC(TEXT_PRIMARY), 120), 0);
        noun = lv_label_create(lead); kit_keep(noun); lv_obj_set_style_text_font(noun, TF(UI_14), 0);
        lv_obj_set_style_text_color(noun, theme_color_mix(TC(CANVAS), TC(TEXT_PRIMARY), 150), 0);
    } else { num = lv_obj_get_child(lead, 0); noun = lv_obj_get_child(lead, 1); }
    char b[16]; snprintf(b, sizeof b, "%d", n);
    if(strcmp(lv_label_get_text(num), b)) lv_label_set_text(num, b);
    const char *w = tr(NOUN[g_view][n == 1 ? 0 : 1]);
    if(strcmp(lv_label_get_text(noun), w)) lv_label_set_text(noun, w);
    int lx = disco_mirror() ? 20 : -14;   /* clear of Disco's navigation shard */
    lv_obj_align(num, LV_ALIGN_BOTTOM_MID, lx, -22); lv_obj_align(noun, LV_ALIGN_BOTTOM_MID, lx, -4);
}
static int g_wheel_settle;
static void wheel_settle_rows(lv_timer_t *t){ (void)t; g_wheel_settle = 2; curve_rows(); g_wheel_settle = 0; }   /* 2: no new settle from inside */
static void curve_rows(void){
    if(!g_list) return;
    if(library_wall_active())return;
    if(th_zune()){ zcurve_rows(); return; }
    if(th_braun_family()){                                                  /* Braun: straight rows, restyled */
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
    if(curvelist_wheel()){
        wheel_lead();
        if(!g_wheel_settle){ g_wheel_settle = 1;   /* once the theme has styled the rows: place them again shortly after */
            lv_timer_t *st = lv_timer_create(wheel_settle_rows, 80, NULL); lv_timer_set_repeat_count(st, 1); }
        curvelist_wheel_list(g_list, ROW_H);
        for(uint32_t i = 0; i < n; i++){ lv_obj_t *r = lv_obj_get_child(g_list, i); if(lv_obj_has_flag(r, LV_OBJ_FLAG_USER_1)) curvelist_wheel_row(r, g_list); }
        return;
    }
    for(uint32_t i = 0; i < n; i++){
        lv_obj_t *r = lv_obj_get_child(g_list, i);
        if(!lv_obj_has_flag(r, LV_OBJ_FLAG_USER_1)) continue;      /* only plain list rows curve */
        lv_area_t a; lv_obj_get_coords(r, &a);
        if(a.y2 < -40 || a.y1 > 400) continue;                     /* off-screen: nothing to do */
        int w, shift;
        if(curvelist_disco_geom(mode, a.y1, a.y2, ROW_W_FULL, &w, &shift)){
            lean_free_edge(a.y1, a.y2, &w, &shift);
        } else {
        int dy = abs((a.y1 + a.y2) / 2 - 180) + ROW_H / 2;          /* the row's far edge sets the limit */
        int half = dy < 176 ? curvelist_circle_half(180,dy) - 8 : 0;
        w = 2 * half;
        if(w > ROW_W_FULL) w = ROW_W_FULL;
        if(w < 186) w = 186;
        shift = 0; int L = disco_clear_left(a.y1, a.y2);
        { int R = disco_clear_right(a.y1, a.y2);                              /* Disco: clear of the closed sliver */
          if(R && 180 + w / 2 > R){ int lft = 180 - w / 2; w = R - lft; shift = (lft + R) / 2 - 180; } }
        if(th_disco_family()) lean_free_edge(a.y1, a.y2, &w, &shift);
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
    if(th_braun_family()) return;                                           /* Braun: no position dots */
    if(th_zune()){ zscroll_update(); return; }                       /* Zune: a short arc on the rim + the letter dot */
    if(!g_pdot[0] || !g_list) return;
    lv_obj_update_layout(g_list);
    int32_t sy = lv_obj_get_scroll_y(g_list), sb = lv_obj_get_scroll_bottom(g_list), tot = sy + sb;
    if(g_count>0 && g_first && g_az.btn && !lv_obj_has_flag(g_az.btn,LV_OBJ_FLAG_HIDDEN)){
        int row=(sy-g_hdr_extra_px)/(ROW_H+4)-g_has_header;
        if(library_wall_active())row*=3;
        if(row<0)row=0;if(row>=g_count)row=g_count-1;
        float fraction=tot>0?(float)sy/tot:0;if(fraction<0)fraction=0;if(fraction>1)fraction=1;
        float angle=(212-64*fraction)*3.14159265f/180;
        int x=th_null()?30:disco_mx((int)(180+153*cosf(angle))-14,28)+14;
        int y=th_null()?108+(int)(140*fraction):(int)(180+153*sinf(angle));
        int at=row;   /* the selected row: the top one, or in Wheel the one in the middle (its padding makes scroll 0 = row 0 centred) */
        azjump_index(&g_az,g_first,g_count,g_first[at]);   /* the pickers: which letters exist, and the selected row's letter */
        azjump_position(&g_az,g_first[at],x,y);
    }
    if(th_null()){null_scroll_line(g_pdot,N_PDOTS,sy,tot);return;}
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
    lv_obj_set_size(r, th_zune() ? 300 : 268, ROW_H);
    lv_obj_add_flag(r, LV_OBJ_FLAG_USER_1);                         /* eligible for the curve */
    lv_obj_set_style_radius(r, 10, 0);
    lv_obj_set_style_bg_color(r, TC(SURFACE), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(r, LV_OPA_70, LV_STATE_PRESSED);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
    return r;
}
/* Library text by Settings > Display > Font Size: titles 18 / 20 / 22 px, second lines 16 / 18 / 20 (one step up from
 * 1.4.0 at every size). Montserrat -> Inter -> international -> CJK, like all user text. */
static const lv_font_t *lib_font(int small_px){ return th_null()?theme_font(small_px>=18?THEME_FONT_USER_20:THEME_FONT_USER_18):ui_font_cjk(small_px + 2 * theme_font_size()); }
static int *g_arep, g_arep_cap;   /* Artist -> Albums: one song id per album row, for its cover */
static void row_two(lv_obj_t *r, const char *top, const char *sub, const char *right){
    if(th_zune()){ zrow_two(r, top, sub, right, 0); return; }
    const lv_font_t *ft = lib_font(18), *fs = lib_font(16);
    int h1 = lv_font_get_line_height(ft), h2 = lv_font_get_line_height(fs), two = sub && sub[0];
    int y1 = (ROW_H - (two ? h1 + h2 : h1)) / 2; if(y1 < 0) y1 = 0;
    int tw = 242;                                                   /* the text runs up to the value on the right */
    if(right && right[0]){ lv_point_t sz; lv_text_get_size(&sz, right, TF(UI_18), 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
                           tw = 256 - 12 - sz.x - 8; if(tw > 230) tw = 230; }
    lv_obj_t *t = lv_label_create(r);
    lv_label_set_text(t, top); lv_label_set_long_mode(t, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_pos(t, 12, y1); lv_obj_set_size(t, tw, h1);
    lv_obj_set_style_text_font(t, ft, 0);   /* CJK titles render via Source Han Sans fallback */
    lv_obj_set_style_text_color(t, TC(TEXT_PRIMARY), 0);
    if(two){
        lv_obj_t *s = lv_label_create(r);
        lv_label_set_text(s, sub); lv_label_set_long_mode(s, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_set_pos(s, 12, y1 + h1); lv_obj_set_size(s, tw, h2);   /* clear of the duration */
        lv_obj_set_style_text_font(s, fs, 0);
        lv_obj_set_style_text_color(s, TC(TEXT_LYRICS), 0);
    }
    if(right && right[0]){
        lv_obj_t *rl = lv_label_create(r);
        lv_label_set_text(rl, right);
        const lv_font_t *fr = TF(UI_18); int hr = lv_font_get_line_height(fr);
        lv_obj_set_pos(rl, 196, (ROW_H - hr) / 2); lv_obj_set_size(rl, 60, hr);
        lv_obj_set_style_text_align(rl, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_style_text_font(rl, fr, 0);
        lv_obj_set_style_text_color(rl, TC(TEXT_MUTED), 0);
    }
    ui_library_row(r);
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
    lv_label_set_long_mode(s, LV_LABEL_LONG_SCROLL_CIRCULAR);
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
    if(lt == 2){                               /* an artist: through the artist plan (exactly what the view shows), or nothing */
        mdb_plan_t plan; int ok = 0;
        if(!mdb_artist_plan(all ? g_artist : g_gnames[gi], &plan)){ ui_toast("Couldn't play that list"); return; }   /* "All Songs" = the artist */
        int i = (shuffle && plan.count > 1) ? rand() % plan.count : 0;   /* shuffle starts anywhere, as Play All's does */
        ok = ui_play_plan(&plan, plan.ids[i]); mdb_plan_free(&plan);
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
static void menu_go(int v){
    if(th_zune() && v==VIEW_HISTORY) v = VIEW_RECENT;    /* Zune (Figma 30): history is the recently played list itself */
    if(v==CAT_FOLDERS){ folderbrowser_open(); return; }   /* opens a screen, not a library view */
    if(v==CAT_BOOKS){ books_open(); return; }             /* audiobooks: a screen, not a library view */
    if(v==VIEW_ALBUMS && simple_cfg_get_int("album_view", 0)==2){ screen_show(SCR_ALBUMWALL); return; }  /* cover flow */
    g_view=v;
    g_drill_kind=0; library_reload();
}
static void menu_cb(lv_event_t *e){
    if(lv_event_get_code(e)!=LV_EVENT_CLICKED) return;
    menu_go((int)(uintptr_t)lv_event_get_user_data(e));
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
    if(mp_active(&g_mp)){ sel_exit(); return 1; }   /* picking: Back just stops picking */
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
    if(th_zune() && g_view==VIEW_MOSTPLAYED){ g_view=VIEW_RECENT; library_reload(); return 1; }   /* Zune: most played -> history */
    if(th_zune() && g_view==VIEW_RECENT){ g_view=VIEW_MENU; library_reload(); return 1; }
    if(g_view==VIEW_MOSTPLAYED || g_view==VIEW_RECENT){ g_view=VIEW_HISTORY; library_reload(); return 1; }  /* stats -> History */
    if(g_view==VIEW_FAVS && g_deeplink){ g_deeplink=0; g_view=VIEW_MENU; library_reload(); return 0; }
    if(g_view!=VIEW_MENU){ g_view=VIEW_MENU; library_reload(); return 1; }
    return 0;
}
/* the header's back arrow: one level UP the hierarchy, never out of a drill the user was placed in */
static int library_up(void){
    if(mp_active(&g_mp)){ sel_exit(); return 1; }
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
/* A location marker belongs to the initial Music jump, not subsequent manual
 * browsing. Scroll ownership distinguishes a real drag from automatic positioning. */
static void focus_scroll_begin(lv_event_t*e){
    (void)e;lv_indev_t*in=lv_indev_active();
    if(!th_simple()||!in||lv_indev_get_type(in)!=LV_INDEV_TYPE_POINTER||lv_indev_get_scroll_obj(in)!=g_list)return;
    if(g_focus_row){
        lv_obj_set_style_bg_opa(g_focus_row,LV_OPA_TRANSP,0);
        lv_obj_set_style_radius(g_focus_row,LV_RADIUS_CIRCLE,0);
        lv_obj_t*t=lv_obj_get_child(g_focus_row,0);
        if(t&&lv_obj_check_type(t,&lv_label_class))lv_obj_set_style_text_color(t,TC(TEXT_PRIMARY),0);
    }
    g_focus_album[0]=g_focus_artist[0]=g_focus_path[0]=0;g_focus_idx=-1;g_focus_row=NULL;
    /* Keep g_np_jump: the back gesture must still return to Music. */
}
static void focus_mark(lv_obj_t *r){   /* lifted row + accent title: "you are here" */
    if(th_zune()){ lv_obj_t *t = lv_obj_get_child(r, 0);            /* Zune: the title in the accent, nothing lifted */
        if(t && lv_obj_check_type(t, &lv_label_class)) lv_obj_set_style_text_color(t, TC(ACCENT_PRIMARY), 0);
        g_focus_row = r; return; }
    lv_obj_set_style_bg_color(r, TC(SURFACE_SELECTED), 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(r, 14, 0);
    lv_obj_t *t = lv_obj_get_child(r, 0);
    if(t && lv_obj_check_type(t, &lv_label_class)){
        lv_obj_set_style_text_color(t, TC(ACCENT_PRIMARY), 0);
        lv_label_set_long_mode(t,LV_LABEL_LONG_SCROLL_CIRCULAR);
    }
    g_focus_row = r;
}
/* ---- Picking several items (multi-select, multipick.c): tracks, albums and artists ------------------------------ */
static int lib_pick_ok(void){
    switch(g_view){
        case VIEW_SONGS: case VIEW_ARTISTS: return 1;
        case VIEW_ALBUMS: return !library_wall_active();                         /* Zune: tiles, three to a row */
        case VIEW_GROUP: return !(g_drill_kind != 1 && g_scope_lvl == 1);   /* an album's / artist's songs, not a scope's albums */
        default: return 0;
    }
}
static int lib_pick_count(void){ return g_count; }
static int lib_is_song_view(void){ return g_view == VIEW_SONGS || g_view == VIEW_GROUP; }
/* A growing list of song IDs with one failure flag: no early returns that could leave temporary buffers behind. */
typedef struct { int *ids; int n, room, fail; } idlist_t;
#define PICK_IDS_MAX 20000
static void ids_add(idlist_t *a, int id){
    if(a->fail || id <= 0) return;
    if(a->n == a->room){
        int r = a->room ? a->room * 2 : 256; if(r > PICK_IDS_MAX) r = PICK_IDS_MAX;
        int *m = r > a->room ? realloc(a->ids, sizeof *m * (size_t)r) : NULL;
        if(!m){ a->fail = 1; return; }                    /* out of memory or over the ceiling: reported, not truncated */
        a->ids = m; a->room = r;
    }
    a->ids[a->n++] = id;
}
static int album_order(const void *x, const void *y){       /* disc, track, then title: the album's own order */
    const mdb_song_t *a = *(const mdb_song_t *const *)x, *b = *(const mdb_song_t *const *)y;
    if(a->disc != b->disc) return a->disc < b->disc ? -1 : 1;
    if(a->track != b->track) return a->track < b->track ? -1 : 1;
    return strcasecmp(a->title, b->title);
}
/* Every song of album i as shown in the list: the same album name AND the artist shown with it, however many. */
static void album_ids(idlist_t *a, int i){
    int total = mdb_song_count(), m = 0;
    const mdb_song_t **sl = malloc(sizeof *sl * (size_t)(total > 0 ? total : 1));
    if(!sl){ a->fail = 1; return; }
    for(int k = 0; k < total; k++){
        const mdb_song_t *so = mdb_song(k);
        if(!so || strcmp(so->album, g_gnames[i])) continue;
        if(g_gartists[i][0] && strcasecmp(so->artist_group, g_gartists[i]) && strcasecmp(so->artist, g_gartists[i])) continue;
        sl[m++] = so;
    }
    qsort(sl, (size_t)m, sizeof *sl, album_order);
    for(int k = 0; k < m; k++) ids_add(a, sl[k]->id);
    free(sl);
}
/* The ticked items as exact songs, in list order: the one list Play, Queue, Playlist and Favourites all use. */
static int lib_pick_collect(const uint8_t *sel, int cap, int **out){
    idlist_t a = {0};
    for(int i = 0; i < g_count && i < cap && !a.fail; i++){
        if(!sel[i]) continue;
        if(lib_is_song_view()) ids_add(&a, g_buf[i]->id);
        else if(g_view == VIEW_ARTISTS){
            mdb_plan_t pl = {0};
            if(mdb_artist_plan(g_gnames[i], &pl)){ for(int k = 0; k < pl.count; k++) ids_add(&a, pl.ids[k]); mdb_plan_free(&pl); }
            else a.fail = 1;
        }
        else if(g_view == VIEW_ALBUMS) album_ids(&a, i);
    }
    if(a.fail){ free(a.ids); *out = NULL; return -1; }
    *out = a.ids; return a.n;
}
static void lib_pick_az(int show){az_show(show?g_count>=20:0);if(g_category_next){if(show)lv_obj_remove_flag(g_category_next,LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(g_category_next,LV_OBJ_FLAG_HIDDEN);}}
static void sel_exit(void){ mp_exit(&g_mp); }
int library_picking(void){ return mp_active(&g_mp); }
void library_pick_demo(const char *csv){ mp_demo(&g_mp, csv); }
static lv_obj_t *base_row(void);
static void zadd_row(lv_obj_t *r, int i);
static void zalbum_row(lv_obj_t *r,int i);
static int  zrow_special(int i);
static void add_row(int i){
    int focus_me = (i == g_focus_idx);
    char dur[12];
    lv_obj_t *r = base_row();
    if(library_wall_active()){zalbum_row(r,i);return;}
    if(th_zune() && zrow_special(i)){ zadd_row(r, i); if(focus_me) focus_mark(r); return; }
    if(lib_pick_ok()) mp_row(&g_mp, r, i);                          /* swipe to pick (runs before the row's own tap/hold) */
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
            if(g_view==VIEW_GROUP && (g_drill_kind==1 || g_scope_lvl==2) && g_buf[i]->track > 0 && simple_cfg_get_int("track_numbers", 0)){
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
            char cnt[24];
            if(library_lift() && i > 0 && g_arep && g_arep_cap > i){   /* a small cover, the title, "N songs" under it */
                snprintf(cnt, sizeof cnt, "%d song%s", g_gcounts[i], g_gcounts[i] == 1 ? "" : "s");
                lv_obj_t *img = lv_image_create(r); lv_obj_remove_flag(img, LV_OBJ_FLAG_CLICKABLE);
                lv_obj_set_size(img, 40, 40); lv_obj_set_pos(img, 8, (ROW_H - 40) / 2);
                lv_obj_set_style_radius(img, th_null() ? 0 : 6, 0); lv_obj_set_style_clip_corner(img, !th_null(), 0);
                lv_obj_set_style_bg_color(img, TC(SURFACE_RAISED), 0); lv_obj_set_style_bg_opa(img, LV_OPA_COVER, 0);
                zthumb_force(1); zthumb_bind(img, g_arep[i], 40);
                row_two(r, g_gnames[i], cnt, NULL);
                for(uint32_t k = 0; k < lv_obj_get_child_count(r); k++){   /* the text moves right of the cover */
                    lv_obj_t *c = lv_obj_get_child(r, k);
                    if(lv_obj_check_type(c, &lv_label_class)){ lv_obj_set_x(c, 58); lv_obj_set_width(c, 196); }
                }
            } else {
                snprintf(cnt,sizeof cnt,"%d",g_gcounts[i]);
                row_two(r, g_gnames[i], NULL, cnt);
            }
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
            ui_on(r, song_cb, LV_EVENT_SHORT_CLICKED, (void*)(uintptr_t)g_favs[i].id,"library.history.song",UI_CORE);
            ui_on(r, song_long_cb, LV_EVENT_LONG_PRESSED, (void*)(uintptr_t)g_favs[i].id,"library.history.song.long",UI_CORE);
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
    if(library_wall_active()){ idx /= 3; if(g_virt){ virt_window(idx - VIRT_AHEAD, idx + LIST_H / PITCH + 1 + VIRT_AHEAD); lv_obj_update_layout(g_list); } }
    lv_obj_scroll_to_y(g_list, g_hdr_extra_px + (idx + g_has_header)*(ROW_H+4), LV_ANIM_OFF);
    curve_rows(); pdots_update();
}
static void az_show(int on){
    azjump_show(&g_az, on);
    if(on)pdots_update();
    if(th_zune()||library_wall_active()){ if(g_list){ lv_obj_update_layout(g_list); curve_rows(); } return; }   /* the rows stop short of the letter dot */
    /* fork: the list always sits on the centre line, rows centred, so the curve narrows them evenly on both sides
     * (as History and the folder browser do); the A-Z button sits just outside the widest row on the right rim */
    if(g_list){
        lv_obj_set_x(g_list, 22);
        lv_obj_set_flex_align(g_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    }
}

/* ---- populate ----------------------------------------------------------- */
static lv_obj_t *empty_label(const char *txt){
    if(th_zune()){                                              /* Figma 35: one big statement, a line of help */
        const char *help = NULL;
        if(strstr(txt, "avourite")) help = "hold any song and choose favourite.";
        else if(strstr(txt, "played")) help = "songs you play show up here, newest first.";
        zune_empty(g_list, strstr(txt, "found") ? "nothing here yet" : txt, help);
        return NULL;
    }
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
    if(th_zune()){
        zune_empty(g_list, "nothing here yet", "the library fills from your card. scan it to look for music.");
        LV_FONT_DECLARE(font_zune_regular_17)
        lv_obj_t *l = lv_label_create(g_list); kit_keep(l); lv_label_set_text(l, "scan library");
        lv_obj_set_style_text_font(l, zune_font(&font_zune_regular_17), 0); lv_obj_set_style_text_color(l, TC(ACCENT_PRIMARY), 0);
        lv_obj_set_style_margin_top(l, 14, 0); lv_obj_add_flag(l, LV_OBJ_FLAG_CLICKABLE); lv_obj_set_ext_click_area(l, 14);
        lv_obj_add_event_cb(l, scan_action_cb, LV_EVENT_CLICKED, NULL);
        return;
    }
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
/* Shared by Library > Songs and the menu shortcut: the stock all-songs list,
 * not whichever artist/album/favourites view was last opened. */
static int play_all_songs(int mode){
    int count=mdb_song_count();
    if(count<=0){ui_toast("Your music library is empty");return 0;}
    ui_set_workmode(mode);
    int pos=mode==1 && count>1?1+rand()%count:1;
    if(!ui_play_list(1,"",pos))return 0;
    screen_show(SCR_NOWPLAYING);
    return 1;
}
int library_shuffle_all(void){return play_all_songs(1);}
static void play_list_mode(int mode){
    if(g_view==VIEW_SONGS){play_all_songs(mode);return;}
    if(g_view==VIEW_GROUP && g_drill_kind!=1 && g_scope_lvl==2){     /* one album of an artist / genre: its own stock queue */
        simple_cfg_set_int("work_mode", mode); ui_set_workmode(mode);
        if(scope_play_album(g_scope_album, mode == 1 ? -2 : -1)) screen_show(SCR_NOWPLAYING);
        return;
    }
    int lt; char nm[160];
    if(!current_list_context(&lt, nm, sizeof nm)) return;
    simple_cfg_set_int("work_mode", mode); ui_set_workmode(mode);
    int pos = 1;
    /* shuffle: start on a random track (not always the first). Skip artist lists (lt==2):
     * the UI's split-artist count can exceed the player's exact ARTIST=? list -> out-of-range pos. */
    if(mode == 1 && g_count > 1 && lt != 2) pos = 1 + (rand() % g_count);
    /* V2.57 favourites start from a MY_LOVE.ID, not a position (index 0 is refused) */
    if(lt == 6 && fw_fav_play_by_love_id()){ if(pos > g_count || !ui_play_favorite(g_favs[pos-1].love_id, pos)) return; }
    else if(lt == 2){                              /* an artist: through the artist plan (exactly what the view shows), or nothing */
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
    lv_obj_set_size(b, th_braun2() ? 112 : 128, 44); lv_obj_set_pos(b, x, 4);   /* Braun 2.0: a pair that fits inside the circle */
    lv_obj_set_style_radius(b, th_braun2() ? 22 : 12, 0);
    int br = th_braun_family();                                            /* Braun: flat off-white keys, dark type */
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
    if(th_braun2()){ lv_obj_set_style_shadow_color(b, TC(ACCENT_PRIMARY), 0); lv_obj_set_style_shadow_width(b, 14, 0); lv_obj_set_style_shadow_opa(b, 46, 0);
                     lv_obj_set_style_bg_color(b, TC(SURFACE_STRONG), 0); }   /* the theme's soft orange glow */
}
/* Album detail header: the album cover on top (from the art cache) with the artist beneath it, shown
 * above the Play All / Shuffle row for an album drill - from EITHER the list view or the cover flow. */
static int g_hdr_cover_ctr;
/* Wheel album view (Disco, Squash, Null): the album's first row - its cover, with Play and Shuffle pills in a column
 * on its right, the album name and (smaller) the artist centred under them. It is a wheel row like the songs:
 * in the middle when the album opens, shrinking into the slot above once a song is in the middle. +Playlist lives in
 * the album's hold menu. */
static void wheel_pill_btn(lv_obj_t *r, int cx, int y, const char *glyph, const char *text, int primary, lv_event_cb_t cb, const char *id){
    lv_obj_t *b = lv_button_create(r); lv_obj_remove_style_all(b);
    lv_obj_set_size(b, 108, 40); lv_obj_align(b, LV_ALIGN_TOP_MID, cx, y);
    lv_obj_set_ext_click_area(b, 4);
    ui_on(b, cb, LV_EVENT_CLICKED, NULL, id, UI_CORE);
    lv_obj_t *l = lv_label_create(b); lv_label_set_text_fmt(l, "%s  %s", glyph, text); lv_obj_center(l);
    ui_pill_style(b, primary);
}
/* Set when an album opens with this header: it then starts raised under the screen header (library_reload). */
static int g_album_wheel_hdr;
#define AWH_H 164        /* 156 of content and 8 clear below the artist (curvelist keeps the songs below its drawn bottom) */
#define AWH_RAISE 20     /* how far under the list's top edge it rests: just past the soft top fade */
static void add_album_wheel_header(const char *album, const char *artist){
    lv_obj_t *r = lv_obj_create(g_list);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, 286, AWH_H);
    g_hdr_extra_px = AWH_H + 4; g_album_wheel_hdr = 1;
    lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(r, LV_OBJ_FLAG_USER_1);                 /* a wheel row */
    lv_obj_set_style_transform_pivot_x(r, 143, 0); lv_obj_add_flag(r, LV_OBJ_FLAG_USER_2);   /* centred content: it grows and shrinks from its middle */
    lv_obj_update_layout(g_list);   /* centre on the screen, wherever this list sits */
    lv_area_t la; lv_obj_get_coords(r, &la); const int CX = 180 - (la.x1 + la.x2) / 2;
    /* the cover through the thumbnail worker (decoded to 96 px in memory: a file image cannot be scaled here) */
    int ids[6]; int n = mdb_album_track_ids(album, ids, 6);
    lv_obj_t *ph = lv_obj_create(r); lv_obj_remove_style_all(ph);
    lv_obj_set_size(ph, 96, 96); lv_obj_align(ph, LV_ALIGN_TOP_MID, CX - 58, 4);   /* cover left, the buttons in a column on its right */
    lv_obj_set_style_bg_color(ph, TC(SURFACE_RAISED), 0); lv_obj_set_style_bg_opa(ph, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(ph, 10, 0); lv_obj_set_style_clip_corner(ph, true, 0);
    lv_obj_t *in = lv_label_create(ph); char init[8] = { album && album[0] ? album[0] : '?', 0 };
    lv_label_set_text(in, init); lv_obj_set_style_text_font(in, TF(UI_28), 0);
    lv_obj_set_style_text_color(in, TC(TEXT_MUTED), 0); lv_obj_center(in);
    if(n > 0){ lv_obj_t *img = lv_image_create(ph); lv_obj_center(img); zthumb_force(1); zthumb_bind(img, ids[0], 96); }   /* covers the letter once loaded */
    wheel_pill_btn(r, CX + 54, 8, LV_SYMBOL_PLAY, "Play", 1, play_all_cb, "library.play_all");
    wheel_pill_btn(r, CX + 54, 56, LV_SYMBOL_SHUFFLE, "Shuffle", 0, shuffle_all_cb, "library.shuffle");
    lv_obj_t *t = lv_label_create(r);
    lv_label_set_long_mode(t, LV_LABEL_LONG_DOT); lv_obj_set_width(t, 220);
    lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(t, album ? album : ""); lv_obj_set_style_text_font(t, ui_font_cjk(18 + 2 * theme_font_size()), 0);
    lv_obj_set_style_text_color(t, TC(TEXT_PRIMARY), 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, CX, 108);
    if(artist && artist[0]){
        lv_obj_t *a = lv_label_create(r);
        lv_label_set_long_mode(a, LV_LABEL_LONG_DOT); lv_obj_set_width(a, 200);
        lv_obj_set_style_text_align(a, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(a, artist); lv_obj_set_style_text_font(a, TF(UI_14), 0);
        lv_obj_set_style_text_color(a, TC(TEXT_MUTED), 0);
        lv_obj_align(a, LV_ALIGN_TOP_MID, CX, 134);
    }
}
static void add_album_cover_header(const char *album, const char *artist){
    lv_obj_t *r = lv_obj_create(g_list);
    lv_obj_remove_style_all(r);
    int has_artist = artist && artist[0];
    int has_title = library_lift() && album && album[0];   /* the album's title under its cover (Disco, Squash, Null) */
    int hdr_h = (has_artist ? 196 : 172) + (has_title ? 26 : 0);
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
        lv_label_set_long_mode(a, LV_LABEL_LONG_SCROLL_CIRCULAR); lv_obj_set_width(a, 240);
        lv_obj_set_style_text_align(a, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(a, artist);
        lv_obj_set_style_text_font(a, ui_text_font(15), 0);
        lv_obj_set_style_text_color(a, TC(TEXT_MUTED), 0);
        lv_obj_align(a, LV_ALIGN_TOP_MID, CX, 166 + (has_title ? 26 : 0));
    }
    if(has_title){
        lv_obj_t *t = lv_label_create(r);
        lv_label_set_long_mode(t, LV_LABEL_LONG_SCROLL_CIRCULAR); lv_obj_set_width(t, 240);
        { const lv_anim_t *disco_scroll5(void); lv_obj_set_style_anim(t, disco_scroll5(), 0); }   /* a few passes, then still */
        lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(t, album);
        lv_obj_set_style_text_font(t, ui_font_cjk(18 + 2 * theme_font_size()), 0);
        lv_obj_set_style_text_color(t, TC(TEXT_PRIMARY), 0);
        lv_obj_align(t, LV_ALIGN_TOP_MID, CX, 164);
    }
}

static void add_plist_cb(lv_event_t *e);
/* Braun 2.0: a square key, its centre at cx in the row - shaded, orange icon, a little glow (no text: the icons say it) */
static void b2_key(lv_obj_t *row, int cx, const char *glyph, lv_event_cb_t cb){
    lv_obj_t *b = lv_button_create(row); lv_obj_remove_style_all(b);
    lv_obj_set_size(b, 52, 52); lv_obj_set_pos(b, cx - 26, 2); lv_obj_set_style_radius(b, 14, 0);
    lv_obj_set_style_bg_color(b, TC(SURFACE_STRONG), 0); lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, TC(SURFACE_PRESSED), LV_STATE_PRESSED);
    lv_obj_t *l = lv_label_create(b); lv_label_set_text(l, glyph); lv_obj_set_style_text_font(l, TF(UI_22), 0); lv_obj_center(l);
    lv_obj_add_flag(b, LV_OBJ_FLAG_USER_4); br2_depth(b, 1); br2_keep_tree(b);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
}
/* the first row of a song list: [ Play All ] [ Shuffle ]. centered=1 screen-centres the pair (used under
 * the album cover card, so it lines up with the centred cover instead of the left-aligned list rows). */
static void add_play_header(int centered){
    lv_obj_t *r = lv_obj_create(g_list);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, centered ? 286 : 268, ROW_H);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    if(library_lift()){   /* the Play / Shuffle pill pair, centred on the screen */
        lv_obj_update_layout(g_list); lv_area_t la; lv_obj_get_coords(r, &la); int cx = 180 - (la.x1 + la.x2) / 2;
        lv_obj_set_height(r, 48);
        wheel_pill_btn(r, cx - 60, 4, LV_SYMBOL_PLAY, "Play", 1, play_all_cb, "library.play_all");
        wheel_pill_btn(r, cx + 60, 4, LV_SYMBOL_SHUFFLE, "Shuffle", 0, shuffle_all_cb, "library.shuffle");
        g_has_header = 1; return;
    }
    int base = centered ? 16 : 0;                 /* r-local 16 => screen-centre (list is at x30/w286) */
    if(th_braun2()){                              /* Braun 2.0: separate square keys on no background, centred on the screen */
        lv_obj_update_layout(g_list); lv_area_t la; lv_obj_get_coords(r, &la);
        lv_obj_add_flag(r, LV_OBJ_FLAG_USER_4);     /* the list's row pass leaves this row bare */
        int n = g_view == VIEW_GROUP ? 3 : 2, mid = 180 - la.x1;
        b2_key(r, mid - (n - 1) * 32, LV_SYMBOL_PLAY, play_all_cb);
        b2_key(r, mid - (n - 1) * 32 + 64, LV_SYMBOL_SHUFFLE, shuffle_all_cb);
        if(n == 3) b2_key(r, mid + 64, LV_SYMBOL_PLUS, add_plist_cb);   /* + Playlist on this row too */
        g_has_header = 1; return;
    }
    hdr_btn(r, base,       tr_sym(LV_SYMBOL_PLAY, "Play All"), play_all_cb);
    hdr_btn(r, base + 140, tr_sym(LV_SYMBOL_SHUFFLE, "Shuffle"), shuffle_all_cb);
    g_has_header = 1;
}

static const char *VIEW_TITLE[] = { "Library","Songs","Albums","Artists","Playlists","Favourites","Genres","","Most Played","Recently Played","History","" };

/* Rows for VIEW_ARTIST_ALBUMS: row 0 "All Songs" (every track by the artist), then each distinct
 * non-empty album among those tracks (case-insensitive, like mdb_albums), alphabetically. Tracks with
 * no album tag appear only under All Songs. Returns the row count (0 = no tracks). */
static int artist_albums(const char *artist){
    if(g_arep_cap < g_grp_cap){ int *n2 = realloc(g_arep, (size_t)g_grp_cap * sizeof *n2); if(n2){ g_arep = n2; g_arep_cap = g_grp_cap; } }
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
        if(g_arep && g_arep_cap >= g_grp_cap){ memmove(g_arep+j+1, g_arep+j, (size_t)(n-j)*sizeof *g_arep); g_arep[j] = g_buf[i]->id; }
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
    if(th_braun2()) return;                      /* Braun 2.0: + Playlist sits beside Play / Shuffle */
    lv_obj_t *r = lv_obj_create(g_list);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, centered ? 286 : 268, ROW_H);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    hdr_btn(r, (centered ? 16 : 0) + 70, tr_sym(LV_SYMBOL_PLUS, "Playlist"), add_plist_cb);   /* centred under the pair above */
    g_has_header = 2;                            /* Play All / Shuffle + this row */
}

/* ==== Zune (Figma, Library page: 29:53 music dial, 29:67 songs, 29:93 album wall, 8:223 album, 29:139 artists,
 * 33:41 artist) ===================================================================================================
 * Text on black, no boxes: a pivot title on top, rows that follow the round glass, the playing song in the accent,
 * a magenta dot with the current letter on the right rim (the A-Z jump), a short arc on the rim for the position. */
static const char *zlow(const char *t){           /* UI words in lower case (the Zune voice); names are left alone */
    static char b[4][96]; static int k; char *o = b[k++ & 3];
    snprintf(o, sizeof b[0], "%s", t ? t : "");
    for(char *c = o; *c; c++) if(*c >= 'A' && *c <= 'Z') *c += 32;
    return o;
}
static lv_obj_t *zlabel(lv_obj_t *p, const char *t, const lv_font_t *f, lv_color_t c, int x, int y, int reserve){
    lv_obj_t *l = lv_label_create(p);
    lv_label_set_text(l, t ? t : ""); lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(l, f, 0); lv_obj_set_style_text_color(l, c, 0);
    lv_obj_set_pos(l, x, y); lv_obj_set_size(l, 200, lv_font_get_line_height(f));
    if(reserve >= 0) lv_obj_set_user_data(l, (void *)(intptr_t)(reserve + 1));   /* the curve fits it: row width - x - reserve */
    lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
    return l;
}
/* title (Light 21; the playing song Regular 19 in the accent), a grey line under it, an optional grey value at the right */
/* Preserve Medium's original list sizes. Small and Large change both lines. */
LV_FONT_DECLARE(font_zune_regular_12) LV_FONT_DECLARE(font_zune_regular_17) LV_FONT_DECLARE(font_zune_regular_16)
static const lv_font_t *zlist_font(int kind){
    int sz=theme_font_size();
    if(kind==1)return zune_font(sz==0?&font_zune_regular_12:sz==2?&font_zune_regular_15:&font_zune_regular_13);
    if(kind==2)return zune_font(sz==0?&font_zune_regular_17:sz==2?&font_zune_light_21:&font_zune_light_18);
    if(kind==3)return zune_font(sz==0?&font_zune_regular_17:sz==2?&font_zune_light_24:&font_zune_regular_19);
    return zune_font(sz==0?&font_zune_light_18:sz==2?&font_zune_light_24:&font_zune_light_21);
}
static void zrow_two(lv_obj_t *r, const char *top, const char *sub, const char *right, int now){
    const lv_font_t *ft = zlist_font(now?3:0), *fs = zlist_font(1);
    int two = sub && sub[0], h1 = lv_font_get_line_height(ft), rw = 0;
    if(right && right[0]){ lv_point_t sz; lv_text_get_size(&sz, right, fs, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE); rw = sz.x + 12; }
    int y1 = two ? 3 : (ROW_H - h1) / 2;
    zlabel(r, top, ft, now ? TC(ACCENT_PRIMARY) : TC(TEXT_PRIMARY), 0, y1, rw);
    if(two) zlabel(r, sub, fs, TC(TEXT_MUTED), 1, y1 + h1 - 4, rw);
    if(rw){ lv_obj_t *l = lv_label_create(r); lv_label_set_text(l, right);
            lv_obj_set_style_text_font(l, fs, 0); lv_obj_set_style_text_color(l, TC(TEXT_MUTED), 0);
            lv_obj_align(l, LV_ALIGN_RIGHT_MID, 0, 0); }
    ui_library_row(r);
}
/* every list row starts on the bezel curve (Figma: x = the circle's edge at the row + 40) and stops short of the
 * other side (and of the letter dot); the labels narrow with it */
static void zcurve_rows(void){
    uint32_t n = lv_obj_get_child_count(g_list);
    int azon = g_az.btn && !lv_obj_has_flag(g_az.btn, LV_OBJ_FLAG_HIDDEN);
    for(uint32_t i = 0; i < n; i++){
        lv_obj_t *r = lv_obj_get_child(g_list, i);
        if(!lv_obj_has_flag(r, LV_OBJ_FLAG_USER_1)) continue;
        lv_area_t a; lv_obj_get_coords(r, &a);
        if(a.y2 < -40 || a.y1 > 400) continue;
        int d = abs((a.y1 + a.y2) / 2 - 180); if(d > 178) d = 178;
        int in = zune_curve_inset(d,0);
        int x = in + 40, right = 360 - in - 24;
        if(azon && right > 296) right = 296;
        int w = right - x; if(w < 90) w = 90;
        intptr_t key = ((intptr_t)w << 16 | (unsigned)x) + 1;               /* (w carries the letter dot's limit too) */
        if((intptr_t)lv_obj_get_user_data(r) == key) continue;
        lv_obj_set_user_data(r, (void *)key);
        lv_obj_set_style_translate_x(r, x, 0); lv_obj_set_width(r, w);
        for(uint32_t k = 0; k < lv_obj_get_child_count(r); k++){
            lv_obj_t *c = lv_obj_get_child(r, k);
            intptr_t ud = (intptr_t)lv_obj_get_user_data(c);
            if(ud <= 0 || !lv_obj_check_type(c, &lv_label_class)) continue;
            int lw = w - lv_obj_get_x(c) - (int)(ud - 1); if(lw < 30) lw = 30;
            if(lv_obj_get_width(c) != lw) lv_obj_set_width(c, lw);
        }
    }
}
static void zscroll_update(void){
    if(!g_zarc || !g_list) return;
    if(g_view == VIEW_MENU || lv_obj_has_flag(g_list, LV_OBJ_FLAG_HIDDEN)){ lv_obj_add_flag(g_zarc, LV_OBJ_FLAG_HIDDEN); return; }
    lv_obj_update_layout(g_list);
    int32_t sy = lv_obj_get_scroll_y(g_list), sb = lv_obj_get_scroll_bottom(g_list), tot = sy + sb;
    if(tot <= 80) lv_obj_add_flag(g_zarc, LV_OBJ_FLAG_HIDDEN);   /* (the 70 px bottom pad alone is not a list to scroll) */
    else {                                                   /* an 18 degree arc travelling down the right rim */
        float f = (float)sy / tot; if(f < 0) f = 0; if(f > 1) f = 1;
        int st = 300 + (int)(f * 102);
        lv_arc_set_bg_angles(g_zarc, st % 360, (st + 18) % 360);
        lv_obj_remove_flag(g_zarc, LV_OBJ_FLAG_HIDDEN);
    }
    zthumb_bump_visible();                                   /* the covers on screen load first */
    if(g_count > 0 && g_first){
        int row = (sy - g_hdr_extra_px) / PITCH; if(row < 0) row = 0;
        int idx = g_view == VIEW_ALBUMS ? row * 3 : row; if(idx >= g_count) idx = g_count - 1;
        azjump_letter(&g_az, g_first[idx]);
        azjump_index(&g_az, g_first, g_count, g_first[idx]);
        azjump_position(&g_az, g_first[idx], 322, 196);       /* the letter wheel setting: the same place */
    }
}
static void zlinks_fit(lv_obj_t *r, const char *const *lb, const lv_event_cb_t *cb, const char *const *act, int n){
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(r, 20, 0);
    for(int k = 0; k < n; k++){
        lv_obj_t *o = lv_obj_create(r); lv_obj_remove_style_all(o); kit_keep(o);
        lv_obj_set_size(o, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE); lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_ext_click_area(o, 8);
        lv_obj_set_style_opa(o, LV_OPA_50, LV_STATE_PRESSED);
        lv_obj_t *l = lv_label_create(o); lv_label_set_text(l, lb[k]);
        int quiet = lb[k][0] == '+';                          /* "+ playlist": the quieter extra */
        lv_obj_set_style_text_font(l, quiet ? zune_font(&font_zune_regular_14) : zune_font(&font_zune_regular_15), 0);
        lv_obj_set_style_text_color(l, quiet ? TC(TEXT_MUTED) : TC(ACCENT_PRIMARY), 0);
        ui_on(o, cb[k], LV_EVENT_CLICKED, NULL, act[k], UI_CORE);
    }
}
/* the links over a song list ("shuffle all"; "play all  shuffle  + playlist"): the first list child, it curves too */
static void zlinks(const char *const *lb, const lv_event_cb_t *cb, const char *const *act, int n){
    lv_obj_t *r = lv_obj_create(g_list); lv_obj_remove_style_all(r); kit_keep(r);
    lv_obj_set_size(r, 300, 32); lv_obj_remove_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(r, LV_OBJ_FLAG_USER_1);
    zlinks_fit(r, lb, cb, act, n);
    g_hdr_extra_px += 32 + 4;
}
static void play_all_cb(lv_event_t *e); static void shuffle_all_cb(lv_event_t *e); static void add_plist_cb(lv_event_t *e);
static void zcover(lv_obj_t *parent, int x, int y, int size, int song_id, const char *name, int initial_only){
    lv_obj_t *ph = lv_obj_create(parent); lv_obj_remove_style_all(ph); kit_keep(ph);
    lv_obj_set_size(ph, size, size); lv_obj_set_pos(ph, x, y);
    lv_obj_set_style_bg_color(ph, TC(SURFACE), 0); lv_obj_set_style_bg_opa(ph, LV_OPA_COVER, 0);
    lv_obj_remove_flag(ph, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    if(name && name[0]){                                      /* no art (yet): the name on a dark tile */
        lv_obj_t *l = lv_label_create(ph);
        if(initial_only){ char in[8] = {0}; unsigned char c0 = (unsigned char)name[0];
            int len = c0 < 0x80 ? 1 : (c0 >> 5) == 6 ? 2 : (c0 >> 4) == 14 ? 3 : (c0 >> 3) == 30 ? 4 : 1;
            for(int k = 0; k < len && name[k]; k++) in[k] = name[k];
            lv_label_set_text(l, zlow(in)); lv_obj_set_style_text_font(l, zune_font(&font_zune_light_50), 0);
            lv_obj_align(l, LV_ALIGN_CENTER, x < 0 ? -x / 2 : 0, 0); }
        else { lv_label_set_text(l, name); lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
            lv_obj_set_width(l, size - 14); lv_obj_set_style_max_height(l, 2 * lv_font_get_line_height((th_zune()?zune_font(&font_zune_regular_13):TF(UI_14))), 0);
            lv_obj_set_style_text_font(l, th_zune()?zune_font(&font_zune_regular_13):TF(UI_14), 0); lv_obj_align(l, LV_ALIGN_BOTTOM_LEFT, 7, -6); }
        lv_obj_set_style_text_color(l, TC(TEXT_MUTED), 0);
    }
    lv_obj_t *img = lv_image_create(ph);
    lv_obj_set_size(img, size, size); lv_obj_center(img);
    if(size > 148) lv_image_set_scale(img, (uint32_t)(256 * size / 148));    /* the 148 px cache, enlarged */
    lv_obj_add_flag(img, LV_OBJ_FLAG_HIDDEN);
    zthumb_bind(img, song_id, size > 148 ? 148 : size);
}
/* album (Figma 8:223): the cover cut by the left rim, the title / artist / count beside it, play and shuffle in the
 * accent; the tracks below, numbered */
static void zalbum_header(const char *album, int n){
    lv_obj_t *h = lv_obj_create(g_list); lv_obj_remove_style_all(h); kit_keep(h);
    lv_obj_set_size(h, 360, 246); lv_obj_remove_flag(h, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    zcover(h, -46, 48, 192, n > 0 ? g_buf[0]->id : 0, album, 1);
    const lv_font_t *ft = zune_font(&font_zune_semibold_22), *fa = zune_font(&font_zune_regular_14), *fm = zune_font(&font_zune_regular_13);
    lv_point_t sz; lv_text_get_size(&sz, album, ft, 0, 0, 158, LV_TEXT_FLAG_NONE);
    int lh = lv_font_get_line_height(ft), th = sz.y > lh ? 2 * lh : lh, y = 62;
    lv_obj_t *t = lv_label_create(h); lv_label_set_text(t, album); lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(t, ft, 0); lv_obj_set_style_text_color(t, TC(TEXT_PRIMARY), 0);
    lv_obj_set_pos(t, 166, y); lv_obj_set_size(t, 158, th); y += th + 2;
    const char *artist = n > 0 ? (g_buf[0]->artist_group[0] ? g_buf[0]->artist_group : g_buf[0]->artist) : "";
    if(artist[0]){ lv_obj_t *a = lv_label_create(h); lv_label_set_text(a, artist); lv_label_set_long_mode(a, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_font(a, fa, 0); lv_obj_set_style_text_color(a, TC(TEXT_PRIMARY), 0);
        lv_obj_set_pos(a, 166, y); lv_obj_set_size(a, 156, lv_font_get_line_height(fa)); y += lv_font_get_line_height(fa); }
    long ms = 0; for(int i = 0; i < n; i++) ms += g_buf[i]->dur_ms > 0 ? g_buf[i]->dur_ms : 0;
    int min = (int)((ms + 30000) / 60000); char meta[64], tm[24];
    if(min >= 60) snprintf(tm, sizeof tm, "%dh %dm", min / 60, min % 60); else snprintf(tm, sizeof tm, "%dm", min);
    snprintf(meta, sizeof meta, "%d song%s \xC2\xB7 %s", n, n == 1 ? "" : "s", tm);
    lv_obj_t *m = lv_label_create(h); lv_label_set_text(m, meta);
    lv_obj_set_style_text_font(m, fm, 0); lv_obj_set_style_text_color(m, TC(TEXT_MUTED), 0); lv_obj_set_pos(m, 166, y);
    y += lv_font_get_line_height(fm) + 12; if(y < 146) y = 146;
    static const char *const LB[] = { "play", "shuffle" }, *const LB2[] = { "+ playlist" };
    static const char *const AC[] = { "library.play_all", "library.shuffle" }, *const AC2[] = { "library.add_plist" };
    static const lv_event_cb_t CB[] = { play_all_cb, shuffle_all_cb }, CB2[] = { add_plist_cb };
    lv_obj_t *lk = lv_obj_create(h); lv_obj_remove_style_all(lk); kit_keep(lk);
    lv_obj_set_size(lk, 170, 28); lv_obj_set_pos(lk, 166, y); lv_obj_remove_flag(lk, LV_OBJ_FLAG_SCROLLABLE);
    zlinks_fit(lk, LB, CB, AC, 2);
    lv_obj_t *lk2 = lv_obj_create(h); lv_obj_remove_style_all(lk2); kit_keep(lk2);
    lv_obj_set_size(lk2, 160, 26); lv_obj_set_pos(lk2, 166, y + 30); lv_obj_remove_flag(lk2, LV_OBJ_FLAG_SCROLLABLE);
    zlinks_fit(lk2, LB2, CB2, AC2, 1);
    g_hdr_extra_px = 246 + 4;
}
static void ztrack_row(lv_obj_t *r, int i){
    const mdb_song_t *so = g_buf[i]; int now = so->id == g_znow_id;
    const lv_font_t *ft = zlist_font(2), *fs = zlist_font(1);
    char num[8], dur[12]; snprintf(num, sizeof num, "%d", so->track > 0 ? so->track : i + 1); fmt_dur(dur, sizeof dur, so->dur_ms);
    zlabel(r, so->title[0] ? so->title : "Untitled", ft, now ? TC(ACCENT_PRIMARY) : TC(TEXT_PRIMARY), 24, (ROW_H - lv_font_get_line_height(ft)) / 2, 50);
    lv_obj_t *nl = lv_label_create(r); lv_label_set_text(nl, num);
    lv_obj_set_style_text_font(nl, fs, 0); lv_obj_set_style_text_color(nl, now ? TC(ACCENT_PRIMARY) : TC(TEXT_MUTED), 0);
    lv_obj_align(nl, LV_ALIGN_LEFT_MID, 0, 1);
    if(dur[0]){ lv_obj_t *dl = lv_label_create(r); lv_label_set_text(dl, dur);
        lv_obj_set_style_text_font(dl, fs, 0); lv_obj_set_style_text_color(dl, TC(TEXT_MUTED), 0); lv_obj_align(dl, LV_ALIGN_RIGHT_MID, 0, 1); }
    ui_library_row(r);
}
static void zalbum_row(lv_obj_t *r, int i){                    /* the album wall (Figma 29:93): three 96 px tiles */
    lv_obj_remove_flag(r, LV_OBJ_FLAG_USER_1 | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_width(r, 360); lv_obj_set_style_bg_opa(r, LV_OPA_TRANSP, LV_STATE_PRESSED);
    for(int c = 0; c < 3; c++){
        int gi = i * 3 + c; if(gi >= g_znalb) break;
        lv_obj_t *t = lv_obj_create(r); lv_obj_remove_style_all(t); kit_keep(t);
        lv_obj_set_size(t, 96, 96); lv_obj_set_pos(t, 30 + c * 102, 0);
        lv_obj_add_flag(t, LV_OBJ_FLAG_CLICKABLE); lv_obj_remove_flag(t, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_opa(t, LV_OPA_70, LV_STATE_PRESSED);
        zcover(t, 0, 0, 96, g_zrep ? g_zrep[gi] : 0, g_gnames[gi], 0);
        ui_on(t, group_cb, LV_EVENT_SHORT_CLICKED, (void *)(uintptr_t)gi, "library.group", UI_CORE);
        ui_on(t, group_play_cb, LV_EVENT_LONG_PRESSED, (void *)(uintptr_t)gi, "library.group_play.long", UI_CORE);
    }
}
static void zartist_row(lv_obj_t *r, int i){                   /* artists (Figma 29:139): an initial in a circle */
    if(g_zac && g_zsc && g_zac[i] < 0){
        g_zsc[i] = mdb_artist_songs(g_gnames[i], g_buf, g_alloc_n);
        g_zac[i] = mdb_scope_albums(MDB_SCOPE_ARTIST, g_gnames[i], NULL, NULL, NULL, 0);
    }
    char sub[64] = "";
    if(g_zac && g_zsc && g_zac[i] >= 0){
        int a = g_zac[i], n = g_zsc[i];
        if(a > 0) snprintf(sub, sizeof sub, "%d album%s \xC2\xB7 %d song%s", a, a == 1 ? "" : "s", n, n == 1 ? "" : "s");
        else snprintf(sub, sizeof sub, "%d song%s", n, n == 1 ? "" : "s");
    }
    const lv_font_t *ft = zlist_font(0);
    int h1 = lv_font_get_line_height(ft);
    zlabel(r, g_gnames[i], ft, TC(TEXT_PRIMARY), 52, sub[0] ? 3 : (ROW_H - h1) / 2, 0);
    if(sub[0]) zlabel(r, sub, zlist_font(1), TC(TEXT_MUTED), 53, 3 + h1 - 4, 0);
    static const uint32_t CIRC[] = { 0x2F8F80, 0x6B5A86, 0x2B3B47, 0x7A2F3B, 0x8A6A2C, 0x355C8A };   /* quiet tile colours */
    unsigned hsh = 5381; for(const char *p = g_gnames[i]; *p; p++) hsh = hsh * 33 + (unsigned char)*p;
    lv_obj_t *c = lv_obj_create(r); lv_obj_remove_style_all(c); kit_keep(c);
    lv_obj_set_size(c, 40, 40); lv_obj_align(c, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_radius(c, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(c, theme_color_from_rgb(CIRC[hsh % 6]), 0); lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    char in[8] = {0}; unsigned char c0 = (unsigned char)g_gnames[i][0];
    int len = c0 < 0x80 ? 1 : (c0 >> 5) == 6 ? 2 : (c0 >> 4) == 14 ? 3 : (c0 >> 3) == 30 ? 4 : 1;
    for(int k = 0; k < len && g_gnames[i][k]; k++) in[k] = g_gnames[i][k];
    lv_obj_t *l = lv_label_create(c); lv_label_set_text(l, in[0] ? zlow(in) : "?");
    lv_obj_set_style_text_font(l, ft, 0); lv_obj_set_style_text_color(l, TC(TEXT_PRIMARY), 0);
    lv_obj_align(l, LV_ALIGN_CENTER, 0, -2);
    ui_library_row(r);
}
static int zrow_special(int i){
    (void)i;
    switch(g_view){
        case VIEW_ALBUMS:return library_wall_active();
        case VIEW_ARTISTS: case VIEW_FAVS: case VIEW_MOSTPLAYED: case VIEW_RECENT: case VIEW_SONGS: case VIEW_GENRES: return 1;
        case VIEW_GROUP: return !(g_drill_kind != 1 && g_scope_lvl == 1);       /* not an artist's / genre's album list */
        default: return 0;
    }
}
static void zhistory_row(lv_obj_t *r, int i);
static void zsel_mark_cb(lv_event_t *e);
static void zadd_row(lv_obj_t *r, int i){
    if(lib_pick_ok()) mp_row(&g_mp, r, i);
    lv_obj_add_event_cb(r, zsel_mark_cb, LV_EVENT_LONG_PRESSED, (void *)(intptr_t)i);   /* before the hold menu opens */
    switch(g_view){
        case VIEW_ALBUMS: zalbum_row(r, i); return;
        case VIEW_ARTISTS:
            ui_on(r, group_cb, LV_EVENT_SHORT_CLICKED, (void*)(uintptr_t)i, "library.group", UI_CORE);
            ui_on(r, group_play_cb, LV_EVENT_LONG_PRESSED, (void*)(uintptr_t)i, "library.group_play.long", UI_CORE);
            zartist_row(r, i); return;
        case VIEW_FAVS:
            ui_on(r, fav_row_cb, LV_EVENT_SHORT_CLICKED, (void*)(uintptr_t)i, "library.fav_row", UI_CORE);
            ui_on(r, fav_row_cb, LV_EVENT_LONG_PRESSED, (void*)(uintptr_t)i, "library.fav_row.long", UI_CORE);
            zrow_two(r, g_favs[i].title, g_favs[i].artist, NULL, g_favs[i].id == g_znow_id);
            for(int k = 0; k < 2; k++){ lv_obj_t *c = lv_obj_get_child(r, k); if(c) lv_obj_set_user_data(c, (void *)(intptr_t)(24 + 1)); }
            { lv_obj_t *hl = lv_label_create(r); lv_label_set_text(hl, TH_IC_HEART);     /* Figma 27: a small magenta heart */
              lv_obj_set_style_text_font(hl, &font_theme_20, 0); lv_obj_set_style_text_color(hl, TC(ACCENT_PRIMARY), 0);
              lv_obj_set_style_transform_pivot_x(hl, LV_PCT(50), 0); lv_obj_set_style_transform_pivot_y(hl, LV_PCT(50), 0);
              lv_obj_set_style_transform_scale(hl, 128, 0); lv_obj_align(hl, LV_ALIGN_RIGHT_MID, -2, 0); }
            return;
        case VIEW_MOSTPLAYED: case VIEW_RECENT:
            ui_on(r, song_cb, LV_EVENT_SHORT_CLICKED, (void*)(uintptr_t)g_favs[i].id,"library.history.song",UI_CORE);
            ui_on(r, song_long_cb, LV_EVENT_LONG_PRESSED, (void*)(uintptr_t)g_favs[i].id,"library.history.song.long",UI_CORE);
            zhistory_row(r, i); return;
        case VIEW_GENRES: {                                      /* Figma 24: the genre, its album count under it */
            ui_on(r, group_cb, LV_EVENT_SHORT_CLICKED, (void*)(uintptr_t)i, "library.group", UI_CORE);
            ui_on(r, group_play_cb, LV_EVENT_LONG_PRESSED, (void*)(uintptr_t)i, "library.group_play.long", UI_CORE);
            if(g_zac && g_zac[i] < 0) g_zac[i] = mdb_scope_albums(MDB_SCOPE_GENRE, g_gnames[i], NULL, NULL, NULL, 0);
            char sub[48]; int a = g_zac ? g_zac[i] : 0;
            if(a > 0) snprintf(sub, sizeof sub, "%d album%s", a, a == 1 ? "" : "s");
            else snprintf(sub, sizeof sub, "%d song%s", g_gcounts[i], g_gcounts[i] == 1 ? "" : "s");
            zrow_two(r, g_gnames[i], sub, NULL, 0); return; }
        default:                                                 /* songs: all of them, or a drill's */
            if(g_view==VIEW_GROUP && g_drill_kind!=1 && g_scope_lvl==2)
                ui_on(r, scope_song_cb, LV_EVENT_SHORT_CLICKED, (void*)(uintptr_t)g_buf[i]->id, "library.scope_song", UI_CORE);
            else
                ui_on(r, song_cb, LV_EVENT_SHORT_CLICKED, (void*)(uintptr_t)g_buf[i]->id, "library.song", UI_CORE);
            ui_on(r, song_long_cb, LV_EVENT_LONG_PRESSED, (void*)(uintptr_t)g_buf[i]->id, "library.song.long", UI_CORE);
            if(lib_zalbum()) ztrack_row(r, i);
            else zrow_two(r, g_buf[i]->title[0] ? g_buf[i]->title : "Untitled", g_buf[i]->artist, NULL, g_buf[i]->id == g_znow_id);
    }
}
/* an artist (Figma 33:41) or a genre (Figma 70): the count, play all / shuffle, its albums as a row of covers,
 * "all songs". names[k] / reps[k] for k in [k0, k1); a tile tap sends tile_cb with k (SHORT_CLICKED), a hold long_cb. */
static void zstrip_page(const char *meta, char (*names)[MDB_STR], const int *reps, int k0, int k1,
                        lv_event_cb_t tile_cb, lv_event_cb_t long_cb, int ud_off, lv_event_cb_t all_cb, intptr_t all_ud){
    lv_obj_t *h = lv_obj_create(g_list); lv_obj_remove_style_all(h); kit_keep(h);
    lv_obj_set_size(h, 360, 236); lv_obj_remove_flag(h, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *m = lv_label_create(h); lv_label_set_text(m, meta);
    lv_obj_set_style_text_font(m, zune_font(&font_zune_regular_14), 0); lv_obj_set_style_text_color(m, TC(TEXT_MUTED), 0);
    lv_obj_set_pos(m, 48, 4);
    static const char *const LB[] = { "play all", "shuffle" }, *const AC[] = { "library.play_all", "library.shuffle" };
    static const lv_event_cb_t CB[] = { play_all_cb, shuffle_all_cb };
    lv_obj_t *lk = lv_obj_create(h); lv_obj_remove_style_all(lk); kit_keep(lk);
    lv_obj_set_size(lk, 260, 28); lv_obj_set_pos(lk, 48, 26); lv_obj_remove_flag(lk, LV_OBJ_FLAG_SCROLLABLE);
    zlinks_fit(lk, LB, CB, AC, 2);
    lv_obj_t *st = lv_obj_create(h); lv_obj_remove_style_all(st); kit_keep(st);   /* the albums, sideways */
    lv_obj_set_size(st, 360, 124); lv_obj_set_pos(st, 0, 64);
    lv_obj_set_flex_flow(st, LV_FLEX_FLOW_ROW); lv_obj_set_style_pad_column(st, 12, 0);
    lv_obj_set_style_pad_left(st, 38, 0); lv_obj_set_style_pad_right(st, 38, 0);
    lv_obj_set_scroll_dir(st, LV_DIR_HOR); lv_obj_set_scrollbar_mode(st, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(st, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    for(int k = k0; k < k1; k++){
        lv_obj_t *t = lv_obj_create(st); lv_obj_remove_style_all(t); kit_keep(t);
        lv_obj_set_size(t, 100, 124); lv_obj_add_flag(t, LV_OBJ_FLAG_CLICKABLE); lv_obj_remove_flag(t, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_opa(t, LV_OPA_70, LV_STATE_PRESSED);
        zcover(t, 0, 0, 100, reps ? reps[k] : 0, NULL, 0);
        lv_obj_t *l = lv_label_create(t); lv_label_set_text(l, names[k]); lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
        lv_obj_set_size(l, 100, lv_font_get_line_height(zune_font(&font_zune_regular_13))); lv_obj_set_pos(l, 0, 105);
        lv_obj_set_style_text_font(l, zune_font(&font_zune_regular_13), 0); lv_obj_set_style_text_color(l, TC(TEXT_PRIMARY), 0);
        ui_on(t, tile_cb, LV_EVENT_SHORT_CLICKED, (void *)(uintptr_t)(k + ud_off), "library.group", UI_CORE);
        ui_on(t, long_cb, LV_EVENT_LONG_PRESSED, (void *)(uintptr_t)(k + ud_off), "library.group_play.long", UI_CORE);
    }
    lv_obj_t *al = lv_obj_create(h); lv_obj_remove_style_all(al); kit_keep(al);
    lv_obj_set_size(al, LV_SIZE_CONTENT, LV_SIZE_CONTENT); lv_obj_set_pos(al, 96, 192);
    lv_obj_add_flag(al, LV_OBJ_FLAG_CLICKABLE); lv_obj_set_ext_click_area(al, 10); lv_obj_set_style_opa(al, LV_OPA_50, LV_STATE_PRESSED);
    lv_obj_t *l = lv_label_create(al); lv_label_set_text(l, "all songs");
    lv_obj_set_style_text_font(l, zune_font(&font_zune_light_24), 0); lv_obj_set_style_text_color(l, TC(TEXT_PRIMARY), 0);
    ui_on(al, all_cb, LV_EVENT_SHORT_CLICKED, (void *)all_ud, "library.scope_all", UI_CORE);
}
static void zartist_page(int n){                        /* g_gnames[0] = All Songs, then the artist's albums */
    char meta[64]; int a = n - 1, s = g_gcounts[0];
    snprintf(meta, sizeof meta, "%d album%s \xC2\xB7 %d song%s", a, a == 1 ? "" : "s", s, s == 1 ? "" : "s");
    int *rep = calloc((size_t)n, sizeof *rep);
    if(rep) for(int k = 1; k < n; k++)
        for(int j = 0; j < g_gcounts[0] && j < g_alloc_n; j++) if(!strcasecmp(g_buf[j]->album, g_gnames[k])){ rep[k] = g_buf[j]->id; break; }
    zstrip_page(meta, g_gnames, rep, 1, n, group_cb, group_play_cb, 0, group_cb, 0);
    free(rep);
}
static void zscope_page(int n){                         /* a genre's (or an artist's) albums: g_gnames[0..n) */
    char meta[64];
    snprintf(meta, sizeof meta, "%d album%s \xC2\xB7 %d song%s", n, n == 1 ? "" : "s", g_scope_total, g_scope_total == 1 ? "" : "s");
    int *rep = calloc((size_t)n, sizeof *rep);
    if(rep) for(int k = 0; k < n; k++){ const mdb_song_t *one[1];
        if(mdb_scope_album_songs(scope_kind(), g_drill, g_gnames[k], one, 1) > 0) rep[k] = one[0]->id; }
    zstrip_page(meta, g_gnames, rep, 0, n, scope_album_cb, scope_album_play_cb, 0, scope_all_cb, 0);
    free(rep);
}
static void zmost_cb(lv_event_t *e){ if(lv_event_get_code(e) == LV_EVENT_CLICKED) menu_go(VIEW_MOSTPLAYED); }
/* playlists (Figma 25): "new playlist" in a ring, then each with a 2 x 2 mosaic and its count + length */
static void zlen(char *b, size_t n, long ms){
    int min = (int)((ms + 30000) / 60000);
    if(min >= 60) snprintf(b, n, "%dh %dm", min / 60, min % 60); else snprintf(b, n, "%dm", min);
}
static void zplaylist_new_row(lv_obj_t *r){
    lv_obj_t *ring = lv_obj_create(r); lv_obj_remove_style_all(ring); kit_keep(ring);
    lv_obj_set_size(ring, 40, 40); lv_obj_align(ring, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(ring, 2, 0); lv_obj_set_style_border_color(ring, TC(ACCENT_PRIMARY), 0);
    lv_obj_remove_flag(ring, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *p = lv_label_create(ring); lv_label_set_text(p, "+");
    lv_obj_set_style_text_font(p, zune_font(&font_zune_light_24), 0); lv_obj_set_style_text_color(p, TC(ACCENT_PRIMARY), 0);
    lv_obj_align(p, LV_ALIGN_CENTER, 0, -2);
    const lv_font_t *ft = zlist_font(0);
    zlabel(r, "new playlist", ft, TC(ACCENT_PRIMARY), 52, (ROW_H - lv_font_get_line_height(ft)) / 2, 0);
}
static void zplaylist_row(lv_obj_t *r, int i){
    int ids[4], ni = 0; long ms = 0;
    int n = zthumb_playlist(g_plids[i], ids, &ni, &ms);
    char sub[64], len[24]; zlen(len, sizeof len, ms);
    if(n > 0) snprintf(sub, sizeof sub, "%d song%s \xC2\xB7 %s", n, n == 1 ? "" : "s", len); else snprintf(sub, sizeof sub, "empty");
    const lv_font_t *ft = zlist_font(0);
    int h1 = lv_font_get_line_height(ft);
    zlabel(r, g_plnames[i], ft, TC(TEXT_PRIMARY), 52, 3, 0);
    zlabel(r, sub, zlist_font(1), TC(TEXT_MUTED), 53, 3 + h1 - 4, 0);
    zthumb_mosaic(r, 0, (ROW_H - 40) / 2, 40, ids, ni);
    ui_library_row(r);
}
/* history (Figma 30): the day over each run of songs, "artist · 7:12 pm" under each */
static int zday(long long t){ time_t tt = (time_t)t; struct tm tm; localtime_r(&tt, &tm); return (tm.tm_year + 1900) * 400 + tm.tm_yday; }
static void zday_label(char *b, size_t n, long long t){
    time_t now = time(NULL), tt = (time_t)t; struct tm tm, nw; localtime_r(&tt, &tm); localtime_r(&now, &nw);
    int d = zday((long long)now) - zday(t);
    static const char *WD[] = { "SUNDAY", "MONDAY", "TUESDAY", "WEDNESDAY", "THURSDAY", "FRIDAY", "SATURDAY" };
    static const char *MO[] = { "JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC" };
    if(t <= 0) snprintf(b, n, "EARLIER");
    else if(d == 0) snprintf(b, n, "TODAY");
    else if(d == 1) snprintf(b, n, "YESTERDAY");
    else if(d > 1 && d < 7) snprintf(b, n, "%s", WD[tm.tm_wday]);
    else snprintf(b, n, "%d %s", tm.tm_mday, MO[tm.tm_mon]);
}
static void zclock(char *b, size_t n, long long t){
    time_t tt = (time_t)t; struct tm tm; localtime_r(&tt, &tm);
    if(simple_cfg_get_int("time_24h", 1)) snprintf(b, n, "%d:%02d", tm.tm_hour, tm.tm_min);
    else snprintf(b, n, "%d:%02d %s", (tm.tm_hour + 11) % 12 + 1, tm.tm_min, tm.tm_hour < 12 ? "am" : "pm");
}
static void zhistory_row(lv_obj_t *r, int i){
    const mdb_song_t *so = &g_favs[i];
    char sub[MDB_STR + 32];
    if(g_view == VIEW_RECENT && so->played_at > 0){ char c[16]; zclock(c, sizeof c, so->played_at);
        snprintf(sub, sizeof sub, "%s%s%s", so->artist, so->artist[0] ? " \xC2\xB7 " : "", c); }
    else snprintf(sub, sizeof sub, "%s", so->artist);
    zrow_two(r, so->title[0] ? so->title : "Untitled", sub, NULL, so->id == g_znow_id);
    if(g_view == VIEW_RECENT && !g_virt && (i == 0 || zday(g_favs[i - 1].played_at) != zday(so->played_at))){
        lv_obj_t *h = lv_obj_create(g_list); lv_obj_remove_style_all(h);              /* the day, over the first song of it */
        lv_obj_set_size(h, 300, 26); lv_obj_add_flag(h, LV_OBJ_FLAG_USER_1); lv_obj_remove_flag(h, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        char d[24]; zday_label(d, sizeof d, so->played_at);
        lv_obj_t *l = zlabel(h, d, &font_zune_semibold_11, TC(TEXT_MUTED), 10, 8, 0);
        lv_obj_set_style_text_letter_space(l, 2, 0);
        lv_obj_move_to_index(h, lv_obj_get_index(r));
    }
}
/* hold an item: its menu offers "select" (picking several; on Zune the sideways swipe belongs to the pivot) */
static int g_zsel_idx = -1;
static void zsel_go(void){ if(g_zsel_idx >= 0 && lib_pick_ok()) mp_start(&g_mp, g_zsel_idx); }
static void zsel_mark_cb(lv_event_t *e){
    if(lv_event_get_code(e) != LV_EVENT_LONG_PRESSED || mp_active(&g_mp)) return;
    g_zsel_idx = (int)(intptr_t)lv_event_get_user_data(e);
    if(lib_pick_ok()) songmenu_offer_select(zsel_go);
}
/* pivots (Windows Phone): a sideways swipe anywhere on the list moves to the next / previous of songs, albums,
 * artists, genres - round, like the titles; the edge swipe stays "back" */
static const int ZPIV[] = { VIEW_SONGS, VIEW_ALBUMS, VIEW_ARTISTS, VIEW_GENRES };
static int zpiv_index(void){ for(int k = 0; k < 4; k++) if(g_view == ZPIV[k]) return k; return -1; }
static int category_index(void){return screen_current()==SCR_ALBUMWALL?1:zpiv_index();}
static void menu_go(int v);
static void zpiv_anim_x(void *o, int32_t v){ lv_obj_set_style_translate_x(o, v, 0); }
static void zpiv_slide(int dir){
    lv_obj_t *o[2] = { g_list, g_category_strip?g_category_strip:g_zpiv };
    for(int k = 0; k < 2; k++){
        if(!o[k])continue;
        lv_anim_t a; lv_anim_init(&a); lv_anim_set_var(&a, o[k]); lv_anim_set_exec_cb(&a, zpiv_anim_x);
        lv_anim_set_values(&a, dir * (k ? 90 : 60), 0); lv_anim_set_duration(&a, k ? 260 : 220);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_out); lv_anim_start(&a);
    }
}
void library_open_pivot(int k);
static void zpiv_go(void *p){
    int dir = (int)(intptr_t)p, k = category_index();
    if(k < 0) return;
    int v = ZPIV[(k + dir + 4) % 4];
    library_open_pivot((k+dir+4)%4);
    if(screen_current() == SCR_LIBRARY && g_view == v) zpiv_slide(dir);
}
/* the pointer's travel is watched from the press on (LVGL's own gesture never reaches a list row here): a long,
 * flat sideways move that isn't scrolling anything turns the pivot, and the touch gives no tap or hold */
static lv_point_t g_zpress;
static lv_timer_t *g_zwatch;
static int zpiv_allowed(void){
    if((screen_current()!=SCR_LIBRARY && screen_current()!=SCR_ALBUMWALL) || category_index() < 0 || mp_active(&g_mp) || azjump_active()) return 0;
    if(g_az.grid && !lv_obj_has_flag(g_az.grid, LV_OBJ_FLAG_HIDDEN)) return 0;
    return g_zpress.y >= 30 && g_zpress.x >= 64;                           /* the status line; the back-swipe strip */
}
static void zwatch_stop(void){ if(g_zwatch){ lv_timer_delete(g_zwatch); g_zwatch = NULL; } }
static void zwatch_cb(lv_timer_t *t){
    lv_indev_t *in = lv_timer_get_user_data(t);
    if(!in || lv_indev_get_state(in) != LV_INDEV_STATE_PRESSED || !zpiv_allowed()){ zwatch_stop(); return; }
    if(lv_indev_get_scroll_obj(in)){ zwatch_stop(); return; }              /* the list is scrolling: not a pivot swipe */
    lv_point_t p; lv_indev_get_point(in, &p);
    int dx = p.x - g_zpress.x, dy = p.y - g_zpress.y;
    if(abs(dx) >= 44 && abs(dy) * 2 < abs(dx)){
        zwatch_stop();
        lv_indev_wait_release(in);                                         /* no tap or hold from this touch */
        lv_async_call(zpiv_go, (void *)(intptr_t)(dx < 0 ? 1 : -1));
    }
}
static void zpiv_indev_cb(lv_event_t *e){
    lv_indev_t *in = lv_indev_active();
    if(!in) return;
    lv_event_code_t c = lv_event_get_code(e);
    if(c == LV_EVENT_PRESSED){
        lv_indev_get_point(in, &g_zpress); zwatch_stop();
        if(zpiv_allowed() && g_zpress.y<100 && g_zpress.x>40 && g_zpress.x<320) g_zwatch = lv_timer_create(zwatch_cb, 10, in);
    } else if(c == LV_EVENT_RELEASED) zwatch_stop();
}
static void zpiv_attach(void){
    static int done;
    if(done) return;
    for(lv_indev_t *i = lv_indev_get_next(NULL); i; i = lv_indev_get_next(i))
        if(lv_indev_get_type(i) == LV_INDEV_TYPE_POINTER){ lv_indev_add_event_cb(i, zpiv_indev_cb, LV_EVENT_ALL, NULL); done = 1; }
}
void library_open_pivot(int k){                 /* the cover flow's "artists": that pivot, in the Library */
    if(k < 0 || k > 3) return;
    focus_clear(); g_deeplink = 0; g_from_artist = 0; g_drill_kind = 0; g_view = ZPIV[k]; g_zmenu_focus = k;
    if(k==1 && simple_cfg_get_int("album_view",0)==2){screen_show(SCR_ALBUMWALL);return;}
    library_reload();screen_show(SCR_LIBRARY);
}
int library_test_pivot(void){return category_index();}
int library_pivot_swipe(int dir){ if(category_index() < 0 || mp_active(&g_mp)) return 0; zpiv_go((void *)(intptr_t)dir); return 1; }

/* the Music dial (Figma 29:53) */
static const int ZCATV[] = { VIEW_SONGS, VIEW_ALBUMS, VIEW_ARTISTS, VIEW_GENRES, VIEW_PLAYLISTS, VIEW_FAVS, CAT_FOLDERS, CAT_BOOKS, VIEW_HISTORY };
static const char *const ZCATS[] = { "songs", "albums", "artists", "genres", "playlists", "favourites", "folders", "books", "history" };
static int g_zmap[9], g_zmap_n;
static void zmenu_go(void *p){ menu_go((int)(intptr_t)p); }
static void zmenu_pick(int i){
    if(i < 0 || i >= g_zmap_n) return;
    g_zmenu_focus = i;
    lv_async_call(zmenu_go, (void *)(intptr_t)ZCATV[g_zmap[i]]);   /* the dial is rebuilt: not from inside its own click */
}
static void zcount(char *b, size_t n, int c, const char *what){
    if(c < 0) b[0] = 0; else snprintf(b, n, "%d %s%s", c, what, c == 1 ? "" : "s");
}
static void zmenu_build(void){
    lv_obj_t *root = lv_obj_get_parent(g_list);
    if(g_zmenu){ lv_obj_delete(g_zmenu); g_zmenu = NULL; }
    g_zmenu = lv_obj_create(root); lv_obj_remove_style_all(g_zmenu); kit_keep(g_zmenu);
    lv_obj_set_size(g_zmenu, 360, 360); lv_obj_remove_flag(g_zmenu, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_to_index(g_zmenu, lv_obj_get_index(g_list) + 1);
    const char *lb[9]; g_zmap_n = 0;
    for(int k = 0; k < 9; k++){ if(!DISKOS_AUDIOBOOKS && ZCATV[k] == CAT_BOOKS) continue; g_zmap[g_zmap_n] = k; lb[g_zmap_n++] = ZCATS[k]; }
    if(g_zmenu_focus >= g_zmap_n) g_zmenu_focus = 0;
    zdial_build(g_zmenu, lb, g_zmap_n, g_zmenu_focus, zmenu_pick, 80, "library.zmenu");
    for(int i = 0; i < g_zmap_n; i++){
        char b[40]; int v = ZCATV[g_zmap[i]], c = -1; const char *w = NULL;
        if(v == VIEW_SONGS){ c = mdb_song_count(); w = "song"; }
        else if(v == VIEW_ALBUMS){ c = mdb_album_count(); w = "album"; }
        else if(v == VIEW_ARTISTS){ c = mdb_artist_count(); w = "artist"; }
        else if(v == VIEW_GENRES && g_gnames && g_gcounts){ c = mdb_genres(g_gnames, g_gcounts, g_grp_cap); w = "genre"; }
        else if(v == VIEW_PLAYLISTS){ c = mdb_playlist_num(); w = "playlist"; }
        else if(v == VIEW_FAVS){ c = mdb_favorite_count(); w = "song"; }
        if(w){ zcount(b, sizeof b, c, w); zdial_set_sub(i, b); }
    }
}
/* Zune geometry for the view about to be built: the list under the pivot (or the whole glass for an album), the
 * pivot title and the next category peeking in at its right (Figma: "songs  alb"), the fades */
static void zlib_layout(void){
    int menu = g_view == VIEW_MENU, full = lib_zalbum();
    if(g_zmenu && !menu){ lv_obj_delete(g_zmenu); g_zmenu = NULL; }
    if(menu) lv_obj_add_flag(g_list, LV_OBJ_FLAG_HIDDEN); else lv_obj_remove_flag(g_list, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(g_list, 0, full ? 0 : 96); lv_obj_set_size(g_list, 360, full ? 360 : 264);
    int pad = full ? 0 : (g_view == VIEW_ALBUMS || g_view == VIEW_PLAYLISTS) ? 8 : (g_view == VIEW_SONGS || g_view == VIEW_FAVS || g_view == VIEW_ARTIST_ALBUMS ||
              (g_view == VIEW_GROUP && !(g_drill_kind != 1 && g_scope_lvl == 1))) ? 8 : 26;
    lv_obj_set_style_pad_top(g_list, pad, 0); lv_obj_set_style_pad_bottom(g_list, 70, 0);
    lv_obj_set_style_pad_left(g_list, 0, 0); lv_obj_set_style_pad_row(g_list, 4, 0);
    lv_obj_set_flex_align(g_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    g_hdr_extra_px = pad;
    if(menu || full) lv_obj_add_flag(g_zpiv, LV_OBJ_FLAG_HIDDEN); else lv_obj_remove_flag(g_zpiv, LV_OBJ_FLAG_HIDDEN);
    if(menu) lv_obj_remove_flag(g_zmusic, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(g_zmusic, LV_OBJ_FLAG_HIDDEN);
    if(menu || full) lv_obj_add_flag(g_zfade[0], LV_OBJ_FLAG_HIDDEN); else lv_obj_remove_flag(g_zfade[0], LV_OBJ_FLAG_HIDDEN);
    if(menu) lv_obj_add_flag(g_zfade[1], LV_OBJ_FLAG_HIDDEN); else lv_obj_remove_flag(g_zfade[1], LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(g_zarc, LV_OBJ_FLAG_HIDDEN);
    track_state_t st; ipc_get_state(&st);
    g_znow_id = st.have_track ? mdb_song_id_by_path(st.path) : 0;
    zthumb_cancel();
    free(g_zac); free(g_zsc); g_zac = g_zsc = NULL;
}
static void zlib_title(const char *t){lv_label_set_text(g_title,t);}
static void zlib_chrome(lv_obj_t *root){
    for(int k = 0; k < 2; k++){                                   /* fades: under the pivot, and at the bottom */
        lv_obj_t *f = lv_obj_create(root); lv_obj_remove_style_all(f); kit_keep(f);
        lv_obj_set_size(f, 360, k ? 76 : 116); lv_obj_align(f, k ? LV_ALIGN_BOTTOM_MID : LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_set_style_bg_color(f, TC(CANVAS), 0); lv_obj_set_style_bg_grad_color(f, TC(CANVAS), 0);
        lv_obj_set_style_bg_grad_dir(f, LV_GRAD_DIR_VER, 0); lv_obj_set_style_bg_opa(f, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_main_opa(f, k ? LV_OPA_TRANSP : LV_OPA_COVER, 0); lv_obj_set_style_bg_grad_opa(f, k ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_main_stop(f, k ? 0 : 210, 0); lv_obj_set_style_bg_grad_stop(f, k ? 230 : 255, 0);
        lv_obj_remove_flag(f, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        g_zfade[k] = f;
    }
    g_zarc = lv_arc_create(root); kit_keep(g_zarc);               /* where the list is: a short arc on the right rim */
    lv_obj_remove_style(g_zarc, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(g_zarc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(g_zarc, 344, 344); lv_obj_center(g_zarc);
    lv_obj_set_style_arc_width(g_zarc, 3, LV_PART_MAIN); lv_obj_set_style_arc_rounded(g_zarc, true, LV_PART_MAIN);
    lv_obj_set_style_arc_color(g_zarc, TC(TEXT_PRIMARY), LV_PART_MAIN); lv_obj_set_style_arc_width(g_zarc, 0, LV_PART_INDICATOR);
    lv_obj_add_flag(g_zarc, LV_OBJ_FLAG_HIDDEN);
    g_zpiv = lv_obj_create(root); lv_obj_remove_style_all(g_zpiv); kit_keep(g_zpiv);
    lv_obj_set_size(g_zpiv, 360, 100); lv_obj_remove_flag(g_zpiv, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    g_title = zune_pivot(g_zpiv, "", back_cb);
    lv_obj_set_style_text_font(g_title,zune_font(&font_zune_light_50),0);lv_obj_set_style_text_letter_space(g_title,-2,0);lv_obj_set_width(g_title,LV_SIZE_CONTENT);lv_obj_set_y(g_title,32);
    g_zmusic = lv_label_create(root); kit_keep(g_zmusic);
    lv_label_set_text(g_zmusic, "MUSIC");
    lv_obj_set_style_text_font(g_zmusic, &font_zune_semibold_11, 0); lv_obj_set_style_text_letter_space(g_zmusic, 2, 0);
    lv_obj_set_style_text_color(g_zmusic, TC(TEXT_MUTED), 0); lv_obj_align(g_zmusic, LV_ALIGN_TOP_MID, 0, 38);
    zune_status(root, SCR_LIBRARY);
    zpiv_attach();
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
    if(g_view!=VIEW_FAVS && g_view!=VIEW_MOSTPLAYED && g_view!=VIEW_RECENT && g_favs){   /* not shown now: let it go */
        free(g_favs); g_favs = NULL; g_fav_cap = 0;
    }
    g_has_header = 0;
    g_hdr_extra_px = 0; g_album_wheel_hdr = 0;
    lv_obj_scroll_to_y(g_list, 0, LV_ANIM_OFF);

    const char *ttl = (g_view==VIEW_GROUP) ? g_drill : (g_view==VIEW_ARTIST_ALBUMS) ? g_artist : VIEW_TITLE[g_view];
    if(th_zune()){ zpiv_attach(); zlib_layout(); zlib_title((g_view==VIEW_GROUP && g_scope_lvl==2 && g_drill_kind!=1) ? g_scope_album
                                             : (g_view==VIEW_GROUP || g_view==VIEW_ARTIST_ALBUMS) ? ttl : g_view==VIEW_RECENT ? "history" : zlow(tr(ttl))); }
    else if(g_title){ lv_label_set_text(g_title, ttl); library_title_refit(g_title, ttl); }   /* the new title fits its line */
    category_refresh();
    az_show(0);

    /* one-time discoverability hints for the invisible long-press actions */
    static int s_group_hold_hint=0, s_fav_hold_hint=0;
    if((g_view==VIEW_ALBUMS || g_view==VIEW_ARTISTS || g_view==VIEW_GENRES) && !s_group_hold_hint){
        s_group_hold_hint=1; ui_toast("Hold an item for more");
    } else if(g_view==VIEW_FAVS && !s_fav_hold_hint){
        s_fav_hold_hint=1; ui_toast("Hold to remove");
    }

    char dur[12];

    if(g_view==VIEW_MENU && th_zune()){ zmenu_build(); return; }
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
            lv_obj_set_pos(ch, 240, (ROW_H - 18) / 2);
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
            row_two(r, th_zune() ? zlow(tr(HCATS[i])) : tr(HCATS[i]), NULL, NULL);
            if(th_zune()) continue;
            lv_obj_t *ch = lv_label_create(r);
            lv_label_set_text(ch, LV_SYMBOL_RIGHT);
            lv_obj_set_pos(ch, 240, (ROW_H - 18) / 2);
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
            if(th_zune()){ g_count = n + 1; zscope_page(n); return; }    /* Figma 70: the covers sideways, "all songs" */
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
        if(th_zune()){
            if(is_album) zalbum_header(g_scope_lvl==2 && g_drill_kind!=1 ? g_scope_album : g_drill, n);
            else if(g_view==VIEW_SONGS){ static const char *const L[] = { "shuffle all" }, *const A[] = { "library.shuffle" };
                static const lv_event_cb_t C[] = { shuffle_all_cb }; zlinks(L, C, A, 1); }
            else { static const char *const L[] = { "play all", "shuffle", "+ playlist" }, *const A[] = { "library.play_all", "library.shuffle", "library.add_plist" };
                static const lv_event_cb_t C[] = { play_all_cb, shuffle_all_cb, add_plist_cb }; zlinks(L, C, A, 3); }
        } else {
        if(is_album && curvelist_wheel()){   /* Wheel: one compact first row - cover with Play and Shuffle beside it */
            add_album_wheel_header(g_scope_lvl==2 && g_drill_kind!=1 ? g_scope_album : g_drill, n>0 ? g_buf[0]->artist : NULL);
        } else {
        if(is_album) add_album_cover_header(g_scope_lvl==2 && g_drill_kind!=1 ? g_scope_album : g_drill, n>0 ? g_buf[0]->artist : NULL);  /* album drill: cover on top */
        add_play_header(is_album);                                       /* centre the pair under the album cover */
        if(g_view==VIEW_GROUP) add_plist_row(is_album);                  /* album / artist / genre: add these songs to a playlist */
        }
        }
        for(int i=0;i<n;i++) g_first[i]=first_letter(g_buf[i]->title);
        g_count=n; fill_start(n);
        if(g_view==VIEW_SONGS) az_show(1);
    } else if(g_view==VIEW_ALBUMS){
        int n=mdb_albums(g_gnames,g_gartists,g_gcounts,g_grp_cap);
        if(n<=0){ empty_scan(tr("No albums found")); return; }
        for(int i=0;i<n;i++) g_first[i]=first_letter(g_gnames[i]);
        if(library_wall_active()){                                                   /* the wall: three covers a row */
            g_znalb = n; free(g_zrep); g_zrep = calloc((size_t)n, sizeof *g_zrep);
            if(g_zrep) mdb_album_rep_ids(g_zrep, n);
            g_count=n; fill_start((n+2)/3); az_show(1);
        } else {
        g_count=n; fill_start(n); az_show(1);
        }
    } else if(g_view==VIEW_ARTIST_ALBUMS){
        lib_ensure_group_cap(512);
        int n=artist_albums(g_artist);
        if(n<=0){ empty_scan("No songs found"); return; }
        if(th_zune()){ g_count=n; zartist_page(n); return; }
        add_play_header(0);                     /* Play All / Shuffle = everything by this artist */
        g_count=n; fill_start(n);
    } else if(g_view==VIEW_ARTISTS){
        lib_ensure_group_cap(mdb_artist_count());   /* size for every distinct artist so none are clipped */
        int n=mdb_artists(g_gnames,g_grp_cap);
        if(n<=0){ empty_scan(tr("No artists found")); return; }
        if(th_zune()){ g_zac = malloc((size_t)n * sizeof *g_zac); g_zsc = malloc((size_t)n * sizeof *g_zsc);
                       if(g_zac && g_zsc) for(int i=0;i<n;i++) g_zac[i] = g_zsc[i] = -1; else { free(g_zac); free(g_zsc); g_zac = g_zsc = NULL; } }
        for(int i=0;i<n;i++) g_first[i]=first_letter(g_gnames[i]);
        g_count=n; fill_start(n); az_show(1);
    } else if(g_view==VIEW_GENRES){
        int n=mdb_genres(g_gnames,g_gcounts,g_grp_cap);
        if(n<=0){ empty_scan(tr("No genres found")); return; }
        if(th_zune()){ g_zac = malloc((size_t)n * sizeof *g_zac); if(g_zac) for(int i=0;i<n;i++) g_zac[i] = -1; }
        for(int i=0;i<n;i++) g_first[i]=first_letter(g_gnames[i]);
        g_count=n; fill_start(n); az_show(1);
    } else if(g_view==VIEW_FAVS){
        /* favourites can outnumber SONG (a favourite whose song left the library stays in MY_LOVE - and in the V2.57
         * queue), so the song-sized buffers must grow to show every one. The rows were cleaned above. */
        lib_ensure_fav_cap(mdb_favorite_count());
        int n=mdb_favorites(g_favs, g_fav_cap < g_grp_cap ? g_fav_cap : g_grp_cap, fw_fav_play_by_love_id());
        if(n<=0){ empty_label(tr("No Favourites yet")); return; }
        if(th_zune()){ static const char *const L[] = { "play", "shuffle" }, *const A[] = { "library.play_all", "library.shuffle" };
                       static const lv_event_cb_t C[] = { play_all_cb, shuffle_all_cb }; zlinks(L, C, A, 2);
                       lv_obj_t *hr = lv_obj_get_child(g_list, -1), *c = lv_label_create(hr);   /* Figma 27: "38 songs" after them */
                       char b[24]; snprintf(b, sizeof b, "%d song%s", n, n == 1 ? "" : "s"); lv_label_set_text(c, b);
                       lv_obj_set_style_text_font(c, zune_font(&font_zune_regular_13), 0); lv_obj_set_style_text_color(c, TC(TEXT_MUTED), 0); }
        else add_play_header(0);
        for(int i=0;i<n;i++) g_first[i]=first_letter(g_favs[i].title);
        g_count=n; fill_start(n); az_show(1);
    } else if(g_view==VIEW_MOSTPLAYED || g_view==VIEW_RECENT){
        /* sized to the history, not the whole library (848 bytes a song): a short history stays small */
        int want=mdb_history_count(); if(want>g_alloc_n) want=g_alloc_n;
        if(want<=0){ empty_label(tr("Nothing played yet")); return; }
        lib_ensure_fav_cap(want);
        int cap=g_fav_cap<want?g_fav_cap:want;
        int n = (g_view==VIEW_MOSTPLAYED) ? mdb_mostplayed(g_favs, cap) : mdb_recent(g_favs, cap);
        if(n<=0){ empty_label(tr("Nothing played yet")); return; }
        if(th_zune() && g_view==VIEW_RECENT){ static const char *const L[] = { "most played" }, *const A[] = { "library.mostplayed" };
                                              static const lv_event_cb_t C[] = { zmost_cb }; zlinks(L, C, A, 1); }
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
        if(th_zune()) zplaylist_new_row(nr); else
        row_two(nr, tr_sym(LV_SYMBOL_PLUS, "New Playlist"), NULL, NULL);
        theme_list_row(nr);
        for(int i=0;i<n;i++){
            lv_obj_t *r=base_row();
            lv_obj_add_event_cb(r, playlist_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
            if(th_zune()){ zplaylist_row(r, i); continue; }
            int c = mdb_playlist_count(g_plids[i]); if(c < 0) c = 0;
            char sub[24]; snprintf(sub, sizeof sub, "%d song%s", c, c == 1 ? "" : "s");
            row_two(r, g_plnames[i], sub, NULL);                         /* the name, and how many songs under it */
        }
    }
    if(th_braun_family() || th_zune() || curvelist_wheel()){ lv_obj_update_layout(g_list); curve_rows(); }   /* Braun: every view's rows get the Braun styling; Zune: the curve */
}

/* Twelve session-local views, no flash writes on scrolling. Restore after rebuilding
 * and after the virtual spacers exist; explicit track focus takes priority. */
static void library_reload(void){
    if(!g_list)return;
    if(mp_active(&g_mp)) sel_exit();               /* a new or refreshed list: picking ends */
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
    } else if((g_album_wheel_hdr || (g_view==VIEW_ARTIST_ALBUMS && g_has_header)) && !g_np_jump && curvelist_wheel()){
        /* Wheel album (and an artist's albums, whose first row is Play / Shuffle), first open: that row would wait in
         * the middle with the rest below the screen. Raise it to just under the screen header so the first songs show beneath it; the first scroll
         * snaps back to the usual wheel. Not a snap point: scroll_to never snaps, only a drag does. */
        lv_obj_update_layout(g_list);
        int y = lv_obj_get_style_pad_top(g_list, 0) - AWH_RAISE;
        if(y > 0){ lv_obj_scroll_to_y(g_list, y, LV_ANIM_OFF); lv_obj_update_layout(g_list); curve_rows(); pdots_update(); }
    }
    onboarding_library(g_count>0 && (g_view==VIEW_SONGS || g_view==VIEW_ALBUMS || g_view==VIEW_GROUP || g_view==VIEW_ARTIST_ALBUMS));
    g_restoring=0;
    if(g_position)lv_obj_add_flag(g_position,LV_OBJ_FLAG_HIDDEN);
    pos_header(0);
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
/* theme_kit.c, after a styling pass over `root`: if that was the library's screen in Wheel mode, lay the wheel out again
 * (the pass gives new rows the theme's row look - Squash: pills - which the wheel takes off). Rows already placed keep
 * their key and return at once. */
void library_kit_done(lv_obj_t *root){
    if(!g_list || !root || !curvelist_wheel() || library_wall_active()) return;
    lv_obj_t *p = g_list; while(p && p != root) p = lv_obj_get_parent(p);   /* the pass covered the list */
    if(p) curve_rows();
}

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
void library_open_overview(void){focus_clear();g_deeplink=g_from_artist=g_drill_kind=0;g_view=VIEW_MENU;library_reload();screen_show(SCR_LIBRARY);}
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
        if(elapsed>850){lv_obj_add_flag(g_position,LV_OBJ_FLAG_HIDDEN);pos_header(0);}
    }
    if(g_lhint && !lv_obj_has_flag(g_lhint, LV_OBJ_FLAG_HIDDEN) && lv_tick_elaps(g_lhint_tick) > 650)
        lv_obj_add_flag(g_lhint, LV_OBJ_FLAG_HIDDEN);
}
/* called from the rim-scroll handler while the Library list is the one scrolling */
void library_open_letter_picker(void){ if(g_az.btn && g_count > 0) azjump_open(&g_az); }   /* a hold on the rim scrollbar */
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
    mdb_song_t *favs = NULL; /* History/favourites allocate only when opened. */
    char *first = malloc((size_t)gc);
    char (*gnames)[MDB_STR] = malloc((size_t)gc * sizeof *gnames);
    char (*gartists)[MDB_STR] = malloc((size_t)gc * sizeof *gartists);
    int *gcounts = malloc((size_t)gc * sizeof *gcounts);
    if(!buf || !first || !gnames || !gartists || !gcounts){
        fprintf(stderr, "library: buffer alloc failed for %d songs (keeping %d)\n", n, g_alloc_n);
        free(buf); free(favs); free(first); free(gnames); free(gartists); free(gcounts);
        return;
    }
    free(g_buf); free(g_favs); free(g_first); free(g_gnames); free(g_gartists); free(g_gcounts);
    g_buf = buf; g_favs = favs; g_first = first; g_gnames = gnames; g_gartists = gartists; g_gcounts = gcounts;
    g_alloc_n = n; g_fav_cap = 0; g_grp_cap = gc;
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

    if(!th_zune()){ uint32_t n0 = lv_obj_get_child_count(root);
        g_title = ui_header_cb(root, "Library", back_cb);   /* shared header; back_cb pops the internal view stack */
        library_header_lift(root, n0, g_title); }

    g_list = lv_obj_create(root);
    lv_obj_remove_style_all(g_list);
    lv_obj_set_pos(g_list, 22, LIST_Y); lv_obj_set_size(g_list, 316, LIST_H);   /* centred; wide enough for rows that lean to the free edge */
    lv_obj_set_style_bg_opa(g_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_bottom(g_list, 44, 0);   /* round bottom bezel */
    lv_obj_set_style_pad_row(g_list, 4, 0);
    lv_obj_set_flex_flow(g_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(g_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);   /* narrowed rows stay centred */
    lv_obj_add_event_cb(g_list, curve_scroll_cb, LV_EVENT_SCROLL, NULL);
    curvelist_edge_fade(g_list);                 /* same soft edges as the other lists, in the empty space only */
    lv_obj_add_event_cb(g_list, focus_scroll_begin, LV_EVENT_SCROLL_BEGIN, NULL);
    ui_library_list_watch(g_list);
    pdots_create(root);                          /* after the list, so they draw on top of it */
    if(th_zune()) zlib_chrome(root);             /* pivot title, fades, rim arc, status (Figma, Library page) */
    lv_obj_set_scroll_dir(g_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(g_list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(g_list, LV_OBJ_FLAG_SCROLL_MOMENTUM);

    /* the "A - Z" pill and its letter grid (azjump.c, shared with Folders) */
    azjump_create(&g_az, root, jump_to_letter, "library");
    g_mp = (mp_host_t){ .root = root, .list = g_list, .title = g_title, .count = lib_pick_count, .can_pick = lib_pick_ok,
                        .collect = lib_pick_collect, .az = lib_pick_az };
    mp_attach(&g_mp);                             /* Play / Queue / More while picking (hidden otherwise) */

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
    lv_obj_set_size(g_position,108,24);lv_obj_align(g_position,LV_ALIGN_TOP_MID,0,library_lift()?(th_null()?24:18):th_null()?70:42);
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
    if(th_braun_family() && g_list){                                        /* Braun: the grille, the list on the lower segment */
        br_face(root); lv_obj_update_layout(g_list);
        int y = lv_obj_get_y(g_list) - 6; if(y < 40) y = 40;
        if(!th_braun2())lv_obj_move_to_index(br_segment(root, y), 1);
        curvelist_braun_watch(g_list);
    }
}
