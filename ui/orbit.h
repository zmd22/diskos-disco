/* SPDX-License-Identifier: GPL-3.0-or-later */
/* "Orbit" menu: round buttons circling a hub, with captions - the shared layout of Quick Settings,
 * Settings and Working Mode. The hub's ring speaks: progress (QS), the active mode (red), switching
 * (a spinning red arc) or nothing in particular (grey). */
#ifndef ORBIT_H
#define ORBIT_H
#include "lvgl/lvgl.h"
#define ORBIT_R      116          /* orbit radius: buttons end at r=140, clear of the rim-scroll band (146+);
                                       the leftmost button's edge (x=40) stays inside the back-swipe start zone (x<64),
                                       as before, so edge swipes still start from the rim */
#define ORBIT_BTN    48           /* button diameter */
#define ORBIT_HUB    88           /* hub disc diameter */
#define ORBIT_RING   104          /* hub ring diameter */
typedef struct { const char *glyph, *cap; } orbit_item_t;
typedef void (*orbit_pick_cb)(int index);
enum { ORBIT_RING_GREY, ORBIT_RING_FULL, ORBIT_RING_SPIN, ORBIT_RING_PROGRESS };
typedef struct {
    int n;
    lv_obj_t *btn[8], *icon[8], *cap[8];
    lv_obj_t *ring, *hub, *hub_icon, *hub_cap;
    orbit_pick_cb pick;
    int first_deg;                       /* where button 0 sits (LVGL degrees) */
    lv_obj_t *lamp[8], *ptr[8];   /* Braun knobs: the lamp above, the pointer */
} orbit_t;
/* buttons clockwise from first_deg (LVGL angles: 0 = 3 o'clock, clockwise), evenly spaced */
void orbit_create(orbit_t *o, lv_obj_t *root, const orbit_item_t *it, int n, int first_deg, orbit_pick_cb pick);
/* long captions (app / device names): a fixed width, centred, ending in "..." */
void orbit_cap_width(orbit_t *o, int w, const lv_font_t *font);
void orbit_braun_icons(orbit_t *o);        /* Braun: knob icons stay white */
void orbit_set_on(orbit_t *o, int i, int on, lv_color_t accent);        /* filled in the accent */
void orbit_set_pending(orbit_t *o, int i, lv_color_t accent);           /* ringed; -1 clears */
void orbit_set_glyph(orbit_t *o, int i, const char *glyph);
/* hub: ring + dark disc with an icon and a caption; hub_cb fires on a tap (Back, by convention) */
void orbit_hub_create(orbit_t *o, lv_obj_t *root, lv_event_cb_t hub_cb, const char *glyph, const char *cap);
void orbit_hub_ring(orbit_t *o, int mode, lv_color_t col, int value_permille);
void orbit_hub_set(orbit_t *o, const char *glyph, lv_color_t col, const char *cap);
/* a small caption at the top rim (screen title) */
lv_obj_t *orbit_title(lv_obj_t *root, const char *text);
#endif
