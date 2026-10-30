/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "screens.h"
#include "config.h"
#include "theme.h"
LV_FONT_DECLARE(font_np_inter_30)
LV_FONT_DECLARE(font_np_inter_22)
LV_FONT_DECLARE(font_np_nunito_30)
LV_FONT_DECLARE(font_np_nunito_22)
/* Rasterized once at build time; international/CJK fallbacks remain available. */
const lv_font_t *ui_np_font(int size){
    int choice=cfg_get_int("np_font",0);
    if(choice<1 || choice>2)return ui_font_cjk(size);
    static lv_font_t fonts[4];static int ready;
    if(!ready){
        const lv_font_t *base[]={&font_np_inter_30,&font_np_inter_22,&font_np_nunito_30,&font_np_nunito_22};
        for(int i=0;i<4;i++){fonts[i]=*base[i];fonts[i].fallback=ui_font_cjk(i%2?20:28);}
        ready=1;
    }
    return &fonts[(choice-1)*2+(size>=24?0:1)];
}

/* Reveal a highlighted row briefly, then stop repainting and return to ellipsis. */
static void reveal_done(lv_anim_t *a){lv_label_set_long_mode((lv_obj_t *)a->var,LV_LABEL_LONG_SCROLL_CIRCULAR);}
void ui_reveal_title(lv_obj_t *label){
    static lv_anim_t hint;static int ready;
    if(!ready){lv_anim_init(&hint);lv_anim_set_repeat_count(&hint,2);
        lv_anim_set_delay(&hint,1000);lv_anim_set_repeat_delay(&hint,1200);
        lv_anim_set_completed_cb(&hint,reveal_done);ready=1;}
    lv_obj_set_style_anim(label,&hint,0);lv_label_set_long_mode(label,LV_LABEL_LONG_SCROLL_CIRCULAR);
}

/* Music metadata runs exactly three cycles, then rests at its start. */
static void music_scroll_done(lv_anim_t *a){
    lv_obj_t *label=a->var;
    lv_label_set_long_mode(label,LV_LABEL_LONG_CLIP);
    if(th_simple()){
        lv_point_t size;lv_text_get_size(&size,lv_label_get_text(label),lv_obj_get_style_text_font(label,0),
            lv_obj_get_style_text_letter_space(label,0),lv_obj_get_style_text_line_space(label,0),LV_COORD_MAX,LV_TEXT_FLAG_NONE);
        if(size.x>lv_obj_get_content_width(label))lv_obj_set_style_text_align(label,LV_TEXT_ALIGN_LEFT,0);
    }
}
void ui_music_scroll(lv_obj_t *label){
    static lv_anim_t motion;static int ready;
    if(!label)return;
    if(th_braun2()){lv_label_set_long_mode(label,LV_LABEL_LONG_DOT);return;}   /* Braun 2.0 wraps or truncates; nothing scrolls */
    /* Restore the saved alignment for a new title; short text remains centred. */
    if(th_simple())lv_obj_set_style_text_align(label,cfg_get_int("simple_title_al",0)==1?LV_TEXT_ALIGN_LEFT:LV_TEXT_ALIGN_CENTER,0);
    if(!ready){lv_anim_init(&motion);lv_anim_set_repeat_count(&motion,3);
        lv_anim_set_delay(&motion,1000);lv_anim_set_repeat_delay(&motion,1200);
        lv_anim_set_completed_cb(&motion,music_scroll_done);ready=1;}
    lv_obj_set_style_anim(label,&motion,0);
    lv_label_set_long_mode(label,LV_LABEL_LONG_CLIP);
    lv_label_set_long_mode(label,LV_LABEL_LONG_SCROLL_CIRCULAR);
}

/* One quiet library list: only the visible row nearest the screen centre scrolls.
 * Focus events can also select a row; virtual row deletion is handled by LVGL. */
static void list_text_mode(lv_obj_t *row,int focused){
    for(uint32_t k=0;k<lv_obj_get_child_count(row);k++){
        lv_obj_t *l=lv_obj_get_child(row,k);
        if(!lv_obj_check_type(l,&lv_label_class))continue;
        lv_label_long_mode_t want=focused?LV_LABEL_LONG_SCROLL_CIRCULAR:LV_LABEL_LONG_DOT;
        if(lv_label_get_long_mode(l)!=want)lv_label_set_long_mode(l,want);
    }
}
static void library_row_focus(lv_event_t *e){list_text_mode(lv_event_get_target(e),1);}
static int library_row_marked(lv_obj_t *row){
    for(uint32_t i=0;i<lv_obj_get_event_count(row);i++)
        if(lv_event_dsc_get_cb(lv_obj_get_event_dsc(row,i))==library_row_focus)return 1;
    return 0;
}
static void list_focus_update(lv_timer_t *t){
    lv_obj_t *list=lv_timer_get_user_data(t);
    /* Pushed pages leave their previous root visible underneath. A covered
     * folder list must still stop its title animation. */
    int covered_folder=th_zune()&&lv_obj_get_parent(list)==screen_get_root(SCR_FOLDER)&&screen_current()!=SCR_FOLDER;
    if(!lv_obj_is_visible(list)||covered_folder){                     /* hidden: no row scrolls (it kept animating, and any running */
        for(uint32_t i=0;i<lv_obj_get_child_count(list);i++){   /* animation keeps the main loop in its busy pace) */
            lv_obj_t *r=lv_obj_get_child(list,i);
            if(library_row_marked(r))list_text_mode(r,0);
        }
        return;
    }
    lv_obj_t *nearest=NULL;int distance=10000;
    for(uint32_t i=0;i<lv_obj_get_child_count(list);i++){
        lv_obj_t *r=lv_obj_get_child(list,i);
        if(!library_row_marked(r) || lv_obj_has_flag(r,LV_OBJ_FLAG_HIDDEN))continue;
        lv_area_t a;lv_obj_get_coords(r,&a);
        if(a.y2<72 || a.y1>318)continue;
        int d=LV_ABS((a.y1+a.y2)/2-180);
        if(lv_obj_has_state(r,LV_STATE_FOCUSED))d=-1;
        if(d<distance){distance=d;nearest=r;}
    }
    for(uint32_t i=0;i<lv_obj_get_child_count(list);i++){
        lv_obj_t *r=lv_obj_get_child(list,i);
        if(library_row_marked(r))list_text_mode(r,r==nearest);
    }
}
static void list_watch_delete(lv_event_t *e){lv_timer_delete(lv_event_get_user_data(e));}
void ui_library_list_watch(lv_obj_t *list){
    lv_timer_t *timer=lv_timer_create(list_focus_update,200,list);
    lv_obj_add_event_cb(list,list_watch_delete,LV_EVENT_DELETE,timer);
}
void ui_library_row(lv_obj_t *row){
    lv_obj_add_event_cb(row,library_row_focus,LV_EVENT_FOCUSED,NULL);list_text_mode(row,0);
}
