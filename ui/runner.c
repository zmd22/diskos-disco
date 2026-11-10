/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Cat Dash: an endless runner built into mq_ui. A cat runs along the ground over boxes and puddles, under birds.
 * Tap: jump. Swipe up: long jump. Swipe down (and hold): duck, or drop fast in the air. Every 100 points the score
 * blinks; now and then day turns to night (the colours swap). Drawn from plain LVGL objects in the theme's ink and
 * canvas, so it follows every theme and its light / dark variant. */
#include "screens.h"
#include "theme.h"
#include "theme_kit.h"
#include "config.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define RN_GROUND 262                 /* the ground line's y */
#define RN_X0 26                      /* the play field, inside the round glass at the ground */
#define RN_X1 334
#define RN_CAT_X 64
#define RN_CAT_W 48
#define RN_CAT_H 34
#define RN_DUCK 10                    /* how far the head comes down while ducking */
#define CS(v) ((v) * 4 / 3)           /* the cat is drawn on a 36 x 26 grid, shown a third larger */
#define RN_MAX_OBS 4
#define RN_DASHES 6
#define RN_SWIPE 22                   /* px of vertical travel that make a press a swipe */
#define RN_GRAVITY 1750.0f
#define RN_JUMP (-540.0f)             /* a tap: ~83 px high */
#define RN_LONG (-700.0f)             /* a swipe up: ~140 px high, and longer in the air */
#define RN_NIGHT_EVERY 700            /* points: night falls at 500 of every 700 and lasts 200 */
#define RN_NIGHT_FROM 500

enum { RN_READY, RN_RUN, RN_OVER };
enum { OB_BOX, OB_PUDDLE, OB_BIRD };
typedef struct { lv_obj_t *o, *wing; float x; int w, h, y, kind, live; } rn_obs_t;

static lv_obj_t *g_root, *g_cat, *g_leg[4], *g_score, *g_best, *g_msg, *g_dash[RN_DASHES];
static int g_cat_base_y[6];                     /* tail, body, head, two ears, eye: their standing y */
static rn_obs_t g_obs[RN_MAX_OBS];
static lv_timer_t *g_tick;
static int g_state, g_best_v, g_frame, g_duck, g_held, g_gesture, g_night, g_last_hundred, g_blink;
static float g_cat_y, g_vy, g_speed, g_dist, g_since, g_next_gap, g_dash_x[RN_DASHES];   /* g_since: travel since the last obstacle */
static uint32_t g_last, g_duck_until, g_seed = 0x9E3779B9u;
static lv_point_t g_press;

static uint32_t rnd(void){ g_seed ^= g_seed << 13; g_seed ^= g_seed >> 17; g_seed ^= g_seed << 5; return g_seed; }
static lv_color_t ink(void){ return g_night ? TC(CANVAS) : TC(TEXT_PRIMARY); }
static lv_color_t paper(void){ return g_night ? TC(TEXT_PRIMARY) : TC(CANVAS); }
static lv_obj_t *blk(lv_obj_t *parent, int x, int y, int w, int h, int radius){
    lv_obj_t *o = lv_obj_create(parent); lv_obj_remove_style_all(o); kit_keep(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(o, x, y); lv_obj_set_size(o, w, h); lv_obj_set_style_radius(o, radius, 0);
    lv_obj_set_style_bg_color(o, ink(), 0); lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    return o;
}
/* day <-> night: every ink and canvas colour on the page trades places (the muted greys stay) */
static void swap_colors(lv_obj_t *o, lv_color_t a, lv_color_t b){
    static const lv_style_prop_t P[3] = { LV_STYLE_BG_COLOR, LV_STYLE_BORDER_COLOR, LV_STYLE_TEXT_COLOR };
    for(int k = 0; k < 3; k++){
        lv_style_value_t v = lv_obj_get_style_prop(o, LV_PART_MAIN, P[k]);
        if(lv_color_eq(v.color, a)) v.color = b; else if(lv_color_eq(v.color, b)) v.color = a; else continue;
        lv_obj_set_local_style_prop(o, P[k], v, 0);
    }
    for(uint32_t i = 0; i < lv_obj_get_child_count(o); i++) swap_colors(lv_obj_get_child(o, (int32_t)i), a, b);
}
static void set_night(int on){
    if(on == g_night) return;
    g_night = on; swap_colors(g_root, TC(TEXT_PRIMARY), TC(CANVAS));
}
/* the cat, facing right: tail, body, head, ears, an eye, four legs */
static void cat_build(void){
    g_cat = lv_obj_create(g_root); lv_obj_remove_style_all(g_cat); kit_keep(g_cat);
    lv_obj_remove_flag(g_cat, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_cat, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_set_size(g_cat, RN_CAT_W + 8, RN_CAT_H);
    blk(g_cat, 0, CS(2), CS(4), CS(10), 2);                       /* tail, raised */
    blk(g_cat, CS(3), CS(8), CS(22), CS(11), 6);                  /* body */
    blk(g_cat, CS(22), CS(3), CS(13), CS(11), 5);                 /* head */
    blk(g_cat, CS(23), 0, CS(4), CS(5), 1); blk(g_cat, CS(31), 0, CS(4), CS(5), 1);   /* ears */
    lv_obj_t *eye = blk(g_cat, CS(29), CS(6), 4, 4, 1); lv_obj_set_style_bg_color(eye, paper(), 0);
    for(int i = 0; i < 6; i++) g_cat_base_y[i] = lv_obj_get_style_y(lv_obj_get_child(g_cat, i), 0);
    static const int LX[4] = { 5, 10, 17, 22 };
    for(int i = 0; i < 4; i++) g_leg[i] = blk(g_cat, CS(LX[i]), CS(18), 4, CS(7), 1);
}
/* the pose: legs trot on the ground and tuck in the air; ducking lowers the head and stretches the body */
static void cat_pose(void){
    int air = g_cat_y < RN_GROUND - RN_CAT_H - 1, step = (g_frame / 4) & 1, d = g_duck && !air;
    for(int i = 0; i < 6; i++){
        lv_obj_t *c = lv_obj_get_child(g_cat, i); int y = g_cat_base_y[i] + (d ? RN_DUCK : 0);
        if(i == 0) y = g_cat_base_y[0] + (d ? RN_DUCK + 4 : 0);   /* the tail flattens too */
        if(lv_obj_get_style_y(c, 0) != y) lv_obj_set_y(c, y);
    }
    lv_obj_t *head = lv_obj_get_child(g_cat, 2), *body = lv_obj_get_child(g_cat, 1);
    int hx = CS(22) + (d ? 6 : 0), bw = CS(22) + (d ? 6 : 0);
    if(lv_obj_get_style_x(head, 0) != hx){ lv_obj_set_x(head, hx); lv_obj_set_width(body, bw);
        lv_obj_set_x(lv_obj_get_child(g_cat, 3), CS(23) + (d ? 6 : 0)); lv_obj_set_x(lv_obj_get_child(g_cat, 4), CS(31) + (d ? 6 : 0));
        lv_obj_set_x(lv_obj_get_child(g_cat, 5), CS(29) + (d ? 6 : 0)); }
    for(int i = 0; i < 4; i++){
        int h = air ? CS(4) : d ? CS(4) : ((i & 1) == step ? CS(7) : CS(4));
        if(lv_obj_get_height(g_leg[i]) != h) lv_obj_set_height(g_leg[i], h);
    }
}
/* a cardboard box (ink frame, taped across), a puddle (a low, wide oval) or a bird at head height (duck under it) */
static void obs_spawn(rn_obs_t *b){
    int r = (int)(rnd() % 8);
    b->kind = (g_dist > 2000 && r >= 6) ? OB_BIRD : (r < 2) ? OB_PUDDLE : OB_BOX;
    if(!b->o){ b->o = lv_obj_create(g_root); lv_obj_remove_style_all(b->o); kit_keep(b->o);
               lv_obj_remove_flag(b->o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
               lv_obj_add_flag(b->o, LV_OBJ_FLAG_OVERFLOW_VISIBLE); }
    lv_obj_clean(b->o); b->wing = NULL;
    lv_obj_set_style_bg_opa(b->o, LV_OPA_TRANSP, 0); lv_obj_set_style_border_width(b->o, 0, 0);
    if(b->kind == OB_PUDDLE){
        b->w = 34 + (int)(rnd() % 22); b->h = 8; b->y = RN_GROUND - b->h + 3;
        lv_obj_set_style_radius(b->o, LV_RADIUS_CIRCLE, 0);   /* inked rim, grey water: seen on light and dark */
        lv_obj_set_style_border_width(b->o, 2, 0); lv_obj_set_style_border_color(b->o, ink(), 0);
        lv_obj_set_style_bg_color(b->o, TC(TEXT_SECONDARY), 0); lv_obj_set_style_bg_opa(b->o, LV_OPA_50, 0);
    } else if(b->kind == OB_BIRD){
        b->w = 28; b->h = 16; b->y = RN_GROUND - 24 - b->h;          /* clears a ducking cat, not a standing one */
        blk(b->o, 4, 7, 20, 7, 3);                                      /* body */
        blk(b->o, 22, 5, 6, 3, 1);                                      /* beak */
        b->wing = blk(b->o, 9, 0, 9, 7, 2);                             /* wing: flaps */
    } else {
        static const int SZ[3][2] = { { 22, 22 }, { 28, 26 }, { 22, 36 } }; int k = (int)(rnd() % 3);
        b->w = SZ[k][0]; b->h = SZ[k][1]; b->y = RN_GROUND - b->h;
        lv_obj_set_style_radius(b->o, 2, 0);
        lv_obj_set_style_border_width(b->o, 2, 0); lv_obj_set_style_border_color(b->o, ink(), 0);
        blk(b->o, b->w / 2 - 3, 0, 4, b->h / 2, 0);   /* the tape over the lid */
        blk(b->o, 0, b->h / 2 - 3, b->w - 4, 2, 0);   /* the flap's edge */
    }
    lv_obj_set_size(b->o, b->w, b->h);
    b->x = RN_X1; b->live = 1;
    lv_obj_set_pos(b->o, (int)b->x, b->y);
    lv_obj_remove_flag(b->o, LV_OBJ_FLAG_HIDDEN);
}
static void score_paint(void){
    char b[24]; snprintf(b, sizeof b, "%05d", (int)(g_dist / 10));
    if(strcmp(lv_label_get_text(g_score), b)) lv_label_set_text(g_score, b);
}
static void best_paint(void){ char b[24]; snprintf(b, sizeof b, "HI %05d", g_best_v); lv_label_set_text(g_best, b); }
static void reset(void){
    set_night(0);
    g_cat_y = RN_GROUND - RN_CAT_H; g_vy = 0; g_speed = 170; g_dist = 0; g_since = 0; g_next_gap = 260; g_frame = 0;
    g_duck = 0; g_held = 0; g_gesture = 0; g_last_hundred = 0; g_blink = 0;
    lv_obj_set_style_opa(g_score, LV_OPA_COVER, 0);
    for(int i = 0; i < RN_MAX_OBS; i++){ g_obs[i].live = 0; if(g_obs[i].o) lv_obj_add_flag(g_obs[i].o, LV_OBJ_FLAG_HIDDEN); }
    lv_obj_set_pos(g_cat, RN_CAT_X, (int)g_cat_y); cat_pose(); score_paint();
}
/* a new best is kept as soon as the run stops - lost, or left for another screen */
static void save_best(void){
    int s = (int)(g_dist / 10);
    if(s > g_best_v){ g_best_v = s; cfg_set_int("runner_best", s); best_paint(); }
}
static void over(void){
    g_state = RN_OVER; g_duck = 0; cat_pose();
    lv_obj_set_style_opa(g_score, LV_OPA_COVER, 0);
    save_best();
    lv_label_set_text(g_msg, "Game over - tap to retry"); lv_obj_remove_flag(g_msg, LV_OBJ_FLAG_HIDDEN);
}
/* ~50 fps while the game is in front and running; the step uses the real elapsed time */
static void tick(lv_timer_t *t){
    (void)t;
    if(screen_current() != SCR_RUNNER){ if(g_state == RN_RUN){ save_best(); g_state = RN_READY; g_duck = 0; lv_label_set_text(g_msg, "Paused - tap to go on");
                                        lv_obj_remove_flag(g_msg, LV_OBJ_FLAG_HIDDEN); } return; }
    uint32_t now = lv_tick_get(); float dt = (now - g_last) / 1000.0f; g_last = now;
    if(g_state != RN_RUN) return;
    if(dt > 0.05f) dt = 0.05f;
    g_frame++;
    g_speed += dt * 6.0f; if(g_speed > 420) g_speed = 420;     /* slowly faster */
    g_dist += g_speed * dt; g_since += g_speed * dt;
    if(g_duck && !g_held && (int32_t)(now - g_duck_until) >= 0) g_duck = 0;
    g_vy += (g_duck ? 3.0f : 1.0f) * RN_GRAVITY * dt;          /* ducking in the air drops fast */
    g_cat_y += g_vy * dt;
    if(g_cat_y > RN_GROUND - RN_CAT_H){ g_cat_y = RN_GROUND - RN_CAT_H; g_vy = 0; }
    lv_obj_set_y(g_cat, (int)g_cat_y); cat_pose();
    for(int i = 0; i < RN_DASHES; i++){                        /* pebbles under the ground line: the sense of speed */
        g_dash_x[i] -= g_speed * dt; if(g_dash_x[i] < RN_X0) g_dash_x[i] += RN_X1 - RN_X0;
        lv_obj_set_x(g_dash[i], (int)g_dash_x[i]);
    }
    int pts = (int)(g_dist / 10);
    if(pts / 100 > g_last_hundred){ g_last_hundred = pts / 100; g_blink = 8; }   /* every 100 points: the score blinks */
    if(g_blink > 0 && (g_frame % 5) == 0){ g_blink--; lv_obj_set_style_opa(g_score, (g_blink & 1) ? LV_OPA_TRANSP : LV_OPA_COVER, 0); }
    set_night(pts % RN_NIGHT_EVERY >= RN_NIGHT_FROM);
    /* the cat's box: ears to paws, a few pixels forgiven at each edge; lower while ducking */
    int d = g_duck && g_cat_y >= RN_GROUND - RN_CAT_H - 1;
    int cx0 = RN_CAT_X + 4, cx1 = RN_CAT_X + RN_CAT_W - 4 + (d ? 6 : 0);
    int cy0 = (int)g_cat_y + (d ? RN_DUCK : 0) + 3, cy1 = (int)g_cat_y + RN_CAT_H - 3;
    for(int i = 0; i < RN_MAX_OBS; i++){
        rn_obs_t *b = &g_obs[i]; if(!b->live) continue;
        b->x -= g_speed * dt * (b->kind == OB_BIRD ? 1.12f : 1.0f);   /* birds fly into the wind a little faster */
        if(b->x + b->w < RN_X0){ b->live = 0; lv_obj_add_flag(b->o, LV_OBJ_FLAG_HIDDEN); continue; }
        lv_obj_set_x(b->o, (int)b->x);
        if(b->wing) lv_obj_set_y(b->wing, ((g_frame / 6) & 1) ? 0 : 9);
        int bx0 = (int)b->x + 3, bx1 = (int)b->x + b->w - 3;
        int by0 = b->y + (b->kind == OB_PUDDLE ? 1 : 2), by1 = b->y + b->h - 2;
        if(cx1 > bx0 && cx0 < bx1 && cy1 > by0 && cy0 < by1){ over(); return; }
    }
    if(g_since >= g_next_gap){                                 /* the next one once the ground since the last is covered */
        for(int i = 0; i < RN_MAX_OBS; i++) if(!g_obs[i].live){ obs_spawn(&g_obs[i]); g_since = 0; break; }
        g_next_gap = g_speed * (0.75f + (rnd() % 100) / 100.0f) + 60;   /* 0.75 - 1.75 s of running, room to land */
    }
    score_paint();
}
static int on_ground(void){ return g_cat_y >= RN_GROUND - RN_CAT_H - 1; }
static void start(void){
    if(g_state == RN_OVER) reset();
    g_state = RN_RUN; lv_obj_add_flag(g_msg, LV_OBJ_FLAG_HIDDEN); g_last = lv_tick_get();
}
/* press: note where; drag: a swipe up jumps long, a swipe down ducks (held while the finger stays); release: a tap jumps */
static void touch_cb(lv_event_t *e){
    lv_event_code_t c = lv_event_get_code(e);
    lv_indev_t *in = lv_indev_active(); lv_point_t p = { 0, 0 }; if(in) lv_indev_get_point(in, &p);
    if(c == LV_EVENT_PRESSED){ g_press = p; g_gesture = 0; g_held = 1; return; }
    if(c == LV_EVENT_PRESSING){
        if(g_gesture) return;
        int dy = p.y - g_press.y;
        if(dy <= -RN_SWIPE){ g_gesture = 1; if(g_state != RN_RUN) start(); if(on_ground()){ g_duck = 0; g_vy = RN_LONG; } }
        else if(dy >= RN_SWIPE){ g_gesture = 2; if(g_state != RN_RUN) start(); g_duck = 1; g_duck_until = lv_tick_get() + 450; }
        return;
    }
    if(c == LV_EVENT_RELEASED || c == LV_EVENT_PRESS_LOST){
        g_held = 0;
        if(g_gesture) return;
        if(c == LV_EVENT_PRESS_LOST) return;
        if(g_state != RN_RUN) start();
        if(on_ground()){ g_duck = 0; g_vy = RN_JUMP; }
    }
}
static void runner_open_text(void){
    lv_label_set_text(g_msg, "Tap: jump\nSwipe up: long jump\nSwipe down: duck");
    lv_obj_remove_flag(g_msg, LV_OBJ_FLAG_HIDDEN);
}
void runner_create(lv_obj_t *root){
    g_root = root;
    lv_obj_set_style_bg_color(root, TC(CANVAS), 0); lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    ui_header(root, "Cat Dash");
    g_best = lv_label_create(root); kit_keep(g_best);
    lv_obj_set_style_text_font(g_best, TF(UI_16), 0); lv_obj_set_style_text_color(g_best, TC(TEXT_SECONDARY), 0);
    lv_obj_align(g_best, LV_ALIGN_TOP_MID, -60, 96);
    g_score = lv_label_create(root); kit_keep(g_score); lv_label_set_text(g_score, "");
    lv_obj_set_style_text_font(g_score, TF(UI_20), 0); lv_obj_set_style_text_color(g_score, TC(TEXT_PRIMARY), 0);
    lv_obj_align(g_score, LV_ALIGN_TOP_MID, 60, 94);
    g_best_v = cfg_get_int("runner_best", 0); best_paint();
    blk(root, RN_X0, RN_GROUND, RN_X1 - RN_X0, 2, 0);          /* the ground */
    for(int i = 0; i < RN_DASHES; i++){
        g_dash_x[i] = RN_X0 + i * (RN_X1 - RN_X0) / RN_DASHES;
        g_dash[i] = blk(root, (int)g_dash_x[i], RN_GROUND + 6 + (i % 3) * 4, 6 + (i % 2) * 4, 2, 1);
        lv_obj_set_style_bg_color(g_dash[i], TC(TEXT_SECONDARY), 0);
    }
    cat_build();
    g_msg = lv_label_create(root); kit_keep(g_msg);
    lv_obj_set_style_text_font(g_msg, TF(UI_18), 0); lv_obj_set_style_text_color(g_msg, TC(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_align(g_msg, LV_TEXT_ALIGN_CENTER, 0); lv_obj_set_width(g_msg, 260);
    lv_obj_align(g_msg, LV_ALIGN_TOP_MID, 0, 128);
    lv_obj_add_flag(root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(root, touch_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(root, touch_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(root, touch_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(root, touch_cb, LV_EVENT_PRESS_LOST, NULL);
    g_state = RN_READY; reset(); runner_open_text();
    if(!g_tick) g_tick = lv_timer_create(tick, 20, NULL);
}
/* the landing page's Play: a fresh run, waiting for the first touch */
void runner_open(void){
    g_state = RN_READY; reset(); runner_open_text();
    g_seed ^= lv_tick_get() | 1u;
    screen_show(SCR_RUNNER);
}
void runner_landing(void){
    applanding_show("Cat Dash", "Jump the boxes, duck the birds.",
                    "Tap to jump, swipe up for a long jump, swipe down to duck. It gets faster the longer you run.", "Play", APP_BUILTIN_RUNNER);
}
/* host renders: jump to pts points (night follows), duck, and put a bird at x (0: none) */
void runner_test(int pts, int duck, int bird_x){
    g_state = RN_RUN; lv_obj_add_flag(g_msg, LV_OBJ_FLAG_HIDDEN);
    g_dist = pts * 10.0f; g_duck = duck; g_held = duck;
    set_night(pts % RN_NIGHT_EVERY >= RN_NIGHT_FROM); score_paint();
    if(bird_x){ g_dist = pts * 10.0f + 2000; obs_spawn(&g_obs[0]); while(g_obs[0].kind != OB_BIRD) obs_spawn(&g_obs[0]);
                g_obs[0].x = bird_x; lv_obj_set_x(g_obs[0].o, bird_x); g_dist = pts * 10.0f; }
    cat_pose();
}
