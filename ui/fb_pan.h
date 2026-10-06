/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef FB_PAN_H
#define FB_PAN_H
#include "lvgl/lvgl.h"
/* Custom triple-buffered framebuffer pan display driver for /dev/fb0 (XRGB8888, 360x360, vyres=1080). */
lv_display_t *fbpan_create(const char *dev);
/* Screen rotation (stock values 0..3, controls.c): fbpan_set_rotation turns the picture now and re-aims the touch bound
 * with fbpan_bind_touch (the evdev pointer; call it right after lv_evdev_create). */
void fbpan_set_rotation(int v);
void fbpan_bind_touch(lv_indev_t *indev);
#endif
