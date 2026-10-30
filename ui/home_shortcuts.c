/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Home's two secondary shortcuts sit in the free rim band, outside A's clock/progress cover. */
#include "screens.h"
#include "theme.h"
#include "theme_kit.h"
#include "braun.h"
#include "config.h"
#include "home_shortcuts.h"
#include "ui.h"
#include <stdlib.h>

const char *const zune_main_names[ZMAIN_COUNT]={"Up Next","Queue","Music screen","Library","Search","Shortcuts","Quick Settings","Weather","Equalizer","Folders","Immersive","Lyrics","Song Info","Audiobooks","Battery","Last.fm","All apps","Settings","Working modes","Volume","Screen off","Play/Pause","Next track","Shuffle!","Nothing","Wi-Fi","Bluetooth"};
void zune_main_run(int action){
    if(action<0||action>=ZMAIN_COUNT)return;
    static const char *const key[ZMAIN_COUNT]={[ZMAIN_WEATHER]="weather",[ZMAIN_EQ]="eq",[ZMAIN_FOLDERS]="folders",[ZMAIN_IMMERSIVE]="immersive",[ZMAIN_LYRICS]="lyrics",[ZMAIN_INFO]="info",[ZMAIN_BOOKS]="books",[ZMAIN_BATTERY]="battery",[ZMAIN_LASTFM]="lastfm",[ZMAIN_APPS]="allapps"};
    if(key[action]){shortcut_run(key[action]);if(action==ZMAIN_APPS)screen_show(SCR_APPS);return;}
    switch(action){
    case ZMAIN_UPNEXT:upnext_open();break;
    case ZMAIN_QUEUE:queue_open();break;
    case ZMAIN_MUSIC:screen_show(SCR_NOWPLAYING);break;
    case ZMAIN_LIBRARY:screen_show(SCR_LIBRARY);break;
    case ZMAIN_SEARCH:screen_show(SCR_SEARCH);break;
    case ZMAIN_SHORTCUTS:apps_reload();screen_show(SCR_APPS);break;
    case ZMAIN_QUICK:screen_show(SCR_QUICK);break;
    case ZMAIN_SETTINGS:screen_show(SCR_SETTINGS);break;
    case ZMAIN_MODES:screen_show(SCR_WORKMODE);break;
    case ZMAIN_WIFI:screen_show(SCR_WIFI);break;
    case ZMAIN_BT:screen_show(SCR_BT);break;
    case ZMAIN_VOLUME:disco_nav_action_run(DISCO_NAV_VOLUME);break;
    case ZMAIN_SCREENOFF:disco_nav_action_run(DISCO_NAV_SCREENOFF);break;
    case ZMAIN_PLAYPAUSE:disco_nav_action_run(DISCO_NAV_PLAYPAUSE);break;
    case ZMAIN_NEXT:disco_nav_action_run(DISCO_NAV_NEXT);break;
    case ZMAIN_SHUFFLE:disco_nav_action_run(DISCO_NAV_SHUFFLE);break;
    default:break;
    }
}
static int main_choice(const char *key,int def){int a=cfg_get_int(key,def);return a>=0&&a<ZMAIN_COUNT?a:def;}
static lv_timer_t *g_main_tap;
static int g_main_held;
static void main_cancel(void){if(g_main_tap){lv_timer_delete(g_main_tap);g_main_tap=NULL;}}
static void main_tap_done(lv_timer_t *t){(void)t;main_cancel();if(screen_current()==SCR_HOME)zune_main_run(main_choice("zune_main",ZMAIN_UPNEXT));}
static lv_obj_t *g_upnext,*g_main_tiles[4];

static lv_point_t g_press;
static int g_press_valid;
static void press_cb(lv_event_t *e){
    (void)e; lv_indev_t *id=lv_indev_active(); g_press_valid=id!=NULL;
    if(id) lv_indev_get_point(id,&g_press);
    g_main_held=0;
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
static void main_hold(lv_event_t *e){(void)e;if(!tap_ok())return;g_main_held=1;main_cancel();zune_main_run(main_choice("zune_hold",ZMAIN_MUSIC));}
static void main_lost(lv_event_t *e){(void)e;main_cancel();}
static void upnext_cb(lv_event_t *e){
    if(th_zune()){
        if(lv_event_get_code(e)!=LV_EVENT_CLICKED||g_main_held||!tap_ok())return;
        int twice=main_choice("zune_double",ZMAIN_SEARCH);
        if(g_main_tap){main_cancel();zune_main_run(twice);}
        else if(twice==ZMAIN_NONE)zune_main_run(main_choice("zune_main",ZMAIN_UPNEXT));
        else g_main_tap=lv_timer_create(main_tap_done,330,NULL);
        return;
    }
    if(lv_event_get_code(e) == LV_EVENT_CLICKED && tap_ok() && cfg_get_int("up_next", 0)) screen_show(SCR_UPNEXT);
}
static lv_obj_t *shortcut(lv_obj_t *root, int cy, const char *glyph, const lv_font_t *font){
    lv_obj_t *b;
    if(th_braun_family()) b = br_button(root, 328, cy, 20, glyph, font, 0);
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
    if(th_braun2()){ lv_obj_add_flag(g_upnext, LV_OBJ_FLAG_HIDDEN); return; }   /* Braun 2.0: Up Next lives in the queue, not on Home */
    if(th_zune()){
        int a=main_choice("zune_main",ZMAIN_UPNEXT);
        for(int i=0;i<4;i++)if(g_main_tiles[i]){
            lv_color_t ink=i==1?ui_current_accent():TC(TEXT_PRIMARY);
            if(!lv_color_eq(lv_obj_get_style_border_color(g_main_tiles[i],0),ink))lv_obj_set_style_border_color(g_main_tiles[i],ink,0);
        }
        if(a==ZMAIN_NONE)lv_obj_add_flag(g_upnext,LV_OBJ_FLAG_HIDDEN);else lv_obj_remove_flag(g_upnext,LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if(cfg_get_int("up_next", 0)) lv_obj_remove_flag(g_upnext, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(g_upnext, LV_OBJ_FLAG_HIDDEN);
}
void home_shortcuts_create(lv_obj_t *root){
    g_upnext = shortcut(root, 206, LV_SYMBOL_LIST, TF(UI_20));
    if(th_zune()){
        kit_keep(g_upnext);lv_obj_set_pos(g_upnext,316,158);lv_obj_set_size(g_upnext,44,44);
        lv_obj_set_style_pad_all(g_upnext,0,0);lv_obj_set_style_bg_opa(g_upnext,LV_OPA_TRANSP,0);
        lv_obj_delete(lv_obj_get_child(g_upnext,0)); /* geometry avoids missing icon-font glyphs */
        lv_obj_t*icon=lv_obj_create(g_upnext);lv_obj_remove_style_all(icon);kit_keep(icon);
        lv_obj_set_size(icon,19,19);lv_obj_align(icon,LV_ALIGN_RIGHT_MID,-2,0);
        lv_obj_clear_flag(icon,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
        for(int i=0;i<4;i++){
            lv_obj_t*tile=lv_obj_create(icon);lv_obj_remove_style_all(tile);kit_keep(tile);g_main_tiles[i]=tile;
            lv_obj_set_pos(tile,(i%2)*11,(i/2)*11);lv_obj_set_size(tile,8,8);
            lv_obj_set_style_border_width(tile,1,0);lv_obj_set_style_border_opa(tile,LV_OPA_COVER,0);
            lv_obj_set_style_radius(tile,0,0);lv_obj_clear_flag(tile,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
        }
        lv_obj_add_event_cb(g_upnext,main_hold,LV_EVENT_LONG_PRESSED,NULL);
        lv_obj_add_event_cb(g_upnext,main_lost,LV_EVENT_PRESS_LOST,NULL);
    }
    lv_obj_add_event_cb(g_upnext, press_cb, LV_EVENT_PRESSED, NULL);
    ui_on(g_upnext, upnext_cb, LV_EVENT_CLICKED, NULL, "home.upnext", UI_REACHABLE);
    home_shortcuts_refresh();
}
