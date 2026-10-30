/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The "A - Z" pill and its letter grid, shared by the Library lists and Folders.
 * The pill opens a 5-column grid of A..Z and #; a tap on a letter jumps the list (the owner's callback) and closes the
 * grid, a tap outside closes it. Disco: a pill at the bottom of the screen (the rim's middle belongs to the navigation
 * circle); other themes: a round button on the right rim. The pill is 84 x 36 with 10 px of extra touch area around it
 * (it was 68 x 30 with none, and easy to miss). */
#include "azjump.h"
#include "screens.h"
#include "theme.h"
#include "theme_kit.h"
#include "i18n.h"
#include <stdint.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <ctype.h>
#include "config.h"
#include "null_theme.h"
#include "braun.h"
#include <stdlib.h>

/* Letter wheel is allocated only while open. No per-song data or artwork. */
static azjump_t *active_wheel,*pickers[4];
static const char wheel_letters[]="#ABCDEFGHIJKLMNOPQRSTUVWXYZ";   /* # first: the wheel starts at the very beginning */
static int rim_mode(void);
static int wheel_mode(azjump_t *a){return a->library&&cfg_get_int("library_seeking",0)==1;}
int azjump_active(void){return active_wheel!=NULL;}
static void wheel_close(azjump_t *a,int commit){
    if(!a||!a->wheel)return;
    char letter=wheel_letters[a->current];
    if(a->timeout){lv_timer_delete(a->timeout);a->timeout=NULL;}
    lv_obj_t *ov=a->wheel;a->wheel=NULL;a->selected=NULL;a->focus=NULL;active_wheel=NULL;
    a->tracking=a->held=0;
    if(rim_mode()){lv_obj_remove_flag(ov,LV_OBJ_FLAG_CLICKABLE);lv_obj_fade_out(ov,140,0);lv_obj_delete_delayed(ov,160);   /* eases out */
                   if(a->btn&&wheel_mode(a))lv_obj_remove_flag(a->btn,LV_OBJ_FLAG_HIDDEN);}
    else lv_obj_delete(ov);
    if(commit&&a->jump)a->jump(letter);
}
int azjump_close_active(void){if(!active_wheel)return 0;wheel_close(active_wheel,0);return 1;}
int azjump_owns_point(int x,int y){
    if(active_wheel)return 1;
    for(int i=0;i<4;i++){
      azjump_t*a=pickers[i];if(!a||!wheel_mode(a)||!a->btn||!lv_obj_is_visible(a->btn))continue;
      lv_area_t box;lv_obj_get_coords(a->btn,&box);
      if(x>=box.x1-8&&x<=box.x2+8&&y>=box.y1-8&&y<=box.y2+8)return 1;
    }return 0;
}
/* Focus: the alphabet turns beneath a fixed twelve-o'clock selection marker.
 * Cache the 27 positions once; repaint only after a selected-letter change. */
static int focus_x[27],focus_y[27],focus_positions_ready;
static char wheel_glyph(char c){return th_zune()&&c>='A'&&c<='Z'?(char)(c+32):c;}
static const lv_font_t *wheel_font(int big){
    if(th_braun_family())return br_font(big?56:18,0);
    if(th_null())return big?TF(UI_36):TF(UI_16);
    if(th_zune()){
        LV_FONT_DECLARE(font_zune_light_50)
        LV_FONT_DECLARE(font_zune_regular_19)
        return big?&font_zune_light_50:&font_zune_regular_19;
    }
    return big?TF(UI_40):TF(UI_18);
}
static void wheel_paint(azjump_t*a){
    char text[2]={wheel_glyph(wheel_letters[a->current]),0};lv_label_set_text(a->selected,text);
    lv_obj_center(a->selected);
    if(!focus_positions_ready){
        for(int i=0;i<27;i++){
            float angle=(-90+i*360.0f/27)*3.14159265f/180;
            focus_x[i]=166+(int)lroundf(134*cosf(angle));
            focus_y[i]=166+(int)lroundf(134*sinf(angle));
        }
        focus_positions_ready=1;
    }
    for(int i=0;i<27;i++){
        int slot=(i-a->current+27)%27,distance=slot>13?27-slot:slot;
        lv_obj_set_pos(a->letter[i],focus_x[slot],focus_y[slot]);
        lv_obj_set_style_text_color(a->letter[i],i==a->current?TC(ON_ACCENT):TC(TEXT_SECONDARY),0);
        lv_obj_set_style_text_opa(a->letter[i],i==a->current?255:distance<4?245:distance<8?170:110,0);
    }
}
/* the letter wheel as a rim index. The letters near the current one stand along the right
 * edge - the current one large on an accent disc at three o'clock, its neighbours smaller and greyer - and slide as
 * the finger moves up or down. The list behind stays in view and jumps live with every letter. */
static int rim_mode(void){return th_disco_family()||th_null()||th_zune();}   /* every theme but Ring and Braun */
int disco_mirror(void);
static int rim_left(void){return disco_mirror();}   /* left-handed Disco: the index on the left edge, all else mirrored */
void azjump_index(azjump_t*a,const char*first,int n,char at){if(a){a->first=first;a->nfirst=n;a->at=at;}}
/* which of the 27 letters the list has (all of them when the list didn't say) */
static void present_letters(azjump_t*a,unsigned char out[27]){
    if(!a->first||a->nfirst<=0){memset(out,1,27);return;}
    memset(out,0,27);
    for(int i=0;i<a->nfirst;i++){const char*p=strchr(wheel_letters,a->first[i]);out[p&&*p?(int)(p-wheel_letters):0]=1;}
}
#define RIM_R 146
#define RIM_STEP 22   /* px of finger travel per letter */
static void rim_paint(azjump_t*a){
    for(int i=0;i<27;i++){
        lv_obj_t*l=a->letter[i];int k=i-a->current,ad=abs(k);
        if(ad>6){lv_obj_add_flag(l,LV_OBJ_FLAG_HIDDEN);continue;}
        lv_obj_remove_flag(l,LV_OBJ_FLAG_HIDDEN);
        float deg=k==0?0:(k>0?1:-1)*(15.5f+(ad-1)*9.5f);float ang=deg*3.14159265f/180;   /* more room either side of the big letter */
        int dxr=(int)lroundf(RIM_R*cosf(ang)),x=rim_left()?180-dxr:180+dxr,y=180+(int)lroundf(RIM_R*sinf(ang));
        const lv_font_t*f=ad==0?TF(UI_28):ad==1?TF(UI_20):ad<=3?TF(UI_18):TF(UI_16);
        lv_obj_set_style_text_font(l,f,0);
        unsigned char have[27];present_letters(a,have);
        uint8_t fade=(uint8_t)(ad==1?40:ad<=3?110:170);if(!have[i]&&ad)fade=(uint8_t)(fade<200?215:230);   /* nothing under it: very dim */
        lv_obj_set_style_text_color(l,ad==0?TC(ON_ACCENT):theme_color_mix(TC(CANVAS),TC(TEXT_PRIMARY),fade),0);
        lv_obj_set_style_pad_top(l,(40-lv_font_get_line_height(f))/2,0);
        lv_obj_set_pos(l,x-20,y-20);
    }
    lv_obj_set_pos(a->focus,(rim_left()?180-RIM_R:180+RIM_R)-23,180-23);
}
static void rim_pointer(azjump_t*a,int begin){   /* turn: clockwise = on through the alphabet, counter-clockwise = back */
    lv_indev_t*in=lv_indev_active();if(!in||!a->wheel)return;
    lv_point_t p;lv_indev_get_point(in,&p);int dx=p.x-180,dy=p.y-180;a->activity=lv_tick_get();
    if(dx*dx+dy*dy<60*60){a->tracking=0;return;}                 /* the middle: no direction to read */
    float ang=atan2f((float)dy,(float)dx);                        /* screen y runs down: the angle grows clockwise */
    if(begin||!a->tracking){a->tracking=1;a->angle=ang;if(begin){a->travel=0;a->moved=0;}return;}
    float d=ang-a->angle;a->angle=ang;
    if(rim_left())d=-d;   /* left edge: sliding down is counter-clockwise there - it still moves on, as on the right */
    if(d>3.14159265f)d-=6.2831853f;
    if(d<-3.14159265f)d+=6.2831853f;
    if(fabsf(d)>1.2f)return;                                      /* a jump of the finger, not a turn */
    a->travel+=d;const float step=12.0f*3.14159265f/180;          /* 12 degrees of turn per letter */
    int cur=a->current;
    while(a->travel>=step&&cur<26){cur++;a->travel-=step;}
    while(a->travel<=-step&&cur>0){cur--;a->travel+=step;}
    if(cur==0&&a->travel<0)a->travel=0;
    if(cur==26&&a->travel>0)a->travel=0;
    if(cur!=a->current){a->current=cur;a->moved=1;rim_paint(a);if(a->jump)a->jump(wheel_letters[cur]);}   /* the list follows live */
}
static void wheel_pointer(azjump_t*a,int begin){
    if(rim_mode()){rim_pointer(a,begin);return;}
    lv_indev_t*in=lv_indev_active();if(!in||!a->wheel)return;
    lv_point_t p;lv_indev_get_point(in,&p);int dx=p.x-180,dy=p.y-180;
    a->activity=lv_tick_get();
    if(dx*dx+dy*dy<80*80){a->tracking=0;return;}
    float angle=atan2f((float)dy,(float)dx);
    if(begin||!a->tracking){a->tracking=1;a->angle=angle;a->travel=0;return;}
    float delta=angle-a->angle;a->angle=angle;
    if(delta>3.14159265f)delta-=6.2831853f;
    if(delta< -3.14159265f)delta+=6.2831853f;
    /* Reject a discontinuous touch jump instead of skipping half the alphabet. */
    if(fabsf(delta)>1.2f){a->travel=0;return;}
    a->travel+=delta;const float step=6.2831853f/27;
    int old=a->current;
    while(a->travel>=step){a->current=(a->current+1)%27;a->travel-=step;}
    while(a->travel<=-step){a->current=(a->current+26)%27;a->travel+=step;}
    if(a->current!=old)wheel_paint(a);
}
static void wheel_event(lv_event_t*e){
    azjump_t*a=lv_event_get_user_data(e);lv_event_code_t c=lv_event_get_code(e);
    if(c==LV_EVENT_PRESSED){a->held=1;wheel_pointer(a,1);}
    else if(c==LV_EVENT_PRESSING)wheel_pointer(a,0);
    else if(c==LV_EVENT_RELEASED||c==LV_EVENT_PRESS_LOST){a->held=0;a->tracking=0;a->activity=lv_tick_get();}
}
static void wheel_commit(lv_event_t*e){wheel_close(lv_event_get_user_data(e),1);}
static void wheel_timeout(lv_timer_t*t){
    azjump_t*a=lv_timer_get_user_data(t);
    if(!a->held&&lv_tick_elaps(a->activity)>=1000)wheel_close(a,!rim_mode());   /* the rim index has jumped already */
}
static lv_obj_t*wheel_text(lv_obj_t*p,const char*t,const lv_font_t*f,lv_color_t color){
    lv_obj_t*l=lv_label_create(p);lv_label_set_text(l,t);lv_obj_set_style_text_font(l,f,0);
    lv_obj_set_style_text_color(l,color,0);lv_obj_remove_flag(l,LV_OBJ_FLAG_CLICKABLE);kit_keep(l);return l;
}
static lv_obj_t *wheel_arc(lv_obj_t *parent,int radius,int width,lv_color_t color,
                           int opacity,int start,int end){
    lv_obj_t *arc=lv_arc_create(parent);lv_obj_remove_style_all(arc);kit_keep(arc);
    lv_obj_set_size(arc,radius*2,radius*2);lv_obj_set_pos(arc,180-radius,180-radius);
    lv_arc_set_bg_angles(arc,start,end);lv_arc_set_angles(arc,0,0);
    lv_obj_set_style_arc_width(arc,width,LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc,color,LV_PART_MAIN);
    lv_obj_set_style_arc_opa(arc,opacity,LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc,1,LV_PART_MAIN);
    lv_obj_set_style_arc_opa(arc,0,LV_PART_INDICATOR);
    lv_obj_remove_flag(arc,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);return arc;
}
static void rim_tap(lv_event_t*e){   /* a tap (no slide) anywhere closes the index */
    azjump_t*a=lv_event_get_user_data(e);if(a&&a->wheel&&!a->held&&!a->moved)wheel_close(a,0);   /* not after a turn */
}
static void rim_open(azjump_t*a){
    azjump_close_active();lv_obj_add_flag(a->grid,LV_OBJ_FLAG_HIDDEN);
    a->wheel=lv_obj_create(lv_layer_top());lv_obj_remove_style_all(a->wheel);
    lv_obj_set_size(a->wheel,360,360);lv_obj_remove_flag(a->wheel,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(a->wheel,LV_OBJ_FLAG_CLICKABLE);kit_keep(a->wheel);
    lv_obj_add_event_cb(a->wheel,wheel_event,LV_EVENT_ALL,a);
    lv_obj_add_event_cb(a->wheel,rim_tap,LV_EVENT_SHORT_CLICKED,a);
    /* a soft shade under the letters only: the list stays readable on the left */
    /* a long, gentle ramp (a third of the screen) into the theme's own background, mirrored for left-handed Disco */
    {   /* a pre-rendered, dithered ramp (no colour steps), then solid under the letters */
        const lv_image_dsc_t*curvelist_fade_strip(int vertical,int len,int opaque_end,lv_color_t col);
        const lv_image_dsc_t*ramp=curvelist_fade_strip(0,140,rim_left()?0:1,TC(CANVAS));
        lv_obj_t*sh=lv_image_create(a->wheel);kit_keep(sh);
        if(ramp){lv_image_set_src(sh,ramp);lv_image_set_inner_align(sh,LV_IMAGE_ALIGN_TILE);}
        lv_obj_set_size(sh,140,360);lv_obj_set_pos(sh,rim_left()?100:120,0);lv_obj_remove_flag(sh,LV_OBJ_FLAG_CLICKABLE);
        lv_obj_t*so=lv_obj_create(a->wheel);lv_obj_remove_style_all(so);kit_keep(so);
        lv_obj_set_size(so,100,360);lv_obj_set_pos(so,rim_left()?0:260,0);
        lv_obj_set_style_bg_color(so,TC(CANVAS),0);lv_obj_set_style_bg_opa(so,LV_OPA_COVER,0);
        lv_obj_remove_flag(so,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
    }
    a->focus=lv_obj_create(a->wheel);lv_obj_remove_style_all(a->focus);kit_keep(a->focus);
    lv_obj_set_size(a->focus,46,46);lv_obj_set_style_radius(a->focus,th_null()?4:LV_RADIUS_CIRCLE,0);
    lv_obj_set_style_bg_color(a->focus,TC(ACCENT_PRIMARY),0);lv_obj_set_style_bg_opa(a->focus,LV_OPA_COVER,0);
    lv_obj_remove_flag(a->focus,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
    for(int i=0;i<27;i++){
        char text[2]={wheel_glyph(wheel_letters[i]),0};
        lv_obj_t*l=wheel_text(a->wheel,text,TF(UI_18),TC(TEXT_SECONDARY));a->letter[i]=l;
        lv_obj_set_size(l,40,40);lv_obj_set_style_text_align(l,LV_TEXT_ALIGN_CENTER,0);
    }
    if(a->at){const char*p=strchr(wheel_letters,a->at);if(p&&*p)a->current=(int)(p-wheel_letters);}
    a->selected=NULL;a->activity=lv_tick_get();a->tracking=0;a->moved=0;active_wheel=a;rim_paint(a);
    lv_obj_fade_in(a->wheel,160,0);   /* eases in over the list */
    if(a->btn)lv_obj_add_flag(a->btn,LV_OBJ_FLAG_HIDDEN);   /* the index takes the dot's place while open */
    a->timeout=lv_timer_create(wheel_timeout,100,a);
}
static void wheel_open(azjump_t*a){
    if(a->wheel)return;
    if(rim_mode()){rim_open(a);return;}
    azjump_close_active();lv_obj_add_flag(a->grid,LV_OBJ_FLAG_HIDDEN);
    a->wheel=lv_obj_create(lv_layer_top());lv_obj_remove_style_all(a->wheel);
    lv_obj_set_size(a->wheel,360,360);lv_obj_set_style_bg_color(a->wheel,TC(CANVAS),0);
    lv_obj_set_style_bg_opa(a->wheel,246,0);lv_obj_remove_flag(a->wheel,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(a->wheel,LV_OBJ_FLAG_CLICKABLE);kit_keep(a->wheel);
    lv_obj_add_event_cb(a->wheel,wheel_event,LV_EVENT_ALL,a);
    /* Static soft highlights; no blur, extra image buffers or animation timers. */
    wheel_arc(a->wheel,151,2,TC(BORDER),110,0,360);
    wheel_arc(a->wheel,151,2,TC(ACCENT_PRIMARY),220,252,288);
    for(int j=0;j<5;j++)wheel_arc(a->wheel,158-j,15-2*j,TC(ACCENT_PRIMARY),8+6*j,255,285);
    wheel_arc(a->wheel,112,1,TC(BORDER),60,0,360);
    a->focus=lv_obj_create(a->wheel);lv_obj_remove_style_all(a->focus);kit_keep(a->focus);
    lv_obj_set_size(a->focus,34,34);lv_obj_set_pos(a->focus,163,29);
    lv_obj_set_style_radius(a->focus,th_null()?3:LV_RADIUS_CIRCLE,0);
    lv_obj_set_style_bg_color(a->focus,TC(ACCENT_PRIMARY),0);lv_obj_set_style_bg_opa(a->focus,255,0);
    lv_obj_set_style_shadow_color(a->focus,TC(ACCENT_PRIMARY),0);
    lv_obj_set_style_shadow_width(a->focus,14,0);lv_obj_set_style_shadow_opa(a->focus,35,0);
    lv_obj_remove_flag(a->focus,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
    const lv_font_t *small=wheel_font(0);
    for(int i=0;i<27;i++){
        char text[2]={wheel_glyph(wheel_letters[i]),0};
        lv_obj_t *l=wheel_text(a->wheel,text,small,TC(TEXT_SECONDARY));a->letter[i]=l;
        lv_obj_set_size(l,28,28);lv_obj_set_style_text_align(l,LV_TEXT_ALIGN_CENTER,0);
        lv_obj_set_style_pad_top(l,(28-lv_font_get_line_height(small))/2,0);
    }
    /* Keep a generous, transparent centre tap target. No heading above the letter. */
    lv_obj_t *hub=lv_button_create(a->wheel);lv_obj_remove_style_all(hub);kit_keep(hub);
    lv_obj_set_size(hub,96,96);lv_obj_set_pos(hub,132,126);
    lv_obj_remove_flag(hub,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(hub,wheel_commit,LV_EVENT_SHORT_CLICKED,a);lv_obj_add_flag(hub,LV_OBJ_FLAG_EVENT_BUBBLE);
    a->selected=wheel_text(hub,"",wheel_font(1),TC(TEXT_PRIMARY));
    lv_obj_t *hint=wheel_text(a->wheel,th_zune()?"tap to jump":"TAP TO JUMP",TF(UI_12),TC(ACCENT_PRIMARY));
    lv_obj_set_size(hint,170,lv_font_get_line_height(TF(UI_12))+4);
    lv_obj_set_style_text_align(hint,LV_TEXT_ALIGN_CENTER,0);
    lv_obj_set_pos(hint,95,234-(lv_font_get_line_height(TF(UI_12))+4)/2);
    lv_obj_t *line=lv_obj_create(a->wheel);lv_obj_remove_style_all(line);kit_keep(line);
    lv_obj_set_size(line,32,2);lv_obj_set_pos(line,164,260);lv_obj_set_style_radius(line,1,0);
    lv_obj_set_style_bg_color(line,TC(ACCENT_PRIMARY),0);lv_obj_set_style_bg_opa(line,100,0);
    lv_obj_remove_flag(line,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
    a->activity=lv_tick_get();a->tracking=0;active_wheel=a;wheel_paint(a);
    a->timeout=lv_timer_create(wheel_timeout,100,a);
    /* Allocation failure leaves a working centre/back dismissal, never a dead overlay. */
}
void azjump_position(azjump_t*a,char letter,int x,int y){
    if(!wheel_mode(a)||!a->btn||(lv_obj_has_flag(a->btn,LV_OBJ_FLAG_HIDDEN)&&!a->wheel))return;
    const char*p=strchr(wheel_letters,letter);
    if(!a->wheel)a->current=p?(int)(p-wheel_letters):0;
    if(rim_mode()){   /* one fixed place - the A - Z pill's spot at the bottom */
        /* Squash: on the right edge, at the middle (left-handed: the left edge); the themes with navigation at the
         * foot keep it at the bottom, where the A - Z pill sits */
        int px=180,py=334;
        if(th_simple()){px=rim_left()?20+4:340-4;py=180;}
        lv_obj_set_size(a->btn,34,34);lv_obj_set_pos(a->btn,px-17,py-17);
        lv_obj_set_style_radius(a->btn,th_null()?3:LV_RADIUS_CIRCLE,0);
        lv_obj_set_style_bg_color(a->btn,TC(SURFACE_RAISED),0);lv_obj_set_style_bg_opa(a->btn,110,0);
        lv_obj_set_style_border_width(a->btn,1,0);lv_obj_set_style_border_color(a->btn,TC(BORDER),0);kit_keep(a->btn);
        lv_obj_t*l=lv_obj_get_child(a->btn,0);char text[2]={wheel_glyph(wheel_letters[a->current]),0};
        if(strcmp(lv_label_get_text(l),text))lv_label_set_text(l,text);
        lv_obj_set_style_text_font(l,TF(UI_16),0);lv_obj_set_style_text_color(l,TC(TEXT_SECONDARY),0);lv_obj_center(l);kit_keep(l);   /* dimmed: a hint */
        (void)x;(void)y;return;
    }
    lv_obj_set_size(a->btn,28,28);lv_obj_set_pos(a->btn,x-14,y-14);
    lv_obj_set_style_radius(a->btn,th_null()?0:LV_RADIUS_CIRCLE,0);
    lv_obj_set_style_bg_color(a->btn,TC(ACCENT_PRIMARY),0);lv_obj_set_style_bg_opa(a->btn,LV_OPA_COVER,0);kit_keep(a->btn);
    lv_obj_t*l=lv_obj_get_child(a->btn,0);char text[2]={wheel_glyph(wheel_letters[a->current]),0};lv_label_set_text(l,text);
    lv_obj_set_style_text_font(l,TF(UI_18),0);lv_obj_set_style_text_color(l,TC(ON_ACCENT),0);lv_obj_center(l);kit_keep(l);
}
static void wheel_button(lv_event_t*e){
    azjump_t*a=lv_event_get_user_data(e);if(!wheel_mode(a))return;
    lv_event_code_t c=lv_event_get_code(e);
    if(c==LV_EVENT_PRESSED)a->held=0;
    else if(c==LV_EVENT_LONG_PRESSED){a->held=1;wheel_open(a);wheel_pointer(a,1);}
    else if(c==LV_EVENT_PRESSING&&a->wheel)wheel_pointer(a,0);
    else if((c==LV_EVENT_RELEASED||c==LV_EVENT_PRESS_LOST)&&a->wheel){a->held=0;a->activity=lv_tick_get();a->tracking=0;}
}


static void grid_close(azjump_t *a){
    if(!a || !a->grid) return;
    lv_obj_add_flag(a->grid, LV_OBJ_FLAG_HIDDEN);
    if(a->btn && lv_obj_get_parent(a->grid) != lv_obj_get_parent(a->btn)) lv_obj_set_parent(a->grid, lv_obj_get_parent(a->btn));   /* home again */
}
static void letter_cb(lv_event_t *e){
    if(lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    lv_obj_t *cell = lv_event_get_current_target(e);
    azjump_t *a = lv_obj_get_user_data(lv_obj_get_parent(cell));
    char L = (char)(intptr_t)lv_event_get_user_data(e);
    if(a){ grid_close(a); if(a->jump) a->jump(L); }
}
static void grid_bg_cb(lv_event_t *e){                       /* a tap outside the letters closes the grid */
    if(lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    lv_obj_t *g = lv_event_get_current_target(e); azjump_t *a = lv_obj_get_user_data(g);
    if(a) grid_close(a); else lv_obj_add_flag(g, LV_OBJ_FLAG_HIDDEN);
}
/* the keypad as it opens: above everything (Null keeps its lists in a higher layer), letters the list doesn't have
 * dimmed and untappable, the list's current letter filled in the accent */
static void keypad_prepare(azjump_t *a){
    if(!rim_mode() || th_zune()) return;
    lv_obj_set_parent(a->grid, lv_layer_top());
    lv_obj_set_style_bg_color(a->grid, TC(CANVAS), 0); lv_obj_set_style_bg_opa(a->grid, 250, 0);   /* set as it opens: no theme pass undoes it */
    unsigned char have[27]; present_letters(a, have);
    const char *p = a->at ? strchr(wheel_letters, a->at) : NULL; int cur = p && *p ? (int)(p - wheel_letters) : -1;
    for(int i = 0; i < 27; i++){
        lv_obj_t *cell = lv_obj_get_child(a->grid, (int32_t)(i + 1)); if(!cell) continue;
        lv_obj_t *l = lv_obj_get_child(cell, 0);
        const char *kp = l ? strchr(wheel_letters, toupper((unsigned char)lv_label_get_text(l)[0])) : NULL;
        int li = kp && *kp ? (int)(kp - wheel_letters) : 0;
        int on = have[li], now = li == cur;
        lv_obj_set_style_bg_color(cell, now ? TC(ACCENT_PRIMARY) : TC(SURFACE_RAISED), 0);
        lv_obj_set_style_bg_opa(cell, on ? LV_OPA_COVER : LV_OPA_30, 0);
        if(l) lv_obj_set_style_text_color(l, now ? TC(ON_ACCENT) : on ? TC(TEXT_PRIMARY) : theme_color_mix(TC(CANVAS), TC(TEXT_PRIMARY), 190), 0);
        if(on) lv_obj_add_flag(cell, LV_OBJ_FLAG_CLICKABLE); else lv_obj_remove_flag(cell, LV_OBJ_FLAG_CLICKABLE);
    }
}
static void zgrid_cb(lv_event_t *e){                        /* Zune: which tile was tapped (or none: close) */
    lv_obj_t *g = lv_event_get_current_target(e); azjump_t *a = lv_obj_get_user_data(g);
    lv_point_t p; lv_indev_get_point(lv_indev_active(), &p);
    for(uint32_t k = 0; k < lv_obj_get_child_count(g); k++){
        lv_obj_t *c = lv_obj_get_child(g, (int32_t)k); if(!lv_obj_get_user_data(c)) continue;
        lv_area_t ar; lv_obj_get_coords(c, &ar);
        if(p.x >= ar.x1 - 2 && p.x <= ar.x2 + 2 && p.y >= ar.y1 - 2 && p.y <= ar.y2 + 2){
            lv_obj_add_flag(g, LV_OBJ_FLAG_HIDDEN); if(a && a->jump) a->jump((char)(intptr_t)lv_obj_get_user_data(c)); return; }
    }
    lv_obj_add_flag(g, LV_OBJ_FLAG_HIDDEN);
}
void azjump_open(azjump_t *a){   /* the list's letter picker: the letter wheel or the A - Z keypad, per the setting */
    if(!a || !a->btn) return;
    if(wheel_mode(a)){ wheel_open(a); return; }
    keypad_prepare(a); lv_obj_clear_flag(a->grid, LV_OBJ_FLAG_HIDDEN); lv_obj_move_foreground(a->grid);
}
static void btn_cb(lv_event_t *e){
    if(lv_event_get_code(e) != LV_EVENT_SHORT_CLICKED) return;
    azjump_t *a = lv_event_get_user_data(e);
    if(a && wheel_mode(a)){wheel_open(a);return;}
    if(a){ keypad_prepare(a); lv_obj_clear_flag(a->grid, LV_OBJ_FLAG_HIDDEN); lv_obj_move_foreground(a->grid); }
}

void azjump_create(azjump_t *a, lv_obj_t *root, azjump_cb_t jump, const char *tag){
    a->library=!strcmp(tag,"library")||!strcmp(tag,"shelf");a->current=0;
    if(a->library)for(int i=0;i<4;i++)if(!pickers[i]||pickers[i]==a){pickers[i]=a;break;}
    a->jump = jump;
    a->btn = lv_button_create(root);
    lv_obj_remove_style_all(a->btn);
    lv_obj_set_pos(a->btn, 308, 158); lv_obj_set_size(a->btn, 44, 44);
    lv_obj_set_style_radius(a->btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(a->btn, th_null()?TC(FIXED_MEDIA_BLACK):TC(SURFACE_RAISED), 0);
    lv_obj_set_style_bg_opa(a->btn, th_null()?128:LV_OPA_90, 0);
    lv_obj_set_style_bg_color(a->btn, TC(SURFACE_SELECTED), LV_STATE_PRESSED);
    lv_obj_set_ext_click_area(a->btn, 10);
    lv_obj_add_flag(a->btn,LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_add_event_cb(a->btn, wheel_button, LV_EVENT_ALL, a);
    lv_obj_add_event_cb(a->btn, btn_cb, LV_EVENT_SHORT_CLICKED, a);
    lv_obj_t *azl = lv_label_create(a->btn); lv_label_set_text(azl, "A-Z");
    lv_obj_set_style_text_font(azl, TF(UI_14), 0);
    if(th_disco_family() || th_null()){ lv_obj_set_pos(a->btn, 138, 316); lv_obj_set_size(a->btn, 84, 36); lv_label_set_text(azl, "A - Z");
                    lv_obj_set_style_text_font(azl, TF(UI_16), 0); }
    lv_obj_set_style_text_color(azl, TC(TEXT_PRIMARY), 0); lv_obj_center(azl);
    lv_obj_add_flag(a->btn, LV_OBJ_FLAG_HIDDEN);

    /* the letter grid */
    a->grid = lv_obj_create(root);
    lv_obj_remove_style_all(a->grid);
    lv_obj_set_user_data(a->grid, a);
    lv_obj_set_size(a->grid, 360, 360); lv_obj_set_pos(a->grid, 0, 0);
    lv_obj_set_style_bg_color(a->grid, th_null()?TC(FIXED_MEDIA_BLACK):TC(CANVAS), 0);
    lv_obj_set_style_bg_opa(a->grid, th_null()?128:th_simple()?255:LV_OPA_80, 0);
    lv_obj_add_flag(a->grid, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(a->grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(a->grid, grid_bg_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(a->grid, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *gt = lv_label_create(a->grid);                  /* says it's navigation, not a sort control */
    lv_label_set_text(gt, tr("Jump to"));
    lv_obj_align(gt, LV_ALIGN_TOP_MID, 0, 20);
    lv_obj_set_style_text_font(gt, th_simple()?TF(UI_22):TF(UI_14), 0);
    lv_obj_set_style_text_color(gt, th_simple()?TC(ACCENT_PRIMARY):TC(TEXT_MUTED), 0);
    static const char *AZ = "ABCDEFGHIJKLMNOPQRSTUVWXYZ#";
    int cols = 5, cw = 52, ch = 48, n = 27, gw = cols * cw, x0 = (360 - gw) / 2, rows = (n + cols - 1) / cols, y0 = 48;
    for(int i = 0; i < n; i++){
        int r = i / cols, c = i % cols;
        int in_row = (r == rows - 1) ? (n - r * cols) : cols;        /* the last row may be partial: centred */
        int rx0 = x0 + ((cols - in_row) * cw) / 2;
        lv_obj_t *cell = lv_button_create(a->grid);
        lv_obj_remove_style_all(cell);
        lv_obj_set_pos(cell, rx0 + c * cw, y0 + r * ch); lv_obj_set_size(cell, cw - 4, ch - 4);
        lv_obj_set_style_radius(cell, 8, 0);
        lv_obj_set_style_bg_color(cell, TC(ACCENT_PRIMARY), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_set_style_text_color(cell, TC(TEXT_PRIMARY), 0);
        lv_obj_set_style_text_color(cell, TC(ON_ACCENT), LV_STATE_PRESSED);
        lv_obj_add_event_cb(cell, letter_cb, LV_EVENT_CLICKED, (void *)(intptr_t)AZ[i]);
        lv_obj_t *l = lv_label_create(cell);
        char b[2] = { AZ[i], 0 }; lv_label_set_text(l, b);
        lv_obj_set_style_text_font(l, th_simple()?TF(UI_24):TF(UI_20), 0);
        lv_obj_set_style_text_color(l, TC(TEXT_PRIMARY), 0); lv_obj_center(l);
        if(th_zune()){                                            /* Figma 31: square tiles in rows that fit the glass */
            static const char *ORD = "#ABCDEFGHIJKLMNOPQRSTUVWXYZ"; static const int ROWN[5] = { 3, 6, 7, 7, 4 };
            int k = (int)(strchr(ORD, AZ[i]) - ORD), row = 0, base = 0;
            while(row < 4 && k >= base + ROWN[row]){ base += ROWN[row]; row++; }
            int col = k - base, x = 180 - ROWN[row] * 40 / 2 + col * 40 + 2, y = 82 + row * 40;
            lv_obj_set_pos(cell, x, y); lv_obj_set_size(cell, 36, 36);
            lv_obj_set_style_radius(cell, 0, 0);
            lv_obj_set_style_bg_color(cell, TC(ACCENT_PRIMARY), 0); lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(cell, TC(TEXT_PRIMARY), LV_STATE_PRESSED);
            LV_FONT_DECLARE(font_zune_regular_19)
            char lo[2] = { (char)(AZ[i] >= 'A' && AZ[i] <= 'Z' ? AZ[i] + 32 : AZ[i]), 0 }; lv_label_set_text(l, lo);
            lv_obj_set_style_text_font(l, &font_zune_regular_19, 0); lv_obj_set_style_text_color(l, TC(ON_ACCENT), 0);
            kit_keep(cell); kit_keep(l);
            lv_obj_remove_flag(cell, LV_OBJ_FLAG_CLICKABLE);      /* the kit would round a clickable tile: the grid takes the tap */
            lv_obj_set_user_data(cell, (void *)(intptr_t)AZ[i]);
        }
    }
    if(rim_mode() && !th_zune()){   /* a round keypad - rows of 5, 6, 6, 6, 4 round keys that follow the circle */
        static const int ROWN[5] = { 5, 6, 6, 6, 4 };
        lv_obj_set_style_bg_color(a->grid, TC(CANVAS), 0); lv_obj_set_style_bg_opa(a->grid, 240, 0); kit_keep(a->grid);   /* the list recedes fully */
        lv_obj_align(gt, LV_ALIGN_TOP_MID, 0, 34);
        for(int i = 0; i < n; i++){
            int row = 0, base = 0; while(row < 4 && i >= base + ROWN[row]){ base += ROWN[row]; row++; }
            int col = i - base, cx = 180 + (col - (ROWN[row] - 1) / 2.0f) * 50, cy = 92 + row * 50;
            lv_obj_t *cell = lv_obj_get_child(a->grid, (int32_t)(i + 1));   /* child 0 is the "Jump to" label */
            lv_obj_set_size(cell, 42, 42); lv_obj_set_pos(cell, cx - 21, cy - 21);
            lv_obj_set_style_radius(cell, th_null() ? 4 : LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(cell, TC(SURFACE_RAISED), 0); lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
            if(th_null()){ lv_obj_set_style_border_width(cell, 1, 0); lv_obj_set_style_border_color(cell, TC(BORDER), 0); }
            lv_obj_t *l = lv_obj_get_child(cell, 0); if(l){ lv_obj_set_style_text_font(l, TF(UI_20), 0); lv_obj_center(l); }
            kit_keep(cell);
        }
    }
    if(th_zune()){ lv_obj_set_style_bg_opa(a->grid, LV_OPA_COVER, 0); lv_obj_add_flag(gt, LV_OBJ_FLAG_HIDDEN); kit_keep(a->grid);
                   lv_obj_remove_event_cb(a->grid, grid_bg_cb); lv_obj_add_event_cb(a->grid, zgrid_cb, LV_EVENT_CLICKED, NULL); }
}
void azjump_show(azjump_t *a, int on){
    if(!a->btn) return;
    if(on){
        lv_obj_clear_flag(a->btn,LV_OBJ_FLAG_HIDDEN);
        if(th_zune() && !wheel_mode(a)){                    /* Zune (Figma 29:67): a 34 px magenta dot on the right rim, the current letter in it */
            lv_obj_t*l=lv_obj_get_child(a->btn,0); kit_keep(a->btn); kit_keep(l);
            lv_obj_set_pos(a->btn,305,179);lv_obj_set_size(a->btn,34,34);lv_obj_set_style_radius(a->btn,LV_RADIUS_CIRCLE,0);
            lv_obj_set_style_bg_color(a->btn,TC(ACCENT_PRIMARY),0);lv_obj_set_style_bg_opa(a->btn,LV_OPA_COVER,0);
            lv_obj_set_style_text_font(l,TF(UI_16),0);lv_obj_set_style_text_color(l,TC(ON_ACCENT),0);
            if(!strcmp(lv_label_get_text(l),"A-Z")||!strcmp(lv_label_get_text(l),"A - Z"))lv_label_set_text(l,"a");
            lv_obj_set_style_pad_bottom(l,3,0);lv_obj_center(l);
        } else
        if(!wheel_mode(a)){
            lv_obj_t*l=lv_obj_get_child(a->btn,0);
            int pill=th_disco_family()||th_null();lv_obj_set_pos(a->btn,pill?138:308,pill?316:158);lv_obj_set_size(a->btn,pill?84:44,pill?36:44);
            lv_obj_set_style_radius(a->btn,LV_RADIUS_CIRCLE,0);lv_obj_set_style_bg_color(a->btn,th_null()?TC(FIXED_MEDIA_BLACK):TC(SURFACE_RAISED),0);lv_obj_set_style_bg_opa(a->btn,th_null()?128:LV_OPA_90,0);
            lv_label_set_text(l,pill?"A - Z":"A-Z");lv_obj_set_style_text_font(l,pill?TF(UI_16):TF(UI_14),0);lv_obj_set_style_text_color(l,TC(TEXT_PRIMARY),0);lv_obj_center(l);
        }
    }
    else { wheel_close(a,0);lv_obj_add_flag(a->btn, LV_OBJ_FLAG_HIDDEN); grid_close(a); }
}

void azjump_destroy(azjump_t*a){
 if(!a)return;wheel_close(a,0);for(int i=0;i<4;i++)if(pickers[i]==a)pickers[i]=NULL;
 if(a->btn)lv_obj_delete(a->btn);if(a->grid)lv_obj_delete(a->grid);memset(a,0,sizeof *a);
}
/* Zune: the dot shows where the list is (its first visible row's letter, lower case) */
void azjump_letter(azjump_t *a, char letter){
    if(!th_zune() || !a || !a->btn || wheel_mode(a) || lv_obj_has_flag(a->btn, LV_OBJ_FLAG_HIDDEN)) return;
    char t[2] = { (char)((letter >= 'A' && letter <= 'Z') ? letter + 32 : letter ? letter : '#'), 0 };
    lv_obj_t *l = lv_obj_get_child(a->btn, 0);
    if(l && strcmp(lv_label_get_text(l), t)){ lv_label_set_text(l, t); lv_obj_center(l); }
}
void azjump_test_grid(void){ if(pickers[0] && pickers[0]->grid){ keypad_prepare(pickers[0]); lv_obj_clear_flag(pickers[0]->grid, LV_OBJ_FLAG_HIDDEN); lv_obj_move_foreground(pickers[0]->grid); } }   /* host renders */
