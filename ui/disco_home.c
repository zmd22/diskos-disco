#include "scrub_math.h"
#include "simple_theme.h"
#include "onboarding.h"
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Music / Home in the Disco theme (fork): the playing album's cover, sharp, across the whole screen; the clock with a
 * small weather icon and the temperature under it in a glass pill at the top (Settings > Display > Show Clock); the
 * title and artist next to the menu picker; previous / play-pause / next on an arc that runs with the progress arc on
 * the lower-right rim. Title and artist taps open their Library pages; More opens the track actions.
 * Nothing moves while idle: the progress arc is updated once a second, only while Music is on screen. home.c forwards
 * its setters here when th_disco_family(). */
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
#include "lvgl/src/core/lv_obj_event_private.h"   /* lv_hit_test_info_t: the gesture zone leaves the Arc ring alone */

extern const lv_font_t font_theme_20, font_theme_24;
extern const lv_font_t font_weather16;
extern const lv_font_t font_weather20;
void dhome_clockinfo_apply(void);

/* text over the cover: every label has a twin one pixel down-right in the opposite colour (a soft shadow), and the pair
 * flips between light-on-dark and dark-on-light with the brightness of the cover behind it (decided once per album) */
typedef struct { lv_obj_t *sh, *fg; int dim, scroll, ink; char cur[192]; } dlabel_t;   /* ink: last dark/light (-1 unset) */   /* cur: the text as set (an ellipsis edits the label's own copy) */
/* Long Music metadata scrolls three times, then rests until the next track. */
static void scroll_done(lv_anim_t *a){ lv_label_set_long_mode((lv_obj_t *)a->var, LV_LABEL_LONG_CLIP); }
const lv_anim_t *disco_scroll5(void){
    static lv_anim_t t; static int init;
    if(!init){ lv_anim_init(&t); lv_anim_set_repeat_count(&t, 3); lv_anim_set_repeat_delay(&t, 1200); lv_anim_set_completed_cb(&t, scroll_done); init = 1; }
    return &t;
}
static void dl_scroll5(dlabel_t *d){
    if(th_simple())return;
    d->scroll = 1;
    lv_obj_t *l2[2] = { d->sh, d->fg };
    for(int k = 0; k < 2; k++){ lv_obj_set_style_anim(l2[k], disco_scroll5(), 0); lv_label_set_long_mode(l2[k], LV_LABEL_LONG_SCROLL_CIRCULAR); }
}
static dlabel_t L_TIME, L_WX, L_STAT, L_TITLE, L_ARTIST, L_MODE, L_EL, L_RM;   /* EL/RM: elapsed / remaining under the line */
static lv_obj_t *g_mode, *g_more;
static void mode_paint(void);
void disco_progress_apply(void);
static void acc_paint(void);
static lv_obj_t *dl_make(lv_obj_t *par, dlabel_t *d, const lv_font_t *f, int w, int h, lv_align_t al, int x, int y, int dim){
    d->dim = dim;
    for(int k = 0; k < 2; k++){
        lv_obj_t *l = lv_label_create(par); lv_label_set_text(l, "");
        lv_obj_set_style_text_font(l, f, 0); lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        if(w > 0){ lv_label_set_long_mode(l, LV_LABEL_LONG_SCROLL_CIRCULAR); lv_obj_set_size(l, w, h); }
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
    if(d->scroll){ ui_music_scroll(d->fg);ui_music_scroll(d->sh); }
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

/* ---- Settings > Disco Options > Music Screen > Controls: 0 Buttons, 1 Auto-hide, 2 Gestures -----------------------
 * Auto-hide: the buttons and the play mode fade out 5 s after the last touch; the next touch anywhere brings them
 * back (a touch where they were only shows them, it presses nothing). Gestures: no buttons; the band below the line is
 * a touch zone - double tap play/pause, swipe right next, swipe left previous - with faint hints along the bottom and
 * a glyph that flashes for what was done. The play mode stays in the track overlay. */
#define GZ_X 44
#define GZ_Y 238
#define GZ_W 272
#define GZ_H 112
#define SIMPLE_SEEK_Y 265
#define SIMPLE_TEXT_Y 74      /* Squash Music: where the title line ends and the artist line starts, inside the text field */
static lv_obj_t *g_gz, *g_gz_hint[4], *g_gz_fx;
static lv_obj_t *simple_play_state, *simple_mode_state;
static void simple_music_indicators(void);
static lv_obj_t *g_ov;                                                  /* the track overlay (below) */
static int g_scrub;                                                     /* the line is being dragged (below) */
static lv_timer_t *g_ctl_tmr;
static int g_ctl_shown = 1, g_gz_px, g_gz_py; static uint32_t g_gz_t0, g_gz_last;
static int ctl_mode(void){ if(th_simple())return 2; return simple_cfg_get_int("disco_controls", 0); }
static void ctl_fade_done(lv_anim_t *a){ lv_obj_t *o = a->var; if(!g_ctl_shown) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN); }
static void ctl_opa_cb(void *o, int32_t v){ lv_obj_set_style_opa(o, (lv_opa_t)v, 0); }
static void ctl_fade(lv_obj_t *o, int in){
    if(!o) return;
    lv_anim_delete(o, ctl_opa_cb);
    if(in) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    lv_anim_t a; lv_anim_init(&a); lv_anim_set_var(&a, o); lv_anim_set_exec_cb(&a, ctl_opa_cb);
    lv_anim_set_values(&a, lv_obj_get_style_opa(o, 0), in ? 255 : 0); lv_anim_set_duration(&a, in ? 180 : 400);
    lv_anim_set_completed_cb(&a, ctl_fade_done); lv_anim_start(&a);
}
static void ctl_show(int on){
    if(on == g_ctl_shown) return;
    g_ctl_shown = on;
    for(int k = 0; k < 3; k++) ctl_fade(g_btn[k], on);
    ctl_fade(g_mode, on);
}
static void ctl_tick(lv_timer_t *t){
    (void)t;
    if(ui_display_is_off() || ctl_mode() != 1 || screen_current() != SCR_HOME) return;
    uint32_t idle = lv_display_get_inactive_time(NULL);
    if(g_ctl_shown && idle >= 5000 && !g_scrub && !g_ov) ctl_show(0);
    else if(!g_ctl_shown && idle < 300) ctl_show(1);                  /* any touch brings them back */
}
static void gz_flash(const char *glyph){
    if(th_simple()){simple_playback_feedback(glyph);return;}
    if(!g_gz_fx) return;
    lv_label_set_text(g_gz_fx, glyph); lv_obj_remove_flag(g_gz_fx, LV_OBJ_FLAG_HIDDEN);
    lv_anim_delete(g_gz_fx, ctl_opa_cb);
    lv_anim_t a; lv_anim_init(&a); lv_anim_set_var(&a, g_gz_fx); lv_anim_set_exec_cb(&a, ctl_opa_cb);
    lv_anim_set_values(&a, 255, 0); lv_anim_set_delay(&a, 250); lv_anim_set_duration(&a, 450); lv_anim_start(&a);
}
static int gz_arc_point(int x, int y){                                 /* on the Arc progress ring: the arc's, not ours */
    if(th_simple()&&simple_cfg_get_int("disco_prog_shape",0)!=1&&simple_cfg_get_int("disco_progress",0)!=2&&y>=SIMPLE_SEEK_Y-16)return 1;
    int dx = x - 180, dy = y - 180;
    return simple_cfg_get_int("disco_prog_shape", 0) == 1 && simple_cfg_get_int("disco_progress", 0) != 2 && dx * dx + dy * dy >= 144 * 144;
}
static void ov_open(void);
static lv_timer_t *simple_tap_timer;
static int simple_gz_moved,simple_gz_consumed;
enum { TRACK_MENU = 2 };
static ui_np_link_press_t track_press, simple_pending;
static lv_point_t track_start;
static int track_armed, track_cancel, track_target, simple_target;
static int track_contains(lv_obj_t *o, const lv_point_t *p){
    if(!o || lv_obj_has_flag(o,LV_OBJ_FLAG_HIDDEN))return 0;
    lv_area_t a;lv_obj_get_coords(o,&a);
    return p->x>=a.x1 && p->x<=a.x2 && p->y>=a.y1 && p->y<=a.y2;
}
static int track_link_at(const lv_point_t *p){
    if(track_contains(L_ARTIST.fg,p))return UI_NP_LINK_ARTIST;
    if(track_contains(L_TITLE.fg,p))return UI_NP_LINK_ALBUM;
    return TRACK_MENU;
}
static void simple_tap_cancel(void){
    if(simple_tap_timer){lv_timer_delete(simple_tap_timer);simple_tap_timer=NULL;}
}
static void track_open(const ui_np_link_press_t *press,int target){
    if(screen_current()!=SCR_HOME || (th_simple() && simple_nav_is_open()) || !ui_np_link_current(press))return;
    if(!press->track.have_track){screen_show(SCR_LIBRARY);return;}
    if(target==TRACK_MENU)ov_open();else ui_np_link_open(press,target);
}
static void simple_tap_done(lv_timer_t*t){
    lv_timer_delete(t);simple_tap_timer=NULL;g_gz_last=0;
    track_open(&simple_pending,simple_target);
}
static void gz_cb(lv_event_t *e){
    lv_event_code_t c = lv_event_get_code(e);
    if(c == LV_EVENT_HIT_TEST){ lv_hit_test_info_t *h = lv_event_get_hit_test_info(e); if(gz_arc_point(h->point->x, h->point->y)) h->res = false; return; }
    lv_indev_t *in = lv_indev_active(); lv_point_t p = { 0, 0 }; if(in) lv_indev_get_point(in, &p);
    if(th_simple()&&c==LV_EVENT_PRESS_LOST){simple_tap_cancel();g_gz_last=0;simple_gz_consumed=1;return;}
    if(th_simple()&&c==LV_EVENT_PRESSING){if(abs(p.x-g_gz_px)>18||abs(p.y-g_gz_py)>18){simple_gz_moved=1;simple_tap_cancel();g_gz_last=0;}return;}
    if(th_simple()&&c==LV_EVENT_LONG_PRESSED){
        simple_tap_cancel();g_gz_last=0;
        if(simple_gz_moved||!g_have||!ui_np_link_current(&track_press))return;
        simple_gz_consumed=1;int hold=simple_cfg_get_int("disco_title_hold",0);if(hold==0)disco_open_np_immersive();else if(hold==1)ui_np_fav_toggle();return;
    }
    if(c == LV_EVENT_PRESSED){
        g_gz_px=p.x;g_gz_py=p.y;g_gz_t0=lv_tick_get();simple_gz_moved=simple_gz_consumed=0;
        if(th_simple()){
            if(simple_tap_timer && (!ui_np_link_current(&simple_pending)||lv_tick_elaps(g_gz_last)>=400)){simple_tap_cancel();g_gz_last=0;}
            else simple_tap_cancel(); /* a second press must finish before a pending single can navigate */
            ui_np_link_capture(&track_press);track_target=track_link_at(&p);
        }
        return;
    }
    if(th_simple()&&c==LV_EVENT_RELEASED){
        if(simple_gz_consumed)return;
        if(g_idle){if(!simple_gz_moved&&lv_tick_elaps(g_gz_t0)<=400)track_open(&track_press,TRACK_MENU);return;}
        if(!ui_np_link_current(&track_press)){simple_tap_cancel();g_gz_last=0;return;}
    }
    if(c != LV_EVENT_RELEASED || !g_have) return;
    int dx = p.x - g_gz_px, dy = p.y - g_gz_py, adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
    uint32_t now = lv_tick_get();
    if(adx >= 40 && adx > 2 * ady && now - g_gz_t0 < 700){            /* a swipe: right = next, left = previous */
        g_gz_last = 0;
        simple_tap_cancel();
        if(dx > 0){ ui_transport_command("0201000C0001"); gz_flash(LV_SYMBOL_NEXT); }
        else      { ui_transport_command("0201000C0002"); gz_flash(LV_SYMBOL_PREV); }
        return;
    }
    if(adx > 18 || ady > 18 || now - g_gz_t0 > 400 || (th_simple() && simple_gz_moved)){simple_tap_cancel();g_gz_last=0;return;}
    if(g_gz_last && now - g_gz_last < 400){                            /* the second tap of a double tap */
        g_gz_last = 0;
        simple_tap_cancel();
        track_state_t st; ipc_get_state(&st);
        gz_flash((th_simple()?ui_is_playing():st.state==2)?LV_SYMBOL_PAUSE:LV_SYMBOL_PLAY);
        ui_transport_command("0201000C0000");
    } else {g_gz_last=now;if(th_simple()){simple_pending=track_press;simple_target=track_target;simple_tap_timer=lv_timer_create(simple_tap_done,410,NULL);}}
}
static void gz_create(lv_obj_t *root){
    g_gz = lv_obj_create(root); lv_obj_remove_style_all(g_gz);
    lv_obj_set_pos(g_gz, disco_mirror() ? 360 - GZ_X - GZ_W : GZ_X, GZ_Y); lv_obj_set_size(g_gz, GZ_W, GZ_H);
    lv_obj_add_flag(g_gz, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_ADV_HITTEST); lv_obj_clear_flag(g_gz, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(g_gz, gz_cb, LV_EVENT_ALL, NULL);
    kit_keep(g_gz); lv_obj_add_flag(g_gz, LV_OBJ_FLAG_USER_2);
    /* the hints, faint, along the bottom: previous at the left, the double tap in the middle, next at the right */
    static const char *const H[3] = { LV_SYMBOL_LEFT "  " LV_SYMBOL_PREV, LV_SYMBOL_PAUSE, LV_SYMBOL_NEXT "  " LV_SYMBOL_RIGHT };
    static const int HX[3] = { 96, 180, 264 }, HY[3] = { 298, 324, 298 };
    for(int k = 0; k < 3; k++){
        lv_obj_t *l = g_gz_hint[k] = lv_label_create(root); lv_label_set_text(l, H[k]);
        lv_obj_set_style_text_font(l, TF(UI_14), 0); lv_obj_set_style_text_color(l, TC(TEXT_PRIMARY), 0);
        lv_obj_set_style_text_opa(l, 140, 0);
        lv_obj_update_layout(l);
        lv_obj_set_pos(l, HX[k] - lv_obj_get_width(l) / 2, HY[k] - lv_obj_get_height(l) / 2);
        lv_obj_clear_flag(l, LV_OBJ_FLAG_CLICKABLE); kit_keep(l);
    }
    { lv_obj_t *dots = g_gz_hint[3] = lv_obj_create(root); lv_obj_remove_style_all(dots);   /* two dots: tap twice */
      lv_obj_set_size(dots, 16, 5); lv_obj_set_pos(dots, 172, 304); lv_obj_clear_flag(dots, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
      for(int k = 0; k < 2; k++){ lv_obj_t *d = lv_obj_create(dots); lv_obj_remove_style_all(d); lv_obj_set_size(d, 5, 5);
          lv_obj_set_pos(d, k * 11, 0); lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
          lv_obj_set_style_bg_color(d, TC(TEXT_PRIMARY), 0); lv_obj_set_style_bg_opa(d, 140, 0); lv_obj_clear_flag(d, LV_OBJ_FLAG_CLICKABLE); }
      kit_keep(dots); lv_obj_add_flag(dots, LV_OBJ_FLAG_USER_2); }
    g_gz_fx = lv_label_create(root); lv_obj_set_style_text_font(g_gz_fx, TF(UI_34), 0);   /* what was done, flashed */
    lv_obj_set_style_text_color(g_gz_fx, g_acc, 0); lv_obj_set_width(g_gz_fx, 80);
    lv_obj_set_style_text_align(g_gz_fx, LV_TEXT_ALIGN_CENTER, 0); lv_obj_set_pos(g_gz_fx, 140, 250);
    lv_obj_clear_flag(g_gz_fx, LV_OBJ_FLAG_CLICKABLE); kit_keep(g_gz_fx); lv_obj_add_flag(g_gz_fx, LV_OBJ_FLAG_HIDDEN);
}
void dhome_controls_apply(void){
    if(!g_btn[0]) return;
    int m = ctl_mode();
    for(int k = 0; k < 3; k++){ lv_anim_delete(g_btn[k], ctl_opa_cb); lv_obj_set_style_opa(g_btn[k], 255, 0); }
    lv_anim_delete(g_mode, ctl_opa_cb); lv_obj_set_style_opa(g_mode, 255, 0);
    g_ctl_shown = 1;
    for(int k = 0; k < 3; k++){ if(m == 2 || g_idle) lv_obj_add_flag(g_btn[k], LV_OBJ_FLAG_HIDDEN); else lv_obj_remove_flag(g_btn[k], LV_OBJ_FLAG_HIDDEN); }   /* idle: the pills stand there */
    if(m == 2 || g_idle) lv_obj_add_flag(g_mode, LV_OBJ_FLAG_HIDDEN); else lv_obj_remove_flag(g_mode, LV_OBJ_FLAG_HIDDEN);
    if(m == 2 && !g_gz) gz_create(lv_obj_get_parent(g_btn[0]));
    if(g_gz){
        lv_obj_t *o[5] = { g_gz, g_gz_hint[0], g_gz_hint[1], g_gz_hint[2], g_gz_hint[3] };
        for(int i = 0; i < 5; i++) if(o[i]){ if(m == 2) lv_obj_remove_flag(o[i], LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(o[i], LV_OBJ_FLAG_HIDDEN); }
    }
    if(m == 1 && !g_ctl_tmr) g_ctl_tmr = lv_timer_create(ctl_tick, 250, NULL);
}
/* main.c asks: is a press here the gesture zone's? (a swipe there must not open Shortcuts) */
static int gz_owns(int x, int y){ if(th_simple())return g_gz&&!lv_obj_has_flag(g_gz,LV_OBJ_FLAG_HIDDEN)&&x>=lv_obj_get_x(g_gz)&&x<lv_obj_get_x(g_gz)+lv_obj_get_width(g_gz)&&y>=lv_obj_get_y(g_gz)&&y<lv_obj_get_y(g_gz)+lv_obj_get_height(g_gz)&&!gz_arc_point(x,y); return g_gz && ctl_mode() == 2 && !lv_obj_has_flag(g_gz, LV_OBJ_FLAG_HIDDEN)
                                         && x >= lv_obj_get_x(g_gz) && x < lv_obj_get_x(g_gz) + GZ_W && y >= GZ_Y && y < GZ_Y + GZ_H && !gz_arc_point(x, y); }
/* ---- the progress underline: a rainbow (the CD's sheen) that fills from left to right; drag or tap it to seek ---- */
/* wider now that a swipe can't press - from near the free edge to just short of the closed shard */
#define UL_W 276
#define UL_X (disco_mirror() ? 360 - 34 - UL_W : 34)
#define UL_Y (th_simple()?SIMPLE_SEEK_Y:214)
static lv_obj_t *g_ul_fill, *g_ul_knob, *g_ul_tr, *g_ul_hit, *g_ul_solid;
static int g_frac = -1, g_scrub;
/* hold the line (or the arc) to scrub: a large time over the controls follows the finger, the head grows */
static lv_obj_t *g_zb, *g_zb_t, *g_zb_d, *g_pa_knob;
static void zoom_show(int on, int frac){
    if(!g_zb) return;
    if(!on){ lv_obj_add_flag(g_zb, LV_OBJ_FLAG_HIDDEN);
             if(g_ul_knob){ lv_obj_set_size(g_ul_knob, 10, 10); lv_obj_set_y(g_ul_knob, UL_Y - 3); }
             if(g_pa_knob) lv_obj_set_style_outline_width(g_pa_knob, 3, 0); return; }
    track_state_t st; ipc_get_state(&st);
    if(!st.have_track || st.duration_ms <= 0) return;
    char a[16], b[24]; long pos = (long)((long long)st.duration_ms * frac / 1000);
    long sp = pos / 1000, sd = st.duration_ms / 1000;
    if(sp >= 3600) snprintf(a, sizeof a, "%ld:%02ld:%02ld", sp / 3600, sp / 60 % 60, sp % 60); else snprintf(a, sizeof a, "%ld:%02ld", sp / 60, sp % 60);
    if(sd >= 3600) snprintf(b, sizeof b, "of %ld:%02ld:%02ld", sd / 3600, sd / 60 % 60, sd % 60); else snprintf(b, sizeof b, "of %ld:%02ld", sd / 60, sd % 60);
    lv_label_set_text(g_zb_t, a); lv_label_set_text(g_zb_d, b);
    if(lv_obj_has_flag(g_zb, LV_OBJ_FLAG_HIDDEN)){
        lv_obj_remove_flag(g_zb, LV_OBJ_FLAG_HIDDEN); lv_obj_move_foreground(g_zb);
        if(g_ul_knob && simple_cfg_get_int("disco_prog_shape", 0) != 1){ lv_obj_set_size(g_ul_knob, 16, 16); lv_obj_set_y(g_ul_knob, UL_Y - 6); }
        if(g_pa_knob) lv_obj_set_style_outline_width(g_pa_knob, 6, 0);
    }
    if(g_ul_knob && simple_cfg_get_int("disco_prog_shape", 0) != 1) lv_obj_set_x(g_ul_knob, UL_X + UL_W * frac / 1000 - 8);
}
static void zoom_create(lv_obj_t *root){
    g_zb = lv_obj_create(root); lv_obj_remove_style_all(g_zb);
    lv_obj_set_size(g_zb, 150, 74); lv_obj_set_pos(g_zb, 105, 238);
    lv_obj_set_style_radius(g_zb, 22, 0); lv_obj_set_style_bg_color(g_zb, TC(SURFACE), 0); lv_obj_set_style_bg_opa(g_zb, 225, 0);
    lv_obj_set_style_border_width(g_zb, 1, 0); lv_obj_set_style_border_color(g_zb, TC(TEXT_PRIMARY), 0); lv_obj_set_style_border_opa(g_zb, 50, 0);
    lv_obj_clear_flag(g_zb, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    g_zb_t = lv_label_create(g_zb); lv_obj_set_style_text_font(g_zb_t, TF(UI_34), 0);
    lv_obj_set_style_text_color(g_zb_t, TC(TEXT_PRIMARY), 0); lv_obj_align(g_zb_t, LV_ALIGN_TOP_MID, 0, 6);
    g_zb_d = lv_label_create(g_zb); lv_obj_set_style_text_font(g_zb_d, TF(UI_14), 0);
    lv_obj_set_style_text_color(g_zb_d, TC(TEXT_SECONDARY), 0); lv_obj_align(g_zb_d, LV_ALIGN_BOTTOM_MID, 0, -6);
    kit_keep(g_zb);
    lv_obj_add_flag(g_zb, LV_OBJ_FLAG_HIDDEN);
}
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
    if(dur > 0){ fmt_t(a, sizeof a, pos, 0); fmt_t(b, sizeof b, th_simple()?dur:dur-pos, th_simple()?0:1); } else { a[0] = 0; b[0] = 0; }
    dl_text(&L_EL, a); dl_text(&L_RM, b);
    if(th_simple())simple_music_indicators();
}
void dhome_times_apply(void){
    if(!L_EL.fg) return;
    int on = !g_idle && simple_cfg_get_int("disco_times", 1) && simple_cfg_get_int("disco_progress", 0) != 2;   /* no line, no times (idle: neither) */
    lv_obj_t *o[4] = { L_EL.sh, L_EL.fg, L_RM.sh, L_RM.fg };
    for(int i = 0; i < 4; i++){ if(on) lv_obj_remove_flag(o[i], LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(o[i], LV_OBJ_FLAG_HIDDEN); }
    if(th_simple()){
        simple_music_indicators();
        for(int k=0;k<4;k++){lv_obj_set_align(o[k],LV_ALIGN_TOP_LEFT);lv_obj_set_pos(o[k],k<2?36:234,278);lv_obj_set_size(o[k],90,24);if(!(k&1))lv_obj_add_flag(o[k],LV_OBJ_FLAG_HIDDEN);}return;
    }
    int arc = simple_cfg_get_int("disco_prog_shape", 0) == 1;                 /* Arc: no line, the two times side by side */
    for(int k = 0; k < 2; k++){
        int d = k ? 0 : 1;
        if(arc){ lv_obj_align(o[k], LV_ALIGN_TOP_RIGHT, -(180 + 8) + d, UL_Y + d); lv_obj_align(o[2 + k], LV_ALIGN_TOP_LEFT, 180 + 8 + d, UL_Y + d); }
        else { lv_obj_align(o[k], LV_ALIGN_TOP_LEFT, UL_X + d, UL_Y + 9 + d); lv_obj_align(o[2 + k], LV_ALIGN_TOP_RIGHT, -(360 - UL_X - UL_W) + d, UL_Y + 9 + d); }
    }
}
static void prog_tick(lv_timer_t *t){
    (void)t;
    if(ui_display_is_off() || !g_ul_fill || g_scrub || screen_current() != SCR_HOME) return;
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
    if(c == LV_EVENT_LONG_PRESSED){ zoom_show(1, f < 0 ? 0 : f > 1000 ? 1000 : f); return; }
    if(c == LV_EVENT_PRESSED || c == LV_EVENT_PRESSING){
        g_scrub = 1; ul_set(f);
        if(g_zb && !lv_obj_has_flag(g_zb, LV_OBJ_FLAG_HIDDEN)) zoom_show(1, g_frac);
        track_state_t st; ipc_get_state(&st);                          /* the times follow the finger */
        if(st.have_track && st.duration_ms > 0) times_paint((long)((long long)st.duration_ms * g_frac / 1000), st.duration_ms);
        return;
    }
    if(c == LV_EVENT_RELEASED || c == LV_EVENT_PRESS_LOST){
        g_scrub = 0; zoom_show(0, 0);
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
#define PA_A0 (th_simple()?135:(disco_mirror() ? 220 : 40))                            /* Disco Left: from just above the shard (upper left), still clockwise */
#define PA_WRAP(d) ((d) % 360)                                          /* Left: the arc runs past 360 degrees */
#define PA_SPAN (th_simple()?270:280)
#define PA_N 6
#define PA_D 344                                                       /* the drawn ring: r 172 */
#define PA_W 5
static lv_obj_t *g_pa_tr, *g_pa_seg[PA_N], *g_pa_cap, *g_pa_hit;
static int g_pa_deg = -1, g_pa_scrub, g_pa_frac;
static int pa_on(void){ return simple_cfg_get_int("disco_prog_shape", 0) == 1; }
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
    if(!pa_on() || simple_cfg_get_int("disco_progress", 0) == 2) return;   /* Linear or Off: the ring stays hidden */
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
static int pa_frac_at(int x,int y){return scrub_arc_fraction(x,y,PA_A0,PA_SPAN);}
static void pa_cb(lv_event_t *e){
    lv_event_code_t c = lv_event_get_code(e);
    if(!g_have){ if(c == LV_EVENT_RELEASED || c == LV_EVENT_PRESS_LOST){ g_scrub = 0; g_pa_scrub = 0; } return; }
    lv_indev_t *in = lv_indev_active(); lv_point_t p = { 0, 0 }; if(in) lv_indev_get_point(in, &p);
    if(c == LV_EVENT_PRESSED){ g_pa_scrub = p.y >= 40; if(!g_pa_scrub) return; }   /* the top edge: Quick Settings */
    if(!g_pa_scrub) return;
    if(c == LV_EVENT_LONG_PRESSED){ zoom_show(1, g_pa_frac); return; }
    if(c == LV_EVENT_PRESSED || c == LV_EVENT_PRESSING){
        g_scrub = 1; pa_set(pa_frac_at(p.x, p.y));
        if(g_zb && !lv_obj_has_flag(g_zb, LV_OBJ_FLAG_HIDDEN)) zoom_show(1, g_pa_frac);
        track_state_t st; ipc_get_state(&st);
        if(st.have_track && st.duration_ms > 0) times_paint((long)((long long)st.duration_ms * g_pa_frac / 1000), st.duration_ms);
        return;
    }
    if(c == LV_EVENT_RELEASED || c == LV_EVENT_PRESS_LOST){
        g_scrub = 0; g_pa_scrub = 0; zoom_show(0, 0);
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
    if(g_ul_fill && th_disco_family() && screen_current() == SCR_HOME && gz_owns(x, y)) return 1;
    if(!g_ul_fill || !th_disco_family() || screen_current() != SCR_HOME || simple_cfg_get_int("disco_progress", 0) == 2) return 0;
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
void dhome_overlay_close(void){simple_tap_cancel();g_gz_last=0;track_armed=0;simple_gz_consumed=1;ov_close();}
int disco_track_overlay_visible(void){ return g_ov != NULL; }
static lv_point_t ov_swipe_start;
static uint32_t ov_swipe_at;
static void ov_swipe_cb(lv_event_t *e){
    lv_indev_t *in=lv_indev_active();if(!in || !g_ov)return;
    lv_point_t p;lv_indev_get_point(in,&p);
    lv_event_code_t code=lv_event_get_code(e);
    if(code==LV_EVENT_PRESSED){ov_swipe_start=p;ov_swipe_at=lv_tick_get();return;}
    if(code!=LV_EVENT_PRESSING && code!=LV_EVENT_RELEASED)return;
    int dx=p.x-ov_swipe_start.x,dy=p.y-ov_swipe_start.y;
    int mirrored=disco_mirror(),edge=mirrored?ov_swipe_start.x>=296:ov_swipe_start.x<=64;
    extern int ui_get_swipe_thresh(void); void ui_back_hint(int y,int dx);
    int inward=mirrored?-dx:dx, sideways=edge && abs(dx)>2*abs(dy);
    /* the same back feedback as everywhere else - the arrow follows the finger and arms at the threshold, and the
     * popup closes on release with the arrow armed (it used to close mid-swipe, with no arrow to see) */
    if(code==LV_EVENT_PRESSING){ ui_back_hint(sideways?p.y:0, sideways?inward:0); return; }
    ui_back_hint(0,0);
    if(sideways && inward>=ui_get_swipe_thresh() && lv_tick_elaps(ov_swipe_at)<700){
        onboarding_discovered(HINT_POPUP);ov_close();
    }
}
static void ov_swipe_bubble(lv_obj_t *o){
    lv_obj_add_flag(o,LV_OBJ_FLAG_EVENT_BUBBLE);
    for(uint32_t i=0;i<lv_obj_get_child_count(o);i++)ov_swipe_bubble(lv_obj_get_child(o,i));
}
static void ov_title_cb(lv_event_t *e){ if(lv_event_get_code(e) == LV_EVENT_CLICKED) ov_close(); }
enum { OV_FAV, OV_ALBUM, OV_ARTIST, OV_QUEUE, OV_LYRICS, OV_INFO, OV_N };
void disco_open_np_immersive(void);
static void ov_mark(int i, int on){
    lv_obj_t *b = g_ov_btn[i], *l = lv_obj_get_child(b, 0);
    lv_obj_set_style_border_width(b, on ? 2 : 1, 0); lv_obj_set_style_border_color(b, g_acc, 0); lv_obj_set_style_border_opa(b, on ? LV_OPA_COVER : 140, 0);
    lv_obj_set_style_border_opa(b, on ? LV_OPA_COVER : 60, 0);
    lv_obj_set_style_text_color(l, i==OV_FAV && !on ? TC(FIXED_MEDIA_WHITE) : g_acc, 0); lv_obj_set_style_bg_color(b, on ? TC(SURFACE_RAISED) : TC(SURFACE), 0);
}
static void ov_place_cap(int i, int x, int y){ lv_obj_update_layout(g_ov_cap[i]); lv_obj_set_pos(g_ov_cap[i], x - lv_obj_get_width(g_ov_cap[i]) / 2, y + 40); }
static void ov_paint(void){
    if(!g_ov) return;
    ov_mark(OV_FAV, ui_np_fav_state() == 1);
}
static void ov_act_cb(lv_event_t *e){
    if(lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    int a = (int)(intptr_t)lv_event_get_user_data(e);
    if(th_simple())simple_action_feedback();
    switch(a){
        case OV_FAV:    ui_np_fav_toggle(); ov_paint(); return;       /* stays open */
        case OV_ALBUM:  ov_close(); ui_np_open_album(); break;
        case OV_ARTIST: ov_close(); ui_np_open_artist(); break;
        case OV_QUEUE:  ov_close(); queue_open(); break;                /* our own Queue */
        case OV_LYRICS: ov_close(); lyrics_open(); break;
        case OV_INFO: { track_state_t st; ipc_get_state(&st); ov_close(); songinfo_unpin(); songinfo_set(&st); screen_show(SCR_SONGINFO); break; }
    }
}
static void ov_mood_cb(lv_event_t *e){                       /* SongSphere: where this song sits */
    if(lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    track_state_t st; ipc_get_state(&st); ov_close();
    if(st.have_track) sphere_ui_tag(0, st.path, st.title);
}
/* the mood pill under the buttons: the song's mood in its colour (its own, else its album's or artist's), or Set mood */
static void ov_mood_pill(lv_obj_t *p, int y){
    track_state_t st; ipc_get_state(&st);
    float x = 0, yy = 0; int src = st.have_track ? sphere_ui_song_spot(st.path, NULL, NULL, &x, &yy) : 0;
    char t[64]; const lv_font_t *f = th_null() ? TF(HEADER_SM) : TF(UI_18), *fs = TF(UI_14);
    snprintf(t, sizeof t, "%s", src ? sphere_ui_mood_name(x, yy) : "Set mood");
    const char *from = src == 2 ? "album" : src == 3 ? "artist" : NULL;
    lv_point_t a, b = { 0, 0 }; lv_text_get_size(&a, t, f, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    if(from) lv_text_get_size(&b, from, fs, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    int w = 18 + 14 + 10 + a.x + (from ? 8 + b.x : 0) + 30 + 16; if(w > 200) w = 200;
    lv_obj_t *pl = lv_button_create(p); lv_obj_remove_style_all(pl);
    lv_obj_set_size(pl, w, 40); lv_obj_set_pos(pl, 180 - w / 2, y);
    lv_obj_set_style_radius(pl, 20, 0);
    lv_obj_set_style_bg_color(pl, TC(SURFACE), 0); lv_obj_set_style_bg_opa(pl, 205, 0);
    lv_obj_set_style_bg_color(pl, TC(SURFACE_RAISED), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(pl, 1, 0); lv_obj_set_style_border_color(pl, src ? sphere_ui_mood_colour(x, yy) : g_acc, 0); lv_obj_set_style_border_opa(pl, 140, 0);
    lv_obj_set_ext_click_area(pl, 8);
    lv_obj_t *d = lv_obj_create(pl); lv_obj_remove_style_all(d);
    lv_obj_set_size(d, 14, 14); lv_obj_align(d, LV_ALIGN_LEFT_MID, 18, 0); lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
    lv_obj_remove_flag(d, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    if(src){ lv_color_t c = sphere_ui_mood_colour(x, yy); lv_obj_set_style_bg_color(d, c, 0); lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
             lv_obj_set_style_shadow_width(d, 10, 0); lv_obj_set_style_shadow_color(d, c, 0); lv_obj_set_style_shadow_opa(d, LV_OPA_50, 0); }
    else { lv_obj_set_style_border_width(d, 2, 0); lv_obj_set_style_border_color(d, g_acc, 0); }
    lv_obj_t *l = lv_label_create(pl); lv_label_set_text(l, t); lv_label_set_long_mode(l, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(l, w - 42 - 30 - (from ? 8 + b.x : 0)); lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, src ? TC(TEXT_PRIMARY) : g_acc, 0); lv_obj_align(l, LV_ALIGN_LEFT_MID, 42, 0);
    if(from){ lv_obj_t *fl = lv_label_create(pl); lv_label_set_text(fl, from);
        lv_obj_set_style_text_font(fl, fs, 0); lv_obj_set_style_text_color(fl, TC(TEXT_MUTED), 0);
        lv_obj_align_to(fl, l, LV_ALIGN_OUT_RIGHT_MID, 8, 1); }
    lv_obj_t *ch = lv_label_create(pl); lv_label_set_text(ch, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_font(ch, TF(UI_14), 0); lv_obj_set_style_text_color(ch, TC(TEXT_SECONDARY), 0);
    lv_obj_align(ch, LV_ALIGN_RIGHT_MID, -16, 0);
    ui_on(pl, ov_mood_cb, LV_EVENT_CLICKED, NULL, "disco.track.mood", UI_REACHABLE);
    if(th_null()){
        kit_keep(pl);
        for(uint32_t i=0;i<lv_obj_get_child_count(pl);i++) kit_keep(lv_obj_get_child(pl,i));
    }
}
static void ov_open(void){
    if(th_disco()||th_simple()||th_null()){screen_show(SCR_NPMENU);return;}
    if(!g_have || g_ov) return;
    int mood = sphere_ui_enabled(), lift = mood ? 28 : 0;                 /* SongSphere on: smaller buttons, moved up for the mood pill */
    g_ov = lv_obj_create(lv_layer_top());
    if(g_ov)onboarding_discovered(HINT_MUSIC);
    lv_obj_remove_style_all(g_ov);
    lv_obj_set_size(g_ov, 360, 360);
    lv_obj_set_style_bg_color(g_ov, TC(CANVAS), 0); lv_obj_set_style_bg_opa(g_ov, th_simple()?255:245, 0);
    lv_obj_add_flag(g_ov, LV_OBJ_FLAG_CLICKABLE); lv_obj_clear_flag(g_ov, LV_OBJ_FLAG_SCROLLABLE);   /* swallows taps */
    lv_obj_t *tb = lv_obj_create(g_ov); lv_obj_remove_style_all(tb);   /* the title + artist: tap here to close */
    lv_obj_set_size(tb, th_simple() ? 228 : 260, 64); lv_obj_align(tb, LV_ALIGN_TOP_MID, 0, 38 - lift / 2);
    lv_obj_add_flag(tb, LV_OBJ_FLAG_CLICKABLE); lv_obj_set_ext_click_area(tb, 10);
    lv_obj_add_event_cb(tb, ov_title_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *t = lv_label_create(tb); lv_label_set_text(t, L_TITLE.cur);
    lv_label_set_long_mode(t, LV_LABEL_LONG_SCROLL_CIRCULAR); lv_obj_set_size(t, th_simple() ? 220 : 250, 28);
    lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0); lv_obj_set_style_text_font(t, th_null() ? TF(HEADER_SM) : ui_font_cjk(22), 0);
    lv_obj_set_style_text_color(t, TC(TEXT_PRIMARY), 0); lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 8);
    if(th_simple()) simple_keep_label(t); else kit_keep(t); /* Never ellipsize popup metadata. */
    lv_obj_t *ar = lv_label_create(tb); lv_label_set_text(ar, L_ARTIST.cur);
    lv_label_set_long_mode(ar, LV_LABEL_LONG_SCROLL_CIRCULAR); lv_obj_set_size(ar, th_simple() ? 210 : 230, 22);
    lv_obj_set_style_text_align(ar, LV_TEXT_ALIGN_CENTER, 0); lv_obj_set_style_text_font(ar, th_null() ? TF(UI_16) : ui_font_cjk(16), 0);
    lv_obj_set_style_text_color(ar, TC(TEXT_SECONDARY), 0); lv_obj_align(ar, LV_ALIGN_TOP_MID, 0, 38);
    if(th_null()) lv_obj_set_style_text_font(ar, TF(HEADER_SM), 0);
    /* Scroll modes choose a vertical loop if the line is taller than a fixed box.
     * Content height guarantees a single full line: only over-wide text can animate. */
    lv_obj_set_height(t,LV_SIZE_CONTENT);lv_obj_set_height(ar,LV_SIZE_CONTENT);
    lv_obj_set_height(tb,42+lv_font_get_line_height(lv_obj_get_style_text_font(ar,0)));
    if(th_simple()) simple_keep_label(ar); else kit_keep(ar);
    static const struct { int x, y; const char *cap; } P[OV_N] = {          /* two gentle arcs of three */
        { 84, 146, "Favourite" }, { 180, 160, "Album" }, { 276, 146, "Artist" },
        { 94, 250, "Queue" },     { 180, 270, "Lyrics" }, { 266, 250, "Info" } };
    const char *G[OV_N] = { TH_IC_HEART, TH_IC_RECORD, "\xEF\x84\xB0", LV_SYMBOL_LIST, LV_SYMBOL_FILE, "i" };   /* Artist: microphone; Info: circled i */
    for(int i = 0; i < OV_N; i++){
        int py = P[i].y - (i < 3 ? (mood ? 26 : 0) : (mood ? 36 : 0));
        lv_obj_t *b = glass_circle(g_ov, P[i].x, py, mood ? 60 : 70, G[i]);
        g_ov_btn[i] = b; lv_obj_set_style_text_color(lv_obj_get_child(b, 0), g_acc, 0); lv_obj_set_style_border_color(b, g_acc, 0); lv_obj_set_style_border_opa(b, 140, 0);
        if(i == OV_FAV || i == OV_ALBUM) lv_obj_set_style_text_font(lv_obj_get_child(b, 0), &font_theme_24, 0);
        if(i == OV_ARTIST) lv_obj_set_style_text_font(lv_obj_get_child(b, 0), TF(ICON_28), 0);
        if(i == OV_INFO){
            lv_obj_t *ic = lv_obj_get_child(b, 0);
            lv_obj_set_style_text_font(ic, TF(UI_24), 0); lv_obj_set_size(ic, 32, 32);
            lv_obj_set_style_text_align(ic, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_style_border_width(ic, 2, 0); lv_obj_set_style_border_color(ic, g_acc, 0);
            lv_obj_set_style_radius(ic, LV_RADIUS_CIRCLE, 0); lv_obj_center(ic);
        }
        static const char *const actions[OV_N] = { "disco.track.favourite", "disco.track.album", "disco.track.artist", "disco.track.queue", "disco.track.lyrics", "disco.track.info" };
        ui_on(b, ov_act_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i, actions[i], UI_REACHABLE);
        g_ov_cap[i] = lv_label_create(g_ov); lv_label_set_text(g_ov_cap[i], P[i].cap);
        lv_obj_set_style_text_font(g_ov_cap[i], TF(UI_18), 0); lv_obj_set_style_text_color(g_ov_cap[i], TC(TEXT_PRIMARY), 0);
        if(mood) lv_obj_set_style_text_font(g_ov_cap[i], TF(UI_16), 0);
        if(th_null()){
            static const char *const caps[OV_N]={"FAV","ALB","ART","QUE","LYR","INF"};
            lv_label_set_text(g_ov_cap[i],caps[i]);
            lv_obj_set_style_text_font(g_ov_cap[i],TF(HEADER_SM),0);
            kit_keep(g_ov_cap[i]);
        }
        ov_place_cap(i, P[i].x, py - (mood ? 7 : 0));
    }
    if(mood) ov_mood_pill(g_ov, 294);
    kit_pass(g_ov);
    if(th_null()) lv_obj_set_style_bg_opa(g_ov, LV_OPA_COVER, 0);
    ov_paint();
    onboarding_context(HINT_POPUP);
    ov_swipe_bubble(g_ov);
    lv_obj_add_event_cb(g_ov,ov_swipe_cb,LV_EVENT_ALL,NULL);
}
/* Null shares the track popup without constructing or updating Disco's Home widgets. */
void disco_track_overlay_open(void){
    if(th_disco()||th_simple()||th_null()){screen_show(SCR_NPMENU);return;}
    track_state_t st; ipc_get_state(&st);
    if((!th_null() && !th_disco_family()) || !st.have_track) return;
    if(th_disco_family()){ov_open();return;}
    g_have = 1;
    g_acc = TC(TEXT_SECONDARY);
    theme_upper(L_TITLE.cur, sizeof L_TITLE.cur, st.title);
    theme_upper(L_ARTIST.cur, sizeof L_ARTIST.cur, st.artist);
    ov_open();
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
static void simple_music_content(int);
/* idle: the Shuffle all / Browse pills under the count; the transport buttons give them the room */
static lv_obj_t *g_idle_row;
static void idle_row_apply(void){
    if(!g_trk) return;
    if(!g_idle_row){ if(!g_idle) return; g_idle_row = ui_idle_actions(lv_obj_get_parent(g_trk)); }
    ui_idle_actions_show(g_idle_row, g_idle);
    if(!g_idle) return;
    lv_obj_update_layout(g_idle_row);
    lv_obj_align(g_idle_row, LV_ALIGN_TOP_MID, 0, th_simple() ? 222 : 204);
    lv_obj_move_foreground(g_idle_row);                                 /* above the gesture field: the pills take their own taps */
}
static void idle_eval(void){                                            /* switch idle on / off as the track state says */
    int want = !g_have && (lv_tick_get() >= 10000 || simple_cfg_get_int("memory_play", 0) == 0);
    if(want == g_idle) return;
    g_idle = want;
    if(g_idle){ dl_text(&L_TITLE, "Nothing playing"); dl_text(&L_ARTIST, ui_idle_count_text()); }
    dhome_controls_apply(); idle_row_apply();                           /* leaving idle: the transport as configured */
    if(th_simple()) simple_music_content(1);
    disco_art_changed();                                                /* the cover, the backdrops and the ink follow */
    disco_progress_apply();
}
static void idle_timer_cb(lv_timer_t *t){ (void)t; idle_eval(); }
static void title_cb(lv_event_t *e){
    int more=lv_event_get_current_target(e)==g_more;
    if(th_simple()&&!more)return;
    lv_event_code_t c = lv_event_get_code(e);
    if(c!=LV_EVENT_PRESSED&&c!=LV_EVENT_PRESSING&&c!=LV_EVENT_RELEASED&&c!=LV_EVENT_PRESS_LOST&&
       c!=LV_EVENT_SHORT_CLICKED&&c!=LV_EVENT_LONG_PRESSED)return;
    lv_indev_t *in=lv_indev_active();lv_point_t p={0,0};if(in)lv_indev_get_point(in,&p);
    if(c==LV_EVENT_PRESSED){
        simple_tap_cancel();g_gz_last=0;track_start=p;track_cancel=0;track_armed=1;
        ui_np_link_capture(&track_press);track_target=more?TRACK_MENU:track_link_at(&p);return;
    }
    if(c==LV_EVENT_PRESS_LOST){track_cancel=1;return;}
    if(in&&(abs(p.x-track_start.x)>18||abs(p.y-track_start.y)>18))track_cancel=1;
    if(!track_armed||track_cancel||screen_current()!=SCR_HOME)return;
    if(c==LV_EVENT_SHORT_CLICKED){track_armed=0;track_open(&track_press,track_target);}
    else if(c == LV_EVENT_LONG_PRESSED){
        track_cancel=1;
        if(more||!g_have||g_ov||!ui_np_link_current(&track_press))return;
        int h = simple_cfg_get_int("disco_title_hold", 0);
        if(h == 0) disco_open_np_immersive();
        else if(h == 1) ui_np_fav_toggle();                             /* toasts Added / Removed */
    }
}
/* Settings > Disco Options > Title Position: 0 Centred, 1 Left */
static void simple_music_layout(void);
static void simple_music_art(void);
static void simple_music_content(int);
void dhome_title_apply(void){
    if(th_simple()){simple_music_layout();return;}
    if(!L_TITLE.fg) return;
    int left = simple_cfg_get_int("disco_title_al", 0) == 1;
    dlabel_t *L[2] = { &L_TITLE, &L_ARTIST }; int Y[2] = { 0, 38 };
    for(int i = 0; i < 2; i++) for(int k = 0; k < 2; k++){
        lv_obj_t *l = k ? L[i]->fg : L[i]->sh; int d = k ? 0 : 1;
        lv_obj_set_style_text_align(l, left ? LV_TEXT_ALIGN_LEFT : LV_TEXT_ALIGN_CENTER, 0);
        if(left) lv_obj_align(l, LV_ALIGN_TOP_LEFT, d, Y[i] + d); else lv_obj_align(l, LV_ALIGN_TOP_MID, d, Y[i] + d);
    }
}
/* the top of Music: a tap on the clock opens Weather; a hold shows or hides the clock and the status icons
 * (the same switch as Disco Options > Music Screen > Show Clock). When hidden, an empty strip keeps the hold. */
static lv_obj_t *g_top_hit;
static void wx_cb(lv_event_t *e){
    lv_event_code_t c = lv_event_get_code(e);
    if(c == LV_EVENT_SHORT_CLICKED && lv_event_get_target(e) == g_pill){   /* not CLICKED: that follows a hold */
        if(simple_cfg_get_int("clock_weather", 1)) weather_app_open();      /* weather hidden: the clock isn't a button */
    }
    else if(c == LV_EVENT_LONG_PRESSED){
        int on = g_pill && lv_obj_has_flag(g_pill, LV_OBJ_FLAG_HIDDEN);   /* what is on screen decides, not a save that may have failed */
        simple_cfg_set_int("disco_clock", on); dhome_show_clock(on);
    }
}

static void mode_paint(void){
    int wm = simple_cfg_get_int("work_mode", 0);
    static const char *const G[5] = { LV_SYMBOL_RIGHT, LV_SYMBOL_SHUFFLE, LV_SYMBOL_LOOP "1", LV_SYMBOL_LOOP, LV_SYMBOL_RIGHT "1" };
    dl_text(&L_MODE, G[wm >= 0 && wm < 5 ? wm : 0]);
}
static void mode_cb(lv_event_t *e){ if(lv_event_get_code(e) == LV_EVENT_CLICKED){ ui_np_mode_next(); mode_paint(); if(th_simple())simple_action_feedback(); } }   /* toasts the new mode */
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
    /* 2.6.2 clock field: icons, clock and weather a touch larger, all inside the 110 px field (title from y 136) */
    dl_make(g_pill, &L_STAT, TF(UI_16), -1, 0, LV_ALIGN_TOP_MID, 0, 2, 1);
    g_time = dl_make(g_pill, &L_TIME, TF(UI_48), -1, 0, LV_ALIGN_TOP_MID, 0, 20, 0);
    dl_text(&L_TIME, "--:--");
    g_wfont = *theme_font_original(20); g_wfont.fallback = &font_weather20;
    g_wx = dl_make(g_pill, &L_WX, &g_wfont, -1, 0, LV_ALIGN_TOP_MID, 0, 80, 1);
    g_top_hit = lv_obj_create(root); lv_obj_remove_style_all(g_top_hit);   /* under the clock: holds while it's hidden */
    lv_obj_set_size(g_top_hit, 200, 110); lv_obj_align(g_top_hit, LV_ALIGN_TOP_MID, 0, 14);
    lv_obj_add_flag(g_top_hit, LV_OBJ_FLAG_CLICKABLE); lv_obj_clear_flag(g_top_hit, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(g_top_hit, wx_cb, LV_EVENT_LONG_PRESSED, NULL);
    lv_obj_move_to_index(g_top_hit, lv_obj_get_index(g_pill));         /* just behind the clock */
    lv_obj_add_flag(g_pill, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(g_pill, wx_cb, LV_EVENT_SHORT_CLICKED, NULL); lv_obj_add_event_cb(g_pill, wx_cb, LV_EVENT_LONG_PRESSED, NULL);
    if(!simple_cfg_get_int("disco_clock", 1)) lv_obj_add_flag(g_pill, LV_OBJ_FLAG_HIDDEN);
    dhome_clockinfo_apply();
    /* Reserve a separate More target beside the title and artist links. */
    g_trk = lv_obj_create(root); lv_obj_remove_style_all(g_trk);
    lv_obj_set_pos(g_trk, disco_mirror() ? 98 : 18, 136); lv_obj_set_size(g_trk, 244, 70);
    lv_obj_clear_flag(g_trk,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_trk, LV_OBJ_FLAG_CLICKABLE); lv_obj_add_event_cb(g_trk, title_cb, LV_EVENT_ALL, NULL);
    g_title = dl_make(g_trk, &L_TITLE, ui_np_font(28), 240, 38, LV_ALIGN_TOP_MID, 0, 0, 0);
    dl_text(&L_TITLE, "Not Playing");
    g_artist = dl_make(g_trk, &L_ARTIST, ui_np_font(20), 240, 32, LV_ALIGN_TOP_MID, 0, 38, 1);
    g_more=glass_circle(root,disco_mirror()?70:290,169,40,"...");
    lv_obj_set_ext_click_area(g_more,0);lv_obj_clear_flag(g_more,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_more,LV_OBJ_FLAG_HIDDEN);
    ui_on(g_more,title_cb,LV_EVENT_ALL,NULL,"disco.track.more",UI_REACHABLE);
    dl_scroll5(&L_TITLE); dl_scroll5(&L_ARTIST);
    dhome_title_apply();
    underline_create(root);
    dl_make(root, &L_EL, TF(UI_14), -1, 0, LV_ALIGN_TOP_LEFT, UL_X + 8, UL_Y + 8, 1);                /* a size up, in from the rim */
    dl_make(root, &L_RM, TF(UI_14), -1, 0, LV_ALIGN_TOP_RIGHT, -(360 - UL_X - UL_W) - 8, UL_Y + 8, 1);
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
    zoom_create(root);                                                 /* hold the line: the large time */
    dhome_controls_apply();                                            /* Buttons / Auto-hide / Gestures */
    g_tick = lv_timer_create(prog_tick, 1000, NULL);
    { lv_timer_t *it = lv_timer_create(idle_timer_cb, 10200, NULL); if(it) lv_timer_set_repeat_count(it, 1); }   /* idle 10 s after start */
    dhome_art_changed();
    if(th_simple())simple_music_layout();
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
static int g_st_batt = -1, g_st_chg, g_st_wifi, g_st_bt;            /* the last status, for redrawing on a setting change */
void dhome_set_status(int batt, int charging, int wifi, int bt){
    g_st_batt = batt; g_st_chg = charging; g_st_wifi = wifi; g_st_bt = bt;
    char b[64] = ""; size_t n = 0;                                       /* the Disc's usual status row: Wi-Fi, BT, battery */
    if(wifi) n += (size_t)snprintf(b + n, sizeof b - n, LV_SYMBOL_WIFI "  ");
    if(bt)   n += (size_t)snprintf(b + n, sizeof b - n, LV_SYMBOL_BLUETOOTH "  ");
    const char *bs = charging ? LV_SYMBOL_CHARGE : batt >= 90 ? LV_SYMBOL_BATTERY_FULL : batt >= 65 ? LV_SYMBOL_BATTERY_3 :
                     batt >= 40 ? LV_SYMBOL_BATTERY_2 : batt >= 15 ? LV_SYMBOL_BATTERY_1 : LV_SYMBOL_BATTERY_EMPTY;
    if(batt >= 0) n += (size_t)snprintf(b + n, sizeof b - n, "%s", bs);
    if(batt >= 0 && n < sizeof b && simple_cfg_get_int("clock_batt_pct", 0))   /* Clock and info > Battery percentage */
        snprintf(b + n, sizeof b - n, " %d%%", batt > 100 ? 100 : batt);
    dl_text(&L_STAT, b);
}
/* Clock and info changed: the percentage and the weather line; with the weather off, a tap on the clock does nothing */
void dhome_clockinfo_apply(void){
    if(!g_pill) return;
    if(g_st_batt >= 0 || g_st_wifi || g_st_bt) dhome_set_status(g_st_batt, g_st_chg, g_st_wifi, g_st_bt);
    int wx = simple_cfg_get_int("clock_weather", 1);
    lv_obj_t *w[2] = { L_WX.fg, L_WX.sh };
    for(int i = 0; i < 2; i++) if(w[i]){ if(wx) lv_obj_remove_flag(w[i], LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(w[i], LV_OBJ_FLAG_HIDDEN); }
}
void dhome_set_now_playing(const char *title, const char *artist, lv_color_t accent, bool playing){
    g_have = title != NULL;
    if(g_more){if(g_have)lv_obj_remove_flag(g_more,LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(g_more,LV_OBJ_FLAG_HIDDEN);}
    if(simple_tap_timer&&!ui_np_link_current(&simple_pending)){simple_tap_cancel();g_gz_last=0;}
    if(track_armed&&!ui_np_link_current(&track_press))track_cancel=1;
    g_acc = ui_disco_fit_accent(accent);
    if(!th_simple() && title && strcmp(L_TITLE.cur,title) && L_ARTIST.fg){ui_music_scroll(L_ARTIST.fg);ui_music_scroll(L_ARTIST.sh);}
    if(!(g_idle && !title)){ dl_text(&L_TITLE, title ? title : "Not Playing"); dl_text(&L_ARTIST, artist ? artist : ""); }
    if(g_mode) mode_paint();                                            /* changed elsewhere (Now Playing, Quick Settings) */
    static int last_play = -1;                                          /* this runs on every position frame: touch only what changed */
    if(g_btn_lbl[1] && last_play != (int)playing){ last_play = playing; lv_label_set_text(g_btn_lbl[1], playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY); }
    if(!title && g_ul_fill){ ul_set(0); pa_set(0); }
    idle_eval();
    if(th_simple())simple_music_content(0);
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
    int m = simple_cfg_get_int("disco_progress", 0); if(m < 0 || m > 2) m = 0;
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
    if(g_gz_fx) lv_obj_set_style_text_color(g_gz_fx, g_acc, 0);
    if(g_ul_solid) lv_obj_set_style_bg_color(g_ul_solid, g_acc, 0);
    if(g_pa_tr) pa_paint(simple_cfg_get_int("disco_progress", 0));
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
    if(th_simple()){simple_music_art();return;}
    if(!g_cover) return;
    const void *src = ui_current_sharp_img();
    if(g_idle) src = disco_idle_art();                                  /* nothing playing: the generated CD (not a stale cover) */
    ink_all((const lv_image_dsc_t *)src);
    lv_image_set_src(g_cover, NULL);                                   /* same buffer, new pixels: force a redraw */
    if(src){ lv_image_set_src(g_cover, src); lv_obj_remove_flag(g_cover, LV_OBJ_FLAG_HIDDEN); }
    else lv_obj_add_flag(g_cover, LV_OBJ_FLAG_HIDDEN);
}
void dhome_test_zoom(int frac){ zoom_show(1, frac); }                  /* host renders: the hold-to-scrub time */

/* ---- Immersive: Music's pieces step aside (the clock up, the controls down, the rest fades) and come back ---- */
static void (*g_imm_done)(void);
static lv_obj_t *imm_part(int i, int *dy){                             /* the pieces and which way each one goes */
    lv_obj_t *o[] = { g_pill, g_trk, g_ul_tr, g_ul_fill, g_ul_knob, L_EL.fg, L_EL.sh, L_RM.fg, L_RM.sh, g_btn[0], g_btn[1], g_btn[2], g_mode, g_more,
                      g_gz_hint[0], g_gz_hint[1], g_gz_hint[2], g_gz_hint[3] };
    static const int D[] = { -26, 0, 0, 0, 0, 0, 0, 0, 0, 30, 34, 30, 30, 0, 30, 30, 30, 30 };
    if(i >= (int)(sizeof D / sizeof D[0])) return (lv_obj_t *)-1;
    *dy = D[i]; return o[i];
}
static void imm_parts_set(int v){                                      /* v: 0 in place .. 1000 gone */
    for(int i = 0;; i++){
        int dy; lv_obj_t *o = imm_part(i, &dy);
        if(o == (lv_obj_t *)-1) break;
        if(!o) continue;
        lv_obj_set_style_translate_y(o, dy * v / 1000, 0);
        lv_obj_set_style_opa(o, (lv_opa_t)(255 - 255 * v / 1000), 0);
    }
}
static void imm_parts_exec(void *x, int32_t v){ (void)x; imm_parts_set(v); }
static void imm_leave_done(lv_anim_t *a){ (void)a; void (*d)(void) = g_imm_done; g_imm_done = NULL; if(d) d(); }
static void imm_back_done(lv_anim_t *a){ (void)a; imm_parts_set(0); }
static int g_imm_tag;
void dhome_imm_leave(void (*done)(void)){
    g_imm_done = done; lv_anim_delete(&g_imm_tag, NULL);
    lv_anim_t a; lv_anim_init(&a); lv_anim_set_var(&a, &g_imm_tag); lv_anim_set_exec_cb(&a, imm_parts_exec);
    lv_anim_set_values(&a, 0, 1000); lv_anim_set_duration(&a, 200); lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
    lv_anim_set_completed_cb(&a, imm_leave_done); lv_anim_start(&a);
}
void dhome_imm_return(void){
    lv_anim_delete(&g_imm_tag, NULL);
    lv_anim_t a; lv_anim_init(&a); lv_anim_set_var(&a, &g_imm_tag); lv_anim_set_exec_cb(&a, imm_parts_exec);
    lv_anim_set_values(&a, 1000, 0); lv_anim_set_duration(&a, 260); lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_completed_cb(&a, imm_back_done); lv_anim_start(&a);
}
void dhome_test_imm_parts(int v){ imm_parts_set(v); }                  /* host renders */

/* Squash rearranges the retained Music controls; their handlers and track overlay remain shared. */
static lv_obj_t *simple_album;
static void simple_label_pair(dlabel_t*d,int x,int y,int w,int h,const lv_font_t*f){
 if(!d->fg)return;lv_obj_add_flag(d->sh,LV_OBJ_FLAG_HIDDEN);d->scroll=0;
 lv_label_set_long_mode(d->fg,LV_LABEL_LONG_WRAP);lv_obj_set_align(d->fg,LV_ALIGN_TOP_LEFT);lv_obj_set_pos(d->fg,x,y);lv_obj_set_size(d->fg,w,h);
 lv_obj_set_style_text_font(d->fg,f,0);lv_obj_set_style_text_align(d->fg,LV_TEXT_ALIGN_CENTER,0);lv_obj_set_style_text_color(d->fg,TC(TEXT_PRIMARY),0);kit_keep(d->fg);
}
static void simple_music_content(int force){
 static char title[192],artist[192],album[160],path[512];
 track_state_t st;ipc_get_state(&st);
 int changed=force||strcmp(title,L_TITLE.cur)||strcmp(artist,L_ARTIST.cur)||strcmp(album,st.album)||strcmp(path,st.path);
 if(!L_TITLE.fg||!simple_album)return;
 if(changed){
  snprintf(title,sizeof title,"%s",L_TITLE.cur);snprintf(artist,sizeof artist,"%s",L_ARTIST.cur);snprintf(album,sizeof album,"%s",st.album);snprintf(path,sizeof path,"%s",st.path);
  int h=lv_font_get_line_height(TF(MUSIC_TITLE));lv_obj_set_height(L_TITLE.fg,h);lv_obj_set_y(L_TITLE.fg,SIMPLE_TEXT_Y-h);
  lv_label_set_long_mode(L_TITLE.fg,LV_LABEL_LONG_CLIP);lv_label_set_long_mode(L_ARTIST.fg,LV_LABEL_LONG_CLIP);lv_label_set_long_mode(simple_album,LV_LABEL_LONG_CLIP);
  lv_obj_set_style_text_font(L_ARTIST.fg,g_idle?TF(UI_22):TF(MUSIC_ARTIST),0);   /* idle: the song count, a line that must fit */
  lv_label_set_text(L_TITLE.fg,L_TITLE.cur);lv_label_set_text(L_ARTIST.fg,L_ARTIST.cur);lv_label_set_text(simple_album,g_idle?"":st.album);
  ui_music_scroll(L_TITLE.fg);ui_music_scroll(L_ARTIST.fg);ui_music_scroll(simple_album);
 }
}
static void simple_music_art(void){
 if(!g_cover)return;
 extern unsigned ui_art_revision(void);static unsigned revision;static int bg_prev=-1;static const void*prev;static lv_obj_t*target;
 int bg=simple_cfg_get_int("simple_background",0);unsigned current=ui_art_revision();const void*src=bg==1?ui_current_backdrop_img():bg==2?ui_current_sharp_img():NULL;
 if(target==g_cover&&revision==current&&bg_prev==bg&&prev==src)return;
 target=g_cover;revision=current;bg_prev=bg;prev=src;
 lv_image_set_src(g_cover,NULL);
 if(src){lv_image_set_src(g_cover,src);lv_obj_remove_flag(g_cover,LV_OBJ_FLAG_HIDDEN);}else lv_obj_add_flag(g_cover,LV_OBJ_FLAG_HIDDEN);
}
static void simple_music_layout(void){
 if(!th_simple()||!g_ul_hit||!g_gz)return;
 g_acc=TC(ACCENT_PRIMARY);
 kit_keep(g_pill);kit_keep(g_trk);lv_obj_set_style_border_width(g_pill,0,0);lv_obj_set_style_bg_opa(g_pill,0,0);lv_obj_set_style_border_width(g_trk,0,0);lv_obj_set_style_bg_opa(g_trk,0,0);kit_keep(g_top_hit);lv_obj_set_align(g_pill,LV_ALIGN_TOP_LEFT);lv_obj_set_align(g_top_hit,LV_ALIGN_TOP_LEFT);
 lv_obj_set_pos(g_pill,80,14);lv_obj_set_size(g_pill,200,82);lv_obj_set_pos(g_top_hit,80,14);lv_obj_set_size(g_top_hit,200,82);
 simple_label_pair(&L_STAT,0,0,200,20,TF(UI_16));simple_label_pair(&L_TIME,0,20,200,35,TF(UI_30));simple_label_pair(&L_WX,0,56,200,26,&g_wfont);   /* a touch larger, still inside the 82 px field */
 int aligned_left=simple_cfg_get_int("disco_title_al",0)==1;
 lv_obj_set_pos(g_trk,36,94);lv_obj_set_size(g_trk,240,136);
 simple_label_pair(&L_TITLE,0,0,240,SIMPLE_TEXT_Y,TF(MUSIC_TITLE));simple_label_pair(&L_ARTIST,0,SIMPLE_TEXT_Y,240,39,TF(MUSIC_ARTIST));   /* More has its own column, clear of both text links. */lv_label_set_long_mode(L_ARTIST.fg,LV_LABEL_LONG_DOT);
 lv_obj_set_style_text_align(L_TITLE.fg,aligned_left?LV_TEXT_ALIGN_LEFT:LV_TEXT_ALIGN_CENTER,0);
 lv_obj_set_style_text_align(L_ARTIST.fg,aligned_left?LV_TEXT_ALIGN_LEFT:LV_TEXT_ALIGN_CENTER,0);
 if(!simple_album){simple_album=lv_label_create(lv_obj_get_parent(g_trk));lv_label_set_long_mode(simple_album,LV_LABEL_LONG_DOT);kit_keep(simple_album);}
 lv_obj_set_style_text_font(simple_album,TF(MUSIC_ALBUM),0);lv_obj_set_style_text_color(simple_album,TC(TEXT_SECONDARY),0);
 lv_obj_set_style_text_align(simple_album,aligned_left?LV_TEXT_ALIGN_LEFT:LV_TEXT_ALIGN_CENTER,0);lv_obj_set_pos(simple_album,36,208);lv_obj_set_size(simple_album,288,35);   /* clear of the progress line */
 simple_music_content(1);
 lv_obj_set_pos(g_gz,36,94);lv_obj_set_size(g_gz,288,171);lv_obj_move_foreground(g_gz);
 if(g_more){lv_obj_set_pos(g_more,284,120);lv_obj_move_foreground(g_more);kit_keep(g_more);}
 lv_label_set_text(g_gz_hint[0],LV_SYMBOL_LEFT LV_SYMBOL_LEFT);lv_obj_set_pos(g_gz_hint[0],112,306);
 lv_label_set_text(g_gz_hint[2],LV_SYMBOL_RIGHT LV_SYMBOL_RIGHT);lv_obj_set_pos(g_gz_hint[2],228,306);
 lv_obj_set_pos(g_gz_hint[3],172,270);lv_obj_add_flag(g_gz_hint[1],LV_OBJ_FLAG_HIDDEN);
 lv_obj_set_pos(g_gz_fx,140,150);lv_obj_move_foreground(g_gz_fx);
 lv_obj_set_size(g_mode,44,44);lv_obj_set_align(g_mode,LV_ALIGN_TOP_LEFT);lv_obj_set_pos(g_mode,248,44);lv_obj_add_flag(g_mode,LV_OBJ_FLAG_HIDDEN);
 for(int k=0;k<2;k++){lv_obj_t*l=k?L_MODE.fg:L_MODE.sh;if(k){lv_obj_set_style_text_color(l,TC(ACCENT_PRIMARY),0);kit_keep(l);}else lv_obj_add_flag(l,LV_OBJ_FLAG_HIDDEN);}
 lv_obj_set_style_bg_color(g_ul_solid,TC(ACCENT_PRIMARY),0);if(!pa_on())lv_obj_remove_flag(g_ul_solid,LV_OBJ_FLAG_HIDDEN);
 lv_obj_set_style_bg_color(g_ul_knob,TC(ACCENT_PRIMARY),0);lv_obj_set_style_outline_opa(g_ul_knob,0,0);
 simple_label_pair(&L_EL,40,272,90,28,TF(UI_20));simple_label_pair(&L_RM,230,272,90,28,TF(UI_20));
 if(!simple_play_state){
  lv_obj_t*root=lv_obj_get_parent(g_trk);simple_play_state=lv_label_create(root);simple_mode_state=lv_label_create(root);
  lv_obj_t*o[2]={simple_play_state,simple_mode_state};
  for(int k=0;k<2;k++){kit_keep(o[k]);lv_obj_clear_flag(o[k],LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_style_text_font(o[k],TF(UI_20),0);lv_obj_set_style_text_align(o[k],LV_TEXT_ALIGN_CENTER,0);lv_obj_set_size(o[k],28,28);lv_obj_set_pos(o[k],k?226:106,304);}
 }
 simple_music_indicators();simple_music_art();idle_row_apply();
 lv_obj_set_style_bg_opa(g_wash,simple_cfg_get_int("simple_background",0)==1?51:77,0);lv_obj_set_style_bg_grad_dir(g_wash,LV_GRAD_DIR_NONE,0);lv_obj_set_pos(g_wash,0,0);lv_obj_set_size(g_wash,360,360);
 acc_paint();
}

static void simple_music_indicators(void){
 if(!th_simple())return;
 int visible=!g_idle;
 static const char*const modes[5]={LV_SYMBOL_RIGHT,LV_SYMBOL_SHUFFLE,LV_SYMBOL_LOOP "1",LV_SYMBOL_LOOP,LV_SYMBOL_RIGHT "1"};
 if(simple_play_state){int playing=ui_is_playing();const char*glyph=playing?LV_SYMBOL_PLAY:LV_SYMBOL_PAUSE;
  if(strcmp(lv_label_get_text(simple_play_state),glyph)){lv_label_set_text(simple_play_state,glyph);lv_obj_set_style_text_color(simple_play_state,playing?TC(STATUS_SUCCESS):TC(TEXT_SECONDARY),0);}
  int mode=cfg_get_int("work_mode",0);glyph=modes[mode>=0&&mode<5?mode:0];
  if(strcmp(lv_label_get_text(simple_mode_state),glyph)){lv_label_set_text(simple_mode_state,glyph);lv_obj_set_style_text_color(simple_mode_state,TC(TEXT_SECONDARY),0);}
  lv_obj_t*o[2]={simple_play_state,simple_mode_state};for(int k=0;k<2;k++){if(visible&&cfg_get_int(k?"simple_play_mode":"simple_play_state",1))lv_obj_remove_flag(o[k],LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(o[k],LV_OBJ_FLAG_HIDDEN);}}
 int hints=0; /* Squash uses status icons, without gesture glyphs around the logo. */
 for(int k=0;k<4;k++)if(g_gz_hint[k]){if(hints&&k!=1)lv_obj_remove_flag(g_gz_hint[k],LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(g_gz_hint[k],LV_OBJ_FLAG_HIDDEN);}
}

int simple_music_protected(lv_obj_t*o){return th_simple()&&(o==g_pill||o==g_trk||o==g_more||o==g_top_hit||o==g_gz||o==L_TITLE.fg||o==L_ARTIST.fg||o==simple_album||o==simple_play_state||o==simple_mode_state);}
