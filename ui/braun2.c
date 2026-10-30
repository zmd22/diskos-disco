/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Braun 2.0: presentation only. Shared screens retain their actions and data owners. */
#include "braun2.h"
#include "braun.h"
#include "screens.h"
#include "config.h"
#include <string.h>
void bhome_create(lv_obj_t *);
void br2_keep_tree(lv_obj_t *o){kit_keep(o);for(uint32_t i=0;i<lv_obj_get_child_count(o);i++)br2_keep_tree(lv_obj_get_child(o,i));}
static void back(lv_event_t *e){(void)e;screen_back();}
lv_obj_t *br2_header(lv_obj_t *root,const char *text,lv_event_cb_t cb){
    lv_obj_t *cap=lv_obj_create(root);lv_obj_remove_style_all(cap);lv_obj_set_size(cap,304,58);lv_obj_set_pos(cap,28,0);
    lv_obj_set_style_radius(cap,28,0);lv_obj_set_style_bg_color(cap,TC(SURFACE_STRONG),0);lv_obj_set_style_bg_opa(cap,LV_OPA_COVER,0);lv_obj_remove_flag(cap,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *b=br_button(root,96,36,20,LV_SYMBOL_LEFT,TF(UI_20),0);ui_on(b,cb?cb:back,LV_EVENT_CLICKED,NULL,"nav.back",UI_CORE);
    lv_obj_t *l=br_label(root,text,br_font(22,1),BR_TXT);lv_obj_set_size(l,168,29);lv_obj_set_pos(l,126,22);lv_label_set_long_mode(l,LV_LABEL_LONG_DOT);
    br2_keep_tree(cap);br2_keep_tree(b);kit_keep(l);return l;
}
void br2_style(lv_obj_t *o,kit_role_t role){
    switch(role){
    case KIT_SCREEN: br_face(o);break;
    case KIT_TITLE:lv_obj_set_style_text_font(o,br_font(22,1),0);lv_obj_set_style_text_color(o,TC(TEXT_PRIMARY),0);break;
    case KIT_CARD:
        /* Keep the grille visible behind full-width content containers. */
        if(lv_obj_get_width(o)>=340&&lv_obj_get_height(o)>=340){br_face(o);}
        else if(lv_obj_get_width(o)>=300){lv_obj_set_style_bg_opa(o,LV_OPA_TRANSP,0);lv_obj_set_style_border_width(o,0,0);}
        else if(lv_obj_get_style_radius(o,0)!=LV_RADIUS_CIRCLE){lv_obj_set_style_radius(o,24,0);lv_obj_set_style_bg_color(o,TC(SURFACE_STRONG),0);}   /* circles (artwork, discs) stay round */
        break;
    case KIT_ROW:
        lv_obj_set_style_radius(o,28,0);lv_obj_set_style_border_width(o,0,0);
        lv_obj_set_style_bg_color(o,TC(SURFACE_STRONG),0);
        lv_obj_set_style_bg_color(o,TC(SURFACE_PRESSED),LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(o,LV_OPA_COVER,LV_STATE_PRESSED);break;
    case KIT_ROW_TEXT:lv_obj_set_style_text_font(o,br_font(20,0),0);lv_obj_set_style_text_color(o,TC(TEXT_PRIMARY),0);break;
    case KIT_ROW_SUB:lv_obj_set_style_text_font(o,br_font(14,0),0);lv_obj_set_style_text_color(o,TC(TEXT_SECONDARY),0);break;
    case KIT_TOGGLE:br_style_switch(o);break;
    case KIT_SLIDER:case KIT_BAR:
        lv_obj_set_style_bg_color(o,TC(CONTROL_TRACK),LV_PART_MAIN);lv_obj_set_style_radius(o,4,LV_PART_MAIN);
        lv_obj_set_style_bg_color(o,TC(ACCENT_PRIMARY),LV_PART_INDICATOR);lv_obj_set_style_radius(o,4,LV_PART_INDICATOR);
        if(role==KIT_SLIDER){lv_obj_set_style_bg_color(o,TC(CONTROL_KNOB),LV_PART_KNOB);lv_obj_set_style_border_color(o,TC(BORDER),LV_PART_KNOB);lv_obj_set_style_border_width(o,1,LV_PART_KNOB);}
        break;
    case KIT_ROLLER:
        lv_obj_set_style_bg_color(o,TC(SURFACE_STRONG),LV_PART_SELECTED);lv_obj_set_style_text_color(o,TC(TEXT_PRIMARY),LV_PART_SELECTED);
        lv_obj_set_style_text_color(o,TC(TEXT_SECONDARY),LV_PART_MAIN);break;
    case KIT_KEYBOARD:
        lv_obj_set_style_bg_color(o,TC(CANVAS),LV_PART_MAIN);lv_obj_set_style_bg_color(o,TC(SURFACE_STRONG),LV_PART_ITEMS);
        lv_obj_set_style_bg_color(o,TC(ACCENT_PRIMARY),LV_PART_ITEMS|LV_STATE_PRESSED);lv_obj_set_style_text_color(o,TC(ON_ACCENT),LV_PART_ITEMS|LV_STATE_PRESSED);
        lv_obj_set_style_radius(o,12,LV_PART_ITEMS);break;
    case KIT_BUTTON:case KIT_ICON_BUTTON:
        if(lv_obj_has_flag(o,LV_OBJ_FLAG_USER_2))break;   /* an accent-filled primary (e.g. Queue's Play) stays orange */
        lv_obj_set_style_radius(o,role==KIT_ICON_BUTTON?LV_RADIUS_CIRCLE:24,0);
        lv_obj_set_style_bg_color(o,role==KIT_ICON_BUTTON?TC(CONTROL_KNOB):TC(SURFACE_STRONG),0);lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);
        if(role==KIT_ICON_BUTTON)for(uint32_t i=0;i<lv_obj_get_child_count(o);i++){lv_obj_t *c=lv_obj_get_child(o,i);if(lv_obj_check_type(c,&lv_label_class))lv_obj_set_style_text_color(c,TC(ON_CONTROL),0);}
        lv_obj_set_style_border_width(o,1,0);lv_obj_set_style_border_color(o,TC(BORDER),0);
        lv_obj_set_style_bg_color(o,TC(SURFACE_PRESSED),LV_STATE_PRESSED);break;
    case KIT_POPUP:
        lv_obj_set_style_bg_color(o,TC(SURFACE_STRONG),0);lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);
        lv_obj_set_style_radius(o,28,0);lv_obj_set_style_border_color(o,TC(BORDER),0);break;
    case KIT_TEXTAREA:lv_obj_set_style_bg_color(o,TC(SURFACE_STRONG),0);lv_obj_set_style_radius(o,24,0);lv_obj_set_style_text_color(o,TC(TEXT_PRIMARY),0);break;
    default:break;
    }
}
const theme_kit_t theme_kit_braun2={.id="braun2",.header=br2_header,.style=br2_style,.home_build=bhome_create,.fork_np_layout=kit_fork_np_braun2};
