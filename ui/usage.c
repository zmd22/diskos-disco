/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
/* Battery & usage: a 24-hour dial (midnight at the top, hours clockwise).
 *   outer band - battery level (empty at the inner edge, full at the outer), line + soft fill; green = charging
 *   amber ring - screen on        red ring - playing
 *   centre     - charge now, a time-left estimate from the recent drain, and the day's totals
 * The recorder runs inside the UI: one sample a minute (battery %, charging, screen on at any point in that
 * minute, playing at any point), kept in RAM for a little over a day (all the screen uses) and saved to
 * /usr/data/usage.bin every 10 minutes
 * (and before an auto power-off). Nothing is recorded while the clock isn't set.
 * Since the last full charge: two running totals (minutes playing, minutes with the screen on) and the time of that
 * charge, reset when the Disc is unplugged after reaching full (100%, or 80% with Charging Limit on). A few bytes in
 * /usr/data/usage_charge.bin, saved with the rest. */
#include "screens.h"
#include "config.h"
#include "theme.h"
#include "braun.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifndef USAGE_FILE
#define USAGE_FILE "/usr/data/usage.bin"
#endif
#define US_CAP   1500                       /* 25 h of minutes: the last 24 h + margin (12 KB, not 80 KB, per save) */
#define FL_CHG   1
#define FL_SCR   2
#define FL_PLAY  4

typedef struct { uint32_t t; uint8_t pct, fl; uint16_t pad; } usmp_t;
static usmp_t g_s[US_CAP];                 /* ring, oldest at g_head when full */
static int g_head, g_n;
static int g_batt = -1, g_chg;
static uint32_t g_cur_min;                 /* minute being accumulated */
static uint8_t g_acc;                      /* flags seen during it */
static int g_unsaved;
#define USAGE_CHG_FILE USAGE_FILE "_charge"
typedef struct { uint32_t magic, full_at, play_min, scr_min, full_seen; } uschg_t;   /* "USC1" */
static uschg_t g_cy = { 0x31435355u, 0, 0, 0, 0 };

static usmp_t *at(int i){ return &g_s[(g_head + i) % US_CAP]; }   /* i = 0 oldest .. g_n-1 newest */
static void push(uint32_t t, int pct, uint8_t fl){
    usmp_t s = { t, (uint8_t)pct, fl, 0 };
    if(g_n < US_CAP){ g_s[(g_head + g_n) % US_CAP] = s; g_n++; }
    else { g_s[g_head] = s; g_head = (g_head + 1) % US_CAP; }
}
void usage_save(void){
    char tmp[128]; snprintf(tmp, sizeof tmp, "%s.tmp", USAGE_FILE);
    FILE *f = fopen(tmp, "wb"); if(!f) return;
    uint32_t hdr[2] = { 0x31475355u /* "USG1" */, (uint32_t)g_n };
    int ok = fwrite(hdr, sizeof hdr, 1, f) == 1;
    for(int i = 0; ok && i < g_n; i++) ok = fwrite(at(i), sizeof(usmp_t), 1, f) == 1;
    if(fflush(f) != 0) ok = 0;
    fsync(fileno(f));
    if(fclose(f) != 0) ok = 0;
    if(ok) rename(tmp, USAGE_FILE); else unlink(tmp);
    g_unsaved = 0;
    snprintf(tmp, sizeof tmp, "%s.tmp", USAGE_CHG_FILE);       /* the since-full-charge totals */
    f = fopen(tmp, "wb"); if(!f) return;
    ok = fwrite(&g_cy, sizeof g_cy, 1, f) == 1;
    if(fflush(f) != 0) ok = 0;
    fsync(fileno(f));
    if(fclose(f) != 0) ok = 0;
    if(ok) rename(tmp, USAGE_CHG_FILE); else unlink(tmp);
}
static void load(void){
    FILE *c = fopen(USAGE_CHG_FILE, "rb");
    if(c){ uschg_t t; if(fread(&t, sizeof t, 1, c) == 1 && t.magic == 0x31435355u) g_cy = t; fclose(c); }
    FILE *f = fopen(USAGE_FILE, "rb"); if(!f) return;
    uint32_t hdr[2];
    if(fread(hdr, sizeof hdr, 1, f) == 1 && hdr[0] == 0x31475355u){
        uint32_t n = hdr[1] > US_CAP ? US_CAP : hdr[1];
        /* a file with more than we keep: skip to its newest US_CAP samples */
        if(hdr[1] > US_CAP) fseek(f, (long)(hdr[1] - US_CAP) * (long)sizeof(usmp_t), SEEK_CUR);
        usmp_t s;
        for(uint32_t i = 0; i < n && fread(&s, sizeof s, 1, f) == 1; i++) if(s.pct <= 100) push(s.t, s.pct, s.fl);
    }
    fclose(f);
}
/* from the 3 s status poll */
void usage_note_battery(int pct, int charging){ if(pct >= 0 && pct <= 100){ g_batt = pct; g_chg = charging; } }
/* from every main-loop pass: cheap unless the minute just turned over */
void usage_tick(int screen_on, int playing){
    static int loaded;
    if(!loaded){ loaded = 1; load(); }
    time_t now = time(NULL);
    if(now < 1600000000) return;                           /* the clock isn't set yet: record nothing */
    uint32_t m = (uint32_t)(now / 60);
    if(!g_cur_min) g_cur_min = m;
    /* a full charge: reached full while charging, then unplugged -> the totals start again from here */
    if(g_batt >= 0){
        int target = cfg_get_int("charge_protect", 0) ? 80 : 100;
        static int boot_checked;                             /* charged while switched off: starts up full, was lower */
        if(!boot_checked){ boot_checked = 1;
            if(!g_chg && g_n > 0 && g_batt >= target - 1 && g_batt > at(g_n - 1)->pct + 5){
                g_cy.full_seen = 0; g_cy.full_at = (uint32_t)now; g_cy.play_min = g_cy.scr_min = 0; usage_save(); } }
        if(g_chg && g_batt >= target - 1) g_cy.full_seen = 1;
        else if(!g_chg && g_cy.full_seen){
            g_cy.full_seen = 0; g_cy.full_at = (uint32_t)now; g_cy.play_min = g_cy.scr_min = 0; usage_save();
        }
    }
    if(m != g_cur_min){
        if(g_acc & FL_PLAY) g_cy.play_min++;
        if(g_acc & FL_SCR)  g_cy.scr_min++;
        if(g_batt >= 0) push(g_cur_min * 60u, g_batt, g_acc);
        g_cur_min = m; g_acc = 0;
        if(++g_unsaved >= 10) usage_save();
    }
    g_acc |= (g_chg ? FL_CHG : 0) | (screen_on ? FL_SCR : 0) | (playing ? FL_PLAY : 0);
}

/* ================================ the screen ================================
 * One clear page in every theme: the charge on a ring around the rim, the percentage big in the middle, the time
 * left at the recent pace (or "Charging"), and two figures for the last 24 hours: playing and screen on. */
static lv_obj_t *g_ring, *g_pct, *g_chg_ic, *g_sub, *g_play_v, *g_scr_v, *g_cap;
static lv_timer_t *g_tmr;

static void fmt_dur(char *b, size_t n, int mins){
    if(mins < 60) snprintf(b, n, "%d min", mins);
    else snprintf(b, n, "%d h %02d m", mins / 60, mins % 60);
}
/* the time left from the drain over the last 3 h of battery-only samples (-1 = not enough data yet, -2 = barely draining).
 * The run stops at a charge, at a gap (the Disc was off: no drain was measured then) and where the level rose (a charge
 * that wasn't seen), so only one unbroken stretch of discharge is measured. */
#define US_GAP_S (5 * 60)
static int left_minutes(void){
    if(g_n < 2) return -1;
    int last = g_n - 1; uint32_t tl = at(last)->t, lim = tl > 3 * 3600 ? tl - 3 * 3600 : 0;
    if(at(last)->fl & FL_CHG) return -1;
    int j = last;
    while(j > 0){
        const usmp_t *p = at(j - 1), *c = at(j);
        if((p->fl & FL_CHG) || p->t < lim || c->t < p->t || c->t - p->t > US_GAP_S || p->pct < c->pct) break;
        j--;
    }
    float mins = (tl - at(j)->t) / 60.0f, drop = (float)at(j)->pct - at(last)->pct;
    if(mins < 30) return -1;
    if(drop < 1) return -2;
    int pct = g_batt >= 0 ? g_batt : at(last)->pct;
    return (int)(pct / (drop / mins));
}
static void stat_block(lv_obj_t *root, int x, const char *icon, const char *cap, lv_obj_t **val){
    lv_obj_t *b = lv_obj_create(root); lv_obj_remove_style_all(b);
    lv_obj_set_size(b, 108, 54); lv_obj_align(b, LV_ALIGN_TOP_MID, x, 250);
    lv_obj_set_style_radius(b, th_braun() ? 8 : 18, 0);
    lv_obj_set_style_bg_color(b, TC(SURFACE), 0); lv_obj_set_style_bg_opa(b, th_disco() ? 150 : LV_OPA_COVER, 0);
    lv_obj_clear_flag(b, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *c = lv_label_create(b); lv_label_set_text_fmt(c, "%s  %s", icon, cap);
    lv_obj_set_style_text_font(c, th_braun() ? br_font(12, 0) : TF(UI_12), 0); lv_obj_set_style_text_color(c, TC(TEXT_SECONDARY), 0);
    lv_obj_align(c, LV_ALIGN_TOP_MID, 0, 8);
    *val = lv_label_create(b); lv_label_set_text(*val, "-");
    lv_obj_set_style_text_font(*val, th_braun() ? br_font(16, 1) : TF(UI_16), 0); lv_obj_set_style_text_color(*val, TC(TEXT_PRIMARY), 0);
    lv_obj_align(*val, LV_ALIGN_BOTTOM_MID, 0, -8);
}
static void draw(void){
    if(!g_pct) return;
    int pct = g_batt >= 0 ? g_batt : (g_n ? at(g_n - 1)->pct : -1);
    char b[64];
    if(pct >= 0) snprintf(b, sizeof b, "%d%%", pct); else snprintf(b, sizeof b, "-");
    lv_label_set_text(g_pct, b);
    lv_arc_set_value(g_ring, pct >= 0 ? pct : 0);
    lv_color_t c = g_chg ? TC(STATUS_SUCCESS) : (pct >= 0 && pct <= 15) ? TC(STATUS_DANGER) : th_braun() ? TC(ACCENT_PRIMARY) : ui_current_accent();
    lv_obj_set_style_arc_color(g_ring, c, LV_PART_INDICATOR);
    if(g_chg) lv_obj_remove_flag(g_chg_ic, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(g_chg_ic, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_text_color(g_chg_ic, c, 0);
    if(g_chg) snprintf(b, sizeof b, "Charging");
    else {
        int m = left_minutes();
        if(m == -1) snprintf(b, sizeof b, "Estimating time left...");
        else if(m == -2) snprintf(b, sizeof b, "Barely draining");
        else if(m >= 48 * 60) snprintf(b, sizeof b, "About %d days left", (m / 60 + 12) / 24);
        else if(m >= 120) snprintf(b, sizeof b, "About %d h left", (m + 30) / 60);
        else { char d[24]; fmt_dur(d, sizeof d, m); snprintf(b, sizeof b, "About %s left", d); }
    }
    lv_label_set_text(g_sub, b);
    time_t nowt = time(NULL);
    if(nowt < 1600000000){ lv_label_set_text(g_play_v, "-"); lv_label_set_text(g_scr_v, "-"); return; }   /* clock not set: no "last 24 h" */
    if(g_cy.full_at && g_cy.full_at <= (uint32_t)nowt){         /* since the last full charge */
        uint32_t ago = (uint32_t)nowt - g_cy.full_at;
        if(ago < 3600) snprintf(b, sizeof b, "Since full charge, just now");
        else if(ago < 48 * 3600) snprintf(b, sizeof b, "Since full charge, %u h ago", ago / 3600);
        else snprintf(b, sizeof b, "Since full charge, %u days ago", ago / 86400);
        lv_label_set_text(g_cap, b);
        fmt_dur(b, sizeof b, (int)g_cy.play_min); lv_label_set_text(g_play_v, b);
        fmt_dur(b, sizeof b, (int)g_cy.scr_min);  lv_label_set_text(g_scr_v, b);
        return;
    }
    lv_label_set_text(g_cap, "Last 24 hours");                   /* no full charge seen yet */
    uint32_t to = (uint32_t)nowt, from = to > 86400 ? to - 86400 : 0;   /* the last 24 hours */
    int scr = 0, play = 0;
    for(int i = 0; i < g_n; i++){ const usmp_t *s = at(i); if(s->t < from || s->t > to) continue; if(s->fl & FL_SCR) scr++; if(s->fl & FL_PLAY) play++; }
    fmt_dur(b, sizeof b, play); lv_label_set_text(g_play_v, b);
    fmt_dur(b, sizeof b, scr);  lv_label_set_text(g_scr_v, b);
}
static void us_tick_cb(lv_timer_t *t){ (void)t; if(screen_current() == SCR_USAGE) draw(); }
void usage_create(lv_obj_t *root){
    int disco = th_disco(), br = th_braun();
    int dx = disco ? -12 : 0;                                /* Disco: clear of the navigation sliver */
    lv_obj_set_style_bg_color(root, TC(CANVAS), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    if(br){ br_face(root); lv_obj_t *pd = br_disc(root, 180, 180, 150, BR_PANEL); (void)pd; }
    int d = disco ? 290 : br ? 296 : 340;
    g_ring = lv_arc_create(root);
    lv_obj_remove_style(g_ring, NULL, LV_PART_KNOB); lv_obj_clear_flag(g_ring, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(g_ring, d, d); lv_obj_align(g_ring, LV_ALIGN_CENTER, dx, 0);
    lv_arc_set_rotation(g_ring, 150); lv_arc_set_bg_angles(g_ring, 0, 240);   /* open at the bottom for the figures */ lv_arc_set_range(g_ring, 0, 100);
    lv_obj_set_style_arc_width(g_ring, 12, LV_PART_MAIN); lv_obj_set_style_arc_width(g_ring, 12, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(g_ring, true, LV_PART_MAIN); lv_obj_set_style_arc_rounded(g_ring, true, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(g_ring, br ? TC(CONTROL_TRACK) : TC(SURFACE_RAISED), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(g_ring, disco ? 170 : LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_t *t = lv_label_create(root); lv_label_set_text(t, "Battery");
    lv_obj_set_style_text_font(t, br ? br_font(16, 1) : TF(UI_18), 0); lv_obj_set_style_text_color(t, TC(TEXT_SECONDARY), 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, dx, disco ? 62 : 46);
    g_pct = lv_label_create(root);
    lv_obj_set_style_text_font(g_pct, br ? br_font(44, 1) : TF(UI_46), 0); lv_obj_set_style_text_color(g_pct, TC(TEXT_PRIMARY), 0);
    lv_obj_align(g_pct, LV_ALIGN_TOP_MID, dx, 96);
    g_chg_ic = lv_label_create(root); lv_label_set_text(g_chg_ic, LV_SYMBOL_CHARGE);
    lv_obj_set_style_text_font(g_chg_ic, TF(UI_20), 0); lv_obj_align(g_chg_ic, LV_ALIGN_TOP_MID, dx + 78, 112);
    g_sub = lv_label_create(root);
    lv_label_set_long_mode(g_sub, LV_LABEL_LONG_DOT); lv_obj_set_width(g_sub, 230);
    lv_obj_set_style_text_align(g_sub, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(g_sub, br ? br_font(16, 0) : TF(UI_16), 0); lv_obj_set_style_text_color(g_sub, TC(TEXT_PRIMARY), 0);
    lv_obj_align(g_sub, LV_ALIGN_TOP_MID, dx, 162);
    lv_obj_t *cap = g_cap = lv_label_create(root); lv_label_set_text(cap, "Last 24 hours");
    lv_obj_set_style_text_font(cap, br ? br_font(12, 0) : TF(UI_12), 0); lv_obj_set_style_text_color(cap, TC(TEXT_SECONDARY), 0);
    lv_obj_align(cap, LV_ALIGN_TOP_MID, dx, 222);
    stat_block(root, dx - 58, LV_SYMBOL_PLAY, "Playing", &g_play_v);
    stat_block(root, dx + 58, LV_SYMBOL_EYE_OPEN, "Screen on", &g_scr_v);
    if(!g_tmr) g_tmr = lv_timer_create(us_tick_cb, 30000, NULL);
    draw();
}
void usage_refresh(void){ draw(); }
/* host renders only: six hours of made-up use (never called on the Disc) */
void usage_demo(int charging){
    uint32_t now = (uint32_t)time(NULL);
    for(int m = 360; m > 0; m--){
        uint8_t fl = (m % 90 < 50 ? FL_PLAY : 0) | (m % 120 < 12 ? FL_SCR : 0);
        push(now - (uint32_t)m * 60u, 82 - (360 - m) / 24, fl);
    }
    g_batt = 67; g_chg = charging;
    if(getenv("USAGE_CYCLE")){ g_cy.full_at = now - 26u * 3600u; g_cy.play_min = 412; g_cy.scr_min = 75; }   /* since a full charge, 26 h ago */
    draw();
}
