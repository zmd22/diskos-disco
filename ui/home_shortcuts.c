/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Home's two secondary shortcuts sit in the free rim band, outside A's clock/progress cover. */
#include "screens.h"
#include "theme.h"
#include "theme_kit.h"
#include "braun.h"
#include "config.h"
#include <stdlib.h>

static lv_obj_t *g_upnext;

static lv_point_t g_press;
static int g_press_valid;
static void press_cb(lv_event_t *e){
    (void)e; lv_indev_t *id=lv_indev_active(); g_press_valid=id!=NULL;
    if(id) lv_indev_get_point(id,&g_press);
}
static int tap_ok(void){
    lv_indev_t *id=lv_indev_active();
    if(!id || !g_press_valid) return 1;
    lv_point_t p; lv_indev_get_point(id,&p);
    return abs(p.x-g_press.x)<=16 && abs(p.y-g_press.y)<=16;
}
static void favourites_cb(lv_event_t *e){
    if(lv_event_get_code(e) != LV_EVENT_CLICKED || !tap_ok()) return;
    library_open_favourites();
    screen_show(SCR_LIBRARY);
}
static void upnext_cb(lv_event_t *e){
    if(lv_event_get_code(e) == LV_EVENT_CLICKED && tap_ok() && cfg_get_int("up_next", 0)) screen_show(SCR_UPNEXT);
}
static lv_obj_t *shortcut(lv_obj_t *root, int cy, const char *glyph, const lv_font_t *font){
    lv_obj_t *b;
    if(th_braun()) b = br_button(root, 328, cy, 20, glyph, font, 0);
    else {
        b = lv_button_create(root);
        lv_obj_remove_style_all(b);
        lv_obj_set_pos(b, 308, cy - 20); lv_obj_set_size(b, 40, 40);
        lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(b, TC(SURFACE), 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(b, TC(SURFACE_RAISED), LV_STATE_PRESSED);
        lv_obj_t *l = lv_label_create(b);
        lv_label_set_text(l, glyph); lv_obj_set_style_text_font(l, font, 0);
        lv_obj_set_style_text_color(l, TC(TEXT_PRIMARY), 0); lv_obj_center(l);
    }
    lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    /* No extended hit area: keep A's moving cover and the round bezel clear. */
    lv_obj_set_ext_click_area(b, 0);
    return b;
}
void home_shortcuts_refresh(void){
    if(!g_upnext) return;
    if(cfg_get_int("up_next", 0)) lv_obj_remove_flag(g_upnext, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(g_upnext, LV_OBJ_FLAG_HIDDEN);
}
void home_shortcuts_create(lv_obj_t *root){
    g_upnext = shortcut(root, 206, LV_SYMBOL_LIST, TF(UI_20));
    lv_obj_add_event_cb(g_upnext, press_cb, LV_EVENT_PRESSED, NULL);
    ui_on(g_upnext, upnext_cb, LV_EVENT_CLICKED, NULL, "home.upnext", UI_REACHABLE);
    home_shortcuts_refresh();
}
