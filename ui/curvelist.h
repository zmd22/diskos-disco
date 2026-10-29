/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Round-panel lists: rows narrow with the circle (band-snapped, visible rows only) and eleven position
 * dots on the left rim. The same behaviour the Library has, for any other list. Rows opt in with
 * LV_OBJ_FLAG_USER_1. */
#ifndef CURVELIST_H
#define CURVELIST_H
#include "lvgl/lvgl.h"
typedef struct { lv_obj_t *list, *dot[11]; int cur, full_w, disco_style; } curvelist_t;
void curvelist_attach(curvelist_t *c, lv_obj_t *list, lv_obj_t *root, int full_w);
void curvelist_update(curvelist_t *c);
void curvelist_braun_row(lv_obj_t *row);
void curvelist_braun_watch(lv_obj_t *list);  /* Braun: keep a list's visible rows styled as it fills */  /* Braun: restyle one list row (straight, rule, dark type, focus dot) */    /* after (re)filling the rows */
/* Original floored circle spans, cached for the list radii 150, 172 and 180. */
int curvelist_circle_half(int radius, int distance);
void curvelist_edge_fade(lv_obj_t *list);   /* Disco, Squash, Null: soft top/bottom edges in the list's empty space */
int curvelist_disco_scroll(void);
const lv_image_dsc_t *curvelist_fade_strip(int vertical, int len, int opaque_end, lv_color_t col);   /* dithered fade ramp */
int  curvelist_wheel(void);
void curvelist_wheel_exclude(lv_obj_t *list);   /* this list keeps its own motion in Wheel mode */                                 /* List motion "Wheel" is on (Disco, Squash, Null) */
void curvelist_wheel_list(lv_obj_t *list, int row_h);       /* Wheel: centring pads + snap */
void curvelist_wheel_row(lv_obj_t *row, lv_obj_t *list);    /* Wheel: one row's size, place and colour */   /* Disco > Scroll: 0 Classic, 1 Straight, 2 Disco (0 outside Disco/Null) */
int curvelist_disco_geom(int mode, int y1, int y2, int full_w, int *w, int *shift);   /* 1 = handled */
#endif
