/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Round-panel lists: rows narrow with the circle (band-snapped, visible rows only) and eleven position
 * dots on the left rim. The same behaviour the Library has, for any other list. Rows opt in with
 * LV_OBJ_FLAG_USER_1. */
#ifndef CURVELIST_H
#define CURVELIST_H
#include "lvgl/lvgl.h"
typedef struct { lv_obj_t *list, *dot[11]; int cur, full_w; } curvelist_t;
void curvelist_attach(curvelist_t *c, lv_obj_t *list, lv_obj_t *root, int full_w);
void curvelist_update(curvelist_t *c);
void curvelist_braun_row(lv_obj_t *row);
void curvelist_braun_watch(lv_obj_t *list);  /* Braun: keep a list's visible rows styled as it fills */  /* Braun: restyle one list row (straight, rule, dark type, focus dot) */    /* after (re)filling the rows */
#endif
