/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
/* Settings -> Last.fm (SCR_LASTFM): per-user setup + account authorization + status.
 *
 * Flow, all driven by the lastfm.c engine (this file is only the view):
 *   1. "Set up"   -> lastfm_setup_start(): a QR of the on-device web page appears; the
 *                    user scans it, pastes their own api_key + secret on their phone.
 *   2. creds land -> the engine auto-runs lastfm_auth_begin(); a QR of the last.fm
 *                    approve URL appears -> user taps Allow on their phone.
 *   3. connected  -> "Connected as <user>", a Scrobbling on/off switch, Disconnect,
 *                    and the pending (offline) scrobble count.
 * A 500ms timer rebuilds the body only when the state signature changes, and stops the
 * setup web server whenever we leave the screen. Round-screen-aware header (clear corners). */
#include "screens.h"
#include "theme_kit.h"
#include "theme.h"
#include "lastfm.h"
#include <stdio.h>
#include <string.h>

static lv_obj_t *g_body;
static lv_timer_t *g_refresh;
static int g_sig = -12345;

static void go_back(lv_event_t *e){ (void)e; lastfm_setup_stop(); screen_back(); }   /* POP the stack like every other back button - screen_show(SCR_APPS) forward-pushed LASTFM, so a later Back resurfaced it (lastfm opened only from Apps, so back -> Apps) */


static lv_obj_t *body_label(const char *txt, const lv_font_t *font, theme_color_role_t color){
    lv_obj_t *l = lv_label_create(g_body);
    /* its line breaks suit a face that fits them; in a wider face it is wrapped again here, at spaces only, as full
     * lines (no word left alone, and "Last.fm" is never split at its dot as LVGL's own wrap would) */
    char buf[192]; lv_snprintf(buf, sizeof buf, "%s", txt);
    lv_point_t sz; lv_text_get_size(&sz, buf, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    if(sz.x > 250){
        for(char *c = buf; *c; c++) if(*c == '\n') *c = ' ';
        char *line = buf, *last_sp = NULL;
        for(char *c = buf; ; c++){
            if(*c == ' ' || *c == 0){
                char keep = *c; *c = 0;
                lv_point_t w; lv_text_get_size(&w, line, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
                *c = keep;
                if(w.x > 250 && last_sp){ *last_sp = '\n'; line = last_sp + 1; }
                if(!*c) break;
                last_sp = c;
            }
        }
    }
    lv_label_set_text(l, buf);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, 250);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, theme_color(color), 0);
    return l;
}
static lv_obj_t *body_button(const char *txt, lv_event_cb_t cb){
    lv_obj_t *b = lv_button_create(g_body);
    lv_obj_set_height(b, 44); lv_obj_set_width(b, 190);
    lv_obj_set_style_radius(b, 22, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);     /* no LVGL default-theme drop shadow: every other button is flat */
    lv_obj_set_style_bg_color(b, th_disco() ? ui_current_accent() : TC(FIXED_LASTFM), 0);   /* Disco: the accent, not Last.fm red */
    lv_obj_set_ext_click_area(b, 6);
    ui_on(b, cb, LV_EVENT_CLICKED, NULL, "lastfm_ui.cb", UI_CORE);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, TF(UI_18), 0);
    lv_obj_set_style_text_color(l, TC(FIXED_ON_LASTFM), 0);   /* brand red is fixed, so is its text */
    lv_obj_center(l);
    return b;
}
static void body_qr(const char *url){
    lv_obj_t *qr = lv_qrcode_create(g_body);
    lv_qrcode_set_size(qr, 156);
    lv_qrcode_set_dark_color(qr, TC(FIXED_QR_DARK));
    lv_qrcode_set_light_color(qr, TC(FIXED_QR_LIGHT));
    lv_qrcode_update(qr, url, (uint32_t)strlen(url));
    lv_obj_set_style_border_width(qr, 5, 0);
    lv_obj_set_style_border_color(qr, TC(FIXED_QR_LIGHT), 0);
}

static void on_setup(lv_event_t *e){ (void)e; lastfm_setup_start(); g_sig=-1; }
static void on_connect(lv_event_t *e){ (void)e; lastfm_auth_begin(); g_sig=-1; }
static void on_disconnect(lv_event_t *e){ (void)e; lastfm_logout(); g_sig=-1; }
static void on_toggle(lv_event_t *e){
    lv_obj_t *sw = lv_event_get_target(e);
    lastfm_set_enabled(lv_obj_has_state(sw, LV_STATE_CHECKED));
}

static void rebuild(void){
    if(!g_body) return;
    lv_obj_clean(g_body);
    int st = lastfm_auth_state();

    if(lastfm_connected()){
        char b[180]; snprintf(b, sizeof b, "Connected as\n%s", lastfm_username());
        body_label(b, TF(UI_20), THEME_CLR_TEXT_PRIMARY);
        /* Scrobbling on/off */
        lv_obj_t *row = lv_obj_create(g_body);
        lv_obj_remove_style_all(row); lv_obj_set_size(row, 210, 40);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_t *rl = lv_label_create(row);
        lv_label_set_text(rl, "Scrobbling");
        lv_obj_set_style_text_font(rl, TF(UI_18), 0);
        lv_obj_set_style_text_color(rl, TC(TEXT_LYRICS), 0);
        lv_obj_t *sw = lv_switch_create(row);
        if(lastfm_enabled()) lv_obj_add_state(sw, LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(sw, TC(FIXED_LASTFM), LV_PART_INDICATOR | LV_STATE_CHECKED);
        ui_on(sw, on_toggle, LV_EVENT_VALUE_CHANGED, NULL, "lastfm_ui.on_toggle.value", UI_CORE);
        int q = lastfm_queue_count();
        if(q > 0){ char qb[48]; snprintf(qb,sizeof qb,"%d scrobble%s queued", q, q==1?"":"s");
                   body_label(qb, TF(UI_14), THEME_CLR_TEXT_MUTED); }
        body_button("Disconnect", on_disconnect);
    }
    else if(st==LFM_AUTH_WAIT && lastfm_auth_url()[0]){
        body_label("Scan, then tap \"Allow\"\non last.fm", TF(UI_18), THEME_CLR_TEXT_PRIMARY);
        body_qr(lastfm_auth_url());
        body_label("Waiting for approval...", TF(UI_14), THEME_CLR_TEXT_MUTED);
    }
    else if(st==LFM_AUTH_TOKEN){
        body_label("Connecting...", TF(UI_20), THEME_CLR_TEXT_PRIMARY);
    }
    else if(lastfm_setup_url()[0]){
        body_label("Scan with your phone\nto enter your Last.fm key", TF(UI_18), THEME_CLR_TEXT_PRIMARY);
        body_qr(lastfm_setup_url());
        body_label("Same Wi-Fi as this player", TF(UI_14), THEME_CLR_TEXT_MUTED);
    }
    else if(st==LFM_AUTH_ERR){
        body_label("Couldn't connect.\nTry again.", TF(UI_18), THEME_CLR_TEXT_PRIMARY);
        body_button(lastfm_has_creds()?"Retry":"Set up", lastfm_has_creds()?on_connect:on_setup);
    }
    else if(lastfm_has_creds()){
        body_label("Authorize this player\non your Last.fm account", TF(UI_18), THEME_CLR_TEXT_PRIMARY);
        body_button("Connect account", on_connect);
        body_button("Re-enter keys", on_setup);
    }
    else {
        body_label("Scrobble the songs you play\nto your Last.fm profile", TF(UI_18), THEME_CLR_TEXT_PRIMARY);
        body_button("Set up", on_setup);
        body_label("Beta: connect flow not fully\ntested yet. Key setup uses your\nlocal Wi-Fi (plain HTTP).",
                   TF(UI_14), THEME_CLR_TEXT_MUTED);
    }
}

/* signature of everything the view depends on -> rebuild only when it changes */
static int state_sig(void){
    return lastfm_connected()*1000000 + lastfm_auth_state()*100000 + lastfm_has_creds()*10000
         + (lastfm_setup_url()[0]?1:0)*1000 + lastfm_enabled()*100 + lastfm_queue_count();
}
static void refresh_cb(lv_timer_t *t){
    (void)t;
    if(screen_current()!=SCR_LASTFM){ lastfm_setup_stop(); g_sig=-12345; return; }
    int s = state_sig();
    if(s != g_sig){ g_sig = s; rebuild(); }
}

void lastfm_create(lv_obj_t *root){
    ui_header_cb(root, "Last.fm (beta)", go_back);   /* shared header; go_back stops setup then pops */
    /* body: a centered vertical flex column in the clear middle band */
    g_body = lv_obj_create(root);
    lv_obj_remove_style_all(g_body);
    lv_obj_set_size(g_body, 300, 250);
    lv_obj_set_style_pad_bottom(g_body, 44, 0);   /* last row scrolls clear of the round bezel */
    lv_obj_align(g_body, LV_ALIGN_TOP_MID, 0, 74);
    lv_obj_set_flex_flow(g_body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(g_body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(g_body, 12, 0);
    lv_obj_set_scroll_dir(g_body, LV_DIR_VER);
    if(!g_refresh) g_refresh = lv_timer_create(refresh_cb, 500, NULL);
}
void lastfm_open(void){
    screen_show(SCR_LASTFM);
    g_sig = -12345; rebuild(); g_sig = state_sig();
}
