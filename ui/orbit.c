/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "orbit.h"
#include "screens.h"
#include <string.h>
#include "fork_theme.h"
#include "braun.h"
#include "theme.h"
#include <math.h>
#include <stdint.h>
extern const lv_font_t font_theme_20, font_theme_24;
static void btn_cb(lv_event_t *e){
    orbit_t *o = lv_event_get_user_data(e);
    lv_obj_t *b = lv_event_get_current_target(e);
    for(int i = 0; i < o->n; i++) if(o->btn[i] == b){ if(o->pick) o->pick(i); return; }
}
/* Disco: on a screen (not a popup on the top layer) the navigation circle's sliver owns 3 o'clock, so the buttons
 * spread over the rest of the ring, from 45 degrees below it round to 45 degrees above it */
static int disco_screen_menu(lv_obj_t *root){
    if(!th_disco()) return 0;
    for(lv_obj_t *p = root; p; p = lv_obj_get_parent(p)) if(p == lv_layer_top() || p == lv_layer_sys()) return 0;
    return 1;
}
#define DISCO_DEG (-1000)                                            /* first_deg marker: the Disco spread */
static float orbit_deg(const orbit_t *o, int i){
    if(o->first_deg == DISCO_DEG) return o->n > 1 ? 45.0f + i * 270.0f / (o->n - 1) : 180.0f;
    return o->first_deg + i * 360.0f / o->n;
}
void orbit_create(orbit_t *o, lv_obj_t *root, const orbit_item_t *it, int n, int first_deg, orbit_pick_cb pick){
    if(n > 8) n = 8;
    if(disco_screen_menu(root)) first_deg = DISCO_DEG;
    o->n = n; o->pick = pick; o->first_deg = first_deg; o->ring = o->hub = o->hub_icon = o->hub_cap = NULL;
    for(int i = 0; i < n; i++){
        float a = orbit_deg(o, i) * 3.14159265f / 180.0f;
        int cx = (int)lroundf(ORBIT_R * cosf(a)), cy = (int)lroundf(ORBIT_R * sinf(a));
        lv_obj_t *b = lv_button_create(root);
        o->btn[i] = b;
        lv_obj_remove_style_all(b);
        if(th_braun()){                                          /* Braun: each knob on its own panel disc over the grille */
            lv_obj_t *pd = br_disc(root, 180 + cx, 180 + cy + 8, ORBIT_BTN / 2 + 16, BR_PANEL);
            lv_obj_move_to_index(pd, lv_obj_get_index(b));       /* just behind the button */
        }
        lv_obj_set_size(b, ORBIT_BTN, ORBIT_BTN);
        lv_obj_align(b, LV_ALIGN_CENTER, cx, cy);
        lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(b, TC(SURFACE), 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(b, TC(SURFACE_RAISED), LV_STATE_PRESSED);
        lv_obj_set_style_border_color(b, TC(ACCENT_PRIMARY), 0);
        lv_obj_set_style_border_width(b, 0, 0);
        lv_obj_set_ext_click_area(b, 8);                       /* a finger-sized target */
        lv_obj_add_event_cb(b, btn_cb, LV_EVENT_SHORT_CLICKED, o);
        o->icon[i] = lv_label_create(b);
        lv_label_set_text(o->icon[i], it[i].glyph);
        lv_obj_set_style_text_font(o->icon[i], TF(UI_18), 0);
        lv_obj_set_style_text_color(o->icon[i], TC(TEXT_PRIMARY), 0);
        if(th_braun()){                                          /* the dark knob: turned edge, soft shadow, pointer, lamp */
            lv_obj_set_style_bg_color(b, TC(CONTROL_KNOB), 0);
            lv_obj_set_style_bg_color(b, TC(ORBIT_PRESSED), LV_STATE_PRESSED);
            lv_obj_set_style_border_color(b, TC(CONTROL_FILL), 0); lv_obj_set_style_border_width(b, 2, 0);
            lv_obj_set_style_shadow_color(b, TC(ORBIT_SHADOW), 0); lv_obj_set_style_shadow_width(b, 8, 0); lv_obj_set_style_shadow_offset_y(b, 2, 0);
            lv_obj_set_style_text_color(o->icon[i], TC(ON_CONTROL), 0);
            lv_obj_t *p = lv_obj_create(b); lv_obj_remove_style_all(p);                 /* the pointer, at 1 o'clock */
            lv_obj_set_size(p, 3, ORBIT_BTN / 6); lv_obj_set_style_radius(p, 1, 0);
            lv_obj_set_style_bg_color(p, TC(ORBIT_POINTER), 0); lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
            lv_obj_align(p, LV_ALIGN_TOP_MID, ORBIT_BTN / 6, 4); lv_obj_set_style_transform_rotation(p, 300, 0);
            lv_obj_clear_flag(p, LV_OBJ_FLAG_CLICKABLE);
            br_pointer_set(p, ORBIT_BTN, 0);                     /* straight up in the icon colour, like an OFF switch (toggles turn it) */
            lv_obj_t *lamp = lv_obj_create(root); lv_obj_remove_style_all(lamp);        /* the lamp above the knob */
            lv_obj_set_size(lamp, 6, 6); lv_obj_set_pos(lamp, 180 + cx - 3, 180 + cy - ORBIT_BTN / 2 - 11);
            lv_obj_set_style_radius(lamp, LV_RADIUS_CIRCLE, 0); lv_obj_set_style_bg_color(lamp, TC(ACCENT_PRIMARY), 0); lv_obj_set_style_bg_opa(lamp, LV_OPA_COVER, 0);
            lv_obj_clear_flag(lamp, LV_OBJ_FLAG_CLICKABLE); lv_obj_add_flag(lamp, LV_OBJ_FLAG_HIDDEN);
            o->lamp[i] = lamp; o->ptr[i] = p;
        }
        lv_obj_center(o->icon[i]);
        ring_button_style(b);                                    /* fork: Ring - accent ring, album-colour glyph */
        o->cap[i] = NULL;
        if(it[i].cap && it[i].cap[0]){
            o->cap[i] = lv_label_create(root);
            lv_label_set_text(o->cap[i], it[i].cap);
            lv_obj_set_style_text_font(o->cap[i], TH_F_CAPTION, 0);
            lv_obj_set_style_text_color(o->cap[i], th_braun() ? TC(TEXT_SECONDARY) : TC(TEXT_SECONDARY), 0);
            if(th_braun()) lv_obj_set_style_text_font(o->cap[i], br_font(12, 0), 0);
            lv_obj_align(o->cap[i], LV_ALIGN_CENTER, cx, cy + ORBIT_BTN / 2 + 11);
            lv_obj_clear_flag(o->cap[i], LV_OBJ_FLAG_CLICKABLE);
        }
    }
}
/* Compact enough for eight actions, but with larger targets and readable captions.
 * Keep this opt-in: screen orbits and their navigation clearance remain unchanged. */
void orbit_popup_polish(orbit_t *o){
    for(int i = 0; i < o->n; i++){
        float a = orbit_deg(o, i) * 3.14159265f / 180.0f;
        int cx = (int)lroundf(108 * cosf(a)), cy = (int)lroundf(108 * sinf(a));
        lv_obj_set_size(o->btn[i], 56, 56);
        lv_obj_align(o->btn[i], LV_ALIGN_CENTER, cx, cy - 8);
        lv_obj_set_ext_click_area(o->btn[i], 2);
        const lv_font_t *old=lv_obj_get_style_text_font(o->icon[i],0);
        lv_obj_set_style_text_font(o->icon[i], old==&font_theme_20?&font_theme_24:TF(UI_24), 0);
        if(o->cap[i]){
            lv_obj_set_style_text_font(o->cap[i], TF(UI_16), 0);
            lv_label_set_long_mode(o->cap[i], LV_LABEL_LONG_DOT);
            lv_obj_set_size(o->cap[i], 96, lv_font_get_line_height(TF(UI_16)));
            lv_obj_set_style_text_align(o->cap[i], LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_align(o->cap[i], LV_ALIGN_CENTER, cx, cy + 29);
        }
    }
}
void orbit_cap_width(orbit_t *o, int w, const lv_font_t *font){
    for(int i = 0; i < o->n; i++){
        if(!o->cap[i]) continue;
        float a = orbit_deg(o, i) * 3.14159265f / 180.0f;
        int cx = (int)lroundf(ORBIT_R * cosf(a)), cy = (int)lroundf(ORBIT_R * sinf(a));
        lv_label_set_long_mode(o->cap[i], LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(o->cap[i], LV_TEXT_ALIGN_CENTER, 0);
        if(font) lv_obj_set_style_text_font(o->cap[i], font, 0);
        lv_obj_set_size(o->cap[i], w, lv_font_get_line_height(lv_obj_get_style_text_font(o->cap[i], 0)) + 2);   /* one line: longer names end in ... */
        lv_obj_align(o->cap[i], LV_ALIGN_CENTER, cx, cy + ORBIT_BTN / 2 + 11);
    }
}
void orbit_set_on(orbit_t *o, int i, int on, lv_color_t accent){
    if(i < 0 || i >= o->n) return;
    if(th_braun()){                                              /* on: the pointer turns orange, the lamp lights */
        if(o->icon[i]) lv_obj_set_style_text_color(o->icon[i], TC(ON_CONTROL), 0);
        br_pointer_set(o->ptr[i], ORBIT_BTN, on);                 /* fork: OFF 0 deg icon colour, ON 45 deg orange */
        if(o->lamp[i]) lv_obj_add_flag(o->lamp[i], LV_OBJ_FLAG_HIDDEN);   /* fork: no lamp */
        if(o->cap[i]){ lv_obj_set_style_text_color(o->cap[i], on ? TC(TEXT_PRIMARY) : TC(TEXT_SECONDARY), 0);
                       lv_obj_set_style_text_font(o->cap[i], br_font(12, 0), 0); }
        return;
    }
    lv_obj_set_style_bg_color(o->btn[i], on ? accent : TC(SURFACE), 0);
    lv_obj_set_style_border_width(o->btn[i], 0, 0);
    lv_obj_set_style_text_color(o->icon[i],on && th_ringlike()?theme_on_color(accent):TC(TEXT_PRIMARY),0);
    if(th_ringlike()){ ring_button_style(o->btn[i]);                     /* fork: keep the ring */
        if(on) lv_obj_set_style_text_color(o->icon[i], theme_on_color(accent), 0); }
    if(o->cap[i]) lv_obj_set_style_text_color(o->cap[i], on ? TC(TEXT_PRIMARY) : TC(TEXT_SECONDARY), 0);
}
void orbit_set_pending(orbit_t *o, int p, lv_color_t accent){
    for(int i = 0; i < o->n; i++){
        int on = (i == p);
        if(th_braun()){ lv_obj_set_style_border_color(o->btn[i], on ? TC(ACCENT_PRIMARY) : TC(CONTROL_FILL), 0); lv_obj_set_style_border_width(o->btn[i], on ? 3 : 2, 0); continue; }
        lv_obj_set_style_border_width(o->btn[i], on ? 3 : 0, 0);
        lv_obj_set_style_border_color(o->btn[i], accent, 0);
        if(!on && th_ringlike()){ lv_obj_set_style_border_width(o->btn[i], 2, 0); lv_obj_set_style_border_color(o->btn[i], ring_border_color(), 0); }   /* fork */
        if(on){ lv_obj_set_style_bg_color(o->btn[i], TC(SURFACE_RAISED), 0);
                lv_obj_set_style_text_color(o->icon[i], accent, 0); }
    }
}
void orbit_set_glyph(orbit_t *o, int i, const char *glyph){ if(i >= 0 && i < o->n) lv_label_set_text(o->icon[i], glyph); }
void orbit_braun_icons(orbit_t *o){ if(!th_braun()) return; for(int i = 0; i < o->n; i++) if(o->icon[i]) lv_obj_set_style_text_color(o->icon[i], TC(ON_CONTROL), 0); }

static void spin_exec(void *var, int32_t v){ lv_arc_set_rotation((lv_obj_t *)var, v % 360); }
void orbit_hub_create(orbit_t *o, lv_obj_t *root, lv_event_cb_t hub_cb, const char *glyph, const char *cap){
    o->ring = lv_arc_create(root);
    lv_obj_remove_style(o->ring, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(o->ring, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(o->ring, ORBIT_RING, ORBIT_RING);
    lv_obj_center(o->ring);
    lv_arc_set_bg_angles(o->ring, 0, 360);
    lv_arc_set_range(o->ring, 0, 1000);
    lv_obj_set_style_arc_width(o->ring, TH_ARC_IND, LV_PART_MAIN);
    lv_obj_set_style_arc_color(o->ring, TC(CONTROL_TRACK), LV_PART_MAIN);
    lv_obj_set_style_arc_width(o->ring, TH_ARC_IND, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(o->ring, true, LV_PART_INDICATOR);
    o->hub = lv_button_create(root);
    lv_obj_remove_style_all(o->hub);
    lv_obj_set_size(o->hub, ORBIT_HUB, ORBIT_HUB);
    lv_obj_center(o->hub);
    lv_obj_set_style_radius(o->hub, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(o->hub, TC(ORBIT_HUB), 0);
    lv_obj_set_style_bg_color(o->hub, TC(SURFACE_RAISED), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(o->hub, LV_OPA_COVER, 0);
    if(hub_cb) lv_obj_add_event_cb(o->hub, hub_cb, LV_EVENT_CLICKED, NULL);
    o->hub_icon = lv_label_create(o->hub);
    lv_obj_set_style_text_font(o->hub_icon, TF(UI_28), 0);
    lv_obj_align(o->hub_icon, LV_ALIGN_CENTER, 0, -9);
    o->hub_cap = lv_label_create(o->hub);
    lv_obj_set_style_text_font(o->hub_cap, TH_F_CAPTION, 0);
    lv_obj_set_style_text_color(o->hub_cap, th_braun() ? TC(TEXT_SECONDARY) : TC(TEXT_SECONDARY), 0);
    if(th_braun()){ lv_obj_set_style_text_font(o->hub_cap, br_font(12, 0), 0); lv_obj_set_style_text_color(o->hub_icon, TC(TEXT_PRIMARY), 0);
                    lv_obj_set_style_arc_color(o->ring, TC(SURFACE), LV_PART_MAIN); }
    lv_obj_align(o->hub_cap, LV_ALIGN_CENTER, 0, 20);
    orbit_hub_set(o, glyph, TC(TEXT_PRIMARY), cap);
    orbit_hub_ring(o, ORBIT_RING_GREY, TC(CONTROL_TRACK), 0);
    if(disco_screen_menu(root) && cap && !strcmp(cap, "Back")){       /* Disco: back is the swipe; no centre back hub */
        lv_obj_add_flag(o->hub, LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(o->ring, LV_OBJ_FLAG_HIDDEN);
    }
}
void orbit_hub_ring(orbit_t *o, int mode, lv_color_t col, int value){
    if(th_braun()) col = TC(ACCENT_PRIMARY);                 /* Braun: progress / active rings in orange */
    if(!o->ring) return;
    lv_anim_delete(o->ring, spin_exec);
    lv_arc_set_rotation(o->ring, 270);                                  /* 12 o'clock */
    lv_obj_set_style_arc_color(o->ring, col, LV_PART_INDICATOR);
    switch(mode){
        case ORBIT_RING_GREY:     lv_arc_set_value(o->ring, 0); break;
        case ORBIT_RING_FULL:     lv_arc_set_value(o->ring, 1000); break;
        case ORBIT_RING_PROGRESS: lv_arc_set_value(o->ring, value < 0 ? 0 : value > 1000 ? 1000 : value); break;
        case ORBIT_RING_SPIN: {
            lv_arc_set_value(o->ring, 280);                             /* a short arc that goes round */
            lv_anim_t a; lv_anim_init(&a);
            lv_anim_set_var(&a, o->ring); lv_anim_set_exec_cb(&a, spin_exec);
            lv_anim_set_values(&a, 270, 270 + 360); lv_anim_set_duration(&a, 1000);
            lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE); lv_anim_start(&a);
        } break;
    }
}
void orbit_hub_set(orbit_t *o, const char *glyph, lv_color_t col, const char *cap){
    if(!o->hub) return;
    lv_label_set_text(o->hub_icon, glyph ? glyph : "");
    lv_obj_set_style_text_color(o->hub_icon, th_braun() ? TC(TEXT_PRIMARY) : col, 0);   /* Braun: no album colour */
    lv_label_set_text(o->hub_cap, cap ? cap : "");
}
lv_obj_t *orbit_title(lv_obj_t *root, const char *text){
    lv_obj_t *t = lv_label_create(root);
    lv_label_set_text(t, text);
    lv_obj_set_style_text_font(t, TH_F_CAPTION, 0);
    lv_obj_set_style_text_color(t, th_braun() ? TC(TEXT_PRIMARY) : TC(TEXT_SECONDARY), 0);
    if(th_braun()) lv_obj_set_style_text_font(t, br_font(14, 0), 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 16);
    return t;
}
