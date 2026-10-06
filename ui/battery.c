/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "battery.h"
#include "theme.h"
#include "theme_kit.h"
void battery_arc_create(battery_arc_t *b,lv_obj_t *root,int diameter,int width,int start,int end,int reverse){
 b->state=-1;b->arc=lv_arc_create(root);lv_obj_t *a=b->arc;kit_keep(a);
 lv_obj_remove_style(a,NULL,LV_PART_KNOB);lv_obj_clear_flag(a,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
 lv_obj_set_size(a,diameter,diameter);lv_obj_center(a);lv_arc_set_rotation(a,0);
 lv_arc_set_bg_angles(a,start,end);lv_arc_set_mode(a,reverse?LV_ARC_MODE_REVERSE:LV_ARC_MODE_NORMAL);
 lv_arc_set_range(a,0,100);lv_arc_set_value(a,0);
 lv_obj_set_style_arc_width(a,width,LV_PART_MAIN);lv_obj_set_style_arc_color(a,TC(CONTROL_TRACK_STRONG),LV_PART_MAIN);
 lv_obj_set_style_arc_width(a,width,LV_PART_INDICATOR);lv_obj_set_style_arc_rounded(a,true,LV_PART_MAIN);lv_obj_set_style_arc_rounded(a,true,LV_PART_INDICATOR);
}
void battery_arc_set(battery_arc_t *b,int pct,int charging){
 if(!b->arc||pct<0)return;if(pct>100)pct=100;
 int state=charging?2:pct<=15?1:0;
 if(lv_arc_get_value(b->arc)!=pct)lv_arc_set_value(b->arc,pct);
 if(state!=b->state){b->state=state;lv_obj_set_style_arc_color(b->arc,state==2?TC(STATUS_SUCCESS):state==1?TC(STATUS_DANGER):TC(TEXT_PRIMARY),LV_PART_INDICATOR);}
}
