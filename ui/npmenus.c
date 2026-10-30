#include "simple_theme.h"
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "screens.h"
#include "i18n.h"
#include "theme_kit.h"
#include "braun.h"
#include "orbit.h"
#include "curvelist.h"
#include "theme.h"
#include <stdlib.h>
#include <string.h>
#include "anim.h"
#include "config.h"
#include "musicdb.h"
#include "ipc.h"
#include <stdint.h>
#include "zune_dial.h"
#include "zune_thumbs.h"
int ui_sleep_state(int *secs_left);
#include "lvgl/src/misc/lv_event_private.h"   /* Zune: run a context action as if its button was clicked */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Two Now Playing side panels:
 *  - SCR_NPMENU  (right "3-dot" icon): context - Song Info / Go to Album / Artist
 *  - SCR_TUNE    (left "tuning" icon): quick playback - Play Mode / Equalizer    */

#define ACC UI_RED


/* ---- shared current-track context (set from main.c) --------------------- */
static char g_album[160];
static char g_artist[160];
static char g_path[256];
static int  g_fav, g_have;
static lv_obj_t *g_fav_lbl;

static orbit_t g_orb;                              /* the menu: seven actions orbiting the cover */
static lv_obj_t *g_cover_img, *g_menu_bg, *g_menu_title;
static void fav_refresh(void){
    if(g_fav_lbl) lv_label_set_text(g_fav_lbl, g_fav ? "Remove from Favourites" : "Add to Favourites");
    if(g_orb.n) orbit_set_on(&g_orb, 0, g_fav, ui_current_accent());   /* a favourite fills the heart button */
}
void npmenu_set(const track_state_t *st, int playing, const void *thumb_src){
    (void)playing; (void)thumb_src;
    if(g_orb.ring && st && st->have_track && st->duration_ms > 0){      /* the cover's ring: the track's progress */
        int v = (int)((long long)st->position_ms * 1000 / st->duration_ms);
        if(abs(v - lv_arc_get_value(g_orb.ring)) >= 3) orbit_hub_ring(&g_orb, ORBIT_RING_PROGRESS, ui_media_accent(), v);
    }
    if(g_menu_title && st){ char t[200]; snprintf(t, sizeof t, "%.90s%s%.90s", st->title, st->artist[0] ? " \xC2\xB7 " : "", st->artist);
        if(strcmp(lv_label_get_text(g_menu_title), t)) lv_label_set_text(g_menu_title, t); }
    g_have = st && st->have_track;
    snprintf(g_album,  sizeof g_album,  "%s", g_have ? st->album  : "");
    snprintf(g_artist, sizeof g_artist, "%s", g_have ? st->artist : "");
    snprintf(g_path,   sizeof g_path,   "%s", g_have ? st->path   : "");
    g_fav = g_have ? st->is_favorite : 0;
    fav_refresh();
}

/* first artist token (so "A, B" -> "A" matches the split Artists browse) */
static void first_artist(const char *raw, char *out, int n){
    int i=0; while(raw[i] && raw[i]!=',' && raw[i]!=';' && i<n-1){ out[i]=raw[i]; i++; }
    out[i]=0;
    while(i>0 && out[i-1]==' ') out[--i]=0;
}

/* ---- generic list row --------------------------------------------------- */
__attribute__((unused)) static lv_obj_t *menu_row(lv_obj_t *list, const char *text, lv_event_cb_t cb, void *ud){
    lv_obj_t *r = lv_button_create(list);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, 268, 52);
    lv_obj_set_style_radius(r, 12, 0);
    lv_obj_set_style_bg_color(r, TC(SURFACE), 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_70, 0);
    lv_obj_set_style_bg_color(r, TC(SURFACE_RAISED), LV_STATE_PRESSED);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(r, cb, LV_EVENT_CLICKED, ud);
    lv_obj_t *l = lv_label_create(r);
    lv_label_set_text(l, text);
    lv_obj_set_pos(l, 16, 16);
    lv_obj_set_style_text_font(l, ui_font_cjk(16), 0);   /* rows carry playlist names: chain (issue #3) */
    lv_obj_set_style_text_color(l, TC(TEXT_PRIMARY), 0);
    return r;
}

static lv_obj_t *panel_header(lv_obj_t *root, const char *title){
    lv_obj_set_style_bg_color(root, TC(CANVAS), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    return ui_header(root, title);   /* shared standard header; returns the title label */
}

static lv_obj_t *panel_list(lv_obj_t *root){
    lv_obj_t *list = lv_obj_create(root);
    lv_obj_remove_style_all(list);
    lv_obj_set_pos(list, 46, 76); lv_obj_set_size(list, 280, 250);
    lv_obj_set_style_pad_bottom(list, 44, 0);   /* last row scrolls clear of the round bottom bezel */
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_row(list, 10, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);
    return list;
}

/* ---- context menu (SCR_NPMENU) ------------------------------------------ */
__attribute__((unused)) static void ctx_tag_cb(lv_event_t *e){ if(lv_event_get_code(e)==LV_EVENT_CLICKED){ screen_back(); tagfix_current_track(); } }
__attribute__((unused)) static void ctx_tagalb_cb(lv_event_t *e){ if(lv_event_get_code(e)==LV_EVENT_CLICKED){ screen_back(); tagfix_current_album(); } }
static void ctx_info_cb(lv_event_t *e){
    if(lv_event_get_code(e) == LV_EVENT_CLICKED) songinfo_unpin();   /* the playing track's details */ if(lv_event_get_code(e)==LV_EVENT_CLICKED){ screen_show(SCR_SONGINFO); } }
static void ctx_queue_add_cb(lv_event_t *e){   /* +Queue - the playing song to the end of the queue */
    if(lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    int r = g_path[0] ? queue_add_path(g_path) : 0;
    ui_toast_icon(LV_SYMBOL_LIST, ui_current_accent(), r > 0 ? "Added to the queue" : r < 0 ? "The queue is full" : "Can't queue that");
}
static void ctx_queue_open_cb(lv_event_t *e){ if(lv_event_get_code(e) == LV_EVENT_CLICKED) queue_open(); }   /* View queue */
static void ctx_lyrics_cb(lv_event_t *e){ if(lv_event_get_code(e)==LV_EVENT_CLICKED){ lyrics_open(); } }
static void ctx_album_cb(lv_event_t *e){
    if(lv_event_get_code(e)!=LV_EVENT_CLICKED) return;
    /* drill the canonical DB album for this track (its path), not the player's
     * metadata album string, which may not match the DB and show an empty list */
    char dbal[160], dbar[160];
    const char *album = g_album;
    if(mdb_song_meta_by_path(g_path, dbal, sizeof dbal, dbar, sizeof dbar) && dbal[0]) album = dbal;
    if(!album[0]){ ui_toast("No album info"); return; }
    library_open_album(album); screen_show(SCR_LIBRARY);
}
static void ctx_artist_cb(lv_event_t *e){
    if(lv_event_get_code(e)!=LV_EVENT_CLICKED) return;
    char dbal[160], dbar[160];
    const char *src = g_artist;
    if(mdb_song_meta_by_path(g_path, dbal, sizeof dbal, dbar, sizeof dbar) && dbar[0]) src = dbar;
    if(!src[0]){ ui_toast("No artist info"); return; }
    char a[160]; first_artist(src, a, sizeof a);
    library_open_artist(a); screen_show(SCR_LIBRARY);
}
static void plpick_reload(void);
static void ctx_addpl_cb(lv_event_t *e){
    if(lv_event_get_code(e)!=LV_EVENT_CLICKED) return;
    if(!g_path[0]){ ui_toast("No song to add"); return; }   /* nothing playing/loaded */
    plpick_set_song(g_path, 0, NULL);          /* add the current track */
    plpick_reload();                  /* refresh list + clear any old toast */
    screen_show(SCR_PLPICK);
}
static void ctx_fav_cb(lv_event_t *e){
    if(lv_event_get_code(e)!=LV_EVENT_CLICKED) return;
    g_fav = !g_fav;                   /* optimistic; player confirms on next a2 */
    ui_set_favorite(g_fav);
    fav_refresh();
    ui_toast(g_fav ? "Added to Favourites" : "Removed from Favourites");   /* match Now Playing */
}
/* the cover in the hub and the album wash behind */
static void npmenu_refresh_art_now(void){
    const void *dsc = ui_current_cover_dsc();
    if(g_cover_img){
        lv_image_set_src(g_cover_img, NULL);
        if(dsc){ const lv_image_dsc_t *d = dsc; lv_image_set_src(g_cover_img, dsc);
                 if(d->header.w > 0) lv_image_set_scale(g_cover_img, (uint32_t)(ORBIT_HUB * 256 / d->header.w) + 2);
                 lv_obj_center(g_cover_img); lv_obj_remove_flag(g_cover_img, LV_OBJ_FLAG_HIDDEN); }
        else lv_obj_add_flag(g_cover_img, LV_OBJ_FLAG_HIDDEN);
    }
    if(g_menu_bg){ const void *bd = ui_current_backdrop_img(); lv_image_set_src(g_menu_bg, NULL);
        if(bd){ lv_image_set_src(g_menu_bg, bd); if(!th_braun_family()) lv_obj_remove_flag(g_menu_bg, LV_OBJ_FLAG_HIDDEN); } else lv_obj_add_flag(g_menu_bg, LV_OBJ_FLAG_HIDDEN); }
    if(g_orb.hub) lv_obj_set_style_bg_color(g_orb.hub, ui_media_accent(), 0);
}
/* ---- the options orbit: one design for the swipe menu (SCR_NPHUB) and the "..." menu (SCR_NPMENU) ----
 * Built fresh each time either is shown, for the current track: music gets eight actions round the cover,
 * an audiobook its own four. The hub is the cover inside its progress ring; tapping it closes the menu. */
static void hub_eq_open_cb(lv_event_t *e);
static void hub_chapters_cb(lv_event_t *e);
static void hub_fsart_cb(lv_event_t *e);
static lv_obj_t *g_tags_ov;
static void tags_close(void){ if(g_tags_ov){ lv_obj_delete(g_tags_ov); g_tags_ov = NULL; } }
static void tags_pick_cb(lv_event_t *e){
    int which = (int)(intptr_t)lv_event_get_user_data(e);
    tags_close();
    if(which == 1){ screen_back(); tagfix_current_track(); }
    else if(which == 2){ screen_back(); tagfix_current_album(); }
}
static lv_obj_t *tags_btn(lv_obj_t *p, const char *t, int y, uint32_t bg, int which){
    lv_obj_t *b = lv_button_create(p);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, 180, 44);
    lv_obj_align(b, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_set_style_radius(b, TH_R_PILL, 0);
    lv_obj_set_style_bg_color(b, theme_color_from_rgb(bg), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, TC(SURFACE_RAISED), LV_STATE_PRESSED);
    lv_obj_add_event_cb(b, tags_pick_cb, LV_EVENT_CLICKED, (void *)(intptr_t)which);
    lv_obj_t *l = lv_label_create(b); lv_label_set_text(l, t);
    lv_obj_set_style_text_font(l, TH_F_DETAIL, 0); lv_obj_set_style_text_color(l, TC(TEXT_PRIMARY), 0); lv_obj_center(l);
    return b;
}
static void tags_cb(lv_event_t *e){                           /* Tags: this track, or the whole album */
    if(lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    tags_close();
    g_tags_ov = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(g_tags_ov);
    lv_obj_set_size(g_tags_ov, 360, 360);
    lv_obj_set_style_bg_color(g_tags_ov, TC(CANVAS), 0);
    lv_obj_set_style_bg_opa(g_tags_ov, 240, 0);
    lv_obj_add_flag(g_tags_ov, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *h = lv_label_create(g_tags_ov); lv_label_set_text(h, "Add lyrics & artwork");
    lv_obj_set_style_text_font(h, TH_F_LIST, 0); lv_obj_set_style_text_color(h, TC(TEXT_PRIMARY), 0);
    lv_obj_align(h, LV_ALIGN_TOP_MID, 0, 92);
    lv_obj_t *n = lv_label_create(g_tags_ov); lv_label_set_text(n, "only what's missing");
    lv_obj_set_style_text_font(n, TH_F_CAPTION, 0); lv_obj_set_style_text_color(n, TC(TEXT_DISABLED), 0);
    lv_obj_align(n, LV_ALIGN_TOP_MID, 0, 118);
    tags_btn(g_tags_ov, "This track", 146, TH_SURF1, 1);
    tags_btn(g_tags_ov, "Whole album", 198, TH_SURF1, 2);
    tags_btn(g_tags_ov, "Cancel", 262, TH_BG, 0);
}
static void close_cb(lv_event_t *e){ (void)e; tags_close(); screen_back(); }
static lv_obj_t *g_box_menu, *g_box_hub;
static void build_menu(lv_obj_t *box);
void npmenu_refresh_art(void){ build_menu(g_box_menu); }   /* SCR_NPMENU shown */
/* ---- Zune: the actions menu and the play-mode list, as dials (Figma: 11 "Now playing - actions", 16 "Play mode") ----
 * Tapping the title on Now Playing (or swiping to the hub) opens the actions; "play mode" swaps the same screen to the
 * mode list, and picking a mode returns to Now Playing. */
static lv_obj_t *g_zbox;
static void zune_actions(lv_obj_t *box, const track_state_t *st);
static void zune_header(lv_obj_t *box, const char *text, int with_art){
    lv_obj_t *row = lv_obj_create(box); lv_obj_remove_style_all(row); kit_keep(row);
    lv_obj_set_size(row, 200, 18); lv_obj_align(row, LV_ALIGN_TOP_MID, 0, 22);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 8, 0); lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    const lv_image_dsc_t *d = with_art ? (const lv_image_dsc_t *)ui_current_cover_dsc() : NULL;
    if(d && d->header.w > 0){
        lv_obj_t *box18 = lv_obj_create(row); lv_obj_remove_style_all(box18); kit_keep(box18);
        lv_obj_set_size(box18, 18, 18); lv_obj_set_style_clip_corner(box18, true, 0);
        lv_obj_remove_flag(box18, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *im = lv_image_create(box18); lv_image_set_src(im, d);
        lv_image_set_scale(im, (uint32_t)(18 * 256 / d->header.w) + 1); lv_obj_center(im);
    }
    lv_obj_t *l = lv_label_create(row); kit_keep(l);
    lv_label_set_text(l, text); lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_point_t tz; lv_text_get_size(&tz, text, TF(UI_12), 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    lv_obj_set_size(l, tz.x < 170 ? tz.x + 1 : 170, lv_font_get_line_height(TF(UI_12)));
    lv_obj_set_style_text_font(l, TF(UI_12), 0); lv_obj_set_style_text_color(l, TC(TEXT_MUTED), 0);
}
static void zune_run(lv_event_cb_t cb){ lv_event_t ev; memset(&ev, 0, sizeof ev); ev.code = LV_EVENT_CLICKED; cb(&ev); }
/* play mode: Figma order, then Single (a stock mode the design left out) */
static const int ZPM_MODE[5] = { 0, 3, 2, 1, 4 };
static const char *const ZPM_LABEL[5] = { "in order", "repeat all", "repeat one", "shuffle", "single" };
static void zune_pm_pick(int i){
    if(i < 0 || i >= 5) return;
    ui_np_set_playmode(ZPM_MODE[i]);
    screen_back();
}
int ui_set_folder_jump(int on);                              /* main.c: 0687; 0 = sent */
static void zune_fj_cb(lv_event_t *e){
    lv_obj_t *sw = lv_event_get_target(e);
    int v = lv_obj_has_state(sw, LV_STATE_CHECKED) ? 1 : 0;
    if(ui_set_folder_jump(v) == 0) simple_cfg_set_int("folder_jump", v);
    else { if(v) lv_obj_remove_state(sw, LV_STATE_CHECKED); else lv_obj_add_state(sw, LV_STATE_CHECKED); ui_toast("Couldn't change it - try again"); }
}
static void zune_playmode(lv_obj_t *box){
    lv_obj_clean(box);
    int cur = simple_cfg_get_int("work_mode", 0), focus = 0;
    for(int i = 0; i < 5; i++) if(ZPM_MODE[i] == cur) focus = i;
    zdial_build(box, ZPM_LABEL, 5, focus, zune_pm_pick, 56, "npmenu.zune.mode");
    /* the stock play-mode glyphs (Figma 16): arrow = in order, loop = repeat all, loop + 1 = repeat one, shuffle; single
     * = arrow + 1 */
    zdial_set_icon(0, "\xEF\x85\xB8", NULL);  zdial_set_icon(1, LV_SYMBOL_LOOP, NULL); zdial_set_icon(2, LV_SYMBOL_LOOP, "1");
    zdial_set_icon(3, LV_SYMBOL_SHUFFLE, NULL); zdial_set_icon(4, "\xEF\x85\xB8", "1");
    zdial_set_color(focus, TC(ACCENT_PRIMARY));         /* the mode that's on */
    zdial_set_bottom(268);
    /* play through folders (the player's FOLDER_JUMP), as on Settings > Playback */
    lv_obj_t *fl = lv_label_create(box); kit_keep(fl);
    lv_label_set_text(fl, "play through folders");
    lv_obj_set_style_text_font(fl, TF(UI_14), 0); lv_obj_set_style_text_color(fl, TC(TEXT_PRIMARY), 0);
    lv_obj_align(fl, LV_ALIGN_TOP_MID, -26, 298);
    lv_obj_t *sw = lv_switch_create(box);
    lv_obj_set_size(sw, 40, 22); lv_obj_align(sw, LV_ALIGN_TOP_MID, 88, 295);
    if(simple_cfg_get_int("folder_jump", 0)) lv_obj_add_state(sw, LV_STATE_CHECKED);
    ui_on(sw, zune_fj_cb, LV_EVENT_VALUE_CHANGED, NULL, "npmenu.zune.folders", UI_REACHABLE);
    lv_obj_t *h = lv_label_create(box); kit_keep(h);
    lv_label_set_text(h, "play mode");
    lv_obj_set_style_text_letter_space(h, 1, 0);
    lv_obj_set_style_text_font(h, TF(UI_10), 0); lv_obj_set_style_text_color(h, TC(TEXT_MUTED), 0);
    lv_obj_align(h, LV_ALIGN_TOP_MID, 0, 26);
}
/* ---- Zune sleep timer (Figma 17 + 66): a dial of minutes - drag round it to set 5 to 120, ticks every 15, 0 at the
 * top - with the minutes huge in the middle, when it stops, what happens then (pause / power off) and "start". While
 * it runs the arc shrinks with the time left, with "+15 min" and "cancel timer". ---- */
LV_FONT_DECLARE(font_zune_light_96)
#include "power.h"
#include <math.h>
#include <time.h>
static lv_obj_t *g_zs_arc, *g_zs_big, *g_zs_sub, *g_zs_then[3], *g_zs_under, *g_zs_go, *g_zs_plus, *g_zs_cancel;
static lv_timer_t *g_zs_tm;
static int g_zs_min = 45, g_zs_drag;
static void zs_stops(char *b, size_t n, int secs, const char *lead){
    time_t t = time(NULL) + secs; struct tm tm; localtime_r(&t, &tm);
    if(simple_cfg_get_int("time_24h", 1)) snprintf(b, n, "%s \xC2\xB7 stops at %d:%02d", lead, tm.tm_hour, tm.tm_min);
    else snprintf(b, n, "%s \xC2\xB7 stops at %d:%02d", lead, tm.tm_hour % 12 ? tm.tm_hour % 12 : 12, tm.tm_min);
}
static void zs_then_paint(void){
    int act = simple_cfg_get_int("sleep_action", POWER_SLEEP_PAUSE);
    lv_obj_t *sel = act == POWER_SLEEP_SHUTDOWN ? g_zs_then[2] : g_zs_then[1];
    lv_obj_update_layout(sel);
    lv_area_t a; lv_obj_get_coords(sel, &a); lv_area_t pa; lv_obj_get_coords(lv_obj_get_parent(g_zs_under), &pa);
    lv_obj_set_width(g_zs_under, lv_area_get_width(&a));
    lv_obj_set_pos(g_zs_under, a.x1 - pa.x1, a.y2 - pa.y1 + 3);
}
static void zs_paint(void){
    int secs = 0, run = ui_sleep_state(&secs) == 1;
    char b[64];
    for(int k = 0; k < 3; k++) if(g_zs_then[k]){ if(run) lv_obj_add_flag(g_zs_then[k], LV_OBJ_FLAG_HIDDEN); else lv_obj_remove_flag(g_zs_then[k], LV_OBJ_FLAG_HIDDEN); }
    if(run){ lv_obj_add_flag(g_zs_under, LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(g_zs_go, LV_OBJ_FLAG_HIDDEN);
             lv_obj_remove_flag(g_zs_plus, LV_OBJ_FLAG_HIDDEN); lv_obj_remove_flag(g_zs_cancel, LV_OBJ_FLAG_HIDDEN); }
    else   { lv_obj_remove_flag(g_zs_under, LV_OBJ_FLAG_HIDDEN); lv_obj_remove_flag(g_zs_go, LV_OBJ_FLAG_HIDDEN);
             lv_obj_add_flag(g_zs_plus, LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(g_zs_cancel, LV_OBJ_FLAG_HIDDEN); }
    int min = run ? (secs + 59) / 60 : g_zs_min;
    lv_arc_set_value(g_zs_arc, run ? (secs > 7200 ? 7200 : secs) : g_zs_min * 60);
    snprintf(b, sizeof b, "%d", min); lv_label_set_text(g_zs_big, b);
    zs_stops(b, sizeof b, run ? secs : g_zs_min * 60, run ? "minutes left" : "minutes"); lv_label_set_text(g_zs_sub, b);
    if(!run) zs_then_paint();
}
static void zs_tick(lv_timer_t *t){ (void)t; if(g_zs_arc && screen_current() == SCR_NPMENU && !g_zs_drag) zs_paint(); }
static void zs_set_from(lv_point_t p){
    double a = atan2((double)(p.y - 180), (double)(p.x - 180)) * 57.2957795 + 90.0;
    while(a < 0) a += 360.0; while(a >= 360.0) a -= 360.0;
    int m = (int)lround(a / 3.0 / 5.0) * 5;                    /* 3 degrees a minute, in steps of 5 */
    if(m < 5) m = 5; if(m > 120) m = 120;
    if(m != g_zs_min){ g_zs_min = m; zs_paint(); }
}
static void zs_touch_cb(lv_event_t *e){
    if(ui_sleep_state(NULL) == 1) return;                      /* running: the dial only shows the time left */
    lv_event_code_t c = lv_event_get_code(e); lv_indev_t *in = lv_indev_active(); if(!in) return;
    lv_point_t p; lv_indev_get_point(in, &p);
    int dx = p.x - 180, dy = p.y - 180, r2 = dx*dx + dy*dy;
    if(c == LV_EVENT_PRESSED){ g_zs_drag = r2 >= 110*110; if(g_zs_drag) zs_set_from(p); }
    else if(c == LV_EVENT_PRESSING && g_zs_drag) zs_set_from(p);
    else if(c == LV_EVENT_RELEASED || c == LV_EVENT_PRESS_LOST){ if(g_zs_drag) simple_cfg_set_int("zune_sleep_min", g_zs_min); g_zs_drag = 0; }
}
static void zs_then_cb(lv_event_t *e){ simple_cfg_set_int("sleep_action", (int)(intptr_t)lv_event_get_user_data(e)); zs_paint(); }
static void zs_start_cb(lv_event_t *e){ (void)e; ui_set_sleep_timer(g_zs_min); zune_status_refresh(); zs_paint();
                                        char t[48]; snprintf(t, sizeof t, "Sleep in %d min", g_zs_min); ui_toast(t); }
static void zs_plus_cb(lv_event_t *e){ (void)e; int secs = 0; ui_sleep_state(&secs); int m = (secs + 59) / 60 + 15; if(m > 120) m = 120;
                                       ui_set_sleep_timer(m); zune_status_refresh(); zs_paint(); }
static void zs_cancel_cb(lv_event_t *e){ (void)e; ui_set_sleep_timer(0); simple_cfg_set_int("sleep_idx", 0); zune_status_refresh(); zs_paint(); ui_toast("Sleep timer off"); }
static void zs_del_cb(lv_event_t *e){ (void)e; if(g_zs_tm){ lv_timer_delete(g_zs_tm); g_zs_tm = NULL; } g_zs_arc = NULL; }
static lv_obj_t *zs_text(lv_obj_t *box, const char *t, const lv_font_t *f, lv_color_t c, int y, lv_event_cb_t cb, void *ud, const char *action){
    lv_obj_t *l = lv_label_create(box); kit_keep(l); lv_label_set_text(l, t);
    lv_obj_set_style_text_font(l, f, 0); lv_obj_set_style_text_color(l, c, 0);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, y);
    if(cb){ lv_obj_add_flag(l, LV_OBJ_FLAG_CLICKABLE); lv_obj_set_ext_click_area(l, 12); ui_on(l, cb, LV_EVENT_CLICKED, ud, action, UI_CORE); }
    return l;
}
static void zune_sleep(lv_obj_t *box){
    lv_obj_clean(box);
    g_zs_min = simple_cfg_get_int("zune_sleep_min", 45); if(g_zs_min < 5 || g_zs_min > 120) g_zs_min = 45;
    lv_obj_t *touch = lv_obj_create(box); lv_obj_remove_style_all(touch); kit_keep(touch);   /* the dial's drag surface */
    lv_obj_set_size(touch, 360, 360); lv_obj_add_flag(touch, LV_OBJ_FLAG_CLICKABLE); lv_obj_remove_flag(touch, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(touch, zs_touch_cb, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(touch, zs_del_cb, LV_EVENT_DELETE, NULL);
    g_zs_arc = lv_arc_create(box); kit_keep(g_zs_arc);
    lv_obj_set_size(g_zs_arc, 332, 332); lv_obj_center(g_zs_arc); lv_obj_remove_flag(g_zs_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_arc_set_rotation(g_zs_arc, 270); lv_arc_set_bg_angles(g_zs_arc, 0, 360); lv_arc_set_range(g_zs_arc, 0, 7200);
    lv_obj_set_style_arc_width(g_zs_arc, 8, LV_PART_MAIN); lv_obj_set_style_arc_width(g_zs_arc, 8, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(g_zs_arc, TC(CONTROL_TRACK), LV_PART_MAIN); lv_obj_set_style_arc_color(g_zs_arc, TC(ACCENT_PRIMARY), LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(g_zs_arc, false, LV_PART_INDICATOR); lv_obj_set_style_arc_rounded(g_zs_arc, false, LV_PART_MAIN);
    lv_obj_set_style_bg_color(g_zs_arc, TC(ACCENT_PRIMARY), LV_PART_KNOB); lv_obj_set_style_bg_opa(g_zs_arc, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_pad_all(g_zs_arc, 6, LV_PART_KNOB);
    /* ticks every 15 minutes (long at 0 / 30 / 60 / 90) and their numbers */
    static lv_point_precise_t pts[8][2];
    for(int k = 0; k < 8; k++){
        double a = (k * 45.0 - 90.0) * 0.0174533; int major = !(k & 1);
        double r1 = major ? 135 : 136, r2 = major ? 145 : 144;
        pts[k][0].x = 180 + r1 * cos(a); pts[k][0].y = 180 + r1 * sin(a); pts[k][1].x = 180 + r2 * cos(a); pts[k][1].y = 180 + r2 * sin(a);
        lv_obj_t *ln = lv_line_create(box); kit_keep(ln); lv_line_set_points(ln, pts[k], 2);
        lv_obj_set_style_line_width(ln, 2, 0); lv_obj_set_style_line_color(ln, TC(TEXT_PRIMARY), 0);
        lv_obj_remove_flag(ln, LV_OBJ_FLAG_CLICKABLE);
        if(major){ static const char *const N[4] = { "0", "30", "60", "90" };
            lv_obj_t *n = lv_label_create(box); kit_keep(n); lv_label_set_text(n, N[k / 2]);
            lv_obj_set_style_text_font(n, TF(UI_10), 0); lv_obj_set_style_text_color(n, TC(TEXT_MUTED), 0);
            lv_obj_update_layout(n);
            lv_obj_set_pos(n, (int)(180 + 122 * cos(a)) - lv_obj_get_width(n) / 2, (int)(180 + 122 * sin(a)) - lv_obj_get_height(n) / 2); }
    }
    g_zs_big = zs_text(box, "45", &font_zune_light_96, TC(TEXT_PRIMARY), 80, NULL, NULL, NULL);
    lv_obj_set_style_text_letter_space(g_zs_big, -4, 0);
    g_zs_sub = zs_text(box, "", TF(UI_14), TC(TEXT_MUTED), 186, NULL, NULL, NULL);
    lv_obj_t *then = lv_obj_create(box); lv_obj_remove_style_all(then); kit_keep(then);       /* then  pause  power off */
    lv_obj_set_size(then, 300, 24); lv_obj_align(then, LV_ALIGN_TOP_MID, 0, 216);
    lv_obj_set_flex_flow(then, LV_FLEX_FLOW_ROW); lv_obj_set_flex_align(then, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(then, 16, 0); lv_obj_remove_flag(then, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    g_zs_then[0] = zs_text(then, "then", TF(UI_12), TC(TEXT_MUTED), 0, NULL, NULL, NULL);
    g_zs_then[1] = zs_text(then, "pause", TF(UI_14), TC(TEXT_PRIMARY), 0, zs_then_cb, (void *)(intptr_t)POWER_SLEEP_PAUSE, "npmenu.zune.sleep.pause");
    g_zs_then[2] = zs_text(then, "power off", TF(UI_14), TC(TEXT_PRIMARY), 0, zs_then_cb, (void *)(intptr_t)POWER_SLEEP_SHUTDOWN, "npmenu.zune.sleep.off");
    lv_obj_set_style_pad_right(g_zs_then[0], 0, 0);
    g_zs_under = lv_obj_create(box); lv_obj_remove_style_all(g_zs_under); kit_keep(g_zs_under);
    lv_obj_set_height(g_zs_under, 2); lv_obj_set_style_bg_color(g_zs_under, TC(ACCENT_PRIMARY), 0); lv_obj_set_style_bg_opa(g_zs_under, LV_OPA_COVER, 0);
    lv_obj_remove_flag(g_zs_under, LV_OBJ_FLAG_CLICKABLE);
    g_zs_go = zs_text(box, "start", TF(UI_16), TC(ACCENT_PRIMARY), 264, zs_start_cb, NULL, "npmenu.zune.sleep.start");
    g_zs_plus = zs_text(box, "+15 min", TF(UI_16), TC(ACCENT_PRIMARY), 224, zs_plus_cb, NULL, "npmenu.zune.sleep.plus");
    g_zs_cancel = zs_text(box, "cancel timer", TF(UI_14), TC(TEXT_MUTED), 256, zs_cancel_cb, NULL, "npmenu.zune.sleep.cancel");
    g_zs_tm = lv_timer_create(zs_tick, 1000, NULL);
    zs_paint();
}
enum { ZA_FAV, ZA_ALBUM, ZA_ARTIST, ZA_PLAYLIST, ZA_QUEUE, ZA_MODE, ZA_LYRICS, ZA_INFO, ZA_SLEEP, ZA_EQ, ZA_TAGS, ZA_N };
static const char *const ZA_LABEL[ZA_N] = { "favourite", "go to album", "go to artist", "add to playlist", "queue",
                                            "play mode", "lyrics", "song info", "sleep timer", "equalizer", "tags" };
static void zune_fav_paint(void){ zdial_set_text(ZA_FAV, "favourite");   /* on: magenta (Open Sans has no heart glyph) */
                                  zdial_set_color(ZA_FAV, g_fav ? TC(ACCENT_PRIMARY) : TC(TEXT_PRIMARY)); }
static void zune_act_pick(int i){
    switch(i){
    case ZA_FAV:      zune_run(ctx_fav_cb); zune_fav_paint(); break;
    case ZA_ALBUM:    zune_run(ctx_album_cb); break;
    case ZA_ARTIST:   zune_run(ctx_artist_cb); break;
    case ZA_PLAYLIST: zune_run(ctx_addpl_cb); break;
    case ZA_QUEUE:    queue_open(); break;
    case ZA_MODE:     if(g_zbox) zune_playmode(g_zbox); break;
    case ZA_LYRICS:   zune_run(ctx_lyrics_cb); break;
    case ZA_INFO:     zune_run(ctx_info_cb); break;
    case ZA_SLEEP:    if(g_zbox) zune_sleep(g_zbox); break;
    case ZA_EQ:       zune_run(hub_eq_open_cb); break;
    case ZA_TAGS:     zune_run(tags_cb); break;
    }
}
static void zune_actions(lv_obj_t *box, const track_state_t *st){
    g_zbox = box;
    npmenu_set(st, 1, NULL);                                    /* the track this menu acts on */
    lv_obj_set_style_bg_color(box, TC(CANVAS), 0); lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    zdial_build(box, ZA_LABEL, ZA_N, ZA_ALBUM, zune_act_pick, 56, "npmenu.zune");
    zune_fav_paint();
    char t[200];
    if(st && st->have_track) snprintf(t, sizeof t, "%.90s%s%.90s", st->title, st->artist[0] ? " \xC2\xB7 " : "", st->artist);
    else snprintf(t, sizeof t, "nothing playing");
    zune_header(box, t, 1);
}
#include "track_sheet.h"
/* The sheet's own order: Immersive sits after Lyrics. Entries map to the shared ZA_ actions; SHEET_IMM is the sheet's
 * one extra (the Zune dial keeps its own list). */
#define SHEET_IMM ZA_N
static const int SHEET_ORDER[]={ZA_FAV,ZA_ALBUM,ZA_ARTIST,ZA_PLAYLIST,ZA_QUEUE,ZA_MODE,ZA_LYRICS,SHEET_IMM,ZA_INFO,ZA_SLEEP,ZA_EQ,ZA_TAGS};
#define SHEET_N ((int)(sizeof SHEET_ORDER/sizeof SHEET_ORDER[0]))
static void sheet_pick(lv_event_t *e){
    int k=(int)(intptr_t)lv_event_get_user_data(e);
    if(k<0||k>=SHEET_N) return;
    int i=SHEET_ORDER[k];
    if(i==SHEET_IMM){void disco_open_np_immersive(void);disco_open_np_immersive();return;}
    if(i==ZA_MODE){setlist_open("Playback");settings_open_key("work_mode");return;}
    if(i==ZA_SLEEP){setlist_open("Battery & power");settings_open_group("Sleep timer");return;}
    if(i==ZA_FAV){zune_run(ctx_fav_cb);return;}
    zune_act_pick(i);
}
__attribute__((unused)) static void track_actions(lv_obj_t *box,const track_state_t *st){
    g_zbox=box;npmenu_set(st,1,NULL);
    static const char *const names[]={"Favourite","Go to album","Go to artist","Add to playlist","Queue","Play mode","Lyrics","Immersive","Song info","Sleep timer","Equalizer","Tags"};
    static const char *const icons[]={TH_IC_HEART,LV_SYMBOL_IMAGE,LV_SYMBOL_HOME,LV_SYMBOL_PLUS,LV_SYMBOL_LIST,LV_SYMBOL_SHUFFLE,LV_SYMBOL_AUDIO,LV_SYMBOL_EYE_OPEN,LV_SYMBOL_FILE,LV_SYMBOL_POWER,LV_SYMBOL_SETTINGS,LV_SYMBOL_EDIT};
    _Static_assert(sizeof names/sizeof names[0]==SHEET_N && sizeof icons/sizeof icons[0]==SHEET_N,"sheet tables");
    track_sheet_build(box,st->title,st->artist,ui_current_cover_dsc(),names,icons,SHEET_N,sheet_pick,close_cb);
}
static int g_zsleep_direct;
void npmenu_open_sleep(void){ g_zsleep_direct = 1; screen_show(SCR_NPMENU); }   /* Zune Quick Settings: straight to the timer */
static void build_menu(lv_obj_t *box){
    if(!box) return;
    tags_close();
    g_fav_lbl=NULL;
    lv_obj_clean(box);
    memset(&g_orb, 0, sizeof g_orb); g_cover_img = g_menu_bg = g_menu_title = NULL;
    track_state_t st; ipc_get_state(&st);
    int book = st.path[0] && mdb_is_book_path(st.path);
    if(th_zune() && g_zsleep_direct){ g_zsleep_direct = 0; g_zbox = box; npmenu_set(&st, 1, NULL);
        lv_obj_set_style_bg_color(box, TC(CANVAS), 0); lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0); zune_sleep(box); return; }
    int ring = !book && (th_disco()||th_simple()||th_null());   /* the round menu, as the Library's album menu */
    if(th_zune() && !book){ zune_actions(box, &st); return; }   /* Zune: the actions as a dial (audiobooks keep their own four) */
    g_menu_bg = lv_image_create(box);                           /* the album's blurred wash, dimmed */
    lv_obj_set_size(g_menu_bg, 360, 360); lv_obj_clear_flag(g_menu_bg, LV_OBJ_FLAG_CLICKABLE); lv_obj_add_flag(g_menu_bg, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *dim = lv_obj_create(box); lv_obj_remove_style_all(dim); lv_obj_set_size(dim, 360, 360);
    lv_obj_set_style_bg_color(dim, TC(CANVAS), 0); lv_obj_set_style_bg_opa(dim, 215, 0); lv_obj_clear_flag(dim, LV_OBJ_FLAG_CLICKABLE);
    if(th_braun_family()){ lv_obj_add_flag(dim, LV_OBJ_FLAG_HIDDEN); br_face(box); }   /* Braun: the grille, no album wash */
    g_menu_title = lv_label_create(box);
    lv_label_set_text(g_menu_title, "");
    lv_label_set_long_mode(g_menu_title, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(g_menu_title, 200);
    lv_obj_set_style_text_align(g_menu_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(g_menu_title, ui_font_cjk(18), 0);
    lv_obj_set_style_text_color(g_menu_title, TC(TEXT_SECONDARY), 0);
    lv_obj_align(g_menu_title, LV_ALIGN_TOP_MID, 0, 10);
    if(book){
        static const orbit_item_t it[4] = { { LV_SYMBOL_LIST, "Chapters" }, { LV_SYMBOL_FILE, "Details" },
                                            { LV_SYMBOL_SETTINGS, "Equalizer" }, { LV_SYMBOL_IMAGE, "Full art" } };
        static lv_event_cb_t const CB[4] = { hub_chapters_cb, ctx_info_cb, hub_eq_open_cb, hub_fsart_cb };
        orbit_create(&g_orb, box, it, 4, -45, NULL);
        for(int i = 0; i < 4; i++) lv_obj_add_event_cb(g_orb.btn[i], CB[i], LV_EVENT_CLICKED, NULL);
    } else if(ring){   /* clockwise from 12 */
        /* "View ..." opens somewhere, "+..." adds: the views together on the right, the adds together lower left */
        static const orbit_item_t it[8] = {
            { TH_IC_HEART, "Favourite" }, { LV_SYMBOL_AUDIO, "Lyrics" }, { LV_SYMBOL_IMAGE, "Album" }, { LV_SYMBOL_HOME, "Artist" },
            { LV_SYMBOL_BARS, "View queue" }, { LV_SYMBOL_FILE, "Info" }, { LV_SYMBOL_LIST, "+Queue" }, { LV_SYMBOL_PLUS, "+Playlist" } };
        static lv_event_cb_t const CB[8] = { ctx_fav_cb, ctx_lyrics_cb, ctx_album_cb, ctx_artist_cb, ctx_queue_open_cb, ctx_info_cb, ctx_queue_add_cb, ctx_addpl_cb };
        static const orbit_item_t nit[8] = {   /* Null: its wide face takes short words; the open-queue button an arrow */
            { TH_IC_HEART, "Fav" }, { LV_SYMBOL_AUDIO, "Lyrics" }, { LV_SYMBOL_IMAGE, "Album" }, { LV_SYMBOL_HOME, "Artist" },
            { LV_SYMBOL_RIGHT, "Queue" }, { LV_SYMBOL_FILE, "Info" }, { LV_SYMBOL_LIST, "+Queue" }, { LV_SYMBOL_PLUS, "+List" } };
        orbit_full_ring();
        orbit_create(&g_orb, box, th_null() ? nit : it, 8, -90, NULL);
        lv_obj_set_style_text_font(g_orb.icon[0], &font_theme_20, 0);
        for(int i = 0; i < 8; i++) lv_obj_add_event_cb(g_orb.btn[i], CB[i], LV_EVENT_CLICKED, NULL);
    } else {
        static const orbit_item_t it[8] = {
            { TH_IC_HEART, "Favourite" }, { LV_SYMBOL_PLUS, "Playlist" }, { LV_SYMBOL_AUDIO, "Lyrics" }, { LV_SYMBOL_SETTINGS, "Equalizer" },
            { LV_SYMBOL_FILE, "Info" }, { LV_SYMBOL_IMAGE, "Album" }, { LV_SYMBOL_HOME, "Artist" }, { LV_SYMBOL_DOWNLOAD, "Tags" } };
        static lv_event_cb_t const CB[8] = { ctx_fav_cb, ctx_addpl_cb, ctx_lyrics_cb, hub_eq_open_cb, ctx_info_cb, ctx_album_cb, ctx_artist_cb, tags_cb };
        orbit_create(&g_orb, box, it, 8, -90, NULL);           /* each button runs its own action */
        lv_obj_set_style_text_font(g_orb.icon[0], &font_theme_20, 0);
        for(int i = 0; i < 8; i++) lv_obj_add_event_cb(g_orb.btn[i], CB[i], LV_EVENT_CLICKED, NULL);
    }
    if(ring) orbit_spread();
    orbit_popup_polish(&g_orb);
    if(ring){ void disco_vec_icon(lv_obj_t *, const char *, int);   /* album: a record; artist: a microphone */
              disco_vec_icon(g_orb.icon[2], "@vinyl", 24); disco_vec_icon(g_orb.icon[3], "@mic", 24);
              if(g_menu_title) lv_obj_add_flag(g_menu_title, LV_OBJ_FLAG_HIDDEN); }   /* the wider ring reaches the top: the cover says which song */
    orbit_hub_create(&g_orb, box, close_cb, "", "");            /* the hub: the cover inside its progress ring */
    lv_obj_set_style_clip_corner(g_orb.hub, true, 0);
    g_cover_img = lv_image_create(g_orb.hub);
    lv_obj_clear_flag(g_cover_img, LV_OBJ_FLAG_CLICKABLE); lv_obj_add_flag(g_cover_img, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *x = lv_obj_create(g_orb.hub);                     /* close: a dark disc with x over the cover */
    lv_obj_remove_style_all(x); lv_obj_set_size(x, 32, 32); lv_obj_center(x);
    lv_obj_set_style_radius(x, LV_RADIUS_CIRCLE, 0); lv_obj_set_style_bg_color(x, TC(CANVAS), 0);
    lv_obj_set_style_bg_opa(x, 190, 0); lv_obj_clear_flag(x, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *xl = lv_label_create(x); lv_label_set_text(xl, ring ? LV_SYMBOL_LEFT : LV_SYMBOL_CLOSE);   /* a back arrow */
    lv_obj_set_style_text_font(xl, TF(UI_14), 0); lv_obj_set_style_text_color(xl, TC(TEXT_PRIMARY), 0); lv_obj_center(xl);
    orbit_hub_ring(&g_orb, ORBIT_RING_PROGRESS, ui_media_accent(), 0);
    if(book) g_fav = 0;
    fav_refresh();
    npmenu_set(&st, 1, NULL);                                   /* title + ring right away */
    npmenu_refresh_art_now();
    if(th_braun_family()){ orbit_braun_icons(&g_orb); if(g_menu_bg) lv_obj_add_flag(g_menu_bg, LV_OBJ_FLAG_HIDDEN);
                    if(g_menu_title){ lv_obj_set_style_text_color(g_menu_title, TC(TEXT_PRIMARY), 0); lv_obj_set_style_text_font(g_menu_title, br_font(14, 0), 0); } }
}
void npmenu_create(lv_obj_t *root){
    lv_obj_set_style_bg_color(root, TC(CANVAS), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    g_box_menu = lv_obj_create(root); lv_obj_remove_style_all(g_box_menu); lv_obj_set_size(g_box_menu, 360, 360);
    lv_obj_clear_flag(g_box_menu, LV_OBJ_FLAG_SCROLLABLE);
}

/* ---- add-to-playlist picker (SCR_PLPICK) -------------------------------- */
static char g_pick_path[256];
static char g_pick_folder[512];
/* what the picker adds: one song (g_pick_path; a CUE/ISO sub-track when g_pick_sub, by g_pick_track), or a whole
 * ALBUM/ARTIST/GENRE group (g_pick_col/g_pick_val, set by plpick_set_group) */
static int  g_pick_sub, g_pick_track;
static int  g_pick_unsure;   /* a CUE/ISO file whose playing track could not be identified: adding is refused */
static char g_pick_col[8], g_pick_val[MDB_STR];
static int *g_pick_ids, g_pick_nids;   /* or exactly these SONG.IDs (set by plpick_set_ids) */
static lv_obj_t *g_pick_list;
static lv_obj_t *g_pick_toast;
void plpick_set_song(const char *path, int pos_id, const char *title){
    { void zpick_visit(void); zpick_visit(); }
    g_pick_folder[0]=0;
    snprintf(g_pick_path, sizeof g_pick_path, "%s", path?path:"");
    g_pick_col[0] = g_pick_val[0] = 0; free(g_pick_ids); g_pick_ids = NULL; g_pick_nids = 0;
    g_pick_sub = g_pick_path[0] && mdb_song_subtrack_verified(g_pick_path, pos_id, title, &g_pick_track, NULL, NULL);
    /* the path alone names several tracks and none was established: never add them all */
    g_pick_unsure = g_pick_path[0] && !g_pick_sub && mdb_song_multi(g_pick_path);
    if(th_zune()) plpick_reload();                                   /* Zune: the ticks and the title follow the song */
}
void plpick_set_group(const char *col, const char *val){
    { void zpick_visit(void); zpick_visit(); }
    g_pick_folder[0]=0;
    g_pick_path[0] = 0; g_pick_sub = 0; g_pick_unsure = 0; free(g_pick_ids); g_pick_ids = NULL; g_pick_nids = 0;
    snprintf(g_pick_col, sizeof g_pick_col, "%s", col?col:"");
    snprintf(g_pick_val, sizeof g_pick_val, "%s", val?val:"");
    plpick_reload();                  /* refresh list + clear any old toast (the caller then shows SCR_PLPICK) */
}
void plpick_set_ids(const int *ids, int n){
    { void zpick_visit(void); zpick_visit(); }
    g_pick_folder[0]=0;
    free(g_pick_ids); g_pick_ids = NULL; g_pick_nids = 0;
    g_pick_path[0] = 0; g_pick_sub = 0; g_pick_unsure = 0; g_pick_col[0] = g_pick_val[0] = 0;
    if(ids && n > 0 && (size_t)n <= ((size_t)-1) / sizeof(int) && (g_pick_ids = malloc((size_t)n * sizeof(int)))){
        memcpy(g_pick_ids, ids, (size_t)n * sizeof(int)); g_pick_nids = n;
    }
    plpick_reload();
}
static int pick_have(void){ return g_pick_path[0] || g_pick_nids > 0 || (g_pick_col[0] && g_pick_val[0]); }
static int pick_has_song(long pid){
    return g_pick_sub ? mdb_playlist_has_subtrack(pid, g_pick_path, g_pick_track) : mdb_playlist_has_song(pid, g_pick_path);
}
/* add the pending song or group; returns rows added (a group adds many, a song 0 or 1) */
static int pick_add_to(long pid){
    if(g_pick_path[0])
        return g_pick_sub ? mdb_playlist_add_subtrack(pid, g_pick_path, g_pick_track, NULL) : mdb_playlist_add_song(pid, g_pick_path);
    if(g_pick_nids > 0){ int r = mdb_playlist_add_ids(pid, g_pick_ids, g_pick_nids); return r < 0 ? 0 : r; }
    return mdb_playlist_add_group(pid, g_pick_col, g_pick_val);
}

void plpick_set_folder(const char *dir){
    plpick_set_song(NULL,0,NULL);
    snprintf(g_pick_folder,sizeof g_pick_folder,"%s",dir?dir:"");
    if(th_zune()) plpick_reload();
}
static lv_timer_t *g_pick_timer;
static void pick_toast_hide_cb(lv_timer_t *t){
    if(g_pick_toast) lv_obj_add_flag(g_pick_toast, LV_OBJ_FLAG_HIDDEN);
    lv_timer_delete(t);
    g_pick_timer = NULL;
}
static void pick_toast(const char *msg){
    if(th_zune()){ ui_toast(msg); return; }                         /* Zune: the shared toast */
    if(!g_pick_toast) return;
    lv_label_set_text(lv_obj_get_child(g_pick_toast,0), msg);
    lv_obj_clear_flag(g_pick_toast, LV_OBJ_FLAG_HIDDEN);
    /* auto-dismiss after 2.2s (one-shot: the cb deletes the timer) */
    if(g_pick_timer) lv_timer_delete(g_pick_timer);
    g_pick_timer = lv_timer_create(pick_toast_hide_cb, 2200, NULL);
}
/* 1 (after a toast) when the pending song is an unidentified track of a CUE/ISO file */
static int pick_refuse_unsure(void){
    if(!g_pick_unsure) return 0;
    pick_toast("Couldn't identify this track - try again while it plays");
    return 1;
}
static void pick_added_toast(int n, const char *one, const char *fail){
    if(g_pick_path[0]){ pick_toast(n ? one : fail); return; }
    if(n > 0){ char m[40]; snprintf(m, sizeof m, "Added %d songs", n); pick_toast(m); }
    else pick_toast("Nothing new to add");
}
/* duplicate-confirm dialog */
static lv_obj_t *g_dup_dlg;
static long g_dup_pid;
static void dup_close(void){ if(g_dup_dlg){ lv_obj_del(g_dup_dlg); g_dup_dlg=NULL; } }
/* Public: dismiss transient popups on any screen navigation, so the duplicate-add
 * confirm dialog (on lv_layer_top, no scrim) can't float onto a later screen and act
 * on a stale g_dup_pid/g_pick_path. Called from the screen-manager transition. */
void npmenu_close_transients(void){
    dup_close();
    if(g_tags_ov){ lv_obj_delete_async(g_tags_ov); g_tags_ov = NULL; }   /* "Add lyrics & artwork" belongs to the screen it opened on */
}
static void dup_add_cb(lv_event_t *e){
    if(lv_event_get_code(e)!=LV_EVENT_CLICKED) return;
    dup_close();
    /* "Add anyway" on a KNOWN duplicate: the song is in the playlist either way, so
     * "Kept in playlist" is accurate; only invalidate the scope if the row actually changed. */
    if(g_pick_path[0]) pick_add_to(g_dup_pid);
    pick_toast("Kept in playlist");
}
static void dup_cancel_cb(lv_event_t *e){ if(lv_event_get_code(e)==LV_EVENT_CLICKED) dup_close(); }
static lv_obj_t *dlg_btn(lv_obj_t *p, int y, const char *txt, lv_color_t bg, lv_color_t fg, lv_event_cb_t cb){
    lv_obj_t *b = lv_button_create(p);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, 200, 38);
    lv_obj_set_ext_click_area(b, 4);   /* 38px pill -> ~46px touch target */
    lv_obj_align(b, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_set_style_radius(b, 12, 0);
    lv_obj_set_style_bg_color(b, bg, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    ui_on(b, cb, LV_EVENT_CLICKED, NULL, "npmenus.cb", UI_CORE);
    lv_obj_t *l = lv_label_create(b); lv_label_set_text(l, tr(txt));
    lv_obj_set_style_text_font(l, TF(UI_16), 0);
    lv_obj_set_style_text_color(l, fg, 0); lv_obj_center(l);
    return b;
}
static void show_dup(long pid){
    g_dup_pid = pid;
    dup_close();
    g_dup_dlg = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(g_dup_dlg);
    lv_obj_set_size(g_dup_dlg, 248, 184);
    lv_obj_center(g_dup_dlg);
    lv_obj_set_style_radius(g_dup_dlg, 16, 0);
    lv_obj_set_style_bg_color(g_dup_dlg, TC(SURFACE), 0);
    lv_obj_set_style_bg_opa(g_dup_dlg, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_dup_dlg, 1, 0);
    lv_obj_set_style_border_color(g_dup_dlg, TC(BORDER), 0);
    lv_obj_clear_flag(g_dup_dlg, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *t = lv_label_create(g_dup_dlg);
    lv_label_set_text(t, tr("Already in this playlist"));
    lv_obj_set_style_text_font(t, TF(UI_16), 0);
    lv_obj_set_style_text_color(t, TC(TEXT_PRIMARY), 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 24);
    dlg_btn(g_dup_dlg, 70,  "Add anyway", TC(SURFACE_RAISED), TC(TEXT_PRIMARY), dup_add_cb);
    dlg_btn(g_dup_dlg, 116, "Cancel",     TC(SURFACE_RAISED), TC(TEXT_MUTED), dup_cancel_cb);
}
static void pick_add_cb(lv_event_t *e){
    if(lv_event_get_code(e)!=LV_EVENT_CLICKED) return;
    long pid = (long)(intptr_t)lv_event_get_user_data(e);
    if(g_pick_folder[0]){ int n=mdb_playlist_add_folder(pid,g_pick_folder); ui_invalidate_play_scope(); char b[64]; snprintf(b,sizeof b,"Added %d songs",n); ui_toast(b); return; }
    if(!pick_have() || pick_refuse_unsure()) return;
    if(g_pick_path[0] && pick_has_song(pid)){ show_dup(pid); return; }   /* a group skips its own duplicates */
    int n = pick_add_to(pid);
    pick_added_toast(n, "Added to playlist", "Couldn't add");
}
static void pick_newname_done(const char *name){
    if(!name || !name[0]) return;
    if(pick_refuse_unsure()) return;   /* before creating a playlist that would stay empty */
    long pid = mdb_playlist_create(name);
    plpick_reload();
    if(pid <= 0){ pick_toast("Couldn't create"); return; }
    if(pick_have()){
        int n = pick_add_to(pid);
        pick_added_toast(n, "Created + added", "Created (song not added)");
    } else
        pick_toast("Playlist created");
}
static void pick_new_cb(lv_event_t *e){
    if(lv_event_get_code(e)==LV_EVENT_CLICKED) kbinput_open("Playlist name", "", pick_newname_done);
}
static void zpick_reload(void);
static void plpick_reload(void){
    if(!g_pick_list) return;
    if(th_zune()){ zpick_reload(); return; }
    if(g_pick_timer){ lv_timer_delete(g_pick_timer); g_pick_timer = NULL; }
    if(g_pick_toast) lv_obj_add_flag(g_pick_toast, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clean(g_pick_list);
    menu_row(g_pick_list, LV_SYMBOL_PLUS "  New Playlist", pick_new_cb, NULL);
    /* size to the real playlist count (no 64 cap). pick_add_cb takes the id by value, so
     * names/ids are only needed during this build loop -> malloc + free here. */
    int num = mdb_playlist_num();
    int okn = num>0 && (size_t)num <= ((size_t)-1) / MDB_STR;   /* 32-bit multiply overflow guard */
    char (*names)[MDB_STR] = okn ? malloc((size_t)num*MDB_STR)     : NULL;
    long  *ids            = okn ? malloc((size_t)num*sizeof(long)) : NULL;
    int n = (names && ids) ? mdb_playlists(names, ids, num) : 0;
    for(int i=0;i<n;i++)
        menu_row(g_pick_list, names[i], pick_add_cb, (void*)(intptr_t)ids[i]);
    free(names); free(ids);
}
static void zpick_create(lv_obj_t *root);
void plpick_create(lv_obj_t *root){
    if(th_zune()){ zpick_create(root); return; }
    panel_header(root, "Add to Playlist");
    g_pick_list = panel_list(root);
    /* a small confirmation toast */
    g_pick_toast = lv_obj_create(root);
    lv_obj_remove_style_all(g_pick_toast);
    lv_obj_set_size(g_pick_toast, 220, 36);
    lv_obj_align(g_pick_toast, LV_ALIGN_BOTTOM_MID, 0, -18);
    lv_obj_set_style_radius(g_pick_toast, 18, 0);
    lv_obj_set_style_bg_color(g_pick_toast, TC(SURFACE_RAISED), 0);
    lv_obj_set_style_bg_opa(g_pick_toast, LV_OPA_COVER, 0);
    lv_obj_t *tl = lv_label_create(g_pick_toast);
    lv_obj_set_style_text_color(tl, TC(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(tl, TF(UI_14), 0);
    lv_obj_center(tl);
    lv_obj_add_flag(g_pick_toast, LV_OBJ_FLAG_HIDDEN);
    plpick_reload();
}

/* ---- Zune add-to-playlist (Figma 68): "add <song> to...", new playlist in a ring, each playlist with its mosaic and a
 * tick - filled when the song is already in it; a tap adds (or, for one song, takes it out again); done goes back ---- */
LV_FONT_DECLARE(font_zune_light_21) LV_FONT_DECLARE(font_zune_light_24) LV_FONT_DECLARE(font_zune_regular_13) LV_FONT_DECLARE(font_zune_regular_16)
static lv_obj_t *g_zpick_hdr, *g_zpick_hdr_l, *g_zpick_art;
static long g_zpick_done[64]; static int g_zpick_nd;              /* a group already added this visit (shows ticked) */
static void zpick_curve(void){
    if(!g_pick_list) return;
    for(uint32_t k = 0; k < lv_obj_get_child_count(g_pick_list); k++){
        lv_obj_t *r = lv_obj_get_child(g_pick_list, k);
        lv_area_t a; lv_obj_get_coords(r, &a);
        int d = abs((a.y1 + a.y2) / 2 - 180); if(d > 178) d = 178;
        int in = 180 - (int)sqrtf((float)(32400 - d * d)), x = in + 40, w = 360 - in - 36 - x; if(w < 120) w = 120;
        if(lv_obj_get_style_translate_x(r, 0) == x && lv_obj_get_width(r) == w) continue;
        lv_obj_set_style_translate_x(r, x, 0); lv_obj_set_width(r, w);
    }
}
static void zpick_scroll_cb(lv_event_t *e){ (void)e; zpick_curve(); }
static int zpick_has(long pid){
    if(g_pick_path[0]) return pick_has_song(pid);
    for(int k = 0; k < g_zpick_nd; k++) if(g_zpick_done[k] == pid) return 1;
    return 0;
}
static void zpick_row_cb(lv_event_t *e){
    if(lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    long pid = (long)(intptr_t)lv_event_get_user_data(e);
    int y = lv_obj_get_scroll_y(g_pick_list);
    if(g_pick_folder[0]){ int n = mdb_playlist_add_folder(pid, g_pick_folder); ui_invalidate_play_scope();
        char b[64]; snprintf(b, sizeof b, "Added %d songs", n); ui_toast(b); }
    else {
        if(!pick_have() || pick_refuse_unsure()) return;
        if(g_pick_path[0] && pick_has_song(pid)){                     /* in it already: a second tap takes it out */
            int pos = g_pick_sub ? 0 : mdb_playlist_index_of(pid, g_pick_path);
            if(pos > 0 && mdb_playlist_remove_at(pid, pos)) ui_toast("Removed from playlist");
            else ui_toast("Already in this playlist");
        } else pick_added_toast(pick_add_to(pid), "Added to playlist", "Couldn't add");
    }
    if(!g_pick_path[0] && g_zpick_nd < 64) g_zpick_done[g_zpick_nd++] = pid;
    library_refresh();
    zpick_reload();
    lv_obj_update_layout(g_pick_list); lv_obj_scroll_to_y(g_pick_list, y, LV_ANIM_OFF); zpick_curve();
}
static void zpick_done_cb(lv_event_t *e){ if(lv_event_get_code(e) == LV_EVENT_CLICKED) screen_back(); }
static lv_obj_t *zpick_row(void){
    lv_obj_t *r = lv_obj_create(g_pick_list); lv_obj_remove_style_all(r); kit_keep(r);
    lv_obj_set_size(r, 280, 54); lv_obj_remove_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(r, 8, 0);
    lv_obj_set_style_bg_color(r, TC(SURFACE), LV_STATE_PRESSED); lv_obj_set_style_bg_opa(r, LV_OPA_COVER, LV_STATE_PRESSED);
    return r;
}
static void zpick_title(void){
    char what[200]; int art = 0;
    if(g_pick_path[0]){ mdb_song_t sg; if(mdb_song_by_path(g_pick_path, &sg) == 1){ snprintf(what, sizeof what, "%s", sg.title); art = sg.id; }
                        else { const char *b = strrchr(g_pick_path, '/'); snprintf(what, sizeof what, "%s", b ? b + 1 : g_pick_path); } }
    else if(g_pick_folder[0]){ const char *b = strrchr(g_pick_folder, '/'); snprintf(what, sizeof what, "%s", b && b[1] ? b + 1 : g_pick_folder); }
    else if(g_pick_nids > 0){ snprintf(what, sizeof what, "%d song%s", g_pick_nids, g_pick_nids == 1 ? "" : "s"); art = g_pick_ids[0]; }
    else snprintf(what, sizeof what, "%s", g_pick_val);
    char t[260]; snprintf(t, sizeof t, "add %s to\xE2\x80\xA6", what);
    lv_label_set_text(g_zpick_hdr_l, t);
    lv_point_t sz; lv_text_get_size(&sz, t, lv_obj_get_style_text_font(g_zpick_hdr_l, 0), 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    lv_obj_set_width(g_zpick_hdr_l, sz.x + 2 < 210 ? sz.x + 2 : 210);
    if(g_zpick_art){ lv_obj_delete(g_zpick_art); g_zpick_art = NULL; }
    if(art > 0){ int ids[1] = { art }; g_zpick_art = lv_obj_create(g_zpick_hdr); lv_obj_remove_style_all(g_zpick_art);
                 lv_obj_set_size(g_zpick_art, 18, 18); lv_obj_remove_flag(g_zpick_art, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
                 lv_obj_move_to_index(g_zpick_art, 0); zthumb_mosaic(g_zpick_art, 0, 0, 18, ids, 1); }
}
static void zpick_reload(void){
    zpick_title();
    lv_obj_clean(g_pick_list);
    lv_obj_t *nr = zpick_row();                                         /* new playlist */
    lv_obj_t *ring = lv_obj_create(nr); lv_obj_remove_style_all(ring); kit_keep(ring);
    lv_obj_set_size(ring, 40, 40); lv_obj_align(ring, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(ring, 2, 0); lv_obj_set_style_border_color(ring, TC(ACCENT_PRIMARY), 0);
    lv_obj_remove_flag(ring, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *p = lv_label_create(ring); lv_label_set_text(p, "+");
    lv_obj_set_style_text_font(p, zune_font(&font_zune_light_24), 0); lv_obj_set_style_text_color(p, TC(ACCENT_PRIMARY), 0);
    lv_obj_align(p, LV_ALIGN_CENTER, 0, -2);
    lv_obj_t *nl = lv_label_create(nr); lv_label_set_text(nl, "new playlist");
    lv_obj_set_style_text_font(nl, zune_font(&font_zune_light_21), 0); lv_obj_set_style_text_color(nl, TC(ACCENT_PRIMARY), 0);
    lv_obj_align(nl, LV_ALIGN_LEFT_MID, 52, 0);
    ui_on(nr, pick_new_cb, LV_EVENT_CLICKED, NULL, "plpick.zune.new", UI_CORE);
    int num = mdb_playlist_num();
    int okn = num > 0 && (size_t)num <= ((size_t)-1) / MDB_STR;
    char (*names)[MDB_STR] = okn ? malloc((size_t)num * MDB_STR) : NULL;
    long *ids = okn ? malloc((size_t)num * sizeof(long)) : NULL;
    int n = (names && ids) ? mdb_playlists(names, ids, num) : 0;
    for(int i = 0; i < n; i++){
        lv_obj_t *r = zpick_row();
        int cov[4], nc = 0; long ms = 0; zthumb_playlist(ids[i], cov, &nc, &ms);
        zthumb_mosaic(r, 0, 7, 40, cov, nc);
        lv_obj_t *l = lv_label_create(r); lv_label_set_text(l, names[i]); lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_font(l, zune_font(&font_zune_light_21), 0); lv_obj_set_style_text_color(l, TC(TEXT_PRIMARY), 0);
        lv_obj_set_width(l, lv_pct(60)); lv_obj_align(l, LV_ALIGN_LEFT_MID, 52, 0);
        int on = zpick_has(ids[i]);                                     /* the tick: filled = in this playlist */
        lv_obj_t *tk = lv_obj_create(r); lv_obj_remove_style_all(tk); kit_keep(tk);
        lv_obj_set_size(tk, 24, 24); lv_obj_align(tk, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_set_style_radius(tk, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(tk, on ? 0 : 2, 0); lv_obj_set_style_border_color(tk, TC(TEXT_PRIMARY), 0);
        lv_obj_set_style_bg_color(tk, TC(ACCENT_PRIMARY), 0); lv_obj_set_style_bg_opa(tk, on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_remove_flag(tk, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        if(on){ static const lv_point_precise_t CK[3] = { { 6, 12 }, { 10, 16 }, { 18, 8 } };   /* the tick, drawn */
                lv_obj_t *c = lv_line_create(tk); lv_line_set_points(c, CK, 3);
                lv_obj_set_style_line_width(c, 2, 0); lv_obj_set_style_line_rounded(c, true, 0);
                lv_obj_set_style_line_color(c, TC(ON_ACCENT), 0); lv_obj_set_pos(c, 0, 0); }
        ui_on(r, zpick_row_cb, LV_EVENT_CLICKED, (void *)(intptr_t)ids[i], "plpick.zune.row", UI_CORE);
    }
    free(names); free(ids);
    lv_obj_update_layout(g_pick_list); zpick_curve();
}
static void zpick_create(lv_obj_t *root){
    lv_obj_set_style_bg_color(root, TC(CANVAS), 0); lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    g_pick_list = lv_obj_create(root); lv_obj_remove_style_all(g_pick_list);
    lv_obj_set_pos(g_pick_list, 0, 56); lv_obj_set_size(g_pick_list, 360, 304);
    lv_obj_set_flex_flow(g_pick_list, LV_FLEX_FLOW_COLUMN); lv_obj_set_style_pad_row(g_pick_list, 4, 0);
    lv_obj_set_style_pad_top(g_pick_list, 6, 0); lv_obj_set_style_pad_bottom(g_pick_list, 90, 0);
    lv_obj_set_scroll_dir(g_pick_list, LV_DIR_VER); lv_obj_set_scrollbar_mode(g_pick_list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(g_pick_list, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_add_event_cb(g_pick_list, zpick_scroll_cb, LV_EVENT_SCROLL, NULL);
    lv_obj_t *f = lv_obj_create(root); lv_obj_remove_style_all(f); kit_keep(f);   /* fade under "done" */
    lv_obj_set_size(f, 360, 90); lv_obj_align(f, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(f, TC(CANVAS), 0); lv_obj_set_style_bg_grad_color(f, TC(CANVAS), 0);
    lv_obj_set_style_bg_grad_dir(f, LV_GRAD_DIR_VER, 0); lv_obj_set_style_bg_opa(f, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_main_opa(f, LV_OPA_TRANSP, 0); lv_obj_set_style_bg_grad_opa(f, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_grad_stop(f, 150, 0);
    lv_obj_remove_flag(f, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *dn = lv_obj_create(root); lv_obj_remove_style_all(dn); kit_keep(dn);
    lv_obj_set_size(dn, LV_SIZE_CONTENT, LV_SIZE_CONTENT); lv_obj_align(dn, LV_ALIGN_TOP_MID, 0, 318);
    lv_obj_add_flag(dn, LV_OBJ_FLAG_CLICKABLE); lv_obj_set_ext_click_area(dn, 12); lv_obj_set_style_opa(dn, LV_OPA_50, LV_STATE_PRESSED);
    lv_obj_t *dl = lv_label_create(dn); lv_label_set_text(dl, "done");
    lv_obj_set_style_text_font(dl, zune_font(&font_zune_regular_16), 0); lv_obj_set_style_text_color(dl, TC(ACCENT_PRIMARY), 0);
    ui_on(dn, zpick_done_cb, LV_EVENT_CLICKED, NULL, "plpick.zune.done", UI_CORE);
    g_zpick_hdr = lv_obj_create(root); lv_obj_remove_style_all(g_zpick_hdr); kit_keep(g_zpick_hdr);   /* "add Holocene to..." */
    lv_obj_set_size(g_zpick_hdr, 240, 20); lv_obj_align(g_zpick_hdr, LV_ALIGN_TOP_MID, 0, 22);
    lv_obj_set_flex_flow(g_zpick_hdr, LV_FLEX_FLOW_ROW); lv_obj_set_style_pad_column(g_zpick_hdr, 8, 0);
    lv_obj_set_flex_align(g_zpick_hdr, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(g_zpick_hdr, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    g_zpick_hdr_l = lv_label_create(g_zpick_hdr); lv_label_set_long_mode(g_zpick_hdr_l, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(g_zpick_hdr_l, zune_font(&font_zune_regular_13), 0); lv_obj_set_style_text_color(g_zpick_hdr_l, TC(TEXT_PRIMARY), 0);
    zpick_reload();
}
void zpick_visit(void){ g_zpick_nd = 0; }                            /* a new add: nothing ticked from the last one */

/* ---- Now Playing hub (SCR_NPHUB): swipe-right page. Secondary actions for
 * the current track (play-mode & favorite live on the Now Playing screen). - */
__attribute__((unused)) static void hub_eq_cb(lv_event_t *e){ if(lv_event_get_code(e)==LV_EVENT_CLICKED){ screen_show(SCR_TUNE); } }

static void fsart_deferred_cb(lv_timer_t *t){ lv_timer_delete(t); ui_np_fsart_open(); }
static void hub_fsart_cb(lv_event_t *e){
    if(lv_event_get_code(e)!=LV_EVENT_CLICKED) return;
    if(th_disco_family() || th_null()){ disco_open_np_immersive(); return; }
    screen_show(SCR_NOWPLAYING);   /* fsart lives on the NP root -> make NP the active screen first */
    /* open after the NP slide-in settles so the transition can't stomp the overlay's z-order/opacity */
    lv_timer_t *t = lv_timer_create(fsart_deferred_cb, 360, NULL);
    lv_timer_set_repeat_count(t, 1);
}
static void hub_chapters_cb(lv_event_t *e){ if(lv_event_get_code(e)==LV_EVENT_CLICKED) chapters_open(); }

static void hub_eq_open_cb(lv_event_t *e){ if(lv_event_get_code(e)==LV_EVENT_CLICKED){ screen_show(SCR_EQ); } }   /* the round EQ */
void nphub_refresh(void){ build_menu(g_box_hub); }   /* called on each SCR_NPHUB show (screenmgr) */
void nphub_create(lv_obj_t *root){
    lv_obj_set_style_bg_color(root, TC(CANVAS), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    g_box_hub = lv_obj_create(root); lv_obj_remove_style_all(g_box_hub); lv_obj_set_size(g_box_hub, 360, 360);
    lv_obj_clear_flag(g_box_hub, LV_OBJ_FLAG_SCROLLABLE);
}
/* ---- tuning menu (SCR_TUNE): Play Mode + Equalizer cyclers --------------- */
static const char *const T_MODE[] = { "Sequential","Shuffle","Repeat One","Repeat All","Single" };
static const char *const T_EQ[]   = { "Off","Jazz","Rock","R&B","Hip-Hop","Pop","Dance","Classical","Retro","Sibilance 1","Sibilance 2",
                                      "USER1","USER2","USER3","USER4","USER5","USER6","USER7","USER8","USER9","USER10" };  /* 11..20 = user PEQ slots */

static lv_obj_t *g_mode_val, *g_eq_val;
static int g_tune_built = 0;

/* re-sync the Tune panel's Play Mode / EQ labels from live cfg. The panel is built once
 * but work_mode/eq_preset also change from the NP mode button, Settings and play paths,
 * so without this the labels go stale on re-open. Called on SCR_TUNE show. */
void tune_refresh(void){
    if(!g_tune_built) return;
    int m = simple_cfg_get_int("work_mode", 0); if(m<0||m>4) m=0;
    int q = simple_cfg_get_int("eq_preset", 0); if(q<0||q>20) q=0;
    if(g_mode_val) lv_label_set_text(g_mode_val, T_MODE[m]);
    if(g_eq_val)   lv_label_set_text(g_eq_val,   T_EQ[q]);
}

static void cyc_apply(const char *key, const char *const *opts, int n, void *valobj, int dir){
    int base = simple_cfg_get_int(key, 0); if(base < 0 || base >= n) base = 0;   /* normalize a corrupt stored value BEFORE stepping (no signed overflow, no OOB) */
    int v = base + dir;
    if(v<0) v=n-1; else if(v>=n) v=0;
    if(strcmp(key,"eq_preset")) simple_cfg_set_int(key, v);   /* EQ persists on a successful send via ui_eq_select */
    if(!strcmp(key,"work_mode")){ if(!ui_book_active()) ui_set_workmode(v); }   /* an active book stays in Single; cfg still updates so music later uses the chosen mode */
    else if(!strcmp(key,"eq_preset")){ if(ui_eq_select(v) < 0){ v = simple_cfg_get_int("eq_preset", 0); if(v<0||v>=n) v=0; } }   /* failed send -> label keeps the real preset (clamped: a corrupt stored value must not index opts[] OOB) */
    if(valobj) lv_label_set_text((lv_obj_t*)valobj, opts[v]);
}
static void mode_dir_cb(lv_event_t *e){ if(lv_event_get_code(e)==LV_EVENT_CLICKED) cyc_apply("work_mode", T_MODE, 5, g_mode_val, (int)(intptr_t)lv_event_get_user_data(e)); }
static void eq_dir_cb(lv_event_t *e){ if(lv_event_get_code(e)==LV_EVENT_CLICKED) cyc_apply("eq_preset", T_EQ, 21, g_eq_val, (int)(intptr_t)lv_event_get_user_data(e)); }
static void eq_custom_cb(lv_event_t *e){ if(lv_event_get_code(e)==LV_EVENT_CLICKED) screen_show(SCR_EQ); }

/* a "< label : value >" stepper row */
static lv_obj_t *cyc_row(lv_obj_t *parent, int y, const char *label,
                         lv_event_cb_t cb, lv_obj_t **out_val, const char *cur){
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, 300, 64); lv_obj_align(row, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lb = lv_label_create(row);
    lv_label_set_text(lb, label);
    lv_obj_align(lb, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_text_font(lb, TF(UI_14), 0);
    lv_obj_set_style_text_color(lb, TC(TEXT_MUTED), 0);

    lv_obj_t *val = lv_label_create(row);
    lv_label_set_text(val, cur);
    lv_obj_set_size(val,190,LV_SIZE_CONTENT);lv_label_set_long_mode(val,LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(val,LV_TEXT_ALIGN_CENTER,0);lv_obj_align(val, LV_ALIGN_TOP_MID, 0, 26);
    lv_obj_set_style_text_font(val, TF(UI_20), 0);
    lv_obj_set_style_text_color(val, TC(ACCENT_PRIMARY), 0);
    *out_val = val;

    lv_obj_t *l = lv_button_create(row); lv_obj_remove_style_all(l);
    lv_obj_set_size(l, 44, 44); lv_obj_align(l, LV_ALIGN_LEFT_MID, 4, 8);
    lv_obj_t *li=lv_label_create(l); lv_label_set_text(li, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(li, TC(TEXT_PRIMARY), 0); lv_obj_center(li);
    lv_obj_add_event_cb(l, cb, LV_EVENT_CLICKED, (void*)(intptr_t)-1);
    lv_obj_t *r = lv_button_create(row); lv_obj_remove_style_all(r);
    lv_obj_set_size(r, 44, 44); lv_obj_align(r, LV_ALIGN_RIGHT_MID, -4, 8);
    lv_obj_t *ri=lv_label_create(r); lv_label_set_text(ri, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_color(ri, TC(TEXT_PRIMARY), 0); lv_obj_center(ri);
    lv_obj_add_event_cb(r, cb, LV_EVENT_CLICKED, (void*)(intptr_t)1);
    return row;
}

void tune_create(lv_obj_t *root){
    panel_header(root, "Playback");
    int m = simple_cfg_get_int("work_mode", 0); if(m<0||m>4) m=0;
    int q = simple_cfg_get_int("eq_preset", 0); if(q<0||q>20) q=0;
    cyc_row(root, 84,  "PLAY MODE",  mode_dir_cb, &g_mode_val, T_MODE[m]);
    cyc_row(root, 158, "EQ PRESET",  eq_dir_cb,   &g_eq_val,   T_EQ[q]);
    g_tune_built = 1;

    lv_obj_t *cust = lv_button_create(root);
    lv_obj_remove_style_all(cust);
    lv_obj_set_size(cust, 180, 40); lv_obj_align(cust, LV_ALIGN_TOP_MID, 0, 236);
    lv_obj_set_style_radius(cust, 20, 0);
    lv_obj_set_style_bg_color(cust, TC(SURFACE), 0);
    lv_obj_set_style_bg_opa(cust, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(cust, TC(SURFACE_RAISED), LV_STATE_PRESSED);
    lv_obj_add_event_cb(cust, eq_custom_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *cl=lv_label_create(cust); lv_label_set_text(cl, "Equalizer");
    lv_obj_set_style_text_font(cl, TF(UI_16), 0);
    lv_obj_set_style_text_color(cl, TC(TEXT_PRIMARY), 0); lv_obj_center(cl);
}
