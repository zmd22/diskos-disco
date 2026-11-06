/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "setup.h"
#include "app_theme.h"
#include "landing.h"
#include "connection.h"
#include "screens.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#define CONFIG_DIR "/usr/data/apps/disc-shelf"
#define CONFIG_PATH CONFIG_DIR "/shelf.conf"
static struct {lv_obj_t *root,*list,*source[2];ds_settings_t cfg;int editing,edit_source;void(*saved)(void);} *form;
static const char*title[]={"Server","Username","Password","Download folder","Share","Domain (optional)"};
static char*value(ds_profile_t*p,int i){switch(i){case 0:return p->server;case 1:return p->user;case 2:return p->password;case 3:return p->dest;case 4:return p->share;default:return p->domain;}}
static void build_fields(void);
int ds_setup_close(void){if(!form)return 0;lv_obj_delete(form->root);ds_secret_clear(form,sizeof *form);free(form);form=NULL;return 1;}
static void dismiss(lv_event_t*e){(void)e;ds_setup_close();}
static void edited(const char*s){if(!form||!s)return;int i=form->editing;size_t max=i==0?1023:i==3?900:i==5?127:255;if(strlen(s)>max||strpbrk(s,"\r\n")){ui_toast("Value too long or contains a new line");return;}char*dst=value(&form->cfg.profile[form->edit_source],i);
 /* Navidrome URL typed as "host:port": add the http:// the validator requires (it said only "Set server, ...") */
 if(i==0&&form->edit_source==DS_NAVIDROME&&s[0]&&!strstr(s,"://")&&strlen(s)+7<=max)snprintf(dst,1024,"http://%s",s);
 else strcpy(dst,s);build_fields();}
static void edit(lv_event_t*e){if(!form)return;int i=(int)(intptr_t)lv_event_get_user_data(e);form->editing=i;form->edit_source=form->cfg.source;char*v=value(&form->cfg.profile[form->edit_source],i);const char*t=i==0?(form->cfg.source==DS_SMB?"SMB host or IP":"Navidrome URL"):title[i];if(i==2)kbinput_open_password(t,v,edited);else kbinput_open(t,v,edited);}
static void remember(lv_event_t*e){(void)e;ds_profile_t*p=&form->cfg.profile[form->cfg.source];p->remember=!p->remember;build_fields();}
static void choose_source(lv_event_t*e){if(!form)return;form->cfg.source=(int)(intptr_t)lv_event_get_user_data(e);build_fields();}
static lv_obj_t*field_row(const char*t,lv_event_cb_t cb,int ud){
 lv_obj_t*b=lv_button_create(form->list);lv_obj_set_size(b,266,38);lv_obj_remove_flag(b,LV_OBJ_FLAG_SCROLLABLE);
 lv_obj_t*l=lv_label_create(b);lv_label_set_text(l,t);app_theme_button(b,0);lv_obj_set_style_text_font(l,TF(UI_16),0);lv_obj_set_width(l,238);lv_obj_set_height(l,lv_font_get_line_height(TF(UI_16)));lv_label_set_long_mode(l,LV_LABEL_LONG_DOT);lv_obj_center(l);
 if(cb)lv_obj_add_event_cb(b,cb,LV_EVENT_SHORT_CLICKED,(void*)(intptr_t)ud);return b;
}
static void build_fields(void){
 if(!form)return;lv_obj_clean(form->list);int source=form->cfg.source;
 for(int i=0;i<2;i++){lv_obj_set_style_border_width(form->source[i],i==source?3:1,0);lv_obj_set_style_bg_color(form->source[i],i==source?TC(SURFACE_RAISED):TC(SURFACE),0);}
 const int nav[]={0,1,2,3},smb[]={0,4,1,2,5,3};const int*order=source==DS_SMB?smb:nav;int n=source==DS_SMB?6:4;ds_profile_t*p=&form->cfg.profile[source];
 for(int j=0;j<n;j++){int i=order[j];char b[1140];snprintf(b,sizeof b,"%s: %s",title[i],i==2?(p->password[0]?"********":"Not set"):value(p,i)[0]?value(p,i):"Not set");field_row(b,edit,i);}
 char b[80];snprintf(b,sizeof b,"Remember password: %s",p->remember?"On":"Off");field_row(b,remember,0);
 lv_obj_t*l=lv_label_create(form->list);lv_obj_set_width(l,254);lv_label_set_text(l,p->remember?"Saved passwords are readable\nwith root or firmware access.":"Password stays in memory\nuntil Disc Shelf closes.");lv_obj_set_style_text_font(l,TF(UI_14),0);lv_obj_set_style_text_color(l,TC(TEXT_SECONDARY),0);lv_obj_set_style_text_align(l,LV_TEXT_ALIGN_CENTER,0);lv_label_set_long_mode(l,LV_LABEL_LONG_WRAP);kit_keep(l);
 lv_obj_scroll_to_y(form->list,0,LV_ANIM_OFF);
}
static void save(lv_event_t*e){
 (void)e;if(!form)return;char msg[200];
 if(!ds_settings_validate(&form->cfg,0,msg,sizeof msg)){ui_toast(msg);return;}
 if((mkdir("/usr/data/apps",0755)&&errno!=EEXIST)||(mkdir(CONFIG_DIR,0700)&&errno!=EEXIST)){ui_toast("Cannot create configuration folder");return;}
 if(!ds_settings_save(CONFIG_PATH,&form->cfg,msg,sizeof msg)){ui_toast(msg);return;}
 void(*done)(void)=form->saved;ds_setup_close();ui_toast(msg);if(done)done();
}
void ds_setup_open(void(*saved)(void)){
 if(form)return;form=calloc(1,sizeof *form);if(!form){ui_toast("Not enough memory");return;}form->saved=saved;ds_settings_load(CONFIG_PATH,&form->cfg);
 form->root=lv_obj_create(screen_get_root(SCR_SHELF));lv_obj_remove_style_all(form->root);lv_obj_set_size(form->root,360,360);lv_obj_set_style_bg_color(form->root,TC(CANVAS),0);lv_obj_set_style_bg_opa(form->root,LV_OPA_COVER,0);lv_obj_remove_flag(form->root,LV_OBJ_FLAG_SCROLLABLE);kit_keep(form->root);
 /* Zune's pivot header puts its back ring (and a wide hit area) at y 38..94: move the source row below it, or a tap
  * on Navidrome lands on Back and closes the form without saving. */
 int dz=th_zune()?26:0;
 ui_header_cb(form->root,"Server",dismiss);shelf_brand_icon(form->root,170,6,20);
 for(int i=0;i<2;i++){lv_obj_t*b=form->source[i]=lv_button_create(form->root);lv_obj_set_pos(b,54+i*128,70+dz);lv_obj_set_size(b,124,34);lv_obj_t*l=lv_label_create(b);lv_label_set_text(l,i?"SMB3":"Navidrome");app_theme_button(b,0);lv_obj_set_style_text_font(l,TF(UI_16),0);lv_obj_center(l);lv_obj_add_event_cb(b,choose_source,LV_EVENT_SHORT_CLICKED,(void*)(intptr_t)i);}
 form->list=lv_obj_create(form->root);lv_obj_remove_style_all(form->list);lv_obj_set_pos(form->list,44,112+dz);lv_obj_set_size(form->list,272,168-dz);lv_obj_set_flex_flow(form->list,LV_FLEX_FLOW_COLUMN);lv_obj_set_flex_align(form->list,LV_FLEX_ALIGN_START,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_CENTER);lv_obj_set_style_pad_row(form->list,5,0);lv_obj_set_style_pad_bottom(form->list,8,0);lv_obj_set_scroll_dir(form->list,LV_DIR_VER);lv_obj_set_scrollbar_mode(form->list,LV_SCROLLBAR_MODE_AUTO);kit_keep(form->list);
 lv_obj_t*b=lv_button_create(form->root);lv_obj_set_pos(b,114,284);lv_obj_set_size(b,132,44);lv_obj_t*l=lv_label_create(b);lv_label_set_text(l,"Save");lv_obj_center(l);app_theme_button(b,0);lv_obj_add_event_cb(b,save,LV_EVENT_SHORT_CLICKED,NULL);build_fields();
}
void ds_setup_preview_source(int source){if(form){form->cfg.source=source;build_fields();}}
