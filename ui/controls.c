/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
/* Screen rotation and volume-key actions (see controls.h for the stock facts this is built on). */
#include "controls.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "sqlite3.h"

int ctl_supported_ver(int os_ver){ return os_ver == 257; }

/* ---- rotation ---------------------------------------------------------------------------------------------------- */
/* Stock value -> clockwise degrees (mq_ui 0x420e24: value 0,1,2,3 -> lv rotation 2,3,0,1). */
static const int ROT_DEG[CTL_ROT_N] = { 180, 270, 0, 90 };
static int g_rot;   /* the value the display is using; 0 = the panel's mounting, as before this setting existed */

int ctl_rot_deg(int v){ return (v >= 0 && v < CTL_ROT_N) ? ROT_DEG[v] : 180; }
int ctl_rot_get(void){ return g_rot; }
void ctl_rot_set(int v){ if(v >= 0 && v < CTL_ROT_N) g_rot = v; }

/* A frame turned d degrees clockwise: logical (x,y) lands on raw (rx,ry). */
void ctl_rot_map(int v, int w, int h, int x, int y, int *rx, int *ry){
    int d = ctl_rot_deg(v);
    if(w != h && (d == 90 || d == 270)) d = 180;
    switch(d){
        case 0:   *rx = x;         *ry = y;         break;
        case 90:  *rx = h - 1 - y; *ry = x;         break;
        case 270: *rx = y;         *ry = w - 1 - x; break;
        default:  *rx = w - 1 - x; *ry = h - 1 - y; break;
    }
}

/* The inverse of ctl_rot_map, expressed as evdev swap + calibration (raw range 0..size-1 -> logical 0..size-1). */
void ctl_rot_touch_cal(int v, int size, int *swap, int *min_x, int *min_y, int *max_x, int *max_y){
    int e = size - 1;
    switch(ctl_rot_deg(v)){
        case 0:   *swap = 0; *min_x = 0; *min_y = 0; *max_x = e; *max_y = e; break;   /* x = rx,     y = ry     */
        case 90:  *swap = 1; *min_x = 0; *min_y = e; *max_x = e; *max_y = 0; break;   /* x = ry,     y = e - rx */
        case 270: *swap = 1; *min_x = e; *min_y = 0; *max_x = 0; *max_y = e; break;   /* x = e - ry, y = rx     */
        default:  *swap = 0; *min_x = e; *min_y = e; *max_x = 0; *max_y = 0; break;   /* x = e - rx, y = e - ry */
    }
}

void ctl_blit(uint8_t *base, uint32_t line_bytes, int w, int h, int v, const uint32_t *src, int x1, int y1, int x2, int y2){
    if(!base || !src || x1 < 0 || y1 < 0 || x2 >= w || y2 >= h || x1 > x2 || y1 > y2) return;
    int d = ctl_rot_deg(v);
    if(w != h && (d == 90 || d == 270)) d = 180;
    const int aw = x2 - x1 + 1;
    /* The turn is chosen once per call, not per row; each loop indexes the real start of its source row. */
    switch(d){
        case 0:
            for(int sy = y1; sy <= y2; sy++)
                memcpy(base + (size_t)sy * line_bytes + (size_t)x1 * 4, src + (size_t)(sy - y1) * aw, (size_t)aw * 4);
            break;
        case 90:
            for(int sy = y1; sy <= y2; sy++){
                const uint32_t *row = src + (size_t)(sy - y1) * aw;
                for(int sx = x1; sx <= x2; sx++)
                    ((uint32_t*)(base + (size_t)sx * line_bytes))[h - 1 - sy] = row[sx - x1];
            }
            break;
        case 270:
            for(int sy = y1; sy <= y2; sy++){
                const uint32_t *row = src + (size_t)(sy - y1) * aw;
                for(int sx = x1; sx <= x2; sx++)
                    ((uint32_t*)(base + (size_t)(w - 1 - sx) * line_bytes))[sy] = row[sx - x1];
            }
            break;
        default:   /* 180, the panel's mounting and the default: the old flush's loop, row flip + column flip */
            for(int sy = y1; sy <= y2; sy++){
                const uint32_t *row = src + (size_t)(sy - y1) * aw;
                uint32_t *dst = (uint32_t*)(base + (size_t)(h - 1 - sy) * line_bytes);
                for(int sx = x1; sx <= x2; sx++) dst[w - 1 - sx] = row[sx - x1];   /* the old flush's loop */
            }
            break;
    }
}

/* ---- rotation rollback: a new orientation stays only if confirmed ----------------------------------------------- */
void ctl_rb_begin(ctl_rb_t *r, int prev, int cur, uint32_t now){
    if(!r->pending) r->prev = prev;          /* a second change while waiting still rolls back to the confirmed one */
    r->cur = cur; r->start = now; r->pending = 1;
}
int ctl_rb_left_ms(const ctl_rb_t *r, uint32_t now){
    if(!r->pending) return 0;
    int32_t used = (int32_t)(now - r->start);
    return used >= CTL_ROLLBACK_MS ? 0 : CTL_ROLLBACK_MS - (used < 0 ? 0 : used);
}
int ctl_rb_expired(ctl_rb_t *r, uint32_t now, int *revert_to){
    if(!r->pending || ctl_rb_left_ms(r, now) > 0) return 0;
    r->pending = 0; if(revert_to) *revert_to = r->prev;
    return 1;
}
int ctl_rb_keep(ctl_rb_t *r, int *value){ if(!r->pending) return 0; r->pending = 0; if(value) *value = r->cur; return 1; }
int ctl_rb_revert(ctl_rb_t *r, int *value){ if(!r->pending) return 0; r->pending = 0; if(value) *value = r->prev; return 1; }

/* ---- keys -------------------------------------------------------------------------------------------------------- */
static const char *const KEY_TAG[CTL_KEY_N] = { "0820", "0821", "0822" };
static const char *const KEY_COL[CTL_KEY_N] = { "KEY_SINGLE_CLICK_SLE", "KEY_DOUBLE_CLICK_SLE", "KEY_LONG_PRESS_SLE" };
const char *ctl_key_tag(int key){ return (key >= 0 && key < CTL_KEY_N) ? KEY_TAG[key] : NULL; }
const char *ctl_key_column(int key){ return (key >= 0 && key < CTL_KEY_N) ? KEY_COL[key] : NULL; }
ctl_effect_t ctl_key_effect(int action){ return action == CTL_ACT_VOLUME ? CTL_DO_VOLUME : CTL_DO_TRACK; }

/* ---- frames and writes ------------------------------------------------------------------------------------------- */
int ctl_frame_rot(char *buf, size_t n, int v){
    if(v < 0 || v >= CTL_ROT_N) return -1;
    return snprintf(buf, n, "0646000C%04X", (unsigned)v) > 0 ? 0 : -1;
}
int ctl_frame_key(char *buf, size_t n, int key, int act){
    const char *tag = ctl_key_tag(key);
    if(!tag || (act != CTL_ACT_TRACK && act != CTL_ACT_VOLUME)) return -1;
    return snprintf(buf, n, "%s000C%04X", tag, (unsigned)act) > 0 ? 0 : -1;
}
int ctl_apply_rot_via(int (*send)(const char *), int v){
    char f[24];
    if(!send || ctl_frame_rot(f, sizeof f, v) != 0) return -1;
    return send(f) == 0 ? 0 : -1;
}
int ctl_apply_key_via(int (*send)(const char *), int key, int act){
    char f[24];
    if(!send || ctl_frame_key(f, sizeof f, key, act) != 0) return -1;
    return send(f) == 0 ? 0 : -1;
}

/* ---- the player's stored values ---------------------------------------------------------------------------------- */
int ctl_read_state(const char *db_path, ctl_state_t *st){
    if(!st) return -1;
    st->rot = -1; for(int i = 0; i < CTL_KEY_N; i++) st->key[i] = -1;
    if(!db_path) return -1;
    sqlite3 *c = NULL; int got = -1;
    if(sqlite3_open_v2(db_path, &c, SQLITE_OPEN_READONLY, NULL) == SQLITE_OK){
        sqlite3_busy_timeout(c, 200);
        sqlite3_stmt *s = NULL;
        if(sqlite3_prepare_v2(c, "SELECT SCREEN_ROT,KEY_SINGLE_CLICK_SLE,KEY_DOUBLE_CLICK_SLE,KEY_LONG_PRESS_SLE "
                                 "FROM SYSCONFIG WHERE ID=1;", -1, &s, NULL) == SQLITE_OK){
            if(sqlite3_step(s) == SQLITE_ROW){
                int *dst[4] = { &st->rot, &st->key[0], &st->key[1], &st->key[2] };
                for(int i = 0; i < 4; i++)
                    if(sqlite3_column_type(s, i) == SQLITE_INTEGER){ int x = sqlite3_column_int(s, i); if(x >= 0){ *dst[i] = x; got = 0; } }
            }
            sqlite3_finalize(s);
        }
    }
    if(c) sqlite3_close(c);
    return got;
}

void ctl_mirror_via(const ctl_state_t *st, void (*set_int)(const char *key, int v)){
    static const char *const CFG[CTL_KEY_N] = { "key_single", "key_double", "key_long" };
    if(!st || !set_int) return;
    set_int(CTL_CFG_ROT, g_rot);   /* what is applied (the stored value, or the default when it could not be read) */
    for(int i = 0; i < CTL_KEY_N; i++)   /* the player treats exactly 1 as volume: any other stored value is not shown as a choice */
        if(st->key[i] == CTL_ACT_TRACK || st->key[i] == CTL_ACT_VOLUME) set_int(CFG[i], st->key[i]);
}

#ifndef CTL_CORE_ONLY
#include "config.h"
#include "fwcaps.h"
#include "ipc.h"
#include "fb_pan.h"
int ctl_supported(void){ return ctl_supported_ver(fw_os_ver()); }

void controls_boot_rotation(void){
    ctl_state_t st;
    if(ctl_supported() && ctl_read_state(CTL_SYSCONFIG, &st) == 0 && st.rot >= 0) ctl_rot_set(st.rot);
}
static void cfg_put(const char *key, int v){ if(cfg_get_int(key, -1) != v) cfg_set_int(key, v); }
void controls_startup_mirror(void){
    if(!ctl_supported()) return;
    ctl_state_t st;
    ctl_read_state(CTL_SYSCONFIG, &st);   /* unreadable values stay -1: the rotation shown is the one applied */
    ctl_mirror_via(&st, cfg_put);
}

/* Screen Rotation: the picture and touch turn at once, but nothing is stored until the user taps Keep. A prompt drawn in
 * the NEW orientation counts down; on timeout or Revert the old rotation comes back. The player's
 * stored value (0646) is only written on Keep, so a crash or reboot while the prompt is up also lands on the old one. */
static void ctl_prompt_show(void);
static void ctl_prompt_hide(void);
static uint32_t ctl_now(void);
static ctl_rb_t g_rb;

static void ctl_do_revert(void){
    int back;
    if(!ctl_rb_revert(&g_rb, &back)) return;
    fbpan_set_rotation(back);
    cfg_set_int(CTL_CFG_ROT, back);
    ctl_prompt_hide();
}
void ctl_prompt_keep(void){
    int v;
    if(!ctl_rb_keep(&g_rb, &v)) return;
    ctl_prompt_hide();
    if(ctl_apply_rot_via(ipc_send_cmd, v) != 0){      /* the player refused: don't leave a picture it won't remember */
        fbpan_set_rotation(g_rb.prev);
        cfg_set_int(CTL_CFG_ROT, g_rb.prev);
        return;
    }
    cfg_set_int(CTL_CFG_ROT, v);
}
void ctl_prompt_revert(void){ ctl_do_revert(); }
void controls_tick(void){
    int back;
    if(ctl_rb_expired(&g_rb, ctl_now(), &back)){
        fbpan_set_rotation(back);
        cfg_set_int(CTL_CFG_ROT, back);
        ctl_prompt_hide();
    }
}
int ctl_rotation_pending(void){ return g_rb.pending; }

int ui_set_screen_rot(int v){
    if(!ctl_supported()){ fprintf(stderr, "screen rotation: unverified firmware (MAIN_OS_VER=%d)\n", fw_os_ver()); return -1; }
    if(v < 0 || v >= CTL_ROT_N) return -1;
    if(v == ctl_rot_get()) return 0;
    ctl_rb_begin(&g_rb, ctl_rot_get(), v, ctl_now());
    fbpan_set_rotation(v);
    ctl_prompt_show();
    return 0;
}
int ui_set_volume_key(int key, int act){
    if(!ctl_supported()){ fprintf(stderr, "volume keys: unverified firmware (MAIN_OS_VER=%d)\n", fw_os_ver()); return -1; }
    return ctl_apply_key_via(ipc_send_cmd, key, act);
}
#endif

#if !defined(CTL_NO_UI) && !defined(CTL_CORE_ONLY)
#include "i18n.h"
#include "theme_kit.h"
#include "screens.h"
#include "theme.h"
static lv_obj_t *g_pr, *g_pr_count;
static lv_timer_t *g_pr_timer;
static uint32_t ctl_now(void){ return lv_tick_get(); }
static void pr_tick(lv_timer_t *t){
    (void)t;
    controls_tick();
    if(g_pr_count){
        lv_label_set_text_fmt(g_pr_count, "%s %ds", tr("Reverting in"),
                              (ctl_rb_left_ms(&g_rb, ctl_now()) + 999) / 1000);
    }
}
static void pr_keep_cb(lv_event_t *e){ (void)e; ctl_prompt_keep(); }
static void pr_revert_cb(lv_event_t *e){ (void)e; ctl_prompt_revert(); }
static void ctl_prompt_hide(void){
    if(g_pr_timer){ lv_timer_delete(g_pr_timer); g_pr_timer = NULL; }
    if(g_pr){ lv_obj_delete_async(g_pr); g_pr = NULL; g_pr_count = NULL; }
}
static lv_obj_t *pr_button(lv_obj_t *par, const char *txt, int dx, lv_event_cb_t cb){
    lv_obj_t *b = lv_button_create(par);
    lv_obj_set_size(b, 110, 48);
    lv_obj_align(b, LV_ALIGN_CENTER, dx, 60);
    lv_obj_set_style_bg_color(b, TC(SURFACE_RAISED), 0);
    lv_obj_set_style_radius(b, 24, 0);
    ui_on(b, cb, LV_EVENT_CLICKED, NULL, "controls.rotation_prompt", UI_CORE);   /* ui_on: a back swipe starting on Keep (x 63) must not click it */
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, tr(txt));
    lv_obj_set_style_text_font(l, TF(USER_16), 0);
    lv_obj_set_style_text_color(l, TC(TEXT_PRIMARY), 0);
    lv_obj_center(l);
    return b;
}
static void ctl_prompt_show(void){
    ctl_prompt_hide();
    g_pr = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(g_pr);
    lv_obj_set_size(g_pr, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(g_pr, TC(SURFACE), 0);
    lv_obj_set_style_bg_opa(g_pr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(g_pr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *t = lv_label_create(g_pr);
    lv_label_set_text(t, tr("Keep this orientation?"));
    lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(t, 240);
    lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(t, TF(USER_18), 0);
    lv_obj_set_style_text_color(t, TC(TEXT_PRIMARY), 0);
    lv_obj_align(t, LV_ALIGN_CENTER, 0, -40);
    g_pr_count = lv_label_create(g_pr);
    lv_obj_set_style_text_font(g_pr_count, TF(USER_14), 0);
    lv_obj_set_style_text_color(g_pr_count, TC(TEXT_SECONDARY), 0);
    lv_obj_align(g_pr_count, LV_ALIGN_CENTER, 0, 0);
    pr_button(g_pr, "Keep", -62, pr_keep_cb);
    pr_button(g_pr, "Revert", 62, pr_revert_cb);
    g_pr_timer = lv_timer_create(pr_tick, 250, NULL);
    pr_tick(NULL);
}
#endif
