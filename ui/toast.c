/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
/* toast.c - short messages as a dark pill near the top rim, with a small icon on the left (the accent by
 * default; a green tick for success). Non-blocking, non-clickable, replaces any prior toast, auto-dismisses.
 * Also the library-rescan indicator: while a scan runs, a small accent dot with a fading trail orbits the rim
 * over whatever is on screen (only those few pixels redraw). */
#include "screens.h"
#include "i18n.h"
#include "theme.h"
#include "scanner.h"
#include "braun.h"
#include <math.h>
#include <string.h>

static lv_obj_t   *g_toast;
static lv_timer_t *g_toast_timer;

static void toast_hide_cb(lv_timer_t *t)
{
    (void)t;
    if(g_toast){ lv_obj_delete_async(g_toast); g_toast = NULL; }
    if(g_toast_timer){ lv_timer_delete(g_toast_timer); g_toast_timer = NULL; }
}
void ui_toast_icon(const char *icon, lv_color_t icol, const char *msg)
{
    if(!msg) return;
    msg = tr(msg);   /* the interface language; text with no entry (a name, a count) shows as given */
    if(g_toast){ lv_obj_delete_async(g_toast); g_toast = NULL; }
    if(g_toast_timer){ lv_timer_delete(g_toast_timer); g_toast_timer = NULL; }

    g_toast = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(g_toast);
    int br = th_braun();                                       /* Braun: an off-white panel, dark text, an orange lamp */
    lv_obj_set_style_bg_color(g_toast, TC(TOAST_SURFACE), 0);
    lv_obj_set_style_bg_opa(g_toast, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(g_toast, TC(TOAST_BORDER), 0);
    if(br){ lv_obj_set_style_shadow_color(g_toast, TC(ACTION_SHADOW), 0); lv_obj_set_style_shadow_width(g_toast, 10, 0); lv_obj_set_style_shadow_offset_y(g_toast, 2, 0); }
    lv_obj_set_style_border_width(g_toast, 1, 0);
    lv_obj_set_style_radius(g_toast, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_hor(g_toast, 16, 0);
    lv_obj_set_style_pad_ver(g_toast, 9, 0);
    lv_obj_set_style_pad_column(g_toast, 9, 0);
    lv_obj_set_size(g_toast, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(g_toast, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(g_toast, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align(g_toast, LV_ALIGN_TOP_MID, 0, 40);          /* near the top rim, inside the circle */
    lv_obj_clear_flag(g_toast, LV_OBJ_FLAG_CLICKABLE);       /* let touches pass through */
    lv_obj_clear_flag(g_toast, LV_OBJ_FLAG_SCROLLABLE);

    if(!icon || !strcmp(icon, LV_SYMBOL_BULLET)){              /* the default mark: a small drawn dot (no glyph needed) */
        lv_obj_t *lamp = lv_obj_create(g_toast); lv_obj_remove_style_all(lamp); lv_obj_set_size(lamp, 8, 8);
        lv_obj_set_style_radius(lamp, LV_RADIUS_CIRCLE, 0); lv_obj_set_style_bg_color(lamp, br ? TC(ACCENT_PRIMARY) : icol, 0); lv_obj_set_style_bg_opa(lamp, LV_OPA_COVER, 0);
    } else {
        lv_obj_t *ic = lv_label_create(g_toast);
        lv_label_set_text(ic, icon ? icon : LV_SYMBOL_BULLET);
        lv_obj_set_style_text_font(ic, TF(UI_14), 0);
        lv_obj_set_style_text_color(ic, br ? TC(ACCENT_PRIMARY) : icol, 0);
    }

    lv_obj_t *l = lv_label_create(g_toast);
    lv_label_set_text(l, msg);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_max_width(l, 172, 0);                   /* the pill stays inside the upper chord */
    lv_obj_set_width(l, LV_SIZE_CONTENT);
    static lv_font_t tf; static int tf_init;                   /* Montserrat -> Inter (has \xC2\xB7 \xE2\x80\x9C \xE2\x80\xA6) -> the international fonts */
    if(!tf_init){ tf = *theme_font_original(14); tf.fallback = br_font(14, 0); tf_init = 1; }
    lv_obj_set_style_text_font(l, br ? br_font(14, 0) : &tf, 0);   /* some toasts embed a filename/name */
    lv_obj_set_style_text_color(l, br ? TC(TEXT_PRIMARY) : TC(TEXT_PRIMARY), 0);

    g_toast_timer = lv_timer_create(toast_hide_cb, 2400, NULL);
}
void ui_toast(const char *msg){ ui_toast_icon(LV_SYMBOL_BULLET, ui_current_accent(), msg); }

/* ---- the rescan orbit ---------------------------------------------------------------------------------- */
#define ORB_N 7
#define ORB_R 170
static lv_obj_t *g_orb[ORB_N];
static lv_timer_t *g_orb_timer;
static int g_orb_on;
static uint32_t g_orb_t0;
static void orb_place(void){
    float a0 = (float)lv_tick_elaps(g_orb_t0) * 0.18f - 90.0f;      /* one lap every 2 s */
    if(th_braun()) a0 = (float)((int)(a0 / 7.5f)) * 7.5f;           /* Braun: steps, like an indicator lamp */
    for(int k = 0; k < ORB_N; k++){
        float a = (a0 - k * 3.4f) * 0.0174533f;
        int sz = lv_obj_get_width(g_orb[k]);
        lv_obj_set_pos(g_orb[k], 180 + (int)lroundf(ORB_R * cosf(a)) - sz / 2, 180 + (int)lroundf(ORB_R * sinf(a)) - sz / 2);
    }
}
static void orb_tick(lv_timer_t *t){ (void)t; if(g_orb_on) orb_place(); }
void ui_scan_orbit(int on){
    on = on ? 1 : 0;
    if(on == g_orb_on) return;
    g_orb_on = on;
    if(!g_orb[0]){
        for(int k = 0; k < ORB_N; k++){
            g_orb[k] = lv_obj_create(lv_layer_top());
            lv_obj_remove_style_all(g_orb[k]);
            int sz = k == 0 ? 10 : 8 - k;                              /* the head, then a shrinking trail */
            if(sz < 3) sz = 3;
            lv_obj_set_size(g_orb[k], sz, sz);
            lv_obj_set_style_radius(g_orb[k], LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_opa(g_orb[k], k == 0 ? LV_OPA_COVER : (lv_opa_t)(220 - k * 30), 0);
            lv_obj_clear_flag(g_orb[k], LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        }
    }
    for(int k = 0; k < ORB_N; k++){
        if(th_disco()){                     /* Disco: over a bright cover the small grey dot got lost - bigger, glowing, CD-white */
            int sz = k == 0 ? 14 : 11 - k; if(sz < 4) sz = 4;
            lv_obj_set_size(g_orb[k], sz, sz);
                        lv_obj_set_style_border_width(g_orb[k], k == 0 ? 2 : 0, 0); lv_obj_set_style_border_color(g_orb[k], ui_current_accent(), 0);
            if(on) lv_obj_move_foreground(g_orb[k]);
        }
        if(th_braun()) lv_obj_set_style_bg_color(g_orb[k], k == 0 ? TC(ACCENT_PRIMARY) : TC(TOAST_ORB), 0);
        else lv_obj_set_style_bg_color(g_orb[k], k == 0 ? TC(TEXT_PRIMARY) : ui_current_accent(), 0);
        if(on) lv_obj_remove_flag(g_orb[k], LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(g_orb[k], LV_OBJ_FLAG_HIDDEN);
    }
    if(on){
        g_orb_t0 = lv_tick_get(); orb_place();
        if(!g_orb_timer) g_orb_timer = lv_timer_create(orb_tick, 50, NULL);   /* 20 fps: smooth enough, cheap */
        else lv_timer_resume(g_orb_timer);
    } else if(g_orb_timer) lv_timer_pause(g_orb_timer);
}
