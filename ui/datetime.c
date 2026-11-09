/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
/* datetime.c - Settings > System > Set Date & Time (SCR_DATETIME): day / month / year and hour / minute rollers,
 * then Set: the system clock (settimeofday) and the battery-backed RTC (hwclock -w, off the UI thread). Parity with
 * stock, whose UI sets the time itself the same way. The time zone is chosen separately (Settings > Time Zone). */
#include "theme.h"
#include "theme_kit.h"
#include "screens.h"
#include "config.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

#define YEAR0  2024
#define YEARS  17                       /* 2024..2040 */

static lv_obj_t *g_root, *r_day, *r_mon, *r_year, *r_hour, *r_min;

static const char MONTHS[] = "Jan\nFeb\nMar\nApr\nMay\nJun\nJul\nAug\nSep\nOct\nNov\nDec";

static int days_in(int mon0, int year){
    static const int D[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    int leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    return D[mon0] + (mon0 == 1 && leap);
}

static lv_obj_t *roller(lv_obj_t *parent, const char *opts, int sel, int w, int x, int y){
    lv_obj_t *r = lv_roller_create(parent);
    lv_roller_set_options(r, opts, LV_ROLLER_MODE_NORMAL);
    lv_obj_set_width(r, w);
    lv_obj_align(r, LV_ALIGN_TOP_MID, x, y);
    lv_obj_set_style_bg_color(r, TC(SURFACE), 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(r, 0, 0);
    lv_obj_set_style_radius(r, 12, 0);
    lv_obj_set_style_text_font(r, TF(UI_16), 0);
    lv_obj_set_style_text_line_space(r, 6, 0);
    lv_obj_set_style_text_color(r, TC(TEXT_MUTED), 0);
    lv_obj_set_style_text_align(r, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_bg_color(r, TC(SURFACE_RAISED), LV_PART_SELECTED);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, LV_PART_SELECTED);
    lv_obj_set_style_text_color(r, TC(TEXT_PRIMARY), LV_PART_SELECTED);
    lv_roller_set_visible_row_count(r, 3);             /* after the font: the height is computed from it */
    lv_roller_set_selected(r, sel, LV_ANIM_OFF);
    return r;
}

static void set_cb(lv_event_t *e){
    if(lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    struct tm tm; memset(&tm, 0, sizeof tm);
    tm.tm_year = YEAR0 + (int)lv_roller_get_selected(r_year) - 1900;
    tm.tm_mon  = (int)lv_roller_get_selected(r_mon);
    int dim = days_in(tm.tm_mon, tm.tm_year + 1900), d = (int)lv_roller_get_selected(r_day) + 1;
    tm.tm_mday = d > dim ? dim : d;                    /* 31 Feb -> 28/29 Feb, never a roll into March */
    tm.tm_hour = (int)lv_roller_get_selected(r_hour);
    tm.tm_min  = (int)lv_roller_get_selected(r_min);
    tm.tm_isdst = -1;                                  /* the zone's own rule for that date */
    time_t t = mktime(&tm);
    struct timeval tv = { .tv_sec = t, .tv_usec = 0 };
    if(t == (time_t)-1 || settimeofday(&tv, NULL) != 0){ ui_toast("Couldn't set the time"); return; }
    ui_rtc_save_now();                                 /* keep it across power-off (hwclock -w, off the UI thread) */
    ui_clock_refresh();
    ui_toast(cfg_get_int("auto_time", 1) ? "Time set - Wi-Fi may correct it" : "Time set");
    screen_back();
}

static void back_cb(lv_event_t *e){ if(lv_event_get_code(e) == LV_EVENT_CLICKED) screen_back(); }

void datetime_create(lv_obj_t *root){ g_root = root; }


/* ==== Zune (Figma 79): the date on three short dials, then the time on a real clock face - drag the accent minute
 * knob round the rim (the hour hand follows) or the hour hand itself; am / pm under the readout ==== */
#include <math.h>
#include <stdlib.h>
LV_FONT_DECLARE(font_zune_regular_19) LV_FONT_DECLARE(font_zune_regular_14) LV_FONT_DECLARE(font_zune_regular_13)
LV_FONT_DECLARE(font_zune_regular_17) LV_FONT_DECLARE(font_zune_semibold_11) LV_FONT_DECLARE(font_zune_light_32)
const lv_font_t *zune_font(const lv_font_t *base);
static int zd_day, zd_mon, zd_year, zt_h, zt_m;           /* the date picked on page 1; the time on page 2 (0..23) */
static lv_obj_t *zt_mline, *zt_hline, *zt_knob, *zt_hknob, *zt_read, *zt_am, *zt_pm, *zd_wday;
static lv_point_precise_t zt_mp[2], zt_hp[2];
static void ztime_page(void);
static lv_obj_t *zlab(lv_obj_t *p, const lv_font_t *f, lv_color_t c, const char *t){
    lv_obj_t *l = lv_label_create(p); kit_keep(l); lv_label_set_text(l, t);
    lv_obj_set_style_text_font(l, zune_font(f), 0); lv_obj_set_style_text_color(l, c, 0); return l;
}
static lv_obj_t *zroller(const char *opts, int sel, int w, int x){
    lv_obj_t *r = lv_roller_create(g_root); kit_keep(r);
    lv_roller_set_options(r, opts, LV_ROLLER_MODE_NORMAL);
    lv_obj_set_width(r, w); lv_obj_align(r, LV_ALIGN_TOP_MID, x, 96);
    lv_obj_set_style_bg_opa(r, LV_OPA_TRANSP, 0); lv_obj_set_style_border_width(r, 0, 0);
    lv_obj_set_style_text_font(r, zune_font(&font_zune_regular_19), 0);
    lv_obj_set_style_text_line_space(r, 16, 0);
    lv_obj_set_style_text_color(r, TC(TEXT_MUTED), 0); lv_obj_set_style_text_align(r, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_TRANSP, LV_PART_SELECTED);
    lv_obj_set_style_text_color(r, TC(TEXT_PRIMARY), LV_PART_SELECTED);
    lv_obj_set_style_text_font(r, zune_font(&font_zune_light_32), LV_PART_SELECTED);
    lv_roller_set_visible_row_count(r, 5);
    lv_roller_set_selected(r, sel, LV_ANIM_OFF);
    return r;
}
static void zd_wday_paint(void){
    static const char *WD[] = { "sunday", "monday", "tuesday", "wednesday", "thursday", "friday", "saturday" };
    struct tm tm; memset(&tm, 0, sizeof tm);
    tm.tm_year = YEAR0 + (int)lv_roller_get_selected(r_year) - 1900; tm.tm_mon = (int)lv_roller_get_selected(r_mon);
    int dim = days_in(tm.tm_mon, tm.tm_year + 1900), d = (int)lv_roller_get_selected(r_day) + 1;
    tm.tm_mday = d > dim ? dim : d; tm.tm_hour = 12; mktime(&tm);
    lv_label_set_text(zd_wday, WD[tm.tm_wday]);
}
static void zd_changed(lv_event_t *e){ (void)e; zd_wday_paint(); }
static void zd_next(lv_event_t *e){
    (void)e;
    zd_year = YEAR0 + (int)lv_roller_get_selected(r_year); zd_mon = (int)lv_roller_get_selected(r_mon);
    int dim = days_in(zd_mon, zd_year), d = (int)lv_roller_get_selected(r_day) + 1; zd_day = d > dim ? dim : d;
    ztime_page();
}
static void zdate_page(void){
    lv_obj_clean(g_root);
    lv_obj_set_style_bg_color(g_root, TC(CANVAS), 0); lv_obj_set_style_bg_opa(g_root, LV_OPA_COVER, 0);
    lv_obj_remove_flag(g_root, LV_OBJ_FLAG_SCROLLABLE);
    time_t now = time(NULL); struct tm lt; localtime_r(&now, &lt);
    static char days[31 * 3], years[YEARS * 5];
    static const char MON[] = "jan\nfeb\nmar\napr\nmay\njun\njul\naug\nsep\noct\nnov\ndec";
    char *p = days; for(int i = 1; i <= 31; i++) p += sprintf(p, i < 31 ? "%d\n" : "%d", i);
    p = years;      for(int i = 0; i < YEARS; i++) p += sprintf(p, i < YEARS - 1 ? "%d\n" : "%d", YEAR0 + i);
    int y = lt.tm_year + 1900 - YEAR0; if(y < 0) y = 0; if(y >= YEARS) y = YEARS - 1;
    lv_obj_t *cap = zlab(g_root, &font_zune_semibold_11, TC(TEXT_MUTED), "DATE");
    lv_obj_set_style_text_letter_space(cap, 2, 0); lv_obj_align(cap, LV_ALIGN_TOP_MID, 0, 40);
    r_day = zroller(days, lt.tm_mday - 1, 70, -88); r_mon = zroller(MON, lt.tm_mon, 80, 0); r_year = zroller(years, y, 96, 90);
    lv_obj_align(r_day, LV_ALIGN_TOP_MID, -88, 70); lv_obj_align(r_mon, LV_ALIGN_TOP_MID, 0, 70); lv_obj_align(r_year, LV_ALIGN_TOP_MID, 90, 70);   /* higher, so "next" clears the bezel */
    lv_obj_update_layout(r_day);
    int ry = lv_obj_get_y(r_day), rh = lv_obj_get_height(r_day);
    for(int k = 0; k < 2; k++){                                 /* the focus band: two accent hairlines */
        lv_obj_t *ln = lv_obj_create(g_root); lv_obj_remove_style_all(ln); kit_keep(ln);
        lv_obj_set_size(ln, 236, 1); lv_obj_set_style_bg_color(ln, TC(ACCENT_PRIMARY), 0); lv_obj_set_style_bg_opa(ln, LV_OPA_COVER, 0);
        lv_obj_align(ln, LV_ALIGN_TOP_MID, 0, ry + rh / 2 + (k ? 24 : -24));
        lv_obj_remove_flag(ln, LV_OBJ_FLAG_CLICKABLE);
    }
    lv_obj_t *rr[3] = { r_day, r_mon, r_year };
    for(int k = 0; k < 3; k++) lv_obj_add_event_cb(rr[k], zd_changed, LV_EVENT_VALUE_CHANGED, NULL);
    zd_wday = zlab(g_root, &font_zune_regular_14, TC(TEXT_MUTED), ""); lv_obj_align(zd_wday, LV_ALIGN_TOP_MID, 0, ry + rh + 8);
    zd_wday_paint();
    lv_obj_t *nx = zlab(g_root, &font_zune_regular_17, TC(ACCENT_PRIMARY), "next");
    lv_obj_align(nx, LV_ALIGN_TOP_MID, 0, ry + rh + 34);
    lv_obj_add_flag(nx, LV_OBJ_FLAG_CLICKABLE); lv_obj_set_ext_click_area(nx, 16);
    ui_on(nx, zd_next, LV_EVENT_CLICKED, NULL, "datetime.zune.next", UI_CORE);
}
/* the clock face */
static void zt_paint(void){
    float am = (zt_m * 6.0f - 90.0f) * 0.0174533f, ah = (((zt_h % 12) + zt_m / 60.0f) * 30.0f - 90.0f) * 0.0174533f;
    zt_mp[0].x = 180; zt_mp[0].y = 180; zt_mp[1].x = 180 + cosf(am) * 150; zt_mp[1].y = 180 + sinf(am) * 150;
    zt_hp[0].x = 180; zt_hp[0].y = 180; zt_hp[1].x = 180 + cosf(ah) * 86;  zt_hp[1].y = 180 + sinf(ah) * 86;
    lv_line_set_points(zt_mline, zt_mp, 2); lv_line_set_points(zt_hline, zt_hp, 2);
    lv_obj_set_pos(zt_knob, (int)zt_mp[1].x - 9, (int)zt_mp[1].y - 9);
    lv_obj_set_pos(zt_hknob, (int)zt_hp[1].x - 6, (int)zt_hp[1].y - 6);
    char b[16]; int h12 = zt_h % 12 ? zt_h % 12 : 12;
    if(cfg_get_int("time_24h", 1)) snprintf(b, sizeof b, "%02d:%02d", zt_h, zt_m); else snprintf(b, sizeof b, "%d:%02d", h12, zt_m);
    lv_label_set_text(zt_read, b);
    lv_obj_set_style_text_color(zt_am, zt_h < 12 ? TC(ACCENT_PRIMARY) : TC(TEXT_MUTED), 0);
    lv_obj_set_style_text_color(zt_pm, zt_h >= 12 ? TC(ACCENT_PRIMARY) : TC(TEXT_MUTED), 0);
}
static int zt_drag = -1;                                       /* 0 minute knob, 1 hour hand */
static void zt_touch(lv_event_t *e){
    lv_event_code_t c = lv_event_get_code(e);
    lv_point_t p; lv_indev_get_point(lv_indev_active(), &p);
    float dx = p.x - 180.0f, dy = p.y - 180.0f, d = sqrtf(dx * dx + dy * dy);
    if(c == LV_EVENT_PRESSED){ zt_drag = d > 112 ? 0 : d > 30 ? 1 : -1; return; }
    if(c == LV_EVENT_RELEASED || c == LV_EVENT_PRESS_LOST){ zt_drag = -1; return; }
    if(c != LV_EVENT_PRESSING || zt_drag < 0) return;
    float deg = atan2f(dy, dx) * 57.29578f + 90.0f; if(deg < 0) deg += 360.0f;
    if(zt_drag == 0){
        int m = (int)lroundf(deg / 6.0f) % 60;
        if(zt_m >= 45 && m < 15) zt_h = (zt_h + 1) % 24;        /* past 12 o'clock: the hour moves on */
        else if(zt_m < 15 && m >= 45) zt_h = (zt_h + 23) % 24;
        zt_m = m;
    } else {
        int h = (int)(deg / 30.0f) % 12;
        zt_h = (zt_h >= 12 ? 12 : 0) + h;
    }
    zt_paint();
}
static void zt_ampm(lv_event_t *e){ int pm = (int)(intptr_t)lv_event_get_user_data(e); if(pm != (zt_h >= 12)) zt_h = (zt_h + 12) % 24; zt_paint(); }
static void zt_set(lv_event_t *e){
    (void)e;
    struct tm tm; memset(&tm, 0, sizeof tm);
    tm.tm_year = zd_year - 1900; tm.tm_mon = zd_mon; tm.tm_mday = zd_day; tm.tm_hour = zt_h; tm.tm_min = zt_m; tm.tm_isdst = -1;
    time_t t = mktime(&tm);
    struct timeval tv = { .tv_sec = t, .tv_usec = 0 };
    if(t == (time_t)-1 || settimeofday(&tv, NULL) != 0){ ui_toast("Couldn't set the time"); return; }
    ui_rtc_save_now(); ui_clock_refresh();
    ui_toast(cfg_get_int("auto_time", 1) ? "time set - wi-fi may correct it" : "time set");
    screen_back();
}
static void ztime_page(void){
    lv_obj_clean(g_root);
    time_t now = time(NULL); struct tm lt; localtime_r(&now, &lt); zt_h = lt.tm_hour; zt_m = lt.tm_min;
    lv_obj_t *face = lv_obj_create(g_root); lv_obj_remove_style_all(face); kit_keep(face);   /* the whole glass takes the drag */
    lv_obj_set_size(face, 360, 360); lv_obj_add_flag(face, LV_OBJ_FLAG_CLICKABLE); lv_obj_remove_flag(face, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(face, zt_touch, LV_EVENT_ALL, NULL);
    for(int i = 0; i < 60; i++){                                /* minute dots, bigger on the hours */
        float a = (i * 6.0f - 90.0f) * 0.0174533f; int big = i % 5 == 0, sz = big ? 4 : 2;
        lv_obj_t *d = lv_obj_create(face); lv_obj_remove_style_all(d); kit_keep(d);
        lv_obj_set_size(d, sz, sz); lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(d, TC(TEXT_PRIMARY), 0); lv_obj_set_style_bg_opa(d, big ? LV_OPA_80 : LV_OPA_40, 0);
        lv_obj_set_pos(d, 180 + (int)lroundf(cosf(a) * 166) - sz / 2, 180 + (int)lroundf(sinf(a) * 166) - sz / 2);
        lv_obj_remove_flag(d, LV_OBJ_FLAG_CLICKABLE);
    }
    for(int h = 1; h <= 12; h++){
        float a = (h * 30.0f - 90.0f) * 0.0174533f; char b[4]; snprintf(b, sizeof b, "%d", h);
        lv_obj_t *l = zlab(face, &font_zune_regular_17, TC(TEXT_PRIMARY), b);
        lv_obj_update_layout(l);
        lv_obj_set_pos(l, 180 + (int)lroundf(cosf(a) * 136) - lv_obj_get_width(l) / 2, 180 + (int)lroundf(sinf(a) * 136) - lv_obj_get_height(l) / 2);
        lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
    }
    zt_read = zlab(face, &font_zune_light_32, TC(TEXT_PRIMARY), ""); lv_obj_align(zt_read, LV_ALIGN_TOP_MID, 0, 74);
    lv_obj_remove_flag(zt_read, LV_OBJ_FLAG_CLICKABLE);
    zt_am = zlab(g_root, &font_zune_regular_14, TC(TEXT_MUTED), "am"); lv_obj_align(zt_am, LV_ALIGN_TOP_MID, -18, 116);
    zt_pm = zlab(g_root, &font_zune_regular_14, TC(TEXT_MUTED), "pm"); lv_obj_align(zt_pm, LV_ALIGN_TOP_MID, 18, 116);
    lv_obj_t *ap[2] = { zt_am, zt_pm };
    for(int k = 0; k < 2; k++){ lv_obj_add_flag(ap[k], LV_OBJ_FLAG_CLICKABLE); lv_obj_set_ext_click_area(ap[k], 10);
        lv_obj_add_event_cb(ap[k], zt_ampm, LV_EVENT_CLICKED, (void *)(intptr_t)k); }
    zt_hline = lv_line_create(face); kit_keep(zt_hline);
    lv_obj_set_style_line_width(zt_hline, 4, 0); lv_obj_set_style_line_color(zt_hline, TC(TEXT_PRIMARY), 0);
    lv_obj_set_style_line_rounded(zt_hline, true, 0); lv_obj_remove_flag(zt_hline, LV_OBJ_FLAG_CLICKABLE);
    zt_mline = lv_line_create(face); kit_keep(zt_mline);
    lv_obj_set_style_line_width(zt_mline, 2, 0); lv_obj_set_style_line_color(zt_mline, TC(ACCENT_PRIMARY), 0);
    lv_obj_remove_flag(zt_mline, LV_OBJ_FLAG_CLICKABLE);
    zt_hknob = lv_obj_create(face); lv_obj_remove_style_all(zt_hknob); kit_keep(zt_hknob);
    lv_obj_set_size(zt_hknob, 12, 12); lv_obj_set_style_radius(zt_hknob, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(zt_hknob, TC(TEXT_PRIMARY), 0); lv_obj_set_style_bg_opa(zt_hknob, LV_OPA_COVER, 0);
    lv_obj_remove_flag(zt_hknob, LV_OBJ_FLAG_CLICKABLE);
    zt_knob = lv_obj_create(face); lv_obj_remove_style_all(zt_knob); kit_keep(zt_knob);
    lv_obj_set_size(zt_knob, 18, 18); lv_obj_set_style_radius(zt_knob, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(zt_knob, TC(ACCENT_PRIMARY), 0); lv_obj_set_style_bg_opa(zt_knob, LV_OPA_COVER, 0);
    lv_obj_remove_flag(zt_knob, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *hub = lv_obj_create(face); lv_obj_remove_style_all(hub); kit_keep(hub);
    lv_obj_set_size(hub, 8, 8); lv_obj_set_pos(hub, 176, 176); lv_obj_set_style_radius(hub, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(hub, TC(TEXT_PRIMARY), 0); lv_obj_set_style_bg_opa(hub, LV_OPA_COVER, 0);
    lv_obj_remove_flag(hub, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *st = zlab(g_root, &font_zune_regular_17, TC(ACCENT_PRIMARY), "set");
    lv_obj_align(st, LV_ALIGN_TOP_MID, 0, 228); lv_obj_add_flag(st, LV_OBJ_FLAG_CLICKABLE); lv_obj_set_ext_click_area(st, 14);
    ui_on(st, zt_set, LV_EVENT_CLICKED, NULL, "datetime.zune.set", UI_CORE);
    zt_paint();
}
/* rebuilt on every entry, starting from the current local time */
void datetime_refresh(void){
    if(!g_root) return;
    if(th_zune()){ void datetime_test_time(void); if(getenv("ZDT_TIME")) datetime_test_time(); else zdate_page(); return; }
    lv_obj_clean(g_root);
    ui_header_cb(g_root, "Date & Time", back_cb);
    time_t now = time(NULL);
    struct tm lt; localtime_r(&now, &lt);
    static char days[31 * 3], years[YEARS * 5], hours[24 * 3], mins[60 * 3];
    char *p = days;   for(int i = 1; i <= 31; i++) p += sprintf(p, i < 31 ? "%d\n" : "%d", i);
    p = years;        for(int i = 0; i < YEARS; i++) p += sprintf(p, i < YEARS - 1 ? "%d\n" : "%d", YEAR0 + i);
    p = hours;        for(int i = 0; i < 24; i++) p += sprintf(p, i < 23 ? "%02d\n" : "%02d", i);
    p = mins;         for(int i = 0; i < 60; i++) p += sprintf(p, i < 59 ? "%02d\n" : "%02d", i);
    int y = lt.tm_year + 1900 - YEAR0; if(y < 0) y = 0; if(y >= YEARS) y = YEARS - 1;
    /* rows laid out from the rollers' real height (it depends on the theme's font) */
    int y0 = 66;
    lv_obj_update_layout(g_root);                       /* below the header, however tall the theme draws it (Braun 2.0's panel) */
    for(uint32_t k = 0; k < lv_obj_get_child_count(g_root); k++){
        lv_obj_t *c = lv_obj_get_child(g_root, (int32_t)k);
        if(lv_obj_has_flag(c, LV_OBJ_FLAG_HIDDEN) || lv_obj_get_y(c) > 60) continue;
        int bot = lv_obj_get_y(c) + lv_obj_get_height(c) + 4;
        if(bot > y0 && bot < 110) y0 = bot;
    }
    r_day  = roller(g_root, days,   lt.tm_mday - 1, 62,  -80, y0);
    r_mon  = roller(g_root, MONTHS, lt.tm_mon,      74,    0, y0);
    r_year = roller(g_root, years,  y,              82,   84, y0);
    lv_obj_update_layout(r_day);
    int h = lv_obj_get_height(r_day), y1 = y0 + h + 8;
    r_hour = roller(g_root, hours,  lt.tm_hour,     70,  -42, y1);
    r_min  = roller(g_root, mins,   lt.tm_min,      70,   42, y1);
    lv_obj_t *colon = lv_label_create(g_root);
    lv_label_set_text(colon, ":");
    lv_obj_set_style_text_font(colon, TF(UI_20), 0);
    lv_obj_set_style_text_color(colon, TC(TEXT_PRIMARY), 0);
    lv_obj_align(colon, LV_ALIGN_TOP_MID, 0, y1 + h / 2 - 12);

    lv_obj_t *b = lv_button_create(g_root);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, 120, 40);
    lv_obj_align(b, LV_ALIGN_TOP_MID, 0, y1 + h + 10);
    lv_obj_set_style_radius(b, 20, 0);
    lv_obj_set_style_bg_color(b, ui_current_accent(), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_opa(b, LV_OPA_80, LV_STATE_PRESSED);
    ui_on(b, set_cb, LV_EVENT_CLICKED, NULL, "datetime.set", UI_CORE);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, "Set");
    lv_obj_set_style_text_font(l, TF(UI_18), 0);
    lv_obj_set_style_text_color(l, TC(ON_ACCENT), 0);
    lv_obj_center(l);
}
void datetime_test_time(void){ if(th_zune()){ zd_year = 2026; zd_mon = 9; zd_day = 22; ztime_page(); } }   /* host renders */
