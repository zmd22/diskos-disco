/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Classic: a monochrome-LCD list theme in ChicagoFLF. Title bar (play state or back, title, battery) over one rule;
 * menus are a fixed column whose pressed row inverts to ink with backlight text. home.c forwards here when th_classic(). */
#include "classic.h"
#include "screens.h"
#include "ipc.h"
#include "theme.h"
#include "theme_kit.h"
#include "config.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define CL_TITLE_Y 52          /* title bar centre line */
#define CL_RULE_Y 70           /* the rule under the title bar */
#define CL_ROW_H CLASSIC_ROW_H
#define CL_LIST_Y CLASSIC_LIST_Y
#define CL_COL_X 50            /* the menu column, fixed for every row */
#define CL_COL_W 260

/* ---- battery glyph: outline, nub and a fill sized to the level; every one on screen follows the last status ---- */
#define CL_BATT_MAX 64
static lv_obj_t *g_batt[CL_BATT_MAX];
static int g_batt_pct = -1, g_batt_chg;
static void batt_paint(lv_obj_t *b){
    lv_obj_t *fill = lv_obj_get_child(b, 1);
    if(!fill) return;
    int pct = g_batt_pct < 0 ? 100 : g_batt_pct > 100 ? 100 : g_batt_pct;
    lv_obj_set_width(fill, pct * 20 / 100 > 1 ? pct * 20 / 100 : 2);
    lv_obj_set_style_bg_opa(fill, g_batt_chg ? LV_OPA_50 : LV_OPA_COVER, 0);   /* charging: a lighter fill */
}
static void batt_deleted(lv_event_t *e){
    lv_obj_t *b = lv_event_get_target(e);
    for(int i = 0; i < CL_BATT_MAX; i++) if(g_batt[i] == b) g_batt[i] = NULL;
}
static lv_obj_t *plain(lv_obj_t *parent){
    lv_obj_t *o = lv_obj_create(parent); lv_obj_remove_style_all(o); kit_keep(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return o;
}
lv_obj_t *classic_battery(lv_obj_t *parent, int x, int y){
    lv_obj_t *b = plain(parent); lv_obj_set_pos(b, x, y); lv_obj_set_size(b, 30, 14);
    lv_obj_t *body = plain(b); lv_obj_set_size(body, 26, 14);
    lv_obj_set_style_border_width(body, 2, 0); lv_obj_set_style_border_color(body, TC(TEXT_PRIMARY), 0);
    lv_obj_set_style_radius(body, 2, 0);
    lv_obj_t *fill = plain(b); lv_obj_set_pos(fill, 3, 3); lv_obj_set_size(fill, 20, 8);
    lv_obj_set_style_bg_color(fill, TC(TEXT_PRIMARY), 0);
    lv_obj_t *nub = plain(b); lv_obj_set_pos(nub, 26, 4); lv_obj_set_size(nub, 3, 6);
    lv_obj_set_style_bg_opa(nub, LV_OPA_COVER, 0); lv_obj_set_style_bg_color(nub, TC(TEXT_PRIMARY), 0);
    for(int i = 0; i < CL_BATT_MAX; i++) if(!g_batt[i]){ g_batt[i] = b; lv_obj_add_event_cb(b, batt_deleted, LV_EVENT_DELETE, NULL); break; }
    batt_paint(b);
    return b;
}

/* ---- title bar ---- */
static lv_obj_t *rule(lv_obj_t *parent, int y){
    int hw = (int)sqrtf(180.0f * 180 - (float)(y - 180) * (y - 180)) - 22;
    lv_obj_t *r = plain(parent); lv_obj_set_pos(r, 180 - hw, y); lv_obj_set_size(r, 2 * hw, 2);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0); lv_obj_set_style_bg_color(r, TC(TEXT_PRIMARY), 0);
    return r;
}
static lv_obj_t *title_label(lv_obj_t *parent, const char *text){
    lv_obj_t *t = lv_label_create(parent);
    lv_obj_set_user_data(t, (void *)&theme_title_tag);
    lv_obj_set_style_text_font(t, TF(HEADER), 0); lv_obj_set_style_text_color(t, TC(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_size(t, 150, lv_font_get_line_height(TF(HEADER)));
    lv_obj_set_pos(t, 105, CL_TITLE_Y - lv_font_get_line_height(TF(HEADER)) / 2);
    lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
    theme_title_text(t, text);   /* after the box is fixed: dots are picked against the final width */
    return t;
}
static void back_home_cb(lv_event_t *e){ if(lv_event_get_code(e) == LV_EVENT_CLICKED && screen_press_was_long()) screen_home(); }
static void back_cb_default(lv_event_t *e){ if(lv_event_get_code(e) == LV_EVENT_CLICKED) screen_back(); }
/* kit header: back chevron, title, battery, rule. The back button stays the title's previous sibling. */
lv_obj_t *classic_header(lv_obj_t *root, const char *title, lv_event_cb_t back_cb){
    lv_obj_t *back = lv_button_create(root);
    lv_obj_remove_style_all(back); kit_keep(back);
    lv_obj_set_pos(back, 56, CL_TITLE_Y - 20); lv_obj_set_size(back, 44, 40);
    lv_obj_set_ext_click_area(back, 10);
    lv_obj_set_style_radius(back, 4, 0);
    lv_obj_set_style_bg_color(back, TC(TEXT_PRIMARY), LV_STATE_PRESSED); lv_obj_set_style_bg_opa(back, LV_OPA_COVER, LV_STATE_PRESSED);
    ui_on(back, back_cb ? back_cb : back_cb_default, LV_EVENT_CLICKED, NULL, "nav.back", UI_CORE);
    lv_obj_add_event_cb(back, back_home_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *ic = lv_label_create(back); lv_label_set_text(ic, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_font(ic, TF(UI_16), 0); lv_obj_set_style_text_color(ic, TC(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_color(ic, TC(CANVAS), LV_STATE_PRESSED);
    lv_obj_add_flag(ic, LV_OBJ_FLAG_EVENT_BUBBLE); lv_obj_center(ic);
    lv_obj_t *t = title_label(root, title);
    classic_battery(root, 258, CL_TITLE_Y - 7);
    rule(root, CL_RULE_Y);
    return t;
}

/* ---- styling pass: flat rows that invert while pressed (labels follow through their own PRESSED state) ---- */
static void label_states(lv_obj_t *o, int on){
    for(uint32_t i = 0; i < lv_obj_get_child_count(o); i++){
        lv_obj_t *ch = lv_obj_get_child(o, (int32_t)i);
        if(lv_obj_check_type(ch, &lv_label_class)){
            lv_obj_set_style_text_color(ch, TC(CANVAS), LV_STATE_PRESSED);
            if(on) lv_obj_add_state(ch, LV_STATE_PRESSED); else lv_obj_remove_state(ch, LV_STATE_PRESSED);
        } else label_states(ch, on);
    }
}
/* a scroll takes PRESSED off the row without an event: watch the inverted row and give its labels back */
static lv_obj_t *g_inv;
static lv_timer_t *g_inv_t;
static void inv_watch(lv_timer_t *t){
    if(g_inv && lv_obj_has_state(g_inv, LV_STATE_PRESSED)) return;
    if(g_inv) label_states(g_inv, 0);
    g_inv = NULL; lv_timer_pause(t);
}
static void invert_cb(lv_event_t *e){
    lv_event_code_t c = lv_event_get_code(e);
    lv_obj_t *o = lv_event_get_current_target(e);
    if(c == LV_EVENT_DELETE){ if(g_inv == o) g_inv = NULL; return; }
    label_states(o, c == LV_EVENT_PRESSED);
    if(c == LV_EVENT_PRESSED){
        if(g_inv && g_inv != o) label_states(g_inv, 0);
        g_inv = o;
        if(!g_inv_t) g_inv_t = lv_timer_create(inv_watch, 40, NULL); else lv_timer_resume(g_inv_t);
    } else if(g_inv == o) g_inv = NULL;
}
static void invert_on_press(lv_obj_t *o){
    lv_obj_set_style_bg_color(o, TC(TEXT_PRIMARY), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_add_event_cb(o, invert_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(o, invert_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(o, invert_cb, LV_EVENT_PRESS_LOST, NULL);
    lv_obj_add_event_cb(o, invert_cb, LV_EVENT_DELETE, NULL);
}
/* a shared list's row in this theme: straight, frameless, inverted while pressed (rows are reused: styled once) */
void classic_row_style(lv_obj_t *r){
    for(uint32_t i = 0; i < lv_obj_get_event_count(r); i++)
        if(lv_event_dsc_get_cb(lv_obj_get_event_dsc(r, i)) == invert_cb) return;
    lv_obj_set_style_bg_opa(r, LV_OPA_TRANSP, 0); lv_obj_set_style_border_width(r, 0, 0);
    lv_obj_set_style_shadow_width(r, 0, 0); lv_obj_set_style_radius(r, 4, 0);
    invert_on_press(r);
}
void classic_style(lv_obj_t *o, kit_role_t role){
    if(role == KIT_TITLE){ lv_obj_set_style_border_width(o, 0, 0); lv_obj_set_style_outline_width(o, 0, 0); return; }
    if(role == KIT_ROW){
        lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0); lv_obj_set_style_shadow_width(o, 0, 0);
        lv_obj_set_style_border_width(o, 0, 0); lv_obj_set_style_radius(o, 4, 0);
        invert_on_press(o); return;
    }
    if(role == KIT_ROW_CHEVRON){ lv_obj_set_style_text_color(o, TC(TEXT_PRIMARY), 0); return; }
    if(!lv_obj_has_flag(o, LV_OBJ_FLAG_CLICKABLE)) return;
    lv_obj_set_style_shadow_width(o, 0, 0); lv_obj_set_style_outline_width(o, 0, LV_STATE_ANY);
    if(role == KIT_BUTTON && lv_obj_get_style_bg_opa(o, 0) >= LV_OPA_50 && !lv_color_eq(lv_obj_get_style_bg_color(o, 0), TC(CANVAS))){
        /* a filled action (any colour) becomes ink with backlight text: one LCD, two tones */
        lv_obj_set_style_border_width(o, 0, 0); lv_obj_set_style_bg_color(o, TC(TEXT_PRIMARY), 0);
        for(uint32_t i = 0; i < lv_obj_get_child_count(o); i++){
            lv_obj_t *ch = lv_obj_get_child(o, (int32_t)i);
            if(lv_obj_check_type(ch, &lv_label_class)) lv_obj_set_style_text_color(ch, TC(CANVAS), 0);
        }
        return;
    }
    if(role == KIT_BUTTON && lv_obj_get_width(o) >= 140){   /* a wide tap target reads as a row: invert, no frame */
        lv_obj_set_style_border_width(o, 0, 0); lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0); lv_obj_set_style_radius(o, 4, 0);
        invert_on_press(o); return;
    }
    if(role == KIT_BUTTON){
        lv_obj_set_style_border_width(o, 2, 0); lv_obj_set_style_border_color(o, TC(TEXT_PRIMARY), 0);
        lv_obj_set_style_border_opa(o, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
        if(lv_obj_get_style_radius(o, 0) > 8 && lv_obj_get_height(o) < 60) lv_obj_set_style_radius(o, 6, 0);
        invert_on_press(o);
    }
}

/* ---- Home: the main menu ---- */
typedef struct { const char *label; const char *action; void (*open)(void); int arrow; } citem_t;
static void go_np(void){ screen_show(SCR_NOWPLAYING); }
static void go_music(void){ screen_show(SCR_LIBRARY); }
static void go_shuffle(void){ disco_nav_action_run(DISCO_NAV_SHUFFLE); }
static void go_search(void){ screen_show(SCR_SEARCH); }
static void go_queue(void){ queue_open(); }
static void go_eq(void){ screen_show(SCR_EQ); }
static void go_modes(void){ modes_open(); }
static void go_bt(void){ bt_open(); }
static void go_wifi(void){ wifi_open(); }
static void go_apps(void){ apps_reload(); screen_show(SCR_APPS); }
static void go_settings(void){ screen_show(SCR_SETTINGS); }
static const citem_t ITEMS[] = {
    { "Music",         "home.classic.music",      go_music,    1 },
    { "Now Playing",   "home.classic.nowplaying", go_np,       1 },
    { "Shuffle Songs", "home.classic.shuffle",    go_shuffle,  0 },
    { "Search",        "home.classic.search",     go_search,   1 },
    { "Queue",         "home.classic.queue",      go_queue,    1 },
    { "Equalizer",     "home.classic.eq",         go_eq,       1 },
    { "Modes",         "home.classic.modes",      go_modes,    1 },
    { "Apps",          "home.classic.apps",       go_apps,     1 },
    { "Bluetooth",     "home.classic.bt",         go_bt,       1 },
    { "Wi-Fi",         "home.classic.wifi",       go_wifi,     1 },
    { "Settings",      "home.classic.settings",   go_settings, 1 },
};
#define CN ((int)(sizeof ITEMS / sizeof ITEMS[0]))
static lv_obj_t *g_home_state;   /* play / pause glyph at the left of Home's title bar */
static lv_obj_t *g_home_clock;   /* Home's title: the time */

static void item_cb(lv_event_t *e){
    if(screen_press_was_long()) return;
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if(i >= 0 && i < CN) ITEMS[i].open();
}
lv_obj_t *classic_row(lv_obj_t *list, const char *label, int arrow){
    lv_obj_t *r = lv_obj_create(list); lv_obj_remove_style_all(r); kit_keep(r);
    lv_obj_set_size(r, CL_COL_W, CL_ROW_H);
    lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE); lv_obj_remove_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(r, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_set_style_radius(r, 4, 0);
    invert_on_press(r);
    lv_obj_t *l = lv_label_create(r); lv_label_set_text(l, label);
    lv_obj_set_style_text_font(l, TF(UI_20), 0); lv_obj_set_style_text_color(l, TC(TEXT_PRIMARY), 0);
    lv_obj_set_size(l, CL_COL_W - 44, lv_font_get_line_height(TF(UI_20))); lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);   /* one line */
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 10, 0);
    if(arrow){
        lv_obj_t *c = lv_label_create(r); lv_label_set_text(c, LV_SYMBOL_RIGHT);
        lv_obj_set_style_text_font(c, TF(UI_14), 0); lv_obj_set_style_text_color(c, TC(TEXT_PRIMARY), 0);
        lv_obj_align(c, LV_ALIGN_RIGHT_MID, -10, 0);
    }
    return r;
}
lv_obj_t *classic_list(lv_obj_t *root, int y, int h){
    lv_obj_t *list = lv_obj_create(root); lv_obj_remove_style_all(list); kit_keep(list);
    lv_obj_set_pos(list, CL_COL_X, y); lv_obj_set_size(list, CL_COL_W, h);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(list, LV_DIR_VER); lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_bottom(list, CL_ROW_H, 0);
    return list;
}
static void state_paint(int playing){
    if(g_home_state) lv_label_set_text(g_home_state, playing ? LV_SYMBOL_PLAY : LV_SYMBOL_PAUSE);
}
void chome_create(lv_obj_t *root){
    lv_obj_set_style_bg_color(root, TC(CANVAS), 0); lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    g_home_state = lv_label_create(root); kit_keep(g_home_state);
    lv_obj_set_style_text_font(g_home_state, TF(UI_14), 0); lv_obj_set_style_text_color(g_home_state, TC(TEXT_PRIMARY), 0);
    lv_obj_set_pos(g_home_state, 72, CL_TITLE_Y - 8);
    state_paint(ui_is_playing());
    g_home_clock = title_label(root, "Graphite");
    classic_battery(root, 258, CL_TITLE_Y - 7);
    rule(root, CL_RULE_Y);
    lv_obj_t *list = classic_list(root, CL_LIST_Y, CLASSIC_LIST_H);   /* whole rows only */
    for(int i = 0; i < CN; i++){
        lv_obj_t *r = classic_row(list, ITEMS[i].label, ITEMS[i].arrow);
        ui_on(r, item_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i, ITEMS[i].action, UI_CORE);
    }
}
void chome_set_clock(const char *t){ if(g_home_clock && t) lv_label_set_text(g_home_clock, t); }
void chome_set_status(int batt, int charging){
    g_batt_pct = batt; g_batt_chg = charging;
    for(int i = 0; i < CL_BATT_MAX; i++) if(g_batt[i]) batt_paint(g_batt[i]);
}
void chome_set_now_playing(const char *title, const char *artist, int playing){
    (void)title; (void)artist; state_paint(playing);
}

/* ---- Bluetooth / Wi-Fi: the theme's header, a power row (scan, switch), the status line and a menu list ---- */
void classic_radio_skin(lv_obj_t *root, const char *title, lv_obj_t *ring, lv_obj_t *glyph, lv_obj_t *name, lv_obj_t *sub,
                        lv_obj_t *sw, lv_obj_t *scan, lv_obj_t *list){
    lv_obj_set_style_bg_color(root, TC(CANVAS), 0); lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_add_flag(ring, LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(glyph, LV_OBJ_FLAG_HIDDEN);
    classic_header(root, title, NULL);
    lv_obj_t *pl = lv_label_create(root); kit_keep(pl); lv_label_set_text(pl, "Power");
    lv_obj_set_style_text_font(pl, TF(UI_20), 0); lv_obj_set_style_text_color(pl, TC(TEXT_PRIMARY), 0);
    lv_obj_set_pos(pl, 60, 84);
    lv_obj_set_align(scan, LV_ALIGN_DEFAULT); lv_obj_set_size(scan, 70, 28); lv_obj_set_pos(scan, 166, 82); lv_obj_set_ext_click_area(scan, 8);
    lv_obj_set_style_radius(scan, 14, 0); lv_obj_set_style_bg_opa(scan, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(scan, 2, 0); lv_obj_set_style_border_color(scan, TC(TEXT_PRIMARY), 0);
    lv_obj_t *cap = lv_obj_get_child(scan, 0);
    if(cap){ lv_label_set_text(cap, "Scan"); lv_obj_set_style_text_font(cap, TF(UI_16), 0);
             lv_obj_set_style_text_color(cap, TC(TEXT_PRIMARY), 0); lv_obj_center(cap); kit_keep(cap); }
    invert_on_press(scan); kit_keep(scan);
    lv_obj_set_align(sw, LV_ALIGN_DEFAULT); lv_obj_set_size(sw, 48, 26); lv_obj_set_pos(sw, 252, 83); lv_obj_set_ext_click_area(sw, 12);
    lv_obj_set_style_bg_color(sw, TC(CANVAS), LV_PART_MAIN); lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(sw, 2, LV_PART_MAIN); lv_obj_set_style_border_color(sw, TC(TEXT_PRIMARY), LV_PART_MAIN);
    lv_obj_set_style_bg_color(sw, TC(TEXT_PRIMARY), (lv_style_selector_t)LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(sw, TC(TEXT_PRIMARY), LV_PART_KNOB);
    lv_obj_set_style_bg_color(sw, TC(CANVAS), (lv_style_selector_t)LV_PART_KNOB | LV_STATE_CHECKED);
    kit_keep(sw);
    lv_obj_set_style_text_font(name, TF(UI_16), 0); lv_obj_set_style_text_color(name, TC(TEXT_PRIMARY), 0);
    lv_obj_set_width(name, 250); lv_obj_align(name, LV_ALIGN_TOP_MID, 0, 116);
    lv_obj_set_style_text_font(sub, TF(UI_14), 0); lv_obj_set_style_text_color(sub, TC(TEXT_SECONDARY), 0);
    lv_obj_set_width(sub, 250); lv_obj_align(sub, LV_ALIGN_TOP_MID, 0, 136);
    rule(root, 172);
    int ly = CLASSIC_LIST_END - 4 * CL_ROW_H;   /* the menu box: power row, status, then whole rows to its end */
    lv_obj_set_align(list, LV_ALIGN_DEFAULT); lv_obj_set_pos(list, CL_COL_X, ly); lv_obj_set_size(list, CL_COL_W, CLASSIC_LIST_END - ly);
    lv_obj_set_style_pad_row(list, 0, 0); lv_obj_set_style_pad_bottom(list, CL_ROW_H, 0);
}

/* ---- Now Playing chrome: title bar, "n of N", lyrics, decorative bars ---- */
#define CL_BARS 16
#define CL_FMT_W 260           /* the format line in place of the bars */
static lv_obj_t *g_np_state, *g_np_num, *g_np_fmt, *g_bar[CL_BARS];
static int bars_on(void){ return cfg_get_int("classic_bars", 1) != 0; }
static lv_timer_t *g_bars_t;
static uint32_t g_seed = 0x2545F491u;
static int g_level[CL_BARS];
static uint32_t rnd(void){ g_seed ^= g_seed << 13; g_seed ^= g_seed >> 17; g_seed ^= g_seed << 5; return g_seed; }
/* ~8 fps while Now Playing is in front and playing; frozen otherwise (decoration: the player's audio is not tapped) */
static void bars_tick(lv_timer_t *t){
    (void)t;
    if(screen_current() != SCR_NOWPLAYING) return;
    int playing = ui_is_playing();
    if(g_np_state) lv_label_set_text(g_np_state, playing ? LV_SYMBOL_PLAY : LV_SYMBOL_PAUSE);
    if(!playing || !bars_on()) return;
    for(int i = 0; i < CL_BARS; i++){
        int peak = 38 - i * 26 / CL_BARS;                    /* low bands run taller, as on a real analyser */
        int target = 4 + (int)(rnd() % (unsigned)peak);
        g_level[i] = (g_level[i] * 2 + target) / 3;
        lv_obj_set_height(g_bar[i], g_level[i]); lv_obj_set_y(g_bar[i], 222 - g_level[i]);
    }
}
static void lyrics_cb(lv_event_t *e){ (void)e; lyrics_open(); }
void classic_np_chrome(lv_obj_t *scr){
    if(g_np_num) return;
    g_np_state = lv_label_create(scr); kit_keep(g_np_state);
    lv_obj_set_style_text_font(g_np_state, TF(UI_14), 0); lv_obj_set_style_text_color(g_np_state, TC(TEXT_PRIMARY), 0);
    lv_obj_set_pos(g_np_state, 72, CL_TITLE_Y - 8); lv_label_set_text(g_np_state, ui_is_playing() ? LV_SYMBOL_PLAY : LV_SYMBOL_PAUSE);
    title_label(scr, "Now Playing");
    classic_battery(scr, 258, CL_TITLE_Y - 7);
    rule(scr, CL_RULE_Y);
    g_np_num = lv_label_create(scr); kit_keep(g_np_num); lv_label_set_text(g_np_num, "");
    lv_obj_set_style_text_font(g_np_num, TF(UI_14), 0); lv_obj_set_style_text_color(g_np_num, TC(TEXT_PRIMARY), 0);
    lv_obj_set_pos(g_np_num, 62, 80);
    lv_obj_t *ly = lv_button_create(scr); lv_obj_remove_style_all(ly); kit_keep(ly);
    lv_obj_set_pos(ly, 230, 74); lv_obj_set_size(ly, 74, 26); lv_obj_set_ext_click_area(ly, 8);
    lv_obj_set_style_radius(ly, 13, 0);
    lv_obj_set_style_bg_color(ly, TC(TEXT_PRIMARY), LV_STATE_PRESSED); lv_obj_set_style_bg_opa(ly, LV_OPA_COVER, LV_STATE_PRESSED);
    ui_on(ly, lyrics_cb, LV_EVENT_CLICKED, NULL, "np.classic.lyrics", UI_REACHABLE);
    lv_obj_t *lt = lv_label_create(ly); lv_label_set_text(lt, "Lyrics");
    lv_obj_set_style_text_font(lt, TF(UI_16), 0); lv_obj_set_style_text_color(lt, TC(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_color(lt, TC(CANVAS), LV_STATE_PRESSED); lv_obj_add_flag(lt, LV_OBJ_FLAG_EVENT_BUBBLE); lv_obj_center(lt);
    int x0 = 66, w = (294 - x0) / CL_BARS;
    for(int i = 0; i < CL_BARS; i++){
        g_bar[i] = plain(scr);
        g_level[i] = 6 + (CL_BARS - i) * 2;
        lv_obj_set_pos(g_bar[i], x0 + i * w + 1, 222 - g_level[i]); lv_obj_set_size(g_bar[i], w - 3, g_level[i]);
        lv_obj_set_style_bg_opa(g_bar[i], LV_OPA_COVER, 0); lv_obj_set_style_bg_color(g_bar[i], TC(TEXT_PRIMARY), 0);
    }
    g_np_fmt = lv_label_create(scr); kit_keep(g_np_fmt); lv_label_set_text(g_np_fmt, "");
    lv_obj_set_style_text_font(g_np_fmt, TF(UI_16), 0); lv_obj_set_style_text_color(g_np_fmt, TC(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_align(g_np_fmt, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(g_np_fmt, CL_FMT_W); lv_label_set_long_mode(g_np_fmt, LV_LABEL_LONG_CLIP);
    lv_obj_set_pos(g_np_fmt, 180 - CL_FMT_W / 2, 194);
    classic_np_bars_apply();
    if(!g_bars_t) g_bars_t = lv_timer_create(bars_tick, 120, NULL);
}
/* Spectrum bars toggle: the bars, or the track's format in their place */
void classic_np_bars_apply(void){
    if(!g_np_fmt) return;
    int on = bars_on();
    for(int i = 0; i < CL_BARS; i++) if(g_bar[i]){ if(on) lv_obj_remove_flag(g_bar[i], LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(g_bar[i], LV_OBJ_FLAG_HIDDEN); }
    if(on) lv_obj_add_flag(g_np_fmt, LV_OBJ_FLAG_HIDDEN); else lv_obj_remove_flag(g_np_fmt, LV_OBJ_FLAG_HIDDEN);
}
static int fmt_width(const char *t, const lv_font_t *f){
    lv_point_t sz; lv_text_get_size(&sz, t, f, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE); return sz.x;
}
static void drop_space_before(char *b, const char *word){   /* " kbps" -> "kbps" */
    char *q = strstr(b, word); if(q && q > b && q[-1] == ' ') memmove(q - 1, q, strlen(q) + 1);
}
/* the whole line, never scrolling: compact ("FLAC/1411kbps/44.1kHz") and then a size smaller until it fits */
void classic_np_format(const char *fmt){
    if(!g_np_fmt || !fmt) return;
    char b[96]; snprintf(b, sizeof b, "%s", fmt);
    const lv_font_t *f = TF(UI_16);
    if(fmt_width(b, f) > CL_FMT_W){
        drop_space_before(b, "kbps"); drop_space_before(b, "kHz");
        char *q; while((q = strstr(b, " / "))){ q[0] = '/'; memmove(q + 1, q + 3, strlen(q + 3) + 1); }
    }
    if(fmt_width(b, f) > CL_FMT_W) f = TF(UI_14);
    if(fmt_width(b, f) > CL_FMT_W) f = TF(UI_12);
    if(lv_obj_get_style_text_font(g_np_fmt, 0) != f) lv_obj_set_style_text_font(g_np_fmt, f, 0);
    if(strcmp(lv_label_get_text(g_np_fmt), b)) lv_label_set_text(g_np_fmt, b);
}
/* "3/19" from the player -> "3 of 19" */
void classic_np_num(const char *num){
    if(!g_np_num) return;
    char b[40] = ""; const char *sl = num ? strchr(num, '/') : NULL;
    if(sl && sl[1]) snprintf(b, sizeof b, "%.*s of %s", (int)(sl - num), num, sl + 1);
    else if(num) snprintf(b, sizeof b, "%s", num);
    if(strcmp(lv_label_get_text(g_np_num), b)) lv_label_set_text(g_np_num, b);
}

/* ---- lock screen: battery, big clock, date, a rule, the track and its progress, "tap to wake" ---- */
static lv_obj_t *g_sv_clock, *g_sv_date, *g_sv_title, *g_sv_artist, *g_sv_state, *g_sv_bar;
static lv_obj_t *sv_label(lv_obj_t *root, const lv_font_t *f, lv_color_t c, int y, int w){
    lv_obj_t *l = lv_label_create(root); kit_keep(l); lv_label_set_text(l, "");
    lv_obj_set_style_text_font(l, f, 0); lv_obj_set_style_text_color(l, c, 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_size(l, w, lv_font_get_line_height(f)); lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, y);
    return l;
}
static void sv_deleted(lv_event_t *e){ (void)e; g_sv_clock = g_sv_date = g_sv_title = g_sv_artist = g_sv_state = g_sv_bar = NULL; }
void csaver_create(lv_obj_t *root){
    lv_obj_set_style_bg_color(root, TC(CANVAS), 0); lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(root, sv_deleted, LV_EVENT_DELETE, NULL);
    classic_battery(root, 165, 44);
    g_sv_clock = sv_label(root, TF(SAVER_CLOCK), TC(TEXT_PRIMARY), 98, 300);
    g_sv_date = sv_label(root, TF(DATE), TC(TEXT_MUTED), 178, 260);
    lv_obj_t *r = plain(root); lv_obj_set_pos(r, 70, 204); lv_obj_set_size(r, 220, 1);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0); lv_obj_set_style_bg_color(r, TC(TEXT_MUTED), 0);
    g_sv_title = sv_label(root, TF(UI_16), TC(TEXT_PRIMARY), 216, 200);
    lv_label_set_long_mode(g_sv_title, LV_LABEL_LONG_SCROLL_CIRCULAR);
    g_sv_state = lv_label_create(root); kit_keep(g_sv_state); lv_label_set_text(g_sv_state, "");
    lv_obj_set_style_text_font(g_sv_state, TF(UI_12), 0); lv_obj_set_style_text_color(g_sv_state, TC(TEXT_PRIMARY), 0);
    lv_obj_set_pos(g_sv_state, 66, 219);
    g_sv_artist = sv_label(root, TF(UI_14), TC(TEXT_MUTED), 240, 220);
    lv_label_set_long_mode(g_sv_artist, LV_LABEL_LONG_SCROLL_CIRCULAR);
    g_sv_bar = lv_bar_create(root); lv_obj_remove_style_all(g_sv_bar); kit_keep(g_sv_bar);
    lv_obj_set_pos(g_sv_bar, 96, 266); lv_obj_set_size(g_sv_bar, 168, 12); lv_bar_set_range(g_sv_bar, 0, 1000);
    lv_obj_set_style_border_width(g_sv_bar, 2, LV_PART_MAIN); lv_obj_set_style_border_color(g_sv_bar, TC(TEXT_PRIMARY), LV_PART_MAIN);
    lv_obj_set_style_radius(g_sv_bar, 6, LV_PART_MAIN); lv_obj_set_style_pad_all(g_sv_bar, 3, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(g_sv_bar, LV_OPA_COVER, LV_PART_INDICATOR); lv_obj_set_style_bg_color(g_sv_bar, TC(TEXT_PRIMARY), LV_PART_INDICATOR);
    lv_obj_set_style_radius(g_sv_bar, 3, LV_PART_INDICATOR);
    sv_label(root, TF(UI_14), TC(TEXT_MUTED), 300, 200);
    lv_label_set_text(lv_obj_get_child(root, -1), "tap to wake");
    csaver_set_track(NULL, NULL);
}
static void sv_progress(void){
    if(!g_sv_bar) return;
    track_state_t st; ipc_get_state(&st);
    int v = st.have_track && st.duration_ms > 0 ? (int)(st.position_ms * 1000 / st.duration_ms) : 0;
    lv_bar_set_value(g_sv_bar, v < 0 ? 0 : v > 1000 ? 1000 : v, LV_ANIM_OFF);
    if(g_sv_state) lv_label_set_text(g_sv_state, st.have_track ? (ui_is_playing() ? LV_SYMBOL_PLAY : LV_SYMBOL_PAUSE) : "");
}
void csaver_set_clock(const char *t, const char *date){
    if(g_sv_clock) lv_label_set_text(g_sv_clock, t ? t : "--:--");
    if(g_sv_date) lv_label_set_text(g_sv_date, date ? date : "");
    sv_progress();
}
void csaver_set_track(const char *title, const char *artist){
    if(!g_sv_title) return;
    int have = title != NULL;
    lv_label_set_text(g_sv_title, have ? (title[0] ? title : "Untitled") : "");
    lv_label_set_text(g_sv_artist, have && artist ? artist : "");
    if(have) lv_obj_remove_flag(g_sv_bar, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(g_sv_bar, LV_OBJ_FLAG_HIDDEN);
    sv_progress();
}

const theme_kit_t theme_kit_classic = { .id = "classic", .header = classic_header, .style = classic_style, .home_build = chome_create, .fork_np_layout = kit_fork_np_classic };
