/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Home in the Zune theme: the main menu as a vertical dial. One item sits in focus at the screen's middle, large and
 * white (Light 46); its neighbours shrink (32, then 24) and fade with distance. Every row hugs the round edge: its
 * left inset follows the bezel curve, so the column bends with the glass. "now playing" is the accent item, with the
 * track under it. A short accent tick on the left rim marks the focus line. The time and battery sit centred at the
 * top. Scroll to move the dial, tap any item to open it. home.c forwards its setters here when th_zune(). */
#include "screens.h"
#include "simple_theme.h"
#include "theme.h"
#include "theme_kit.h"
#include "zune_dial.h"
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

void ui_np_status_changed(void);   /* ui.c: Now Playing repeats this status line */
LV_FONT_DECLARE(font_zune_semibold_24)
LV_FONT_DECLARE(font_zune_bold_14)
LV_FONT_DECLARE(font_zune_regular_16)
#define ZH_PITCH 64          /* row pitch */
#define ZH_FOCUS 180         /* the focus line (screen y) */
#define ZH_INSET 34          /* text inset from the bezel at the focus line */

typedef struct { const char *label; const char *action; void (*open)(void); } zitem_t;

static void go_np(void){ screen_show(SCR_NOWPLAYING); }
static void go_music(void){ screen_show(SCR_LIBRARY); }
static void go_search(void){ screen_show(SCR_SEARCH); }
static void go_queue(void){ queue_open(); }
static void go_eq(void){ screen_show(SCR_EQ); }
static void go_modes(void){ modes_open(); }
static void go_ma(void){ screen_show(SCR_MA); }          /* MA Sendspin, as in the other themes' navigation */
static void go_bt(void){ bt_open(); }
static void go_wifi(void){ wifi_open(); }
static void go_weather(void){ weather_app_open(); }
static void go_apps(void){ apps_reload(); screen_show(SCR_APPS); }
static void go_settings(void){ screen_show(SCR_SETTINGS); }

static const zitem_t ITEMS[] = {
    { "now playing", "home.zune.nowplaying", go_np },
    { "music",       "home.zune.music",      go_music },
    { "queue",       "home.zune.queue",      go_queue },
    { "search",      "home.zune.search",     go_search },
    { "equalizer",   "home.zune.eq",         go_eq },
    { "modes",       "home.zune.modes",      go_modes },
    { "sendspin",    "home.zune.sendspin",   go_ma },
    { "bluetooth",   "home.zune.bt",         go_bt },
    { "wi-fi",       "home.zune.wifi",       go_wifi },
    { "weather",     "home.zune.weather",    go_weather },
    { "apps",        "home.zune.apps",       go_apps },
    { "settings",    "home.zune.settings",   go_settings },
};
#define ZN ((int)(sizeof ITEMS / sizeof ITEMS[0]))
#define NP_IDX 0
#define WX_IDX 9

/* the dial loops: three copies of the list, and when a scroll settles outside the middle copy it jumps by one copy
 * (an identical picture), so it never runs out either way */
#define ZC 3
#define ZP (ZN * ZC)
static lv_obj_t *g_dial, *g_row[ZP], *g_lbl[ZP], *g_sub[ZP], *g_status, *g_tick;
static int g_size[ZP];                  /* the font step each label is on now (0 = 46, 1 = 32, 2 = 24), -1 unset */
static char g_time[24],g_batt[16],g_np_caption[192],g_weather_caption[192];
static const lv_font_t *small_font(void){return zune_font(&font_zune_bold_14);}
static const lv_font_t *np_sub_font(void){return zune_font(&font_zune_regular_16);}
static int g_batt_pct = -1, g_batt_chg;
int zhome_battery_pct(void){ return g_batt_pct; }   /* the lock screen shows it as "82%" */


/* scroll-time setters that do nothing when the value is already set: every LVGL style write re-resolves the object's
 * styles and repaints it, and these run for every row on every scroll step */
static inline void zg_hide(lv_obj_t *o, int h){ if(o && lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN) != (h != 0)){ if(h) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN); else lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN); } }
static inline void zg_opa(lv_obj_t *o, lv_opa_t v){ v = v >= 248 ? 255 : (lv_opa_t)(v & 0xF8); if(o && lv_obj_get_style_text_opa(o, 0) != v) lv_obj_set_style_text_opa(o, v, 0); }
static inline void zg_align(lv_obj_t *o, int x, int y){
    if(!o) return;
    if(lv_obj_get_style_align(o, 0) == LV_ALIGN_LEFT_MID && lv_obj_get_style_x(o, 0) == x && lv_obj_get_style_y(o, 0) == y) return;
    lv_obj_align(o, LV_ALIGN_LEFT_MID, x, y);
}
static const lv_font_t *step_font(int s){ return s == 0 ? TF(UI_46) : s == 1 ? TF(UI_32) : zune_font(&font_zune_semibold_24); }

/* the bezel's left edge at screen y, plus the inset */
static int edge_x(int y){return zune_curve_inset(y-180,1)+ZH_INSET;}

static void dial_layout(void){
    if(!g_dial) return;
    lv_area_t da; lv_obj_get_coords(g_dial, &da);
    for(int i = 0; i < ZP; i++){
        lv_area_t a; lv_obj_get_coords(g_row[i], &a);
        int yc = (a.y1 + a.y2) / 2 - da.y1;                        /* row centre on screen */
        int d = abs(yc - ZH_FOCUS);
        if(d > 200 || yc < 38){ zg_hide(g_lbl[i], 1); zg_hide(g_sub[i], 1); continue; }   /* clear of the status line: the loop puts rows up there now */
        zg_hide(g_lbl[i], 0);
        int s = d < ZH_PITCH / 2 ? 0 : d < ZH_PITCH * 3 / 2 ? 1 : 2;
        if(s != g_size[i]){ g_size[i] = s; lv_obj_set_style_text_font(g_lbl[i], step_font(s), 0); }
        float o = 1.0f - (float)d / ZH_PITCH * 0.4f; if(o < 0.45f)o=0.45f;
        lv_opa_t opa = (lv_opa_t)(o * 255);
        zg_opa(g_lbl[i], opa);
        int x = edge_x(yc);
        int has_sub = g_sub[i] && lv_label_get_text(g_sub[i])[0];
        zg_align(g_lbl[i], x, has_sub ? (i%ZN==NP_IDX?-12:-8) : 0);
        if(g_sub[i]){
            if(has_sub){
                zg_hide(g_sub[i], 0);
                int np=i%ZN==NP_IDX;
                int w = (np?342:300) - x, h = lv_font_get_line_height(np?np_sub_font():small_font())+(np?4:0);      /* one line, then "..." */
                if(lv_obj_get_style_width(g_sub[i], 0) != w || lv_obj_get_style_height(g_sub[i], 0) != h) lv_obj_set_size(g_sub[i], w, h);
                lv_obj_align_to(g_sub[i], g_lbl[i], LV_ALIGN_OUT_BOTTOM_LEFT, 2, i%ZN==NP_IDX?0:-2);
                zg_opa(g_sub[i], opa);
            } else zg_hide(g_sub[i], 1);
        }
    }
}
static void scroll_cb(lv_event_t *e){ (void)e; dial_layout(); }
static int g_recentring;
static void scroll_end_cb(lv_event_t *e){                 /* settled in an outer copy: hop to the same row in the middle one */
    (void)e; if(!g_dial || g_recentring) return;
    int y = lv_obj_get_scroll_y(g_dial), f = (y + ZH_PITCH / 2) / ZH_PITCH;
    if(f >= ZN && f < 2 * ZN) return;
    g_recentring = 1;
    lv_obj_scroll_to_y(g_dial, y + (f < ZN ? ZN : -ZN) * ZH_PITCH, LV_ANIM_OFF);
    g_recentring = 0;
    dial_layout();
}
static void item_hold(lv_event_t *e){(void)e;int a=simple_cfg_get_int("zune_nav_hold",DISCO_NAV_MUSIC);if(a==DISCO_NAV_MUSIC)screen_show(SCR_NOWPLAYING);else disco_nav_action_run(a);}
static void item_cb(lv_event_t *e){
    if(screen_press_was_long())return;
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if(i >= 0 && i < ZN && ITEMS[i].open) ITEMS[i].open();
}

static void status_paint(void){ zune_status_refresh(); }

void zhome_create(lv_obj_t *root){
    lv_obj_set_style_bg_color(root, TC(CANVAS), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);

    g_dial = lv_obj_create(root);
    lv_obj_remove_style_all(g_dial); kit_keep(g_dial);
    lv_obj_set_size(g_dial, 360, 360); lv_obj_set_pos(g_dial, 0, 0);
    lv_obj_set_flex_flow(g_dial, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_top(g_dial, ZH_FOCUS - ZH_PITCH / 2, 0);
    lv_obj_set_style_pad_bottom(g_dial, 360 - ZH_FOCUS - ZH_PITCH / 2, 0);
    lv_obj_set_scroll_dir(g_dial, LV_DIR_VER);
    lv_obj_set_scroll_snap_y(g_dial, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_scrollbar_mode(g_dial, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(g_dial, LV_OBJ_FLAG_SCROLL_MOMENTUM);          /* free-flowing: a flick runs on and settles on a row */
    lv_obj_remove_flag(g_dial, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_add_event_cb(g_dial, scroll_cb, LV_EVENT_SCROLL, NULL);
    lv_obj_add_event_cb(g_dial, scroll_end_cb, LV_EVENT_SCROLL_END, NULL);

    for(int p = 0; p < ZP; p++){
        int i = p % ZN, c = p / ZN;
        lv_obj_t *r = lv_obj_create(g_dial);
        lv_obj_remove_style_all(r); kit_keep(r);
        lv_obj_set_size(r, 360, ZH_PITCH);
        lv_obj_remove_flag(r, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(r, LV_OBJ_FLAG_SNAPPABLE);
        if(c == 1) ui_on(r, item_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i, ITEMS[i].action, UI_CORE);   /* the middle copy carries the names */
        else lv_obj_add_event_cb(r, item_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_add_event_cb(r,item_hold,LV_EVENT_LONG_PRESSED,NULL);
        g_row[p] = r;
        lv_obj_t *l = lv_label_create(r); kit_keep(l);
        lv_label_set_text(l, ITEMS[i].label);
        lv_obj_set_style_text_color(l, i == NP_IDX ? TC(ACCENT_PRIMARY) : TC(TEXT_PRIMARY), 0);
        lv_obj_set_style_text_font(l, TF(UI_32), 0);
        lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
        g_lbl[p] = l; g_size[p] = -1;
        g_sub[p] = NULL;
        if(i == NP_IDX || i == WX_IDX){
            lv_obj_t *s = lv_label_create(i==NP_IDX?root:r); kit_keep(s); /* caption draws above fixed-pitch rows without being clipped by them */
            lv_label_set_text(s, "");
            lv_obj_set_style_text_font(s,i==NP_IDX?np_sub_font():small_font(),0);
            if(i==NP_IDX){lv_obj_set_style_pad_ver(s,2,0);}
            lv_obj_set_style_text_color(s,TC(TEXT_SECONDARY),0);
            lv_label_set_long_mode(s, LV_LABEL_LONG_DOT);
            lv_obj_remove_flag(s, LV_OBJ_FLAG_CLICKABLE);
            g_sub[p] = s;
        }
    }

    /* the focus tick on the left rim */
    g_tick = lv_arc_create(root); kit_keep(g_tick);
    lv_obj_remove_style(g_tick, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(g_tick, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(g_tick, 352, 352); lv_obj_center(g_tick);
    lv_arc_set_bg_angles(g_tick, 174, 186); lv_arc_set_value(g_tick, 0);
    lv_obj_set_style_arc_width(g_tick, 4, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(g_tick, true, LV_PART_MAIN);
    lv_obj_set_style_arc_color(g_tick, TC(ACCENT_PRIMARY), LV_PART_MAIN);
    lv_obj_set_style_arc_width(g_tick, 0, LV_PART_INDICATOR);

    /* time + battery, centred at the top */
    g_status = zune_status(root, SCR_HOME);                  /* time + battery (and a sleep timer), shared with every Zune screen */

    lv_obj_update_layout(g_dial);
    lv_obj_scroll_to_y(g_dial, ZH_PITCH * (ZN + 1), LV_ANIM_OFF);   /* "music" in focus, in the middle copy */
    dial_layout();
}

void zhome_set_clock(const char *t, const char *sub){
    (void)sub;
    if(!strcmp(g_time,t?t:""))return;
    snprintf(g_time,sizeof g_time,"%s",t?t:"");status_paint();
}
LV_FONT_DECLARE(font_weather16)
static void zhome_set_weather_one(lv_obj_t *sub, const char *s);
void zhome_set_weather(const char *s){
    char text[sizeof g_weather_caption];snprintf(text,sizeof text,"%s",s?s:"");
    if(!strcmp(g_weather_caption,text))return;snprintf(g_weather_caption,sizeof g_weather_caption,"%s",text);
    for(int c=0;c<ZC;c++)zhome_set_weather_one(g_sub[c*ZN+WX_IDX],text);
    dial_layout();
}
static void zhome_set_weather_one(lv_obj_t *sub, const char *s){
    if(!sub) return;
    {   /* the weather line leads with a FontAwesome weather glyph: give its font the weather icons as a fallback
         * (text font -> weather icons -> the text font's own CJK chain) */
        static lv_font_t txt, wx; static const lv_font_t *made_for;
        const lv_font_t *base=small_font();
        if(made_for != base){ made_for = base; wx = font_weather16; wx.fallback = base->fallback; txt = *base; txt.fallback = &wx; }
        lv_obj_set_style_text_font(sub, &txt, 0);
        lv_obj_add_flag(sub, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    }
    while(s && *s == ' ') s++;
    lv_label_set_text(sub, s ? s : "");
}
void zhome_set_status(int batt, int charging, int wifi, int bt){
    (void)wifi; (void)bt;
    if(g_batt_pct==batt&&g_batt_chg==charging)return;
    g_batt_pct=batt;g_batt_chg=charging;
    const char *g = charging ? LV_SYMBOL_CHARGE : batt >= 90 ? LV_SYMBOL_BATTERY_FULL : batt >= 65 ? LV_SYMBOL_BATTERY_3 :
                    batt >= 40 ? LV_SYMBOL_BATTERY_2 : batt >= 15 ? LV_SYMBOL_BATTERY_1 : LV_SYMBOL_BATTERY_EMPTY;
    snprintf(g_batt, sizeof g_batt, "%s", batt >= 0 ? g : "");
    status_paint();
}
void zhome_set_now_playing(const char *title, const char *artist, bool playing){
    (void)playing;
    if(!g_sub[NP_IDX]) return;
    char b[192];
    if(title && artist && artist[0]) snprintf(b, sizeof b, "%s \xC2\xB7 %s", title, artist);
    else snprintf(b, sizeof b, "%s", title ? title : "");
    if(strcmp(g_np_caption,b)){
        snprintf(g_np_caption,sizeof g_np_caption,"%s",b);
        for(int c = 0; c < ZC; c++) if(g_sub[c * ZN + NP_IDX]) lv_label_set_text(g_sub[c * ZN + NP_IDX], b);
        dial_layout();
    }
}

/* Now Playing shows the same status line (ui.c) */
/* the status line's parts: the time text, the battery level (-1 unknown) and whether it is charging */
void zhome_status_parts(char *t, size_t n, int *pct, int *chg){ snprintf(t, n, "%s", g_time); *pct = g_batt_pct; *chg = g_batt_chg; }
void zhome_status_text(char *b, size_t n){ snprintf(b, n, "%s%s%s", g_time, g_time[0] && g_batt[0] ? "  " : "", g_batt); }
lv_obj_t *zhome_test_subtext(void){return g_sub[ZN+NP_IDX];}
void zhome_focus_now_playing(void){if(g_dial){lv_obj_scroll_to_y(g_dial,ZH_PITCH*ZN,LV_ANIM_OFF);dial_layout();}}
void zhome_test_focus(int i){ if(g_dial){ lv_obj_scroll_to_y(g_dial, ZH_PITCH * (ZN + i), LV_ANIM_OFF); dial_layout(); } }   /* host renders */
