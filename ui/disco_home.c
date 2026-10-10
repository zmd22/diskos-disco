/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Music / Home in the Disco theme (fork): the playing album's cover, sharp, across the whole screen; the clock with a
 * small weather icon and the temperature under it in a glass pill at the top (Settings > Display > Show Clock); the
 * title and artist next to the menu picker; previous / play-pause / next on an arc that runs with the progress arc on
 * the lower-right rim. Tapping the title opens the track overlay: Favourite, Play Mode, Up Next, Lyrics, Immersive.
 * Nothing moves while idle: the progress arc is updated once a second, only while Music is on screen. home.c forwards
 * its setters here when th_disco(). */
#include "screens.h"
#include "theme.h"
#include "theme_kit.h"
#include "config.h"
#include "battery.h"
#include "ipc.h"
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

extern const lv_font_t font_theme_20;
extern const lv_font_t font_weather16;

/* text over the cover: every label has a twin one pixel down-right in the opposite colour (a soft shadow), and the pair
 * flips between light-on-dark and dark-on-light with the brightness of the cover behind it (decided once per album) */
typedef struct { lv_obj_t *sh, *fg; int dim, scroll, ink; char cur[192]; } dlabel_t;   /* ink: last dark/light (-1 unset) */   /* cur: the text as set (an ellipsis edits the label's own copy) */
/* a long title / artist scrolls five times, then settles on an ellipsis (again on the next track) */
static void scroll_done(lv_anim_t *a){ lv_label_set_long_mode((lv_obj_t *)a->var, LV_LABEL_LONG_DOT); }
const lv_anim_t *disco_scroll5(void){
    static lv_anim_t t; static int init;
    if(!init){ lv_anim_init(&t); lv_anim_set_repeat_count(&t, 5); lv_anim_set_repeat_delay(&t, 1200); lv_anim_set_completed_cb(&t, scroll_done); init = 1; }
    return &t;
}
static void dl_scroll5(dlabel_t *d){
    d->scroll = 1;
    lv_obj_t *l2[2] = { d->sh, d->fg };
    for(int k = 0; k < 2; k++){ lv_obj_set_style_anim(l2[k], disco_scroll5(), 0); lv_label_set_long_mode(l2[k], LV_LABEL_LONG_SCROLL_CIRCULAR); }
}
static dlabel_t L_TIME, L_WX, L_STAT, L_TITLE, L_ARTIST, L_MODE, L_EL, L_RM;   /* EL/RM: elapsed / remaining under the line */
static lv_obj_t *g_mode;
static void mode_paint(void);
void disco_progress_apply(void);
static void acc_paint(void);
static lv_obj_t *dl_make(lv_obj_t *par, dlabel_t *d, const lv_font_t *f, int w, int h, lv_align_t al, int x, int y, int dim){
    d->dim = dim;
    for(int k = 0; k < 2; k++){
        lv_obj_t *l = lv_label_create(par); lv_label_set_text(l, "");
        lv_obj_set_style_text_font(l, f, 0); lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        if(w > 0){ lv_label_set_long_mode(l, LV_LABEL_LONG_DOT); lv_obj_set_size(l, w, h); }
        lv_obj_align(l, al, x + (k ? 0 : 1), y + (k ? 0 : 1));
        lv_obj_clear_flag(l, LV_OBJ_FLAG_CLICKABLE);
        if(k) d->fg = l; else { d->sh = l; lv_obj_set_style_text_opa(l, 150, 0); }
    }
    return d->fg;
}
static void dl_text(dlabel_t *d, const char *t){
    if(!d->fg || !t) return;
    if(!strcmp(d->cur, t)) return;
    snprintf(d->cur, sizeof d->cur, "%s", t);
    if(d->scroll){ lv_label_set_long_mode(d->fg, LV_LABEL_LONG_SCROLL_CIRCULAR); lv_label_set_long_mode(d->sh, LV_LABEL_LONG_SCROLL_CIRCULAR); }
    lv_label_set_text(d->fg, t); lv_label_set_text(d->sh, t);
}
static void dl_ink(dlabel_t *d, int dark_text){                     /* dark text on a light cover, and the reverse */
    if(!d->fg || d->ink == dark_text + 1) return;                     /* unchanged: a restyle would restart a title scroll */
    d->ink = dark_text + 1;
    lv_color_t ink = theme_color(dark_text ? THEME_CLR_FIXED_MEDIA_BLACK : THEME_CLR_FIXED_MEDIA_WHITE);
    lv_color_t shd = theme_color(dark_text ? THEME_CLR_FIXED_MEDIA_WHITE : THEME_CLR_FIXED_MEDIA_BLACK);
    lv_obj_set_style_text_color(d->fg, ink, 0); lv_obj_set_style_text_opa(d->fg, d->dim ? 205 : LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(d->sh, shd, 0);
}
static lv_obj_t *g_cover, *g_wash, *g_pill, *g_time, *g_wx, *g_title, *g_artist, *g_trk, *g_btn[3], *g_btn_lbl[3];
static lv_timer_t *g_tick;
#define TINT_OPA (theme_variant() ? 110 : 95)                        /* over the sharp cover, for readable text */
static lv_color_t g_acc;
static lv_font_t g_wfont;                                           /* small UI face + the weather icons as fallback */
static int g_have;
static int g_idle;                                                  /* nothing playing: see idle_eval() */

static lv_obj_t *glass_circle(lv_obj_t *root, int cx, int cy, int d, const char *glyph){
    lv_obj_t *b = lv_button_create(root); lv_obj_remove_style_all(b);
    lv_obj_set_size(b, d, d); lv_obj_set_pos(b, cx - d / 2, cy - d / 2);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(b, TC(SURFACE), 0); lv_obj_set_style_bg_opa(b, 205, 0);
    lv_obj_set_style_bg_color(b, TC(SURFACE_RAISED), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(b, 1, 0); lv_obj_set_style_border_color(b, TC(TEXT_PRIMARY), 0); lv_obj_set_style_border_opa(b, 60, 0);
    lv_obj_set_ext_click_area(b, 6);
    lv_obj_add_flag(b, LV_OBJ_FLAG_USER_2);                             /* the kit leaves it alone: the accent ring stays */
    lv_obj_t *l = lv_label_create(b); lv_label_set_text(l, glyph);
    lv_obj_set_style_text_font(l, d > 46 ? TF(UI_22) : TF(UI_18), 0);
    lv_obj_set_style_text_color(l, TC(TEXT_PRIMARY), 0); lv_obj_center(l);
    return b;
}
static void transport_cb(lv_event_t *e){
    if(lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    ui_transport_command((const char *)lv_event_get_user_data(e));
}
static void disco_progress_arc_create(lv_obj_t *root);
static void pa_set(int f);
/* ---- the progress underline: a rainbow (the CD's sheen) that fills from left to right; drag or tap it to seek ---- */
#define UL_W 240
#define UL_X ((360 - UL_W) / 2)
#define UL_Y 214
static lv_obj_t *g_ul_fill, *g_ul_knob, *g_ul_tr, *g_ul_hit, *g_ul_solid;
static int g_frac = -1, g_scrub;
static void ul_set(int f){                                         /* f: 0..1000 */
    if(f < 0) f = 0;
    if(f > 1000) f = 1000;
    if(f == g_frac) return;
    g_frac = f;
    int w = UL_W * f / 1000;
    if(w == lv_obj_get_width(g_ul_fill)) return;                       /* same pixel: nothing to redraw */
    lv_obj_set_width(g_ul_fill, w);
    lv_obj_set_x(g_ul_knob, UL_X + w - 5);
}
/* Settings > Display > Track Times: small elapsed / remaining under the ends of the line */
static void fmt_t(char *b, size_t n, long ms, int neg){ long s = ms / 1000; if(s < 0) s = 0;
    if(s >= 3600) snprintf(b, n, "%s%ld:%02ld:%02ld", neg ? "-" : "", s / 3600, s / 60 % 60, s % 60);
    else snprintf(b, n, "%s%ld:%02ld", neg ? "-" : "", s / 60, s % 60); }
static void times_paint(long pos, long dur){
    if(!L_EL.fg) return;
    char a[16], b[16];
    if(dur > 0){ fmt_t(a, sizeof a, pos, 0); fmt_t(b, sizeof b, dur - pos, 1); } else { a[0] = 0; b[0] = 0; }
    dl_text(&L_EL, a); dl_text(&L_RM, b);
}
void dhome_times_apply(void){
    if(!L_EL.fg) return;
    int on = !g_idle && cfg_get_int("disco_times", 1) && cfg_get_int("disco_progress", 0) != 2;   /* no line, no times (idle: neither) */
    lv_obj_t *o[4] = { L_EL.sh, L_EL.fg, L_RM.sh, L_RM.fg };
    for(int i = 0; i < 4; i++){ if(on) lv_obj_remove_flag(o[i], LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(o[i], LV_OBJ_FLAG_HIDDEN); }
    int arc = cfg_get_int("disco_prog_shape", 0) == 1;                 /* Arc: no line, the two times side by side */
    for(int k = 0; k < 2; k++){
        int d = k ? 0 : 1;
        if(arc){ lv_obj_align(o[k], LV_ALIGN_TOP_RIGHT, -(180 + 8) + d, UL_Y + d); lv_obj_align(o[2 + k], LV_ALIGN_TOP_LEFT, 180 + 8 + d, UL_Y + d); }
        else { lv_obj_align(o[k], LV_ALIGN_TOP_LEFT, UL_X + d, UL_Y + 9 + d); lv_obj_align(o[2 + k], LV_ALIGN_TOP_RIGHT, -(360 - UL_X - UL_W) + d, UL_Y + 9 + d); }
    }
}
static void prog_tick(lv_timer_t *t){
    (void)t;
    if(!g_ul_fill || g_scrub || screen_current() != SCR_HOME) return;
    track_state_t st; ipc_get_state(&st);
    int f = (st.have_track && st.duration_ms > 0) ? (int)((long long)st.position_ms * 1000 / st.duration_ms) : 0;
    ul_set(f); pa_set(f);
    times_paint(st.have_track ? st.position_ms : 0, st.have_track ? st.duration_ms : 0);
}
static void ul_cb(lv_event_t *e){
    lv_event_code_t c = lv_event_get_code(e);
    if(!g_have){ if(c == LV_EVENT_RELEASED || c == LV_EVENT_PRESS_LOST) g_scrub = 0; return; }   /* never stuck scrubbing */
    lv_indev_t *in = lv_indev_active(); lv_point_t p = { 0, 0 }; if(in) lv_indev_get_point(in, &p);
    int f = (p.x - UL_X) * 1000 / UL_W;
    if(c == LV_EVENT_PRESSED || c == LV_EVENT_PRESSING){
        g_scrub = 1; ul_set(f);
        track_state_t st; ipc_get_state(&st);                          /* the times follow the finger */
        if(st.have_track && st.duration_ms > 0) times_paint((long)((long long)st.duration_ms * g_frac / 1000), st.duration_ms);
        return;
    }
    if(c == LV_EVENT_RELEASED || c == LV_EVENT_PRESS_LOST){
        g_scrub = 0;
        track_state_t st; ipc_get_state(&st);
        if(st.have_track && st.duration_ms > 0) ui_seek_to((long)((long long)st.duration_ms * g_frac / 1000));
    }
}
static void underline_create(lv_obj_t *root){
    static const uint32_t IRI[7] = { 0xFF5AA0, 0xFFB45A, 0xFFF06A, 0x62F28C, 0x4AD8FF, 0x6A8CFF, 0xB36BFF };
    lv_obj_t *tr = g_ul_tr = lv_obj_create(root); lv_obj_remove_style_all(tr);   /* the unfilled line */
    lv_obj_set_pos(tr, UL_X, UL_Y); lv_obj_set_size(tr, UL_W, 4);
    lv_obj_set_style_radius(tr, 2, 0); lv_obj_set_style_bg_color(tr, TC(TEXT_PRIMARY), 0); lv_obj_set_style_bg_opa(tr, 55, 0);
    lv_obj_clear_flag(tr, LV_OBJ_FLAG_CLICKABLE);
    g_ul_fill = lv_obj_create(root); lv_obj_remove_style_all(g_ul_fill);   /* the filled part clips a full-width rainbow */
    lv_obj_set_pos(g_ul_fill, UL_X, UL_Y - 1); lv_obj_set_size(g_ul_fill, 0, 6);
    lv_obj_set_style_radius(g_ul_fill, 3, 0); lv_obj_set_style_clip_corner(g_ul_fill, true, 0);
    lv_obj_clear_flag(g_ul_fill, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    int seg = UL_W / 6;
    for(int i = 0; i < 6; i++){
        lv_obj_t *b = lv_obj_create(g_ul_fill); lv_obj_remove_style_all(b);
        lv_obj_set_pos(b, i * seg, 0); lv_obj_set_size(b, i == 5 ? UL_W - 5 * seg : seg + 1, 6);
        lv_obj_set_style_bg_color(b, theme_color_from_rgb(IRI[i]), 0); lv_obj_set_style_bg_grad_color(b, theme_color_from_rgb(IRI[i + 1]), 0);
        lv_obj_set_style_bg_grad_dir(b, LV_GRAD_DIR_HOR, 0); lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_clear_flag(b, LV_OBJ_FLAG_CLICKABLE);
    }
    g_ul_solid = lv_obj_create(g_ul_fill); lv_obj_remove_style_all(g_ul_solid);   /* Accent style: one colour over the rainbow */
    lv_obj_set_size(g_ul_solid, UL_W, 6); lv_obj_set_style_bg_opa(g_ul_solid, LV_OPA_COVER, 0);
    lv_obj_clear_flag(g_ul_solid, LV_OBJ_FLAG_CLICKABLE);
    g_ul_knob = lv_obj_create(root); lv_obj_remove_style_all(g_ul_knob);   /* a small bright head */
    lv_obj_set_size(g_ul_knob, 10, 10); lv_obj_set_pos(g_ul_knob, UL_X - 5, UL_Y - 3);
    lv_obj_set_style_radius(g_ul_knob, LV_RADIUS_CIRCLE, 0); lv_obj_set_style_bg_color(g_ul_knob, TC(TEXT_PRIMARY), 0);
    lv_obj_set_style_bg_opa(g_ul_knob, LV_OPA_COVER, 0);
    lv_obj_set_style_outline_width(g_ul_knob, 3, 0); lv_obj_set_style_outline_opa(g_ul_knob, 70, 0);   /* a soft halo: an outline, */
    lv_obj_set_style_outline_color(g_ul_knob, TC(TEXT_PRIMARY), 0);                                     /* not a blurred shadow redrawn every second */
    lv_obj_clear_flag(g_ul_knob, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *hit = g_ul_hit = lv_obj_create(root); lv_obj_remove_style_all(hit);    /* a comfortable touch band over the line */
    lv_obj_set_pos(hit, UL_X - 12, UL_Y - 16); lv_obj_set_size(hit, UL_W + 24, 36);
    lv_obj_add_flag(hit, LV_OBJ_FLAG_CLICKABLE); lv_obj_clear_flag(hit, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(hit, ul_cb, LV_EVENT_ALL, NULL);
    ul_set(0);
    disco_progress_arc_create(root);
    disco_progress_apply();
}

/* ---- Settings > Disco Options > Progress Shape: Arc --------------------------------------------------------------
 * The progress runs round the rim instead: from just below the open menu (40 degrees, lower right) clockwise past the
 * bottom and the left, over the top to just above it (320 degrees). Same colours as the line (Disco / Accent / Off),
 * the same bright head, and the same drag or tap to seek. Its touch ring sits under everything else on Music, so the
 * buttons, the clock and the title keep their taps; a press in the top 40 px is left to the Quick Settings pull-down. */
#define PA_A0 (disco_mirror() ? 220 : 40)                            /* Disco Left: from just above the shard (upper left), still clockwise */
#define PA_WRAP(d) ((d) % 360)                                          /* Left: the arc runs past 360 degrees */
#define PA_SPAN 280
#define PA_N 6
#define PA_D 344                                                       /* the drawn ring: r 172 */
#define PA_W 5
static lv_obj_t *g_pa_tr, *g_pa_seg[PA_N], *g_pa_cap, *g_pa_knob, *g_pa_hit;
static int g_pa_deg = -1, g_pa_scrub, g_pa_frac;
static int pa_on(void){ return cfg_get_int("disco_prog_shape", 0) == 1; }
static lv_obj_t *pa_arc(lv_obj_t *root, int d, int w){
    lv_obj_t *a = lv_arc_create(root);
    lv_obj_remove_style(a, NULL, LV_PART_KNOB); lv_obj_clear_flag(a, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(a, d, d); lv_obj_center(a);
    lv_arc_set_rotation(a, 0); lv_arc_set_bg_angles(a, 0, 0);
    lv_obj_set_style_arc_opa(a, 0, LV_PART_MAIN);
    lv_obj_set_style_arc_width(a, w, LV_PART_INDICATOR); lv_obj_set_style_arc_rounded(a, false, LV_PART_INDICATOR);
    lv_arc_set_angles(a, 0, 0);
    return a;
}
static void pa_dot(lv_obj_t *o, int deg, int d){
    float a = deg * 0.0174533f;
    lv_obj_set_pos(o, 180 + (int)lroundf((PA_D / 2 - PA_W / 2) * cosf(a)) - d / 2, 180 + (int)lroundf((PA_D / 2 - PA_W / 2) * sinf(a)) - d / 2);
}
static void pa_set(int f){                                           /* f: 0..1000 */
    if(!g_pa_tr) return;
    if(f < 0) f = 0;
    if(f > 1000) f = 1000;
    g_pa_frac = f;
    if(!pa_on() || cfg_get_int("disco_progress", 0) == 2) return;   /* Linear or Off: the ring stays hidden */
    int e = PA_A0 + PA_SPAN * f / 1000;
    if(e == g_pa_deg) return;                                         /* same degree: nothing to redraw */
    g_pa_deg = e;
    lv_arc_set_angles(g_pa_tr, PA_WRAP(e), PA_WRAP(PA_A0 + PA_SPAN));                     /* the track only where there's no fill: drawn once, not twice */
    for(int i = 0; i < PA_N; i++){
        int s0 = PA_A0 + PA_SPAN * i / PA_N, s1 = PA_A0 + PA_SPAN * (i + 1) / PA_N;
        if(e <= s0){ lv_obj_add_flag(g_pa_seg[i], LV_OBJ_FLAG_HIDDEN); continue; }
        lv_arc_set_angles(g_pa_seg[i], PA_WRAP(s0), PA_WRAP(e < s1 ? e : s1 + (i < PA_N - 1)));   /* +1: no hairline between segments */
        lv_obj_remove_flag(g_pa_seg[i], LV_OBJ_FLAG_HIDDEN);
    }
    if(e > PA_A0) lv_obj_remove_flag(g_pa_cap, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(g_pa_cap, LV_OBJ_FLAG_HIDDEN);
    pa_dot(g_pa_knob, e, 11);
}
static int pa_frac_at(int x, int y){                                  /* the point's place along the arc, 0..1000 */
    float a = atan2f((float)(y - 180), (float)(x - 180)) * 57.29578f;
    int rel = (int)lroundf(a) - PA_A0;
    while(rel < 0) rel += 360;
    while(rel >= 360) rel -= 360;
    if(rel > PA_SPAN) return rel < PA_SPAN + (360 - PA_SPAN) / 2 ? 1000 : 0;   /* in the gap: the nearer end */
    return rel * 1000 / PA_SPAN;
}
static void pa_cb(lv_event_t *e){
    lv_event_code_t c = lv_event_get_code(e);
    if(!g_have){ if(c == LV_EVENT_RELEASED || c == LV_EVENT_PRESS_LOST){ g_scrub = 0; g_pa_scrub = 0; } return; }
    lv_indev_t *in = lv_indev_active(); lv_point_t p = { 0, 0 }; if(in) lv_indev_get_point(in, &p);
    if(c == LV_EVENT_PRESSED){ g_pa_scrub = p.y >= 40; if(!g_pa_scrub) return; }   /* the top edge: Quick Settings */
    if(!g_pa_scrub) return;
    if(c == LV_EVENT_PRESSED || c == LV_EVENT_PRESSING){
        g_scrub = 1; pa_set(pa_frac_at(p.x, p.y));
        track_state_t st; ipc_get_state(&st);
        if(st.have_track && st.duration_ms > 0) times_paint((long)((long long)st.duration_ms * g_pa_frac / 1000), st.duration_ms);
        return;
    }
    if(c == LV_EVENT_RELEASED || c == LV_EVENT_PRESS_LOST){
        g_scrub = 0; g_pa_scrub = 0;
        track_state_t st; ipc_get_state(&st);
        if(st.have_track && st.duration_ms > 0) ui_seek_to((long)((long long)st.duration_ms * g_pa_frac / 1000));
    }
}
static void disco_progress_arc_create(lv_obj_t *root){
    static const uint32_t IRI[PA_N] = { 0xFF5AA0, 0xFFB45A, 0xFFF06A, 0x62F28C, 0x4AD8FF, 0xB36BFF };
    g_pa_hit = lv_arc_create(root);                                   /* the touch ring, under everything else */
    lv_obj_remove_style(g_pa_hit, NULL, LV_PART_KNOB);
    lv_obj_set_size(g_pa_hit, 360, 360); lv_obj_center(g_pa_hit);
    lv_arc_set_rotation(g_pa_hit, 0); lv_arc_set_bg_angles(g_pa_hit, PA_A0, PA_WRAP(PA_A0 + PA_SPAN));
    lv_obj_set_style_arc_width(g_pa_hit, 36, LV_PART_MAIN); lv_obj_set_style_arc_opa(g_pa_hit, 0, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(g_pa_hit, 0, LV_PART_INDICATOR);
    lv_obj_add_flag(g_pa_hit, LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_clear_flag(g_pa_hit, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(g_pa_hit, pa_cb, LV_EVENT_ALL, NULL);
    lv_obj_move_to_index(g_pa_hit, 0);
    g_pa_tr = pa_arc(root, PA_D, PA_W);                                /* the unfilled ring */
    lv_arc_set_angles(g_pa_tr, PA_A0, PA_WRAP(PA_A0 + PA_SPAN));
    lv_obj_set_style_arc_color(g_pa_tr, TC(TEXT_PRIMARY), LV_PART_INDICATOR); lv_obj_set_style_arc_opa(g_pa_tr, 55, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(g_pa_tr, true, LV_PART_INDICATOR);
    for(int i = 0; i < PA_N; i++){
        g_pa_seg[i] = pa_arc(root, PA_D, PA_W);
        lv_obj_set_style_arc_color(g_pa_seg[i], theme_color_from_rgb(IRI[i]), LV_PART_INDICATOR);
        lv_obj_add_flag(g_pa_seg[i], LV_OBJ_FLAG_HIDDEN);
    }
    g_pa_cap = lv_obj_create(root); lv_obj_remove_style_all(g_pa_cap);   /* the rounded start */
    lv_obj_set_size(g_pa_cap, PA_W, PA_W); lv_obj_set_style_radius(g_pa_cap, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(g_pa_cap, theme_color_from_rgb(IRI[0]), 0); lv_obj_set_style_bg_opa(g_pa_cap, LV_OPA_COVER, 0);
    lv_obj_clear_flag(g_pa_cap, LV_OBJ_FLAG_CLICKABLE); pa_dot(g_pa_cap, PA_A0, PA_W);
    lv_obj_add_flag(g_pa_cap, LV_OBJ_FLAG_HIDDEN);
    g_pa_knob = lv_obj_create(root); lv_obj_remove_style_all(g_pa_knob);   /* the bright head, as on the line */
    lv_obj_set_size(g_pa_knob, 11, 11);
    lv_obj_set_style_radius(g_pa_knob, LV_RADIUS_CIRCLE, 0); lv_obj_set_style_bg_color(g_pa_knob, TC(TEXT_PRIMARY), 0);
    lv_obj_set_style_bg_opa(g_pa_knob, LV_OPA_COVER, 0);
    lv_obj_set_style_outline_width(g_pa_knob, 3, 0); lv_obj_set_style_outline_opa(g_pa_knob, 70, 0);   /* halo as an outline (cheap) */
    lv_obj_set_style_outline_color(g_pa_knob, TC(TEXT_PRIMARY), 0);
    lv_obj_clear_flag(g_pa_knob, LV_OBJ_FLAG_CLICKABLE);
    g_pa_deg = -1; pa_set(0);
}
static void pa_paint(int m){                                          /* m: 1 Accent, else the rainbow */
    static const uint32_t IRI[PA_N] = { 0xFF5AA0, 0xFFB45A, 0xFFF06A, 0x62F28C, 0x4AD8FF, 0xB36BFF };
    if(!g_pa_tr) return;
    for(int i = 0; i < PA_N; i++) lv_obj_set_style_arc_color(g_pa_seg[i], m == 1 ? g_acc : theme_color_from_rgb(IRI[i]), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(g_pa_cap, m == 1 ? g_acc : theme_color_from_rgb(IRI[0]), 0);
}
/* main.c asks at the press: is this finger on Music's seek line or ring? (so a backwards drag isn't a swipe to Apps) */
int dhome_owns_point(int x, int y){
    if(!g_ul_fill || !th_disco() || screen_current() != SCR_HOME || cfg_get_int("disco_progress", 0) == 2) return 0;
    if(pa_on()){
        int dx = x - 180, dy = y - 180, r2 = dx * dx + dy * dy;
        if(y < 40 || r2 < 144 * 144) return 0;
        float a = atan2f((float)dy, (float)dx) * 57.29578f;
        int rel = (int)lroundf(a) - PA_A0; while(rel < 0) rel += 360;
        return rel <= PA_SPAN;
    }
    return x >= UL_X - 12 && x < UL_X + UL_W + 12 && y >= UL_Y - 16 && y < UL_Y + 20;
}

/* ---- the track overlay (title tap) ----------------------------------------------------------------
 * Favourite changes in place (the overlay stays); Album, Artist, Queue, Lyrics and Immersive go there. Only a tap on
 * the title closes it. The play-mode button shows the actual mode: its icon (with a "1" for Repeat One / Single) and
 * its name as the caption. */
static lv_obj_t *g_ov, *g_ov_btn[6], *g_ov_cap[6];
static void ov_close(void){ if(g_ov){ lv_obj_delete(g_ov); g_ov = NULL; } }
void dhome_overlay_close(void){ ov_close(); }
static void ov_title_cb(lv_event_t *e){ if(lv_event_get_code(e) == LV_EVENT_CLICKED) ov_close(); }
enum { OV_FAV, OV_ALBUM, OV_ARTIST, OV_QUEUE, OV_LYRICS, OV_IMM, OV_N };
void disco_open_np_immersive(void);
static void ov_mark(int i, int on){
    lv_obj_t *b = g_ov_btn[i], *l = lv_obj_get_child(b, 0);
    lv_obj_set_style_border_width(b, on ? 2 : 1, 0); lv_obj_set_style_border_color(b, g_acc, 0); lv_obj_set_style_border_opa(b, on ? LV_OPA_COVER : 140, 0);
    lv_obj_set_style_border_opa(b, on ? LV_OPA_COVER : 60, 0);
    lv_obj_set_style_text_color(l, g_acc, 0); lv_obj_set_style_bg_color(b, on ? TC(SURFACE_RAISED) : TC(SURFACE), 0);
}
static void ov_place_cap(int i, int x, int y){ lv_obj_update_layout(g_ov_cap[i]); lv_obj_set_pos(g_ov_cap[i], x - lv_obj_get_width(g_ov_cap[i]) / 2, y + 33); }
static void ov_paint(void){
    if(!g_ov) return;
    ov_mark(OV_FAV, ui_np_fav_state() == 1);
}
static void ov_act_cb(lv_event_t *e){
    if(lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    int a = (int)(intptr_t)lv_event_get_user_data(e);
    switch(a){
        case OV_FAV:    ui_np_fav_toggle(); ov_paint(); return;       /* stays open */
        case OV_ALBUM:  ov_close(); ui_np_open_album(); break;
        case OV_ARTIST: ov_close(); ui_np_open_artist(); break;
        case OV_QUEUE:  ov_close(); queue_open(); break;                /* our own Queue */
        case OV_LYRICS: ov_close(); lyrics_open(); break;
        case OV_IMM:    ov_close(); disco_open_np_immersive(); break;
    }
}
static void ov_open(void){
    if(!g_have || g_ov) return;
    g_ov = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(g_ov);
    lv_obj_set_size(g_ov, 360, 360);
    lv_obj_set_style_bg_color(g_ov, TC(CANVAS), 0); lv_obj_set_style_bg_opa(g_ov, 225, 0);
    lv_obj_add_flag(g_ov, LV_OBJ_FLAG_CLICKABLE); lv_obj_clear_flag(g_ov, LV_OBJ_FLAG_SCROLLABLE);   /* swallows taps */
    lv_obj_t *tb = lv_obj_create(g_ov); lv_obj_remove_style_all(tb);   /* the title + artist: tap here to close */
    lv_obj_set_size(tb, 260, 64); lv_obj_align(tb, LV_ALIGN_TOP_MID, 0, 66);
    lv_obj_add_flag(tb, LV_OBJ_FLAG_CLICKABLE); lv_obj_set_ext_click_area(tb, 10);
    lv_obj_add_event_cb(tb, ov_title_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *t = lv_label_create(tb); lv_label_set_text(t, L_TITLE.cur);
    lv_label_set_long_mode(t, LV_LABEL_LONG_DOT); lv_obj_set_size(t, 250, 28);
    lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0); lv_obj_set_style_text_font(t, ui_font_cjk(20), 0);
    lv_obj_set_style_text_color(t, TC(TEXT_PRIMARY), 0); lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 8);
    lv_obj_t *ar = lv_label_create(tb); lv_label_set_text(ar, L_ARTIST.cur);
    lv_label_set_long_mode(ar, LV_LABEL_LONG_DOT); lv_obj_set_size(ar, 230, 22);
    lv_obj_set_style_text_align(ar, LV_TEXT_ALIGN_CENTER, 0); lv_obj_set_style_text_font(ar, ui_font_cjk(16), 0);
    lv_obj_set_style_text_color(ar, TC(TEXT_SECONDARY), 0); lv_obj_align(ar, LV_ALIGN_TOP_MID, 0, 38);
    static const struct { int x, y; const char *cap; } P[OV_N] = {          /* two gentle arcs of three */
        { 104, 164, "Favourite" }, { 180, 174, "Album" }, { 256, 164, "Artist" },
        { 112, 254, "Queue" },     { 180, 266, "Lyrics" }, { 248, 254, "Immersive" } };
    const char *G[OV_N] = { TH_IC_HEART, TH_IC_RECORD, "\xEF\x84\xB0", LV_SYMBOL_LIST, LV_SYMBOL_FILE, LV_SYMBOL_IMAGE };   /* Artist: microphone */
    for(int i = 0; i < OV_N; i++){
        lv_obj_t *b = glass_circle(g_ov, P[i].x, P[i].y, 58, G[i]);
        g_ov_btn[i] = b; lv_obj_set_style_text_color(lv_obj_get_child(b, 0), g_acc, 0); lv_obj_set_style_border_color(b, g_acc, 0); lv_obj_set_style_border_opa(b, 140, 0);
        if(i == OV_FAV || i == OV_ALBUM) lv_obj_set_style_text_font(lv_obj_get_child(b, 0), &font_theme_20, 0);
        if(i == OV_ARTIST) lv_obj_set_style_text_font(lv_obj_get_child(b, 0), TF(ICON_20), 0);
        lv_obj_add_event_cb(b, ov_act_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        g_ov_cap[i] = lv_label_create(g_ov); lv_label_set_text(g_ov_cap[i], P[i].cap);
        lv_obj_set_style_text_font(g_ov_cap[i], TF(UI_12), 0); lv_obj_set_style_text_color(g_ov_cap[i], TC(TEXT_SECONDARY), 0);
        ov_place_cap(i, P[i].x, P[i].y);
    }
    kit_pass(g_ov);
    ov_paint();
}
/* a tap opens the overlay; a hold does Settings > Disco Options > Title Hold: 0 Immersive, 1 Favourite, 2 nothing */
/* ---- idle: nothing playing (10 s after start, or at once with Resume Playback off) ------------------------------
 * Music shows a generated CD instead of a cover - a disc seen close up, iridescent bands that turn with the angle,
 * fine grooves, a clear hub, a soft glint - and "Go to Library" instead of the title (a tap opens the Library).
 * Drawn once into its own picture; the other Disco screens tint it into their backdrop as they do a cover. */
static lv_image_dsc_t g_idle_dsc;
static uint8_t *g_idle_px;
const void *disco_idle_art(void){
    if(g_idle_px) return &g_idle_dsc;
    g_idle_px = malloc(360 * 360 * 4);
    if(!g_idle_px) return NULL;
    for(int y = 0; y < 360; y++) for(int x = 0; x < 360; x++){
        float dx = x - 180.0f, dy = y - 180.0f, r = sqrtf(dx * dx + dy * dy), a = atan2f(dy, dx);
        float R, G, B;
        float bg = 0.10f + 0.10f * (1.0f - (float)y / 360.0f);           /* night-blue field behind the disc */
        R = bg * 0.55f; G = bg * 0.45f; B = bg * 1.40f;
        if(r < 176.0f && r > 46.0f){                                    /* the data side: rainbow bands that turn with the angle */
            float t = 2.0f * a + r * 0.018f;
            float k = 0.35f + 0.30f * (0.5f + 0.5f * cosf(4.0f * a));    /* brighter in four soft spokes, like light on a CD */
            R = 0.18f + k * (0.5f + 0.5f * cosf(t));
            G = 0.18f + k * (0.5f + 0.5f * cosf(t - 2.094f));
            B = 0.24f + k * (0.5f + 0.5f * cosf(t + 2.094f));
            float g = 0.92f + 0.08f * sinf(r * 1.7f);                     /* fine grooves */
            R *= g; G *= g; B *= g;
            float glint = expf(-((dx + dy + 60.0f) * (dx + dy + 60.0f)) / 900.0f) * 0.35f;   /* a diagonal glint */
            R += glint; G += glint; B += glint;
            if(r > 172.0f){ R = R * 0.6f + 0.25f; G = G * 0.6f + 0.25f; B = B * 0.6f + 0.28f; }   /* the rim */
        } else if(r <= 46.0f && r > 16.0f){                             /* the clear hub */
            float c = r > 40.0f ? 0.55f : 0.30f + 0.10f * (r - 16.0f) / 24.0f;
            R = c * 0.9f; G = c * 0.95f; B = c;
        } else if(r <= 16.0f){ R = bg * 0.4f; G = bg * 0.35f; B = bg; }  /* the hole */
        if(R > 1) R = 1;
        if(G > 1) G = 1;
        if(B > 1) B = 1;
        uint8_t *p = g_idle_px + ((size_t)y * 360 + x) * 4;
        p[0] = (uint8_t)(B * 255); p[1] = (uint8_t)(G * 255); p[2] = (uint8_t)(R * 255); p[3] = 255;
    }
    memset(&g_idle_dsc, 0, sizeof g_idle_dsc);
    g_idle_dsc.header.magic = LV_IMAGE_HEADER_MAGIC; g_idle_dsc.header.cf = LV_COLOR_FORMAT_XRGB8888;
    g_idle_dsc.header.w = 360; g_idle_dsc.header.h = 360; g_idle_dsc.header.stride = 360 * 4;
    g_idle_dsc.data = g_idle_px; g_idle_dsc.data_size = 360 * 360 * 4;
    return &g_idle_dsc;
}
int dhome_idle(void){ return g_idle; }
static void idle_eval(void){                                            /* switch idle on / off as the track state says */
    int want = !g_have && (lv_tick_get() >= 10000 || cfg_get_int("memory_play", 0) == 0);
    if(want == g_idle) return;
    g_idle = want;
    if(g_idle){ dl_text(&L_TITLE, "Go to Library"); dl_text(&L_ARTIST, ""); }
    disco_art_changed();                                                /* the cover, the backdrops and the ink follow */
    disco_progress_apply();
}
static void idle_timer_cb(lv_timer_t *t){ (void)t; idle_eval(); }
static void title_cb(lv_event_t *e){
    lv_event_code_t c = lv_event_get_code(e);
    if(c == LV_EVENT_SHORT_CLICKED && g_idle){ screen_show(SCR_LIBRARY); return; }   /* idle: "Go to Library" */
    if(c == LV_EVENT_SHORT_CLICKED) ov_open();                         /* not CLICKED: that also follows a hold */
    else if(c == LV_EVENT_LONG_PRESSED && g_have && !g_ov){
        int h = cfg_get_int("disco_title_hold", 0);
        if(h == 0) disco_open_np_immersive();
        else if(h == 1) ui_np_fav_toggle();                             /* toasts Added / Removed */
    }
}
/* Settings > Disco Options > Title Position: 0 Centred, 1 Left */
void dhome_title_apply(void){
    if(!L_TITLE.fg) return;
    int left = cfg_get_int("disco_title_al", 0) == 1;
    dlabel_t *L[2] = { &L_TITLE, &L_ARTIST }; int Y[2] = { 0, 38 };
    for(int i = 0; i < 2; i++) for(int k = 0; k < 2; k++){
        lv_obj_t *l = k ? L[i]->fg : L[i]->sh; int d = k ? 0 : 1;
        lv_obj_set_style_text_align(l, left ? LV_TEXT_ALIGN_LEFT : LV_TEXT_ALIGN_CENTER, 0);
        if(left) lv_obj_align(l, LV_ALIGN_TOP_LEFT, 14 + d, Y[i] + d); else lv_obj_align(l, LV_ALIGN_TOP_MID, d, Y[i] + d);
    }
}
/* the top of Music: a tap on the clock opens Weather; a hold shows or hides the clock and the status icons
 * (the same switch as Disco Options > Music Screen > Show Clock). When hidden, an empty strip keeps the hold. */
static lv_obj_t *g_top_hit;
static void wx_cb(lv_event_t *e){
    lv_event_code_t c = lv_event_get_code(e);
    if(c == LV_EVENT_SHORT_CLICKED && lv_event_get_target(e) == g_pill) weather_app_open();   /* not CLICKED: that follows a hold */
    else if(c == LV_EVENT_LONG_PRESSED){
        int on = g_pill && lv_obj_has_flag(g_pill, LV_OBJ_FLAG_HIDDEN);   /* what is on screen decides, not a save that may have failed */
        cfg_set_int("disco_clock", on); dhome_show_clock(on);
    }
}

static void mode_paint(void){
    int wm = cfg_get_int("work_mode", 0);
    static const char *const G[5] = { LV_SYMBOL_RIGHT, LV_SYMBOL_SHUFFLE, LV_SYMBOL_LOOP "1", LV_SYMBOL_LOOP, LV_SYMBOL_RIGHT "1" };
    dl_text(&L_MODE, G[wm >= 0 && wm < 5 ? wm : 0]);
}
static void mode_cb(lv_event_t *e){ if(lv_event_get_code(e) == LV_EVENT_CLICKED){ ui_np_mode_next(); mode_paint(); } }   /* toasts the new mode */
/* ---- build ---------------------------------------------------------------------------------------- */
void dhome_create(lv_obj_t *root){
    g_acc = ui_current_accent();
    lv_obj_set_style_bg_color(root, TC(CANVAS), 0); lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    g_cover = lv_image_create(root);                                  /* the sharp cover (364 px, centred) */
    lv_obj_set_pos(g_cover, -2, -2);
    lv_obj_clear_flag(g_cover, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(g_cover, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *tint = lv_obj_create(root); lv_obj_remove_style_all(tint);   /* the whole cover a step darker (light: lighter) */
    lv_obj_set_size(tint, 360, 360); lv_obj_set_style_bg_color(tint, TC(CANVAS), 0); lv_obj_set_style_bg_opa(tint, TINT_OPA, 0);
    lv_obj_clear_flag(tint, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    g_wash = lv_obj_create(root); lv_obj_remove_style_all(g_wash);  /* a soft wash where the text sits */
    lv_obj_set_pos(g_wash, 0, 96); lv_obj_set_size(g_wash, 360, 264);   /* full strength from the title down (set per album) */
    lv_obj_set_style_bg_color(g_wash, TC(CANVAS), 0); lv_obj_set_style_bg_grad_color(g_wash, TC(CANVAS), 0);
    lv_obj_set_style_bg_grad_dir(g_wash, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_main_opa(g_wash, 0, 0); lv_obj_set_style_bg_grad_opa(g_wash, theme_variant() ? 200 : 150, 0);
    lv_obj_set_style_bg_main_stop(g_wash, 0, 0); lv_obj_set_style_bg_grad_stop(g_wash, 40, 0);
    lv_obj_set_style_bg_opa(g_wash, LV_OPA_COVER, 0);
    lv_obj_clear_flag(g_wash, LV_OBJ_FLAG_CLICKABLE);
    /* the clock: Wi-Fi / Bluetooth / battery above it, a small weather icon + temperature below (no pill behind) */
    g_pill = lv_obj_create(root); lv_obj_remove_style_all(g_pill);
    lv_obj_set_size(g_pill, 200, 110); lv_obj_align(g_pill, LV_ALIGN_TOP_MID, 0, 14);
    lv_obj_clear_flag(g_pill, LV_OBJ_FLAG_SCROLLABLE);
    dl_make(g_pill, &L_STAT, TF(UI_14), -1, 0, LV_ALIGN_TOP_MID, 0, 4, 1);
    g_time = dl_make(g_pill, &L_TIME, TF(UI_46), -1, 0, LV_ALIGN_TOP_MID, 0, 22, 0);
    dl_text(&L_TIME, "--:--");
    g_wfont = *theme_font_original(18); g_wfont.fallback = &font_weather16;
    g_wx = dl_make(g_pill, &L_WX, &g_wfont, -1, 0, LV_ALIGN_TOP_MID, 0, 78, 1);
    g_top_hit = lv_obj_create(root); lv_obj_remove_style_all(g_top_hit);   /* under the clock: holds while it's hidden */
    lv_obj_set_size(g_top_hit, 200, 110); lv_obj_align(g_top_hit, LV_ALIGN_TOP_MID, 0, 14);
    lv_obj_add_flag(g_top_hit, LV_OBJ_FLAG_CLICKABLE); lv_obj_clear_flag(g_top_hit, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(g_top_hit, wx_cb, LV_EVENT_LONG_PRESSED, NULL);
    lv_obj_move_to_index(g_top_hit, lv_obj_get_index(g_pill));         /* just behind the clock */
    lv_obj_add_flag(g_pill, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(g_pill, wx_cb, LV_EVENT_SHORT_CLICKED, NULL); lv_obj_add_event_cb(g_pill, wx_cb, LV_EVENT_LONG_PRESSED, NULL);
    if(!cfg_get_int("disco_clock", 1)) lv_obj_add_flag(g_pill, LV_OBJ_FLAG_HIDDEN);
    /* title + artist across the wide middle; a tap opens the track overlay */
    g_trk = lv_obj_create(root); lv_obj_remove_style_all(g_trk);
    lv_obj_set_pos(g_trk, 30, 136); lv_obj_set_size(g_trk, 300, 66);
    lv_obj_add_flag(g_trk, LV_OBJ_FLAG_CLICKABLE); lv_obj_add_event_cb(g_trk, title_cb, LV_EVENT_ALL, NULL);
    g_title = dl_make(g_trk, &L_TITLE, ui_font_cjk(28), 272, 36, LV_ALIGN_TOP_MID, 0, 0, 0);
    dl_text(&L_TITLE, "Not Playing");
    g_artist = dl_make(g_trk, &L_ARTIST, ui_font_cjk(20), 252, 26, LV_ALIGN_TOP_MID, 0, 38, 1);
    dl_scroll5(&L_TITLE); dl_scroll5(&L_ARTIST);
    dhome_title_apply();
    underline_create(root);
    dl_make(root, &L_EL, TF(UI_12), -1, 0, LV_ALIGN_TOP_LEFT, UL_X, UL_Y + 9, 1);
    dl_make(root, &L_RM, TF(UI_12), -1, 0, LV_ALIGN_TOP_RIGHT, -(360 - UL_X - UL_W), UL_Y + 9, 1);
    dhome_times_apply();                                            /* the seekable rainbow progress under them */
    /* previous / play-pause / next below, on an arc centred on the screen's middle */
    static const int ANG[3] = { 139, 90, 41 }, RAD[3] = { 134, 136, 134 }, DIA[3] = { 56, 60, 56 };   /* prev/next out to the sides, play/pause low (the bottom edge is safe here) */
    static const char *const CMD[3] = { "0201000C0002", "0201000C0000", "0201000C0001" };
    static const char *const GL[3] = { LV_SYMBOL_PREV, LV_SYMBOL_PLAY, LV_SYMBOL_NEXT };
    for(int k = 0; k < 3; k++){
        float a = ANG[k] * 0.0174533f; int R = RAD[k];
        g_btn[k] = glass_circle(root, 180 + (int)lroundf(R * cosf(a)), 180 + (int)lroundf(R * sinf(a)), DIA[k], GL[k]);
        lv_obj_set_style_bg_opa(g_btn[k], 51, 0);                         /* 80% see-through */
        lv_obj_set_style_text_font(lv_obj_get_child(g_btn[k], 0), k == 1 ? TF(UI_24) : TF(UI_20), 0);
        g_btn_lbl[k] = lv_obj_get_child(g_btn[k], 0);
        lv_obj_add_event_cb(g_btn[k], transport_cb, LV_EVENT_CLICKED, (void *)CMD[k]);
    }
    /* the play mode: a small icon, no background, above play/pause; a tap cycles the modes (with a toast) */
    g_mode = lv_obj_create(root); lv_obj_remove_style_all(g_mode);
    lv_obj_set_size(g_mode, 64, 40); lv_obj_set_pos(g_mode, 180 - 32, 240);   /* a roomy target between them */
    lv_obj_add_flag(g_mode, LV_OBJ_FLAG_CLICKABLE); lv_obj_set_ext_click_area(g_mode, 6);
    lv_obj_clear_flag(g_mode, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(g_mode, mode_cb, LV_EVENT_CLICKED, NULL);
    dl_make(g_mode, &L_MODE, TF(UI_18), -1, 0, LV_ALIGN_CENTER, 0, 0, 0);
    kit_keep(g_mode); lv_obj_add_flag(g_mode, LV_OBJ_FLAG_USER_2);     /* no button styling: just the icon */
    mode_paint();
    g_tick = lv_timer_create(prog_tick, 1000, NULL);
    { lv_timer_t *it = lv_timer_create(idle_timer_cb, 10200, NULL); if(it) lv_timer_set_repeat_count(it, 1); }   /* idle 10 s after start */
    dhome_art_changed();
}

/* ---- setters (home.c forwards) ------------------------------------------------------------------- */
void dhome_set_clock(const char *t, const char *sub){
    (void)sub;
    dl_text(&L_TIME, t);
}
/* "<icon>  14°C  Partly cloudy" -> "<icon> 14°" */
void dhome_set_weather(const char *s){
    if(!g_wx) return;
    char o[48] = ""; int n = 0;
    while(s && *s == ' ') s++;
    for(int i = 0; s && s[i] && n < (int)sizeof o - 1; i++){
        if(s[i] == ' ' && s[i + 1] == ' ' && n > 0 && strchr(o, 0xB0)) break;          /* stop before the condition */
        if(s[i] == ' ' && n > 0 && o[n - 1] == ' ') continue;
        if((s[i] == 'C' || s[i] == 'F') && n > 1 && (unsigned char)o[n - 1] == 0xB0) continue;
        o[n++] = s[i]; o[n] = 0;
    }
    dl_text(&L_WX, o);
}
void dhome_set_status(int batt, int charging, int wifi, int bt){
    char b[64] = ""; size_t n = 0;                                       /* the Disc's usual status row: Wi-Fi, BT, battery */
    if(wifi) n += (size_t)snprintf(b + n, sizeof b - n, LV_SYMBOL_WIFI "  ");
    if(bt)   n += (size_t)snprintf(b + n, sizeof b - n, LV_SYMBOL_BLUETOOTH "  ");
    const char *bs = charging ? LV_SYMBOL_CHARGE : batt >= 90 ? LV_SYMBOL_BATTERY_FULL : batt >= 65 ? LV_SYMBOL_BATTERY_3 :
                     batt >= 40 ? LV_SYMBOL_BATTERY_2 : batt >= 15 ? LV_SYMBOL_BATTERY_1 : LV_SYMBOL_BATTERY_EMPTY;
    if(batt >= 0) snprintf(b + n, sizeof b - n, "%s", bs);
    dl_text(&L_STAT, b);
}
void dhome_set_now_playing(const char *title, const char *artist, lv_color_t accent, bool playing){
    g_have = title != NULL;
    g_acc = ui_disco_fit_accent(accent);
    if(!(g_idle && !title)){ dl_text(&L_TITLE, title ? title : "Not Playing"); dl_text(&L_ARTIST, artist ? artist : ""); }
    if(g_mode) mode_paint();                                            /* changed elsewhere (Now Playing, Quick Settings) */
    static int last_play = -1;                                          /* this runs on every position frame: touch only what changed */
    if(g_btn_lbl[1] && last_play != (int)playing){ last_play = playing; lv_label_set_text(g_btn_lbl[1], playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY); }
    if(!title && g_ul_fill){ ul_set(0); pa_set(0); }
    idle_eval();
    static lv_color_t last_acc; static int acc_set;
    if(!acc_set || !lv_color_eq(last_acc, g_acc)){ acc_set = 1; last_acc = g_acc; acc_paint(); }
}
void dhome_set_accent(lv_color_t accent){
    g_acc = ui_disco_fit_accent(accent); acc_paint();
}
/* Settings > Display > Progress Bar: 0 Disco (rainbow), 1 Accent, 2 Off */
void disco_progress_apply(void){
    if(!g_ul_fill) return;
    dhome_times_apply();
    int m = cfg_get_int("disco_progress", 0); if(m < 0 || m > 2) m = 0;
    if(g_idle) m = 2;                                                  /* idle: no progress at all */
    int arc = pa_on();                                                 /* Progress Shape: Linear or Arc */
    lv_obj_t *all[4] = { g_ul_tr, g_ul_fill, g_ul_knob, g_ul_hit };
    for(int i = 0; i < 4; i++) if(all[i]){ if(m == 2 || arc) lv_obj_add_flag(all[i], LV_OBJ_FLAG_HIDDEN); else lv_obj_remove_flag(all[i], LV_OBJ_FLAG_HIDDEN); }
    lv_obj_t *pa[3] = { g_pa_tr, g_pa_knob, g_pa_hit };
    for(int i = 0; i < 3; i++) if(pa[i]){ if(m == 2 || !arc) lv_obj_add_flag(pa[i], LV_OBJ_FLAG_HIDDEN); else lv_obj_remove_flag(pa[i], LV_OBJ_FLAG_HIDDEN); }
    if(g_pa_tr){
        if(m == 2 || !arc){ for(int i = 0; i < PA_N; i++) lv_obj_add_flag(g_pa_seg[i], LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(g_pa_cap, LV_OBJ_FLAG_HIDDEN); }
        else { int f = g_pa_frac; g_pa_deg = -1; pa_set(f); }
        pa_paint(m);
    }
    if(m == 1) lv_obj_remove_flag(g_ul_solid, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(g_ul_solid, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(g_ul_solid, g_acc, 0);
}
/* the accent (cover colour or a picked one) on the transport glyphs + a soft ring, the play-mode icon, the overlay */
static void acc_paint(void){
    for(int k = 0; k < 3; k++) if(g_btn[k]){
        lv_obj_set_style_border_color(g_btn[k], g_acc, 0); lv_obj_set_style_border_opa(g_btn[k], 140, 0);
        if(g_btn_lbl[k]) lv_obj_set_style_text_color(g_btn_lbl[k], g_acc, 0);
    }
    if(L_MODE.fg) lv_obj_set_style_text_color(L_MODE.fg, g_acc, 0);
    if(g_ul_solid) lv_obj_set_style_bg_color(g_ul_solid, g_acc, 0);
    if(g_pa_tr) pa_paint(cfg_get_int("disco_progress", 0));
    if(g_ov) ov_paint();
}
void dhome_test_top_hold(void){ lv_obj_t *t = (g_pill && !lv_obj_has_flag(g_pill, LV_OBJ_FLAG_HIDDEN)) ? g_pill : g_top_hit; if(t) lv_obj_send_event(t, LV_EVENT_LONG_PRESSED, NULL); }   /* host tests */
void dhome_show_clock(int on){ if(g_pill){ if(on) lv_obj_remove_flag(g_pill, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(g_pill, LV_OBJ_FLAG_HIDDEN); } }
/* how light the picture is behind a band of rows (0..255), the wash over it included */
static int band_luma(const lv_image_dsc_t *d, int y0, int y1, int wash_opa, int wash_l){
    int sum = 0, n = 0;
    if(d && d->data && d->header.cf == LV_COLOR_FORMAT_RGB888){
        int w = d->header.w, st = d->header.stride ? (int)d->header.stride : w * 3;
        for(int y = y0; y < y1; y += 4) for(int x = 70; x < 290; x += 6){
            int yy = y + 2, xx = x + 2; if(yy < 0 || yy >= (int)d->header.h || xx >= w) continue;
            const uint8_t *p = d->data + (size_t)yy * st + (size_t)xx * 3;   /* B, G, R */
            sum += (p[2] * 77 + p[1] * 150 + p[0] * 29) >> 8; n++;
        }
    }
    int l = n ? sum / n : wash_l;
    return (l * (255 - wash_opa) + wash_l * wash_opa) / 255;
}
/* Text contrast. Clock area (no wash): ink picked from the cover's brightness, with the shadow twin. Title / artist:
 * ink follows the theme (light: dark text, dark: white text) and the wash behind them is made just strong enough for
 * this cover to keep that ink readable - so a white cover on the dark theme gets more shade, a dark cover on the light
 * theme more fog, and the text never ends up white on a white fade. */
static void ink_all(const lv_image_dsc_t *d){
    uint32_t c = theme_rgb(THEME_CLR_CANVAS);
    int cl = (int)(((c >> 16 & 255) * 77 + (c >> 8 & 255) * 150 + (c & 255) * 29) >> 8);
    int light = theme_variant();
    int top = band_luma(d, 14, 124, TINT_OPA, cl) > 150;
    int l = band_luma(d, 136, 202, TINT_OPA, cl);                         /* the cover under the title, tint included */
    int target = light ? 185 : 75, w = light ? 140 : 110;               /* readable background brightness; least wash */
    if(light ? l < target : l > target){
        int d2 = light ? cl - l : l - cl;
        if(d2 > 0){ int need = 255 * (light ? target - l : l - target) / d2; if(need > w) w = need; }
        else w = 235;
    }
    if(w > 235) w = 235;
    if(g_wash) lv_obj_set_style_bg_grad_opa(g_wash, (lv_opa_t)w, 0);
    dl_ink(&L_STAT, top); dl_ink(&L_TIME, top); dl_ink(&L_WX, top);
    dl_ink(&L_TITLE, light); dl_ink(&L_ARTIST, light); dl_ink(&L_MODE, light); dl_ink(&L_EL, light); dl_ink(&L_RM, light);
    acc_paint();                                                          /* the mode icon keeps the accent, with the shadow */
}
void dhome_art_changed(void){
    if(!g_cover) return;
    const void *src = ui_current_sharp_img();
    if(g_idle) src = disco_idle_art();                                  /* nothing playing: the generated CD (not a stale cover) */
    ink_all((const lv_image_dsc_t *)src);
    lv_image_set_src(g_cover, NULL);                                   /* same buffer, new pixels: force a redraw */
    if(src){ lv_image_set_src(g_cover, src); lv_obj_remove_flag(g_cover, LV_OBJ_FLAG_HIDDEN); }
    else lv_obj_add_flag(g_cover, LV_OBJ_FLAG_HIDDEN);
}
