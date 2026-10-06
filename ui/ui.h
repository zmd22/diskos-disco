/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef UI_H
#define UI_H
#include "lvgl/lvgl.h"
#include "ipc.h"
void ui_create(lv_obj_t *root);
void ui_update(const track_state_t *st);
const char *ui_current_cover_src(void);
void ui_set_np_style(int vinyl);
void ui_vinyl_spin(int want);

/* Now Playing seek recognizer - fed raw touch from main.c (ring is display-only).
 * press/move arm/own the gesture; release returns 1 if it committed a seek
 * (so the caller skips its own nav-swipe handling). */
int  ui_np_seek_press(int x, int y);
int  ui_np_seek_move(int x, int y);
int  ui_np_seek_release(int x, int y);
#endif
