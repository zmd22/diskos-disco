/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "screens.h"
#include "theme_kit.h"
#include "theme.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "zune_dial.h"
#include "lvgl/src/core/lv_obj_event_private.h"

/* Text entry (Wi-Fi password, MA Sendspin server and name, playlist names): a full-screen modal with Search's
 * field and big keyboard. */

/* ---- the big keyboard (Search's look: big keys in the wide middle of the circle) ------------------------
 * Four rows of 7/8/7/6 keys, 46 px tall, alphabetical like Search's. Pages: abc, ABC (Aa), 123 and #+= - between
 * them every printable ASCII character, so any Wi-Fi password can be typed. The last row ends in space and
 * Backspace (hold = clear all); Aa and 123 sit at the bottom like Search's mode key. */
#define BK_H 46
#define BK_P 50
#define BK_Y 116
static const int BKN[4] = { 7, 8, 7, 6 };
static const int BKW[4] = { 42, 39, 40, 36 };
#define BK_BKSP '\b'
static const char *const BK_LOW[4] = { "abcdefg", "hijklmno", "pqrstuv", "wxyz \b" };
static const char *const BK_UP [4] = { "ABCDEFG", "HIJKLMNO", "PQRSTUV", "WXYZ \b" };
static const char *const BK_NUM[4] = { "1234567", "890'&-.!", "?,:()/+", "#@\"* \b" };
static const char *const BK_SYM[4] = { "[]{}%^=", "_\\|~<>;$", "`'.,!?-", "@#* \b" };
typedef struct { lv_obj_t *ta, *key[4][8], *lbl[4][8], *shift_lbl, *mode_lbl, *shift_btn; int page; void (*submit)(void); } bkb_t;
static bkb_t g_bk;                                    /* one keyboard at a time (the modal) */
static const char *const *bk_set(void){ return g_bk.page == 1 ? BK_UP : g_bk.page == 2 ? BK_NUM : g_bk.page == 3 ? BK_SYM : BK_LOW; }
static void bk_paint(void){
    const char *const *set = bk_set();
    for(int r = 0; r < 4; r++){
        int cnt = (int)strlen(set[r]), x = 180 - (cnt * BKW[r] + (cnt - 1) * 4) / 2;
        for(int c = 0; c < 8; c++){
            lv_obj_t *k = g_bk.key[r][c]; if(!k) continue;
            if(c >= cnt){ lv_obj_add_flag(k, LV_OBJ_FLAG_HIDDEN); continue; }
            char ch = set[r][c];
            lv_obj_remove_flag(k, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_user_data(k, (void *)(uintptr_t)(unsigned char)ch);
            lv_obj_set_pos(k, x, BK_Y + r * BK_P); x += BKW[r] + 4;
            lv_obj_t *l = g_bk.lbl[r][c]; char t[2] = { ch, 0 };
            if(ch == ' '){ lv_label_set_text(l, "space"); lv_obj_set_style_text_font(l, TF(UI_12), 0); lv_obj_set_style_text_color(l, TC(TEXT_SECONDARY), 0); }
            else if(ch == BK_BKSP){ lv_label_set_text(l, LV_SYMBOL_BACKSPACE); lv_obj_set_style_text_font(l, TF(UI_18), 0); lv_obj_set_style_text_color(l, TC(TEXT_SECONDARY), 0); }
            else { lv_label_set_text(l, t); lv_obj_set_style_text_font(l, TF(UI_24), 0); lv_obj_set_style_text_color(l, TC(TEXT_PRIMARY), 0); }
        }
    }
    int sym = g_bk.page >= 2;
    lv_label_set_text(g_bk.shift_lbl, sym ? (g_bk.page == 2 ? "#+=" : "123") : "Aa");
    lv_label_set_text(g_bk.mode_lbl, sym ? "abc" : "123");
    lv_obj_set_style_bg_color(g_bk.shift_btn, g_bk.page == 1 ? ui_current_accent() : TC(SURFACE), 0);
    lv_obj_set_style_text_color(g_bk.shift_lbl, g_bk.page == 1 ? TC(ON_ACCENT) : TC(TEXT_SECONDARY), 0);
}
static void bk_key_cb(lv_event_t *e){
    lv_event_code_t code = lv_event_get_code(e);
    char ch = (char)(uintptr_t)lv_obj_get_user_data(lv_event_get_current_target(e));
    if(!g_bk.ta) return;
    if(code == LV_EVENT_LONG_PRESSED){ if(ch == BK_BKSP) lv_textarea_set_text(g_bk.ta, ""); return; }   /* hold Backspace: clear all */
    if(code != LV_EVENT_SHORT_CLICKED) return;
    if(ch == BK_BKSP){ lv_textarea_delete_char(g_bk.ta); return; }
    char t[2] = { ch, 0 }; lv_textarea_add_text(g_bk.ta, t);
    if(g_bk.page == 1){ g_bk.page = 0; bk_paint(); }        /* one capital, then back to small letters */
}
static void bk_shift_cb(lv_event_t *e){ (void)e; g_bk.page = g_bk.page == 0 ? 1 : g_bk.page == 1 ? 0 : g_bk.page == 2 ? 3 : 2; bk_paint(); }
void kbinput_test_page(void){ g_bk.page = (g_bk.page + 1) % 4; bk_paint(); }   /* host renders */
static void bk_mode_cb(lv_event_t *e){ (void)e; g_bk.page = g_bk.page >= 2 ? 0 : 2; bk_paint(); }
static void bk_style(lv_obj_t *k, int radius){
    lv_obj_remove_style_all(k);
    lv_obj_set_style_radius(k, radius, 0);
    lv_obj_set_style_bg_color(k, TC(SURFACE), 0);
    lv_obj_set_style_bg_opa(k, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(k, TC(SURFACE_RAISED), LV_STATE_PRESSED);
}
static lv_obj_t *bk_small(lv_obj_t *parent, int x, int w, lv_event_cb_t cb, lv_obj_t **lbl){
    lv_obj_t *b = lv_button_create(parent);
    bk_style(b, 14);
    lv_obj_set_size(b, w, 28); lv_obj_align(b, LV_ALIGN_TOP_MID, x, 318);
    lv_obj_set_ext_click_area(b, 6);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    *lbl = lv_label_create(b);
    lv_obj_set_style_text_font(*lbl, TF(UI_14), 0);
    lv_obj_set_style_text_color(*lbl, TC(TEXT_SECONDARY), 0);
    lv_obj_center(*lbl);
    return b;
}
static void bkb_create(lv_obj_t *parent, lv_obj_t *ta, void (*submit)(void)){
    memset(&g_bk, 0, sizeof g_bk);
    g_bk.ta = ta; g_bk.submit = submit;
    for(int r = 0; r < 4; r++) for(int c = 0; c < BKN[r]; c++){
        lv_obj_t *k = lv_button_create(parent);
        bk_style(k, 12);
        lv_obj_set_size(k, BKW[r], BK_H);
        lv_obj_add_event_cb(k, bk_key_cb, LV_EVENT_SHORT_CLICKED, NULL);
        lv_obj_add_event_cb(k, bk_key_cb, LV_EVENT_LONG_PRESSED, NULL);
        g_bk.key[r][c] = k;
        g_bk.lbl[r][c] = lv_label_create(k); lv_obj_center(g_bk.lbl[r][c]);
    }
    g_bk.shift_btn = bk_small(parent, -28, 48, bk_shift_cb, &g_bk.shift_lbl);
    bk_small(parent, 26, 48, bk_mode_cb, &g_bk.mode_lbl);
    bk_paint();
}

/* ---- modal wrapper ------------------------------------------------------- */
static lv_obj_t *g_modal, *g_ta;
static kbinput_done_cb_t g_done;
static int g_keep_spaces;   /* password entry: trailing spaces are part of the secret, never stripped */

int kbinput_active(void){ return g_modal != NULL; }

static void finish(const char *result){
    kbinput_done_cb_t cb = g_done; g_done = NULL;
    if(g_modal){ lv_obj_delete_async(g_modal); g_modal = NULL; g_ta = NULL; g_bk.ta = NULL; }
    if(cb) cb(result);
}
static void do_save(void){
    static char buf[256];                                     /* a whole file name (255 bytes) fits */
    const char *txt = g_ta ? lv_textarea_get_text(g_ta) : "";
    if(strlen(txt) >= sizeof buf){ ui_toast("That's too long"); return; }   /* never save a silently cut text */
    snprintf(buf, sizeof buf, "%s", txt);
    int n = (int)strlen(buf);
    while(!g_keep_spaces && n > 0 && buf[n-1]==' ') buf[--n] = 0;
    finish(buf); /* saved empty string is distinct from NULL (cancel) */
}
static void save_btn(lv_event_t *e){ if(lv_event_get_code(e)==LV_EVENT_CLICKED) do_save(); }
static void cancel_btn(lv_event_t *e){ if(lv_event_get_code(e)==LV_EVENT_CLICKED) finish(NULL); }

static void pill(lv_obj_t *parent, int x, int y, const char *sym, lv_color_t col, lv_event_cb_t cb){
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, 88, 38);
    lv_obj_align(b, LV_ALIGN_TOP_MID, x, y);
    lv_obj_set_ext_click_area(b, 8);   /* Save/Cancel sit in open space - generous hit area */
    lv_obj_set_style_radius(b, 20, 0);
    lv_obj_set_style_bg_color(b, TC(SURFACE), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, TC(CONTROL_TRACK), LV_STATE_PRESSED);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, sym);
    lv_obj_set_style_text_color(l, col, 0);
    lv_obj_center(l);
}

/* set by kbinput_open_password() for the next open only, then consumed/reset. */
static int g_mask_next = 0;
void kbinput_open_password(const char *title, const char *initial, kbinput_done_cb_t cb){
    g_mask_next = 1;
    kbinput_open(title, initial, cb);
}
/* a password field: the eye at its right end shows or hides what was typed (Zune has its own "show") */
static void eye_cb(lv_event_t *e){
    lv_obj_t *l = lv_obj_get_child(lv_event_get_target_obj(e), 0);
    if(!g_ta || !l) return;
    int masked = lv_textarea_get_password_mode(g_ta);
    lv_textarea_set_password_mode(g_ta, !masked);
    lv_label_set_text(l, masked ? LV_SYMBOL_EYE_CLOSE : LV_SYMBOL_EYE_OPEN);
}
void kbinput_open(const char *title, const char *initial, kbinput_done_cb_t cb){
    if(g_modal) finish(NULL);
    g_done = cb;

    g_modal = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(g_modal);
    lv_obj_set_size(g_modal, 360, 360);
    lv_obj_center(g_modal);
    lv_obj_set_style_bg_color(g_modal, TC(CANVAS), 0);
    lv_obj_set_style_bg_opa(g_modal, LV_OPA_COVER, 0);
    lv_obj_clear_flag(g_modal, LV_OBJ_FLAG_SCROLLABLE);

    g_ta = lv_textarea_create(g_modal);
    lv_textarea_set_one_line(g_ta, true);
    lv_obj_set_scroll_dir(g_ta, LV_DIR_HOR);   /* one line: allow only the cursor-follow HORIZONTAL scroll - no vertical jitter */
    lv_obj_set_scrollbar_mode(g_ta, LV_SCROLLBAR_MODE_OFF);
    if(title && title[0]) lv_textarea_set_placeholder_text(g_ta, title);
    if(initial && initial[0]) lv_textarea_set_text(g_ta, initial);
    g_keep_spaces = g_mask_next;
    if(g_mask_next){ lv_textarea_set_password_mode(g_ta, true); g_mask_next = 0; }  /* masked secret entry */
    lv_obj_set_size(g_ta, 212, 40);                          /* Search's field look: a pill near the top, inside the circle */
    lv_obj_align(g_ta, LV_ALIGN_TOP_MID, 0, 22);
    lv_obj_set_style_radius(g_ta, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_left(g_ta, 18, 0); lv_obj_set_style_pad_right(g_ta, 18, 0);
    lv_obj_set_style_bg_color(g_ta, TC(SURFACE), 0);
    lv_obj_set_style_text_color(g_ta, TC(TEXT_PRIMARY), 0);
    /* typed text and the placeholder ("Password for <SSID>", a playlist name on Rename) are user
     * data: route both parts through the chain so Cyrillic/CJK don't tofu (issue #3). */
    lv_obj_set_style_text_font(g_ta, TF(USER_16), 0);
    lv_obj_set_style_text_font(g_ta, TF(USER_16), LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_style_border_width(g_ta, 0, 0);
    /* Pin the internal label to a FIXED one-line height so the textarea's vertical centering can't drift
     * as the measured text extent (ascenders/descenders/cursor) changes per keystroke -> no vertical bounce. */
    lv_obj_set_style_pad_top(g_ta, 8, 0); lv_obj_set_style_pad_bottom(g_ta, 8, 0);   /* 40 - 24 line = 16 -> 8/8 centres one full line */
    { lv_obj_t *ta_lbl = lv_textarea_get_label(g_ta);   /* pin to the FONT's 24px line height so the text can't clip and centring can't drift */
      if(ta_lbl){ lv_obj_set_style_min_height(ta_lbl, 24, 0); lv_obj_set_style_max_height(ta_lbl, 24, 0); } }

    if(th_zune()){ void zkb_build(lv_obj_t *modal, lv_obj_t *ta, const char *title, int password); zkb_build(g_modal, g_ta, title, g_keep_spaces); return; }
    /* Cancel / Save in the wide mid-band, above the keyboard */
    /* Cancel and Save between the field and the keys, where Search has its Search button */
    pill(g_modal, -50, 70, LV_SYMBOL_CLOSE, TC(TEXT_SECONDARY), cancel_btn);
    pill(g_modal,  50, 70, LV_SYMBOL_OK,    TC(STATUS_SUCCESS), save_btn);
    if(g_keep_spaces){                                       /* password: an eye inside the field's right end */
        lv_obj_set_style_pad_right(g_ta, 40, 0);
        lv_obj_t *b = lv_button_create(g_modal); lv_obj_remove_style_all(b);
        lv_obj_set_size(b, 36, 36); lv_obj_align_to(b, g_ta, LV_ALIGN_RIGHT_MID, -4, 0);
        lv_obj_set_ext_click_area(b, 6);
        lv_obj_add_event_cb(b, eye_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_t *l = lv_label_create(b); lv_label_set_text(l, LV_SYMBOL_EYE_OPEN);
        lv_obj_set_style_text_font(l, TF(UI_16), 0); lv_obj_set_style_text_color(l, TC(TEXT_SECONDARY), 0); lv_obj_center(l);
    }

    bkb_create(g_modal, g_ta, do_save);
}

/* ==== Zune (Figma 08): the letters round the rim - tap one, it lights in the accent; the field in the middle on a
 * hairline, what it's for above it; Aa, 123, backspace and done as four discs; cancel (and show, for a password) ==== */
LV_FONT_DECLARE(font_zune_light_28) LV_FONT_DECLARE(font_zune_light_32) LV_FONT_DECLARE(font_zune_regular_16) LV_FONT_DECLARE(font_zune_regular_12) LV_FONT_DECLARE(font_zune_regular_13)
LV_FONT_DECLARE(font_zune_regular_14) LV_FONT_DECLARE(font_zune_regular_17) LV_FONT_DECLARE(font_zune_regular_19)
#define ZK_MAX 32
static const char *const ZK_SET[4] = {
    "abcdefghijklmnopqrstuvwxyz-._",
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ-._",
    "0123456789 !\"#$%&'()*+,/:;<=>?",
    "@[\\]^`{|}~" };
static struct { lv_obj_t *ta, *key[ZK_MAX], *lbl[ZK_MAX], *hit[ZK_MAX], *aa, *aa_l, *mode_l, *show_l; int page, n; } ZK;
static void zk_hit_test(lv_event_t *e){
 lv_hit_test_info_t *info=lv_event_get_param(e);if(!info||!info->res)return;
 int i=(int)(intptr_t)lv_event_get_user_data(e);lv_obj_t*h=lv_event_get_target(e);lv_area_t a;lv_obj_get_coords(lv_obj_get_parent(h),&a);
 float dx=info->point->x-a.x1-180,dy=info->point->y-a.y1-180,r2=dx*dx+dy*dy,angle=atan2f(dy,dx)*57.2957795f+90;if(angle<0)angle+=360;
 int nearest=ZK.n?(int)floorf(angle*ZK.n/360+0.5f)%ZK.n:-1;info->res=r2>=124*124&&r2<=174*174&&nearest==i;
}
static void zk_paint(void){
    const char *set = ZK_SET[ZK.page]; int n = (int)strlen(set); if(n > ZK_MAX) n = ZK_MAX; ZK.n = n;
    for(int i = 0; i < ZK_MAX; i++){
        if(i >= n){ lv_obj_add_flag(ZK.key[i], LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(ZK.hit[i], LV_OBJ_FLAG_HIDDEN); continue; }
        float a = (-90.0f + 360.0f * i / n) * 0.0174533f;
        int cx = 180 + (int)lroundf(146 * cosf(a)), cy = 180 + (int)lroundf(146 * sinf(a));
        lv_obj_set_pos(ZK.key[i], cx - 14, cy - 14); lv_obj_set_pos(ZK.hit[i], cx - 22, cy - 22);
        char t[2] = { set[i], 0 };
        lv_label_set_text(ZK.lbl[i], set[i] == ' ' ? "sp" : t);              /* space: "sp", small */
        lv_obj_set_style_text_font(ZK.lbl[i], zune_font(set[i] == ' ' ? &font_zune_regular_14 : &font_zune_regular_19), 0);
        lv_obj_center(ZK.lbl[i]);
        lv_obj_set_user_data(ZK.hit[i], (void *)(uintptr_t)(unsigned char)set[i]);
        lv_obj_remove_flag(ZK.key[i], LV_OBJ_FLAG_HIDDEN); lv_obj_remove_flag(ZK.hit[i], LV_OBJ_FLAG_HIDDEN);
    }
    lv_label_set_text(ZK.mode_l, ZK.page >= 2 ? "abc" : "123");
    lv_label_set_text(ZK.aa_l, ZK.page >= 2 ? (ZK.page == 2 ? "#+=" : "123") : "Aa");
    lv_obj_set_style_bg_color(ZK.aa, ZK.page == 1 ? TC(ACCENT_PRIMARY) : TC(SURFACE), 0);
}
static void zk_key_cb(lv_event_t *e){
    lv_event_code_t c = lv_event_get_code(e);
    lv_obj_t *h = lv_event_get_current_target(e);
    int i = 0; while(i < ZK_MAX && ZK.hit[i] != h) i++;
    if(i >= ZK_MAX) return;
    if(c == LV_EVENT_PRESSED){ lv_obj_set_style_bg_opa(ZK.key[i], LV_OPA_COVER, 0); return; }
    if(c == LV_EVENT_RELEASED || c == LV_EVENT_PRESS_LOST){ lv_obj_set_style_bg_opa(ZK.key[i], LV_OPA_TRANSP, 0); return; }
    if(c != LV_EVENT_SHORT_CLICKED || !ZK.ta) return;
    char t[2] = { (char)(uintptr_t)lv_obj_get_user_data(h), 0 };
    lv_textarea_add_text(ZK.ta, t);
    if(ZK.page == 1){ ZK.page = 0; zk_paint(); }                        /* one capital, then small letters again */
}
static void zk_aa_cb(lv_event_t *e){ if(lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    ZK.page = ZK.page == 0 ? 1 : ZK.page == 1 ? 0 : ZK.page == 2 ? 3 : 2; zk_paint(); }
static void zk_mode_cb(lv_event_t *e){ if(lv_event_get_code(e) != LV_EVENT_CLICKED) return; ZK.page = ZK.page >= 2 ? 0 : 2; zk_paint(); }
static void zk_bksp_cb(lv_event_t *e){
    if(!ZK.ta) return;
    if(lv_event_get_code(e) == LV_EVENT_LONG_PRESSED) lv_textarea_set_text(ZK.ta, "");   /* hold: clear all */
    else if(lv_event_get_code(e) == LV_EVENT_SHORT_CLICKED) lv_textarea_delete_char(ZK.ta);
}
static void zk_show_cb(lv_event_t *e){
    if(lv_event_get_code(e) != LV_EVENT_CLICKED || !ZK.ta) return;
    int masked = lv_textarea_get_password_mode(ZK.ta);
    lv_textarea_set_password_mode(ZK.ta, !masked);
    if(ZK.show_l) lv_label_set_text(ZK.show_l, masked ? "hide" : "show");
}
static lv_obj_t *zk_disc(lv_obj_t *m, int cx, int cy, lv_color_t bg, lv_event_cb_t cb, const char *act, lv_obj_t **lbl, const char *txt){
    lv_obj_t *d = lv_obj_create(m); lv_obj_remove_style_all(d); kit_keep(d);
    lv_obj_set_size(d, 50, 50); lv_obj_set_pos(d, cx - 25, cy - 25);
    lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0); lv_obj_set_style_bg_color(d, bg, 0); lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
    lv_obj_remove_flag(d, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    if(txt){ lv_obj_t *l = lv_label_create(d); kit_keep(l); lv_label_set_text(l, txt);
             lv_obj_set_style_text_font(l, zune_font(&font_zune_regular_16), 0); lv_obj_set_style_text_color(l, TC(TEXT_PRIMARY), 0);
             lv_obj_center(l); if(lbl) *lbl = l; }
    lv_obj_t *h = lv_obj_create(m); lv_obj_remove_style_all(h); kit_keep(h);   /* the tap target (see Quick Settings) */
    lv_obj_set_size(h, 20, 20); lv_obj_set_pos(h, cx - 10, cy - 10); lv_obj_set_ext_click_area(h, 14);
    lv_obj_add_flag(h, LV_OBJ_FLAG_CLICKABLE); lv_obj_remove_flag(h, LV_OBJ_FLAG_SCROLLABLE);
    ui_on(h, cb, cb == zk_bksp_cb ? LV_EVENT_SHORT_CLICKED : LV_EVENT_CLICKED, NULL, act, UI_CORE);
    if(cb == zk_bksp_cb) lv_obj_add_event_cb(h, cb, LV_EVENT_LONG_PRESSED, NULL);
    return d;
}
static lv_obj_t *zk_text(lv_obj_t *m, const char *t, lv_color_t c, int cx, int y, lv_event_cb_t cb, const char *act){
    lv_obj_t *o = lv_obj_create(m); lv_obj_remove_style_all(o); kit_keep(o);
    lv_obj_set_size(o, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE); lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE); lv_obj_set_ext_click_area(o, 10);
    lv_obj_set_style_opa(o, LV_OPA_50, LV_STATE_PRESSED);
    lv_obj_t *l = lv_label_create(o); kit_keep(l); lv_label_set_text(l, t);
    lv_obj_set_style_text_font(l, zune_font(&font_zune_regular_19), 0); lv_obj_set_style_text_color(l, c, 0);
    lv_obj_update_layout(l);lv_obj_set_size(o,lv_obj_get_width(l)+16,40);lv_obj_center(l);lv_obj_align(o, LV_ALIGN_TOP_MID, cx - 180, y-4);
    ui_on(o, cb, LV_EVENT_CLICKED, NULL, act, UI_CORE);
    return l;
}
void zkb_build(lv_obj_t *m, lv_obj_t *ta, const char *title, int password){
    memset(&ZK, 0, sizeof ZK); ZK.ta = ta;
    lv_textarea_set_placeholder_text(ta, "");
    lv_obj_set_size(ta, 200, 44); lv_obj_align(ta, LV_ALIGN_TOP_MID, 0, 116);
    lv_obj_set_style_bg_opa(ta, LV_OPA_TRANSP, 0); lv_obj_set_style_pad_all(ta, 0, 0);
    lv_obj_set_style_text_font(ta, zune_font(&font_zune_light_32), 0);
    lv_obj_set_style_text_align(ta, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_bg_color(ta, TC(ACCENT_PRIMARY), LV_PART_CURSOR);
    { lv_obj_t *tl = lv_textarea_get_label(ta); if(tl){ lv_obj_set_style_min_height(tl, 0, 0); lv_obj_set_style_max_height(tl, LV_COORD_MAX, 0); } }
    lv_obj_t *ln = lv_obj_create(m); lv_obj_remove_style_all(ln);                  /* the field's hairline */
    lv_obj_set_size(ln, 180, 1); lv_obj_align(ln, LV_ALIGN_TOP_MID, 0, 166);
    lv_obj_set_style_bg_color(ln, TC(TEXT_MUTED), 0); lv_obj_set_style_bg_opa(ln, LV_OPA_COVER, 0);
    char low[160]; snprintf(low, sizeof low, "%s", title ? title : "");
    for(char *c = low; *c; c++) if(*c >= 'A' && *c <= 'Z' && (c == low || c[-1] == ' ')) *c += 32;
    lv_obj_t *t = lv_label_create(m); kit_keep(t); lv_label_set_text(t, low);
    lv_label_set_long_mode(t, LV_LABEL_LONG_DOT); lv_obj_set_width(t, 200); lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(t, zune_font(&font_zune_regular_19), 0); lv_obj_set_style_text_color(t, TC(TEXT_MUTED), 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 89);
    for(int i = 0; i < ZK_MAX; i++){
        lv_obj_t *k = lv_obj_create(m); lv_obj_remove_style_all(k); kit_keep(k);
        lv_obj_set_size(k, 28, 28); lv_obj_set_style_radius(k, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(k, TC(ACCENT_PRIMARY), 0); lv_obj_set_style_bg_opa(k, LV_OPA_TRANSP, 0);
        lv_obj_remove_flag(k, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        ZK.key[i] = k;
        ZK.lbl[i] = lv_label_create(k); kit_keep(ZK.lbl[i]); lv_obj_set_style_text_color(ZK.lbl[i], TC(TEXT_PRIMARY), 0);
        lv_obj_t *h = lv_obj_create(m); lv_obj_remove_style_all(h); kit_keep(h);
        lv_obj_set_size(h, 44, 44); lv_obj_set_ext_click_area(h, 4);lv_obj_add_flag(h,LV_OBJ_FLAG_ADV_HITTEST);lv_obj_add_event_cb(h,zk_hit_test,LV_EVENT_HIT_TEST,(void*)(intptr_t)i);
        lv_obj_add_flag(h, LV_OBJ_FLAG_CLICKABLE); lv_obj_remove_flag(h, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(h, zk_key_cb, LV_EVENT_ALL, NULL);
        ZK.hit[i] = h;
    }
    ZK.aa = zk_disc(m, 99, 214, TC(SURFACE), zk_aa_cb, "kb.zune.shift", &ZK.aa_l, "Aa");
    zk_disc(m, 153, 214, TC(SURFACE), zk_mode_cb, "kb.zune.mode", &ZK.mode_l, "123");
    lv_obj_t *bk = zk_disc(m, 207, 214, TC(SURFACE), zk_bksp_cb, "kb.zune.backspace", NULL, NULL);
    static const lv_point_precise_t BX[6] = { { 9, 11 }, { 15, 5 }, { 27, 5 }, { 27, 17 }, { 15, 17 }, { 9, 11 } };   /* a drawn backspace */
    lv_obj_t *bl = lv_line_create(bk); lv_line_set_points(bl, BX, 6); lv_obj_set_pos(bl, 7, 14);
    lv_obj_set_style_line_width(bl, 2, 0); lv_obj_set_style_line_rounded(bl, true, 0); lv_obj_set_style_line_color(bl, TC(TEXT_PRIMARY), 0);
    static const lv_point_precise_t BXX[2][2] = { { { 18, 8 }, { 24, 14 } }, { { 24, 8 }, { 18, 14 } } };
    for(int k = 0; k < 2; k++){ lv_obj_t *x = lv_line_create(bk); lv_line_set_points(x, BXX[k], 2); lv_obj_set_pos(x, 7, 14);
        lv_obj_set_style_line_width(x, 2, 0); lv_obj_set_style_line_rounded(x, true, 0); lv_obj_set_style_line_color(x, TC(TEXT_PRIMARY), 0); }
    lv_obj_t *dn = zk_disc(m, 261, 214, TC(ACCENT_PRIMARY), save_btn, "kb.zune.done", NULL, NULL);
    static const lv_point_precise_t CK[3] = { { 17, 25 }, { 22, 30 }, { 33, 19 } };
    lv_obj_t *ck = lv_line_create(dn); lv_line_set_points(ck, CK, 3);
    lv_obj_set_style_line_width(ck, 3, 0); lv_obj_set_style_line_rounded(ck, true, 0); lv_obj_set_style_line_color(ck, TC(ON_ACCENT), 0);
    if(password){ zk_text(m, "cancel", TC(TEXT_MUTED), 132, 256, cancel_btn, "kb.zune.cancel");
                  ZK.show_l = zk_text(m, "show", TC(ACCENT_PRIMARY), 228, 256, zk_show_cb, "kb.zune.show"); }
    else zk_text(m, "cancel", TC(TEXT_MUTED), 180, 256, cancel_btn, "kb.zune.cancel");
    zk_paint();
}
