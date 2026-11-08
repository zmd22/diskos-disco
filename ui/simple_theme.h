/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DISKOS_SIMPLE_THEME_H
#define DISKOS_SIMPLE_THEME_H
#include "lvgl/lvgl.h"
int simple_cfg_get_int(const char *,int);
const char *simple_cfg_get_str(const char *,const char *);
int simple_cfg_set_int(const char *,int);
int simple_cfg_set_str(const char *,const char *);
int simple_cfg_set_int_deferred(const char *,int);
int simple_cfg_set_str_deferred(const char *,const char *);
void simple_radio_skin(lv_obj_t *ring,lv_obj_t *glyph,lv_obj_t *name,lv_obj_t *status,
                       lv_obj_t *toggle,lv_obj_t *scan,lv_obj_t *list);
void simple_nav_init(lv_obj_t *);
void simple_nav_open(int);
void simple_nav_screen(int);
void simple_nav_refresh(void);
void simple_accent_changed(uint32_t old_acc, uint32_t old_sel);   /* Squash following the Accent Colour */
void simple_refresh_budget(int);
int simple_nav_is_open(void);
int simple_owns_point(int,int);
int simple_music_protected(lv_obj_t *);
void simple_keep_label(lv_obj_t *);
void simple_test_nav_phase(int);
int simple_test_nav_bindings(void);
int simple_test_nav_y(void);
int simple_test_qs_paused(void);
int simple_test_selection(void);
int simple_test_qs_count(void);
lv_obj_t*simple_test_qs_button(int);
void simple_qs_create(lv_obj_t *);
void simple_qs_build(void);
void simple_qs_art(const void *);
void simple_action_feedback(void);
void simple_playback_feedback(const char *);
void simple_test_feedback_frame(int);
void simple_qs_battery(int,int);
void simple_qs_track(const char *,const char *,int);
void simple_qs_refresh(int);
void simple_qs_visible(int);
void simple_volume_show(int);
int simple_volume_visible(void);
lv_obj_t *simple_volume_root(void);
#endif
