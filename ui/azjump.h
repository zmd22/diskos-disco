/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The "A - Z" pill and its letter grid, shared by the Library lists and Folders. */
#ifndef AZJUMP_H
#define AZJUMP_H
#include "lvgl/lvgl.h"
typedef void (*azjump_cb_t)(char letter);       /* 'A'..'Z' or '#' */
typedef struct { lv_obj_t *btn, *grid; azjump_cb_t jump; } azjump_t;
void azjump_create(azjump_t *a, lv_obj_t *root, azjump_cb_t jump, const char *tag);
void azjump_show(azjump_t *a, int on);           /* the pill (off also closes the grid) */
#endif
