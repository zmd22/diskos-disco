/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DISKOS_BATTERY_H
#define DISKOS_BATTERY_H
#include "lvgl/lvgl.h"
typedef struct {lv_obj_t *arc;int state;} battery_arc_t;
void battery_arc_create(battery_arc_t *,lv_obj_t *,int,int,int,int,int);
void battery_arc_set(battery_arc_t *,int,int);
#endif
