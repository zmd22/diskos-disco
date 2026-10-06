/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef ANIM_H
#define ANIM_H
#include "lvgl/lvgl.h"

/* Reusable animation helpers tuned for the 360x360 software-rendered panel.
 * A global concurrency cap keeps simultaneous value-anims low so FULL-mode
 * redraws don't tank the frame rate on the MIPS core. */

#define ANIM_FAST   180
#define ANIM_BASE   260
#define ANIM_SLOW   340

/* spring-ish easing paths (no custom bezier tables, cheap O(1) callbacks) */
int32_t anim_path_spring(const lv_anim_t *a);        /* smooth, settles, no overshoot */
int32_t anim_path_spring_bouncy(const lv_anim_t *a); /* slight overshoot */

void anim_slide_x(lv_obj_t *o, int from, int to, uint32_t ms, lv_anim_completed_cb_t done);
void anim_slide_y(lv_obj_t *o, int from, int to, uint32_t ms, lv_anim_completed_cb_t done);
void anim_fade(lv_obj_t *o, lv_opa_t from, lv_opa_t to, uint32_t ms, lv_anim_completed_cb_t done);
void anim_scale(lv_obj_t *o, int from_pct, int to_pct, uint32_t ms); /* 256 = 1.0 (LVGL zoom) */
void anim_press(lv_obj_t *o);  /* quick 100%->94%->100% tactile feedback */

/* Give a full-screen panel a crisp 1px hairline on its LEADING (left) edge - the card boundary for
 * a push transition, visible on a near-black UI where a drop-shadow would not be. */
void anim_panel_shadow(lv_obj_t *root);
/* iOS-style ease-in-out page slide (gentle start + stop). */
void anim_page_slide(lv_obj_t *o, int from, int to, uint32_t ms, lv_anim_completed_cb_t done);
/* Fade the depth scrim in (over the covered screen) on push / out on back. */
void anim_scrim_fade(lv_obj_t *scrim, int in, uint32_t ms);
/* Zoom (scale+fade) transition - no moving edge, no seam. in=1 appear, in=0 recede. */
void anim_zoom(lv_obj_t *o, int in, uint32_t ms, lv_anim_completed_cb_t done);
/* Sweep an lv_arc's value from->to (expo-out) - the boot splash's ring drawing itself in. */
void anim_arc_value(lv_obj_t *o, int from, int to, uint32_t ms, lv_anim_completed_cb_t done);

#endif
