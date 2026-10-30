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
/* Depth for a key: a soft top-to-bottom shade from its own colour, the light top edge and a drop; pressed, it goes flat
 * and a little darker (pushed in). Optionally its icon glyphs turn orange. Call after the key's colour is set. */
void br2_depth(lv_obj_t *o,int orange_icons){
    lv_color_t c=lv_obj_get_style_bg_color(o,0);
    lv_obj_set_style_bg_color(o,theme_color_mix(TC(ON_ACCENT),c,34),0);
    lv_obj_set_style_bg_grad_color(o,theme_color_mix(TC(FIXED_MEDIA_BLACK),c,46),0);lv_obj_set_style_bg_grad_dir(o,LV_GRAD_DIR_VER,0);
    lv_obj_set_style_bg_grad_dir(o,LV_GRAD_DIR_NONE,LV_STATE_PRESSED);
    br2_emboss(o,1);
    if(orange_icons)for(uint32_t i=0;i<lv_obj_get_child_count(o);i++){lv_obj_t *l=lv_obj_get_child(o,i);
        if(lv_obj_check_type(l,&lv_label_class)){const char *t=lv_label_get_text(l);if(t&&(uint8_t)t[0]==0xEF)lv_obj_set_style_text_color(l,TC(ACCENT_PRIMARY),0);}}
    if(orange_icons&&lv_obj_check_type(o,&lv_label_class))lv_obj_set_style_text_color(o,TC(ACCENT_PRIMARY),0);
}
/* A subtle emboss: a 1 px light edge along the top and, for raised parts, a soft drop below. */
void br2_emboss(lv_obj_t *o,int raised){
    int light=theme_variant()!=0;
    lv_obj_set_style_border_side(o,LV_BORDER_SIDE_TOP,0);lv_obj_set_style_border_width(o,1,0);
    lv_obj_set_style_border_color(o,light?TC(ON_ACCENT):TC(TEXT_PRIMARY),0);lv_obj_set_style_border_opa(o,light?170:34,0);
    if(raised){lv_obj_set_style_shadow_color(o,TC(FIXED_MEDIA_BLACK),0);lv_obj_set_style_shadow_width(o,8,0);lv_obj_set_style_shadow_offset_y(o,2,0);lv_obj_set_style_shadow_opa(o,light?34:90,0);}
}
/* The header title steps down 22 -> 18 -> 16 px to fit beside the back knob, then clips; it refits
 * whenever a screen changes the text (album and folder names). */
#define BR2_HDR_Y 50      /* the knob's and the title's shared centre line, inside the circle */
#define BR2_HDR_X 132
#define BR2_HDR_W 160     /* inside the circle at that height */
static void br2_header_fit(lv_obj_t *l){
    const lv_font_t *steps[3]={br_font(22,1),br_font(18,1),br_font(16,1)},*f=steps[2];
    const char *t=lv_label_get_text(l);
    for(int k=0;k<3;k++){lv_point_t sz;lv_text_get_size(&sz,t,steps[k],0,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);if(sz.x<=BR2_HDR_W){f=steps[k];break;}}
    if(lv_obj_get_style_text_font(l,0)!=f)lv_obj_set_style_text_font(l,f,0);
    int h=lv_font_get_line_height(f);lv_obj_set_height(l,h);lv_obj_set_y(l,BR2_HDR_Y-h/2);
}
static void br2_header_refit(void *l){if(lv_obj_is_valid(l))br2_header_fit(l);}
static void br2_header_text_cb(lv_event_t *e){   /* a screen set new text: refit after this frame */
    lv_obj_t *l=lv_event_get_target(e);uint32_t h=0;for(const char *c=lv_label_get_text(l);*c;c++)h=h*31+(uint8_t)*c;
    if((uint32_t)(uintptr_t)lv_obj_get_user_data(l)!=h){lv_obj_set_user_data(l,(void *)(uintptr_t)h);lv_async_call(br2_header_refit,l);}
}
lv_obj_t *br2_header(lv_obj_t *root,const char *text,lv_event_cb_t cb){
    lv_obj_t *cap=lv_obj_create(root);lv_obj_remove_style_all(cap);lv_obj_set_size(cap,304,72);lv_obj_set_pos(cap,28,-4);
    lv_obj_set_style_radius(cap,28,0);lv_obj_set_style_bg_color(cap,TC(SURFACE_STRONG),0);lv_obj_set_style_bg_opa(cap,LV_OPA_COVER,0);lv_obj_remove_flag(cap,LV_OBJ_FLAG_CLICKABLE);br2_emboss(cap,1);
    lv_obj_t *b=br_button(root,108,BR2_HDR_Y,18,LV_SYMBOL_LEFT,TF(UI_20),0);br2_depth(b,1);   /* edge at 168 px from the centre */ui_on(b,cb?cb:back,LV_EVENT_CLICKED,NULL,"nav.back",UI_CORE);lv_obj_set_ext_click_area(b,6);
    lv_obj_t *l=br_label(root,text,br_font(22,1),BR_TXT);lv_obj_set_width(l,BR2_HDR_W);lv_obj_set_x(l,BR2_HDR_X);lv_label_set_long_mode(l,LV_LABEL_LONG_CLIP);   /* not DOT: it rewrites the text the refit measures */
    br2_header_fit(l);lv_obj_add_event_cb(l,br2_header_text_cb,LV_EVENT_DRAW_MAIN_BEGIN,NULL);
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
        lv_obj_set_style_bg_opa(o,LV_OPA_COVER,LV_STATE_PRESSED);br2_emboss(o,0);break;
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
        lv_obj_set_style_bg_color(o,role==KIT_ICON_BUTTON?TC(CONTROL_KNOB):TC(SURFACE_STRONG),0);lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);br2_depth(o,0);
        for(uint32_t i=0;i<lv_obj_get_child_count(o);i++){   /* icon glyphs glow orange; text stays neutral */
            lv_obj_t *c=lv_obj_get_child(o,i);if(!lv_obj_check_type(c,&lv_label_class))continue;
            const char *t=lv_label_get_text(c);int icon=t&&(uint8_t)t[0]==0xEF;
            lv_obj_set_style_text_color(c,icon?TC(ACCENT_PRIMARY):role==KIT_ICON_BUTTON?TC(ON_CONTROL):TC(TEXT_PRIMARY),0);
        }
        lv_obj_set_style_shadow_color(o,TC(ACCENT_PRIMARY),0);lv_obj_set_style_shadow_width(o,14,0);lv_obj_set_style_shadow_opa(o,46,0);
        br2_emboss(o,0);   /* the glow is its shadow; the light top edge gives the relief */
        lv_obj_set_style_bg_color(o,TC(SURFACE_PRESSED),LV_STATE_PRESSED);break;
    case KIT_POPUP:
        lv_obj_set_style_bg_color(o,TC(SURFACE_STRONG),0);lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);
        lv_obj_set_style_radius(o,28,0);lv_obj_set_style_border_color(o,TC(BORDER),0);break;
    case KIT_TEXTAREA:lv_obj_set_style_bg_color(o,TC(SURFACE_STRONG),0);lv_obj_set_style_radius(o,24,0);lv_obj_set_style_text_color(o,TC(TEXT_PRIMARY),0);break;
    default:break;
    }
}
const theme_kit_t theme_kit_braun2={.id="braun2",.header=br2_header,.style=br2_style,.home_build=bhome_create,.fork_np_layout=kit_fork_np_braun2};
