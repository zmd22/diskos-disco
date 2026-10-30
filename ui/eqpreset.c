#include "simple_theme.h"
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "screens.h"
#include "braun.h"
#include "theme.h"
#include "theme_kit.h"
#include "config.h"
#include "musicdb.h"
#include "fwcaps.h"
#include "eqrate.h"
#include "eqprofile.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
static lv_obj_t *name_label,*edit_button,*rename_button,*switch_button,*switch_label,*note_label;
static int rename_preset;
static lv_obj_t *switch_pointer;
static const char *const names[]={"Off","Jazz","Rock","R&B","Hip-Hop","Pop","Dance","Classical","Retro","Sibilance 1","Sibilance 2"};
static int selected(void){int p=simple_cfg_get_int("eq_preset",0);return p<0?0:p>20?20:p;}
static void name_for(int p,char *b,size_t n){
 if(p<=10){snprintf(b,n,"%s",names[p]);return;}
 char key[24];snprintf(key,sizeof key,"eq_name%d",p);const char *s=simple_cfg_get_str(key,NULL);
 if(s && *s)snprintf(b,n,"%s",s);else snprintf(b,n,"USER%d",p-10);
}
static unsigned profile_ticket;
static lv_timer_t *profile_timer;
static void profile_paint(int p,int read,int graphic,const int hz[10]){
 int user=p>=11 && read>=0 && graphic;
 if(user){lv_obj_remove_state(edit_button,LV_STATE_DISABLED);lv_obj_remove_state(rename_button,LV_STATE_DISABLED);}
 else {lv_obj_add_state(edit_button,LV_STATE_DISABLED);lv_obj_add_state(rename_button,LV_STATE_DISABLED);}
 if(th_simple()){   /* Squash paints button text once, from the state at creation: repaint it when the state changes */
  lv_obj_t *b2[2]={edit_button,rename_button};
  for(int k=0;k<2;k++){lv_obj_t *l=lv_obj_get_child(b2[k],0);if(l)lv_obj_set_style_text_color(l,user?TC(TEXT_PRIMARY):TC(TEXT_DISABLED),0);}
 }
 lv_label_set_text(note_label,read==-2?"Loading profile...":read<0?"Profile unavailable":p<11?"Built-in preset":!graphic?"Parametric profile":!fw_custom_peq_curve_writable(hz,10)?"Custom editing unavailable":"Editable profile");
}
static void profile_poll(lv_timer_t *timer){
 (void)timer;if(screen_current()!=SCR_EQ || !profile_ticket)return;
 eq_profile_t result;
 if(eq_profile_take(profile_ticket,&result)){
  profile_ticket=0;
  if(result.preset==selected())profile_paint(result.preset,result.read,result.graphic,result.hz);
 }
}

/* ==== Zune (Figma 36): the presets on a dial, the one in use in the accent; "equalizer on" with a switch at the foot;
 * edit / rename under it while a user profile is on the focus line ==== */
#include "zune_dial.h"
static void toggle(lv_event_t *e); static void renamed(const char *text);
LV_FONT_DECLARE(font_zune_semibold_11) LV_FONT_DECLARE(font_zune_regular_14) LV_FONT_DECLARE(font_zune_regular_19) LV_FONT_DECLARE(font_zune_regular_13)
static lv_obj_t *g_zroot, *g_zsw, *g_zlinks; static lv_timer_t *g_zfoc_tm; static int g_zlast_focus = -1;
static char g_znames[20][40];
static void zlow(char *o, size_t n, const char *s){ size_t i = 0; for(; s[i] && i + 1 < n; i++) o[i] = (s[i] >= 'A' && s[i] <= 'Z') ? s[i] + 32 : s[i]; o[i] = 0; }
static void zsw_paint(void){
    if(!g_zsw) return;
    int on = selected() != 0;
    lv_obj_set_style_bg_color(g_zsw, on ? TC(ACCENT_PRIMARY) : TC(CONTROL_TRACK), 0);
    lv_obj_t *k = lv_obj_get_child(g_zsw, 0); if(k) lv_obj_align(k, on ? LV_ALIGN_RIGHT_MID : LV_ALIGN_LEFT_MID, on ? -3 : 3, 0);
}
static void zfoc_tick(lv_timer_t *t){
    (void)t; if(screen_current() != SCR_EQ || !g_zlinks) return;
    int f = zdial_focus(); if(f == g_zlast_focus) return; g_zlast_focus = f;
    if(f >= 10) lv_obj_remove_flag(g_zlinks, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(g_zlinks, LV_OBJ_FLAG_HIDDEN);
}
static void zeq_build(void);
static void zeq_rebuild(void *u){ (void)u; zeq_build(); }
static void zeq_pick(int i){
    int p = i + 1;
    if(ui_eq_select(p) < 0){ ui_toast("Couldn't switch EQ"); return; }
    lv_async_call(zeq_rebuild, NULL);
}
static void zeq_toggle(lv_event_t *e){ toggle(e); lv_async_call(zeq_rebuild, NULL); }
static void zeq_edit(lv_event_t *e){ (void)e; int f = zdial_focus() + 1; if(f != selected() && ui_eq_select(f) < 0) return; screen_show(SCR_EQ_EDITOR); }
static void zeq_rename(lv_event_t *e){ (void)e; rename_preset = zdial_focus() + 1; char b[80]; name_for(rename_preset, b, sizeof b); kbinput_open("Rename EQ", b, renamed); }
static lv_obj_t *zlink(lv_obj_t *p, const char *t, lv_color_t c, lv_event_cb_t cb){
    lv_obj_t *l = lv_label_create(p); kit_keep(l); lv_label_set_text(l, t);
    lv_obj_set_style_text_font(l, zune_font(&font_zune_regular_19), 0); lv_obj_set_style_text_color(l, c, 0);
    lv_obj_add_flag(l, LV_OBJ_FLAG_CLICKABLE); lv_obj_set_ext_click_area(l, 12);
    lv_obj_add_event_cb(l, cb, LV_EVENT_CLICKED, NULL); return l;
}
static void zeq_build(void){
    lv_obj_t *root = g_zroot; if(!root) return;
    lv_obj_clean(root); g_zsw = g_zlinks = NULL; g_zlast_focus = -1;
    lv_obj_set_style_bg_color(root, TC(CANVAS), 0); lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    const char *lb[20];
    for(int i = 0; i < 20; i++){ char b[40]; name_for(i + 1, b, sizeof b); zlow(g_znames[i], sizeof g_znames[i], b); lb[i] = g_znames[i]; }
    int cur = selected(), focus = cur ? cur - 1 : simple_cfg_get_int("eq_last", 1) - 1;
    if(focus < 0 || focus >= 20) focus = 0;
    zdial_build(root, lb, 20, focus, zeq_pick, 44, "eq.zune");
    zdial_set_bottom(250);
    if(cur) zdial_set_color(cur - 1, TC(ACCENT_PRIMARY));
    for(int k = 0; k < 2; k++){                                   /* fades so the dial melts into the caption and the foot */
        lv_obj_t *f = lv_obj_create(root); lv_obj_remove_style_all(f); kit_keep(f);
        lv_obj_set_size(f, 360, k ? 96 : 62); lv_obj_align(f, k ? LV_ALIGN_BOTTOM_MID : LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_set_style_bg_color(f, TC(CANVAS), 0); lv_obj_set_style_bg_grad_color(f, TC(CANVAS), 0);
        lv_obj_set_style_bg_grad_dir(f, LV_GRAD_DIR_VER, 0); lv_obj_set_style_bg_opa(f, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_main_opa(f, k ? LV_OPA_TRANSP : LV_OPA_COVER, 0); lv_obj_set_style_bg_grad_opa(f, k ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_main_stop(f, k ? 0 : 140, 0); lv_obj_set_style_bg_grad_stop(f, k ? 120 : 255, 0);
        lv_obj_remove_flag(f, LV_OBJ_FLAG_CLICKABLE);
    }
    lv_obj_t *cap = lv_label_create(root); kit_keep(cap); lv_label_set_text(cap, "EQUALIZER");
    lv_obj_set_style_text_font(cap, &font_zune_semibold_11, 0); lv_obj_set_style_text_letter_space(cap, 2, 0);
    lv_obj_set_style_text_color(cap, TC(TEXT_MUTED), 0); lv_obj_align(cap, LV_ALIGN_TOP_MID, 0, 26);
    /* the foot: "equalizer on" + switch, and edit / rename for user profiles */
    lv_obj_t *row = lv_obj_create(root); lv_obj_remove_style_all(row); kit_keep(row);
    lv_obj_set_size(row, 200, 42); lv_obj_align(row, LV_ALIGN_TOP_MID, 0, 284);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE); lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(row, zeq_toggle, LV_EVENT_CLICKED, NULL);
    lv_obj_t *t = lv_label_create(row); kit_keep(t); lv_label_set_text(t, "equalizer on");
    lv_obj_set_style_text_font(t, zune_font(&font_zune_regular_19), 0); lv_obj_set_style_text_color(t, TC(TEXT_PRIMARY), 0);
    lv_obj_align(t, LV_ALIGN_LEFT_MID, 8, 0);
    g_zsw = lv_obj_create(row); lv_obj_remove_style_all(g_zsw); kit_keep(g_zsw);
    lv_obj_set_size(g_zsw, 36, 20); lv_obj_set_style_radius(g_zsw, LV_RADIUS_CIRCLE, 0); lv_obj_set_style_bg_opa(g_zsw, LV_OPA_COVER, 0);
    lv_obj_align(g_zsw, LV_ALIGN_RIGHT_MID, -8, 0); lv_obj_remove_flag(g_zsw, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *k = lv_obj_create(g_zsw); lv_obj_remove_style_all(k); kit_keep(k);
    lv_obj_set_size(k, 14, 14); lv_obj_set_style_radius(k, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(k, TC(FIXED_MEDIA_WHITE), 0); lv_obj_set_style_bg_opa(k, LV_OPA_COVER, 0);
    zsw_paint();
    g_zlinks = lv_obj_create(root); lv_obj_remove_style_all(g_zlinks); kit_keep(g_zlinks);
    lv_obj_set_size(g_zlinks, 180, 32); lv_obj_align(g_zlinks, LV_ALIGN_TOP_MID, 0, 250);
    lv_obj_remove_flag(g_zlinks, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *ed = zlink(g_zlinks, "edit", TC(ACCENT_PRIMARY), zeq_edit); lv_obj_align(ed, LV_ALIGN_LEFT_MID, 26, 0);
    lv_obj_t *rn = zlink(g_zlinks, "rename", TC(TEXT_PRIMARY), zeq_rename); lv_obj_align(rn, LV_ALIGN_RIGHT_MID, -16, 0);
    if(!g_zfoc_tm) g_zfoc_tm = lv_timer_create(zfoc_tick, 150, NULL);
    zfoc_tick(NULL);
}
void eqpreset_refresh(void){
 if(th_zune()){ if(fw_os_ver()==257)eq_rate_request(); zeq_build(); return; }
 if(!name_label)return;if(fw_os_ver()==257)eq_rate_request();int p=selected();char b[80];name_for(p,b,sizeof b);if(th_null()){char up[80];theme_upper(up,sizeof up,b);lv_label_set_text(name_label,up);}else lv_label_set_text(name_label,b);
 if(library_lift()&&!th_null()){   /* a name wider than its space steps down a size before it has to scroll */
  const lv_font_t *steps[3]={TF(UI_24),TF(UI_20),TF(UI_18)},*f=steps[0];int room=lv_obj_get_style_width(name_label,0);
  for(int k=0;k<3;k++){lv_point_t sz;f=steps[k];lv_text_get_size(&sz,lv_label_get_text(name_label),f,0,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);if(sz.x<=room)break;}
  lv_obj_set_style_text_font(name_label,f,0);
 }
 profile_ticket=0;
 if(p>=11){profile_ticket=eq_profile_request(p);profile_paint(p,profile_ticket?-2:-1,0,NULL);}
 else profile_paint(p,0,0,NULL);
 lv_label_set_text(switch_label,p?"On":"Off");
 if(switch_pointer&&th_braun_family())br_pointer_set(switch_pointer,58,p);
 if(switch_pointer)lv_obj_set_style_bg_color(switch_pointer,p?theme_on_color(ui_current_accent()):TC(TEXT_MUTED),0);
 lv_obj_set_style_bg_color(switch_button,p?ui_current_accent():TC(SURFACE),0);
 lv_obj_set_style_text_color(switch_label,p?theme_on_color(ui_current_accent()):TC(TEXT_PRIMARY),0);
 if(th_simple()){if(p)lv_obj_add_state(switch_button,LV_STATE_CHECKED);else lv_obj_remove_state(switch_button,LV_STATE_CHECKED);lv_obj_set_style_bg_grad_dir(switch_button,LV_GRAD_DIR_NONE,0);lv_obj_set_style_bg_color(switch_button,p?TC(ACCENT_PRIMARY):theme_color_mix(TC(ACCENT_PRIMARY),TC(CANVAS),24),0);lv_obj_set_style_text_color(switch_label,p?theme_on_color(TC(ACCENT_PRIMARY)):TC(TEXT_PRIMARY),0);lv_obj_add_flag(switch_pointer,LV_OBJ_FLAG_HIDDEN);}
 if(th_null()){
  lv_obj_set_style_bg_opa(switch_button,0,0);lv_obj_set_style_border_width(switch_button,1,0);
  lv_obj_set_style_border_color(switch_button,p?TC(TEXT_PRIMARY):TC(TEXT_SECONDARY),0);
  lv_obj_set_style_text_color(switch_label,p?TC(TEXT_PRIMARY):TC(TEXT_SECONDARY),0);
 }
}
static void cycle(lv_event_t *e){int dir=(int)(intptr_t)lv_event_get_user_data(e);int p=(selected()+dir+21)%21;
 if(ui_eq_select(p)<0){ui_toast("Couldn't switch EQ");return;}eqpreset_refresh();}
static void toggle(lv_event_t *e){(void)e;int p=0;if(!selected()){p=simple_cfg_get_int("eq_last",1);if(p<1||p>20)p=1;}
 if(ui_eq_select(p)<0){ui_toast("Couldn't switch EQ");return;}eqpreset_refresh();}
static void edit(lv_event_t *e){(void)e;screen_show(SCR_EQ_EDITOR);}
static void renamed(const char *text){if(!text||!*text)return;char key[24];snprintf(key,sizeof key,"eq_name%d",rename_preset);
 if(simple_cfg_set_str(key,text)){ui_toast("Couldn't save profile name");return;}eqpreset_refresh();}
static void rename_cb(lv_event_t *e){(void)e;rename_preset=selected();char b[80];name_for(rename_preset,b,sizeof b);kbinput_open("Rename EQ",b,renamed);}
static lv_obj_t *button(lv_obj_t *root,const char *text,int x,int y,int w,int h,lv_event_cb_t cb,void *data,const char *action){
 lv_obj_t *b=lv_button_create(root);lv_obj_set_size(b,w,h);lv_obj_align(b,LV_ALIGN_TOP_MID,x,y);
 lv_obj_set_style_bg_color(b,TC(SURFACE),0);lv_obj_set_style_radius(b,w==h?LV_RADIUS_CIRCLE:14,0);lv_obj_set_style_border_width(b,0,0);
 lv_obj_t *l=lv_label_create(b);lv_label_set_text(l,text);lv_obj_set_style_text_font(l,TF(UI_18),0);lv_obj_set_style_text_color(l,TC(TEXT_PRIMARY),0);lv_obj_center(l);
 ui_on(b,cb,LV_EVENT_CLICKED,data,action,UI_CORE);return b;
}
void eqpreset_create(lv_obj_t *root){
 if(th_zune()){ g_zroot=root; return; }
 lv_obj_set_style_bg_color(root,TC(CANVAS),0);ui_header(root,"Equalizer");
 lv_obj_t *prev_b=button(root,LV_SYMBOL_LEFT,-118,123,42,42,cycle,(void *)(intptr_t)-1,"eq.previous");
 lv_obj_t *next_b=button(root,LV_SYMBOL_RIGHT,118,123,42,42,cycle,(void *)(intptr_t)1,"eq.next");
 name_label=lv_label_create(root);lv_obj_set_width(name_label,178);lv_label_set_long_mode(name_label,LV_LABEL_LONG_SCROLL_CIRCULAR);lv_obj_align(name_label,LV_ALIGN_TOP_MID,0,128);
 lv_obj_set_style_text_align(name_label,LV_TEXT_ALIGN_CENTER,0);lv_obj_set_style_text_font(name_label,TF(UI_24),0);lv_obj_set_style_text_color(name_label,TC(TEXT_PRIMARY),0);
 edit_button=button(root,"Edit",-60,183,104,44,edit,NULL,"eq.edit");rename_button=button(root,"Rename",60,183,104,44,rename_cb,NULL,"eq.rename");
 note_label=lv_label_create(root);lv_obj_set_width(note_label,280);lv_obj_align(note_label,LV_ALIGN_TOP_MID,0,244);lv_obj_set_style_text_align(note_label,LV_TEXT_ALIGN_CENTER,0);lv_obj_set_style_text_font(note_label,library_lift()?TF(UI_14):TF(UI_12),0);lv_obj_set_style_text_color(note_label,TC(TEXT_SECONDARY),0);
 switch_button=button(root,"Off",0,274,58,58,toggle,NULL,"eq.toggle");switch_label=lv_obj_get_child(switch_button,0);
 switch_pointer=lv_obj_create(switch_button);lv_obj_remove_style_all(switch_pointer);
 lv_obj_set_size(switch_pointer,3,9);lv_obj_align(switch_pointer,LV_ALIGN_TOP_MID,10,4);
 lv_obj_set_style_bg_opa(switch_pointer,LV_OPA_COVER,0);lv_obj_set_style_radius(switch_pointer,1,0);
 lv_obj_set_style_transform_rotation(switch_pointer,300,0);lv_obj_clear_flag(switch_pointer,LV_OBJ_FLAG_CLICKABLE);
 if(th_disco()){   /* fork: Disco keeps the navigation circle's sliver on the right rim - the controls step left of it */
  for(uint32_t k=0;k<lv_obj_get_child_count(root);k++){
   lv_obj_t *o=lv_obj_get_child(root,(int32_t)k);
   if(o==name_label||o==edit_button||o==rename_button||o==note_label||o==switch_button) lv_obj_set_style_translate_x(o,disco_mirror()?20:-20,0);   /* Left: step right of it */
  }
  lv_obj_set_width(name_label,150);
  lv_obj_set_style_translate_x(next_b,disco_mirror()?4:-36,0); lv_obj_set_style_translate_x(prev_b,disco_mirror()?36:-4,0);
  lv_obj_add_flag(switch_pointer,LV_OBJ_FLAG_HIDDEN);
 }
 if(th_null()){
  lv_label_set_long_mode(name_label,LV_LABEL_LONG_SCROLL_CIRCULAR);lv_obj_set_width(name_label,192);
  lv_obj_align(name_label,LV_ALIGN_TOP_MID,0,125);lv_obj_set_style_text_font(name_label,TF(HEADER_SM),0);
  lv_obj_t *buttons[]={prev_b,next_b,edit_button,rename_button,switch_button};
  for(int i=0;i<5;i++){
   lv_obj_t *b=buttons[i];lv_obj_set_style_radius(b,0,0);lv_obj_set_style_bg_opa(b,0,0);
   lv_obj_set_style_text_color(b,TC(TEXT_PRIMARY),0);kit_keep(b);
   lv_obj_t *l=lv_obj_get_child(b,0);if(l){lv_obj_set_style_text_color(l,TC(TEXT_SECONDARY),0);kit_keep(l);}
  }
  lv_obj_set_style_text_font(lv_obj_get_child(edit_button,0),TF(HEADER_SM),0);
  lv_obj_set_style_text_font(lv_obj_get_child(rename_button,0),TF(HEADER_SM),0);
  lv_obj_set_style_text_font(switch_label,TF(HEADER_SM),0);
  lv_obj_set_size(switch_button,104,48);lv_obj_align(switch_button,LV_ALIGN_TOP_MID,0,280);
  lv_obj_add_flag(switch_pointer,LV_OBJ_FLAG_HIDDEN);kit_keep(name_label);
 }
 if(!profile_timer)profile_timer=lv_timer_create(profile_poll,100,NULL);
 /* Screen entry schedules a read; this page never waits on SQLite. */
}
