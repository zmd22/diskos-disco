/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
/* The standby screen in the Braun theme: the Home clock enlarged, with no numerals and no second hand, the
 * weather window at 3, the track on the lower segment, and the whole face dimmed. The hands move only when
 * the minute changes, so it redraws once a minute. saver.c forwards here when th_braun(). */
#include "screens.h"
#include "braun.h"
#include <math.h>
#include <string.h>
#include <time.h>

#define CX 180
#define CY 142
#define RD 112
static lv_obj_t *g_hh, *g_mm, *g_wx, *g_title, *g_artist, *g_seg_box;
static lv_point_precise_t g_hp[2], g_mp[2];
static char g_last[16];

static lv_obj_t *mkline(lv_obj_t *root, int w){
    lv_obj_t *l = lv_line_create(root);
    lv_obj_set_style_line_width(l, w, 0); lv_obj_set_style_line_color(l, TC(TEXT_PRIMARY), 0);
    lv_obj_set_style_line_rounded(l, true, 0); lv_obj_clear_flag(l, LV_OBJ_FLAG_CLICKABLE);
    return l;
}
static void hand(lv_obj_t *l, lv_point_precise_t *p, float deg, int len){
    float an = (deg - 90) * 0.0174533f;
    p[0].x = CX; p[0].y = CY; p[1].x = CX + len * cosf(an); p[1].y = CY + len * sinf(an);
    lv_line_set_points(l, p, 2);
}
static void hands_now(void){
    time_t now = time(NULL); struct tm tm; localtime_r(&now, &tm);
    hand(g_hh, g_hp, (tm.tm_hour % 12 + tm.tm_min / 60.0f) * 30, 58);
    hand(g_mm, g_mp, tm.tm_min * 6, 92);
}
void bsaver_create(lv_obj_t *root){
    br_face(root);
    br_disc(root, CX, CY, RD + 6, BR_PANEL);
    br_clock_face(root, CX, CY, RD, 0);                       /* hour marks only */
    br_weather_window(root, CX + 48, CY - 13, &g_wx);
    g_hh = mkline(root, 7); g_mm = mkline(root, 5);
    br_disc(root, CX, CY, 6, BR_ACC);
    g_seg_box = br_segment(root, 268);
    g_title = br_label(root, "", br_font(16, 1), BR_TXT);
    lv_obj_set_width(g_title, 220); lv_label_set_long_mode(g_title, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(g_title, LV_TEXT_ALIGN_CENTER, 0); lv_obj_align(g_title, LV_ALIGN_TOP_MID, 0, 280);
    g_artist = br_label(root, "", br_font(14, 0), BR_TXT2);
    lv_obj_set_width(g_artist, 200); lv_label_set_long_mode(g_artist, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(g_artist, LV_TEXT_ALIGN_CENTER, 0); lv_obj_align(g_artist, LV_ALIGN_TOP_MID, 0, 302);
    lv_obj_t *dim = lv_obj_create(root);                        /* standby: the whole face dimmed */
    lv_obj_remove_style_all(dim); lv_obj_set_size(dim, 360, 360);
    lv_obj_set_style_bg_color(dim, TC(SCRIM), 0); lv_obj_set_style_bg_opa(dim, 96, 0);
    lv_obj_clear_flag(dim, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    hands_now();
}
void bsaver_set_clock(const char *t){                          /* the hands move when the minute changes */
    if(!g_hh || !t || !strcmp(t, g_last)) return;
    strncpy(g_last, t, sizeof g_last - 1); hands_now();
}
void bsaver_set_weather(const char *s){ br_weather_text(g_wx, s); }
void bsaver_set_track(const char *title, const char *artist){
    int have = title != NULL;
    if(g_title) lv_label_set_text(g_title, have ? title : "");
    if(g_artist) lv_label_set_text(g_artist, have && artist ? artist : "");
    if(g_seg_box){ if(have) lv_obj_remove_flag(g_seg_box, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(g_seg_box, LV_OBJ_FLAG_HIDDEN); }   /* nothing playing: just the clock */
}
