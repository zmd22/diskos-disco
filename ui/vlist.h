/* SPDX-License-Identifier: GPL-3.0-or-later */
/* A windowed list for long, uniform-row flex lists: only the rows near the viewport exist, between two
 * spacers that keep the list's full height (the same technique as the Library). Rows are created by
 * the owner's own add(i) callback, so they look and behave exactly as before. */
#ifndef VLIST_H
#define VLIST_H
#include "lvgl/lvgl.h"
typedef void (*vlist_add_cb)(int i);          /* append row i to the list (it becomes the last child) */
typedef struct {
    lv_obj_t *list, *top, *bot;
    int active, n, first, last, pitch, gap, lead, view_h;
    vlist_add_cb add;
} vlist_t;
#define VLIST_MIN 60                           /* shorter lists are simply built in full */
/* the list must already hold any leading header rows (lead = their total height incl. gaps) */
void vlist_begin(vlist_t *v, lv_obj_t *list, int n, int row_h, int gap, int lead, int view_h, vlist_add_cb add);
void vlist_end(vlist_t *v);                    /* forget the window (call before lv_obj_clean of the list) */
void vlist_follow(vlist_t *v);                 /* from the list's LV_EVENT_SCROLL */
void vlist_show(vlist_t *v, int i);            /* make row i exist and scroll it into view */
#endif
