#include "simple_theme.h"
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "curvelist.h"
#include "null_theme.h"
#include "theme.h"
#include "braun.h"
#include "config.h"
int disco_clear_left(int y1, int y2); int disco_clear_right(int y1, int y2); int disco_mirror(void);   /* disco.c: rows clear of the Disco menu picker (0 = no limit) */
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
    /* Dashboard sections use the shared scroller but are not list pills. */
    if(th_braun2()&&lv_obj_get_height(r)>100)return;
    int focus = 0, fresh = 0;          /* fresh: the list (re)coloured this row since our last pass */
    lv_color_t bg = lv_obj_get_style_bg_color(r, 0);
    uint32_t bgv = lv_color_to_u32(bg) & 0xFFFFFF;
    if(lv_obj_get_style_bg_opa(r, 0) > LV_OPA_50 && (bgv == TH_SURF2 || saturated(bg))) focus = 1;   /* lifted (the list's own "current") */
    if(lv_obj_get_style_bg_opa(r, 0) != (th_braun2()?LV_OPA_COVER:LV_OPA_TRANSP) || lv_obj_get_style_border_width(r, 0) != (th_braun2()?0:1)){   /* first time for this row */
        lv_obj_set_style_bg_opa(r, th_braun2()?LV_OPA_COVER:LV_OPA_TRANSP, 0);
        if(th_braun2())lv_obj_set_style_bg_color(r,TC(SURFACE_STRONG),0);
        lv_obj_set_style_bg_color(r, TC(SURFACE), LV_STATE_PRESSED); lv_obj_set_style_bg_opa(r, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_set_style_radius(r, th_braun2()?28:6, 0);
        lv_obj_set_style_border_side(r, LV_BORDER_SIDE_BOTTOM, 0); lv_obj_set_style_border_width(r, th_braun2()?0:1, 0);
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
    if(!th_braun_family() || !list) return;
    for(int i = 0; i < g_nwatch; i++) if(g_watch[i] == list) return;
    int slot = -1; for(int i = 0; i < g_nwatch; i++) if(!g_watch[i]){ slot = i; break; }
    if(slot < 0){ if(g_nwatch >= WATCH_MAX) return; slot = g_nwatch++; }
    g_watch[slot] = list;
    lv_obj_add_event_cb(list, watch_del_cb, LV_EVENT_DELETE, NULL);
    if(!g_watch_tmr) g_watch_tmr = lv_timer_create(watch_cb, 250, NULL);
}
/* Integer row positions revisit the same circle spans throughout a scroll.
 * Retain the original sqrtf/truncation exactly; 540 bytes and no per-frame heap. */
int curvelist_circle_half(int radius, int distance){
    static uint8_t spans[3][180];static unsigned ready;
    if(distance<0||distance>=radius)return 0;
    if(radius!=150&&radius!=172&&radius!=180)return (int)sqrtf((float)((int64_t)radius*radius-(int64_t)distance*distance));
    int slot=radius==150?0:radius==172?1:2;
    if(!(ready&(1u<<slot))){
        for(int d=0;d<radius;d++)spans[slot][d]=(uint8_t)sqrtf((float)(radius*radius-d*d));
        ready|=1u<<slot;
    }
    return spans[slot][distance];
}
/* Disco > Scroll: 0 Classic (the circle), 1 Straight (Braun-like, full width), 2 Disco (the rows' right ends
 * start on a circle round the navigation hub's centre, so they swing round that fixed point like spokes). */
int curvelist_disco_scroll(void){ return th_null() ? simple_cfg_get_int("null_scroll", 0) : th_disco_family() ? simple_cfg_get_int("disco_scroll", 0) : 0; }
int curvelist_disco_geom(int mode, int y1, int y2, int full_w, int *w, int *shift){
    if(mode != 1 && mode != 2) return 0;
    int left = 180 - full_w / 2, right = 180 + full_w / 2;
    if(mode == 2){                                                   /* a CD's rim round the hub's centre (286,180), radius 150: */
        int cy = (y1 + y2) / 2, dy = abs(cy - 180), far = dy + (y2 - y1) / 2;   /* the middle row sits furthest out, */
        int off = 150 - (dy < 150 ? curvelist_circle_half(150,dy) : 150);   /* rows above and below swing in */
        int edge = far < 172 ? curvelist_circle_half(172,far) : 0;    /* and stay inside the screen circle */
        left = 180 - full_w / 2 + 2 + off; right = 180 + full_w / 2 - 10;   /* times stay in one straight column; the list clips left of its own edge */
        if(left < 180 - edge + 8) left = 180 - edge + 8;
    }
    int R;                                                           /* clear of the closed sliver */
    if(disco_mirror()){ int L = disco_clear_left(y1, y2); R = L ? 360 - L : 0; }   /* Left: work it out as if on the right, mirror below */
    else R = disco_clear_right(y1, y2);
    if(R && right > R) right = R;
    int minw = full_w - 82;                                          /* as Classic: never narrower (a 120 px row reads as a button) */
    if(right - left < minw){ int mid = (left + right) / 2; left = mid - minw / 2; right = left + minw; if(R && right > R){ right = R; left = R - minw; } }
    if(disco_mirror()){ int l = 360 - right; right = 360 - left; left = l; }   /* Left: the CD rim and the sliver swap sides */
    *w = right - left; *shift = (left + right) / 2 - 180;
    return 1;
}
/* ---- Wheel (List motion 4, Disco / Squash / Null): the row in the middle is the current one - large, in the main
 * text colour, no pill; the rows next to it a size smaller and grey; the outer ones smallest and dim, sinking under
 * the soft edges. Rows step between three sizes (no per-frame scaling); spacing tightens away from the middle. */
#define WHEEL_MODE 3
int curvelist_wheel(void){ return curvelist_disco_scroll() == WHEEL_MODE && (th_disco_family() || th_null()) && !th_zune(); }
static lv_color_t g_wheel_icon;   /* Squash: the colour icons take in this row (its orange, faded like the text) */
static int is_icon(lv_obj_t *l){ const char *t = lv_label_get_text(l); return t && (unsigned char)t[0] >= 0xE0 && strcmp(t, LV_SYMBOL_RIGHT) && strcmp(t, LV_SYMBOL_LEFT); }   /* a symbol glyph, not the › chevron */
static void wheel_paint(lv_obj_t *o, lv_color_t c){
    for(uint32_t i = 0; i < lv_obj_get_child_count(o); i++){
        lv_obj_t *ch = lv_obj_get_child(o, i);
        if(lv_obj_check_type(ch, &lv_button_class)) continue;   /* a button inside a row keeps its own colours */
        if(lv_obj_check_type(ch, &lv_label_class)) lv_obj_set_style_text_color(ch, th_simple() && is_icon(ch) ? g_wheel_icon : c, 0);
        else if(lv_obj_check_type(ch, &lv_image_class)) lv_obj_set_style_image_opa(ch, c.red == TC(TEXT_PRIMARY).red ? LV_OPA_COVER : LV_OPA_60, 0);
        else wheel_paint(ch, c);
    }
}
/* The current row is pinned to the middle of the screen. The list starts one row above it (the one row that fits
 * under the header) and runs down to the bottom edge; rows snap so one always rests exactly in the middle. */
#define WHEEL_FADE_TOP 26   /* Wheel: the soft top edge, taken from the header's space above the row above */
#define WHEEL_FADE_BOT 52   /* Wheel: the soft bottom edge, the footer's space at the foot of the screen */
#define WHEEL_ABOVE 46      /* room for the one row shown above the current one */
/* Lists that stay as they are in Wheel mode (the Queue: its rows are handles to drag, its current song a pill). */
static lv_obj_t *g_wheel_off[4];
void curvelist_wheel_exclude(lv_obj_t *list){
    for(int i = 0; i < 4; i++) if(g_wheel_off[i] == list) return;
    for(int i = 0; i < 4; i++) if(!g_wheel_off[i] || !lv_obj_is_valid(g_wheel_off[i])){ g_wheel_off[i] = list; return; }
}
/* Excluded too: a list that does not start right under the header (Bluetooth, Wi-Fi: a status and controls above) -
 * it has no room to pin its current row to the middle without covering them, so it keeps its usual motion. */
static int wheel_excluded(lv_obj_t *list){
    for(int i = 0; i < 4; i++) if(g_wheel_off[i] == list) return 1;
    return lv_obj_get_y(list) > 70;
}
void curvelist_wheel_list(lv_obj_t *list, int row_h){
    int top = 50, h = 352 - top;   /* the list starts right under the header line: more shows above the current row */
    if(lv_obj_get_y(list) != top || lv_obj_get_height(list) != h){ lv_obj_set_y(list, top); lv_obj_set_height(list, h); }
    int pt = 180 - row_h / 2 - top, pb = h - pt - row_h; if(pb < 0) pb = 0;
    /* a lead in the space above the first row (USER_3, not a row): it takes that space, the padding gives it back */
    lv_obj_t *lead = lv_obj_get_child_count(list) ? lv_obj_get_child(list, 0) : NULL;
    if(lead && lv_obj_has_flag(lead, LV_OBJ_FLAG_USER_3) && !lv_obj_has_flag(lead, LV_OBJ_FLAG_USER_1)){
        int lh = pt - lv_obj_get_style_pad_row(list, 0); if(lh < 0) lh = 0;
        if(lv_obj_get_height(lead) != lh) lv_obj_set_height(lead, lh);
        pt = 0;
    }
    if(lv_obj_get_style_pad_top(list, 0) != pt || lv_obj_get_style_pad_bottom(list, 0) != pb){
        lv_obj_set_style_pad_top(list, pt, 0); lv_obj_set_style_pad_bottom(list, pb, 0); }
    lv_obj_set_scroll_snap_y(list, LV_SCROLL_SNAP_START);
}
/* The wheel's size profile, in 1/256: 110 % in the middle, easing down to 84 % one row away and 68 % two rows away,
 * and its running integral (how far a row's centre sits from the middle when the rows in between touch). */
static int wheel_scale(int u){   /* u: distance in hundredths of a row */
    if(u <= 100) return 282 - 66 * u / 100;          /* 282 -> 216 */
    if(u <= 200) return 216 - 42 * (u - 100) / 100;  /* 216 -> 174 */
    return 174;
}
static int wheel_reach(int u){   /* integral of the profile over 0..u, in hundredths of a row (x 1/256) */
    long r = 0; int seg;
    seg = u < 100 ? u : 100; r += (long)seg * (282 + wheel_scale(seg)) / 2;
    if(u > 100){ seg = (u < 200 ? u : 200) - 100; r += (long)seg * (216 + wheel_scale(100 + seg)) / 2; }
    if(u > 200) r += (long)(u - 200) * 174;
    return (int)(r / 256);
}
void curvelist_wheel_row(lv_obj_t *r, lv_obj_t *list){
    lv_area_t a; lv_obj_get_coords(r, &a);
    int h = lv_area_get_height(&a); if(h < 1) return;
    lv_area_t ra; lv_obj_get_coords(lv_obj_get_parent(list), &ra);   /* its screen, which may be sliding in */
    int centred = lv_obj_has_flag(r, LV_OBJ_FLAG_USER_2);             /* centred content (the album row) */
    int cy = (a.y1 + a.y2) / 2 - lv_obj_get_style_translate_y(r, 0) - ra.y1;   /* where the row sits on its own */
    int pitch = centred ? h : h + lv_obj_get_style_pad_row(list, 0);   /* one row: its height and the gap to the next */
    int dq = (cy - 180) * 100 / pitch, u = abs(dq);                   /* distance from the middle, hundredths of a row */
    /* Size, colour and place change smoothly with the distance: a few steps per row, so a scroll reads as one
     * continuous roll. The rows pack round the large middle one (no gaps, no overlap) and their starts follow the
     * round edge a little, like text on a drum. */
    intptr_t key = ((intptr_t)(dq / 6) & 0xFFFF) + 0x5A000000;
    int sc = wheel_scale(u);
    int fade = u * 120 / 100; if(fade > 205) fade = 205;              /* 0 in the middle .. 205 at the outside */
    lv_color_t want = fade < 8 ? TC(TEXT_PRIMARY) : theme_color_mix(TC(CANVAS), TC(TEXT_PRIMARY), (uint8_t)fade);
    lv_obj_t *probe = NULL;                                           /* a theme pass may have restyled the row since */
    for(uint32_t i = 0; i < lv_obj_get_child_count(r) && !probe; i++)
        if(lv_obj_check_type(lv_obj_get_child(r, i), &lv_label_class) && !is_icon(lv_obj_get_child(r, i))) probe = lv_obj_get_child(r, i);
    int fresh = lv_obj_get_style_bg_opa(r, 0) == LV_OPA_TRANSP && lv_obj_get_style_border_width(r, 0) == 0 &&
                (!probe || lv_color_eq(lv_obj_get_style_text_color(probe, 0), want));
    if(fresh && (intptr_t)lv_obj_get_user_data(r) == key) return;
    lv_obj_set_user_data(r, (void *)key);
    lv_obj_set_style_bg_opa(r, LV_OPA_TRANSP, 0); lv_obj_set_style_border_width(r, 0, 0);   /* no pill */
    lv_obj_set_style_shadow_width(r, 0, 0); lv_obj_set_style_outline_width(r, 0, 0);
    lv_obj_set_style_transform_pivot_y(r, h / 2, 0);
    /* packing: the centre goes where the rows in between, at their sizes, put it */
    int ty = 0;
    if(!centred){ int want_off = wheel_reach(u) * pitch / 100; ty = (dq < 0 ? -1 : 1) * (want_off - u * pitch / 100); }
    if(!centred && dq > 0){
        /* The packing above assumes every row in between is song-sized; a taller centred row (the album's cover /
         * Play / Shuffle row) is not, so the songs below it would be pulled up into it. Keep this row's top clear of
         * the drawn bottom of the row above (its scale and offset are already set this pass); among song rows this
         * never applies, and below the tall row it carries the push down the list. */
        for(int32_t i = (int32_t)lv_obj_get_index(r) - 1; i >= 0; i--){
            lv_obj_t *p = lv_obj_get_child(list, i);
            if(!lv_obj_has_flag(p, LV_OBJ_FLAG_USER_1)) continue;
            lv_area_t pa; lv_obj_get_coords(p, &pa);
            int ph = lv_area_get_height(&pa), psc = lv_obj_get_style_transform_scale_y(p, 0);
            int pcy = (pa.y1 + pa.y2) / 2 - ra.y1;                        /* coords include its translate_y */
            int pbot = pcy + ph * psc / 512, top = cy + ty - h * sc / 512;
            int gap = lv_obj_has_flag(p, LV_OBJ_FLAG_USER_2) ? 6 : 0;
            if(top < pbot + gap) ty += pbot + gap - top;
            break;
        }
    }
    int vy = cy + ty, dy = abs(vy - 180);
    /* the drum: rows away from the middle start a little further in, along the round edge */
    int indent = 0;
    if(!centred){ int half = dy < 170 ? curvelist_circle_half(170, dy) : 0; indent = (170 - half) * 2 / 3; }
    /* Disco's navigation shard: no row reaches under it. A row whose far end would moves left until it is 16 px
     * clear, as far as the round edge allows; whatever is still over comes off its size. Rows are never narrowed. */
    int w0 = lv_obj_get_width(r), shift = 0;
    int ax1 = a.x1 - lv_obj_get_style_translate_x(r, 0) - ra.x1 + indent;
    int pv = centred ? w0 / 2 : 18;                                   /* grow from the text's start (centred: the middle) */
    int y1 = vy - h * sc / 512, y2 = y1 + h * sc / 256;
    int rx = centred ? 0 : disco_clear_right(y1, y2);
    if(rx > 0){
        int limit = rx - 16, right = ax1 + pv + (w0 - pv) * sc / 256, over = right - limit;
        if(over > 0){
            int edge = 180 - (dy < 180 ? curvelist_circle_half(180, dy) : 0) + 12;
            int left = ax1 + pv - pv * sc / 256;
            shift = over < left - edge ? over : (left - edge > 0 ? left - edge : 0);
            over -= shift;
            if(over > 0 && w0 - pv > 0){ sc = (limit + shift - ax1 - pv) * 256 / (w0 - pv); if(sc < 150) sc = 150; }
        }
    }
    if(lv_obj_get_style_transform_pivot_x(r, 0) != pv) lv_obj_set_style_transform_pivot_x(r, pv, 0);
    lv_obj_set_style_transform_scale(r, sc, 0);
    lv_obj_set_style_translate_x(r, indent - shift, 0);
    lv_obj_set_style_translate_y(r, ty, 0);
    g_wheel_icon = fade < 8 ? TC(ACCENT_PRIMARY) : theme_color_mix(TC(CANVAS), TC(ACCENT_PRIMARY), (uint8_t)fade);   /* Squash icons stay orange */
    wheel_paint(r, want);
}
static void curve(curvelist_t *c){
    if(curvelist_wheel() && !wheel_excluded(c->list)){
        curvelist_wheel_list(c->list, 48);
        for(uint32_t i = 0; i < lv_obj_get_child_count(c->list); i++){
            lv_obj_t *r = lv_obj_get_child(c->list, i);
            if(lv_obj_has_flag(r, LV_OBJ_FLAG_USER_1)) curvelist_wheel_row(r, c->list);
        }
        return;
    }
    if(th_braun_family()){                                               /* Braun: straight rows, restyled */
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
    int mode = c->disco_style ? simple_cfg_get_int("disco_scroll",0) : curvelist_disco_scroll();
    for(uint32_t i = 0; i < n; i++){
        lv_obj_t *r = lv_obj_get_child(c->list, i);
        if(!lv_obj_has_flag(r, LV_OBJ_FLAG_USER_1)) continue;
        lv_area_t a; lv_obj_get_coords(r, &a);
        if(a.y2 < -40 || a.y1 > 400) continue;                      /* off-screen: nothing to do */
        int w, shift;
        if(!curvelist_disco_geom(mode, a.y1, a.y2, c->full_w, &w, &shift)){
        int dy = abs((a.y1 + a.y2) / 2 - 180) + lv_area_get_height(&a) / 2;
        int half = dy < 176 ? curvelist_circle_half(180,dy) - 10 : 0;
        w = 2 * half;
        if(w > c->full_w) w = c->full_w;
        if(w < c->full_w - (NB - 1) * 20) w = c->full_w - (NB - 1) * 20;
        shift = 0; int L = disco_clear_left(a.y1, a.y2);
        { int R = disco_clear_right(a.y1, a.y2);                              /* Disco: clear of the closed sliver */
          if(R && 180 + w / 2 > R){ int lft = 180 - w / 2; w = R - lft; shift = (lft + R) / 2 - 180; } }
        if(L && 180 - w / 2 < L){ int right = 180 + w / 2; int nw = right - L; if(nw < 120) nw = 120; shift = L + nw / 2 - 180; w = nw; }   /* Disco: start right of the picker (never narrower than 120) */   /* Disco: clear of the picker */
        }
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
    if(th_braun_family()) return;                                       /* Braun: no position dots */
    if(th_zune()){ if(c->cur != -3){ c->cur = -3; for(int k = 0; k < 11; k++) if(c->dot[k]) lv_obj_add_flag(c->dot[k], LV_OBJ_FLAG_HIDDEN); } return; }   /* Zune: none either (hidden once, not on every scroll step) */
    int32_t sy = lv_obj_get_scroll_y(c->list), sb = lv_obj_get_scroll_bottom(c->list), tot = sy + sb;
    if(th_null() && !c->disco_style){null_scroll_line(c->dot,11,sy,tot);return;}
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
        int x = (int)(180 + 168 * cosf(a)) - sz / 2;                  /* Left: the dots move to the right rim */
        lv_obj_set_pos(c->dot[k], disco_mirror() ? 360 - x - sz : x, (int)(180 + 168 * sinf(a)) - sz / 2);
        lv_obj_set_style_bg_color(c->dot[k], on ? TC(ACCENT_PRIMARY) : TC(PAGE_DOT_QUIET), 0);
    }
}
static void scroll_cb(lv_event_t *e){ curvelist_t *c = lv_event_get_user_data(e); curve(c); dots(c); }
/* Fixed viewport overlays: transparent beside the rows, opaque at the safe
 * edges. Drawing after children keeps them stationary during scrolling and
 * adds no objects, input surfaces or animation timers.
 * Each band only takes space that holds no row at rest: the list's own padding plus however far it is scrolled
 * on that side. At the top of a list the first row is never faded, at the end the last one is not either; rows
 * only fade while they are on their way out of view. */
/* Pre-rendered edge fades: a 16 px wide strip of the theme's background whose transparency ramps over the band,
 * with a 4x4 ordered dither in the ramp - a plain gradient breaks into visible steps (blocks) on the panel, most of
 * all where two fades overlap. Built once per size and colour, then tiled across the band. */
static const uint8_t BAYER4[4][4] = { { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 } };
const lv_image_dsc_t *curvelist_fade_strip(int vertical, int len, int opaque_end, lv_color_t col){
    static lv_image_dsc_t dsc[4]; static uint8_t *buf[4]; static int key_len[4]; static uint32_t key_col[4]; static int key_v[4], key_e[4];
    uint32_t c = lv_color_to_u32(col); if(len < 2) len = 2;
    for(int i = 0; i < 4; i++) if(buf[i] && key_len[i] == len && key_col[i] == c && key_v[i] == vertical && key_e[i] == opaque_end) return &dsc[i];
    static int next; int i = next++ & 3;
    int w = vertical ? 16 : len, h = vertical ? len : 16;
    lv_free(buf[i]); buf[i] = lv_malloc((size_t)w * h * 4); if(!buf[i]) return NULL;
    for(int y = 0; y < h; y++) for(int x = 0; x < w; x++){
        int pos = vertical ? y : x;                                       /* 0 .. len-1 along the ramp */
        int t = opaque_end ? pos : (len - 1 - pos);                        /* 0 = clear end .. len-1 = solid end */
        int a = 255 * t / (len - 1) + ((int)BAYER4[y & 3][x & 3] - 8) * 3 / 2;   /* +-12: hides the steps */
        if(t == 0) a = 0;
        if(t == len - 1) a = 255;
        if(a < 0) a = 0;
        if(a > 255) a = 255;
        uint8_t *px = buf[i] + ((size_t)y * w + x) * 4;
        px[0] = col.blue; px[1] = col.green; px[2] = col.red; px[3] = (uint8_t)a;
    }
    memset(&dsc[i], 0, sizeof dsc[i]);
    dsc[i].header.magic = LV_IMAGE_HEADER_MAGIC; dsc[i].header.cf = LV_COLOR_FORMAT_ARGB8888;
    dsc[i].header.w = (uint32_t)w; dsc[i].header.h = (uint32_t)h; dsc[i].header.stride = (uint32_t)w * 4;
    dsc[i].data = buf[i]; dsc[i].data_size = (uint32_t)w * h * 4;
    key_len[i] = len; key_col[i] = c; key_v[i] = vertical; key_e[i] = opaque_end;
    return &dsc[i];
}
static void edge_fade_cb(lv_event_t *e){
    if(!(th_disco() || th_simple() || th_null())) return;
    lv_obj_t *list = lv_event_get_current_target(e);
    lv_area_t a; lv_obj_get_coords(list, &a);
    /* Roughly half a standard menu row; clamp for small embedded lists. */
    int h = (curvelist_wheel() && !wheel_excluded(list)) ? WHEEL_FADE_BOT : th_null() ? 32 : 27;   /* Wheel: the footer's space */
    int height = lv_area_get_height(&a);
    if(h > height / 2) h = height / 2;
    int h_top = (curvelist_wheel() && !wheel_excluded(list)) ? WHEEL_FADE_TOP : h;       /* Wheel: the header's space above the row above */
    int room[2] = { lv_obj_get_style_pad_top(list, LV_PART_MAIN) + lv_obj_get_scroll_y(list),
                    lv_obj_get_style_pad_bottom(list, LV_PART_MAIN) + lv_obj_get_scroll_bottom(list) };
    int band_h[2];
    for(int k = 0; k < 2; k++){ int hk = k ? h : h_top; band_h[k] = room[k] < hk ? room[k] : hk; }
    if(band_h[0] < 2 && band_h[1] < 2) return;
    lv_draw_rect_dsc_t d; lv_draw_rect_dsc_init(&d);
    lv_obj_t *screen = list;   /* the nearest background actually painted behind the rows (the list's own, in Squash) */
    while(lv_obj_get_parent(screen) && lv_obj_get_style_bg_opa(screen, LV_PART_MAIN) != LV_OPA_COVER)
        screen = lv_obj_get_parent(screen);
    /* Disco can paint album art over its canvas. Repaint that same fixed
     * backdrop through a row-wise opacity ramp instead of fading to black. */
    const void *backdrop = lv_obj_get_style_bg_image_src(screen, LV_PART_MAIN);
    if(backdrop){
        lv_layer_t *layer = lv_event_get_layer(e);
        lv_area_t original = layer->_clip_area, image_area;
        lv_obj_get_coords(screen, &image_area);
        lv_draw_image_dsc_t image; lv_draw_image_dsc_init(&image);
        image.src = backdrop;
        image.recolor = lv_obj_get_style_bg_image_recolor(screen, LV_PART_MAIN);
        image.recolor_opa = lv_obj_get_style_bg_image_recolor_opa(screen, LV_PART_MAIN);
        for(int edge = 0; edge < 2; edge++) for(int n = 0, bh = band_h[edge]; bh >= 2 && n < bh; n++){
            lv_area_t line = a;
            line.y1 = line.y2 = edge ? a.y2 - n : a.y1 + n;
            if(!lv_area_intersect(&layer->_clip_area, &original, &line)) continue;
            image.opa = (lv_opa_t)(255 * (bh - 1 - n) / (bh - 1));
            if(image.opa) lv_draw_image(layer, &image, &image_area);
        }
        layer->_clip_area = original;
        return;
    }
    lv_color_t canvas = lv_obj_get_style_bg_color(screen, LV_PART_MAIN);
    lv_area_t sa; lv_obj_get_coords(screen, &sa);
    int vgrad = lv_obj_get_style_bg_grad_dir(screen, LV_PART_MAIN) == LV_GRAD_DIR_VER;
    lv_color_t gcol = lv_obj_get_style_bg_grad_color(screen, LV_PART_MAIN);
    for(int edge = 0; edge < 2; edge++){                                  /* the dithered strips, tiled across */
        int bh = band_h[edge]; if(bh < 2) continue;
        lv_color_t col = canvas;
        if(vgrad){   /* a shaded background: the exact colour it has at this band's height, so no step shows */
            int ym = edge ? a.y2 - bh / 2 : a.y1 + bh / 2, sh = lv_area_get_height(&sa); if(sh < 1) sh = 1;
            int t = (ym - sa.y1) * 255 / sh; if(t < 0) t = 0; if(t > 255) t = 255;
            col = theme_color_mix(gcol, canvas, (uint8_t)t);
        }
        const lv_image_dsc_t *img = curvelist_fade_strip(1, bh, edge ? 1 : 0, col);
        if(!img) continue;
        lv_area_t band = a;
        if(edge) band.y1 = band.y2 - bh + 1; else band.y2 = band.y1 + bh - 1;
        lv_draw_image_dsc_t id; lv_draw_image_dsc_init(&id); id.src = img; id.tile = 1;
        lv_draw_image(lv_event_get_layer(e), &id, &band);
    }
    return;
    d.bg_color = canvas; d.bg_opa = LV_OPA_COVER;
    d.bg_grad.dir = LV_GRAD_DIR_VER; d.bg_grad.stops_count = 2;
    d.bg_grad.stops[0].color = d.bg_grad.stops[1].color = canvas;
    d.bg_grad.stops[0].frac = 0; d.bg_grad.stops[1].frac = 255;
    lv_area_t band = a; band.y2 = band.y1 + band_h[0] - 1;
    d.bg_grad.stops[0].opa = LV_OPA_COVER;
    d.bg_grad.stops[1].opa = LV_OPA_TRANSP;
    if(band_h[0] >= 2) lv_draw_rect(lv_event_get_layer(e), &d, &band);
    band = a; band.y1 = band.y2 - band_h[1] + 1;
    d.bg_grad.stops[0].opa = LV_OPA_TRANSP;
    d.bg_grad.stops[1].opa = LV_OPA_COVER;
    if(band_h[1] >= 2) lv_draw_rect(lv_event_get_layer(e), &d, &band);
}
void curvelist_edge_fade(lv_obj_t *list){
    lv_obj_remove_event_cb(list, edge_fade_cb);
    if(th_disco() || th_simple() || th_null())
        lv_obj_add_event_cb(list, edge_fade_cb, LV_EVENT_DRAW_POST, NULL);
}
void curvelist_attach(curvelist_t *c, lv_obj_t *list, lv_obj_t *root, int full_w){
    curvelist_edge_fade(list);
    c->list = list; c->full_w = full_w; c->cur = -2; c->disco_style=0;
    if(th_braun_family()){                                                /* Braun: the grille, and the list on the lower segment */
        br_face(root);
        lv_obj_update_layout(list);
        int y = lv_obj_get_y(list) - 6; if(y < 40) y = 40;
        if(!th_braun2()){lv_obj_t *seg = br_segment(root, y);
        lv_obj_move_to_index(seg, 1);}                            /* above the grille, below the list and the header */
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
static void wheel_settle_cb(lv_timer_t *t){   /* Wheel: once the screen is built and themed, place the rows for real */
    curvelist_t *c = lv_timer_get_user_data(t);
    if(c && c->list && lv_obj_is_valid(c->list)){ lv_obj_update_layout(c->list); curve(c); }
}
void curvelist_update(curvelist_t *c){
    if(!c->list) return;
    lv_obj_update_layout(c->list);
    curve(c); c->cur = -2; dots(c);
    if(curvelist_wheel()){ lv_timer_t *t = lv_timer_create(wheel_settle_cb, 60, c); lv_timer_set_repeat_count(t, 1); }
}
