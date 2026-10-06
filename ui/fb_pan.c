/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "prof.h"
#include "fb_pan.h"
#include "controls.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include "lvgl/src/drivers/evdev/lv_evdev.h"

/* The panel is mounted 180 degrees turned; we render upright into LVGL draw buffers and copy each dirty area into the
   visible fb page through ctl_blit (180 by default; stock's Screen rotation can turn it 0/90/180/270). */
typedef struct {
    int fd;
    uint8_t *map;
    size_t frame_bytes;
    size_t visible_off;     /* byte offset of the page actually on screen */
    int npages;
    int next_page;
    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;
} fbpan_t;

static uint32_t tick_cb(void){
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint32_t)(t.tv_sec*1000u + t.tv_nsec/1000000u);
}

/* PARTIAL mode: LVGL hands us only the dirty rectangle(s); we rotate-copy each
 * one into the single persistent visible page (page 0, panned once at init). Cost is
 * proportional to the dirty area, not the whole 360x360 frame - a 1Hz time/arc update
 * touches a few KB instead of ~1MB. Direct write to the shown page can tear on huge
 * (full-screen) updates, but those are rare (screen changes) and already flash. */
static void fbpan_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map){
    fbpan_t *f = lv_display_get_driver_data(disp);
    uint8_t *base = f->map + f->visible_off;         /* the page actually on screen */
    /* Turn the logical (upright) area into the raw frame: 180 by default (the panel's mounting), or the rotation
     * the player has stored (controls.c, stock's Screen rotation). */
    if(ctl_rot_get() == 0)      /* the default: the original flush loop, untouched */
        ctl_flush_default(base, f->finfo.line_length, (int)f->vinfo.xres, (int)f->vinfo.yres,
                          (const uint32_t*)px_map, area->x1, area->y1, area->x2, area->y2);
    else
        ctl_blit(base, f->finfo.line_length, (int)f->vinfo.xres, (int)f->vinfo.yres, ctl_rot_get(),
                 (const uint32_t*)px_map, area->x1, area->y1, area->x2, area->y2);
#ifdef DISKOS_PROFILE
    prof_flush((area->x2 - area->x1 + 1) * (area->y2 - area->y1 + 1));
#endif
    lv_display_flush_ready(disp);
}

/* Touch follows the picture: the evdev swap + calibration is the inverse of the frame's rotation (controls.c). */
static lv_indev_t *g_touch_indev;
static void fbpan_touch_cal(void){
    if(!g_touch_indev) return;
    int swap, x0, y0, x1, y1;
    ctl_rot_touch_cal(ctl_rot_get(), lv_display_get_horizontal_resolution(lv_display_get_default()), &swap, &x0, &y0, &x1, &y1);
    lv_evdev_set_swap_axes(g_touch_indev, swap != 0);
    lv_evdev_set_calibration(g_touch_indev, x0, y0, x1, y1);
}
void fbpan_bind_touch(lv_indev_t *indev){ g_touch_indev = indev; fbpan_touch_cal(); }
void fbpan_set_rotation(int v){
    ctl_rot_set(v);
    fbpan_touch_cal();
    if(lv_display_get_default()){                     /* redraw everything through the new mapping */
        lv_obj_invalidate(lv_screen_active());
        lv_obj_invalidate(lv_layer_top());
        lv_obj_invalidate(lv_layer_sys());
    }
}

lv_display_t *fbpan_create(const char *dev){
    fbpan_t *f = calloc(1, sizeof(*f));
    if(!f){ perror("fbpan calloc"); return NULL; }
    f->fd = open(dev, O_RDWR);
    if(f->fd < 0){ perror("open fb"); free(f); return NULL; }
    if(ioctl(f->fd, FBIOGET_VSCREENINFO, &f->vinfo) < 0){ perror("vinfo"); close(f->fd); free(f); return NULL; }
    if(ioctl(f->fd, FBIOGET_FSCREENINFO, &f->finfo) < 0){ perror("finfo"); close(f->fd); free(f); return NULL; }
    f->frame_bytes = (size_t)f->vinfo.xres * f->vinfo.yres * (f->vinfo.bits_per_pixel/8);
    f->npages = f->finfo.smem_len / f->frame_bytes;
    if(f->npages < 1) f->npages = 1;
    f->next_page = 0;
    f->map = mmap(NULL, f->finfo.smem_len, PROT_READ|PROT_WRITE, MAP_SHARED, f->fd, 0);
    if(f->map == MAP_FAILED){ perror("mmap"); close(f->fd); free(f); return NULL; }
    /* Clear the WHOLE framebuffer (all pages) to black up front. On a switch from stock (or our
     * own relaunch) the fb still holds the stock boot splash; partial rendering only touches
     * widget-dirty areas, so background regions would show the stale splash until a full redraw.
     * Starting black means any not-yet-rendered pixel is black, not leftover splash. */
    memset(f->map, 0, f->finfo.smem_len);
    controls_boot_rotation();                       /* the player's stored Screen rotation, from the first frame */
    fprintf(stderr,"fbpan: %ux%u bpp=%u frame=%zu pages=%d (rot=%d, PARTIAL)\n",
            f->vinfo.xres,f->vinfo.yres,f->vinfo.bits_per_pixel,f->frame_bytes,f->npages,ctl_rot_get());

    /* Render into ONE persistent visible page (no page flipping). Try to pan to page 0
     * for determinism, then re-read vinfo and use whatever page is ACTUALLY visible -
     * so even if the pan is rejected we write the page on screen, not a hidden one. */
    f->next_page = 0;
    f->vinfo.xoffset = 0; f->vinfo.yoffset = 0; f->vinfo.activate = FB_ACTIVATE_NOW;
    if(ioctl(f->fd, FBIOPAN_DISPLAY, &f->vinfo) < 0) perror("fbpan: pan to 0");
    if(ioctl(f->fd, FBIOGET_VSCREENINFO, &f->vinfo) < 0) perror("fbpan: re-read vinfo");
    f->visible_off = (size_t)f->vinfo.yoffset * f->finfo.line_length;
    fprintf(stderr,"fbpan: visible yoffset=%u off=%zu line=%u\n",
            f->vinfo.yoffset, f->visible_off, f->finfo.line_length);

    /* PARTIAL mode: two small banded draw buffers (~60 rows) instead of two full
     * 518KB frames - ~1MB less RAM and the renderer only touches dirty regions. */
    #define FBPAN_BAND_ROWS 120   /* 2 x 173 KB: ~10% faster full redraws (transitions, screen changes) than 60 */
    size_t bufsz = (size_t)f->vinfo.xres * FBPAN_BAND_ROWS * 4;
    uint8_t *b1 = aligned_alloc(64, bufsz);
    uint8_t *b2 = aligned_alloc(64, bufsz);
    if(!b1 || !b2){ perror("fbpan: aligned_alloc"); free(b1); free(b2);
                    munmap(f->map, f->finfo.smem_len); close(f->fd); free(f); return NULL; }

    lv_tick_set_cb(tick_cb);
    lv_display_t *disp = lv_display_create(f->vinfo.xres, f->vinfo.yres);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_XRGB8888);
    lv_display_set_driver_data(disp, f);
    lv_display_set_flush_cb(disp, fbpan_flush_cb);
    lv_display_set_buffers(disp, b1, b2, bufsz, LV_DISPLAY_RENDER_MODE_PARTIAL);
    return disp;
}
