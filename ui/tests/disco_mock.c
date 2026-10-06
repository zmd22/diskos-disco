/* SPDX-License-Identifier: GPL-3.0-or-later */
/* "Disco" theme concept (cover-backdrop design) mockups - plain LVGL, the way the theme would really be built on the Disc:
 * the album's pre-blurred 360px backdrop (made once per album by the cover decoder) + a dark wash, flat
 * translucent panels (no live blur), 2 px accent borders instead of glows, a hub disc for the section,
 * page dots on the right rim. Included by host_render.c (mode disco-<n>). Colours are literal here
 * only because this is a mockup; the real theme gets palette roles. */
#include "lvgl/lvgl.h"
#include "fork_theme.h"
#include <string.h>
#include <math.h>
#define font_disco_clk lv_font_montserrat_48   /* the real theme: a generated 64-72 px clock face */

LV_FONT_DECLARE(font_inter_bold_44);
LV_FONT_DECLARE(font_inter_bold_22);
LV_FONT_DECLARE(font_inter_bold_18);
LV_FONT_DECLARE(font_inter_medium_20);
LV_FONT_DECLARE(font_inter_medium_16);
LV_FONT_DECLARE(font_inter_medium_14);
LV_FONT_DECLARE(font_theme_20);

static int g_light;                                  /* light variant: lightened cover, dark grey text */
static uint32_t g_acc_rgb = 0x22D3EE;                /* accent: picked (cyan here) or taken from the cover */
#define G_ACC   lv_color_hex(g_acc_rgb)
#define G_TXT   lv_color_hex(g_light ? 0x1E2228 : 0xFFFFFF)
#define G_TXT2  lv_color_hex(g_light ? 0x5A616B : 0xB8C2CC)
#define G_PANEL lv_color_hex(g_light ? 0xFFFFFF : 0x10151B)
#define G_LINE  lv_color_hex(g_light ? 0x000000 : 0xFFFFFF)
#define G_WASH  lv_color_hex(g_light ? 0xF4F2EE : 0x000000)
#define G_HUB   lv_color_hex(0x10151B)               /* the menu picker stays black in both variants */
#define G_DIR   "/tmp/claude-0/glass"
static lv_obj_t *g_box(lv_obj_t *p, int x, int y, int w, int h, int r, lv_color_t c, lv_opa_t o){
    lv_obj_t *b = lv_obj_create(p); lv_obj_remove_style_all(b);
    lv_obj_set_pos(b, x, y); lv_obj_set_size(b, w, h);
    lv_obj_set_style_radius(b, r, 0); lv_obj_set_style_bg_color(b, c, 0); lv_obj_set_style_bg_opa(b, o, 0);
    lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    return b;
}
static lv_obj_t *g_lbl(lv_obj_t *p, const char *t, const lv_font_t *f, lv_color_t c){
    lv_obj_t *l = lv_label_create(p); lv_label_set_text(l, t);
    lv_obj_set_style_text_font(l, f, 0); lv_obj_set_style_text_color(l, c, 0);
    return l;
}
static int g_retro;                                  /* 90s touches: CD hub, segment clock, segmented progress */
static void g_arc(lv_obj_t *p, int cx, int cy, int r, int w, int a0, int a1, uint32_t c, lv_opa_t o){
    lv_obj_t *a = lv_arc_create(p); lv_obj_remove_style_all(a);
    lv_obj_set_size(a, 2 * r, 2 * r); lv_obj_set_pos(a, cx - r, cy - r); lv_arc_set_bg_angles(a, a0, a1);
    lv_obj_set_style_arc_width(a, w, LV_PART_MAIN); lv_obj_set_style_arc_color(a, lv_color_hex(c), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(a, o, LV_PART_MAIN); lv_obj_set_style_arc_rounded(a, 0, LV_PART_MAIN); lv_obj_remove_flag(a, LV_OBJ_FLAG_CLICKABLE);
}
static const uint8_t G_SEG[10] = { 0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F };
static void g_segclock(lv_obj_t *root, const char *txt, int y){     /* LCD-style digits with faint ghost segments */
    int w = 34, h = 62, t = 7, x = 180 - (4 * (w + t + 4) + t + 6) / 2;
    for(const char *c = txt; *c; c++){
        if(*c == ':'){ g_box(root, x + 1, y + h / 3 - t / 2, t, t, 2, G_TXT, LV_OPA_COVER); g_box(root, x + 1, y + 2 * h / 3 - t / 2, t, t, 2, G_TXT, LV_OPA_COVER);
            x += t + 6; continue; }
        int m = G_SEG[*c - '0'], hh = h / 2;
        int S[7][4] = { { t, 0, w - 2 * t, t }, { w - t, t, t, hh - t }, { w - t, hh, t, hh - t }, { t, h - t, w - 2 * t, t },
                        { 0, hh, t, hh - t }, { 0, t, t, hh - t }, { t, hh - t / 2, w - 2 * t, t } };
        for(int i = 0; i < 7; i++){ int on = (m >> i) & 1;
            g_box(root, x + S[i][0], y + S[i][1], S[i][2], S[i][3], 2, on ? G_TXT : lv_color_hex(0xFFFFFF), on ? LV_OPA_COVER : 34); }
        x += w + t + 4;
    }
}



static void g_backdrop(lv_obj_t *root, int sharp, lv_opa_t wash){
    lv_obj_t *im = lv_image_create(root);
    lv_image_set_src(im, sharp ? "A:" G_DIR "/sharp.bmp" : "A:" G_DIR "/out/b.bmp");
    lv_obj_set_pos(im, 0, 0);
    if(wash) g_box(root, 0, 0, 360, 360, 0, G_WASH, g_light ? (lv_opa_t)(wash + 90 > 255 ? 255 : wash + 90) : wash);
}
static void g_battery(lv_obj_t *root, int y){
    lv_obj_t *b = g_lbl(root, LV_SYMBOL_BATTERY_3, &lv_font_montserrat_16, G_TXT);
    lv_obj_align(b, LV_ALIGN_TOP_MID, 0, y);
}
/* the section hub: a dark disc on the left with the section icon + name, up/down chevrons, level dots */
/* the navigation hub: the current main section (configurable list - here Music, Library, Settings, Modes, Shortcuts,
 * EQ). Up/down = previous/next section, a tap opens its top level, a hold always returns to Music. The dots follow
 * the hub's left edge, one per section. */
#define G_NSEC 6
static const char *const G_SEC_ICON[G_NSEC] = { LV_SYMBOL_AUDIO, LV_SYMBOL_LIST, LV_SYMBOL_SETTINGS, LV_SYMBOL_USB, LV_SYMBOL_HOME, "EQ" };
static const char *const G_SEC_NAME[G_NSEC] = { "Music", "Library", "Settings", "Modes", "Shortcuts", "EQ" };
static void g_hub_sec(lv_obj_t *root, int sec, int cy){
    int d = 116, x = 22, y = cy - d / 2;
    lv_obj_t *h = g_box(root, x, y, d, d, LV_RADIUS_CIRCLE, G_HUB, 235);
    if(g_retro){                                                           /* a CD: rainbow sheen + grooves round the label */
        static const uint32_t IRI[6] = { 0xFF5AA0, 0xFFC94A, 0x62F28C, 0x4AC8FF, 0x9B6BFF, 0xFF5AA0 };
        int cx = x + d / 2, cyy = cy;
        for(int i = 0; i < 6; i++){ g_arc(root, cx, cyy, d / 2 + 8, 7, 200 + i * 12, 212 + i * 12, IRI[i], 200);
                                    g_arc(root, cx, cyy, d / 2 + 8, 7, 20 + i * 12, 32 + i * 12, IRI[5 - i], 150); }
        g_arc(root, cx, cyy, d / 2 + 8, 7, 0, 360, 0xD8DCE2, 60);
        g_arc(root, cx, cyy, d / 2 + 1, 1, 0, 360, 0xFFFFFF, 90);
    }
    const lv_font_t *fi = sec == 5 ? &font_inter_bold_22 : &lv_font_montserrat_32;
    lv_obj_t *i = g_lbl(h, G_SEC_ICON[sec], fi, lv_color_hex(0xFFFFFF)); lv_obj_align(i, LV_ALIGN_CENTER, 6, -10);
    lv_obj_t *n = g_lbl(h, G_SEC_NAME[sec], &font_inter_bold_18, lv_color_hex(0xFFFFFF)); lv_obj_align(n, LV_ALIGN_CENTER, 6, 24);
    lv_obj_t *u = g_lbl(h, LV_SYMBOL_UP, &lv_font_montserrat_14, lv_color_hex(0xB8C2CC)); lv_obj_align(u, LV_ALIGN_TOP_MID, 6, 6);
    lv_obj_t *dn = g_lbl(h, LV_SYMBOL_DOWN, &lv_font_montserrat_14, lv_color_hex(0xB8C2CC)); lv_obj_align(dn, LV_ALIGN_BOTTOM_MID, 6, -6);
    for(int k = 0; k < G_NSEC; k++){                                  /* curved, like the rim dots */
        float a = (180.0f - 36.0f + 72.0f * k / (G_NSEC - 1)) * 3.14159265f / 180.0f;
        int on = k == sec, sz = on ? 9 : 7, r = d / 2 - 11;
        g_box(h, (int)(d / 2 + r * cosf(a)) - sz / 2, (int)(d / 2 - r * sinf(a)) - sz / 2, sz, sz, LV_RADIUS_CIRCLE,
              on ? G_ACC : lv_color_hex(0x9AA3AD), LV_OPA_COVER);
    }
}
static void g_hub(lv_obj_t *root, const char *icon, const char *name, int cy, int level){
    (void)icon; (void)level;
    int sec = 0; for(int k = 0; k < G_NSEC; k++) if(!strcmp(name, G_SEC_NAME[k])) sec = k;
    if(!strcmp(name, "Albums") || !strcmp(name, "Playlist")) sec = 1;          /* sub-levels keep the main heading */
    g_hub_sec(root, sec, cy);
}
static void g_pdots(lv_obj_t *root, int n, int cur){                       /* position dots on the right rim */
    for(int k = 0; k < n; k++){
        float a = (-22.0f + 44.0f * k / (n - 1)) * 3.14159265f / 180.0f;
        int on = k == cur, s = on ? 9 : 7;
        g_box(root, (int)(180 + 165 * cosf(a)) - s / 2, (int)(180 + 165 * sinf(a)) - s / 2, s, s, LV_RADIUS_CIRCLE,
              on ? G_ACC : (g_light ? G_TXT2 : lv_color_hex(0xC8CDD2)), on ? LV_OPA_COVER : 200);
    }
}
static void g_header(lv_obj_t *root, const char *t){
    lv_obj_t *l = g_lbl(root, LV_SYMBOL_LEFT "  ", &lv_font_montserrat_20, G_TXT);
    lv_obj_t *h = g_lbl(root, t, &font_inter_bold_22, G_TXT);
    lv_obj_align(h, LV_ALIGN_TOP_MID, 8, 40); lv_obj_align_to(l, h, LV_ALIGN_OUT_LEFT_MID, 0, 0);
    g_battery(root, 12);
}
/* a list row: icon, title, optional value, chevron; the selected row is a tinted pill with an accent border */
static void g_row(lv_obj_t *root, int x, int y, int w, const char *icon, const char *t, const char *val, int sel, int chevron){
    lv_obj_t *r = g_box(root, x, y, w, 42, 14, sel ? G_ACC : G_PANEL, sel ? 60 : 0);
    if(sel){ lv_obj_set_style_border_width(r, 2, 0); lv_obj_set_style_border_color(r, G_ACC, 0); }
    else { lv_obj_t *ln = g_box(root, x + 10, y + 45, w - 20, 1, 0, G_LINE, 50); (void)ln; }
    int tx = 14;
    if(icon){ lv_obj_t *i = g_lbl(r, icon, &lv_font_montserrat_18, sel ? G_ACC : G_TXT); lv_obj_align(i, LV_ALIGN_LEFT_MID, 12, 0); tx = 46; }
    lv_obj_t *l = g_lbl(r, t, sel ? &font_inter_bold_18 : &font_inter_medium_16, G_TXT); lv_obj_align(l, LV_ALIGN_LEFT_MID, tx, 0);
    if(val){ lv_obj_t *v = g_lbl(r, val, &font_inter_bold_18, G_TXT); lv_obj_align(v, LV_ALIGN_RIGHT_MID, -14, 0); }
    if(chevron){ lv_obj_t *c = g_lbl(r, LV_SYMBOL_RIGHT, &lv_font_montserrat_14, G_TXT2); lv_obj_align(c, LV_ALIGN_RIGHT_MID, -12, 0); }
}
static lv_obj_t *g_circle_btn(lv_obj_t *root, int cx, int cy, int d, const char *glyph, int on, const lv_font_t *f){
    lv_obj_t *b = g_box(root, cx - d / 2, cy - d / 2, d, d, LV_RADIUS_CIRCLE, G_PANEL, 215);
    lv_obj_set_style_border_width(b, on ? 2 : 1, 0);
    lv_obj_set_style_border_color(b, on ? G_ACC : G_LINE, 0);
    lv_obj_set_style_border_opa(b, on ? LV_OPA_COVER : 60, 0);
    lv_obj_t *g = g_lbl(b, glyph, f ? f : &lv_font_montserrat_24, on ? G_ACC : G_TXT); lv_obj_center(g);
    return b;
}
static void g_rim_arc(lv_obj_t *root, int start, int end, int val, int w){
    lv_obj_t *a = lv_arc_create(root);
    lv_obj_remove_style(a, NULL, LV_PART_KNOB); lv_obj_clear_flag(a, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(a, 346, 346); lv_obj_center(a);
    lv_arc_set_bg_angles(a, start, end); lv_arc_set_range(a, 0, 100); lv_arc_set_value(a, val);
    lv_obj_set_style_arc_width(a, w, LV_PART_MAIN); lv_obj_set_style_arc_color(a, G_LINE, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(a, 70, LV_PART_MAIN);
    lv_obj_set_style_arc_width(a, w, LV_PART_INDICATOR); lv_obj_set_style_arc_color(a, G_ACC, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(a, true, LV_PART_INDICATOR);
}

static void disco_home(lv_obj_t *root){
    g_backdrop(root, 1, 0);
    lv_obj_t *wb = g_box(root, 0, 140, 360, 220, 0, G_WASH, LV_OPA_COVER);   /* a soft wash where the text sits */
    lv_obj_set_style_bg_grad_color(wb, G_WASH, 0); lv_obj_set_style_bg_grad_dir(wb, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_main_opa(wb, 0, 0); lv_obj_set_style_bg_grad_opa(wb, g_light ? 200 : 150, 0);
    lv_obj_set_style_bg_main_stop(wb, 0, 0); lv_obj_set_style_bg_grad_stop(wb, 130, 0);
    g_battery(root, 12);
    lv_obj_t *cp = g_box(root, 96, 30, 168, 82, 41, G_PANEL, g_light ? 190 : 170);   /* C: the clock in a glass pill */
    lv_obj_t *t = g_lbl(cp, "23:03", &font_disco_clk, G_TXT); lv_obj_align(t, LV_ALIGN_CENTER, 0, -8);
    lv_obj_t *dd = g_lbl(cp, "SUN 5   18\xC2\xB0", &lv_font_montserrat_12, G_TXT2); lv_obj_align(dd, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_obj_set_style_text_letter_space(dd, 2, 0);
    lv_obj_t *ti = g_lbl(root, "Into the Wild", &font_inter_bold_22, G_TXT); lv_obj_set_pos(ti, 168, 192);
    lv_obj_t *ar = g_lbl(root, "Tycho", &font_inter_medium_20, G_TXT2); lv_obj_set_pos(ar, 170, 220);
    g_hub(root, LV_SYMBOL_AUDIO, "Music", 250, 0);
    static const int BA[3] = { 82, 56, 30 };                              /* prev / pause / next: one arc, concentric with */
    static const char *const BG[3] = { LV_SYMBOL_PREV, LV_SYMBOL_PAUSE, LV_SYMBOL_NEXT };   /* the rim progress */
    for(int k = 0; k < 3; k++){
        float a = BA[k] * 3.14159265f / 180.0f; int R = 128;
        g_circle_btn(root, 180 + (int)lroundf(R * cosf(a)), 180 + (int)lroundf(R * sinf(a)), k == 1 ? 50 : 44, BG[k], 0,
                     k == 1 ? &lv_font_montserrat_20 : &lv_font_montserrat_18);
    }
    if(g_retro){ for(int a = 0; a < 100; a += 5) g_arc(root, 180, 180, 173, 6, a, a + 3, a < 62 ? 0x22D3EE : 0xFFFFFF, a < 62 ? LV_OPA_COVER : 70); }
    else g_rim_arc(root, 0, 100, 62, 5);                                   /* progress on the lower-right rim */
}
static void disco_library(lv_obj_t *root){
    g_backdrop(root, 0, 90);
    g_header(root, "Library");
    static const char *I[6] = { LV_SYMBOL_AUDIO, LV_SYMBOL_IMAGE, LV_SYMBOL_HOME, LV_SYMBOL_LIST, LV_SYMBOL_LIST, TH_IC_HEART };
    static const char *T[6] = { "Songs", "Albums", "Artists", "Genres", "Playlists", "Favourites" };
    static const int X[6] = { 54, 52, 60, 140, 140, 140 }, W[6] = { 252, 256, 244, 172, 172, 172 };
    for(int i = 0; i < 6; i++) g_row(root, X[i], 76 + i * 46, W[i], i == 5 ? NULL : I[i], T[i], NULL, i == 1, 1);
    g_hub(root, LV_SYMBOL_AUDIO, "Library", 250, 0);
    g_pdots(root, 6, 3);
}
static void disco_albums(lv_obj_t *root){
    g_backdrop(root, 0, 90);
    g_header(root, "Albums");
    lv_obj_t *s1 = lv_image_create(root); lv_image_set_src(s1, "A:" G_DIR "/out/c.bmp"); lv_obj_set_pos(s1, 22, 92);
    lv_image_set_scale(s1, 180); lv_obj_set_style_image_opa(s1, 150, 0);
    lv_obj_t *s2 = lv_image_create(root); lv_image_set_src(s2, "A:" G_DIR "/out/c.bmp"); lv_obj_set_pos(s2, 220, 92);
    lv_image_set_scale(s2, 180); lv_obj_set_style_image_opa(s2, 150, 0);
    lv_obj_t *c = lv_image_create(root); lv_image_set_src(c, "A:" G_DIR "/out/c.bmp"); lv_obj_set_pos(c, 106, 72);
    lv_obj_set_style_radius(c, 14, 0); lv_obj_set_style_clip_corner(c, true, 0);
    lv_obj_t *a = g_lbl(root, "Awake", &font_inter_bold_22, G_TXT); lv_obj_align(a, LV_ALIGN_TOP_MID, 34, 226);
    lv_obj_t *b = g_lbl(root, "Tycho", &font_inter_medium_16, G_TXT2); lv_obj_align(b, LV_ALIGN_TOP_MID, 34, 252);
    g_circle_btn(root, 210, 300, 46, LV_SYMBOL_PLAY, 1, &lv_font_montserrat_20);
    g_circle_btn(root, 266, 300, 46, LV_SYMBOL_SHUFFLE, 0, &lv_font_montserrat_18);
    g_hub(root, LV_SYMBOL_IMAGE, "Albums", 262, 0);
    g_pdots(root, 6, 3);
}
static void disco_playlist(lv_obj_t *root){
    g_backdrop(root, 0, 80);
    g_header(root, "Playlist");
    static const char *T[5] = { "Awake", "Into the Wild", "Horizon", "A Walk", "Dive" };
    static const int X[5] = { 64, 52, 150, 160, 160 }, W[5] = { 240, 262, 160, 150, 140 }, Y[5] = { 74, 122, 174, 222, 270 };
    for(int i = 0; i < 5; i++){
        int sel = i == 1, th = 42;
        lv_obj_t *r = g_box(root, X[i], Y[i], W[i], 48, 14, G_ACC, sel ? 60 : 0);
        if(sel){ lv_obj_set_style_border_width(r, 2, 0); lv_obj_set_style_border_color(r, G_ACC, 0); }
        lv_obj_t *im = lv_image_create(r); lv_image_set_src(im, "A:" G_DIR "/out/t.bmp"); lv_obj_set_pos(im, 4, 3);
        lv_obj_set_style_radius(im, 8, 0); lv_obj_set_style_clip_corner(im, true, 0);
        int tx = th + 14;
        if(sel){ lv_obj_t *eq = g_lbl(r, LV_SYMBOL_VOLUME_MAX, &lv_font_montserrat_16, G_ACC); lv_obj_set_pos(eq, tx, 14); tx += 26; }
        lv_obj_t *l = g_lbl(r, T[i], &font_inter_bold_18, G_TXT); lv_obj_set_pos(l, tx, 3);
        lv_obj_t *s = g_lbl(r, "Tycho", &font_inter_medium_14, G_TXT2); lv_obj_set_pos(s, tx, 25);
    }
    g_hub(root, LV_SYMBOL_LIST, "Playlist", 262, 0);
    g_pdots(root, 8, 3);
    lv_obj_t *az = g_box(root, 146, 322, 68, 28, 14, G_PANEL, 220);
    lv_obj_set_style_border_width(az, 1, 0); lv_obj_set_style_border_color(az, G_LINE, 0); lv_obj_set_style_border_opa(az, 60, 0);
    lv_obj_t *azl = g_lbl(az, "A - Z", &font_inter_bold_18, G_TXT); lv_obj_center(azl);
}
static void disco_info(lv_obj_t *root){
    g_backdrop(root, 0, 90);
    g_header(root, "Song Info");
    static const char *I[6] = { LV_SYMBOL_AUDIO, LV_SYMBOL_HOME, LV_SYMBOL_IMAGE, LV_SYMBOL_FILE, LV_SYMBOL_SHUFFLE, LV_SYMBOL_SETTINGS };
    static const char *K[6] = { "Title", "Artist", "Album", "Format", "Rate", "Bit depth" };
    static const char *V[6] = { "Into the Wild", "Tycho", "Awake", "FLAC", "44.1 kHz", "16 bit" };
    static const int X[6] = { 50, 50, 140, 140, 140, 140 }, W[6] = { 262, 262, 172, 172, 172, 172 };
    for(int i = 0; i < 6; i++){
        int y = 74 + i * 42;
        lv_obj_t *ic = g_lbl(root, I[i], &lv_font_montserrat_16, G_TXT); lv_obj_set_pos(ic, X[i] + 4, y + 10);
        lv_obj_t *k = g_lbl(root, K[i], &font_inter_medium_14, G_TXT2); lv_obj_set_pos(k, X[i] + 30, y + 11);
        lv_obj_t *v = g_lbl(root, V[i], i < 2 ? &font_inter_bold_18 : &font_inter_medium_14, G_TXT);
        lv_obj_align(v, LV_ALIGN_TOP_RIGHT, -(360 - X[i] - W[i]) - 6, y + (i < 2 ? 9 : 11));
        g_box(root, X[i], y + 40, W[i], 1, 0, G_LINE, 50);
    }
    g_hub(root, "i", "Music", 250, 1);
    g_pdots(root, 6, 2);
}
static void disco_quick(lv_obj_t *root){
    g_backdrop(root, 0, 60);
    g_battery(root, 12);
    g_circle_btn(root, 104, 108, 70, LV_SYMBOL_BLUETOOTH, 1, &lv_font_montserrat_26);
    g_circle_btn(root, 180, 108, 70, LV_SYMBOL_AUDIO, 0, &lv_font_montserrat_26);
    g_circle_btn(root, 256, 108, 70, LV_SYMBOL_SHUFFLE, 1, &lv_font_montserrat_26);
    lv_obj_t *pill = g_box(root, 46, 152, 262, 60, 30, G_PANEL, 225);
    lv_obj_set_style_border_width(pill, 1, 0); lv_obj_set_style_border_color(pill, G_LINE, 0); lv_obj_set_style_border_opa(pill, 50, 0);
    lv_obj_t *t = g_lbl(pill, "Into the Wild", &font_inter_bold_18, G_TXT); lv_obj_set_pos(t, 30, 8);
    lv_obj_t *a = g_lbl(pill, "Tycho", &font_inter_medium_14, G_TXT2); lv_obj_set_pos(a, 30, 32);
    lv_obj_t *pp = g_box(pill, 202, 2, 56, 56, LV_RADIUS_CIRCLE, lv_color_hex(0x1C232B), LV_OPA_COVER);
    lv_obj_t *ppl = g_lbl(pp, LV_SYMBOL_PAUSE, &lv_font_montserrat_22, G_TXT); lv_obj_center(ppl);
    g_circle_btn(root, 104, 256, 70, LV_SYMBOL_LOOP, 0, &lv_font_montserrat_26);
    g_circle_btn(root, 180, 256, 70, "EQ", 0, &font_inter_bold_22);
    g_circle_btn(root, 256, 256, 70, LV_SYMBOL_EYE_CLOSE, 0, &lv_font_montserrat_26);
    g_rim_arc(root, 300, 60, 62, 5);                                       /* brightness on the right rim */
}

static void g_list(lv_obj_t *root, const char *const *ic, const char *const *t, const char *const *v, int n, int sel){
    static const int X[5] = { 64, 52, 130, 140, 150 }, W[5] = { 240, 262, 182, 172, 160 };
    for(int i = 0; i < n && i < 5; i++){
        int y = 74 + i * 48;
        lv_obj_t *r = g_box(root, X[i], y, W[i], 44, 14, G_ACC, i == sel ? 60 : 0);
        if(i == sel){ lv_obj_set_style_border_width(r, 2, 0); lv_obj_set_style_border_color(r, G_ACC, 0); }
        lv_obj_t *a = g_lbl(r, ic[i], &lv_font_montserrat_16, i == sel ? G_ACC : G_TXT); lv_obj_set_pos(a, 10, 12);
        lv_obj_t *b = g_lbl(r, t[i], &font_inter_bold_18, G_TXT); lv_obj_set_pos(b, 36, 10);
        if(v && v[i]){ lv_obj_t *c = g_lbl(r, v[i], ((unsigned char)v[i][0] == 0xEF) ? &lv_font_montserrat_14 : &font_inter_medium_14, i == sel ? G_ACC : G_TXT2); lv_obj_align(c, LV_ALIGN_RIGHT_MID, -10, 0); }
    }
}
static void disco_overlay(lv_obj_t *root){                     /* tap on title */
    disco_home(root);
    g_box(root, 0, 0, 360, 360, 0, lv_color_hex(0x000000), 225);
    lv_obj_t *t = g_lbl(root, "Into the Wild", &font_inter_bold_22, G_TXT); lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 96);
    lv_obj_t *a = g_lbl(root, "Tycho - Awake", &font_inter_medium_16, G_TXT2); lv_obj_align(a, LV_ALIGN_TOP_MID, 0, 124);
    g_circle_btn(root, 110, 186, 58, LV_SYMBOL_OK, 1, &lv_font_montserrat_22);          /* heart placeholder */
    g_circle_btn(root, 180, 186, 58, LV_SYMBOL_SHUFFLE, 0, &lv_font_montserrat_22);
    g_circle_btn(root, 250, 186, 58, LV_SYMBOL_LIST, 0, &lv_font_montserrat_22);
    g_circle_btn(root, 145, 254, 58, LV_SYMBOL_EDIT, 0, &lv_font_montserrat_22);         /* lyrics */
    g_circle_btn(root, 215, 254, 58, LV_SYMBOL_REFRESH, 0, &lv_font_montserrat_22);      /* immersive */
}
static void disco_settings(lv_obj_t *root){
    g_backdrop(root, 0, 110); g_header(root, "Settings");
    static const char *I[5] = { LV_SYMBOL_IMAGE, LV_SYMBOL_AUDIO, LV_SYMBOL_EYE_OPEN, LV_SYMBOL_POWER, LV_SYMBOL_DOWNLOAD };
    static const char *T[5] = { "Theme", "Playback", "Display", "Power", "Update" };
    static const char *V[5] = { "Disco", LV_SYMBOL_RIGHT, LV_SYMBOL_RIGHT, LV_SYMBOL_RIGHT, "SD" };
    g_list(root, I, T, V, 5, 0);
    g_hub_sec(root, 2, 262); g_pdots(root, 9, 0);
}
static void disco_modes(lv_obj_t *root){
    g_backdrop(root, 0, 110); g_header(root, "Modes");
    static const char *I[5] = { LV_SYMBOL_USB, LV_SYMBOL_BLUETOOTH, LV_SYMBOL_AUDIO, LV_SYMBOL_WIFI, LV_SYMBOL_EYE_CLOSE };
    static const char *T[5] = { "USB storage", "BT receiver", "USB DAC", "Wi-Fi", "Screen off" };
    static const char *V[5] = { "On", "Off", "Off", "On", "" };
    g_list(root, I, T, V, 5, 0);
    g_hub_sec(root, 3, 262); g_pdots(root, 5, 0);
}
static void disco_shortcuts(lv_obj_t *root){
    g_backdrop(root, 0, 110); g_header(root, "Shortcuts");
    static const char *I[5] = { LV_SYMBOL_SHUFFLE, LV_SYMBOL_LIST, LV_SYMBOL_DIRECTORY, LV_SYMBOL_OK, LV_SYMBOL_PLUS };
    static const char *T[5] = { "Shuffle all", "Recently added", "Files", "Favourites", "Add shortcut" };
    g_list(root, I, T, NULL, 5, 0);
    g_hub_sec(root, 4, 262); g_pdots(root, 5, 0);
}
static void disco_hubcfg(lv_obj_t *root){
    g_backdrop(root, 0, 110); g_header(root, "Hub items");
    static const char *I[5] = { LV_SYMBOL_AUDIO, LV_SYMBOL_LIST, LV_SYMBOL_SETTINGS, LV_SYMBOL_USB, LV_SYMBOL_HOME };
    static const char *T[5] = { "Music", "Library", "Settings", "Modes", "Shortcuts" };
    static const char *V[5] = { "Fixed", LV_SYMBOL_UP " " LV_SYMBOL_DOWN, LV_SYMBOL_OK, LV_SYMBOL_OK, LV_SYMBOL_CLOSE };
    g_list(root, I, T, V, 5, 1);
    g_hub_sec(root, 2, 262); g_pdots(root, 6, 1);
}


/* the edge sheen: a CD's rainbow, very faint, round the rim only - a fixed overlay, drawn once per screen */
static void g_sheen(lv_obj_t *root){
    static const uint32_t IRI[8] = { 0xFF5AA0, 0xFFB45A, 0xFFF06A, 0x62F28C, 0x4AD8FF, 0x6A8CFF, 0xB36BFF, 0xFF5AA0 };
    for(int band = 0; band < 2; band++){                                 /* outer bright, fading inwards */
        int r = 180 - band * 5, w = 5; lv_opa_t o = band == 0 ? 60 : 26;
        for(int i = 0; i < 24; i++){
            int a0 = i * 15 + band * 20;                                 /* bands slightly shifted: the colours drift */
            g_arc(root, 180, 180, r, w, a0, a0 + 15, IRI[(i + band) % 8], o);
        }
    }
}

/* ---- Home concepts (Home is its own screen; Now Playing is the Music section) ------------------------ */
static void g_np_pill(lv_obj_t *root, int y){                          /* small now-playing pill: tap = Now Playing */
    lv_obj_t *p = g_box(root, 70, y, 220, 50, 25, G_PANEL, 220);
    lv_obj_set_style_border_width(p, 1, 0); lv_obj_set_style_border_color(p, G_LINE, 0); lv_obj_set_style_border_opa(p, 50, 0);
    lv_obj_t *im = lv_image_create(p); lv_image_set_src(im, "A:" G_DIR "/out/t.bmp"); lv_obj_set_pos(im, 5, 5);
    lv_obj_set_style_radius(im, LV_RADIUS_CIRCLE, 0); lv_obj_set_style_clip_corner(im, true, 0);
    lv_obj_t *t = g_lbl(p, "Into the Wild", &font_inter_bold_18, G_TXT); lv_obj_set_pos(t, 56, 5);
    lv_obj_t *a = g_lbl(p, "Tycho", &font_inter_medium_14, G_TXT2); lv_obj_set_pos(a, 56, 26);
    lv_obj_t *pp = g_lbl(p, LV_SYMBOL_PAUSE, &lv_font_montserrat_18, G_TXT); lv_obj_align(pp, LV_ALIGN_RIGHT_MID, -16, 0);
    lv_obj_t *ar = lv_arc_create(p); lv_obj_remove_style(ar, NULL, LV_PART_KNOB); lv_obj_clear_flag(ar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(ar, 40, 40); lv_obj_set_pos(ar, 5, 5); lv_arc_set_bg_angles(ar, 0, 360); lv_arc_set_rotation(ar, 270);
    lv_arc_set_value(ar, 62); lv_obj_set_style_arc_width(ar, 3, LV_PART_MAIN); lv_obj_set_style_arc_opa(ar, 0, LV_PART_MAIN);
    lv_obj_set_style_arc_width(ar, 3, LV_PART_INDICATOR); lv_obj_set_style_arc_color(ar, G_ACC, LV_PART_INDICATOR);
}
static void disco_home_a(lv_obj_t *root){                              /* A: clock first, music below */
    g_backdrop(root, 0, 60);
    g_battery(root, 12);
    lv_obj_t *t = g_lbl(root, "23:03", &font_disco_clk, G_TXT); lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 58);
    lv_obj_t *d = g_lbl(root, "Sunday, 5 October", &font_inter_medium_16, G_TXT2); lv_obj_align(d, LV_ALIGN_TOP_MID, 0, 120);
    lv_obj_t *w = g_lbl(root, "18\xC2\xB0  Clear", &font_inter_medium_16, G_TXT); lv_obj_align(w, LV_ALIGN_TOP_MID, 0, 146);
    g_np_pill(root, 196);
    static const char *const I[4] = { LV_SYMBOL_LIST, LV_SYMBOL_SHUFFLE, "EQ", LV_SYMBOL_SETTINGS };
    for(int k = 0; k < 4; k++){ float a = (135.0f - k * 30.0f) * 3.14159265f / 180.0f; int R = 128;
        g_circle_btn(root, 180 + (int)lroundf(R * cosf(a)), 180 + (int)lroundf(R * sinf(a)) + 6, 44, I[k], 0, k == 2 ? &font_inter_bold_18 : &lv_font_montserrat_18); }
}
static void disco_home_b(lv_obj_t *root){                              /* B: the hub in the middle, sections around it */
    g_backdrop(root, 0, 80);
    g_battery(root, 12);
    lv_obj_t *t = g_lbl(root, "23:03", &lv_font_montserrat_32, G_TXT); lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 26);
    lv_obj_t *w = g_lbl(root, "Sun 5  \xC2\xB7  18\xC2\xB0 Clear", &font_inter_medium_14, G_TXT2); lv_obj_align(w, LV_ALIGN_TOP_MID, 0, 64);
    lv_obj_t *cw = g_box(root, 180 - 44, 210 - 44, 88, 88, LV_RADIUS_CIRCLE, G_PANEL, LV_OPA_COVER);
    lv_obj_set_style_clip_corner(cw, true, 0);
    lv_obj_t *c = lv_image_create(cw); lv_image_set_src(c, "A:" G_DIR "/out/c.bmp"); lv_obj_set_pos(c, -30, -30);
    lv_obj_t *hub = g_box(root, 180 - 46, 210 - 46, 92, 92, LV_RADIUS_CIRCLE, G_PANEL, 110);
    lv_obj_set_style_border_width(hub, 3, 0); lv_obj_set_style_border_color(hub, G_ACC, 0);
    lv_obj_t *pp = g_lbl(hub, LV_SYMBOL_PAUSE, &lv_font_montserrat_26, G_TXT); lv_obj_center(pp);
    static const char *const I[6] = { LV_SYMBOL_AUDIO, LV_SYMBOL_LIST, LV_SYMBOL_SETTINGS, LV_SYMBOL_USB, LV_SYMBOL_HOME, "EQ" };
    static const char *const N[6] = { "Music", "Library", "Settings", "Modes", "Shortcuts", "EQ" };
    for(int k = 0; k < 6; k++){ float a = (-150.0f + k * 60.0f) * 3.14159265f / 180.0f; int R = 100;
        int x = 180 + (int)lroundf(R * cosf(a)), y = 206 + (int)lroundf(R * 0.86f * sinf(a));
        g_circle_btn(root, x, y, 46, I[k], k == 0, k == 5 ? &font_inter_bold_18 : &lv_font_montserrat_20);
        lv_obj_t *n = g_lbl(root, N[k], &font_inter_medium_14, G_TXT2); lv_obj_update_layout(n);
        lv_obj_set_pos(n, x - lv_obj_get_width(n) / 2, y + 26); }
}
static void disco_home_c(lv_obj_t *root){                              /* C: cover-led, clock in a glass pill */
    g_backdrop(root, 1, 0);
    g_box(root, 0, 0, 360, 360, 0, lv_color_hex(0x000000), 40);
    g_battery(root, 12);
    lv_obj_t *cp = g_box(root, 106, 34, 148, 74, 37, G_PANEL, 170);
    lv_obj_t *t = g_lbl(cp, "23:03", &lv_font_montserrat_32, G_TXT); lv_obj_align(t, LV_ALIGN_CENTER, 0, -8);
    lv_obj_t *d = g_lbl(cp, "SUN 5  18\xC2\xB0", &lv_font_montserrat_12, G_TXT2); lv_obj_align(d, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_obj_set_style_text_letter_space(d, 2, 0);
    lv_obj_t *ti = g_lbl(root, "Into the Wild", &font_inter_bold_22, G_TXT); lv_obj_align(ti, LV_ALIGN_TOP_MID, 0, 226);
    lv_obj_t *ar = g_lbl(root, "Tycho", &font_inter_medium_16, G_TXT2); lv_obj_align(ar, LV_ALIGN_TOP_MID, 0, 254);
    g_circle_btn(root, 124, 300, 44, LV_SYMBOL_PREV, 0, &lv_font_montserrat_18);
    g_circle_btn(root, 180, 306, 52, LV_SYMBOL_PAUSE, 1, &lv_font_montserrat_20);
    g_circle_btn(root, 236, 300, 44, LV_SYMBOL_NEXT, 0, &lv_font_montserrat_18);
    g_rim_arc(root, 120, 60, 62, 5);
    lv_obj_t *sw = g_lbl(root, LV_SYMBOL_UP "  swipe up: menu", &font_inter_medium_14, G_TXT2); (void)sw; lv_obj_add_flag(sw, LV_OBJ_FLAG_HIDDEN);
}
void disco_mock(lv_obj_t *root, int which){
    int fl = which / 100; which %= 100;
    g_light = fl & 1; g_acc_rgb = (fl & 2) ? 0xF08A3C : 0x22D3EE;   /* bit 1: accent from the cover (its warm orange) */
    lv_obj_set_style_bg_color(root, lv_color_hex(0x000000), 0); lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    switch(which){
        case 1: disco_home(root); break;
        case 21: disco_home_a(root); break;
        case 22: disco_home_b(root); break;
        case 23: disco_home_c(root); break;
        case 2: disco_library(root); break;
        case 3: disco_albums(root); break;
        case 4: disco_playlist(root); break;
        case 5: disco_info(root); break;
        case 7: disco_overlay(root); break;
        case 8: disco_settings(root); break;
        case 9: disco_modes(root); break;
        case 10: disco_shortcuts(root); break;
        case 11: disco_hubcfg(root); break;
        default: disco_quick(root); break;
    }
    if(which == 1 || which == 4 || which >= 21) g_sheen(root);   /* sheen: Now Playing, Playlist, Home only */
}
