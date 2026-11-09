#include "simple_theme.h"
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "screens.h"
#include "theme.h"
#include "theme_kit.h"
#include "config.h"
#include "theme.h"
#include <stdint.h>
#include <stdio.h>
#include <math.h>

/* Accent colour picker: choose "Album Art" (dynamic) or ANY fixed colour via
 * Hue/Saturation/Brightness sliders with a live preview. Writes cfg accent_mode
 * (0=dynamic,1=static) + accent_color (0xRRGGBB as a decimal int) and applies live
 * through ui_set_accent_config(). */

static lv_obj_t *g_hsl, *g_ssl, *g_vsl;   /* hue/sat/val sliders */
static lv_obj_t *g_preview;                /* colour swatch */
static int g_loading = 0;                  /* suppress callbacks while seeding sliders */

static void hsv2rgb(int h,int s,int v,int*r,int*g,int*b){
    float S=s/100.0f, V=v/100.0f;
    float C=V*S, X=C*(1.0f-fabsf(fmodf(h/60.0f,2.0f)-1.0f)), m=V-C, rr,gg,bb;
    if(h<60){rr=C;gg=X;bb=0;} else if(h<120){rr=X;gg=C;bb=0;} else if(h<180){rr=0;gg=C;bb=X;}
    else if(h<240){rr=0;gg=X;bb=C;} else if(h<300){rr=X;gg=0;bb=C;} else {rr=C;gg=0;bb=X;}
    *r=(int)((rr+m)*255+0.5f); *g=(int)((gg+m)*255+0.5f); *b=(int)((bb+m)*255+0.5f);
}
static void rgb2hsv(int r,int g,int b,int*h,int*s,int*v){
    float R=r/255.0f,G=g/255.0f,B=b/255.0f;
    float mx=fmaxf(R,fmaxf(G,B)), mn=fminf(R,fminf(G,B)), d=mx-mn, H=0;
    if(d>0){ if(mx==R)H=fmodf((G-B)/d,6.0f); else if(mx==G)H=(B-R)/d+2.0f; else H=(R-G)/d+4.0f;
             H*=60.0f; if(H<0)H+=360.0f; }
    *h=(int)(H+0.5f); if(*h>=360)*h=359;
    *s=(int)((mx>0?d/mx:0)*100+0.5f); *v=(int)(mx*100+0.5f);
}

static int g_mode = 1, g_rgb = UI_RED;   /* the ring section below reads these */
/* Squash: its own orange is a third choice. Any other choice switches Squash to the shared Accent Colour first, so the
 * accent keys below are the shared ones (simple_cfg scopes them to Squash's own while it keeps its orange). */
void ui_set_accent_config(int mode, int rgb);
static int squash_orange(void){ return th_simple() && !theme_squash_follows_accent(); }
static void squash_take(void){
    if(!squash_orange()) return;
    cfg_set_int("squash_accent", 1);
    g_mode = simple_cfg_get_int("accent_mode", 1);   /* now the shared colour */
}
static int g_pending_rgb = UI_RED;       /* last live colour; persisted on slider release */


/* a slider moved -> recompute colour, preview + apply LIVE (no cfg write on drag). */
static void slider_cb(lv_event_t *e){
    (void)e;
    if(g_loading) return;
    int h = lv_slider_get_value(g_hsl);
    int s = lv_slider_get_value(g_ssl);
    int v = lv_slider_get_value(g_vsl);
    int r,gg,b; hsv2rgb(h,s,v,&r,&gg,&b);
    int rgb = (r<<16)|(gg<<8)|b;
    g_pending_rgb = rgb;
    squash_take();
    if(g_preview) lv_obj_set_style_bg_color(g_preview, theme_color_from_rgb(rgb), 0);
    g_mode = 1; g_rgb = rgb;                                       /* leaving dynamic: the Custom colour is now the accent */
    ui_set_accent_config(1, rgb);
}
/* persist only when the finger lifts -> one durable cfg write per adjustment, not per tick. */
static void slider_release_cb(lv_event_t *e){
    (void)e;
    if(g_loading) return;
    simple_cfg_set_int("accent_mode", 1);
    simple_cfg_set_int("accent_color", g_pending_rgb);
}


/* seed the sliders + preview from the saved colour (called on open) */
static lv_obj_t *mk_slider(lv_obj_t *root, int y, int max){
    lv_obj_t *sl = lv_slider_create(root);
    lv_obj_set_pos(sl, 62, y); lv_obj_set_size(sl, 236, 12);   /* a knob at 0 lines up with its label, not over it */
    lv_obj_set_ext_click_area(sl, 16);   /* 12px slider -> ~44px grab band on the capacitive panel */
    lv_slider_set_range(sl, 0, max);
    lv_obj_set_style_bg_color(sl, TC(SURFACE_RAISED), LV_PART_MAIN);
    lv_obj_set_style_bg_color(sl, TC(TEXT_MUTED), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(sl, TC(TEXT_PRIMARY), LV_PART_KNOB);
    lv_obj_add_event_cb(sl, slider_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(sl, slider_release_cb, LV_EVENT_RELEASED, NULL);
    /* a drag that ends in press-lost (finger slid off) must persist too, else the live
     * static accent never gets written and reverts to dynamic on next boot. */
    lv_obj_add_event_cb(sl, slider_release_cb, LV_EVENT_PRESS_LOST, NULL);
    return sl;
}
static void mk_label(lv_obj_t *root, int y, const char *txt){
    lv_obj_t *l = lv_label_create(root);
    lv_label_set_text(l, txt); lv_obj_set_pos(l, 56, y);
    lv_obj_set_style_text_color(l, TC(TEXT_MUTED), 0);
    lv_obj_set_style_text_font(l, TF(UI_14), 0);
}

/* ---- the ring ---------------------------------------------------------------------------------------
 * Ten preset colours on a ring, the current one outlined, and repeated in the middle with its name. "Follow
 * album" takes the colour from each album; "Custom" opens the full hue / saturation / brightness sliders
 * (the previous screen), for any colour the presets don't have. */
#define NSW 10
static const uint32_t SW_RGB[NSW] = { 0xE4122C, 0xFF9500, 0xFFCC00, 0x34C759, 0x00C7BE, 0x0A84FF, 0x5E5CE6, 0xBF5AF2, 0xFF375F, 0xAC8E68 };
static const char *const SW_NAME[NSW] = { "Red", "Orange", "Yellow", "Green", "Teal", "Blue", "Indigo", "Purple", "Pink", "Sand" };
static lv_obj_t *g_sw[NSW], *g_hub, *g_hub_glyph, *g_hub_name, *g_follow, *g_custom_btn, *g_custom;
#define SW_SQUASH 1                                         /* Squash: the Orange preset is Squash's own orange */

static int preset_of(int rgb){ for(int i = 0; i < NSW; i++) if((int)SW_RGB[i] == (rgb & 0xFFFFFF)) return i; return -1; }
static void paint(void){                                    /* selection, hub and the two pills from g_mode / g_rgb */
    int orange = squash_orange();
    int sel = orange ? SW_SQUASH : g_mode ? preset_of(g_rgb) : -1;
    if(th_simple() && !orange && sel == SW_SQUASH) sel = -1;   /* that swatch means Squash orange here */
    for(int i = 0; i < NSW; i++){
        lv_obj_set_style_border_width(g_sw[i], i == sel ? 3 : 0, 0);
        lv_obj_set_style_border_color(g_sw[i], TC(TEXT_PRIMARY), 0);
    }
    lv_color_t hc = orange ? TC(ACCENT_PRIMARY) : g_mode ? theme_color_from_rgb((uint32_t)g_rgb) : ui_media_accent();
    lv_obj_set_style_bg_color(g_hub, hc, 0);
    lv_label_set_text(g_hub_glyph, orange || g_mode ? LV_SYMBOL_OK : LV_SYMBOL_IMAGE);
    lv_label_set_text(g_hub_name, orange ? "Squash orange" : !g_mode ? "Album" : sel >= 0 ? SW_NAME[sel] : "Custom");
    if(th_simple()) lv_obj_set_style_bg_color(g_sw[SW_SQUASH], theme_color_from_rgb(theme_squash_orange()), 0);
    lv_obj_set_style_bg_color(g_follow, !orange && !g_mode ? ui_current_accent() : TC(SURFACE), 0);
    lv_obj_set_style_bg_color(g_custom_btn, (!orange && g_mode && sel < 0) ? ui_current_accent() : TC(SURFACE), 0);
    lv_obj_t *pl[] = { g_follow, g_custom_btn };   /* Squash: the chosen pill's text reads on the accent */
    if(th_simple()) for(int i = 0; i < 2; i++) if(pl[i]){
        lv_obj_t *l = lv_obj_get_child(pl[i], 0); int on = lv_color_eq(lv_obj_get_style_bg_color(pl[i], 0), ui_current_accent());
        if(l) lv_obj_set_style_text_color(l, on ? theme_on_color(ui_current_accent()) : TC(TEXT_PRIMARY), 0);
    }
}
static void squash_orange_pick(void);
static void swatch_cb(lv_event_t *e){
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if(i < 0 || i >= NSW) return;
    if(th_simple() && i == SW_SQUASH){ squash_orange_pick(); return; }
    squash_take();
    g_mode = 1; g_rgb = (int)SW_RGB[i];
    simple_cfg_set_int("accent_mode", 1); simple_cfg_set_int("accent_color", g_rgb);
    ui_set_accent_config(1, g_rgb);
    paint();
}
static void follow_cb(lv_event_t *e){
    (void)e;
    squash_take();
    g_mode = 0;
    simple_cfg_set_int("accent_mode", 0);
    ui_set_accent_config(0, 0);
    paint();
}
static void squash_orange_pick(void){                       /* Squash: back to its own orange, live */
    if(squash_orange()) return;
    cfg_set_int("squash_accent", 0);
    uint32_t oa, os;
    if(theme_squash_reset_palette(&oa, &os)) simple_accent_changed(oa, os);
    ui_set_accent_config(simple_cfg_get_int("accent_mode", 1), simple_cfg_get_int("accent_color", UI_RED));   /* Squash's own */
    paint();
}
static void hub_done_cb(lv_event_t *e){ (void)e; screen_back(); }
static void custom_open_cb(lv_event_t *e){ (void)e; if(g_custom) lv_obj_remove_flag(g_custom, LV_OBJ_FLAG_HIDDEN); }
static void custom_done_cb(lv_event_t *e){
    (void)e;
    if(g_custom) lv_obj_add_flag(g_custom, LV_OBJ_FLAG_HIDDEN);
    g_mode = simple_cfg_get_int("accent_mode", 1); g_rgb = simple_cfg_get_int("accent_color", UI_RED);
    paint();
}


/* ---- Zune (Figma 77): twelve swatches round the rim, the pick big in the middle in its own colour, with a switch and
 * a progress line to preview it on; "apply" re-launches the screen in it (the theme's palette is fixed per launch) ---- */
LV_FONT_DECLARE(font_zune_light_32) LV_FONT_DECLARE(font_zune_semibold_11) LV_FONT_DECLARE(font_zune_regular_13)
LV_FONT_DECLARE(font_zune_regular_14)
const lv_font_t *zune_font(const lv_font_t *base);
lv_obj_t *zune_pivot(lv_obj_t *root, const char *title, lv_event_cb_t back_cb);
#define ZNSW 12
static const uint32_t ZSW_RGB[ZNSW] = { 0xEC008C, 0xA200FF, 0x0050EF, 0x1BA1E2, 0x00ABA9, 0x008A00,
                                        0xA4C400, 0xF0A30A, 0xFA6800, 0xE51400, 0xA20025, 0xF472D0 };
static const char *const ZSW_NAME[ZNSW] = { "magenta", "purple", "cobalt", "cyan", "teal", "emerald",
                                            "lime", "amber", "orange", "red", "crimson", "pink" };
static lv_obj_t *g_zsw[ZNSW], *g_zring[ZNSW], *g_ztick[ZNSW], *g_zname, *g_zsw_on, *g_zfill, *g_zapply;
static int g_zpick, g_zcur;
static void zpaint(void){
    lv_color_t c = theme_color_from_rgb(ZSW_RGB[g_zpick]);
    for(int i = 0; i < ZNSW; i++){
        int on = i == g_zpick;
        lv_obj_set_size(g_zsw[i], on ? 34 : 28, on ? 34 : 28);
        if(on) lv_obj_remove_flag(g_zring[i], LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(g_zring[i], LV_OBJ_FLAG_HIDDEN);
        if(on) lv_obj_remove_flag(g_ztick[i], LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(g_ztick[i], LV_OBJ_FLAG_HIDDEN);
    }
    lv_label_set_text(g_zname, ZSW_NAME[g_zpick]); lv_obj_set_style_text_color(g_zname, c, 0);
    lv_obj_set_style_bg_color(g_zsw_on, c, 0); lv_obj_set_style_bg_color(g_zfill, c, 0);
    lv_label_set_text(g_zapply, g_zpick == g_zcur ? "current" : "apply");
    lv_obj_set_style_text_color(g_zapply, g_zpick == g_zcur ? TC(TEXT_MUTED) : c, 0);
}
static void zswatch_cb(lv_event_t *e){ g_zpick = (int)(intptr_t)lv_event_get_user_data(e); zpaint(); }
static void zapply_cb(lv_event_t *e){
    (void)e;
    if(g_zpick == g_zcur){ screen_back(); return; }
    int prev = cfg_get_int("zune_accent", 0xEC008C);
    cfg_set_int("zune_accent", (int)ZSW_RGB[g_zpick]);
    ui_toast("Restarting the screen - your music keeps playing");
    if(ui_theme_reload("settings") != 0){ cfg_set_int("zune_accent", prev); ui_toast("Couldn't change it - try again"); }
}
static lv_obj_t *g_zart_l;
void ui_zune_art_sync(void);
static void zart_paint(void){
    int on = cfg_get_int("zune_accent_art", 0);
    lv_label_set_text(g_zart_l, on ? "from album art \xC2\xB7 on" : "from album art \xC2\xB7 off");
    lv_obj_set_style_text_color(g_zart_l, on ? TC(ACCENT_PRIMARY) : TC(TEXT_SECONDARY), 0);
}
static void zart_cb(lv_event_t *e){
    (void)e;
    int on = !cfg_get_int("zune_accent_art", 0);
    cfg_set_int("zune_accent_art", on); ui_zune_art_sync(); ui_art_retry();
    ui_toast(on ? "now playing takes its colour from the album" : "now playing uses your accent");
    zart_paint();
}
static void zcp_open(void){
    int cur = cfg_get_int("zune_accent", 0xEC008C); g_zcur = 0;
    for(int i = 0; i < ZNSW; i++) if((int)ZSW_RGB[i] == (cur & 0xFFFFFF)) g_zcur = i;
    g_zpick = g_zcur; zpaint();
    if(g_zart_l) zart_paint();
}
static void zcp_create(lv_obj_t *root){
    lv_obj_set_style_bg_color(root, TC(CANVAS), 0); lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    for(int i = 0; i < ZNSW; i++){
        float a = (-90.0f + i * 30.0f) * 0.0174533f;
        int cx = 180 + (int)lroundf(146 * cosf(a)), cy = 180 + (int)lroundf(146 * sinf(a));
        lv_obj_t *r = lv_obj_create(root); lv_obj_remove_style_all(r); kit_keep(r); g_zring[i] = r;
        lv_obj_set_size(r, 46, 46); lv_obj_set_pos(r, cx - 23, cy - 23); lv_obj_set_style_radius(r, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(r, 2, 0); lv_obj_set_style_border_color(r, TC(TEXT_PRIMARY), 0);
        lv_obj_remove_flag(r, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *d = lv_obj_create(root); lv_obj_remove_style_all(d); kit_keep(d); g_zsw[i] = d;
        lv_obj_set_size(d, 28, 28); lv_obj_align(d, LV_ALIGN_TOP_LEFT, 0, 0);
        lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0); lv_obj_set_style_bg_color(d, theme_color_from_rgb(ZSW_RGB[i]), 0);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
        lv_obj_set_align(d, LV_ALIGN_CENTER); lv_obj_set_pos(d, cx - 180, cy - 180);
        lv_obj_remove_flag(d, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *t = lv_label_create(d); kit_keep(t); g_ztick[i] = t; lv_label_set_text(t, LV_SYMBOL_OK);
        lv_obj_set_style_text_font(t, TF(UI_14), 0); lv_obj_set_style_text_color(t, TC(FIXED_MEDIA_WHITE), 0); lv_obj_center(t);
        lv_obj_t *h = lv_obj_create(root); lv_obj_remove_style_all(h); kit_keep(h);   /* a small target, a wide hit area */
        lv_obj_set_size(h, 20, 20); lv_obj_set_pos(h, cx - 10, cy - 10);
        lv_obj_add_flag(h, LV_OBJ_FLAG_CLICKABLE); lv_obj_remove_flag(h, LV_OBJ_FLAG_SCROLLABLE); lv_obj_set_ext_click_area(h, 10);
        lv_obj_add_event_cb(h, zswatch_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
    lv_obj_t *cap = lv_label_create(root); kit_keep(cap); lv_label_set_text(cap, "ACCENT COLOUR");
    lv_obj_set_style_text_font(cap, &font_zune_semibold_11, 0); lv_obj_set_style_text_letter_space(cap, 2, 0);
    lv_obj_set_style_text_color(cap, TC(TEXT_MUTED), 0); lv_obj_align(cap, LV_ALIGN_TOP_MID, 0, 104);
    g_zname = lv_label_create(root); kit_keep(g_zname);
    lv_obj_set_style_text_font(g_zname, zune_font(&font_zune_light_32), 0); lv_obj_align(g_zname, LV_ALIGN_TOP_MID, 0, 120);
    g_zsw_on = lv_obj_create(root); lv_obj_remove_style_all(g_zsw_on); kit_keep(g_zsw_on);   /* a sample switch, on */
    lv_obj_set_size(g_zsw_on, 34, 18); lv_obj_set_pos(g_zsw_on, 116, 180);
    lv_obj_set_style_radius(g_zsw_on, LV_RADIUS_CIRCLE, 0); lv_obj_set_style_bg_opa(g_zsw_on, LV_OPA_COVER, 0);
    lv_obj_remove_flag(g_zsw_on, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *k = lv_obj_create(g_zsw_on); lv_obj_remove_style_all(k); kit_keep(k);
    lv_obj_set_size(k, 12, 12); lv_obj_set_style_radius(k, LV_RADIUS_CIRCLE, 0); lv_obj_set_style_bg_opa(k, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(k, TC(FIXED_MEDIA_WHITE), 0); lv_obj_align(k, LV_ALIGN_RIGHT_MID, -3, 0);
    lv_obj_t *sl = lv_label_create(root); kit_keep(sl); lv_label_set_text(sl, "shuffle all");
    lv_obj_set_style_text_font(sl, zune_font(&font_zune_regular_13), 0); lv_obj_set_style_text_color(sl, TC(TEXT_PRIMARY), 0);
    lv_obj_set_pos(sl, 160, 180);
    lv_obj_t *tr = lv_obj_create(root); lv_obj_remove_style_all(tr); kit_keep(tr);           /* a sample progress line */
    lv_obj_set_size(tr, 120, 3); lv_obj_set_pos(tr, 120, 214); lv_obj_set_style_bg_color(tr, TC(CONTROL_TRACK), 0);
    lv_obj_set_style_bg_opa(tr, LV_OPA_COVER, 0); lv_obj_remove_flag(tr, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    g_zfill = lv_obj_create(tr); lv_obj_remove_style_all(g_zfill); kit_keep(g_zfill);
    lv_obj_set_size(g_zfill, 70, 3); lv_obj_set_style_bg_opa(g_zfill, LV_OPA_COVER, 0);
    lv_obj_remove_flag(g_zfill, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    g_zapply = lv_label_create(root); kit_keep(g_zapply);
    lv_obj_set_style_text_font(g_zapply, zune_font(&font_zune_regular_14), 0); lv_obj_align(g_zapply, LV_ALIGN_TOP_MID, 0, 240);
    lv_obj_add_flag(g_zapply, LV_OBJ_FLAG_CLICKABLE); lv_obj_set_ext_click_area(g_zapply, 14);
    lv_obj_add_event_cb(g_zapply, zapply_cb, LV_EVENT_CLICKED, NULL);
    g_zart_l = lv_label_create(root); kit_keep(g_zart_l);                        /* Figma 77: "from album art" */
    lv_obj_set_style_text_font(g_zart_l, zune_font(&font_zune_regular_13), 0); lv_obj_align(g_zart_l, LV_ALIGN_TOP_MID, 0, 268);
    lv_obj_add_flag(g_zart_l, LV_OBJ_FLAG_CLICKABLE); lv_obj_set_ext_click_area(g_zart_l, 12);
    lv_obj_add_event_cb(g_zart_l, zart_cb, LV_EVENT_CLICKED, NULL);
    zcp_open();
}
void colorpick_open(void){
    if(th_zune()){ if(g_zname) zcp_open(); screen_show(SCR_COLORPICK); return; }
    g_mode = simple_cfg_get_int("accent_mode", 1);
    g_rgb  = simple_cfg_get_int("accent_color", UI_RED);
    int h,s,v; rgb2hsv((g_rgb>>16)&0xFF,(g_rgb>>8)&0xFF,g_rgb&0xFF,&h,&s,&v);
    g_loading = 1;                                            /* seed the Custom sliders */
    if(g_hsl) lv_slider_set_value(g_hsl, h, LV_ANIM_OFF);
    if(g_ssl) lv_slider_set_value(g_ssl, s, LV_ANIM_OFF);
    if(g_vsl) lv_slider_set_value(g_vsl, v, LV_ANIM_OFF);
    g_loading = 0;
    if(g_preview) lv_obj_set_style_bg_color(g_preview, theme_color_from_rgb((uint32_t)g_rgb), 0);
    if(g_custom) lv_obj_add_flag(g_custom, LV_OBJ_FLAG_HIDDEN);
    if(g_hub) paint();
    screen_show(SCR_COLORPICK);
}

static lv_obj_t *pill(lv_obj_t *row, const char *txt, lv_event_cb_t cb){   /* sized to its text in any theme's font */
    lv_obj_t *b = lv_button_create(row);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, LV_SIZE_CONTENT, 26); lv_obj_set_style_pad_hor(b, 9, 0);
    lv_obj_set_style_radius(b, 12, 0);
    lv_obj_set_style_bg_color(b, TC(SURFACE), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_ext_click_area(b, 4);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l = lv_label_create(b); lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, TH_F_CAPTION, 0); lv_obj_set_style_text_color(l, TC(TEXT_PRIMARY), 0); lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
    return b;
}
void colorpick_create(lv_obj_t *root){
    if(th_zune()){ zcp_create(root); return; }
    lv_obj_set_style_bg_color(root, TC(CANVAS), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *t = lv_label_create(root); lv_label_set_text(t, "Accent colour");
    lv_obj_set_style_text_font(t, TH_F_CAPTION, 0); lv_obj_set_style_text_color(t, TC(TEXT_SECONDARY), 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 16);
    for(int i = 0; i < NSW; i++){                            /* the ring of presets */
        float a = (-90.0f + i * 36.0f) * 0.0174533f;
        lv_obj_t *b = lv_button_create(root);
        g_sw[i] = b;
        lv_obj_remove_style_all(b);
        lv_obj_set_size(b, 34, 34);
        lv_obj_align(b, LV_ALIGN_CENTER, (int32_t)lroundf(124 * cosf(a)), (int32_t)lroundf(124 * sinf(a)));
        lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(b, theme_color_from_rgb(SW_RGB[i]), 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_set_style_outline_width(b, 0, 0);
        lv_obj_set_ext_click_area(b, 4);
        lv_obj_add_event_cb(b, swatch_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
    g_hub = lv_button_create(root);                          /* the current colour; tap = done */
    lv_obj_remove_style_all(g_hub);
    lv_obj_set_size(g_hub, 78, 78); lv_obj_align(g_hub, LV_ALIGN_CENTER, 0, -28);
    lv_obj_set_style_radius(g_hub, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(g_hub, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(g_hub, hub_done_cb, LV_EVENT_CLICKED, NULL);
    g_hub_glyph = lv_label_create(g_hub);
    lv_obj_set_style_text_font(g_hub_glyph, TF(UI_20), 0); lv_obj_set_style_text_color(g_hub_glyph, TC(TEXT_PRIMARY), 0); lv_obj_center(g_hub_glyph);
    g_hub_name = lv_label_create(root);
    lv_obj_set_style_text_font(g_hub_name, TH_F_DETAIL, 0); lv_obj_set_style_text_color(g_hub_name, TC(TEXT_PRIMARY), 0);
    lv_obj_align(g_hub_name, LV_ALIGN_CENTER, 0, 24);
    lv_obj_t *row = lv_obj_create(root); lv_obj_remove_style_all(row);   /* the two pills, centred as a pair */
    lv_obj_set_size(row, LV_SIZE_CONTENT, 26); lv_obj_align(row, LV_ALIGN_TOP_MID, 0, 233);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW); lv_obj_set_style_pad_column(row, 6, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    g_follow = pill(row, "Follow album", follow_cb);
    g_custom_btn = pill(row, "Custom", custom_open_cb);

    /* Custom: the hue / saturation / brightness sliders (the previous screen, unchanged) over the ring */
    g_custom = lv_obj_create(root);
    lv_obj_remove_style_all(g_custom);
    lv_obj_set_size(g_custom, 360, 360);
    lv_obj_set_style_bg_color(g_custom, TC(CANVAS), 0);
    lv_obj_set_style_bg_opa(g_custom, LV_OPA_COVER, 0);
    lv_obj_add_flag(g_custom, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(g_custom, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *ct = lv_label_create(g_custom); lv_label_set_text(ct, "Custom colour");
    lv_obj_set_style_text_font(ct, TH_F_CAPTION, 0); lv_obj_set_style_text_color(ct, TC(TEXT_SECONDARY), 0);
    lv_obj_align(ct, LV_ALIGN_TOP_MID, 0, 16);
    g_preview = lv_obj_create(g_custom);
    lv_obj_remove_style_all(g_preview);
    lv_obj_set_size(g_preview, 54, 54); lv_obj_set_pos(g_preview, 153, 46);
    lv_obj_set_style_radius(g_preview, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(g_preview, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(g_preview, TC(CONTROL_TRACK), 0);
    lv_obj_set_style_border_width(g_preview, 2, 0);
    mk_label(g_custom, 118, "Hue");        g_hsl = mk_slider(g_custom, 136, 359);
    mk_label(g_custom, 164, "Saturation"); g_ssl = mk_slider(g_custom, 182, 100);
    mk_label(g_custom, 210, "Brightness"); g_vsl = mk_slider(g_custom, 228, 100);
    lv_obj_t *dn = lv_button_create(g_custom);
    lv_obj_remove_style_all(dn);
    lv_obj_set_size(dn, 120, 38); lv_obj_set_pos(dn, 120, 266);
    lv_obj_set_style_radius(dn, 19, 0);
    lv_obj_set_style_bg_color(dn, TC(SURFACE), 0);
    lv_obj_set_style_bg_opa(dn, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(dn, custom_done_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *dl = lv_label_create(dn); lv_label_set_text(dl, "Done");
    lv_obj_set_style_text_color(dl, TC(TEXT_PRIMARY), 0); lv_obj_center(dl);
    lv_obj_add_flag(g_custom, LV_OBJ_FLAG_HIDDEN);
    g_mode = simple_cfg_get_int("accent_mode", 1); g_rgb = simple_cfg_get_int("accent_color", UI_RED);
    paint();
}
