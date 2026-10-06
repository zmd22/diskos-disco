/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
/* Search, laid out for the round panel (fork rework):
 *   - the query field at the top,
 *   - a Search (check) button between the field and the keyboard: nothing is searched until it is pressed,
 *   - a big alphabetical keyboard with its own Backspace key (tap = one character, hold = clear),
 *   - the results on their own page with a back header that returns to the keyboard, query kept.
 * Tap a result to play it, hold it for the track menu. "123" swaps the letters for digits and punctuation.
 * Other text entry (Wi-Fi passwords, playlist names) keeps the full QWERTY keyboard in kbinput.c. */
#include "screens.h"
#include "theme_kit.h"
#include "theme.h"
#include "musicdb.h"
#include "curvelist.h"
#include "config.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MAX_RESULTS 80
#define KEY_H 46
#define ROW_P 50                                    /* key row pitch */
#define GRID_Y 116
#define RES_W 268
#define RES_ROW_H 56
/* keys per row, key width and gap: every key sits fully inside the circle (rows 2-4 narrow downwards) */
static const int KN[4] = { 7, 8, 7, 6 };
static const int KW[4] = { 42, 39, 40, 36 };
static const int KG[4] = {  4,  4,  4,  4 };
#define BKSP '\b'                                   /* the Backspace key's code in the key tables */

static lv_obj_t *g_root, *g_field, *g_query_lbl, *g_caret, *g_go, *g_go_lbl, *g_mode_btn, *g_mode_lbl;
static lv_obj_t *g_keys[4][8], *g_key_lbl[4][8];
static lv_obj_t *g_res_page, *g_res_list, *g_res_count;
static curvelist_t g_rcl;
static library_song_click_cb_t g_song_cb;
static char g_query[96];
static lv_obj_t *g_land;                                  /* Disco: the landing page over the keyboard */
static int g_land_on;
void search_landing_reset(void);
static int g_digits;                                       /* 0 = letters, 1 = digits & punctuation */

/* '_' is the space key, '\b' Backspace */
static const char *const LET[4] = { "ABCDEFG", "HIJKLMNO", "PQRSTUV", "WXYZ_\b" };
static const char *const DIG[4] = { "1234567", "890'&-.!", "?,:()/+", "#@\"*_\b" };

void search_set_song_click_cb(library_song_click_cb_t cb){ g_song_cb = cb; }
lv_obj_t *search_scroller(void){
    return (g_res_page && !lv_obj_has_flag(g_res_page, LV_OBJ_FLAG_HIDDEN)) ? g_res_list : NULL;
}

/* ---- results page --------------------------------------------------------------------------------- */
static void results_close(void){
    if(!g_res_page) return;
    lv_obj_add_flag(g_res_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clean(g_res_list);
}
int search_back_consumed(void){                    /* screen_back(): the results page closes first */
    if(!g_res_page || lv_obj_has_flag(g_res_page, LV_OBJ_FLAG_HIDDEN)) return 0;
    results_close();
    return 1;
}
int search_back_to_landing(void){                    /* screen_back(): Disco's keyboard returns to the landing page */
    if(!g_land || g_land_on) return 0;
    search_landing_reset();
    return 1;
}
static void res_back_cb(lv_event_t *e){ if(lv_event_get_code(e) == LV_EVENT_CLICKED) results_close(); }
static void result_cb(lv_event_t *e){
    if(lv_event_get_code(e) != LV_EVENT_SHORT_CLICKED) return;
    int id = (int)(uintptr_t)lv_event_get_user_data(e);
    if(!g_song_cb || !g_song_cb(id)) return;   /* refused / not sent: already toasted, stay on the results */
    screen_show(SCR_NOWPLAYING);
}
static void result_long_cb(lv_event_t *e){     /* hold a result: the track menu, like the Library */
    if(lv_event_get_code(e) != LV_EVENT_LONG_PRESSED) return;
    int id = (int)(uintptr_t)lv_event_get_user_data(e);
    char p[300]; if(mdb_song_path(id, p, sizeof p) == 1) songmenu_open_song(p);
}
static void res_note(const char *msg){
    lv_obj_t *l = lv_label_create(g_res_list);
    lv_label_set_text(l, msg);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, 240);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(l, TC(TEXT_MUTED), 0);
    lv_obj_set_style_text_font(l, TF(UI_16), 0);
}
static void res_row(const mdb_song_t *s){
    lv_obj_t *r = lv_obj_create(g_res_list);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, RES_W, RES_ROW_H);
    lv_obj_add_flag(r, LV_OBJ_FLAG_USER_1);                         /* curves with the circle */
    lv_obj_set_style_radius(r, TH_R_ROW, 0);
    lv_obj_set_style_bg_color(r, TC(SURFACE), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(r, LV_OPA_70, LV_STATE_PRESSED);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
    ui_on(r, result_cb, LV_EVENT_SHORT_CLICKED, (void*)(uintptr_t)s->id, "search.result", UI_CORE);
    ui_on(r, result_long_cb, LV_EVENT_LONG_PRESSED, (void*)(uintptr_t)s->id, "search.result.long", UI_CORE);
    lv_obj_t *t = lv_label_create(r);
    lv_label_set_text(t, s->title[0] ? s->title : "Untitled");
    lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(t, 12, 6); lv_obj_set_size(t, 244, 24);
    lv_obj_set_style_text_font(t, ui_font_user(18), 0);           /* CJK titles via the fallback chain */
    lv_obj_set_style_text_color(t, TC(TEXT_PRIMARY), 0);
    lv_obj_t *a = lv_label_create(r);
    char sub[2 * MDB_STR + 8];
    if(s->album[0]) snprintf(sub, sizeof sub, "%s \xC2\xB7 %s", s->artist, s->album); else snprintf(sub, sizeof sub, "%s", s->artist);
    lv_label_set_text(a, sub);
    lv_label_set_long_mode(a, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(a, 12, 31); lv_obj_set_size(a, 244, 20);
    lv_obj_set_style_text_font(a, ui_font_user(16), 0);
    lv_obj_set_style_text_color(a, TC(TEXT_LYRICS), 0);
}
/* an artist hit: one line with the microphone; a tap opens the artist's albums */
static char g_res_art[4][MDB_STR];
static void artist_cb(lv_event_t *e){
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if(i < 0 || i >= 4) return;
    library_open_artist(g_res_art[i]); screen_show(SCR_LIBRARY);
}
static void res_artist(int i){
    lv_obj_t *r = lv_obj_create(g_res_list); lv_obj_remove_style_all(r);
    lv_obj_set_size(r, RES_W, 44);
    lv_obj_add_flag(r, LV_OBJ_FLAG_USER_1);
    lv_obj_set_style_radius(r, th_disco() ? LV_RADIUS_CIRCLE : TH_R_ROW, 0);
    lv_obj_set_style_bg_color(r, TC(SURFACE), 0); lv_obj_set_style_bg_opa(r, LV_OPA_50, 0);
    lv_obj_set_style_bg_color(r, TC(SURFACE_RAISED), LV_STATE_PRESSED);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE); lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(r, artist_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    lv_obj_t *ic = lv_label_create(r); lv_label_set_text(ic, "\xEF\x84\xB0");          /* microphone */
    lv_obj_set_style_text_font(ic, TF(ICON_20), 0); lv_obj_set_style_text_color(ic, ui_current_accent(), 0);
    lv_obj_align(ic, LV_ALIGN_LEFT_MID, 14, 0);
    lv_obj_t *t = lv_label_create(r); lv_label_set_text(t, g_res_art[i]); lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
    lv_obj_set_width(t, RES_W - 90); lv_obj_set_style_text_font(t, ui_font_user(18), 0);
    lv_obj_set_style_text_color(t, TC(TEXT_PRIMARY), 0); lv_obj_align(t, LV_ALIGN_LEFT_MID, 42, 0);
    lv_obj_t *c = lv_label_create(r); lv_label_set_text(c, "Artist"); lv_obj_set_style_text_font(c, TF(UI_12), 0);
    lv_obj_set_style_text_color(c, TC(TEXT_SECONDARY), 0); lv_obj_align(c, LV_ALIGN_RIGHT_MID, -14, 0);
}
static void res_rule(void){                                       /* the line between artists and songs */
    lv_obj_t *l = lv_obj_create(g_res_list); lv_obj_remove_style_all(l);
    lv_obj_set_size(l, RES_W - 40, 1); lv_obj_set_style_bg_color(l, TC(TEXT_SECONDARY), 0); lv_obj_set_style_bg_opa(l, 90, 0);
    lv_obj_set_style_margin_top(l, 4, 0); lv_obj_set_style_margin_bottom(l, 4, 0);
}
static void results_open(void){
    lv_obj_clean(g_res_list);
    lv_obj_remove_flag(g_res_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(g_res_page);
    static const mdb_song_t *buf[MAX_RESULTS + 1];   /* +1 to detect "more than 80" vs "exactly 80" */
    int n = mdb_search(g_query, buf, MAX_RESULTS + 1);
    int shown = n > MAX_RESULTS ? MAX_RESULTS : (n > 0 ? n : 0);
    char cnt[64];
    if(n <= 0) snprintf(cnt, sizeof cnt, "\xE2\x80\x9C%s\xE2\x80\x9D", g_query);
    else snprintf(cnt, sizeof cnt, "%s%d for \xE2\x80\x9C%s\xE2\x80\x9D", n > MAX_RESULTS ? "First " : "", shown, g_query);
    lv_label_set_text(g_res_count, cnt);
    int na = mdb_search_artists(g_query, g_res_art, 4);            /* artists first, then the songs */
    for(int i = 0; i < na; i++) res_artist(i);
    if(na && shown) res_rule();
    if(n <= 0 && !na) res_note("No results\nGo back to change the search");
    for(int i = 0; i < shown; i++) res_row(buf[i]);
    if(n > MAX_RESULTS) res_note("Type more to narrow it down");
    lv_obj_update_layout(g_res_list); curvelist_update(&g_rcl);
    lv_obj_scroll_to_y(g_res_list, 0, LV_ANIM_OFF);
}

/* ---- query + keyboard ----------------------------------------------------------------------------- */
static void query_changed(void){
    lv_label_set_text(g_query_lbl, g_query[0] ? g_query : "Search");
    lv_obj_set_style_text_color(g_query_lbl, g_query[0] ? TC(TEXT_PRIMARY) : TC(TEXT_DISABLED), 0);
    lv_obj_update_layout(g_query_lbl);                          /* the caret sits right after the text */
    int w = g_query[0] ? lv_obj_get_width(g_query_lbl) : 0;
    lv_obj_set_x(g_caret, g_query[0] ? 47 + (w > 190 ? 190 : w) + 1 : 43);
    int on = g_query[0] != 0;                                   /* Search is live only with something typed */
    lv_obj_set_style_bg_color(g_go, on ? ui_current_accent() : TC(SURFACE), 0);
    lv_obj_set_style_text_color(g_go_lbl, on ? TC(ON_ACCENT) : TC(TEXT_DISABLED), 0);
}
static void bksp(int all){
    size_t n = strlen(g_query);
    if(!n) return;
    if(all) n = 0;
    else do { n--; } while(n > 0 && ((unsigned char)g_query[n] & 0xC0) == 0x80);   /* whole UTF-8 character */
    g_query[n] = 0;
    query_changed();
}
static void key_press(char c){
    if(c == BKSP){ bksp(0); return; }
    size_t n = strlen(g_query);
    if(n + 1 >= sizeof g_query) return;
    if(c == '_') c = ' ';
    if(c == ' ' && (n == 0 || g_query[n-1] == ' ')) return;    /* no leading or double spaces */
    g_query[n] = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c; g_query[n+1] = 0;
    query_changed();
}
/* ---- Disco: the landing page (Menu > Search): a big search row + recent searches; the keyboard goes full screen --- */
#define RECENT_N 3
/* one setting per search (settings values hold at most 47 bytes): search_r1 = the newest */
static void recent_get(char out[RECENT_N][96], int *n){
    *n = 0;
    for(int i = 0; i < RECENT_N; i++){
        char k[16]; snprintf(k, sizeof k, "search_r%d", i + 1);
        const char *v = cfg_get_str(k, "");
        if(v && v[0]) snprintf(out[(*n)++], 96, "%s", v);
    }
}
static void recent_add(const char *q){
    char nq[48]; size_t l = strlen(q); if(l > 47) l = 47;
    while(l > 0 && ((unsigned char)q[l] & 0xC0) == 0x80) l--;      /* never cut a character in half */
    memcpy(nq, q, l); nq[l] = 0;
    if(!nq[0]) return;
    char r[RECENT_N][96]; int n; recent_get(r, &n);
    if(n && !strcmp(r[0], nq)) return;                                 /* already the newest: no settings rewrite */
    const char *list[RECENT_N]; int m = 0;
    list[m++] = nq;
    for(int i = 0; i < n && m < RECENT_N; i++) if(strcasecmp(r[i], nq)) list[m++] = r[i];   /* the newest copy only */
    for(int i = 0; i < RECENT_N; i++){
        char k[16]; snprintf(k, sizeof k, "search_r%d", i + 1);
        cfg_set_str_deferred(k, i < m ? list[i] : "");
    }
    cfg_flush();
}
int search_landing_active(void){ return g_land && g_land_on; }
void disco_nav_refresh(void);
static void land_hide(void){ if(!g_land) return; g_land_on = 0; lv_obj_add_flag(g_land, LV_OBJ_FLAG_HIDDEN); disco_nav_refresh(); }
void search_landing_leave(void){ land_hide(); }
static void land_build(void);
static void land_go_cb(lv_event_t *e){ (void)e; land_hide(); }                            /* into the full-screen search */
static char g_recent[RECENT_N][96];                                 /* the full text (a row's label may end in an ellipsis) */
static void land_recent_cb(lv_event_t *e){
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if(i < 0 || i >= RECENT_N || !g_recent[i][0]) return;
    snprintf(g_query, sizeof g_query, "%s", g_recent[i]);
    land_hide(); query_changed(); results_open();                 /* straight to the results; back = the keyboard */
}
static void land_build(void){
    lv_obj_clean(g_land);
    lv_obj_set_style_bg_image_src(g_land, lv_obj_get_style_bg_image_src(g_root, 0), 0);   /* the same blurred cover */
    disco_title(g_land, "Search");
    lv_obj_t *big = disco_row(g_land, 74, 56, TH_IC_SEARCH, &font_theme_20, "Search library", land_go_cb, NULL);
    lv_obj_set_style_border_width(big, 1, 0); lv_obj_set_style_border_color(big, ui_current_accent(), 0); lv_obj_set_style_border_opa(big, 160, 0);
    char (*r)[96] = g_recent; int n; memset(g_recent, 0, sizeof g_recent); recent_get(g_recent, &n);
    lv_obj_t *cap = lv_label_create(g_land); lv_label_set_text(cap, n ? "Recent" : "Your recent searches show up here");
    lv_obj_set_style_text_font(cap, TF(UI_14), 0); lv_obj_set_style_text_color(cap, TC(TEXT_SECONDARY), 0);
    lv_obj_align(cap, LV_ALIGN_TOP_MID, 0, 146);
    for(int i = 0; i < n; i++){
        lv_obj_t *row = disco_row(g_land, 172 + i * 50, 44, LV_SYMBOL_REFRESH, NULL, r[i], land_recent_cb, (void *)(intptr_t)i);
        lv_obj_set_style_text_font(lv_obj_get_child(row, 1), ui_font_cjk(18), 0);
    }
}
void search_landing_reset(void){                                   /* disco.c: on entry to Search */
    if(!g_land) return;
    results_close(); g_query[0] = 0; query_changed();
    g_land_on = 1; land_build();
    lv_obj_remove_flag(g_land, LV_OBJ_FLAG_HIDDEN); lv_obj_move_foreground(g_land);
}
static void go_cb(lv_event_t *e){
    if(lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    size_t n = strlen(g_query);
    while(n && g_query[n-1] == ' ') g_query[--n] = 0;          /* a trailing space never matters */
    if(!n){ ui_toast("Type something to search for"); query_changed(); return; }
    query_changed();
    if(th_disco()) recent_add(g_query);
    results_open();
}
static void paint_keys(void){
    const char *const *set = g_digits ? DIG : LET;
    for(int r = 0; r < 4; r++){
        int cnt = (int)strlen(set[r]), kw = KW[r], g = KG[r];
        int x = 180 - (cnt * kw + (cnt - 1) * g) / 2;
        for(int c = 0; c < 8; c++){
            lv_obj_t *k = g_keys[r][c]; if(!k) continue;
            if(c >= cnt){ lv_obj_add_flag(k, LV_OBJ_FLAG_HIDDEN); continue; }
            char ch = set[r][c];
            lv_obj_remove_flag(k, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_user_data(k, (void*)(uintptr_t)(unsigned char)ch);
            lv_obj_set_pos(k, x, GRID_Y + r * ROW_P); x += kw + g;
            char s[2] = { ch, 0 };
            lv_obj_t *l = g_key_lbl[r][c];
            if(ch == '_'){ lv_label_set_text(l, "space"); lv_obj_set_style_text_font(l, TF(UI_12), 0); lv_obj_set_style_text_color(l, TC(TEXT_SECONDARY), 0); }
            else if(ch == BKSP){ lv_label_set_text(l, LV_SYMBOL_BACKSPACE); lv_obj_set_style_text_font(l, TF(UI_18), 0); lv_obj_set_style_text_color(l, TC(TEXT_SECONDARY), 0); }
            else { lv_label_set_text(l, s); lv_obj_set_style_text_font(l, TF(UI_24), 0); lv_obj_set_style_text_color(l, TC(TEXT_PRIMARY), 0); }
        }
    }
    lv_label_set_text(g_mode_lbl, g_digits ? "ABC" : "123");
}
static void keyev_cb(lv_event_t *e){
    lv_event_code_t c = lv_event_get_code(e);
    char ch = (char)(uintptr_t)lv_obj_get_user_data(lv_event_get_current_target(e));
    if(c == LV_EVENT_LONG_PRESSED){ if(ch == BKSP) bksp(1); return; }   /* hold Backspace: clear all */
    if(c == LV_EVENT_SHORT_CLICKED) key_press(ch);
}
static void mode_cb(lv_event_t *e){ (void)e; g_digits = !g_digits; paint_keys(); }
static void back_cb(lv_event_t *e){ (void)e; if(g_land && !g_land_on){ search_landing_reset(); return; } screen_back(); }

static void key_style(lv_obj_t *k, int radius){
    lv_obj_remove_style_all(k);
    lv_obj_set_style_radius(k, radius, 0);
    lv_obj_set_style_bg_color(k, TC(SURFACE), 0);
    lv_obj_set_style_bg_opa(k, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(k, TC(SURFACE_RAISED), LV_STATE_PRESSED);
}

void search_create(lv_obj_t *root){
    g_root = root;
    lv_obj_set_style_bg_color(root, TC(CANVAS), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    /* the field: magnifier, query, caret */
    g_field = lv_obj_create(root);
    lv_obj_remove_style_all(g_field);
    lv_obj_set_size(g_field, 248, 42);
    lv_obj_align(g_field, LV_ALIGN_TOP_MID, 0, 18);
    lv_obj_set_style_radius(g_field, TH_R_PILL, 0);
    lv_obj_set_style_bg_color(g_field, TC(SURFACE), 0);
    lv_obj_set_style_bg_opa(g_field, LV_OPA_COVER, 0);
    lv_obj_clear_flag(g_field, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *mg = lv_label_create(g_field);
    lv_label_set_text(mg, TH_IC_SEARCH);
    lv_obj_set_style_text_font(mg, &font_theme_20, 0);
    lv_obj_set_style_text_color(mg, TC(TEXT_SECONDARY), 0);
    lv_obj_align(mg, LV_ALIGN_LEFT_MID, 14, 0);
    g_query_lbl = lv_label_create(g_field);
    lv_label_set_long_mode(g_query_lbl, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_max_width(g_query_lbl, 190, 0);
    lv_obj_set_style_text_font(g_query_lbl, ui_font_cjk(18), 0);
    lv_obj_align(g_query_lbl, LV_ALIGN_LEFT_MID, 47, 0);   /* clear of the caret when empty */
    g_caret = lv_obj_create(g_field);
    lv_obj_remove_style_all(g_caret);
    lv_obj_set_size(g_caret, 2, 20);
    lv_obj_set_style_bg_color(g_caret, TC(ACCENT_PRIMARY), 0);
    lv_obj_set_style_bg_opa(g_caret, LV_OPA_COVER, 0);
    lv_obj_align(g_caret, LV_ALIGN_LEFT_MID, 42, 0);
    /* Search: the only thing that runs a search */
    g_go = lv_button_create(root);
    key_style(g_go, TH_R_PILL);
    lv_obj_set_size(g_go, 150, 40);
    lv_obj_align(g_go, LV_ALIGN_TOP_MID, 0, 68);
    lv_obj_set_ext_click_area(g_go, 4);
    ui_on(g_go, go_cb, LV_EVENT_CLICKED, NULL, "search.go", UI_CORE);
    g_go_lbl = lv_label_create(g_go);
    lv_label_set_text(g_go_lbl, LV_SYMBOL_OK "  Search");
    lv_obj_set_style_text_font(g_go_lbl, TF(UI_18), 0);
    lv_obj_center(g_go_lbl);
    /* the keyboard: big keys in the wide middle of the circle */
    for(int r = 0; r < 4; r++) for(int c = 0; c < KN[r]; c++){
        lv_obj_t *k = lv_button_create(root);
        key_style(k, 12);
        lv_obj_set_size(k, KW[r], KEY_H);
        lv_obj_add_event_cb(k, keyev_cb, LV_EVENT_SHORT_CLICKED, NULL);
        lv_obj_add_event_cb(k, keyev_cb, LV_EVENT_LONG_PRESSED, NULL);
        g_keys[r][c] = k;
        g_key_lbl[r][c] = lv_label_create(k);
        lv_obj_center(g_key_lbl[r][c]);
    }
    /* bottom: back, and the letters/digits switch */
    lv_obj_t *bk = lv_button_create(root);
    lv_obj_remove_style_all(bk);
    lv_obj_set_size(bk, 40, 28); lv_obj_align(bk, LV_ALIGN_TOP_MID, -28, 318);
    lv_obj_set_ext_click_area(bk, 8);
    lv_obj_add_event_cb(bk, back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bkl = lv_label_create(bk); lv_label_set_text(bkl, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_font(bkl, TF(UI_16), 0);
    lv_obj_set_style_text_color(bkl, TC(TEXT_SECONDARY), 0); lv_obj_center(bkl);
    g_mode_btn = lv_button_create(root);
    key_style(g_mode_btn, 14);
    lv_obj_set_size(g_mode_btn, 48, 28); lv_obj_align(g_mode_btn, LV_ALIGN_TOP_MID, 26, 318);
    lv_obj_set_ext_click_area(g_mode_btn, 6);
    lv_obj_add_event_cb(g_mode_btn, mode_cb, LV_EVENT_CLICKED, NULL);
    g_mode_lbl = lv_label_create(g_mode_btn);
    lv_obj_set_style_text_font(g_mode_lbl, TF(UI_14), 0);
    lv_obj_set_style_text_color(g_mode_lbl, TC(TEXT_SECONDARY), 0);
    lv_obj_center(g_mode_lbl);
    /* the results page: covers the keyboard; its back header returns to it with the query kept */
    g_res_page = lv_obj_create(root);
    lv_obj_remove_style_all(g_res_page);
    lv_obj_set_size(g_res_page, 360, 360);
    lv_obj_set_style_bg_color(g_res_page, TC(CANVAS), 0);
    lv_obj_set_style_bg_opa(g_res_page, LV_OPA_COVER, 0);
    lv_obj_clear_flag(g_res_page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_res_page, LV_OBJ_FLAG_CLICKABLE);
    ui_header_cb(g_res_page, "Results", res_back_cb);
    g_res_count = lv_label_create(g_res_page);
    lv_label_set_long_mode(g_res_count, LV_LABEL_LONG_DOT);
    lv_obj_set_width(g_res_count, 220);
    lv_obj_set_style_text_align(g_res_count, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(g_res_count, ui_font_cjk(14), 0);
    lv_obj_set_style_text_color(g_res_count, TC(TEXT_MUTED), 0);
    lv_obj_align(g_res_count, LV_ALIGN_TOP_MID, 0, 66);
    g_res_list = lv_obj_create(g_res_page);
    lv_obj_remove_style_all(g_res_list);
    lv_obj_set_pos(g_res_list, (360 - RES_W) / 2, 88); lv_obj_set_size(g_res_list, RES_W, 272);
    lv_obj_set_style_pad_bottom(g_res_list, 40, 0);   /* the last row clears the round bottom bezel */
    lv_obj_set_flex_flow(g_res_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(g_res_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(g_res_list, 4, 0);
    lv_obj_set_scroll_dir(g_res_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(g_res_list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(g_res_list, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    curvelist_attach(&g_rcl, g_res_list, g_res_page, RES_W);
    lv_obj_add_flag(g_res_page, LV_OBJ_FLAG_HIDDEN);
    g_digits = 0; paint_keys();
    g_query[0] = 0; query_changed();
    if(th_disco()){
        g_land = lv_obj_create(root); lv_obj_remove_style_all(g_land);
        lv_obj_set_size(g_land, 360, 360);
        lv_obj_set_style_bg_color(g_land, TC(CANVAS), 0); lv_obj_set_style_bg_opa(g_land, LV_OPA_COVER, 0);
        lv_obj_add_flag(g_land, LV_OBJ_FLAG_CLICKABLE); lv_obj_clear_flag(g_land, LV_OBJ_FLAG_SCROLLABLE);
        g_land_on = 1; land_build();
    }
}
