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
    if(g_preview) lv_obj_set_style_bg_color(g_preview, theme_color_from_rgb(rgb), 0);
    g_mode = 1; g_rgb = rgb;                                       /* leaving dynamic: the Custom colour is now the accent */
    ui_set_accent_config(1, rgb);
}
/* persist only when the finger lifts -> one durable cfg write per adjustment, not per tick. */
static void slider_release_cb(lv_event_t *e){
    (void)e;
    if(g_loading) return;
    cfg_set_int("accent_mode", 1);
    cfg_set_int("accent_color", g_pending_rgb);
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

static int preset_of(int rgb){ for(int i = 0; i < NSW; i++) if((int)SW_RGB[i] == (rgb & 0xFFFFFF)) return i; return -1; }
static void paint(void){                                    /* selection, hub and the two pills from g_mode / g_rgb */
    int sel = g_mode ? preset_of(g_rgb) : -1;
    for(int i = 0; i < NSW; i++){
        lv_obj_set_style_border_width(g_sw[i], i == sel ? 3 : 0, 0);
        lv_obj_set_style_border_color(g_sw[i], TC(TEXT_PRIMARY), 0);
    }
    lv_color_t hc = g_mode ? theme_color_from_rgb((uint32_t)g_rgb) : ui_media_accent();
    lv_obj_set_style_bg_color(g_hub, hc, 0);
    lv_label_set_text(g_hub_glyph, g_mode ? LV_SYMBOL_OK : LV_SYMBOL_IMAGE);
    lv_label_set_text(g_hub_name, !g_mode ? "Album" : sel >= 0 ? SW_NAME[sel] : "Custom");
    lv_obj_set_style_bg_color(g_follow, !g_mode ? ui_current_accent() : TC(SURFACE), 0);
    lv_obj_set_style_bg_color(g_custom_btn, (g_mode && sel < 0) ? ui_current_accent() : TC(SURFACE), 0);
}
static void swatch_cb(lv_event_t *e){
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if(i < 0 || i >= NSW) return;
    g_mode = 1; g_rgb = (int)SW_RGB[i];
    cfg_set_int("accent_mode", 1); cfg_set_int("accent_color", g_rgb);
    ui_set_accent_config(1, g_rgb);
    paint();
}
static void follow_cb(lv_event_t *e){
    (void)e;
    g_mode = 0;
    cfg_set_int("accent_mode", 0);
    ui_set_accent_config(0, 0);
    paint();
}
static void hub_done_cb(lv_event_t *e){ (void)e; screen_back(); }
static void custom_open_cb(lv_event_t *e){ (void)e; if(g_custom) lv_obj_remove_flag(g_custom, LV_OBJ_FLAG_HIDDEN); }
static void custom_done_cb(lv_event_t *e){
    (void)e;
    if(g_custom) lv_obj_add_flag(g_custom, LV_OBJ_FLAG_HIDDEN);
    g_mode = cfg_get_int("accent_mode", 1); g_rgb = cfg_get_int("accent_color", UI_RED);
    paint();
}

void colorpick_open(void){
    g_mode = cfg_get_int("accent_mode", 1);
    g_rgb  = cfg_get_int("accent_color", UI_RED);
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

static lv_obj_t *pill(lv_obj_t *root, const char *txt, int x, int w, lv_event_cb_t cb){
    lv_obj_t *b = lv_button_create(root);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, w, 24); lv_obj_set_pos(b, x, 234);
    lv_obj_set_style_radius(b, 12, 0);
    lv_obj_set_style_bg_color(b, TC(SURFACE), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_ext_click_area(b, 4);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l = lv_label_create(b); lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, TH_F_CAPTION, 0); lv_obj_set_style_text_color(l, TC(TEXT_PRIMARY), 0); lv_obj_center(l);
    return b;
}
void colorpick_create(lv_obj_t *root){
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
    g_follow = pill(root, "Follow album", 96, 84, follow_cb);
    g_custom_btn = pill(root, "Custom", 184, 76, custom_open_cb);

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
    g_mode = cfg_get_int("accent_mode", 1); g_rgb = cfg_get_int("accent_color", UI_RED);
    paint();
}
