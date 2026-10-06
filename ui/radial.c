#include "theme.h"
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "radial.h"
#include <math.h>
#include <stdint.h>
#define R_OUT 172                 /* outer radius: just inside the bezel */
#define R_IN   64                 /* inner radius: the hub sits in here */
#define GAP     3                 /* degrees between slices */
#define C_CARD  0x1C1C1E
#define C_LIFT  0x2A2A2D
#define C_TXT2  0x8E8E93
/* Which slice is under (x, y)? Our own geometry, not the arc widget's hit test: LVGL pads every arc's
 * hit area by ~50 px worth of degrees, so neighbouring slices overlapped and the topmost one won taps
 * meant for the others. Gaps between slices count toward the nearer slice, which is kinder to fingers. */
static int slice_at(const radial_t *r, lv_coord_t x, lv_coord_t y){
    float dx = x - 180.0f, dy = y - 180.0f, d = sqrtf(dx * dx + dy * dy);
    if(d < R_IN - 4 || d > 184) return -1;                    /* the hub, or outside the panel */
    float a = atan2f(dy, dx) * 180.0f / 3.14159265f;          /* -180..180, clockwise from 3 o'clock */
    float rel = a - r->a_first;
    while(rel < 0) rel += 360.0f;
    while(rel >= 360.0f) rel -= 360.0f;
    int i = (int)(rel / r->span);
    return (i >= 0 && i < r->n) ? i : -1;
}
static void paint_slice(radial_t *r, int i){
    int lift = (i == r->active || i == r->pending || i == r->pressed);
    lv_obj_set_style_arc_color(r->slice[i], lv_color_hex(lift ? C_LIFT : C_CARD), LV_PART_MAIN);
}
static void wheel_cb(lv_event_t *e){
    radial_t *r = lv_event_get_user_data(e);
    lv_event_code_t code = lv_event_get_code(e);
    lv_point_t p; lv_indev_t *in = lv_indev_active(); if(!in) return;
    lv_indev_get_point(in, &p);
    int i = slice_at(r, p.x, p.y), was = r->pressed;
    if(code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING) r->pressed = i;
    else if(code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) r->pressed = -1;
    if(was != r->pressed){ if(was >= 0) paint_slice(r, was); if(r->pressed >= 0) paint_slice(r, r->pressed); }
    if(code == LV_EVENT_CLICKED && i >= 0 && r->pick) r->pick(i);
}
void radial_create(radial_t *r, lv_obj_t *root, const radial_item_t *it, int n,
                   radial_pick_cb pick, lv_event_cb_t hub_cb, const char *hub_glyph, const char *hub_caption){
    if(n > 8) n = 8;
    r->n = n; r->pick = pick; r->pressed = r->active = r->pending = -1;
    float span = 360.0f / n, rc = (R_OUT + R_IN) / 2.0f + 4;
    r->span = span; r->a_first = 270.0f - span / 2;
    for(int i = 0; i < n; i++){
        /* slice 0 is centred on 12 o'clock; LVGL angles run clockwise from 3 o'clock */
        float a0 = 270.0f - span / 2 + i * span + GAP / 2.0f, a1 = a0 + span - GAP;
        lv_obj_t *s = lv_arc_create(root);
        r->slice[i] = s;
        lv_obj_remove_style(s, NULL, LV_PART_KNOB);
        lv_obj_set_size(s, 2 * R_OUT, 2 * R_OUT);
        lv_obj_center(s);
        lv_arc_set_rotation(s, 0);
        lv_arc_set_bg_angles(s, (lv_value_precise_t)a0, (lv_value_precise_t)a1);
        lv_arc_set_range(s, 0, 100); lv_arc_set_value(s, 0);                  /* no indicator */
        lv_obj_set_style_arc_width(s, R_OUT - R_IN, LV_PART_MAIN);
        lv_obj_set_style_arc_rounded(s, false, LV_PART_MAIN);
        lv_obj_set_style_arc_color(s, lv_color_hex(C_CARD), LV_PART_MAIN);
        lv_obj_set_style_arc_color(s, lv_color_hex(C_LIFT), LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_set_style_arc_width(s, 0, LV_PART_INDICATOR);
        lv_obj_clear_flag(s, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);   /* drawing only: the wheel hit-tests */
        /* icon + one or two caption lines at the slice's centre */
        float mid = (a0 + a1) / 2 * 3.14159265f / 180.0f;
        int cx = (int)(rc * cosf(mid)), cy = (int)(rc * sinf(mid));
        int two = it[i].l2 && it[i].l2[0];
        r->icon[i] = lv_label_create(root);
        lv_label_set_text(r->icon[i], it[i].glyph);
        lv_obj_set_style_text_font(r->icon[i], TF(UI_20), 0);
        lv_obj_set_style_text_color(r->icon[i], TC(TEXT_PRIMARY), 0);
        lv_obj_align(r->icon[i], LV_ALIGN_CENTER, cx, cy - (two ? 16 : 12));
        r->l1[i] = lv_label_create(root);
        lv_label_set_text(r->l1[i], it[i].l1);
        lv_obj_set_style_text_font(r->l1[i], TF(UI_14), 0);
        lv_obj_set_style_text_color(r->l1[i], TC(TEXT_PRIMARY), 0);
        lv_obj_align(r->l1[i], LV_ALIGN_CENTER, cx, cy + (two ? 6 : 12));
        r->l2[i] = NULL;
        if(two){
            r->l2[i] = lv_label_create(root);
            lv_label_set_text(r->l2[i], it[i].l2);
            lv_obj_set_style_text_font(r->l2[i], TF(UI_12), 0);
            lv_obj_set_style_text_color(r->l2[i], lv_color_hex(C_TXT2), 0);
            lv_obj_align(r->l2[i], LV_ALIGN_CENTER, cx, cy + 22);
        }
        lv_obj_t *lbls[3] = { r->icon[i], r->l1[i], r->l2[i] };
        for(int k = 0; k < 3; k++) if(lbls[k]) lv_obj_clear_flag(lbls[k], LV_OBJ_FLAG_CLICKABLE);
    }
    /* the active slice's outline: one polyline round the wedge, moved when the selection changes */
    r->outline = lv_line_create(root);
    lv_obj_set_style_line_width(r->outline, 3, 0);
    lv_obj_set_style_line_rounded(r->outline, true, 0);
    lv_obj_clear_flag(r->outline, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(r->outline, LV_OBJ_FLAG_HIDDEN);
    /* one transparent hit area over the whole wheel; the hub is created after it, so it stays on top */
    r->wheel = lv_obj_create(root);
    lv_obj_remove_style_all(r->wheel);
    lv_obj_set_size(r->wheel, 360, 360);
    lv_obj_center(r->wheel);
    lv_obj_clear_flag(r->wheel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(r->wheel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(r->wheel, wheel_cb, LV_EVENT_ALL, r);
    r->hub = lv_button_create(root);                                           /* the hub: tap = back */
    lv_obj_remove_style_all(r->hub);
    lv_obj_set_size(r->hub, 2 * R_IN - 12, 2 * R_IN - 12);
    lv_obj_center(r->hub);
    lv_obj_set_style_radius(r->hub, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(r->hub, lv_color_hex(0x121214), 0);
    lv_obj_set_style_bg_color(r->hub, lv_color_hex(C_LIFT), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(r->hub, LV_OPA_COVER, 0);
    if(hub_cb) lv_obj_add_event_cb(r->hub, hub_cb, LV_EVENT_CLICKED, NULL);
    r->hub_icon = lv_label_create(r->hub);
    lv_label_set_text(r->hub_icon, hub_glyph ? hub_glyph : "");
    lv_obj_set_style_text_font(r->hub_icon, TF(UI_28), 0);
    lv_obj_set_style_text_color(r->hub_icon, TC(TEXT_PRIMARY), 0);
    lv_obj_align(r->hub_icon, LV_ALIGN_CENTER, 0, -10);
    r->hub_cap = lv_label_create(r->hub);
    lv_label_set_text(r->hub_cap, hub_caption ? hub_caption : "");
    lv_obj_set_style_text_font(r->hub_cap, TF(UI_12), 0);
    lv_obj_set_style_text_color(r->hub_cap, lv_color_hex(C_TXT2), 0);
    lv_obj_align(r->hub_cap, LV_ALIGN_CENTER, 0, 20);
}
static void outline_slice(radial_t *r, int i, lv_color_t col, lv_opa_t opa){
    if(i < 0){ lv_obj_add_flag(r->outline, LV_OBJ_FLAG_HIDDEN); return; }
    const int K = 44;                                          /* points per edge arc */
    float a0 = (r->a_first + i * r->span + GAP / 2.0f) * 3.14159265f / 180.0f;
    float a1 = a0 + (r->span - GAP) * 3.14159265f / 180.0f;
    float ro = R_OUT - 1.5f, ri = R_IN + 1.5f; int n = 0;
    for(int k = 0; k <= K; k++){ float a = a0 + (a1 - a0) * k / K; r->pts[n].x = 180 + ro * cosf(a); r->pts[n].y = 180 + ro * sinf(a); n++; }
    for(int k = K; k >= 0; k--){ float a = a0 + (a1 - a0) * k / K; r->pts[n].x = 180 + ri * cosf(a); r->pts[n].y = 180 + ri * sinf(a); n++; }
    r->pts[n] = r->pts[0]; n++;                                /* close the wedge */
    lv_line_set_points(r->outline, r->pts, (uint32_t)n);
    lv_obj_set_pos(r->outline, 0, 0);
    lv_obj_set_style_line_color(r->outline, col, 0);
    lv_obj_set_style_line_opa(r->outline, opa, 0);
    lv_obj_remove_flag(r->outline, LV_OBJ_FLAG_HIDDEN);
}
void radial_set_state(radial_t *r, int active, int pending, lv_color_t accent){
    r->active = active; r->pending = (pending == active) ? -1 : pending;
    for(int i = 0; i < r->n; i++){
        paint_slice(r, i);
        int mark = (i == active || i == r->pending);
        lv_obj_set_style_text_color(r->icon[i], mark ? accent : TC(ON_ACCENT), 0);
        if(r->l2[i]) lv_obj_set_style_text_color(r->l2[i], lv_color_hex(C_TXT2), 0);
    }
    if(active >= 0)          outline_slice(r, active, accent, LV_OPA_COVER);      /* outline, no fill */
    else if(r->pending >= 0) outline_slice(r, r->pending, accent, LV_OPA_50);     /* faint while it settles */
    else                     outline_slice(r, -1, accent, 0);
}
void radial_set_hub_glyph(radial_t *r, const char *glyph, lv_color_t col){
    lv_label_set_text(r->hub_icon, glyph);
    lv_obj_set_style_text_color(r->hub_icon, col, 0);
}
