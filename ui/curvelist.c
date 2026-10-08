/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "curvelist.h"
#include "theme.h"
#include "braun.h"
int disco_clear_left(int y1, int y2); int disco_clear_right(int y1, int y2);   /* disco.c: rows clear of the Disco menu picker (0 = no limit) */
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#define NB 5
/* Braun: straight rows, no pill, a thin rule; dark / grey text; the playing or focused row (shown in the
 * accent colour, or highlighted, by the list itself) becomes the single orange dot + dark type. Rows of long
 * lists are reused as they scroll, so this runs on every scroll step - only for the visible rows. */
static int saturated(lv_color_t c){
    int mx = c.red, mn = c.red;
    if(c.green > mx) mx = c.green;
    if(c.blue > mx) mx = c.blue;
    if(c.green < mn) mn = c.green;
    if(c.blue < mn) mn = c.blue;
    return mx - mn > 48;
}
void curvelist_braun_row(lv_obj_t *r){
    int focus = 0, fresh = 0;          /* fresh: the list (re)coloured this row since our last pass */
    lv_color_t bg = lv_obj_get_style_bg_color(r, 0);
    uint32_t bgv = lv_color_to_u32(bg) & 0xFFFFFF;
    if(lv_obj_get_style_bg_opa(r, 0) > LV_OPA_50 && (bgv == TH_SURF2 || saturated(bg))) focus = 1;   /* lifted (the list's own "current") */
    if(lv_obj_get_style_bg_opa(r, 0) != LV_OPA_TRANSP || lv_obj_get_style_border_width(r, 0) != 1){   /* first time for this row */
        lv_obj_set_style_bg_opa(r, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(r, TC(SURFACE), LV_STATE_PRESSED); lv_obj_set_style_bg_opa(r, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_set_style_radius(r, 6, 0);
        lv_obj_set_style_border_side(r, LV_BORDER_SIDE_BOTTOM, 0); lv_obj_set_style_border_width(r, 1, 0);
        lv_obj_set_style_border_color(r, TC(BORDER), 0);
    }
    lv_obj_t *dot = NULL;
    lv_obj_t *labels[16]; int nl = 0;                               /* the row's labels, one container deep */
    for(uint32_t k = 0; k < lv_obj_get_child_count(r) && nl < 16; k++){
        lv_obj_t *ch = lv_obj_get_child(r, k);
        if(lv_obj_has_flag(ch, LV_OBJ_FLAG_USER_3)){ dot = ch; continue; }
        if(lv_obj_check_type(ch, &lv_label_class)){ labels[nl++] = ch; continue; }
        if(lv_obj_has_flag(ch, LV_OBJ_FLAG_USER_4)) continue;       /* a key styled by its list (e.g. Play All): leave it */
        for(uint32_t j = 0; j < lv_obj_get_child_count(ch) && nl < 16; j++){ lv_obj_t *g = lv_obj_get_child(ch, j); if(lv_obj_check_type(g, &lv_label_class)) labels[nl++] = g; }
    }
    int seen_title = 0;
    for(int k = 0; k < nl; k++){
        lv_obj_t *ch = labels[k];
        const char *t = lv_label_get_text(ch);
        int icon = t && (unsigned char)t[0] == 0xEF;                 /* a symbol glyph (folder, chevron...) */
        lv_color_t col = lv_obj_get_style_text_color(ch, 0);
        uint32_t cv = lv_color_to_u32(col) & 0xFFFFFF;
        if(!icon && cv != BR_TXT && cv != BR_TXT2 && !(saturated(col) && seen_title)) fresh = 1;   /* the list recoloured this row's text (icons and status colours don't count) */
        if(!icon && saturated(col) && !seen_title){ focus = 1;         /* only the title's colour marks the current row: */
            lv_obj_set_style_text_color(ch, TC(TEXT_PRIMARY), 0); } /* the dot says it; the title goes dark */
        if(!icon) seen_title = 1;
        int white = (col.red + col.green + col.blue) / 3 > 235;       /* primary text -> black; light greys -> Braun grey */
        uint32_t want = icon ? BR_TXT2 : (white || saturated(col)) ? BR_TXT : BR_TXT2;
        if(cv != BR_TXT && cv != BR_TXT2 && !saturated(col)) lv_obj_set_style_text_color(ch, theme_color_from_rgb(want), 0);   /* leftover greys only: coloured status text keeps its meaning */
    }
    if(!dot){
        dot = lv_obj_create(r); lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, 8, 8); lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(dot, TC(ACCENT_PRIMARY), 0); lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_add_flag(dot, LV_OBJ_FLAG_USER_3 | LV_OBJ_FLAG_IGNORE_LAYOUT | LV_OBJ_FLAG_HIDDEN); lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);   /* hidden until a row is current */
        lv_obj_align(dot, LV_ALIGN_LEFT_MID, 0, 0);
    }
    if((int32_t)lv_obj_get_index(dot) != (int32_t)lv_obj_get_child_count(r) - 1) lv_obj_move_to_index(dot, -1);   /* the dot stays last: the row's own children keep their order */
    if(!fresh && !focus && lv_obj_get_style_bg_opa(r, 0) == LV_OPA_TRANSP) return;   /* already ours, unchanged: keep the dot as it was */
    if(focus != !lv_obj_has_flag(dot, LV_OBJ_FLAG_HIDDEN)){ if(focus) lv_obj_remove_flag(dot, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN); }
}
#define WATCH_MAX 24
static lv_obj_t *g_watch[WATCH_MAX]; static int g_nwatch; static lv_timer_t *g_watch_tmr;
static void braun_pass(lv_obj_t *list){
    uint32_t n = lv_obj_get_child_count(list);
    for(uint32_t i = 0; i < n; i++){
        lv_obj_t *r = lv_obj_get_child(list, i);
        if(lv_obj_check_type(r, &lv_label_class) || lv_obj_has_flag(r, LV_OBJ_FLAG_IGNORE_LAYOUT) || lv_obj_has_flag(r, LV_OBJ_FLAG_HIDDEN)) continue;
        lv_area_t a; lv_obj_get_coords(r, &a);
        if(a.y2 < -40 || a.y1 > 400) continue;
        curvelist_braun_row(r);
    }
}
static void watch_cb(lv_timer_t *t){
    (void)t;
    for(int i = 0; i < g_nwatch; i++) if(g_watch[i] && lv_obj_is_visible(g_watch[i])) braun_pass(g_watch[i]);
}
static void watch_del_cb(lv_event_t *e){ lv_obj_t *o = lv_event_get_target(e); for(int i = 0; i < g_nwatch; i++) if(g_watch[i] == o) g_watch[i] = NULL; }
/* Braun: keep this list's visible rows styled, whenever and however the list fills them */
void curvelist_braun_watch(lv_obj_t *list){
    if(!th_braun() || !list) return;
    for(int i = 0; i < g_nwatch; i++) if(g_watch[i] == list) return;
    int slot = -1; for(int i = 0; i < g_nwatch; i++) if(!g_watch[i]){ slot = i; break; }
    if(slot < 0){ if(g_nwatch >= WATCH_MAX) return; slot = g_nwatch++; }
    g_watch[slot] = list;
    lv_obj_add_event_cb(list, watch_del_cb, LV_EVENT_DELETE, NULL);
    if(!g_watch_tmr) g_watch_tmr = lv_timer_create(watch_cb, 250, NULL);
}
static void curve(curvelist_t *c){
    if(th_braun()){                                               /* Braun: straight rows, restyled */
        uint32_t n = lv_obj_get_child_count(c->list);
        for(uint32_t i = 0; i < n; i++){
            lv_obj_t *r = lv_obj_get_child(c->list, i);
            if(!lv_obj_has_flag(r, LV_OBJ_FLAG_USER_1)) continue;
            lv_area_t a; lv_obj_get_coords(r, &a);
            if(a.y2 < -40 || a.y1 > 400) continue;
            if(lv_obj_get_width(r) != c->full_w) lv_obj_set_width(r, c->full_w);
            curvelist_braun_row(r);
        }
        return;
    }
    uint32_t n = lv_obj_get_child_count(c->list);
    for(uint32_t i = 0; i < n; i++){
        lv_obj_t *r = lv_obj_get_child(c->list, i);
        if(!lv_obj_has_flag(r, LV_OBJ_FLAG_USER_1)) continue;
        lv_area_t a; lv_obj_get_coords(r, &a);
        if(a.y2 < -40 || a.y1 > 400) continue;                      /* off-screen: nothing to do */
        int dy = abs((a.y1 + a.y2) / 2 - 180) + lv_area_get_height(&a) / 2;
        int half = dy < 176 ? (int)sqrtf((float)(180 * 180 - dy * dy)) - 10 : 0;
        int w = 2 * half;
        if(w > c->full_w) w = c->full_w;
        if(w < c->full_w - (NB - 1) * 20) w = c->full_w - (NB - 1) * 20;
        int shift = 0, L = disco_clear_left(a.y1, a.y2);
        { int R = disco_clear_right(a.y1, a.y2);                              /* Disco: clear of the closed sliver */
          if(R && 180 + w / 2 > R){ int lft = 180 - w / 2; w = R - lft; shift = (lft + R) / 2 - 180; } }
        if(L && 180 - w / 2 < L){ int right = 180 + w / 2; int nw = right - L; if(nw < 120) nw = 120; shift = L + nw / 2 - 180; w = nw; }   /* Disco: start right of the picker (never narrower than 120) */   /* Disco: clear of the picker */
        intptr_t key = ((intptr_t)w << 16 | (unsigned)(shift + 32768)) + 1;
        if((intptr_t)lv_obj_get_user_data(r) == key) continue;      /* unchanged: no work */
        lv_obj_set_user_data(r, (void *)key);
        int d = c->full_w - w;
        lv_obj_set_width(r, w);
        lv_obj_set_style_translate_x(r, shift, 0);
        for(uint32_t k = 0; k < lv_obj_get_child_count(r); k++){       /* keep text inside the row */
            lv_obj_t *ch = lv_obj_get_child(r, k);
            if(!lv_obj_check_type(ch, &lv_label_class)) continue;
            intptr_t orig = (intptr_t)lv_obj_get_user_data(ch);
            if(!orig){ orig = ((intptr_t)lv_obj_get_x(ch) << 16) | (lv_obj_get_width(ch) & 0xFFFF); lv_obj_set_user_data(ch, (void *)orig); }
            int ox = (int)(orig >> 16), ow = (int)(orig & 0xFFFF);
            if(ox < 130) continue;
            if(shift && lv_obj_get_style_text_align(ch, 0) == LV_TEXT_ALIGN_RIGHT && ow - d >= 40){   /* Disco: shrink it in place, */
                lv_obj_set_x(ch, ox); lv_obj_set_width(ch, ow - d);                                     /* so it never runs into the title */
            } else { lv_obj_set_x(ch, ox - d); if(shift || lv_obj_get_width(ch) < ow) lv_obj_set_width(ch, ow); }   /* right-hand value slides in; a shrunk one gets its width back */
        }
    }
}
static void dots(curvelist_t *c){
    if(th_braun()) return;                                       /* Braun: no position dots */
    int32_t sy = lv_obj_get_scroll_y(c->list), sb = lv_obj_get_scroll_bottom(c->list), tot = sy + sb;
    int idx = (tot <= 24) ? -1 : (int)((sy * 10 + tot / 2) / tot);
    if(idx > 10) idx = 10;
    if(idx == c->cur) return;
    c->cur = idx;
    for(int k = 0; k < 11; k++){
        if(idx < 0){ lv_obj_add_flag(c->dot[k], LV_OBJ_FLAG_HIDDEN); continue; }
        lv_obj_remove_flag(c->dot[k], LV_OBJ_FLAG_HIDDEN);
        int on = (k == idx), sz = on ? 8 : 5;
        float a = (212.0f - 6.4f * k) * 3.14159265f / 180.0f;         /* top to bottom along the left rim */
        lv_obj_set_size(c->dot[k], sz, sz);
        lv_obj_set_pos(c->dot[k], (int)(180 + 168 * cosf(a)) - sz / 2, (int)(180 + 168 * sinf(a)) - sz / 2);
        lv_obj_set_style_bg_color(c->dot[k], on ? TC(ACCENT_PRIMARY) : TC(PAGE_DOT_QUIET), 0);
    }
}
static void scroll_cb(lv_event_t *e){ curvelist_t *c = lv_event_get_user_data(e); curve(c); dots(c); }
void curvelist_attach(curvelist_t *c, lv_obj_t *list, lv_obj_t *root, int full_w){
    c->list = list; c->full_w = full_w; c->cur = -2;
    if(th_braun()){                                                /* Braun: the grille, and the list on the lower segment */
        br_face(root);
        lv_obj_update_layout(list);
        int y = lv_obj_get_y(list) - 6; if(y < 40) y = 40;
        lv_obj_t *seg = br_segment(root, y);
        lv_obj_move_to_index(seg, 1);                            /* above the grille, below the list and the header */
        curvelist_braun_watch(list);
    }
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);   /* narrowed rows stay centred */
    lv_obj_add_event_cb(list, scroll_cb, LV_EVENT_SCROLL, c);
    for(int k = 0; k < 11; k++){
        c->dot[k] = lv_obj_create(root);
        lv_obj_remove_style_all(c->dot[k]);
        lv_obj_set_style_radius(c->dot[k], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(c->dot[k], LV_OPA_COVER, 0);
        lv_obj_clear_flag(c->dot[k], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(c->dot[k], LV_OBJ_FLAG_HIDDEN | LV_OBJ_FLAG_IGNORE_LAYOUT);
    }
}
void curvelist_update(curvelist_t *c){
    if(!c->list) return;
    lv_obj_update_layout(c->list);
    curve(c); c->cur = -2; dots(c);
}
