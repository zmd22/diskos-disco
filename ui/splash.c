/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
/* splash.c - the diskOS boot splash: the Disc loads a disc.
 *
 * A drawing of the Snowsky Disc (in the body colour set in Settings > Display > Disc Colour) rises into view; a CD
 * rolls into its window from the right, seats on the spindle with a click of light and spins up; "diskOS" appears
 * beneath. Then the drawing grows until its round window is exactly the real screen, and the spinning disc gives way
 * to Home underneath. 2.4 s, once per boot, on lv_layer_top; it swallows taps and deletes itself.
 *
 * Cost on the GPU-less renderer: about 30 plain objects (rounded rectangles, circles, arcs) moved and resized from one
 * timeline; nothing is scaled as an image. The window clips the CD only while it rolls in (a circular mask is the
 * one costly draw here); once seated the CD fits inside the window and the clip is turned off.
 * Integer maths only (the timeline runs every frame). */
#include "screens.h"
#include "theme.h"
#include "theme_fonts.h"
#include "config.h"
#include "theme_kit.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SP_MS    2400          /* the whole splash */
#define SP_BASE  176           /* the drawn Disc's width while it is a drawing */
#define SP_FULL  450           /* its width when the window (40% of it, as a radius) reaches the screen edge */
#define SP_BANDS 5

static struct {
    lv_obj_t *root, *stage, *btn, *body, *bezel, *well, *spindle, *cd, *band[2 * SP_BANDS], *hub, *hubl[3], *hole,
             *marks, *click, *word;
    int clip, frames;
    uint32_t t0;
} g_sp;

/* 0..1024 fixed point */
static int clampk(int v){ return v < 0 ? 0 : v > 1024 ? 1024 : v; }
static int win_k(int t, int a, int b){ return clampk((t - a) * 1024 / (b - a)); }
static int ease_out(int k){ int r = 1024 - k; return 1024 - (int)((int64_t)r * r * r >> 20); }   /* cubic out */
static int scl(int v, int k){ return (int)((int64_t)v * k >> 10); }

/* every splash object is marked styled: it lives on the top layer, where a theme's styling pass would otherwise
 * restyle it as a popup (square corners, theme borders). The splash is a fixed identity in every theme. */
static lv_obj_t *shape(lv_obj_t *parent, int radius){
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    kit_keep(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(o, radius, 0);
    return o;
}
static lv_obj_t *circle(lv_obj_t *parent, lv_color_t fill, lv_opa_t opa){
    lv_obj_t *o = shape(parent, LV_RADIUS_CIRCLE);
    lv_obj_set_style_bg_color(o, fill, 0);
    lv_obj_set_style_bg_opa(o, opa, 0);
    return o;
}
static lv_obj_t *ring_line(lv_obj_t *parent, lv_color_t c, lv_opa_t opa){   /* an outline circle */
    lv_obj_t *o = shape(parent, LV_RADIUS_CIRCLE);
    lv_obj_set_style_border_color(o, c, 0);
    lv_obj_set_style_border_opa(o, opa, 0);
    lv_obj_set_style_border_width(o, 1, 0);
    return o;
}
static lv_obj_t *arc(lv_obj_t *parent, lv_color_t c, lv_opa_t opa){           /* a bare arc: MAIN part only */
    lv_obj_t *a = lv_arc_create(parent);
    lv_obj_remove_style_all(a);
    kit_keep(a);
    lv_obj_remove_flag(a, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_arc_color(a, c, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(a, opa, LV_PART_MAIN);
    lv_obj_set_style_arc_color(a, c, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(a, opa, LV_PART_INDICATOR);
    return a;
}
static void place(lv_obj_t *o, int cx, int cy, int r){   /* a circle of radius r centred on (cx, cy) */
    if(r < 1) r = 1;
    lv_obj_set_pos(o, cx - r, cy - r);
    lv_obj_set_size(o, 2 * r, 2 * r);
}

/* one frame of the timeline, t in ms */
static void splash_frame(int t){
    int rise = ease_out(win_k(t, 0, 500));                          /* the Disc rises in */
    int g = win_k(t, 1700, 2250); g = g * g >> 10;                  /* then grows, gathering speed */
    int size = SP_BASE + scl(SP_FULL - SP_BASE, g);
    int cx = 180, cy = 180;
    int r = size * 9 / 100, win = size * 40 / 100;                  /* from the product photo: screen ~80% of the body */

    lv_obj_set_style_opa(g_sp.stage, (lv_opa_t)(rise * 255 >> 10), 0);
    lv_obj_set_y(g_sp.stage, scl(14, 1024 - rise));

    lv_obj_set_style_radius(g_sp.body, r, 0);
    lv_obj_set_pos(g_sp.body, cx - size / 2, cy - size / 2);
    lv_obj_set_size(g_sp.body, size, size);
    int bw = size * 12 / 100, bh = 4 + size / 176;                  /* the button on the top edge, near the right */
    lv_obj_set_pos(g_sp.btn, cx + size * 28 / 100, cy - size / 2 - bh + 1);
    lv_obj_set_size(g_sp.btn, bw, bh + 2);
    place(g_sp.bezel, cx, cy, win + 3 * size / SP_BASE);
    lv_obj_set_style_border_width(g_sp.bezel, 1 + size / 300, 0);
    place(g_sp.well, cx, cy, win);
    place(g_sp.spindle, win, win, win * 11 / 100);

    /* the CD: rolls in from the right 500..1050, overshoots the spindle a touch and settles, then spins up */
    int R = win * 99 / 100, dx = 2 * win + win / 20, rot = 0;
    if(t >= 500){
        int p = ease_out(win_k(t, 500, 1050));
        dx = scl(2 * win + win / 20, 1024 - p);
        rot = -scl(120, 1024 - p);                                  /* it rolls as it travels */
        if(t > 1050 && t < 1230){
            int q = win_k(t, 1050, 1230);
            dx -= scl(scl(5 * size / SP_BASE, 1024 - q), lv_trigo_sin((int16_t)(q * 180 >> 10)) >> 5);
        }
    }
    if(t > 1150){ int64_t s = t - 1150; rot = (int)(940 * s * s / 1000000); }
    int clip = t < 1230;                                            /* the CD sits inside the window once seated */
    if(clip != g_sp.clip){ g_sp.clip = clip; lv_obj_set_style_clip_corner(g_sp.well, clip, 0); }
    place(g_sp.cd, win + dx, win, R);

    for(int i = 0; i < SP_BANDS; i++){                              /* the light it throws: fixed to the light, not the disc */
        int br = R * (69 + 21 * i) / 178, mid = 325 + 3 * i;
        for(int s = 0; s < 2; s++){
            lv_obj_t *a = g_sp.band[2 * i + s];
            place(a, R, R, br + scl(R * 21 / 178, 512));
            lv_obj_set_style_arc_width(a, R * 21 / 178, LV_PART_MAIN);
            lv_arc_set_bg_angles(a, (mid + 180 * s - 15) % 360, (mid + 180 * s + 15) % 360);
        }
    }
    int hr[3] = { 56, 44, 34 };
    place(g_sp.hub, R, R, R * 56 / 178);
    for(int i = 0; i < 3; i++) place(g_sp.hubl[i], R, R, R * hr[i] / 178);
    place(g_sp.hole, R, R, R * 22 / 178);
    int mr = R * 72 / 178;                                          /* two marks show it turning; they blur out at speed */
    place(g_sp.marks, R, R, mr);
    lv_obj_set_style_arc_width(g_sp.marks, R * 6 / 178 + 1, LV_PART_MAIN);
    lv_obj_set_style_arc_width(g_sp.marks, R * 6 / 178 + 1, LV_PART_INDICATOR);
    lv_arc_set_rotation(g_sp.marks, ((rot % 360) + 360) % 360);
    lv_obj_set_style_opa(g_sp.marks, (lv_opa_t)(255 - scl(180, win_k(t, 1500, 1950))), 0);

    int ck = 1024 - clampk(abs(t - 1120) * 1024 / 90);              /* the click of light as it seats */
    lv_obj_set_style_opa(g_sp.click, (lv_opa_t)(ck * 204 >> 10), 0);
    place(g_sp.click, cx, cy, win * 14 / 100 + scl(6, 1024 - ck));

    /* the name, once, beneath the Disc; it leaves as the Disc grows */
    int wm = scl(ease_out(win_k(t, 1150, 1550)), 1024 - win_k(t, 1700, 1950));
    lv_obj_set_style_opa(g_sp.word, (lv_opa_t)(wm * 255 >> 10), 0);
    lv_obj_set_y(g_sp.word, cy + size / 2 + 17 + scl(14, 1024 - rise));

    lv_obj_set_style_opa(g_sp.root, (lv_opa_t)(255 - (win_k(t, 2150, SP_MS) * 255 >> 10)), 0);   /* hand-off to Home */
}

static void splash_anim_cb(void *var, int32_t v){ (void)var; if(g_sp.root) splash_frame((int)v); }
static void splash_drawn_cb(lv_event_t *e){ (void)e; g_sp.frames++; }   /* a frame actually drawn to the screen */
static void splash_done(lv_anim_t *a){
    (void)a;
    /* one line in the boot log: how smoothly this Disc drew it */
    lv_display_remove_event_cb_with_user_data(lv_display_get_default(), splash_drawn_cb, NULL);
    fprintf(stderr, "splash: %d frames drawn in %u ms\n", g_sp.frames, (unsigned)lv_tick_elaps(g_sp.t0));
    if(g_sp.root) lv_obj_delete(g_sp.root);
    memset(&g_sp, 0, sizeof g_sp);
}

void splash_start(void){
    if(g_sp.root) return;                                           /* one splash at a time */
    static const theme_color_role_t BODY[3][2] = {
        { THEME_CLR_FIXED_SPLASH_BLACK_HI, THEME_CLR_FIXED_SPLASH_BLACK_LO },
        { THEME_CLR_FIXED_SPLASH_TURQ_HI,  THEME_CLR_FIXED_SPLASH_TURQ_LO },
        { THEME_CLR_FIXED_SPLASH_PINK_HI,  THEME_CLR_FIXED_SPLASH_PINK_LO } };
    static const theme_color_role_t BAND[SP_BANDS] = { THEME_CLR_FIXED_SPLASH_BAND_1, THEME_CLR_FIXED_SPLASH_BAND_2,
        THEME_CLR_FIXED_SPLASH_BAND_3, THEME_CLR_FIXED_SPLASH_BAND_4, THEME_CLR_FIXED_SPLASH_BAND_5 };
    int col = cfg_get_int("disc_colour", 0);
    if(col < 0 || col > 2) col = 0;

    g_sp.root = shape(lv_layer_top(), 0);
    lv_obj_set_size(g_sp.root, 360, 360);
    lv_obj_set_style_bg_color(g_sp.root, TC(FIXED_SPLASH_BG), 0);
    lv_obj_set_style_bg_opa(g_sp.root, LV_OPA_COVER, 0);
    lv_obj_add_flag(g_sp.root, LV_OBJ_FLAG_CLICKABLE);             /* swallow taps during the splash */

    g_sp.stage = shape(g_sp.root, 0);
    lv_obj_set_size(g_sp.stage, 360, 360);
    g_sp.btn = shape(g_sp.stage, 2);                                /* behind the body: only its top shows */
    lv_obj_set_style_bg_color(g_sp.btn, theme_color(BODY[col][1]), 0);
    lv_obj_set_style_bg_opa(g_sp.btn, LV_OPA_COVER, 0);
    g_sp.body = shape(g_sp.stage, 0);
    lv_obj_set_style_bg_color(g_sp.body, theme_color(BODY[col][0]), 0);
    lv_obj_set_style_bg_grad_color(g_sp.body, theme_color(BODY[col][1]), 0);
    lv_obj_set_style_bg_grad_dir(g_sp.body, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(g_sp.body, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(g_sp.body, TC(FIXED_SPLASH_3), 0);   /* the machined edge catching the light */
    lv_obj_set_style_border_opa(g_sp.body, 46, 0);
    lv_obj_set_style_border_width(g_sp.body, 1, 0);
    g_sp.bezel = circle(g_sp.stage, TC(FIXED_SPLASH_RING), LV_OPA_COVER);
    lv_obj_set_style_border_color(g_sp.bezel, TC(FIXED_SPLASH_3), 0);
    lv_obj_set_style_border_opa(g_sp.bezel, 70, 0);
    g_sp.well = circle(g_sp.stage, TC(FIXED_SPLASH_WELL), LV_OPA_COVER);
    g_sp.spindle = circle(g_sp.well, TC(FIXED_SPLASH_SPINDLE), LV_OPA_COVER);

    g_sp.cd = circle(g_sp.well, TC(FIXED_SPLASH_1), LV_OPA_COVER);
    lv_obj_set_style_bg_grad_color(g_sp.cd, TC(FIXED_SPLASH_2), 0);
    lv_obj_set_style_bg_grad_dir(g_sp.cd, LV_GRAD_DIR_VER, 0);
    for(int i = 0; i < 2 * SP_BANDS; i++) g_sp.band[i] = arc(g_sp.cd, theme_color(BAND[i / 2]), 80);
    g_sp.hub = circle(g_sp.cd, TC(FIXED_SPLASH_3), 140);
    for(int i = 0; i < 3; i++) g_sp.hubl[i] = ring_line(g_sp.cd, TC(FIXED_SPLASH_HUB_LINE), 140);
    g_sp.hole = circle(g_sp.cd, TC(FIXED_SPLASH_HOLE), LV_OPA_COVER);
    g_sp.marks = arc(g_sp.cd, TC(FIXED_SPLASH_MARK), 150);
    lv_arc_set_bg_angles(g_sp.marks, 0, 12);                        /* two marks, opposite */
    lv_arc_set_angles(g_sp.marks, 180, 192);

    g_sp.click = ring_line(g_sp.stage, TC(FIXED_SPLASH_3), LV_OPA_COVER);
    lv_obj_set_style_border_width(g_sp.click, 2, 0);

    /* "diskOS": a light "disk", a bold "OS" */
    g_sp.word = shape(g_sp.root, 0);
    lv_obj_set_size(g_sp.word, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(g_sp.word, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(g_sp.word, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    const lv_font_t *F[2] = { &diskos_splash_light_26, &diskos_splash_bold_26 };
    const char *T[2] = { "disk", "OS" };
    for(int i = 0; i < 2; i++){
        lv_obj_t *l = lv_label_create(g_sp.word);
        kit_keep(l);
        lv_label_set_text(l, T[i]);
        lv_obj_set_style_text_font(l, F[i], 0);
        lv_obj_set_style_text_color(l, TC(FIXED_SPLASH_3), 0);
    }
    lv_obj_update_layout(g_sp.word);
    lv_obj_set_x(g_sp.word, 180 - lv_obj_get_width(g_sp.word) / 2);

    g_sp.clip = -1;
    g_sp.t0 = lv_tick_get();
    lv_display_add_event_cb(lv_display_get_default(), splash_drawn_cb, LV_EVENT_RENDER_READY, NULL);
    splash_frame(0);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, g_sp.root);
    lv_anim_set_values(&a, 0, SP_MS);
    lv_anim_set_duration(&a, SP_MS);
    lv_anim_set_exec_cb(&a, splash_anim_cb);
    lv_anim_set_completed_cb(&a, splash_done);
    lv_anim_start(&a);
}
