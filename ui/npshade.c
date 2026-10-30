/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
/* Quick settings style 2, "Shade" (Display > QS style; one setting for all themes).
 *
 * A pull down from the top edge on any screen but Music lowers a now-playing shade over the top two thirds of the
 * screen: the clock, the cover, title and artist, a thin progress line and previous / play-pause / next, with a grab
 * bar and "pull down for quick settings" at its foot. What is behind dims. A second pull down (or a tap on the grab
 * bar) stretches the shade to full screen and hands over to the theme's own Quick settings; a pull up or a tap on the
 * dimmed part closes it. Style 1, "Classic" (the default), keeps today's behaviour: the pull opens Quick settings.
 * Every theme draws the same shape in its own colours and type. */
#include "npshade.h"
#include "screens.h"
#include "theme.h"
#include "theme_kit.h"
#include "braun.h"
#include "braun2.h"
#include "ipc.h"
#include "simple_theme.h"
#include "musicdb.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SHADE_H   250      /* the first stage reaches this far down */
#define SHADE_MS  150      /* open / close: quick */
#define GROW_MS   170      /* the second pull: to full screen */
#define COVER     112      /* the cover, with play-pause on it and previous / next either side */
#define COVER_Y   36

static lv_obj_t *g_batt, *g_flash, *g_flash_l;
static lv_obj_t *g_ov, *g_scrim, *g_panel, *g_handle, *g_hint, *g_clock, *g_cover, *g_title, *g_artist, *g_bar, *g_btn[3], *g_btn_l[3];
static lv_timer_t *g_tick;
static int g_closing;

int npshade_style(void){ return simple_cfg_get_int("qs_style", 0) == 1; }
int npshade_active(void){ return g_ov != NULL; }

static void text_for_theme(char *out, int cap, const char *s){
    if(th_null()) theme_upper(out, cap, s);
    else snprintf(out, (size_t)cap, "%s", s);
}
static void refresh(void){
    if(!g_ov) return;
    track_state_t st; ipc_get_state(&st);
    char b[192];
    text_for_theme(b, sizeof b, st.have_track && st.title[0] ? st.title : "Not playing"); lv_label_set_text(g_title, b);
    text_for_theme(b, sizeof b, st.have_track ? st.artist : ""); lv_label_set_text(g_artist, b);
    long dur = st.duration_ms > 0 ? st.duration_ms : 1;
    lv_bar_set_value(g_bar, (int32_t)(st.have_track ? st.position_ms * 1000 / dur : 0), LV_ANIM_OFF);
    const void *cv = ui_current_cover_dsc();
    if(cv){ const lv_image_dsc_t *d = cv; lv_image_set_src(g_cover, cv); if(d->header.w) lv_image_set_scale(g_cover, 256 * COVER / d->header.w); lv_obj_remove_flag(g_cover, LV_OBJ_FLAG_HIDDEN); }
    else lv_obj_add_flag(g_cover, LV_OBJ_FLAG_HIDDEN);
    time_t now = time(NULL); struct tm lt; localtime_r(&now, &lt);
    char c[8]; snprintf(c, sizeof c, "%d:%02d", lt.tm_hour, lt.tm_min); lv_label_set_text(g_clock, c);
    int chg = 0, pct = ui_battery(&chg);   /* the battery beside the clock */
    const char *bg = chg ? LV_SYMBOL_CHARGE : pct < 0 ? LV_SYMBOL_BATTERY_3 : pct > 80 ? LV_SYMBOL_BATTERY_FULL : pct > 55 ? LV_SYMBOL_BATTERY_3
                   : pct > 30 ? LV_SYMBOL_BATTERY_2 : pct > 12 ? LV_SYMBOL_BATTERY_1 : LV_SYMBOL_BATTERY_EMPTY;
    lv_label_set_text(g_batt, bg);
    lv_obj_set_style_text_color(g_batt, pct >= 0 && pct <= 12 && !chg ? TC(STATUS_DANGER) : TC(TEXT_SECONDARY), 0);
}
static void tick_cb(lv_timer_t *t){ (void)t; refresh(); }

static void ty_exec(void *o, int32_t v){ lv_obj_set_y((lv_obj_t *)o, v); }
static void h_exec(void *o, int32_t v){ lv_obj_set_height((lv_obj_t *)o, v); }
static void opa_exec(void *o, int32_t v){ lv_obj_set_style_bg_opa((lv_obj_t *)o, (lv_opa_t)v, 0); }
static void fade_exec(void *o, int32_t v){ lv_obj_set_style_opa((lv_obj_t *)o, (lv_opa_t)v, 0); }
static void anim(void *var, lv_anim_exec_xcb_t cb, int32_t a, int32_t b, uint32_t ms, lv_anim_completed_cb_t done){
    lv_anim_t x; lv_anim_init(&x); lv_anim_set_var(&x, var); lv_anim_set_exec_cb(&x, cb); lv_anim_set_values(&x, a, b);
    lv_anim_set_duration(&x, ms); lv_anim_set_path_cb(&x, lv_anim_path_ease_out); if(done) lv_anim_set_completed_cb(&x, done);
    lv_anim_start(&x);
}
static void destroy(void){
    if(g_tick){ lv_timer_delete(g_tick); g_tick = NULL; }
    if(g_ov){ lv_obj_delete(g_ov); g_ov = NULL; }
    g_closing = 0;
}
static void closed_cb(lv_anim_t *a){ (void)a; destroy(); }
void npshade_close(void){
    if(!g_ov || g_closing) return;
    g_closing = 1;
    anim(g_panel, ty_exec, lv_obj_get_y(g_panel), -SHADE_H - 20, SHADE_MS, closed_cb);
    anim(g_scrim, opa_exec, lv_obj_get_style_bg_opa(g_scrim, 0), 0, SHADE_MS, NULL);
    anim(g_hint, fade_exec, 255, 0, SHADE_MS / 2, NULL);
}
/* the second pull: the shade stretches to full screen while its contents fade, then the theme's Quick settings
 * take over (without their own slide, so the shade appears to become them) */
static void grown_cb(lv_anim_t *a){
    (void)a;
    int anim_was = screen_get_anim(); screen_set_anim(0);
    screen_show(SCR_QUICK); quicksettings_refresh(ui_is_playing());
    screen_set_anim(anim_was);
    destroy();
}
void npshade_grow(void);
static void grow(void){ npshade_grow(); }
void npshade_dismiss(void){ destroy(); }   /* gone at once (the Home gesture) */
static lv_point_t g_down;
static void down_ev(lv_event_t *e){ lv_indev_t *in = lv_indev_active(); if(in) lv_indev_get_point(in, &g_down); (void)e; }
static int was_swipe(void){   /* a tap that travelled is a swipe the gesture code handles: no tap action */
    lv_indev_t *in = lv_indev_active(); if(!in) return 0; lv_point_t p; lv_indev_get_point(in, &p);
    return abs(p.x - g_down.x) > 16 || abs(p.y - g_down.y) > 16;
}
void npshade_grow(void){
    if(!g_ov || g_closing) return;
    g_closing = 1;
    lv_obj_t *parts[] = { g_clock, g_batt, g_cover, g_title, g_artist, g_bar, g_btn[0], g_btn[2], g_handle, g_hint };
    for(unsigned i = 0; i < sizeof parts / sizeof parts[0]; i++) anim(parts[i], fade_exec, 255, 0, GROW_MS * 2 / 3, NULL);
    anim(g_panel, h_exec, lv_obj_get_height(g_panel), 360 + 60, GROW_MS, grown_cb);
    anim(g_scrim, opa_exec, lv_obj_get_style_bg_opa(g_scrim, 0), 255, GROW_MS, NULL);
}
static void album_ev(lv_event_t *e){
    if(lv_event_get_code(e) != LV_EVENT_SHORT_CLICKED || was_swipe()) return;
    track_state_t st; ipc_get_state(&st); if(!st.have_track || !st.path[0]) return;
    char dbal[160] = "", dbar[160] = ""; const char *album = st.album;
    if(mdb_song_meta_by_path(st.path, dbal, sizeof dbal, dbar, sizeof dbar) && dbal[0]) album = dbal;   /* the library's own album name */
    if(!album[0]){ ui_toast("No album info"); return; }
    destroy();
    library_open_album_focus(album, dbar[0] ? dbar : st.artist, st.path); screen_show(SCR_LIBRARY);
}
static void handle_ev(lv_event_t *e){ if(lv_event_get_code(e) == LV_EVENT_SHORT_CLICKED && !was_swipe()) grow(); }
static void scrim_ev(lv_event_t *e){ if(lv_event_get_code(e) == LV_EVENT_SHORT_CLICKED && !was_swipe()) npshade_close(); }
static void transport_ev(lv_event_t *e){
    if(lv_event_get_code(e) != LV_EVENT_SHORT_CLICKED || was_swipe()) return;
    ui_transport_command((const char *)lv_event_get_user_data(e)); refresh();
}
static void flash_opa(void *o, int32_t v){ lv_obj_set_style_opa((lv_obj_t *)o, (lv_opa_t)v, 0); }
static void cover_ev(lv_event_t *e){   /* tap the cover: play / pause, confirmed by a glyph that shows briefly and fades */
    if(lv_event_get_code(e) != LV_EVENT_SHORT_CLICKED || was_swipe()) return;
    int was = ui_is_playing();
    if(ui_transport_command("0201000C0000") != 0) return;
    lv_label_set_text(g_flash_l, was ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);   /* what it is now */
    lv_obj_remove_flag(g_flash, LV_OBJ_FLAG_HIDDEN); lv_anim_delete(g_flash, flash_opa);
    lv_anim_t a; lv_anim_init(&a); lv_anim_set_var(&a, g_flash); lv_anim_set_exec_cb(&a, flash_opa);
    lv_anim_set_values(&a, 255, 0); lv_anim_set_delay(&a, 250); lv_anim_set_duration(&a, 450); lv_anim_start(&a);
}

/* the theme's look: panel colour, accent, type */
static lv_color_t panel_col(void){ return th_null() ? TC(CANVAS) : th_zune() ? theme_color_mix(TC(TEXT_PRIMARY), TC(CANVAS), 22) : TC(SURFACE); }
static lv_obj_t *label(lv_obj_t *p, const lv_font_t *f, lv_color_t c, int y, int w){
    lv_obj_t *l = lv_label_create(p); lv_label_set_long_mode(l, LV_LABEL_LONG_DOT); lv_obj_set_width(l, w);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0); lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0); lv_obj_align(l, LV_ALIGN_TOP_MID, 0, y); kit_keep(l); return l;
}
static void button(int k, int x, int y, int d, const char *glyph, const char *cmd){
    lv_obj_t *b = lv_button_create(g_panel); lv_obj_remove_style_all(b); kit_keep(b);
    lv_obj_set_size(b, d, d); lv_obj_align(b, LV_ALIGN_TOP_MID, x, y - d / 2);
    lv_obj_set_ext_click_area(b, 6);
    int filled = 0;   /* play-pause sits on the cover: see-through, not filled */
    lv_obj_set_style_radius(b, th_null() ? 0 : LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(b, filled ? TC(ACCENT_PRIMARY) : theme_color_mix(TC(TEXT_PRIMARY), panel_col(), 26), 0);
    lv_obj_set_style_bg_opa(b, th_null() ? LV_OPA_TRANSP : (filled || th_disco() ? LV_OPA_COVER : LV_OPA_TRANSP), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_50, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(b, th_null() ? 0 : (th_zune() ? 2 : (filled ? 0 : 2)), 0);
    lv_obj_set_style_border_color(b, th_zune() ? TC(TEXT_PRIMARY) : TC(ACCENT_PRIMARY), 0);
    lv_obj_t *l = lv_label_create(b); lv_label_set_text(l, glyph); lv_obj_center(l); kit_keep(l);
    lv_obj_set_style_text_font(l, k == 1 ? TF(UI_24) : TF(UI_20), 0);
    lv_obj_set_style_text_color(l, filled ? TC(CANVAS) : th_zune() ? TC(TEXT_PRIMARY) : TC(ACCENT_PRIMARY), 0);
    lv_obj_add_event_cb(b, transport_ev, LV_EVENT_SHORT_CLICKED, (void *)cmd);
    g_btn[k] = b; g_btn_l[k] = l;
}
void npshade_open(void){
    if(g_ov) return;
    g_ov = lv_obj_create(lv_layer_top()); lv_obj_remove_style_all(g_ov); kit_keep(g_ov);
    lv_obj_set_size(g_ov, 360, 360); lv_obj_remove_flag(g_ov, LV_OBJ_FLAG_SCROLLABLE);
    g_scrim = lv_obj_create(g_ov); lv_obj_remove_style_all(g_scrim); kit_keep(g_scrim);
    lv_obj_set_size(g_scrim, 360, 360); lv_obj_set_style_bg_color(g_scrim, TC(FIXED_MEDIA_BLACK), 0); lv_obj_set_style_bg_opa(g_scrim, 0, 0);
    lv_obj_add_flag(g_scrim, LV_OBJ_FLAG_CLICKABLE); lv_obj_add_event_cb(g_scrim, scrim_ev, LV_EVENT_SHORT_CLICKED, NULL);
    /* the shade: a full-width panel with rounded lower corners (its top runs off the screen) */
    g_panel = lv_obj_create(g_ov); lv_obj_remove_style_all(g_panel); kit_keep(g_panel);
    lv_obj_set_size(g_panel, 360, SHADE_H + 40); lv_obj_set_pos(g_panel, 0, -SHADE_H - 40);
    lv_obj_set_style_radius(g_panel, 104, 0);   /* the lower corners sweep up towards the rim */ lv_obj_set_style_bg_color(g_panel, panel_col(), 0); lv_obj_set_style_bg_opa(g_panel, 250, 0);
    if(th_null()){ lv_obj_set_style_border_width(g_panel, 2, 0); lv_obj_set_style_border_side(g_panel, LV_BORDER_SIDE_BOTTOM, 0);
                   lv_obj_set_style_border_color(g_panel, TC(TEXT_SECONDARY), 0); lv_obj_set_style_radius(g_panel, 0, 0); }
    lv_obj_set_style_pad_top(g_panel, 40, 0);   /* the part above the screen */
    lv_obj_remove_flag(g_panel, LV_OBJ_FLAG_SCROLLABLE); lv_obj_add_flag(g_panel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(g_ov, down_ev, LV_EVENT_PRESSED, NULL);
    lv_color_t tx = TC(TEXT_PRIMARY), mu = TC(TEXT_SECONDARY);
    g_clock = label(g_panel, TF(UI_14), mu, 14, 60); lv_obj_align(g_clock, LV_ALIGN_TOP_MID, -14, 14);
    g_batt = label(g_panel, TF(UI_14), mu, 14, 30); lv_obj_align(g_batt, LV_ALIGN_TOP_MID, 28, 14);   /* clock and battery, side by side */
    g_cover = lv_image_create(g_panel); kit_keep(g_cover); lv_obj_set_size(g_cover, COVER, COVER); lv_image_set_inner_align(g_cover, LV_IMAGE_ALIGN_CENTER);
    lv_obj_align(g_cover, LV_ALIGN_TOP_MID, 0, COVER_Y); lv_obj_set_style_radius(g_cover, th_null() ? 0 : 12, 0); lv_obj_set_style_clip_corner(g_cover, !th_null(), 0);
    lv_obj_add_flag(g_cover, LV_OBJ_FLAG_CLICKABLE); lv_obj_add_event_cb(g_cover, cover_ev, LV_EVENT_SHORT_CLICKED, NULL);   /* tap the cover: play / pause */
    /* title and artist: one line each, larger; a long one scrolls sideways a few times, then rests */
    g_title = label(g_panel, TF(UI_20), tx, COVER_Y + COVER + 10, 264);
    g_artist = label(g_panel, TF(UI_16), th_null() ? TC(TEXT_SECONDARY) : mu, COVER_Y + COVER + 38, 244);
    { const lv_anim_t *disco_scroll5(void);
      lv_obj_t *one[2] = { g_title, g_artist };
      for(int i = 0; i < 2; i++){ lv_label_set_long_mode(one[i], LV_LABEL_LONG_SCROLL_CIRCULAR); lv_obj_set_style_anim(one[i], disco_scroll5(), 0);
                                  lv_obj_set_height(one[i], lv_font_get_line_height(lv_obj_get_style_text_font(one[i], 0)));
                                  lv_obj_add_flag(one[i], LV_OBJ_FLAG_CLICKABLE); lv_obj_set_ext_click_area(one[i], 6);
                                  lv_obj_add_event_cb(one[i], album_ev, LV_EVENT_SHORT_CLICKED, NULL); } }   /* tap: the album */
    g_bar = lv_bar_create(g_panel); kit_keep(g_bar); lv_obj_set_size(g_bar, 200, 4); lv_obj_align(g_bar, LV_ALIGN_TOP_MID, 0, COVER_Y + COVER + 68);
    lv_bar_set_range(g_bar, 0, 1000);
    lv_obj_set_style_bg_color(g_bar, theme_color_mix(TC(TEXT_PRIMARY), panel_col(), 60), LV_PART_MAIN); lv_obj_set_style_bg_opa(g_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(g_bar, TC(ACCENT_PRIMARY), LV_PART_INDICATOR); lv_obj_set_style_bg_opa(g_bar, LV_OPA_COVER, LV_PART_INDICATOR);
    /* previous and next either side of the cover; play-pause on the cover itself, see-through */
    button(0, -(COVER / 2 + 36), COVER_Y + COVER / 2, 48, LV_SYMBOL_PREV, "0201000C0002");
    /* play-pause has no button: a tap on the cover plays or pauses, and a glyph flashes on the cover to confirm (as Disco's gestures do) */
    g_flash = lv_obj_create(g_panel); lv_obj_remove_style_all(g_flash); kit_keep(g_flash);
    lv_obj_set_size(g_flash, 56, 56); lv_obj_align(g_flash, LV_ALIGN_TOP_MID, 0, COVER_Y + COVER / 2 - 28);
    lv_obj_set_style_radius(g_flash, LV_RADIUS_CIRCLE, 0); lv_obj_set_style_bg_color(g_flash, TC(FIXED_MEDIA_BLACK), 0); lv_obj_set_style_bg_opa(g_flash, 140, 0);
    lv_obj_remove_flag(g_flash, LV_OBJ_FLAG_CLICKABLE); lv_obj_add_flag(g_flash, LV_OBJ_FLAG_HIDDEN);
    g_flash_l = lv_label_create(g_flash); kit_keep(g_flash_l); lv_obj_center(g_flash_l);
    lv_obj_set_style_text_font(g_flash_l, TF(UI_24), 0); lv_obj_set_style_text_color(g_flash_l, TC(FIXED_MEDIA_WHITE), 0);
    button(2, COVER / 2 + 36, COVER_Y + COVER / 2, 48, LV_SYMBOL_NEXT, "0201000C0001");
    g_handle = lv_obj_create(g_panel); lv_obj_remove_style_all(g_handle); kit_keep(g_handle);
    lv_obj_set_size(g_handle, 40, 5); lv_obj_align(g_handle, LV_ALIGN_TOP_MID, 0, SHADE_H - 13);
    lv_obj_set_style_radius(g_handle, 3, 0); lv_obj_set_style_bg_color(g_handle, theme_color_mix(TC(TEXT_PRIMARY), panel_col(), 130), 0);
    lv_obj_set_style_bg_opa(g_handle, LV_OPA_COVER, 0);
    lv_obj_add_flag(g_handle, LV_OBJ_FLAG_CLICKABLE); lv_obj_set_ext_click_area(g_handle, 18);
    lv_obj_add_event_cb(g_handle, handle_ev, LV_EVENT_SHORT_CLICKED, NULL);
    g_hint = label(g_ov, TF(UI_18), theme_color_mix(TC(TEXT_PRIMARY), TC(FIXED_MEDIA_BLACK), 200), SHADE_H + 6, 40);   /* pull down: more */
    lv_label_set_text(g_hint, LV_SYMBOL_DOWN);
    { lv_obj_t *tap[] = { g_scrim, g_panel, g_cover, g_title, g_artist, g_btn[0], g_btn[2], g_handle };
      for(unsigned i = 0; i < sizeof tap / sizeof tap[0]; i++) if(tap[i]) lv_obj_add_event_cb(tap[i], down_ev, LV_EVENT_PRESSED, NULL); }
    if(th_braun2()){   /* Braun 2.0: the warm canvas, dark knobs either side of a rounded tile, Inter type, a tuning bar */
        lv_obj_set_style_bg_color(g_panel, TC(CANVAS), 0); lv_obj_set_style_bg_opa(g_panel, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(g_panel, 48, 0); lv_obj_set_style_shadow_color(g_panel, TC(ACTION_SHADOW), 0); lv_obj_set_style_shadow_width(g_panel, 20, 0);
        lv_obj_set_style_text_font(g_clock, br_font(18, 1), 0); lv_obj_set_style_text_font(g_batt, br_font(18, 1), 0);
        lv_obj_set_style_text_color(g_clock, TC(TEXT_PRIMARY), 0); lv_obj_set_style_text_color(g_batt, TC(TEXT_PRIMARY), 0);
        lv_obj_set_width(g_clock, 64); lv_obj_set_width(g_batt, 50); lv_obj_align(g_clock, LV_ALIGN_TOP_MID, -30, 12); lv_obj_align(g_batt, LV_ALIGN_TOP_MID, 34, 12);
        lv_obj_set_style_radius(g_cover, 20, 0); br2_emboss(g_panel, 0);
        lv_obj_set_style_text_font(g_title, br_font(22, 1), 0); lv_obj_set_style_text_font(g_artist, br_font(16, 0), 0);
        lv_obj_set_height(g_title, lv_font_get_line_height(br_font(22, 1))); lv_obj_set_height(g_artist, lv_font_get_line_height(br_font(16, 0)));
        lv_obj_set_size(g_bar, 200, 6); lv_obj_set_style_radius(g_bar, 3, LV_PART_MAIN); lv_obj_set_style_radius(g_bar, 3, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(g_bar, TC(SURFACE_RAISED), LV_PART_MAIN);
        for(int k = 0; k < 3; k += 2) if(g_btn[k]){
            lv_obj_set_style_bg_color(g_btn[k], TC(CONTROL_KNOB), 0); lv_obj_set_style_bg_opa(g_btn[k], LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(g_btn[k], 0, 0); lv_obj_set_style_text_color(g_btn_l[k], TC(ON_CONTROL), 0);
            lv_obj_set_style_shadow_color(g_btn[k], TC(ACCENT_PRIMARY), 0); lv_obj_set_style_shadow_width(g_btn[k], 14, 0); lv_obj_set_style_shadow_opa(g_btn[k], 46, 0);
        }
        lv_obj_set_style_bg_color(g_handle, TC(SURFACE_RAISED), 0);
    }
    refresh();
    g_tick = lv_timer_create(tick_cb, 1000, NULL);
    /* in: the shade slides down and the screen behind dims, together */
    anim(g_panel, ty_exec, -SHADE_H - 40, -40, SHADE_MS, NULL);
    anim(g_scrim, opa_exec, 0, 165, SHADE_MS, NULL);   /* dim enough that the hint reads over any screen */
    anim(g_hint, fade_exec, 0, 255, SHADE_MS, NULL);
}
