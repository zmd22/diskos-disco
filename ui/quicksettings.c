#include "battery.h"
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "fwcaps.h"
#include "theme.h"
#include "theme_kit.h"
#include "screens.h"
#include "braun.h"
#include "theme.h"
#include "orbit.h"
#include "ipc.h"
#include "config.h"
#include <math.h>
#include "scanner.h"
#include "i18n.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Quick Settings, "orbit on rails": six round toggles orbit a hub showing the playing cover inside its
 * progress ring (the Ring Now Playing motif). Brightness is a control arc on the top rim; the battery is
 * an indicator arc on the bottom rim with the Home screen's logic (white, red + slow pulse at <=15%).
 * A tap on the cover goes Home, the small centre button plays/pauses. A short tap on Wi-Fi / Bluetooth
 * toggles the radio, a long press opens its settings. Background: the album's blurred backdrop, dimmed. */

LV_FONT_DECLARE(font_icons_28)          /* FontAwesome 28px */
#define WI_SUN "\xEF\x86\x85"           /* f185 sun */
#define QS_ARC_D     348                /* rim arcs */
#define QS_ARC_W     TH_ARC_CTL         /* brightness: a control arc, rounded ends */

enum { T_WIFI, T_BT, T_LIB, T_RESCAN, T_MODE, T_SET, T_N };
static orbit_t g_orb;
static lv_obj_t *g_bright, *g_batt, *g_bg, *g_dim;
static lv_obj_t *g_cover_clip, *g_cover_img, *g_cover_note, *g_prog, *g_pp, *g_pp_glyph;
static lv_timer_t *g_prog_timer;
static battery_arc_t g_battery;
static lv_obj_t *g_batt_icon, *g_sun;
static int g_scanning = -1;
static void rescan_show(int on);
static void dq_paint(void);

static void bright_change_cb(lv_event_t *e){ ui_backlight(lv_arc_get_value(lv_event_get_target(e))); }
static void bright_release_cb(lv_event_t *e){ ui_set_brightness(lv_arc_get_value(lv_event_get_target(e))); }
static void pp_cb(lv_event_t *e){ (void)e; if(ui_transport_command("0201000C0000")==0) ui_pp_glyph(g_pp_glyph,ui_pp_icon_playing(ui_is_playing())); }
static void home_cb(lv_event_t *e){ (void)e; screen_show(SCR_HOME); }            /* the cover leads Home */

/* the Mode toggle wears the icon of the mode the player is actually in (same set as Working Mode) */
static const char *mode_glyph(void){
    switch(ui_get_source_mode()){
        case 1:  return LV_SYMBOL_USB;
        case 2:  return LV_SYMBOL_BLUETOOTH;
        case 3:  return LV_SYMBOL_DRIVE;
        case 4:  return LV_SYMBOL_VOLUME_MAX;
        case 5:  return LV_SYMBOL_WIFI;
        default: return LV_SYMBOL_SD_CARD;
    }
}
static void paint_radios(void){
    lv_color_t acc = th_ringlike()?ui_current_accent():TC(ACCENT_PRIMARY);
    orbit_set_on(&g_orb, T_WIFI, wifi_radio_live(), acc);           /* the real radio, not the saved intent */
    int bs = bt_state();                                        /* the real radio, not the saved intent */
    orbit_set_on(&g_orb, T_BT, bs == BT_ON, acc);
    orbit_set_pending(&g_orb, bs == BT_TURNING_ON ? T_BT : -1, acc);   /* ringed while it comes up */
}
static void rescan_go(void){ ui_rescan_library(); ui_toast("Rescan requested"); rescan_show(1); }
static void pick_cb(int i){                                       /* short taps */
    switch(i){
        case T_WIFI: { int on = wifi_toggle(); orbit_set_on(&g_orb,T_WIFI,on,th_ringlike()?ui_current_accent():TC(ACCENT_PRIMARY));
                       ui_toast(on ? "Turning on Wi-Fi\xE2\x80\xA6" : "Wi-Fi off"); } break;
        case T_BT:   { int on = bt_toggle(); paint_radios();
                       ui_toast(on ? "Turning on Bluetooth\xE2\x80\xA6" : "Bluetooth off"); } break;
        case T_LIB:    screen_show(SCR_LIBRARY); break;
        case T_RESCAN: if(scanner_active()){ ui_toast("Already scanning"); break; }
                       fileops_confirm("Rescan library?", "Looks for new and changed music", "Rescan", rescan_go); break;
        case T_MODE:   modes_open(); break;
        case T_SET:    screen_show(SCR_SETTINGS); break;
    }
}
static void wifi_long_cb(lv_event_t *e){ (void)e; wifi_open(); }
static void bt_long_cb(lv_event_t *e){ (void)e; bt_open(); }

/* the hub's progress ring follows the track (1 s poll; only redraws on a visible change) */
static void prog_tick(lv_timer_t *t){
    (void)t;
    if(!g_prog || screen_current() != SCR_QUICK) return;
    rescan_show(scanner_active() ? 1 : 0);                     /* also picks up a scan started elsewhere */
    paint_radios();                                             /* BT comes up in the background: follow it */
    dq_paint();
    track_state_t st; ipc_get_state(&st);
    int v = (st.have_track && st.duration_ms > 0) ? (int)((long long)st.position_ms * 1000 / st.duration_ms) : 0;
    if(abs(v - lv_arc_get_value(g_prog)) >= 3) lv_arc_set_value(g_prog, v);
}
/* rescan: while the library scan runs, the Rescan icon turns red and spins (1 turn/s) */
static void spin_exec(void *var, int32_t v){ lv_obj_set_style_transform_rotation((lv_obj_t *)var, v, 0); }
static void rescan_show(int on){
    if(on == g_scanning) return;
    g_scanning = on;
    lv_obj_t *ic = g_orb.icon[T_RESCAN];
    lv_anim_delete(ic, spin_exec);
    lv_obj_set_style_text_color(ic, on ? TC(ACCENT_PRIMARY) : (th_braun() ? TC(ON_CONTROL) : TC(TEXT_PRIMARY)), 0);
    if(g_orb.cap[T_RESCAN]){ lv_label_set_text(g_orb.cap[T_RESCAN], on ? "Scanning" : "Rescan");
                            lv_obj_set_style_text_color(g_orb.cap[T_RESCAN], on ? TC(TEXT_PRIMARY) : TC(TEXT_SECONDARY), 0); }
    if(on){
        lv_obj_update_layout(ic);
        lv_obj_set_style_transform_pivot_x(ic, lv_obj_get_width(ic) / 2, 0);
        lv_obj_set_style_transform_pivot_y(ic, lv_obj_get_height(ic) / 2, 0);
        lv_anim_t a; lv_anim_init(&a);
        lv_anim_set_var(&a, ic); lv_anim_set_exec_cb(&a, spin_exec);
        lv_anim_set_values(&a, 0, 3600); lv_anim_set_duration(&a, 1000);
        lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE); lv_anim_start(&a);
    } else lv_obj_set_style_transform_rotation(ic, 0, 0);
}

/* ---- Disco: the navigation field popped out on the right holds the music (a small cover in its progress ring, a
 * faint CD glow, play/pause); the six toggles sit on the left, either on two arcs (cfg disco_qs 0) or as a list (1).
 * Brightness stays on the top rim, the battery on the bottom rim. */
#define DQ_CX 272                                                   /* the field's visible middle */
#define DQ_CY 180
static int g_hub_d = ORBIT_HUB;                                    /* the cover disc (Disco: bigger) */
static lv_obj_t *g_dq_row[T_N], *g_dq_ico[T_N], *g_dq_val[T_N], *g_dq_name[T_N], *g_dq_badge[T_N];
static void set_text_if(lv_obj_t *l, const char *t){ if(l && strcmp(lv_label_get_text(l), t)) lv_label_set_text(l, t); }
static void set_col_if(lv_obj_t *l, lv_color_t c){ if(l && !lv_color_eq(lv_obj_get_style_text_color(l, 0), c)) lv_obj_set_style_text_color(l, c, 0); }
static void dq_paint(void){                                          /* runs every second: only what changed is touched */
    if(!th_disco()) return;
    lv_color_t acc = ui_current_accent();
    int on[T_N] = { wifi_radio_live(), bt_state() == BT_ON, 0, scanner_active(), 0, 0 };   /* real radio states */
    for(int i = 0; i < T_N; i++){
        if(!g_dq_ico[i]) continue;
        set_col_if(g_dq_ico[i], on[i] ? acc : TC(TEXT_PRIMARY));
        set_text_if(g_dq_val[i], i <= T_BT ? (on[i] ? "On" : "Off") : i == T_RESCAN ? "" : LV_SYMBOL_RIGHT);
        if(g_dq_badge[i]){                                         /* the radios' badge: accent when on */
            lv_color_t bc = on[i] ? acc : TC(SURFACE_RAISED);
            if(!lv_color_eq(lv_obj_get_style_bg_color(g_dq_badge[i], 0), bc)) lv_obj_set_style_bg_color(g_dq_badge[i], bc, 0);
            set_col_if(g_dq_val[i], on[i] ? theme_on_color(acc) : TC(TEXT_PRIMARY));
        }
        if(i == T_RESCAN) set_text_if(g_dq_name[i], on[i] ? "Scanning" : "Rescan");   /* in the name: no room for both */
    }
    set_text_if(g_dq_ico[T_MODE], mode_glyph());
}
static void dq_row_cb(lv_event_t *e){
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if(lv_event_get_code(e) == LV_EVENT_LONG_PRESSED){ if(i == T_WIFI) wifi_open(); else if(i == T_BT) bt_open(); return; }
    if(lv_event_get_code(e) == LV_EVENT_SHORT_CLICKED){ pick_cb(i); dq_paint(); }
}
static void disco_layout(lv_obj_t *root){
    int list = cfg_get_int("disco_qs", 1) == 1;                    /* the list (default); 0 = the two arcs */
    /* the field: the open navigation circle's shape, its right end cut by the rim */
    lv_obj_t *f = lv_obj_create(root); lv_obj_remove_style_all(f);
    lv_obj_set_pos(f, 196, 90); lv_obj_set_size(f, 230, 180);
    lv_obj_set_style_radius(f, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(f, TC(SCRIM), 0); lv_obj_set_style_bg_opa(f, 204, 0);
    lv_obj_clear_flag(f, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_to_index(f, lv_obj_get_index(g_dim) + 1);
    g_hub_d = 120;                                                   /* a bigger cover in its ring */
    lv_obj_set_size(g_cover_clip, g_hub_d, g_hub_d); lv_obj_set_size(g_prog, g_hub_d + 16, g_hub_d + 16);
    lv_obj_align(g_prog, LV_ALIGN_CENTER, DQ_CX - 180, DQ_CY - 180);
    lv_obj_align(g_cover_clip, LV_ALIGN_CENTER, DQ_CX - 180, DQ_CY - 180);
    /* the album-colour glow: soft outline rings, not a 24 px blurred shadow (it sits inside the progress ring, which
     * redraws every second, and an uncached shadow was recomputed with it) */
    lv_obj_set_style_outline_width(g_cover_clip, 5, 0); lv_obj_set_style_outline_opa(g_cover_clip, 70, 0); lv_obj_set_style_outline_pad(g_cover_clip, 0, 0);
    lv_obj_set_style_outline_color(g_cover_clip, ui_media_accent(), 0);
    lv_obj_align(g_pp, LV_ALIGN_CENTER, DQ_CX - 180, DQ_CY - 180);
    if(!list){                                                       /* two arcs: radios outside, library things inside */
        static const struct { int a, r; } P[T_N] = { { 211, 210 }, { 180, 214 }, { 206, 140 }, { 180, 136 }, { 149, 210 }, { 154, 140 } };
        for(int i = 0; i < T_N; i++){
            float an = P[i].a * 0.0174533f;
            int x = DQ_CX + (int)lroundf(P[i].r * cosf(an)), y = DQ_CY + (int)lroundf(P[i].r * sinf(an));
            lv_obj_align(g_orb.btn[i], LV_ALIGN_CENTER, x - 180, y - 180 - 6);
            if(g_orb.cap[i]){ lv_obj_set_style_text_font(g_orb.cap[i], TF(UI_10), 0); lv_obj_align(g_orb.cap[i], LV_ALIGN_CENTER, x - 180, y - 180 + 26); }
        }
        return;
    }
    for(int i = 0; i < T_N; i++){ lv_obj_add_flag(g_orb.btn[i], LV_OBJ_FLAG_HIDDEN); if(g_orb.cap[i]) lv_obj_add_flag(g_orb.cap[i], LV_OBJ_FLAG_HIDDEN); }
    static const char *const G[T_N] = { LV_SYMBOL_WIFI, LV_SYMBOL_BLUETOOTH, LV_SYMBOL_DIRECTORY, LV_SYMBOL_REFRESH, LV_SYMBOL_SD_CARD, LV_SYMBOL_SETTINGS };
    static const char *const N[T_N] = { "Wi-Fi", "Bluetooth", "Library", "Rescan", "Mode", "Settings" };
    /* the sun and the battery glyph move to the right-hand ends of their arcs (clear of the rows), a little bigger */
    if(g_sun){ lv_obj_set_style_transform_scale(g_sun, 176, 0); lv_obj_set_style_transform_pivot_x(g_sun, lv_pct(50), 0); lv_obj_set_style_transform_pivot_y(g_sun, lv_pct(50), 0);
               lv_obj_align(g_sun, LV_ALIGN_CENTER, 126, -100); }
    if(g_batt_icon){ lv_obj_set_style_text_font(g_batt_icon, TF(UI_20), 0); lv_obj_align(g_batt_icon, LV_ALIGN_CENTER, 126, 102); }
    /* five rows of tall pills, 6 px apart: Wi-Fi and Bluetooth share the top one (icon + a round On/Off badge set into
     * the pill), then Mode, Library, Rescan beside the field, Settings centred in the battery arc */
    #define DQ_H 50
    static const struct { int i, x, y, w; } RP[T_N] = {
        { T_WIFI,  80, 42, 98 }, { T_BT, 184, 42, 98 },
        { T_MODE,   0, 102, 0 }, { T_LIB, 0, 158, 0 }, { T_RESCAN, 0, 214, 0 },
        { T_SET,   86, 270, 188 } };
    for(int k = 0; k < T_N; k++){
        int i = RP[k].i, y = RP[k].y, left = RP[k].x, w = RP[k].w;
        if(!w){                                                       /* follow the rim on the left, stop short of the field */
            int d1 = y - 180, d2 = y + DQ_H - 180; if(d1 < 0) d1 = -d1; if(d2 < 0) d2 = -d2;
            int dy = d1 > d2 ? d1 : d2;
            left = 180 - (int)sqrtf((float)(176 * 176 - dy * dy)) + 10; if(left < 16) left = 16;
            w = 190 - left;
        }
        int radio = (i == T_WIFI || i == T_BT);
        lv_obj_t *r = lv_obj_create(root); lv_obj_remove_style_all(r); g_dq_row[i] = r;
        lv_obj_set_pos(r, left, y); lv_obj_set_size(r, w, DQ_H);
        lv_obj_set_style_radius(r, LV_RADIUS_CIRCLE, 0); lv_obj_set_style_bg_color(r, TC(SURFACE), 0); lv_obj_set_style_bg_opa(r, 150, 0);
        lv_obj_set_style_bg_color(r, TC(SURFACE_RAISED), LV_STATE_PRESSED);
        lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_USER_2); lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(r, dq_row_cb, LV_EVENT_SHORT_CLICKED, (void *)(intptr_t)i);
        lv_obj_add_event_cb(r, dq_row_cb, LV_EVENT_LONG_PRESSED, (void *)(intptr_t)i);
        g_dq_ico[i] = lv_label_create(r); lv_label_set_text(g_dq_ico[i], G[i]); lv_obj_set_style_text_font(g_dq_ico[i], TF(UI_24), 0);
        lv_obj_align(g_dq_ico[i], LV_ALIGN_LEFT_MID, radio ? 18 : 16, 0);
        if(radio){                                                    /* no name: the icon says it; On/Off in a round badge */
            g_dq_name[i] = NULL;
            lv_obj_t *b = lv_obj_create(r); lv_obj_remove_style_all(b); g_dq_badge[i] = b;
            lv_obj_set_size(b, DQ_H - 8, DQ_H - 8); lv_obj_align(b, LV_ALIGN_RIGHT_MID, -4, 0);
            lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0); lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(b, TC(SURFACE_RAISED), 0);
            lv_obj_clear_flag(b, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
            g_dq_val[i] = lv_label_create(b); lv_obj_set_style_text_font(g_dq_val[i], TF(UI_14), 0);
            lv_obj_set_style_text_color(g_dq_val[i], TC(TEXT_PRIMARY), 0); lv_label_set_text(g_dq_val[i], "Off"); lv_obj_center(g_dq_val[i]);
        } else {
            lv_obj_t *n = g_dq_name[i] = lv_label_create(r); lv_label_set_text(n, N[i]); lv_obj_set_style_text_font(n, TF(UI_24), 0);
            lv_obj_set_style_text_color(n, TC(TEXT_PRIMARY), 0); lv_obj_align(n, LV_ALIGN_LEFT_MID, 52, 0);
            g_dq_val[i] = lv_label_create(r); lv_obj_set_style_text_font(g_dq_val[i], TF(UI_16), 0);
            lv_obj_set_style_text_color(g_dq_val[i], TC(TEXT_SECONDARY), 0); lv_obj_align(g_dq_val[i], LV_ALIGN_RIGHT_MID, -16, 0);
        }
    }
    dq_paint();
}
/* Battery fill: normal text colour, low red, charging green; no pulse or glow. */
void quicksettings_create(lv_obj_t *root)
{
    lv_obj_set_style_bg_color(root, TC(CANVAS), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);

    /* media surface: the album's blurred backdrop, dimmed well down (hidden when there is no art) */
    g_bg = lv_image_create(root);
    lv_obj_set_size(g_bg, 360, 360); lv_obj_center(g_bg);
    lv_obj_clear_flag(g_bg, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(g_bg, LV_OBJ_FLAG_HIDDEN);
    g_dim = lv_obj_create(root);
    lv_obj_remove_style_all(g_dim);
    lv_obj_set_size(g_dim, 360, 360);
    lv_obj_set_style_bg_color(g_dim, TC(CANVAS), 0);
    lv_obj_set_style_bg_opa(g_dim,th_ringlike()?85:210,0);
    lv_obj_clear_flag(g_dim, LV_OBJ_FLAG_CLICKABLE);

    /* brightness: a control arc on the top rim, rounded ends; ADV_HITTEST keeps its touch area on the band */
    g_bright = lv_arc_create(root);
    lv_obj_remove_style(g_bright, NULL, LV_PART_KNOB);
    lv_obj_set_size(g_bright, QS_ARC_D, QS_ARC_D);
    lv_obj_center(g_bright);
    lv_obj_add_flag(g_bright, LV_OBJ_FLAG_ADV_HITTEST);
    lv_arc_set_rotation(g_bright, 0);
    lv_arc_set_bg_angles(g_bright, 228, 312);
    lv_arc_set_range(g_bright, 4, 40);
    lv_arc_set_value(g_bright, ui_get_brightness());
    lv_obj_set_style_arc_width(g_bright, QS_ARC_W, LV_PART_MAIN);
    lv_obj_set_style_arc_color(g_bright, th_braun() ? TC(SURFACE) : TC(CONTROL_TRACK_STRONG), LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(g_bright, true, LV_PART_MAIN);
    lv_obj_set_style_arc_width(g_bright, QS_ARC_W, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(g_bright, th_braun() ? TC(TEXT_PRIMARY) : TC(TEXT_PRIMARY), LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(g_bright, true, LV_PART_INDICATOR);
    lv_obj_add_event_cb(g_bright, bright_change_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(g_bright, bright_release_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(g_bright, bright_release_cb, LV_EVENT_PRESS_LOST, NULL);
    lv_obj_t *sun = lv_label_create(root);
    lv_obj_clear_flag(sun, LV_OBJ_FLAG_CLICKABLE);
    lv_label_set_text(sun, WI_SUN);
    lv_obj_set_style_text_font(sun, TF(ICON_28), 0);
    lv_obj_set_style_transform_scale(sun, 128, 0);            /* half size: a small marker under the arc */
    lv_obj_set_style_text_color(sun, TC(TEXT_SECONDARY), 0);
    lv_obj_align(sun, LV_ALIGN_TOP_MID, 0, 18);
    g_sun = sun;

    /* battery: an indicator arc on the bottom rim, filling left to right; display only */
    battery_arc_create(&g_battery,root,QS_ARC_D,QS_ARC_W,48,132,1);g_batt=g_battery.arc;
    g_batt_icon = lv_label_create(root);                        /* battery glyph above the arc, the sun's mirror */
    lv_obj_clear_flag(g_batt_icon, LV_OBJ_FLAG_CLICKABLE);
    lv_label_set_text(g_batt_icon, LV_SYMBOL_BATTERY_FULL);
    lv_obj_set_style_text_font(g_batt_icon, TF(UI_14), 0);
    lv_obj_set_style_text_color(g_batt_icon, TC(TEXT_SECONDARY), 0);
    lv_obj_align(g_batt_icon, LV_ALIGN_BOTTOM_MID, 0, -20);

    /* the six toggles, clockwise from the upper right, with captions */
    static const orbit_item_t it[T_N] = {
        { LV_SYMBOL_WIFI, "Wi-Fi" }, { LV_SYMBOL_BLUETOOTH, "Bluetooth" }, { LV_SYMBOL_DIRECTORY, "Library" },
        { LV_SYMBOL_REFRESH, "Rescan" }, { LV_SYMBOL_SD_CARD, "Mode" }, { LV_SYMBOL_SETTINGS, "Settings" } };
    orbit_create(&g_orb, root, it, T_N, -60, pick_cb);
    lv_obj_add_event_cb(g_orb.btn[T_WIFI], wifi_long_cb, LV_EVENT_LONG_PRESSED, NULL);
    lv_obj_add_event_cb(g_orb.btn[T_BT],   bt_long_cb,   LV_EVENT_LONG_PRESSED, NULL);
    orbit_set_glyph(&g_orb, T_MODE, mode_glyph());
    paint_radios();

    /* hub: progress ring around the cover; the cover goes Home, the centre button plays/pauses */
    g_prog = lv_arc_create(root);
    lv_obj_remove_style(g_prog, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(g_prog, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(g_prog, ORBIT_RING, ORBIT_RING);
    lv_obj_center(g_prog);
    lv_arc_set_rotation(g_prog, 270);
    lv_arc_set_bg_angles(g_prog, 0, 360);
    lv_arc_set_range(g_prog, 0, 1000);
    lv_obj_set_style_arc_width(g_prog, TH_ARC_IND, LV_PART_MAIN);
    lv_obj_set_style_arc_color(g_prog, th_braun() ? TC(SURFACE) : TC(CONTROL_TRACK_STRONG), LV_PART_MAIN);
    lv_obj_set_style_arc_width(g_prog, TH_ARC_IND, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(g_prog, true, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(g_prog, th_braun() ? TC(ACCENT_PRIMARY) : ui_media_accent(), LV_PART_INDICATOR);
    g_cover_clip = lv_button_create(root);
    lv_obj_remove_style_all(g_cover_clip);
    lv_obj_set_size(g_cover_clip, ORBIT_HUB, ORBIT_HUB);
    lv_obj_center(g_cover_clip);
    lv_obj_set_style_radius(g_cover_clip, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_clip_corner(g_cover_clip, true, 0);
    lv_obj_set_style_bg_color(g_cover_clip, ui_media_accent(), 0);
    lv_obj_set_style_bg_opa(g_cover_clip, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(g_cover_clip, home_cb, LV_EVENT_CLICKED, NULL);
    g_cover_note = lv_label_create(g_cover_clip);                 /* shown when there's no cover */
    lv_label_set_text(g_cover_note, LV_SYMBOL_AUDIO);
    lv_obj_set_style_text_font(g_cover_note, TF(UI_20), 0);
    lv_obj_set_style_text_color(g_cover_note, TC(TEXT_PRIMARY), 0);
    lv_obj_align(g_cover_note, LV_ALIGN_CENTER, 0, -22);
    g_cover_img = lv_image_create(g_cover_clip);
    lv_obj_clear_flag(g_cover_img, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(g_cover_img, LV_OBJ_FLAG_HIDDEN);
    g_pp = lv_button_create(root);
    lv_obj_remove_style_all(g_pp);
    lv_obj_set_size(g_pp, 40, 40);
    lv_obj_center(g_pp);
    lv_obj_set_style_radius(g_pp, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(g_pp, TC(CANVAS), 0);
    lv_obj_set_style_bg_opa(g_pp, 170, 0);
    lv_obj_set_style_bg_opa(g_pp, 230, LV_STATE_PRESSED);
    lv_obj_set_ext_click_area(g_pp, 4);
    lv_obj_add_event_cb(g_pp, pp_cb, LV_EVENT_CLICKED, NULL);
    g_pp_glyph = lv_label_create(g_pp);
    lv_label_set_text(g_pp_glyph, LV_SYMBOL_PLAY);
    lv_obj_set_style_text_font(g_pp_glyph, TF(UI_16), 0);
    lv_obj_set_style_text_color(g_pp_glyph, TC(TEXT_PRIMARY), 0);
    lv_obj_center(g_pp_glyph);

    if(!g_prog_timer) g_prog_timer = lv_timer_create(prog_tick, 1000, NULL);
    if(th_disco()) disco_layout(root);
    if(th_braun()){ br_face(root); if(g_dim) lv_obj_add_flag(g_dim, LV_OBJ_FLAG_HIDDEN);   /* Braun: the grille face, no dimming veil */
                    lv_obj_set_style_text_color(g_batt_icon, TC(TEXT_SECONDARY), 0);
                    for(uint32_t k = 0; k < lv_obj_get_child_count(root); k++){ lv_obj_t *o = lv_obj_get_child(root, k);   /* the sun marker */
                        if(lv_obj_check_type(o, &lv_label_class) && lv_obj_get_style_text_font(o, 0) == TF(ICON_28)) lv_obj_set_style_text_color(o, TC(TEXT_SECONDARY), 0); } }
}

/* the cover (RAM decode, scaled into the hub) and the blurred backdrop; called on track / art changes */
void quicksettings_set_art(const void *cover_dsc, const void *backdrop_src)
{
    if(!g_cover_img) return;
    lv_image_set_src(g_cover_img, NULL);
    if(cover_dsc){
        const lv_image_dsc_t *d = cover_dsc;
        lv_image_set_src(g_cover_img, cover_dsc);
        if(d->header.w > 0) lv_image_set_scale(g_cover_img, (uint32_t)(g_hub_d * 256 / d->header.w) + 2);
        lv_obj_center(g_cover_img);
        lv_obj_remove_flag(g_cover_img, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_cover_note, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(g_cover_img, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(g_cover_note, LV_OBJ_FLAG_HIDDEN);
    }
    lv_image_set_src(g_bg, NULL);
    if(backdrop_src && !th_braun()){ lv_image_set_src(g_bg, backdrop_src); lv_obj_remove_flag(g_bg, LV_OBJ_FLAG_HIDDEN); }   /* Braun: the grille, no album wash */
    else lv_obj_add_flag(g_bg, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(g_cover_clip, ui_media_accent(), 0);
    lv_obj_set_style_arc_color(g_prog, th_braun() ? TC(ACCENT_PRIMARY) : ui_media_accent(), LV_PART_INDICATOR);
}
void quicksettings_set_battery(int pct, int charging)
{
    if(!g_batt) return;
    if(pct < 0) return;
    if(pct > 100) pct = 100;
    battery_arc_set(&g_battery,pct,charging);
    if(g_batt_icon){
        const char *g = charging ? LV_SYMBOL_CHARGE : pct > 80 ? LV_SYMBOL_BATTERY_FULL : pct > 55 ? LV_SYMBOL_BATTERY_3
                      : pct > 30 ? LV_SYMBOL_BATTERY_2 : pct > 10 ? LV_SYMBOL_BATTERY_1 : LV_SYMBOL_BATTERY_EMPTY;
        if(strcmp(lv_label_get_text(g_batt_icon), g)) lv_label_set_text(g_batt_icon, g);
        lv_obj_set_style_text_color(g_batt_icon, charging?TC(STATUS_SUCCESS):pct<=15?TC(STATUS_DANGER):TC(TEXT_SECONDARY), 0);
    }
    quicksettings_refresh(ui_is_playing());
}
void quicksettings_set_now_playing(const char *title, const char *artist, int playing)
{
    (void)title; (void)artist;
    if(g_pp_glyph) lv_label_set_text(g_pp_glyph, playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
    if(g_prog) lv_obj_set_style_arc_color(g_prog, th_braun() ? TC(ACCENT_PRIMARY) : ui_media_accent(), LV_PART_INDICATOR);
}
void quicksettings_set_volume(int vol){ (void)vol; }       /* the volume arc gave way to the battery */
void quicksettings_refresh(int playing)
{
    if(!g_prog)return;
    if(g_pp_glyph) lv_label_set_text(g_pp_glyph, playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
    if(g_bright && !lv_obj_has_state(g_bright, LV_STATE_PRESSED))   /* don't fight an active drag */
        lv_arc_set_value(g_bright, ui_get_brightness());
    orbit_set_glyph(&g_orb, T_MODE, mode_glyph());
    paint_radios();
}

/* ---- compatibility with 1.1's screen manager ----------------------------------------------------
 * Upstream's Quick Settings is user-configurable: a tile palette plus a config screen (SCR_QSCONFIG),
 * and the manager rebuilds the panel when that changes. This fork's panel is a fixed layout, so the
 * rebuild is a no-op and the config screen just says so rather than offering switches that do nothing. */
void quicksettings_build(void){ }

static lv_obj_t *g_qscfg_root;
void qsconfig_create(lv_obj_t *root){
    g_qscfg_root = root;
    lv_obj_set_style_bg_color(root, TC(CANVAS), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    ui_header(root, "Quick Settings");
    lv_obj_t *m = lv_label_create(root);
    lv_label_set_text(m, "This build uses a fixed panel:\n"
                         "brightness and battery arcs,\nthe cover and six toggles.");
    lv_label_set_long_mode(m, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(m, 50, 120); lv_obj_set_size(m, 260, 120);
    lv_obj_set_style_text_align(m, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(m, TH_F_DETAIL, 0);
    lv_obj_set_style_text_color(m, TC(TEXT_SECONDARY), 0);
}
void qsconfig_refresh(void){ }
