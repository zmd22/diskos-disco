#include "battery.h"
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
/* Home in the Braun theme: a Braun wall clock on the grille (the face drawn once into a canvas; only the
 * hands move - the second hand once a second while Home is on screen), the weather in a window at 3
 * o'clock, the playing track and Library / play-pause / Search on the lower segment, the battery as a thin
 * black arc on the top rim. home.c forwards its setters here when th_braun_family(). */
#include "screens.h"
#include "braun.h"
#include "braun2.h"
#include "theme.h"
#include <math.h>
#include <string.h>
#include <time.h>
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include "ipc.h"
extern const lv_font_t font_theme_20;

extern const lv_font_t font_weather16;
#define CX 180
#define CY (th_braun2()?126:138)
#define RD (th_braun2()?90:104)
#define HAND_H (th_braun2()?46:50)   /* Braun 2.0: hands fit its smaller face */
#define HAND_M (th_braun2()?74:82)
#define HAND_S (th_braun2()?82:92)
static lv_obj_t *g_canvas, *g_hh, *g_mm, *g_ss, *g_hub, *g_wx, *g_date, *g_title, *g_artist, *g_pp, *g_batt;
static lv_point_precise_t g_hp[2], g_mp[2], g_sp[2];
static lv_timer_t *g_tick;
static battery_arc_t g_battery;

static void hand(lv_obj_t *l, lv_point_precise_t *p, float deg, int len, int back){
    float an = (deg - 90) * 0.0174533f;
    p[0].x = CX - back * cosf(an); p[0].y = CY - back * sinf(an);
    p[1].x = CX + len * cosf(an);  p[1].y = CY + len * sinf(an);
    lv_line_set_points(l, p, 2);
}
static void tick_cb(lv_timer_t *t){
    (void)t;
    if(screen_current() != SCR_HOME) return;
    time_t now = time(NULL); struct tm tm; localtime_r(&now, &tm);
    hand(g_hh, g_hp, (tm.tm_hour % 12 + tm.tm_min / 60.0f) * 30, HAND_H, 0);
    hand(g_mm, g_mp, (tm.tm_min + tm.tm_sec / 60.0f) * 6, HAND_M, 0);
    hand(g_ss, g_sp, tm.tm_sec * 6, HAND_S, 18);
}
static lv_obj_t *mkline(lv_obj_t *root, int w, uint32_t col){
    lv_obj_t *l = lv_line_create(root);
    lv_obj_set_style_line_width(l, w, 0); lv_obj_set_style_line_color(l, theme_color_from_rgb(col), 0);
    lv_obj_set_style_line_rounded(l, true, 0);
    lv_obj_clear_flag(l, LV_OBJ_FLAG_CLICKABLE);
    return l;
}
static void nav_cb(lv_event_t *e){ screen_show((int)(intptr_t)lv_event_get_user_data(e)); }
static void pp_cb(lv_event_t *e){ (void)e; if(ui_transport_command("0201000C0000")==0 && g_pp) ui_pp_glyph(lv_obj_get_child(g_pp,0),ui_pp_icon_playing(ui_is_playing())); }   /* play/pause, as Home does */
static void wx_cb(lv_event_t *e){ (void)e; weather_app_open(); }

void bhome_create(lv_obj_t *root){
    br_face(root);
    br_disc(root, CX, CY, RD + 6, BR_PANEL);                    /* the clock's solid surround */
    g_canvas = br_clock_face(root, CX, CY, RD, th_braun2()?0:1);             /* drawn once; numerals, a gap at 3 for the weather */
    /* the weather window at 3 o'clock (tap: the Weather screen) */
    lv_obj_t *w = br_weather_window(root, CX + 44, CY - 13, &g_wx);
    lv_obj_add_flag(w, LV_OBJ_FLAG_CLICKABLE); lv_obj_set_ext_click_area(w, 8);
    lv_obj_add_event_cb(w, wx_cb, LV_EVENT_CLICKED, NULL);
    g_date = br_label(root, "", br_font(12, 0), BR_TXT3); lv_obj_align(g_date, LV_ALIGN_TOP_MID, 0, CY + 22);
    /* the hands */
    g_hh = mkline(root, 6, BR_TXT); g_mm = mkline(root, 4, BR_TXT); g_ss = mkline(root, 2, BR_ACC);
    g_hub = br_disc(root, CX, CY, 5, BR_ACC);
    /* the lower segment: the track + Library / play-pause / Search */
    lv_obj_t *seg = br_segment(root, th_braun2()?228:250);
    lv_obj_t *trk = lv_obj_create(root);
    lv_obj_remove_style_all(trk); lv_obj_set_size(trk, 244, th_braun2()?64:52); lv_obj_align(trk, LV_ALIGN_TOP_MID, 0, th_braun2()?228:250);
    lv_obj_add_flag(trk, LV_OBJ_FLAG_CLICKABLE); lv_obj_add_event_cb(trk, nav_cb, LV_EVENT_CLICKED, (void *)(intptr_t)SCR_NOWPLAYING);
    g_title = br_label(trk, "Not Playing", br_font(th_braun2()?24:22, 1), BR_TXT);
    lv_obj_set_size(g_title, 244, th_braun2()?32:27); lv_label_set_long_mode(g_title, LV_LABEL_LONG_SCROLL_CIRCULAR); lv_obj_set_style_text_align(g_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(g_title, LV_ALIGN_TOP_MID, 0, 2);
    g_artist = br_label(trk, "", br_font(th_braun2()?20:16, 0), BR_TXT2);
    lv_obj_set_size(g_artist, 230, th_braun2()?28:20); lv_label_set_long_mode(g_artist, LV_LABEL_LONG_SCROLL_CIRCULAR); lv_obj_set_style_text_align(g_artist, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(g_artist, LV_ALIGN_TOP_MID, 0, th_braun2()?34:30);
    (void)seg;
    /* Library and Search (40 px) hang from the same top edge as the play disc (50 px), not centre-to-centre */
    #define BH_TOP (th_braun2()?294:302)
    lv_obj_t *lib = br_button(root, 125, BH_TOP + 22, th_braun2()?22:20, th_braun2()?LV_SYMBOL_LIST:LV_SYMBOL_AUDIO, TF(UI_20), 0);
    lv_obj_add_event_cb(lib, nav_cb, LV_EVENT_CLICKED, (void *)(intptr_t)SCR_LIBRARY);
    g_pp = br_button(root, 180, BH_TOP + 26, th_braun2()?28:25, LV_SYMBOL_PLAY, TF(UI_26), 1);
    lv_obj_add_event_cb(g_pp, pp_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *srch = br_button(root, 235, BH_TOP + 22, th_braun2()?22:20, TH_IC_SEARCH, &font_theme_20, 0);
    lv_obj_add_event_cb(srch, nav_cb, LV_EVENT_CLICKED, (void *)(intptr_t)SCR_SEARCH);
    #undef BH_TOP
    /* the battery: a thin black arc on the top rim */
    battery_arc_create(&g_battery,root,344,4,236,304,0);g_batt=g_battery.arc;
    if(th_braun2()){
        lv_obj_set_pos(w,130,CY+22);lv_obj_set_size(w,100,24);lv_obj_set_style_bg_opa(w,LV_OPA_TRANSP,0);lv_obj_set_style_border_width(w,0,0);
        { static lv_font_t wxf; wxf=*br_font(14,0); wxf.fallback=&font_weather16; lv_obj_set_style_text_font(g_wx,&wxf,0); }   /* Inter + weather glyphs */lv_obj_set_style_text_color(g_wx,TC(TEXT_SECONDARY),0);lv_obj_center(g_wx);
        /* the date in a small window left of the 3 o'clock tick, as on Braun's wall clocks */
        lv_obj_set_size(g_date,56,24);lv_obj_set_align(g_date,LV_ALIGN_TOP_LEFT);lv_obj_set_pos(g_date,CX+RD-76,CY-12);lv_obj_set_style_text_font(g_date,br_font(14,1),0);lv_obj_set_style_text_color(g_date,TC(TEXT_PRIMARY),0);
        lv_obj_set_style_text_align(g_date,LV_TEXT_ALIGN_CENTER,0);lv_obj_set_style_pad_top(g_date,3,0);lv_obj_set_style_radius(g_date,8,0);lv_obj_set_style_bg_color(g_date,TC(SURFACE_STRONG),0);lv_obj_set_style_bg_opa(g_date,LV_OPA_COVER,0);
        lv_label_set_long_mode(g_title,LV_LABEL_LONG_DOT);lv_label_set_long_mode(g_artist,LV_LABEL_LONG_DOT);
        br2_keep_tree(root);
    }
    g_tick = lv_timer_create(tick_cb, 1000, NULL);
    tick_cb(NULL); { time_t now = time(NULL); struct tm tm; localtime_r(&now, &tm);   /* first paint even before Home shows */
        hand(g_hh, g_hp, (tm.tm_hour % 12 + tm.tm_min / 60.0f) * 30, HAND_H, 0); hand(g_mm, g_mp, tm.tm_min * 6, HAND_M, 0); hand(g_ss, g_sp, tm.tm_sec * 6, HAND_S, 18); }
}
/* "Thursday, 17 September" -> "THU 17" */
void bhome_set_clock(const char *t, const char *sub){
    (void)t;
    if(!g_date) return;
    char out[16] = ""; int n = 0;
    if(sub) for(int i = 0; sub[i] && n < 3 && isalpha((unsigned char)sub[i]); i++) out[n++] = (char)toupper((unsigned char)sub[i]);
    out[n] = 0;
    const char *d = sub; while(d && *d && !isdigit((unsigned char)*d)) d++;
    if(d && *d){ int day = atoi(d); snprintf(out + n, sizeof out - n, " %d", day); }
    lv_label_set_text(g_date, out);
}
void bhome_set_weather(const char *s){
    if(th_braun2()){
        /* "<icon>  14°C  Sunny" -> "<icon> 14°C": the glyph when the feed has one, then the temperature */
        const char *p=s;while(p&&*p&&!isdigit((unsigned char)*p)&&*p!='-')p++;
        char icon[16]="",temp[24]="";
        if(s&&p>s){int n=0;for(const char *c=s;c<p&&n<15;c++)if(*c!=' ')icon[n++]=*c;icon[n]=0;}
        if(p&&*p){int n=0;while(p[n]&&n<23&&!(p[n]==' '&&p[n+1]==' '))n++;memcpy(temp,p,(size_t)n);temp[n]=0;}
        char out[48];snprintf(out,sizeof out,"%s%s%s",icon,icon[0]&&temp[0]?" ":"",temp);
        if(g_wx)lv_label_set_text(g_wx,out);
    }else br_weather_text(g_wx,s);
}
void bhome_set_status(int batt, int charging, int wifi, int bt){
    (void)wifi; (void)bt;
    battery_arc_set(&g_battery,batt,charging);
}
void bhome_set_now_playing(const char *title, const char *artist, bool playing){
    if(g_title){ const char *nt = title ? title : "Not Playing"; if(strcmp(lv_label_get_text(g_title), nt)){lv_label_set_text(g_title, nt);if(!th_braun2()){ui_music_scroll(g_title);ui_music_scroll(g_artist);}}
                 lv_obj_set_style_text_color(g_title, title ? TC(TEXT_PRIMARY) : TC(TEXT_DISABLED), 0); }
    if(g_artist && strcmp(lv_label_get_text(g_artist),artist?artist:"")){lv_label_set_text(g_artist,artist?artist:"");if(!th_braun2())ui_music_scroll(g_artist);}
    if(g_pp){ lv_obj_t *l = lv_obj_get_child(g_pp, 0); if(l) lv_label_set_text(l, playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY); }
}
