/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Classic theme: shared pieces (classic.c). */
#ifndef CLASSIC_H
#define CLASSIC_H
#include "lvgl/lvgl.h"
#include "theme_kit.h"
lv_obj_t *classic_battery(lv_obj_t *parent, int x, int y);
lv_obj_t *classic_header(lv_obj_t *root, const char *title, lv_event_cb_t back_cb);
#define CLASSIC_ROW_H 34
#define CLASSIC_LIST_Y 78
#define CLASSIC_LIST_END 316                      /* menus end on a whole row here, clear of the round bottom */
#define CLASSIC_LIST_H (CLASSIC_LIST_END - CLASSIC_LIST_Y)
lv_obj_t *classic_list(lv_obj_t *root, int y, int h);
lv_obj_t *classic_row(lv_obj_t *list, const char *label, int arrow);
void classic_style(lv_obj_t *o, kit_role_t role);
void classic_row_style(lv_obj_t *r);
void classic_radio_skin(lv_obj_t *root, const char *title, lv_obj_t *ring, lv_obj_t *glyph, lv_obj_t *name, lv_obj_t *sub,
                        lv_obj_t *sw, lv_obj_t *scan, lv_obj_t *list);
void chome_create(lv_obj_t *root);
void chome_set_clock(const char *t);
void chome_set_status(int batt, int charging);
void chome_set_now_playing(const char *title, const char *artist, int playing);
void classic_np_chrome(lv_obj_t *scr);
void classic_np_num(const char *num);
void classic_np_format(const char *fmt);
void classic_np_bars_apply(void);
void kit_fork_np_classic(void);
void csaver_create(lv_obj_t *root);
void csaver_set_clock(const char *t, const char *date);
void csaver_set_track(const char *title, const char *artist);   /* ui.c */
extern const theme_kit_t theme_kit_classic;
#endif
