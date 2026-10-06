/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Radial ("pizza") menu: up to 8 slices around a central hub, sized for the round 360 px panel. */
#ifndef RADIAL_H
#define RADIAL_H
#include "lvgl/lvgl.h"
typedef struct { const char *glyph, *l1, *l2; } radial_item_t;
typedef void (*radial_pick_cb)(int index);
typedef struct {
    int n, pressed, active, pending;
    float a_first, span;                      /* slice 0 starts at a_first (deg, clockwise from 3 o'clock) */
    lv_obj_t *slice[8], *icon[8], *l1[8], *l2[8];
    lv_obj_t *wheel, *outline, *hub, *hub_icon, *hub_cap;
    lv_point_precise_t pts[96];
    radial_pick_cb pick;
} radial_t;
/* pick(index) fires on a tap inside a slice; hub_cb fires on a hub tap (Back, by convention) */
void radial_create(radial_t *r, lv_obj_t *root, const radial_item_t *items, int n,
                   radial_pick_cb pick, lv_event_cb_t hub_cb, const char *hub_glyph, const char *hub_caption);
/* active: outlined in the accent (-1 none). pending: outlined faintly while a change settles (-1 none) */
void radial_set_state(radial_t *r, int active, int pending, lv_color_t accent);
void radial_set_hub_glyph(radial_t *r, const char *glyph, lv_color_t col);
#endif
