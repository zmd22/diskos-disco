#include "simple_theme.h"
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
/* The standby screen in the Disco theme: the Music screen at rest. The sharp cover stays, dimmed under a dark veil;
 * the status icons, a large clock and the weather sit in the middle, the track below, and the progress runs round
 * the rim as a thin arc (rainbow, accent or none, like Settings > Display > Progress Bar). Nothing moves between
 * minutes: it redraws once a minute. saver.c forwards here when th_disco_family(). */
#include "screens.h"
#include "theme.h"
#include "config.h"
#include "ipc.h"
#include <string.h>
#include <stdio.h>

extern const lv_font_t font_weather16;
const void *ui_current_sharp_img(void);
const void *ui_current_backdrop_img(void);
static const void *saver_art(void){if(!th_simple())return ui_current_sharp_img();int bg=simple_cfg_get_int("simple_background",0);return bg==1?ui_current_backdrop_img():bg==2?ui_current_sharp_img():NULL;}
lv_color_t ui_current_accent(void);
const lv_anim_t *disco_scroll5(void);

#define NSEG 6
static lv_obj_t *g_img, *g_t[2], *g_wx[2], *g_ti[2], *g_ar[2], *g_status[2], *g_seg[NSEG], *g_tr;
static lv_font_t g_wf,g_title_font,g_artist_font;
static char g_last[16];
static int g_batt=-1,g_charging,g_wifi,g_bt;
void dsaver_set_status(int batt,int charging,int wifi,int bt);

static void pair(lv_obj_t *root, lv_obj_t **p, const lv_font_t *f, int w, int y, int dim){
    for(int k = 0; k < 2; k++){                                   /* [0] a shadow 1 px down-right, [1] the text */
        lv_obj_t *l = lv_label_create(root); lv_label_set_text(l, "");
        lv_obj_set_style_text_font(l, f, 0); lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        if(w > 0){ lv_obj_set_size(l, w, lv_font_get_line_height(f)); lv_obj_set_style_anim(l, disco_scroll5(), 0); lv_label_set_long_mode(l, LV_LABEL_LONG_SCROLL_CIRCULAR); }
        lv_obj_set_style_text_color(l, TC(FIXED_MEDIA_BLACK), 0); lv_obj_set_style_text_opa(l, 150, 0);
        if(k){ lv_obj_set_style_text_color(l, (th_simple()?(dim?TC(TEXT_SECONDARY):TC(TEXT_PRIMARY)):TC(FIXED_MEDIA_WHITE)), 0); lv_obj_set_style_text_opa(l, dim ? 170 : 225, 0); }
        lv_obj_align(l, LV_ALIGN_TOP_MID, k ? 0 : 1, y + (k ? 0 : 1));
        lv_obj_clear_flag(l, LV_OBJ_FLAG_CLICKABLE);
        p[k] = l;
    }
}
static char g_cur[4][192];                                         /* the text as set, per pair (an ellipsis edits the label's copy) */
static void ptext(lv_obj_t **p, const char *t){
    if(!p[1] || !t) return;
    int slot = p == g_t ? 0 : p == g_wx ? 1 : p == g_ti ? 2 : 3;
    if(!strcmp(g_cur[slot], t)) return;
    snprintf(g_cur[slot], sizeof g_cur[slot], "%s", t);
    if(lv_obj_get_style_anim(p[1], 0)){ lv_label_set_long_mode(p[0], LV_LABEL_LONG_SCROLL_CIRCULAR); lv_label_set_long_mode(p[1], LV_LABEL_LONG_SCROLL_CIRCULAR); }
    lv_label_set_text(p[0], t); lv_label_set_text(p[1], t);
}

static lv_obj_t *rim_arc(lv_obj_t *root, lv_color_t c, int w, int opa){
    lv_obj_t *a = lv_arc_create(root);
    lv_obj_remove_style(a, NULL, LV_PART_KNOB); lv_obj_clear_flag(a, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(a, 344, 344); lv_obj_center(a);
    lv_arc_set_bg_angles(a, 0, 360); lv_arc_set_rotation(a, 270);
    lv_obj_set_style_arc_opa(a, 0, LV_PART_MAIN);
    lv_obj_set_style_arc_width(a, w, LV_PART_INDICATOR); lv_obj_set_style_arc_rounded(a, false, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(a, c, LV_PART_INDICATOR); lv_obj_set_style_arc_opa(a, opa, LV_PART_INDICATOR);
    lv_arc_set_angles(a, 0, 0);
    return a;
}
static void progress(void){                                       /* once a minute */
    if(!g_seg[0]) return;
    int m = simple_cfg_get_int("disco_progress", 0); if(m < 0 || m > 2) m = 0;
    long pos, dur; int playing;
    int deg = saver_song(&pos, &dur, &playing) && dur > 0 ? (int)((long long)pos * 360 / dur) : 0;
    if(deg > 360) deg = 360;
    if(th_simple()){static int old_deg=-1,old_mode=-1;if(deg==old_deg&&m==old_mode)return;old_deg=deg;old_mode=m;}
    if(m == 2) lv_obj_add_flag(g_tr, LV_OBJ_FLAG_HIDDEN); else lv_obj_remove_flag(g_tr, LV_OBJ_FLAG_HIDDEN);
    for(int i = 0; i < NSEG; i++){
        int a0 = i * 60, a1 = a0 + 60;
        if(m == 1){ lv_obj_set_style_arc_color(g_seg[i], ui_current_accent(), LV_PART_INDICATOR); }
        int e = deg < a1 ? deg : a1;
        if(m == 2 || e <= a0) lv_obj_add_flag(g_seg[i], LV_OBJ_FLAG_HIDDEN);
        else { lv_obj_remove_flag(g_seg[i], LV_OBJ_FLAG_HIDDEN); lv_arc_set_angles(g_seg[i], a0, e); }
    }
}
void dsaver_create(lv_obj_t *root){
    memset(g_cur, 0, sizeof g_cur);
    static const uint32_t IRI[NSEG] = { 0xFF5AA0, 0xFFB45A, 0xFFF06A, 0x62F28C, 0x4AD8FF, 0xB36BFF };
    lv_obj_set_style_bg_color(root, th_simple()?TC(CANVAS):TC(FIXED_MEDIA_BLACK), 0); lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_image_src(root, NULL, 0);
    g_img = lv_image_create(root); lv_obj_set_pos(g_img, -2, -2);
    lv_obj_clear_flag(g_img, LV_OBJ_FLAG_CLICKABLE);
    const void *src = saver_art();
    if(src) lv_image_set_src(g_img, src); else lv_obj_add_flag(g_img, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *veil = lv_obj_create(root); lv_obj_remove_style_all(veil);   /* standby: the cover far down */
    lv_obj_set_size(veil, 360, 360); lv_obj_set_style_bg_color(veil, th_simple()?TC(CANVAS):TC(FIXED_MEDIA_BLACK), 0);
    lv_obj_set_style_bg_opa(veil, th_simple()?(simple_cfg_get_int("simple_background",0)==1?153:166):175, 0);   /* 3.2.0 Squash: the picture at half its old brightness */ lv_obj_clear_flag(veil, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    g_tr = rim_arc(root, TC(FIXED_MEDIA_WHITE), 3, 40); lv_arc_set_angles(g_tr, 0, 360);
    for(int i = 0; i < NSEG; i++) g_seg[i] = rim_arc(root, theme_color_from_rgb(IRI[i]), 3, 200);
    LV_FONT_DECLARE(font_simple_nunito_38)
    pair(root, g_t, th_simple()?&font_simple_nunito_38:TF(UI_48), -1, 68, 0);   /* 3.2.0 Squash: its own bold face, */
    lv_obj_set_style_transform_scale(g_t[0], th_simple()?377:380, 0); lv_obj_set_style_transform_scale(g_t[1], th_simple()?377:380, 0);   /* twice its old 28 px */
    ptext(g_t, "--:--");
    g_wf = *theme_font_original(th_simple()?18:24); g_wf.fallback = &font_weather16;
    pair(root, g_wx, &g_wf, -1, 142, 0);
    for(int k=0;k<2;k++){lv_obj_set_style_transform_scale(g_wx[k],th_simple()?256:300,0);lv_obj_set_style_transform_pivot_x(g_wx[k],lv_pct(50),0);lv_obj_set_style_transform_pivot_y(g_wx[k],lv_pct(50),0);}
    g_title_font=*(th_simple()?TF(MUSIC_TITLE):TF(UI_36));g_title_font.fallback=ui_font_cjk(28);
    g_artist_font=*(th_simple()?TF(MUSIC_ARTIST):TF(UI_28));g_artist_font.fallback=ui_font_cjk(24);
    pair(root, g_ti, &g_title_font, 296, 194, 0);
    pair(root, g_ar, &g_artist_font, 280, th_simple()?250:244, 1);
    pair(root,g_status,th_simple()?TF(UI_14):TF(UI_20),160,20,1);dsaver_set_status(g_batt,g_charging,g_wifi,g_bt);
    for(int k = 0; k < 2; k++){                                   /* the scaled clock grows from its middle */
        lv_obj_update_layout(g_t[k]);
        lv_obj_set_style_transform_pivot_x(g_t[k], lv_pct(50), 0); lv_obj_set_style_transform_pivot_y(g_t[k], lv_pct(50), 0);
    }
    g_last[0] = 0;
    progress();
}
void dsaver_set_status(int batt,int charging,int wifi,int bt){
    g_batt=batt;g_charging=charging;g_wifi=wifi;g_bt=bt;
    if(!g_status[0])return;
    char text[96]="";
    snprintf(text,sizeof text,"%s%s%s",wifi?LV_SYMBOL_WIFI "  ":"",bt?LV_SYMBOL_BLUETOOTH "  ":"",
        batt<0?"":charging?LV_SYMBOL_CHARGE:batt<15?LV_SYMBOL_BATTERY_EMPTY:batt<40?LV_SYMBOL_BATTERY_1:batt<70?LV_SYMBOL_BATTERY_2:LV_SYMBOL_BATTERY_FULL);
    for(int k=0;k<2;k++)lv_label_set_text(g_status[k],text);
}
void dsaver_set_clock(const char *t){
    if(!g_t[1] || !t || !strcmp(t, g_last)) return;
    strncpy(g_last, t, sizeof g_last - 1); ptext(g_t, t); progress();
}
void dsaver_set_weather(const char *s){                           /* "<icon>  14°C  Partly cloudy" -> "<icon> 14°" */
    char o[48] = ""; int n = 0;
    while(s && *s == ' ') s++;
    for(int i = 0; s && s[i] && n < (int)sizeof o - 1; i++){
        if(s[i] == ' ' && s[i + 1] == ' ' && n > 0 && strchr(o, 0xB0)) break;
        if(s[i] == ' ' && n > 0 && o[n - 1] == ' ') continue;
        if((s[i] == 'C' || s[i] == 'F') && n > 1 && (unsigned char)o[n - 1] == 0xB0) continue;
        o[n++] = s[i]; o[n] = 0;
    }
    ptext(g_wx, o);
}
void dsaver_set_track(const char *title, const char *artist){
    ptext(g_ti, title ? title : ""); ptext(g_ar, artist ? artist : "");
    if(g_img){ const void *src = saver_mode_active() ? NULL : saver_art();
        if(th_simple()){extern unsigned ui_art_revision(void);static unsigned revision;static const void*previous;static int seen;unsigned current=ui_art_revision();if(seen&&revision==current&&previous==src){if(screen_current()==SCR_SAVER)progress();return;}seen=1;revision=current;previous=src;}
        if(src){ lv_image_set_src(g_img, src); lv_obj_remove_flag(g_img, LV_OBJ_FLAG_HIDDEN); } else lv_obj_add_flag(g_img, LV_OBJ_FLAG_HIDDEN); }
    progress();
}
