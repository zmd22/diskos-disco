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

/* rebuilt on every entry, starting from the current local time */
void datetime_refresh(void){
    if(!g_root) return;
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
    const int y0 = 66;
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
