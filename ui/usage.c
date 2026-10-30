#include "simple_theme.h"
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
/* Battery & usage: an open charge gauge around the navigation side.
 * Right: 100% upper-right, counter-clockwise down to 0%; Left mirrors it.
 * The centre shows charge and an estimate; two pills show playback/screen-on totals.
 * All colours/fonts use theme roles. Pill backgrounds sit below the gauge.
 * The recorder runs inside the UI: one sample a minute (battery %, charging, screen on at any point in that
 * minute, playing at any point), kept in RAM for a little over a day (all the screen uses) and saved to
 * /usr/data/usage.bin every 10 minutes
 * (and before an auto power-off). Nothing is recorded while the clock isn't set.
 * Since the last full charge: two running totals (minutes playing, minutes with the screen on) and the time of that
 * charge, reset when the Disc is unplugged after reaching full (95% - the Disc rarely shows 100 - or 80% with Charging Limit on). A few bytes in
 * /usr/data/usage_charge.bin, saved with the rest. */
#include "screens.h"
#include "zune_dial.h"
#include "config.h"
#include "theme.h"
#include "braun.h"
#include <math.h>
#include <stdint.h>
#include <dirent.h>
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
int usage_charging(void){ return g_chg; }   /* the battery is being charged (positive charge current) */
/* Plugged in: any power source other than the battery reports online=1 (USB, a charger: whatever the kernel calls it,
 * so no name is assumed). Catches a full battery on the charger, which draws no charge current. Read at most every
 * 5 s; -1 when the Disc lists no such source (callers then rely on usage_charging()). */
#ifndef POWER_SUPPLY_DIR
#define POWER_SUPPLY_DIR "/sys/class/power_supply"
#endif
/* This Disc lists no charger at all, and its gauge reports current only while it is actually charging: once charging
 * stops (battery full, or held at 80 % by Charging optimization) a connected charger is invisible. So remember it -
 * seen charging means on the charger, until the level drops below where the charging stopped (a 1 % fall can only
 * happen on battery). A USB connection to a computer counts too. Errs on the safe side: after unplugging a full
 * Disc, it counts as plugged in until the first 1 % is used. */
static int charger_remembered(void){
    static int on, held = -1;
    if(g_chg){ on = 1; if(g_batt > held) held = g_batt; }
    else if(on && g_batt >= 0 && held >= 0 && g_batt < held){ on = 0; held = -1; }
    if(!on) held = -1;
    return on;
}
static int usb_to_computer(void){   /* read at most every 5 s */
    static uint32_t at; static int have, on;
    if(have && lv_tick_get() - at < 5000u) return on;
    have = 1; at = lv_tick_get();
    char v[24] = ""; FILE *f = fopen("/sys/class/android_usb/android0/state", "r");
    if(f){ if(!fgets(v, sizeof v, f)) v[0] = 0; fclose(f); }
    on = !strncmp(v, "CONFIGURED", 10);
    return on;
}
static int plugged_listed(void);
int usage_plugged_in(void){
    int r = plugged_listed();
    if(r == 1) return 1;
    if(charger_remembered() || usb_to_computer()) return 1;
    return r;
}
static int plugged_listed(void){
    static uint32_t at; static int have, cached = -1;
    uint32_t now = lv_tick_get();
    if(have && now - at < 5000u) return cached;
    have = 1; at = now; cached = -1;
    DIR *d = opendir(POWER_SUPPLY_DIR); if(!d) return cached;
    struct dirent *e;
    while((e = readdir(d))){
        if(e->d_name[0] == '.') continue;
        char p[320], v[32] = ""; FILE *f;
        snprintf(p, sizeof p, POWER_SUPPLY_DIR "/%s/type", e->d_name);
        if((f = fopen(p, "r"))){ if(!fgets(v, sizeof v, f)) v[0] = 0; fclose(f); }
        if(!strncmp(v, "Battery", 7)) continue;                    /* the battery itself doesn't count */
        snprintf(p, sizeof p, POWER_SUPPLY_DIR "/%s/online", e->d_name);
        if(!(f = fopen(p, "r"))) continue;
        int on = 0; if(fscanf(f, "%d", &on) != 1) on = -1; fclose(f);
        if(on < 0) continue;
        if(cached < 0) cached = 0;                                 /* at least one source tells us */
        if(on == 1){ cached = 1; break; }
    }
    closedir(d);
    return cached;
}
/* The status poll that feeds usage_note_battery stops while the screen is off, so on its own the level and the
 * charger state froze there: a charge made with the screen off was never seen (the since-full totals never reset)
 * and screen-off minutes were logged at a stale level. So the recorder reads the gauge itself, once a minute. */
static void read_gauge(void){
#ifndef USAGE_NO_SYSFS
    int pct = -1; long cur = 0;
    FILE *f = fopen("/sys/class/power_supply/cw221X-bat/capacity", "r");
    if(f){ if(fscanf(f, "%d", &pct) != 1) pct = -1; fclose(f); }
    f = fopen("/sys/class/power_supply/cw221X-bat/current_now", "r");
    if(f){ if(fscanf(f, "%ld", &cur) != 1) cur = 0; fclose(f); }
    if(pct >= 0 && pct <= 100){ g_batt = pct; g_chg = cur > 0; }
#endif
}
/* from every main-loop pass: cheap unless the minute just turned over */
void usage_tick(int screen_on, int playing){
    static int loaded;
    if(!loaded){ loaded = 1; load(); read_gauge(); }
    time_t now = time(NULL);
    if(now < 1600000000) return;                           /* the clock isn't set yet: record nothing */
    uint32_t m = (uint32_t)(now / 60);
    if(!g_cur_min) g_cur_min = m;
    if(m != g_cur_min) read_gauge();                       /* a fresh level for the minute being closed */
    /* a full charge: reached full while charging, then unplugged -> the totals start again from here */
    if(g_batt >= 0){
        int target = simple_cfg_get_int("charge_protect", 0) ? 80 : 96;   /* full = 95% or more (the gauge seldom reaches 100); 79+ with the limit */
        /* Seen charging at full: remember it. Unplugged at full: a full charge, also when the charge itself wasn't
         * watched - the Disc asleep or switched off while it charged - which shows as the level having risen 5 points
         * or more since the last minute we recorded, or that minute having been on the charger. */
        if(g_chg && g_batt >= target - 1 && !g_cy.full_seen){ g_cy.full_seen = 1; usage_save(); }   /* kept on disk at once: */
        /* a restart of the interface between full and unplugging (USB, an update) must not forget it */
        if(!g_chg && g_batt >= target - 1 && (g_cy.full_seen ||
           (g_n > 0 && ((at(g_n - 1)->fl & FL_CHG) || g_batt >= at(g_n - 1)->pct + 5)))){
            g_cy.full_seen = 0; g_cy.full_at = (uint32_t)now; g_cy.play_min = g_cy.scr_min = 0; usage_save();
            if(cfg_get_int("batt_manual", 0)) cfg_set_int("batt_manual", 0);   /* a real full charge again */
            g_acc &= (uint8_t)~FL_CHG;                       /* this minute starts the new cycle */
            if(g_n > 0) at(g_n - 1)->fl &= (uint8_t)~FL_CHG; /* and the reset fires once, not again next pass */
            if(g_n > 0 && at(g_n - 1)->pct < g_batt) at(g_n - 1)->pct = (uint8_t)g_batt;
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
/* minutes until full while charging, from the rise over the last unbroken charging stretch (-1 = not enough yet).
 * "Full" is the Charging Limit's 80% when it's on, else 100%. */
static int full_minutes(int target){
    if(g_n < 2 || !(at(g_n - 1)->fl & FL_CHG)) return -1;
    int last = g_n - 1; uint32_t tl = at(last)->t, lim = tl > 2 * 3600 ? tl - 2 * 3600 : 0;
    int j = last;
    while(j > 0){
        const usmp_t *p = at(j - 1), *c = at(j);
        if(!(p->fl & FL_CHG) || p->t < lim || c->t < p->t || c->t - p->t > US_GAP_S || p->pct > c->pct) break;
        j--;
    }
    float mins = (tl - at(j)->t) / 60.0f, rise = (float)at(last)->pct - at(j)->pct;
    if(mins < 10 || rise < 1) return -1;
    int pct = g_batt >= 0 ? g_batt : at(last)->pct;
    if(pct >= target) return 0;
    return (int)((target - pct) / (rise / mins) + 0.5f);
}
/* "14:30" / "2:30 PM", with the weekday when it isn't today */
static void fmt_clock(char *b, size_t n, time_t t){
    struct tm tm, now_tm; time_t now = time(NULL); localtime_r(&t, &tm); localtime_r(&now, &now_tm);
    static const char *const WD[7] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
    char hm[16];
    if(simple_cfg_get_int("time_24h", 1)) snprintf(hm, sizeof hm, "%02d:%02d", tm.tm_hour, tm.tm_min);
    else snprintf(hm, sizeof hm, "%d:%02d %s", tm.tm_hour % 12 ? tm.tm_hour % 12 : 12, tm.tm_min, tm.tm_hour < 12 ? "AM" : "PM");
    if(tm.tm_yday == now_tm.tm_yday && tm.tm_year == now_tm.tm_year) snprintf(b, n, "%s", hm);
    else snprintf(b, n, "%s %s", WD[tm.tm_wday], hm);
}
/* one figure at the bottom: the time, a small caption under it (no pill: they crowded the ring) */
static void figure(lv_obj_t *root, int cx, const char *cap, lv_obj_t **val){
    *val=lv_label_create(root);lv_label_set_text(*val,"-");lv_obj_set_width(*val,110);
    lv_obj_set_style_text_align(*val,LV_TEXT_ALIGN_CENTER,0);
    lv_obj_set_style_text_font(*val,TF(UI_20),0);lv_obj_set_style_text_color(*val,TC(TEXT_PRIMARY),0);
    lv_obj_align(*val,LV_ALIGN_TOP_MID,cx,246);
    lv_obj_t *c=lv_label_create(root);lv_label_set_text(c,cap);lv_obj_set_width(c,110);
    lv_obj_set_style_text_align(c,LV_TEXT_ALIGN_CENTER,0);
    lv_obj_set_style_text_font(c,TF(UI_14),0);lv_obj_set_style_text_color(c,TC(TEXT_SECONDARY),0);
    lv_obj_align(c,LV_ALIGN_TOP_MID,cx,274);
}
/* the glowing tip at the charge level: angle along the ring for pct */
static lv_obj_t *g_tip, *g_sub2; static int g_left, g_start = 40;
static void place_tip(int pct, lv_color_t c){
    if(!g_tip) return;
    float deg = g_left ? g_start + 280.0f - 2.8f * pct : g_start + 2.8f * pct, a = deg * 3.14159265f / 180.0f;
    int x = 180 + (int)lroundf(cosf(a) * 169), y = 180 + (int)lroundf(sinf(a) * 169);
    lv_obj_set_pos(g_tip, x - 7, y - 7);
    lv_obj_set_style_bg_color(g_tip, c, 0); lv_obj_set_style_shadow_color(g_tip, c, 0);
    if(pct >= 0) lv_obj_remove_flag(g_tip, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(g_tip, LV_OBJ_FLAG_HIDDEN);
}
static int g_zu;static void zu_draw(void);
static void draw(void){
    if(g_zu){zu_draw();return;}
    if(!g_pct) return;
    int pct = g_batt >= 0 ? g_batt : (g_n ? at(g_n - 1)->pct : -1);
    char b[64];
    if(pct >= 0) snprintf(b, sizeof b, "%d%%", pct); else snprintf(b, sizeof b, "-");
    lv_label_set_text(g_pct, b);
    lv_arc_set_value(g_ring, pct >= 0 ? pct : 0);
    lv_color_t c = g_chg ? TC(STATUS_SUCCESS) : (pct >= 0 && pct <= 15) ? TC(STATUS_DANGER) : th_braun_family() ? TC(ACCENT_PRIMARY) : ui_current_accent();
    lv_obj_set_style_arc_color(g_ring, c, LV_PART_INDICATOR);
    if(g_chg) lv_obj_remove_flag(g_chg_ic, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(g_chg_ic, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_text_color(g_chg_ic, c, 0);
    char b2[64]; b2[0] = 0;
    if(g_chg){
        int lim = simple_cfg_get_int("charge_protect", 0), target = lim ? 80 : 100, m = full_minutes(target);
        if(pct >= 0 && pct >= target - (lim ? 1 : 4)){ snprintf(b, sizeof b, "Charged"); snprintf(b2, sizeof b2, lim ? "Charging Limit: 80%%" : "Topping up"); }
        else if(m < 0){ snprintf(b, sizeof b, "Charging"); snprintf(b2, sizeof b2, "Estimating time to full..."); }
        else {
            char d[24]; fmt_dur(d, sizeof d, m < 1 ? 1 : m); snprintf(b, sizeof b, "Full in ~%s", d);
            char t[24]; fmt_clock(t, sizeof t, time(NULL) + (time_t)m * 60); snprintf(b2, sizeof b2, "%s at %s", lim ? "80%" : "Full", t);
        }
    } else {
        int m = left_minutes();
        if(m == -1){ snprintf(b, sizeof b, "Estimating..."); snprintf(b2, sizeof b2, "Needs 30 min on battery"); }
        else if(m == -2){ snprintf(b, sizeof b, "Low drain"); snprintf(b2, sizeof b2, "Barely used lately"); }
        else {
            if(m >= 48 * 60) snprintf(b, sizeof b, "~%d days left", (m / 60 + 12) / 24);
            else if(m >= 120) snprintf(b, sizeof b, "~%d h left", (m + 30) / 60);
            else { char d[24]; fmt_dur(d, sizeof d, m); snprintf(b, sizeof b, "~%s left", d); }
            if(m < 48 * 60){ char t[24]; fmt_clock(t, sizeof t, time(NULL) + (time_t)m * 60); snprintf(b2, sizeof b2, "Until %s at this pace", t); }
            else snprintf(b2, sizeof b2, "At your recent pace");
        }
    }
    lv_label_set_text(g_sub2, b2);
    place_tip(pct, c);
    lv_label_set_text(g_sub, b);
    time_t nowt = time(NULL);
    if(nowt < 1600000000){ lv_label_set_text(g_play_v, "-"); lv_label_set_text(g_scr_v, "-"); return; }   /* clock not set: no "last 24 h" */
    if(g_cy.full_at && g_cy.full_at <= (uint32_t)nowt){         /* since the last full charge */
        uint32_t ago = (uint32_t)nowt - g_cy.full_at;
        int man = cfg_get_int("batt_manual", 0);         /* restarted by hand (hold the caption), not a full charge */
        const char *w = th_null() ? (man ? "Reset" : "Full charge") : (man ? "Since reset" : "Since full charge");
        const char *sep = th_null() ? ":" : ",";
        if(ago < 3600) snprintf(b, sizeof b, "%s%s just now", w, sep);
        else if(ago < 48 * 3600) snprintf(b, sizeof b, "%s%s %u h ago", w, sep, ago / 3600);
        else snprintf(b, sizeof b, "%s%s %u days ago", w, sep, ago / 86400);
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
/* Hold the caption under the hairline: restart the totals from now (for when the full charge wasn't noticed) */
static void cap_hold_cb(lv_event_t *e){
    (void)e;
    time_t now = time(NULL);
    if(now < 1600000000){ ui_toast("Set the clock first"); return; }
    g_cy.full_seen = 0; g_cy.full_at = (uint32_t)now; g_cy.play_min = g_cy.scr_min = 0;
    usage_save(); cfg_set_int("batt_manual", 1);
    ui_toast("Battery totals restarted");
    draw();
}
static void us_tick_cb(lv_timer_t *t){ (void)t; if(screen_current() == SCR_USAGE) draw(); }
LV_FONT_DECLARE(font_zune_regular_17) LV_FONT_DECLARE(font_zune_regular_16) LV_FONT_DECLARE(font_zune_light_96) LV_FONT_DECLARE(font_zune_regular_14) LV_FONT_DECLARE(font_zune_regular_12)
LV_FONT_DECLARE(font_zune_regular_13) LV_FONT_DECLARE(font_zune_semibold_11)
static lv_obj_t *g_zu_cap, *g_zu_l1, *g_zu_l2, *g_zu_l3, *g_zu_lim, *g_zu_tick, *g_zu_tlab;
static void zdur(char *b, size_t n, int m){
    if(m < 60) snprintf(b, n, "%dm", m); else if(m % 60 == 0 || m >= 600) snprintf(b, n, "%dh", (m + 30) / 60);
    else snprintf(b, n, "%dh %dm", m / 60, m % 60);
}
static lv_obj_t *zu_lab(lv_obj_t *root, const lv_font_t *f, lv_color_t c, int y){
    lv_obj_t *l = lv_label_create(root); lv_label_set_text(l, "");
    lv_obj_set_width(l, 260); lv_label_set_long_mode(l, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(l, zune_font(f), 0); lv_obj_set_style_text_color(l, c, 0);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, y); return l;
}
static void zu_draw(void){
    int pct = g_batt >= 0 ? g_batt : (g_n ? at(g_n - 1)->pct : -1);
    int lim = simple_cfg_get_int("charge_protect", 0);
    char b[80], d[24];
    if(pct >= 0) snprintf(b, sizeof b, "%d%%", pct); else snprintf(b, sizeof b, "-");
    lv_label_set_text(g_pct, b);
    lv_arc_set_value(g_ring, pct >= 0 ? pct : 0);
    lv_color_t c = (!g_chg && pct >= 0 && pct <= 15) ? TC(STATUS_DANGER) : TC(ACCENT_PRIMARY);
    lv_obj_set_style_arc_color(g_ring, c, LV_PART_INDICATOR);
    if(lim){ lv_obj_remove_flag(g_zu_tick, LV_OBJ_FLAG_HIDDEN); } else lv_obj_add_flag(g_zu_tick, LV_OBJ_FLAG_HIDDEN);
    int plugged = !g_chg && usage_plugged_in() == 1;       /* on the charger but drawing nothing: full, or held at 80% */
    if(g_chg || plugged){
        lv_obj_add_flag(g_zu_cap, LV_OBJ_FLAG_HIDDEN); lv_obj_remove_flag(g_chg_ic, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_zu_l2, LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(g_zu_l3, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_zu_tlab, LV_OBJ_FLAG_HIDDEN);   /* the limit reads at the bottom: a side label crowded the figure */
        if(lim) lv_obj_remove_flag(g_zu_lim, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(g_zu_lim, LV_OBJ_FLAG_HIDDEN);
        lv_obj_align(g_pct, LV_ALIGN_TOP_MID, 0, 96);
        int target = lim ? 80 : 100, m = full_minutes(target);
        if(plugged) snprintf(b, sizeof b, lim && pct >= 0 && pct >= 75 ? "plugged in · held at 80%%" : pct >= 96 ? "plugged in · full" : "plugged in · not charging");
        else if(pct >= 0 && pct >= target - (lim ? 1 : 4)) snprintf(b, sizeof b, lim ? "charged · held at 80%%" : "charged · topping up");
        else if(m < 0) snprintf(b, sizeof b, "charging · estimating");
        else { zdur(d, sizeof d, m < 1 ? 1 : m); snprintf(b, sizeof b, "charging · %s in %s", lim ? "80%" : "full", d); }
        lv_label_set_text(g_zu_l1, b); lv_obj_align(g_zu_l1, LV_ALIGN_TOP_MID, 0, 204);
        return;
    }
    lv_obj_remove_flag(g_zu_cap, LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(g_chg_ic, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(g_zu_tlab, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(g_zu_l2, LV_OBJ_FLAG_HIDDEN); lv_obj_remove_flag(g_zu_l3, LV_OBJ_FLAG_HIDDEN);
    if(lim) lv_obj_remove_flag(g_zu_lim, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(g_zu_lim, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(g_pct, LV_ALIGN_TOP_MID, 0, 88);
    int m = left_minutes();
    if(m == -1) snprintf(b, sizeof b, "estimating time left...");
    else if(m == -2) snprintf(b, sizeof b, "barely draining lately");
    else if(m >= 48 * 60) snprintf(b, sizeof b, "about %d days left", (m / 60 + 12) / 24);
    else { zdur(d, sizeof d, m >= 120 ? (m + 30) / 60 * 60 : m); snprintf(b, sizeof b, "about %s playback left", d); }
    lv_label_set_text(g_zu_l1, b); lv_obj_align(g_zu_l1, LV_ALIGN_TOP_MID, 0, 184);
    time_t nowt = time(NULL); int play = -1, scr = -1;
    if(nowt < 1600000000){ lv_label_set_text(g_zu_l2, "set the clock to see totals"); lv_label_set_text(g_zu_l3, ""); return; }
    if(g_cy.full_at && g_cy.full_at <= (uint32_t)nowt){
        lv_label_set_text(g_zu_l2, cfg_get_int("batt_manual", 0) ? "since reset" : "since full charge");
        play = (int)g_cy.play_min; scr = (int)g_cy.scr_min;
    } else {
        lv_label_set_text(g_zu_l2, "last 24 hours");
        uint32_t to = (uint32_t)nowt, from = to > 86400 ? to - 86400 : 0; play = scr = 0;
        for(int i = 0; i < g_n; i++){ const usmp_t *s = at(i); if(s->t < from || s->t > to) continue; if(s->fl & FL_SCR) scr++; if(s->fl & FL_PLAY) play++; }
    }
    char p[24], s2[24]; zdur(p, sizeof p, play); zdur(s2, sizeof s2, scr);
    snprintf(b, sizeof b, "play %s · screen %s", p, s2); lv_label_set_text(g_zu_l3, b);
}
static void zu_cap_hold_cb(lv_event_t *e);
static void zusage_create(lv_obj_t *root){
    g_zu = 1;
    lv_obj_set_style_bg_color(root, TC(CANVAS), 0); lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    g_ring = lv_arc_create(root); lv_obj_remove_style(g_ring, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(g_ring, LV_OBJ_FLAG_CLICKABLE); lv_obj_set_size(g_ring, 344, 344); lv_obj_center(g_ring);
    lv_arc_set_rotation(g_ring, 270); lv_arc_set_bg_angles(g_ring, 0, 360); lv_arc_set_range(g_ring, 0, 100);
    lv_obj_set_style_arc_width(g_ring, 3, LV_PART_MAIN); lv_obj_set_style_arc_width(g_ring, 6, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(g_ring, false, LV_PART_MAIN); lv_obj_set_style_arc_rounded(g_ring, true, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(g_ring, TC(CONTROL_TRACK), LV_PART_MAIN);
    /* the Charging Limit mark at 80% on the ring and its label just inside */
    float a = (270.0f + 288.0f) * 3.14159265f / 180.0f;
    g_zu_tick = lv_obj_create(root); lv_obj_remove_style_all(g_zu_tick); lv_obj_set_size(g_zu_tick, 8, 8);
    lv_obj_set_style_radius(g_zu_tick, LV_RADIUS_CIRCLE, 0); lv_obj_set_style_bg_opa(g_zu_tick, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(g_zu_tick, TC(TEXT_PRIMARY), 0);
    lv_obj_set_pos(g_zu_tick, 180 + (int)lroundf(cosf(a) * 170.5f) - 4, 180 + (int)lroundf(sinf(a) * 170.5f) - 4);
    lv_obj_clear_flag(g_zu_tick, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    g_zu_tlab = lv_label_create(root); lv_label_set_text(g_zu_tlab, "limit 80%");
    lv_obj_set_style_text_font(g_zu_tlab, zune_font(&font_zune_regular_16), 0); lv_obj_set_style_text_color(g_zu_tlab, TC(TEXT_MUTED), 0);
    lv_obj_set_pos(g_zu_tlab, 180 + (int)lroundf(cosf(a) * 152), 180 + (int)lroundf(sinf(a) * 152) - 26);
    g_zu_cap = zu_lab(root, &font_zune_regular_16, TC(TEXT_MUTED), 70); lv_label_set_text(g_zu_cap, "BATTERY");
    lv_obj_set_style_text_letter_space(g_zu_cap, 2, 0);
    g_chg_ic = lv_label_create(root); lv_label_set_text(g_chg_ic, LV_SYMBOL_CHARGE);
    lv_obj_set_style_text_font(g_chg_ic, TF(UI_20), 0); lv_obj_set_style_text_color(g_chg_ic, TC(ACCENT_PRIMARY), 0);
    lv_obj_align(g_chg_ic, LV_ALIGN_TOP_MID, 0, 64);
    g_pct = lv_label_create(root); lv_obj_set_style_text_font(g_pct, zune_font(&font_zune_light_96), 0);
    lv_obj_set_style_text_color(g_pct, TC(TEXT_PRIMARY), 0);
    g_zu_l1 = zu_lab(root, &font_zune_regular_17, TC(TEXT_SECONDARY), 184);
    g_zu_l2 = zu_lab(root, &font_zune_regular_16, TC(TEXT_MUTED), 218);
    lv_obj_add_flag(g_zu_l2, LV_OBJ_FLAG_CLICKABLE); lv_obj_set_ext_click_area(g_zu_l2, 12);   /* hold: restart the totals */
    lv_obj_add_event_cb(g_zu_l2, zu_cap_hold_cb, LV_EVENT_LONG_PRESSED, NULL);
    g_zu_l3 = zu_lab(root, &font_zune_regular_16, TC(TEXT_PRIMARY), 244);
    g_zu_lim = zu_lab(root, &font_zune_regular_16, TC(TEXT_MUTED), 276); lv_label_set_text(g_zu_lim, "charge limit 80%");
    /* keep the stock statics non-NULL so shared code paths stay safe */
    g_sub = g_sub2 = g_zu_l1; g_cap = g_zu_l2; g_play_v = g_scr_v = g_zu_l3;
    if(!g_tmr) g_tmr = lv_timer_create(us_tick_cb, 30000, NULL);
    draw();
}
static void zu_cap_hold_cb(lv_event_t *e){ cap_hold_cb(e); }

void usage_create(lv_obj_t *root){
    if(th_zune()){zusage_create(root);return;}
    lv_obj_set_style_bg_color(root,TC(CANVAS),0);lv_obj_set_style_bg_opa(root,LV_OPA_COVER,0);
    lv_obj_clear_flag(root,LV_OBJ_FLAG_SCROLLABLE);
    if(th_braun_family())br_face(root);
    int centered=th_simple()||th_null();
    int left=centered?0:disco_mirror(),dx=centered?0:(left?30:-30);
    g_start=centered?130:(left?220:40);
    g_ring=lv_arc_create(root);lv_obj_remove_style(g_ring,NULL,LV_PART_KNOB);
    lv_obj_clear_flag(g_ring,LV_OBJ_FLAG_CLICKABLE);lv_obj_set_size(g_ring,344,344);lv_obj_center(g_ring);
    lv_arc_set_rotation(g_ring,g_start);lv_arc_set_bg_angles(g_ring,0,280);
    lv_arc_set_mode(g_ring,left?LV_ARC_MODE_REVERSE:LV_ARC_MODE_NORMAL);lv_arc_set_range(g_ring,0,100);
    lv_obj_set_style_arc_width(g_ring,6,LV_PART_MAIN);lv_obj_set_style_arc_width(g_ring,6,LV_PART_INDICATOR);g_left=left;
    lv_obj_set_style_arc_rounded(g_ring,true,LV_PART_MAIN);lv_obj_set_style_arc_rounded(g_ring,true,LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(g_ring,TC(CONTROL_TRACK),LV_PART_MAIN);
    lv_obj_t *title=lv_label_create(root);lv_label_set_text(title,"Battery");
    lv_obj_set_style_text_font(title,TF(UI_24),0);lv_obj_set_style_text_color(title,TC(TEXT_PRIMARY),0);
    lv_obj_align(title,LV_ALIGN_TOP_MID,0,40);
    g_pct=lv_label_create(root);lv_obj_set_style_text_font(g_pct,TF(UI_46),0);
    lv_obj_set_style_text_color(g_pct,TC(TEXT_PRIMARY),0);lv_obj_align(g_pct,LV_ALIGN_TOP_MID,dx,92);
    g_chg_ic=lv_label_create(root);lv_label_set_text(g_chg_ic,LV_SYMBOL_CHARGE);
    lv_obj_set_style_text_font(g_chg_ic,TF(UI_20),0);lv_obj_align(g_chg_ic,LV_ALIGN_TOP_MID,(left?-78:78),109);
    g_sub=lv_label_create(root);lv_label_set_long_mode(g_sub,LV_LABEL_LONG_SCROLL_CIRCULAR);lv_obj_set_width(g_sub,230);
    lv_obj_set_style_text_align(g_sub,LV_TEXT_ALIGN_CENTER,0);lv_obj_set_style_text_font(g_sub,TF(UI_22),0);
    lv_obj_set_style_text_color(g_sub,TC(TEXT_PRIMARY),0);lv_obj_align(g_sub,LV_ALIGN_TOP_MID,dx,158);
    g_sub2=lv_label_create(root);lv_label_set_long_mode(g_sub2,LV_LABEL_LONG_SCROLL_CIRCULAR);lv_obj_set_width(g_sub2,230);
    lv_obj_set_style_text_align(g_sub2,LV_TEXT_ALIGN_CENTER,0);lv_obj_set_style_text_font(g_sub2,TF(UI_14),0);
    lv_obj_set_style_text_color(g_sub2,TC(TEXT_SECONDARY),0);lv_obj_align(g_sub2,LV_ALIGN_TOP_MID,dx,188);
    lv_obj_t *rule=lv_obj_create(root);lv_obj_remove_style_all(rule);lv_obj_set_size(rule,140,1);   /* a hairline above the totals */
    lv_obj_set_style_bg_color(rule,TC(TEXT_SECONDARY),0);lv_obj_set_style_bg_opa(rule,LV_OPA_30,0);lv_obj_align(rule,LV_ALIGN_TOP_MID,dx,216);
    g_cap=lv_label_create(root);lv_label_set_text(g_cap,"Last 24 hours");lv_obj_set_width(g_cap,th_null()?260:240);
    if(th_null())lv_label_set_long_mode(g_cap,LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(g_cap,LV_TEXT_ALIGN_CENTER,0);lv_obj_set_style_text_font(g_cap,TF(UI_14),0);
    lv_obj_set_style_text_color(g_cap,TC(TEXT_MUTED),0);lv_obj_align(g_cap,LV_ALIGN_TOP_MID,dx,222);
    lv_obj_add_flag(g_cap,LV_OBJ_FLAG_CLICKABLE);lv_obj_set_ext_click_area(g_cap,14);   /* hold: restart the totals */
    lv_obj_add_event_cb(g_cap,cap_hold_cb,LV_EVENT_LONG_PRESSED,NULL);
    figure(root,dx-56,"playing",&g_play_v);figure(root,dx+56,"screen on",&g_scr_v);
    for(int k=0;k<=4;k++){                                   /* ticks at 0 / 25 / 50 / 75 / 100% just inside the ring */
        float deg=left?g_start+280.0f-70.0f*k:g_start+70.0f*k,a=deg*3.14159265f/180.0f;
        lv_obj_t *t=lv_obj_create(root);lv_obj_remove_style_all(t);lv_obj_set_size(t,4,4);
        lv_obj_set_style_radius(t,LV_RADIUS_CIRCLE,0);lv_obj_set_style_bg_color(t,TC(TEXT_SECONDARY),0);
        lv_obj_set_style_bg_opa(t,k==0||k==4?LV_OPA_70:LV_OPA_40,0);
        lv_obj_set_pos(t,180+(int)lroundf(cosf(a)*156)-2,180+(int)lroundf(sinf(a)*156)-2);
        lv_obj_clear_flag(t,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
    }
    g_tip=lv_obj_create(root);lv_obj_remove_style_all(g_tip);lv_obj_set_size(g_tip,14,14);   /* the glowing tip at the level */
    lv_obj_set_style_radius(g_tip,LV_RADIUS_CIRCLE,0);lv_obj_set_style_bg_opa(g_tip,LV_OPA_COVER,0);
    lv_obj_set_style_border_width(g_tip,3,0);lv_obj_set_style_border_color(g_tip,TC(CANVAS),0);
    lv_obj_set_style_shadow_width(g_tip,16,0);lv_obj_set_style_shadow_opa(g_tip,LV_OPA_70,0);
    lv_obj_clear_flag(g_tip,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
    if(!g_tmr)g_tmr=lv_timer_create(us_tick_cb,30000,NULL);draw();
}
void usage_refresh(void){ draw(); }
/* host renders only: six hours of made-up use (never called on the Disc) */
void usage_demo(int charging){
    uint32_t now = (uint32_t)time(NULL);
    for(int m = 360; m > 0; m--){
        uint8_t fl = (m % 90 < 50 ? FL_PLAY : 0) | (m % 120 < 12 ? FL_SCR : 0);
        push(now - (uint32_t)m * 60u, 82 - (360 - m) / 24, fl);
    }
    if(charging) for(int m = 45; m > 0; m--) push(now - (uint32_t)m * 60u, 52 + (45 - m) / 3, FL_CHG);   /* charging for 45 min */
    g_batt = 67; g_chg = charging;
    if(getenv("USAGE_CYCLE")){ g_cy.full_at = now - 26u * 3600u; g_cy.play_min = 412; g_cy.scr_min = 75; }   /* since a full charge, 26 h ago */
    draw();
}
