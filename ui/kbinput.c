/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "screens.h"
#include "theme_kit.h"
#include "theme.h"
#include <stdio.h>
#include <string.h>

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
    static char buf[160];
    snprintf(buf, sizeof buf, "%s", g_ta ? lv_textarea_get_text(g_ta) : "");
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

    /* Cancel / Save in the wide mid-band, above the keyboard */
    /* Cancel and Save between the field and the keys, where Search has its Search button */
    pill(g_modal, -50, 70, LV_SYMBOL_CLOSE, TC(TEXT_SECONDARY), cancel_btn);
    pill(g_modal,  50, 70, LV_SYMBOL_OK,    TC(STATUS_SUCCESS), save_btn);

    bkb_create(g_modal, g_ta, do_save);
}
