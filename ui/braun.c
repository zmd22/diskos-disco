/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
/* The Braun theme's building blocks (see braun.h). */
#include "braun.h"
#include <time.h>
#include "theme.h"
#include "config.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "screens.h"
#include "theme_kit.h"

/* ---- Inter (converted from the open-source Inter typeface): Medium 12/14/16, Bold 16/18/22, Bold 44 digits;
 * anything outside Latin-1 falls back to the international fonts (ui_font_cjk) ---- */
extern const lv_font_t font_inter_medium_12, font_inter_medium_14, font_inter_medium_16, font_inter_medium_20,
                       font_inter_bold_16, font_inter_bold_18, font_inter_bold_22, font_inter_bold_44;
extern const lv_font_t diskos_hifi_ui_24; /* bundled Inter SemiBold, reused for Braun's larger track title */
const lv_font_t *br_font(int size, int bold){
    static lv_font_t f[7]; static int init;
    if(!init){
        const lv_font_t *src[7] = { &font_inter_medium_12, &font_inter_medium_14, &font_inter_medium_16,
                                    &font_inter_bold_16, &font_inter_bold_18, &font_inter_bold_22, &font_inter_bold_44 };
        const int fb[7] = { 14, 14, 16, 16, 18, 20, 20 };
        for(int i = 0; i < 7; i++){ f[i] = *src[i]; f[i].fallback = ui_font_cjk(fb[i]); }
        init = 1;
    }
    if(size >= 40) return &f[6];
    if(size == 24 && bold){
        static lv_font_t title24; static int ready;
        if(!ready){ title24 = diskos_hifi_ui_24; title24.fallback = ui_font_cjk(24); ready = 1; }
        return &title24;
    }
    if(bold){ if(size >= 22) return &f[5];
              if(size >= 18) return &f[4];
              return &f[3]; }
    if(size >= 20){ static lv_font_t m20; static int i20; if(!i20){ m20 = font_inter_medium_20; m20.fallback = ui_font_cjk(20); i20 = 1; } return &m20; }
    if(size >= 16) return &f[2];
    if(size >= 14) return &f[1];
    return &f[0];
}

/* Settings > Display > Theme (cfg "ui_theme"): 0 Ring, 1 Braun, 2 Braun Dark. Auto day/night (cfg "theme_auto")
 * runs the Braun family dark from 20:00 to 07:00. Read once at boot; a change restarts the UI (main.c switches
 * at the next screen-off when the hour crosses over). */
static int g_theme = -1;
static int th_index(void){
#ifdef TH_FORCE_BRAUN
    return 1;                                                /* test renders only */
#endif
#ifdef TH_FORCE_BRAUN_DARK
    return 2;
#endif
    if(g_theme < 0){ g_theme = theme_preset()==THEME_PRESET_BRAUN ? (theme_variant() ? 1 : 2) : 0; }
    return g_theme;
}
int th_braun(void){
#ifdef TH_FORCE_BRAUN
    return 1;
#elif defined(TH_FORCE_BRAUN_DARK)
    return 1;
#else
    const theme_def_t *d=theme_def(); return d && d->kit && !strcmp(d->kit->id,"braun");
#endif
}
int th_night_now(void){ time_t now = time(NULL); struct tm tm; localtime_r(&now, &tm); return tm.tm_hour >= 20 || tm.tm_hour < 7; }
/* the variant the current settings and clock ask for (Braun family only) */
int th_want_dark(void){
#ifdef TH_FORCE_BRAUN_DARK
    return 1;
#endif
    if(!th_braun()) return 0;
    if(cfg_get_int("theme_auto", 0)) return th_night_now();
    return th_index() == 2;
}
int br_dark(void){ return !theme_variant(); }

/* ---- the palettes behind theme.h's tokens ---- */
uint32_t th_hex(int token){
    static const theme_color_role_t roles[TH_C_COUNT] = {
        THEME_CLR_CANVAS, THEME_CLR_SURFACE, THEME_CLR_SURFACE_RAISED, THEME_CLR_CONTROL_TRACK,
        THEME_CLR_TEXT_PRIMARY, THEME_CLR_TEXT_SECONDARY, THEME_CLR_TEXT_DISABLED, THEME_CLR_ACCENT_PRIMARY,
        THEME_CLR_TEXT_MUTED, THEME_CLR_TEXT_LYRICS, THEME_CLR_ON_ACCENT
    };
    return (token>=0 && token<TH_C_COUNT) ? theme_rgb(roles[token]) : theme_rgb(THEME_CLR_STATUS_DANGER);
}

/* ---- the grille: one 360x360 image, drawn once into RAM and shared by every screen ---- */
static lv_image_dsc_t g_grille; static uint8_t *g_grille_px;
static const lv_image_dsc_t *grille(void){
    if(g_grille_px) return &g_grille;
    const int W = 360, stride = W * 4;
    g_grille_px = malloc((size_t)stride * W); if(!g_grille_px) return NULL;
    uint32_t bg = BR_BG, dot = BR_DOT;
    for(int i = 0; i < W * W; i++) ((uint32_t *)g_grille_px)[i] = 0xFF000000u | bg;
    /* dots every 8 px, radius ~1.6 px, anti-aliased by coverage */
    for(int cy = 4; cy < W; cy += 8) for(int cx = 4; cx < W; cx += 8)
        for(int y = cy - 2; y <= cy + 2; y++) for(int x = cx - 2; x <= cx + 2; x++){
            if(x < 0 || y < 0 || x >= W || y >= W) continue;
            float dx = x + 0.5f - (cx + 0.5f), dy = y + 0.5f - (cy + 0.5f), dd = dx * dx + dy * dy;
            float cov = 2.2f - dd; if(cov <= 0) continue; if(cov > 1) cov = 1;
            uint32_t *p = &((uint32_t *)g_grille_px)[y * W + x];
            int br_ = (int)((bg >> 16) & 255), bg_ = (int)((bg >> 8) & 255), bb_ = (int)(bg & 255);
            int dr_ = (int)((dot >> 16) & 255), dg_ = (int)((dot >> 8) & 255), db_ = (int)(dot & 255);
            int r = br_ + (int)((dr_ - br_) * cov), g = bg_ + (int)((dg_ - bg_) * cov), b = bb_ + (int)((db_ - bb_) * cov);
            *p = 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
        }
    memset(&g_grille, 0, sizeof g_grille);
    g_grille.header.magic = LV_IMAGE_HEADER_MAGIC; g_grille.header.cf = LV_COLOR_FORMAT_XRGB8888;
    g_grille.header.w = W; g_grille.header.h = W; g_grille.header.stride = stride;
    g_grille.data = g_grille_px; g_grille.data_size = (uint32_t)stride * W;
    return &g_grille;
}
void br_face(lv_obj_t *root){
    lv_obj_set_style_bg_color(root, TC(CANVAS), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    const lv_image_dsc_t *g = grille();
    if(g){ lv_obj_t *im = lv_image_create(root); lv_image_set_src(im, g); lv_obj_set_pos(im, 0, 0);
           lv_obj_clear_flag(im, LV_OBJ_FLAG_CLICKABLE); lv_obj_move_to_index(im, 0); }
}
lv_obj_t *br_segment(lv_obj_t *root, int y){
    lv_obj_t *s = lv_obj_create(root);
    lv_obj_remove_style_all(s);
    lv_obj_set_pos(s, 0, y); lv_obj_set_size(s, 360, 360 - y);    /* the round screen draws its lower edge */
    lv_obj_set_style_bg_color(s, TC(SURFACE_STRONG), 0); lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
    lv_obj_set_style_border_side(s, LV_BORDER_SIDE_TOP, 0); lv_obj_set_style_border_width(s, 1, 0);
    lv_obj_set_style_border_color(s, TC(BORDER), 0);
    lv_obj_clear_flag(s, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return s;
}
lv_obj_t *br_disc(lv_obj_t *parent, int cx, int cy, int r, uint32_t col){
    lv_obj_t *d = lv_obj_create(parent);
    lv_obj_remove_style_all(d);
    lv_obj_set_size(d, 2 * r, 2 * r); lv_obj_set_pos(d, cx - r, cy - r);
    lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(d, theme_color_from_rgb(col), 0); lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
    lv_obj_clear_flag(d, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return d;
}
lv_obj_t *br_label(lv_obj_t *parent, const char *txt, const lv_font_t *font, uint32_t col){
    lv_obj_t *l = lv_label_create(parent); lv_label_set_text(l, txt);
    if(font) lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, theme_color_from_rgb(col), 0);
    return l;
}
/* a dark knob: a turned edge, the icon in the middle, a short pointer; "on" = orange pointer + a lamp above */
lv_obj_t *br_knob(lv_obj_t *parent, int cx, int cy, int r, const char *icon, const lv_font_t *font){
    lv_obj_t *k = lv_obj_create(parent);
    lv_obj_remove_style_all(k);
    lv_obj_set_size(k, 2 * r, 2 * r); lv_obj_set_pos(k, cx - r, cy - r);
    lv_obj_set_style_radius(k, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(k, TC(CONTROL_KNOB), 0); lv_obj_set_style_bg_opa(k, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(k, TC(CONTROL_FILL), 0); lv_obj_set_style_border_width(k, 2, 0);
    lv_obj_set_style_shadow_color(k, TC(ACTION_SHADOW), 0); lv_obj_set_style_shadow_width(k, 6, 0); lv_obj_set_style_shadow_offset_y(k, 2, 0);
    lv_obj_clear_flag(k, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *l = lv_label_create(k); lv_label_set_text(l, icon ? icon : "");
    if(font) lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, TC(ON_CONTROL), 0); lv_obj_center(l);
    lv_obj_t *p = lv_obj_create(k);                              /* the pointer, at 1 o'clock */
    lv_obj_remove_style_all(p); lv_obj_set_size(p, 3, r / 3);
    lv_obj_set_style_bg_color(p, TC(GRABBER), 0); lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(p, 1, 0); lv_obj_align(p, LV_ALIGN_TOP_MID, r / 3, 3);
    lv_obj_set_style_transform_rotation(p, 300, 0);
    lv_obj_clear_flag(p, LV_OBJ_FLAG_CLICKABLE);
    br_pointer_set(p, 2 * r, 0);                                 /* straight up in the icon colour, as on the switches */
    lv_obj_t *lamp = lv_obj_create(parent);                      /* the lamp above the knob */
    lv_obj_remove_style_all(lamp); lv_obj_set_size(lamp, 6, 6); lv_obj_set_pos(lamp, cx - 3, cy - r - 11);
    lv_obj_set_style_radius(lamp, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(lamp, TC(ACCENT_PRIMARY), 0); lv_obj_set_style_bg_opa(lamp, LV_OPA_COVER, 0);
    lv_obj_clear_flag(lamp, LV_OBJ_FLAG_CLICKABLE); lv_obj_add_flag(lamp, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_user_data(k, lamp);
    return k;
}
/* fork: an ON/OFF knob's pointer. OFF = straight up (0 deg) in the icon's colour; ON = turned to 45 deg, orange.
 * No lamp. d = the knob's diameter; the pointer (3 x len) rides just inside the edge. */
void br_pointer_set(lv_obj_t *p, int d, int on){
    if(!p) return;
    int len = lv_obj_get_height(p); if(len <= 0) len = d / 6;
    float a = (on ? 45.0f : 0.0f) * 3.14159265f / 180.0f, rr = d / 2.0f - 4 - len / 2.0f;
    int cx = (int)lroundf(d / 2.0f + rr * sinf(a)), cy = (int)lroundf(d / 2.0f - rr * cosf(a));
    lv_obj_set_align(p, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(p, cx - 1, cy - len / 2);
    lv_obj_set_style_transform_pivot_x(p, 1, 0); lv_obj_set_style_transform_pivot_y(p, len / 2, 0);
    lv_obj_set_style_transform_rotation(p, on ? 450 : 0, 0);
    lv_obj_set_style_bg_color(p, on ? TC(ACCENT_PRIMARY) : TC(ON_CONTROL), 0);
}
void br_knob_set_on(lv_obj_t *k, int on){
    if(!k) return;
    lv_obj_t *p = lv_obj_get_child(k, 1), *lamp = lv_obj_get_user_data(k);
    lv_obj_update_layout(k);
    br_pointer_set(p, lv_obj_get_width(k), on);
    if(lamp) lv_obj_add_flag(lamp, LV_OBJ_FLAG_HIDDEN);          /* fork: no lamp - the pointer says it */
}
lv_obj_t *br_button(lv_obj_t *parent, int cx, int cy, int r, const char *icon, const lv_font_t *font, int primary){
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, 2 * r, 2 * r); lv_obj_set_pos(b, cx - r, cy - r);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(b, primary ? TC(ACCENT_PRIMARY) : TC(SURFACE), 0); lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, primary ? TC(PRIMARY_PRESSED) : TC(SURFACE_PRESSED), LV_STATE_PRESSED);
    lv_obj_set_ext_click_area(b, 6);
    lv_obj_t *l = lv_label_create(b); lv_label_set_text(l, icon ? icon : "");
    if(font) lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, primary ? TC(ON_ACCENT) : TC(TEXT_PRIMARY), 0); lv_obj_center(l);
    return b;
}
void br_style_switch(lv_obj_t *sw){                          /* a slot with a dark slider; the slot goes orange-tinted when on */
    lv_obj_set_size(sw, 44, 20);
    lv_obj_set_style_bg_color(sw, TC(SURFACE), LV_PART_MAIN);
    lv_obj_set_style_border_color(sw, TC(DOT_GRID), LV_PART_MAIN); lv_obj_set_style_border_width(sw, 1, LV_PART_MAIN);
    lv_obj_set_style_bg_color(sw, TC(SWITCH_SELECTED), (lv_style_selector_t)LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(sw, TC(CONTROL_KNOB), LV_PART_KNOB);
    lv_obj_set_style_radius(sw, 5, LV_PART_KNOB);
    lv_obj_set_style_pad_all(sw, -2, LV_PART_KNOB);
}

/* ---- the Braun clock face: drawn once into a canvas (ticks, optional numerals; 3 o'clock left free for the
 * weather window). Only the hands move afterwards. ---- */
lv_obj_t *br_clock_face(lv_obj_t *parent, int cx, int cy, int rd, int numerals){
    const int D = 2 * rd + 4;
    uint8_t *buf = malloc(LV_CANVAS_BUF_SIZE(D, D, 32, LV_DRAW_BUF_STRIDE_ALIGN));
    if(!buf) return NULL;
    lv_obj_t *cv = lv_canvas_create(parent);
    lv_canvas_set_buffer(cv, buf, D, D, LV_COLOR_FORMAT_ARGB8888);
    lv_obj_set_pos(cv, cx - D / 2, cy - D / 2);
    lv_obj_clear_flag(cv, LV_OBJ_FLAG_CLICKABLE);
    lv_canvas_fill_bg(cv, TC(SURFACE_STRONG), LV_OPA_TRANSP);
    lv_layer_t L; lv_canvas_init_layer(cv, &L);
    lv_draw_rect_dsc_t rdsc; lv_draw_rect_dsc_init(&rdsc);
    rdsc.bg_color = TC(SURFACE_STRONG); rdsc.radius = LV_RADIUS_CIRCLE; rdsc.bg_opa = LV_OPA_COVER;
    lv_area_t a = { 0, 0, D - 1, D - 1 }; lv_draw_rect(&L, &rdsc, &a);
    rdsc.bg_color = TC(SURFACE); lv_area_t b = { 5, 5, D - 6, D - 6 }; lv_draw_rect(&L, &rdsc, &b);
    float c = D / 2.0f;
    for(int k = 0; k < 60; k++){
        float an = (-90 + k * 6) * 0.0174533f; int major = k % 5 == 0;
        float r1 = major ? rd - (numerals ? 19 : 24) : rd - 10, r2 = rd - 4;
        lv_draw_line_dsc_t ld; lv_draw_line_dsc_init(&ld);
        ld.color = major ? TC(TEXT_PRIMARY) : TC(TEXT_DISABLED); ld.width = major ? (numerals ? 3 : 4) : 1;
        ld.p1.x = c + r1 * cosf(an); ld.p1.y = c + r1 * sinf(an); ld.p2.x = c + r2 * cosf(an); ld.p2.y = c + r2 * sinf(an);
        lv_draw_line(&L, &ld);
    }
    if(numerals) for(int n = 1; n <= 12; n++){
        if(n == 3) continue;
        static const char *const NUM[13] = { "", "1","2","3","4","5","6","7","8","9","10","11","12" };   /* drawn later: must outlive the loop */
        float an = (-90 + n * 30) * 0.0174533f;
        lv_draw_label_dsc_t td; lv_draw_label_dsc_init(&td);
        td.color = TC(TEXT_PRIMARY); td.font = br_font(14, 0); td.text = NUM[n]; td.align = LV_TEXT_ALIGN_CENTER;
        int x = (int)(c + (rd - 33) * cosf(an)), y = (int)(c + (rd - 33) * sinf(an));
        lv_area_t ta = { x - 12, y - 9, x + 12, y + 9 }; lv_draw_label(&L, &td, &ta);
    }
    lv_canvas_finish_layer(cv, &L);
    return cv;
}
/* the weather window at 3 o'clock: "<icon> 14°" (Braun prints just the degrees) */
static lv_font_t s_wxf;
lv_obj_t *br_weather_window(lv_obj_t *parent, int x, int y, lv_obj_t **label_out){
    extern const lv_font_t font_weather16;
    lv_obj_t *w = lv_obj_create(parent);
    lv_obj_remove_style_all(w);
    lv_obj_set_size(w, 48, 26); lv_obj_set_pos(w, x, y);
    lv_obj_set_style_radius(w, 4, 0); lv_obj_set_style_bg_color(w, TC(SURFACE_STRONG), 0); lv_obj_set_style_bg_opa(w, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(w, TC(TEXT_DISABLED), 0); lv_obj_set_style_border_width(w, 1, 0);
    lv_obj_clear_flag(w, LV_OBJ_FLAG_SCROLLABLE);
    s_wxf = *br_font(12, 0); s_wxf.fallback = &font_weather16;
    lv_obj_t *l = br_label(w, "", &s_wxf, BR_TXT); lv_obj_center(l);
    if(label_out) *label_out = l;
    return w;
}
void br_weather_text(lv_obj_t *label, const char *s){
    if(!label) return;
    char o[48] = {0}; int n = 0;
    while(s && *s == ' ') s++;
    for(int i = 0; s && s[i] && n < (int)sizeof o - 1; i++){
        if(s[i] == ' ' && n > 0 && o[n-1] == ' ') continue;
        if((s[i] == 'C' || s[i] == 'F') && n > 1 && (unsigned char)o[n-1] == 0xB0) continue;
        if(s[i] == ' ' && s[i+1] == ' '){ o[n] = 0;                                   /* stop before the condition (o was not terminated yet) */
                                         if(n > 3 && strchr(o, 0xB0)) break; }
        o[n++] = s[i];
    }
    while(n > 0 && o[n-1] == ' ') n--;
    o[n] = 0; lv_label_set_text(label, o);
}
