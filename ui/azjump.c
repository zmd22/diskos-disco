/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The "A - Z" pill and its letter grid, shared by the Library lists and Folders.
 * The pill opens a 5-column grid of A..Z and #; a tap on a letter jumps the list (the owner's callback) and closes the
 * grid, a tap outside closes it. Disco: a pill at the bottom of the screen (the rim's middle belongs to the navigation
 * circle); other themes: a round button on the right rim. The pill is 84 x 36 with 10 px of extra touch area around it
 * (it was 68 x 30 with none, and easy to miss). */
#include "azjump.h"
#include "screens.h"
#include "theme.h"
#include "theme_kit.h"
#include "i18n.h"
#include <stdint.h>

static void letter_cb(lv_event_t *e){
    if(lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    lv_obj_t *cell = lv_event_get_current_target(e);
    azjump_t *a = lv_obj_get_user_data(lv_obj_get_parent(cell));
    char L = (char)(intptr_t)lv_event_get_user_data(e);
    if(a){ lv_obj_add_flag(a->grid, LV_OBJ_FLAG_HIDDEN); if(a->jump) a->jump(L); }
}
static void grid_bg_cb(lv_event_t *e){                       /* a tap outside the letters closes the grid */
    if(lv_event_get_code(e) == LV_EVENT_CLICKED) lv_obj_add_flag(lv_event_get_current_target(e), LV_OBJ_FLAG_HIDDEN);
}
static void btn_cb(lv_event_t *e){
    if(lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    azjump_t *a = lv_event_get_user_data(e);
    if(a){ lv_obj_clear_flag(a->grid, LV_OBJ_FLAG_HIDDEN); lv_obj_move_foreground(a->grid); }
}

void azjump_create(azjump_t *a, lv_obj_t *root, azjump_cb_t jump, const char *tag){
    (void)tag;
    a->jump = jump;
    a->btn = lv_button_create(root);
    lv_obj_remove_style_all(a->btn);
    lv_obj_set_pos(a->btn, 308, 158); lv_obj_set_size(a->btn, 44, 44);
    lv_obj_set_style_radius(a->btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(a->btn, TC(SURFACE_RAISED), 0);
    lv_obj_set_style_bg_opa(a->btn, LV_OPA_90, 0);
    lv_obj_set_style_bg_color(a->btn, TC(SURFACE_SELECTED), LV_STATE_PRESSED);
    lv_obj_set_ext_click_area(a->btn, 10);
    lv_obj_add_event_cb(a->btn, btn_cb, LV_EVENT_CLICKED, a);
    lv_obj_t *azl = lv_label_create(a->btn); lv_label_set_text(azl, "A-Z");
    lv_obj_set_style_text_font(azl, TF(UI_14), 0);
    if(th_disco()){ lv_obj_set_pos(a->btn, 138, 316); lv_obj_set_size(a->btn, 84, 36); lv_label_set_text(azl, "A - Z");
                    lv_obj_set_style_text_font(azl, TF(UI_16), 0); }
    lv_obj_set_style_text_color(azl, TC(TEXT_PRIMARY), 0); lv_obj_center(azl);
    lv_obj_add_flag(a->btn, LV_OBJ_FLAG_HIDDEN);

    /* the letter grid */
    a->grid = lv_obj_create(root);
    lv_obj_remove_style_all(a->grid);
    lv_obj_set_user_data(a->grid, a);
    lv_obj_set_size(a->grid, 360, 360); lv_obj_set_pos(a->grid, 0, 0);
    lv_obj_set_style_bg_color(a->grid, TC(CANVAS), 0);
    lv_obj_set_style_bg_opa(a->grid, LV_OPA_80, 0);
    lv_obj_add_flag(a->grid, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(a->grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(a->grid, grid_bg_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(a->grid, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *gt = lv_label_create(a->grid);                  /* says it's navigation, not a sort control */
    lv_label_set_text(gt, tr("Jump to"));
    lv_obj_align(gt, LV_ALIGN_TOP_MID, 0, 20);
    lv_obj_set_style_text_font(gt, TF(UI_14), 0);
    lv_obj_set_style_text_color(gt, TC(TEXT_MUTED), 0);
    static const char *AZ = "ABCDEFGHIJKLMNOPQRSTUVWXYZ#";
    int cols = 5, cw = 52, ch = 48, n = 27, gw = cols * cw, x0 = (360 - gw) / 2, rows = (n + cols - 1) / cols, y0 = 48;
    for(int i = 0; i < n; i++){
        int r = i / cols, c = i % cols;
        int in_row = (r == rows - 1) ? (n - r * cols) : cols;        /* the last row may be partial: centred */
        int rx0 = x0 + ((cols - in_row) * cw) / 2;
        lv_obj_t *cell = lv_button_create(a->grid);
        lv_obj_remove_style_all(cell);
        lv_obj_set_pos(cell, rx0 + c * cw, y0 + r * ch); lv_obj_set_size(cell, cw - 4, ch - 4);
        lv_obj_set_style_radius(cell, 8, 0);
        lv_obj_set_style_bg_color(cell, TC(ACCENT_PRIMARY), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_set_style_text_color(cell, TC(TEXT_PRIMARY), 0);
        lv_obj_set_style_text_color(cell, TC(ON_ACCENT), LV_STATE_PRESSED);
        lv_obj_add_event_cb(cell, letter_cb, LV_EVENT_CLICKED, (void *)(intptr_t)AZ[i]);
        lv_obj_t *l = lv_label_create(cell);
        char b[2] = { AZ[i], 0 }; lv_label_set_text(l, b);
        lv_obj_set_style_text_font(l, TF(UI_20), 0);
        lv_obj_set_style_text_color(l, TC(TEXT_PRIMARY), 0); lv_obj_center(l);
    }
}
void azjump_show(azjump_t *a, int on){
    if(!a->btn) return;
    if(on) lv_obj_clear_flag(a->btn, LV_OBJ_FLAG_HIDDEN);
    else { lv_obj_add_flag(a->btn, LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(a->grid, LV_OBJ_FLAG_HIDDEN); }
}
