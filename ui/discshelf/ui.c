/* SPDX-License-Identifier: GPL-3.0-or-later */
#define _GNU_SOURCE
#include "app_theme.h"
#include "theme_kit.h"
#include "azjump.h"
#include "discshelf.h"
#include "multipick.h"
#include "curvelist.h"
#include "sdio.h"
#include "onboarding.h"
#include "null_theme.h"
#include "simple_theme.h"
#include "fonts_intl.h"
#include "screens.h"
#include "shelf.h"
#include "landing.h"
#include "setup.h"
#include "ipc.h"
#include <math.h>
#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <unistd.h>
static lv_obj_t *landing,*row_pool[PAGE],*list_extent,*progress_bar,*progress_amount,*progress_percent;
static int total_rows,window_pending,window_delivery;
static unsigned next_generation;
static lv_obj_t *root, *content, *heading, *note, *download,
    *az, *backbtn, *brand_icon, *clearbtn, *letters, *progress_label;
static azjump_t alphabet;
static unsigned modal_hidden;
static int modal_saved;
static item_t *rows;
#define ROW_STEP (th_null()?64:56)
static lv_obj_t *host_root, *choice;
static curvelist_t list_curve;
static int closing, preview_mode, hint_requested;
static uint32_t last_tick;
/* Four one-second samples, computed only while the download screen is visible. */
static struct {
 unsigned serial; uint32_t tick, elapsed[4]; int64_t bytes, delta[4];
 int active, count, next; double rate;
} download_speed;
static double progress_speed(unsigned serial,int64_t bytes,uint32_t now){
 if(!download_speed.active || serial!=download_speed.serial || bytes<download_speed.bytes){
  memset(&download_speed,0,sizeof download_speed);
  download_speed.active=1;download_speed.serial=serial;
  download_speed.tick=now;download_speed.bytes=bytes;return -1;
 }
 uint32_t elapsed=now-download_speed.tick;
 if(elapsed>=1000){
  int i=download_speed.next;download_speed.delta[i]=bytes-download_speed.bytes;
  download_speed.elapsed[i]=elapsed;download_speed.next=(i+1)%4;
  if(download_speed.count<4)download_speed.count++;
  int64_t sum=0;uint64_t ms=0;
  for(i=0;i<download_speed.count;i++){sum+=download_speed.delta[i];ms+=download_speed.elapsed[i];}
  download_speed.rate=ms?(double)sum*1000/ms:0;
  download_speed.tick=now;download_speed.bytes=bytes;
 }
 return download_speed.count?download_speed.rate:-1;
}

static int title_width, restart_after_close;
static void settings_cb(lv_event_t *e);
static mp_gesture_t swipe;
static void update_title(void);
static void positions(void);
static void close_choice(void);
/* UI owns picks while idle; worker owns them until completion. No second copy.
 */
#define selected (ds_shared.picks)
static int nrows, nselected, view = HOME, offset, revision = -1, busy = 1,
                             selecting, quit, fatal, reporting;
static char report_message[256],browse_message[256];
static request_t stack[32], current;
static char captions[32][STR];
static int level, rendering;
static int copying;
static int set_text(lv_obj_t *obj,const char *text);
LV_FONT_DECLARE(diskos_splash_disco_72)
static lv_obj_t *music_button, *music_overlay, *music_track, *music_artist, *music_progress, *music_pp;
static void music_close(void){
 if(music_overlay)lv_obj_delete(music_overlay);
 music_overlay=music_track=music_artist=music_progress=music_pp=NULL;
}
static void music_refresh(void){
 if(!music_overlay)return;
 track_state_t state;ipc_get_state(&state);
 set_text(music_track,state.have_track?state.title:"Not Playing");
 set_text(music_artist,state.have_track?state.artist:"");
 int pct=state.have_track && state.duration_ms>0?(int)((int64_t)state.position_ms*1000/state.duration_ms):0;
 if(pct<0)pct=0;if(pct>1000)pct=1000;
 if(lv_bar_get_value(music_progress)!=pct)lv_bar_set_value(music_progress,pct,LV_ANIM_OFF);
 ui_pp_glyph(music_pp,ui_pp_icon_playing(ui_is_playing()));
}
static void music_control(lv_event_t *e){
 int action=(int)(intptr_t)lv_event_get_user_data(e);
 if(action==3){music_close();return;}
 const char *commands[]={"0201000C0002","0201000C0000","0201000C0001"};
 ui_transport_command(commands[action]);music_refresh();
}
static void music_open(lv_event_t *e);
static void render(void);
static void refresh_selection(void);
static int az_open(void) {return alphabet.wheel || (letters && !lv_obj_has_flag(letters,LV_OBJ_FLAG_HIDDEN));}
static void modal_chrome(int hide) {
  lv_obj_t *items[] = {content, heading, backbtn, clearbtn, note, download, az};
  if (hide && modal_saved) return;
  if (!hide && !modal_saved) return;
  if (hide) modal_hidden = 0;
  for (unsigned i = 0; i < sizeof items / sizeof *items; i++) {
    if (!items[i]) continue;
    if (hide) {
      if (lv_obj_has_flag(items[i], LV_OBJ_FLAG_HIDDEN)) modal_hidden |= 1u << i;
      lv_obj_add_flag(items[i], LV_OBJ_FLAG_HIDDEN);
    } else if (!(modal_hidden & (1u << i)))
      lv_obj_remove_flag(items[i], LV_OBJ_FLAG_HIDDEN);
  }
  modal_saved = hide;
}
static void az_close(void) {
  azjump_show(&alphabet,0);
  modal_chrome(0);

}
static void az_visibility(lv_event_t *e) {
  (void)e;
  if (az_open()) {
    modal_chrome(1);

  } else az_close();
}
static int set_text(lv_obj_t *obj, const char *text) {
  if (!strcmp(lv_label_get_text(obj), text))
    return 0;
  lv_label_set_text(obj, text);
  return 1;
}
static lv_obj_t *label(lv_obj_t *parent, const char *text, int x, int y, int w,
                       const lv_font_t *font) {
  lv_obj_t *l = lv_label_create(parent);
  set_text(l, text);
  lv_obj_set_pos(l, x, y);
  lv_obj_set_width(l, w);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, TC(TEXT_PRIMARY), 0);
  lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
  return l;
}
static lv_obj_t *button(lv_obj_t *parent, const char *text, int x, int y, int w,
                        int h, lv_event_cb_t cb, void *data) {
  lv_obj_t *b = lv_button_create(parent);
  lv_obj_set_pos(b, x, y);
  lv_obj_set_size(b, w, h);
  lv_obj_t *l = lv_label_create(b);
  set_text(l, text);
  lv_obj_set_style_text_font(l, ui_font_cjk(16), 0);
  lv_obj_center(l);
  app_theme_button(b, 0);
  if (cb)
    lv_obj_add_event_cb(b, cb, LV_EVENT_SHORT_CLICKED, data);
  return b;
}
static void music_open(lv_event_t *e){
 (void)e;
 if(music_overlay || !copying)return;
 music_overlay=lv_obj_create(root);lv_obj_remove_style_all(music_overlay);
 lv_obj_set_size(music_overlay,360,360);lv_obj_set_style_bg_color(music_overlay,TC(CANVAS),0);
 lv_obj_set_style_bg_opa(music_overlay,LV_OPA_COVER,0);
 lv_obj_remove_flag(music_overlay,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_GESTURE_BUBBLE);kit_keep(music_overlay);
 lv_obj_t *title=label(music_overlay,"Now playing",80,46,200,TF(UI_24));
 lv_obj_set_style_text_align(title,LV_TEXT_ALIGN_CENTER,0);
 music_track=label(music_overlay,"",44,102,272,TF(UI_24));kit_keep(music_track);
 lv_label_set_long_mode(music_track,LV_LABEL_LONG_SCROLL_CIRCULAR);
 lv_obj_set_style_text_align(music_track,LV_TEXT_ALIGN_CENTER,0);
 music_artist=label(music_overlay,"",44,140,272,TF(UI_20));kit_keep(music_artist);
 lv_label_set_long_mode(music_artist,LV_LABEL_LONG_SCROLL_CIRCULAR);
 lv_obj_set_style_text_align(music_artist,LV_TEXT_ALIGN_CENTER,0);
 lv_obj_set_style_text_color(music_artist,TC(TEXT_SECONDARY),0);
 music_progress=lv_bar_create(music_overlay);kit_keep(music_progress);
 lv_obj_set_pos(music_progress,72,184);lv_obj_set_size(music_progress,216,5);lv_bar_set_range(music_progress,0,1000);
 lv_obj_set_style_bg_color(music_progress,TC(CONTROL_TRACK),0);
 lv_obj_set_style_bg_color(music_progress,TC(ACCENT_PRIMARY),LV_PART_INDICATOR);
 lv_obj_remove_flag(music_progress,LV_OBJ_FLAG_CLICKABLE);

 const char *icons[]={LV_SYMBOL_PREV,LV_SYMBOL_PLAY,LV_SYMBOL_NEXT};
 for(int i=0;i<3;i++){
  lv_obj_t *b=button(music_overlay,icons[i],62+i*82,212,72,64,music_control,(void*)(intptr_t)i);
  lv_obj_t *glyph=lv_obj_get_child(b,0);
  lv_obj_set_style_text_font(glyph,ui_font_cjk(24),0);
  lv_obj_set_style_text_color(glyph,TC(ACCENT_PRIMARY),0);
  if(th_zune()){lv_obj_set_style_shadow_width(b,0,0);lv_obj_set_style_bg_opa(b,LV_OPA_TRANSP,0);lv_obj_set_style_border_width(b,i==1?1:0,0);}
  if(i==1)music_pp=glyph;
 }
 button(music_overlay,"Close",108,302,144,38,music_control,(void*)3);
 music_refresh();
}
static void music_button_create(void){
 music_button=button(root,"",312,159,42,42,music_open,NULL);
 /* The same outline D! logo as Squash's hub, reduced for the screen edge. */
 lv_obj_set_style_radius(music_button,LV_RADIUS_CIRCLE,0);
 lv_obj_t *logo=lv_obj_get_child(music_button,0);set_text(logo,"D!");
 lv_obj_set_style_text_font(logo,&diskos_splash_disco_72,0);
 lv_obj_set_size(logo,90,72);lv_obj_set_style_text_align(logo,LV_TEXT_ALIGN_CENTER,0);
 lv_obj_set_style_text_color(logo,TC(ACCENT_PRIMARY),0);
 lv_obj_set_style_transform_scale_x(logo,96,0);lv_obj_set_style_transform_scale_y(logo,96,0);
 lv_obj_set_style_transform_pivot_x(logo,45,0);lv_obj_set_style_transform_pivot_y(logo,36,0);
 lv_obj_center(logo);kit_keep(logo);
 lv_obj_add_flag(music_button,LV_OBJ_FLAG_HIDDEN);
}
static int pick_index(const item_t *r) {
  for (int i = 0; i < nselected; i++)
    if (selected[i].kind == r->kind && !strcmp(selected[i].id, r->id))
      return i;
  return -1;
}
static void toggle(const item_t *r) {

  int i = pick_index(r);
  if (i >= 0) {
    memmove(selected + i, selected + i + 1,
            (nselected - i - 1) * sizeof *selected);
    nselected--;
  } else if (nselected < PICKS)
    selected[nselected++] = *r;
  else
    ui_toast("Selection limit: 256 items");
}
static void request(request_t req) {
  if (busy || fatal)
    return;
  atomic_store(&ds_abort_job, 0);
  copying=0;
  req.generation=++next_generation;
  current = req;
  window_pending=0;window_delivery=0;
  view = req.view;
  busy = 1;
  pthread_mutex_lock(&ds_shared.mu);
  ds_shared.req = req;
  ds_shared.command = 1;
  pthread_cond_signal(&ds_shared.wake);
  ds_shared.busy = 1;
  ds_shared.count = 0;
  ds_shared.done = 0;
  snprintf(ds_shared.message, sizeof ds_shared.message, "Loading...");
  pthread_mutex_unlock(&ds_shared.mu);
  nselected=selecting=0;
  render();
}
static void exit_cb(lv_event_t *e) {
  (void)e;
  if(discshelf_navigation_locked())return;
  close_choice(); quit = 1; screen_back();
  atomic_store(&ds_stopping, 1);
  atomic_store(&ds_abort_job, 1);
}
static void back_cb(lv_event_t *e) {
  (void)e;
  if(discshelf_navigation_locked())return;
  if (az_open()) {
    az_close();
    return;
  }
  if (busy) {atomic_store(&ds_abort_job, 1);return;}
  if(nselected){nselected=selecting=0;refresh_selection();return;}
  if (reporting) {
    reporting = 0;
    if (view == HOME)
      render();
    else
      request(current);
    return;
  }
  if (level > 0) {
    level--;
    request(stack[level]);
  } else {
    view = HOME;
    current = (request_t){0};
    render();
  }
}
static void home_cb(lv_event_t *e) {
  int v = atomic_load(&ds_source)==DS_SMB?SMB_BROWSER:(int)(intptr_t)lv_event_get_user_data(e);
  if (busy)
    return;
  level = 0;
  strcpy(captions[0], v==SMB_BROWSER?"Files":v == ALBUMS ? "Albums" : "Artists");
  request((request_t){.view = v, .letter = 'A'});
}
static void row_event(lv_event_t *e) {
  int i = (int)(intptr_t)lv_event_get_user_data(e);
  if (i < 0 || i >= nrows || busy)
    return;
  item_t r = rows[i];
  if (mp_swipe_event(&swipe,e)) {
    selecting=1;toggle(&r);onboarding_discovered(HINT_SHELF_PICK);refresh_selection();return;
  }
  if(lv_event_get_code(e)!=LV_EVENT_SHORT_CLICKED)return;
  if(selecting){toggle(&r);refresh_selection();return;}
  if (lv_event_get_code(e) == LV_EVENT_LONG_PRESSED) {
    selecting = 1;
    toggle(&r);
    refresh_selection();
    return;
  }

  if (r.kind == TRACKS || r.kind==SMB_FILE) {
    toggle(&r);
    selecting = 1;
    refresh_selection();
    return;
  }
  if (level >= (atomic_load(&ds_source)==DS_SMB?31:3))
    return;
  current.scroll_y=lv_obj_get_scroll_y(content);
  stack[level] = current;
  level++;
  snprintf(captions[level], sizeof captions[level], "%s", r.name);
  request_t req = {.view = r.kind==SMB_DIR?SMB_BROWSER:r.kind == ARTISTS ? ARTIST_ALBUMS : TRACKS};
  snprintf(req.id, sizeof req.id, "%s", r.id);
  request(req);
}
static void scroll_page(lv_event_t *e) {
  (void)e;
  if(rendering || busy || fatal || reporting || az_open() || view==HOME)return;
  int y=lv_obj_get_scroll_y(content);if(y<0)y=0;current.scroll_y=y;
  int first=y/ROW_STEP;
  if(window_pending || total_rows<=PAGE || (first>=offset && first+6<offset+nrows))return;
  int off=first>6?first-6:0;
  if(off>total_rows-PAGE)off=total_rows-PAGE;if(off<0)off=0;
  if(off==offset)return;
  request_t req=current;req.offset=off;
  pthread_mutex_lock(&ds_shared.mu);
  if(!ds_shared.command){ds_shared.req=req;ds_shared.command=3;pthread_cond_signal(&ds_shared.wake);window_pending=1;}
  pthread_mutex_unlock(&ds_shared.mu);
}

static void start_download(int replace) {
  if (busy) {
    atomic_store(&ds_abort_job, 1);
    return;
  }
  if (fatal)
    return;
  if (reporting) {
    reporting = 0;
    if (view == HOME)
      render();
    else
      request(current);
    return;
  }
  if (!nselected && view != TRACKS && view != ARTIST_ALBUMS) {
    ds_status_text("Select items using the circles");
    return;
  }
  atomic_store(&ds_abort_job, 0);
  pthread_mutex_lock(&ds_shared.mu);
  if (nselected) {
    ds_shared.npicks = nselected;
  } else {
    memset(&ds_shared.picks[0], 0, sizeof ds_shared.picks[0]);
    ds_shared.picks[0].kind = view == TRACKS ? ALBUMS : ARTISTS;
    snprintf(ds_shared.picks[0].id, sizeof ds_shared.picks[0].id, "%s", current.id);
    snprintf(ds_shared.picks[0].name, sizeof ds_shared.picks[0].name, "%s",
             captions[level]);
    ds_shared.npicks = 1;
  }
  current.generation=++next_generation;window_pending=0;copying=1;
  ds_shared.req=current;
  ds_shared.replace=replace;
  ds_shared.command = 2;
  pthread_cond_signal(&ds_shared.wake);
  ds_shared.busy = 1;
  ds_shared.done = 0;
  snprintf(ds_shared.message, sizeof ds_shared.message, "%s",atomic_load(&ds_source)==DS_SMB?"Checking selected files...":"Checking missing tracks...");
  pthread_mutex_unlock(&ds_shared.mu);
  busy = 1;
  render();
}
static void close_choice(void){if(choice){lv_obj_delete(choice);choice=NULL;}}
static void choose_download(lv_event_t *e){int replace=(int)(intptr_t)lv_event_get_user_data(e);close_choice();start_download(replace);}
static void dismiss_choice(lv_event_t *e){(void)e;close_choice();}
static void download_cb(lv_event_t *e){
 (void)e;
 if(busy || reporting){start_download(0);return;}
 if(fatal || !nselected)return;
 if(choice)return;
 choice=lv_obj_create(root);lv_obj_remove_style_all(choice);lv_obj_set_size(choice,360,360);lv_obj_set_style_bg_color(choice,TC(SCRIM),0);lv_obj_set_style_bg_opa(choice,LV_OPA_80,0);lv_obj_add_flag(choice,LV_OBJ_FLAG_CLICKABLE);lv_obj_remove_flag(choice,LV_OBJ_FLAG_SCROLLABLE);kit_keep(choice);
 lv_obj_t *p=lv_obj_create(choice);lv_obj_remove_style_all(p);lv_obj_set_size(p,284,218);lv_obj_center(p);lv_obj_set_style_bg_color(p,TC(SURFACE),0);lv_obj_set_style_bg_opa(p,LV_OPA_COVER,0);lv_obj_set_style_radius(p,th_null()?0:24,0);kit_keep(p);
 lv_obj_t*l=label(p,"Download selection",12,12,260,TF(UI_22));lv_obj_set_style_text_align(l,LV_TEXT_ALIGN_CENTER,0);
 button(p,"Missing only",26,49,232,43,choose_download,(void*)0);
 button(p,"Download again",26,102,232,43,choose_download,(void*)1);
 button(p,"Cancel",70,159,144,40,dismiss_choice,NULL);
}
static void clear_cb(lv_event_t *e) {
  (void)e;
  if (busy)
    return;
  nselected = 0;
  selecting = 0;
  refresh_selection();
}
static void select_cb(lv_event_t *e) {
  int i = (int)(intptr_t)lv_event_get_user_data(e);
  if (busy || i < 0 || i >= nrows) return;

  selecting = 1;
  toggle(&rows[i]);
  refresh_selection();
}
static void jump_to_letter(char c) {
  az_close();
  if (busy) return;
  request_t req = current;
  req.offset = 0;
  req.scroll_y=0;
  req.letter = c;req.jump=1;
  request(req);
}
static void az_cb(lv_event_t *e) {
  (void)e;
  if (busy || fatal || reporting || view == HOME || view == TRACKS) return;
  if (az_open()) az_close();
  else lv_obj_send_event(az, LV_EVENT_CLICKED, NULL);
}
static void curve_rows(void){
 curvelist_update(&list_curve);
 for(int i=0;i<PAGE;i++){
  lv_obj_t *r=row_pool[i];if(!r||lv_obj_has_flag(r,LV_OBJ_FLAG_HIDDEN))continue;
  /* Fit both ends to the actual round glass; no unused right-hand column. */
  lv_area_t a;lv_obj_get_coords(r,&a);
  int far=abs(a.y1-180)>abs(a.y2-180)?abs(a.y1-180):abs(a.y2-180);
  int half=far<176?(int)sqrtf((float)(176*176-far*far)):40;
  int left=180-half+8,right=180+half-8;
  /* Preserve the scroll-marker lane on either handedness. */
  if(disco_mirror()){if(right>330)right=330;}else if(left<30)left=30;
  int w=right-left;if(w<80)w=80;
  lv_obj_set_style_translate_x(r,0,0);lv_obj_set_x(r,left-8);lv_obj_set_width(r,w);
  lv_obj_set_x(lv_obj_get_child(r,0),12);lv_obj_set_width(lv_obj_get_child(r,0),w-60);
 }
 positions();
}
static void curved(lv_event_t *e){(void)e;if(!rendering && view!=HOME)curve_rows();}
static void positions(void){
 if(!az || view==HOME)return;
 int total=lv_obj_get_scroll_y(content)+lv_obj_get_scroll_bottom(content),y=lv_obj_get_scroll_y(content);float f=total>0?(float)y/total:0;if(f<0)f=0;if(f>1)f=1;
 char c='A';int i=y/ROW_STEP-offset;if(i>=0&&i<nrows){unsigned char k=rows[i].name[0];c=k>='a'&&k<='z'?k-32:k>='A'&&k<='Z'?k:'#';}
 float a=(212-64*f)*3.14159265f/180;
 int x=th_null()?30:disco_mx((int)(180+153*cosf(a))-14,28)+14;
 azjump_position(&alphabet,c,x,th_null()?108+(int)(140*f):(int)(180+153*sinf(a)));
}
static void update_title(void){
 if(!heading)return;char b[STR];
 if(busy&&copying)snprintf(b,sizeof b,"Downloading");
 else if(nselected)snprintf(b,sizeof b,"%d selected",nselected);
 else snprintf(b,sizeof b,"%s",view==ARTISTS?"Artists":view==ARTIST_ALBUMS?"Albums":view==SMB_BROWSER&&level==0?"Files":captions[level]);
 theme_title_text(heading,b);
 /* Dynamic titles may step down a face: centre the new face rather than
  * retaining the shared title helper's old baseline padding. */
 const lv_font_t *f=lv_obj_get_style_text_font(heading,0);
 int h=lv_font_get_line_height(f);lv_obj_set_style_pad_top(heading,0,0);
 lv_obj_set_y(heading,44-h/2);lv_obj_set_height(heading,h);
}
static void download_text(void) {
  char text[80];
  if (busy)
    strcpy(text, "Cancel");
  else if (reporting)
    strcpy(text, "Continue");
  else if (nselected)
    snprintf(text, sizeof text, "Download (%d)", nselected);
  else
    strcpy(text, "Download");
  if (set_text(lv_obj_get_child(download, 0), text))
    app_theme_button(download, 0); /* Fit the changed selection count too. */
  const lv_font_t *font=TF(UI_24);
  for(int i=0;i<4;i++){
    lv_point_t size;lv_text_get_size(&size,text,font,0,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);
    if(size.x<=lv_obj_get_width(download)-16)break;
    const lv_font_t *next=theme_font_smaller(font);if(!next || next==font)break;font=next;
  }
  lv_obj_set_style_text_font(lv_obj_get_child(download,0),font,0);
  lv_obj_set_width(heading,nselected?124:title_width);
  if ((selecting || nselected) && view!=HOME && !busy && !reporting)
    lv_obj_remove_flag(clearbtn, LV_OBJ_FLAG_HIDDEN);
  else
    lv_obj_add_flag(clearbtn, LV_OBJ_FLAG_HIDDEN);
  if (view == HOME || fatal)
    lv_obj_add_flag(download, LV_OBJ_FLAG_HIDDEN);
  else if(nselected || busy || reporting)
    lv_obj_remove_flag(download, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(download,LV_OBJ_FLAG_HIDDEN);
}
static void refresh_selection(void) {
  /* Touch feedback changes styles only: no list rebuild, no scroll jump,
   * and no deletion of the object currently dispatching the touch event. */
  if (!busy && !fatal && !reporting && view != HOME) {
    for (int i = 0; i < nrows; i++) {
      lv_obj_t *row = lv_obj_get_child(content, i);
      if (!row)
        break;
      int picked = pick_index(&rows[i]) >= 0;
      lv_obj_set_style_bg_opa(row, picked ? LV_OPA_30 : LV_OPA_TRANSP, 0);
      lv_obj_set_style_outline_width(row,picked?1:0,0);lv_obj_set_style_outline_color(row,TC(ACCENT_PRIMARY),0);
      lv_obj_t *mark = lv_obj_get_child(row, 2);
      lv_obj_t *circle = lv_obj_get_child(mark, 0);
      lv_obj_t *dot = lv_obj_get_child(circle, 0);
      int local = rows[i].local > 0;
      lv_obj_t *name=lv_obj_get_child(row,0);
      lv_label_set_long_mode(name,picked?LV_LABEL_LONG_SCROLL_CIRCULAR:LV_LABEL_LONG_DOT);
      lv_obj_set_style_border_color(circle, local ? TC(STATUS_SUCCESS) : picked ? TC(ACCENT_PRIMARY) : TC(TEXT_SECONDARY), 0);
      lv_obj_set_style_bg_color(circle, local ? TC(TEXT_MUTED) : TC(ACCENT_PRIMARY), 0);
      lv_obj_set_style_bg_opa(circle,LV_OPA_TRANSP,0);
      lv_obj_set_style_border_width(circle,picked?3:2,0);
      lv_label_set_text(dot,rows[i].local==1?LV_SYMBOL_SD_CARD:LV_SYMBOL_OK);
      lv_obj_set_style_text_color(dot,local?TC(STATUS_SUCCESS):TC(ACCENT_PRIMARY),0);
      if(rows[i].local==1 && picked)lv_label_set_text(dot,LV_SYMBOL_SD_CARD);
      lv_obj_center(dot);
      lv_obj_t*stamp=lv_obj_get_child(circle,1);if(stamp){lv_obj_set_style_text_color(stamp,TC(STATUS_SUCCESS),0);if(picked&&rows[i].local==1)lv_obj_remove_flag(stamp,LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(stamp,LV_OBJ_FLAG_HIDDEN);}
      if (picked) lv_obj_remove_flag(dot, LV_OBJ_FLAG_HIDDEN);
      else lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
    }
  }
  selecting=nselected>0;update_title();download_text();
  lv_obj_set_style_pad_bottom(content,nselected?100:44,0);
  if(az)azjump_show(&alphabet,view!=HOME && view!=TRACKS && !busy && !reporting && !fatal && !selecting);
}
static void row_create(int i){
 lv_obj_t *row=row_pool[i]=lv_obj_create(content);lv_obj_remove_style_all(row);
 lv_obj_set_size(row,276,ROW_STEP-4);lv_obj_add_flag(row,LV_OBJ_FLAG_USER_1|LV_OBJ_FLAG_IGNORE_LAYOUT);kit_keep(row);lv_obj_set_style_bg_color(row,TC(SURFACE_RAISED),0);
 lv_obj_set_style_border_side(row,LV_BORDER_SIDE_BOTTOM,0);lv_obj_set_style_border_width(row,1,0);lv_obj_set_style_border_color(row,TC(BORDER),0);
 lv_obj_set_style_bg_color(row,TC(LIST_PRESSED),LV_STATE_PRESSED);lv_obj_set_style_bg_opa(row,255,LV_STATE_PRESSED);
 lv_obj_add_flag(row,LV_OBJ_FLAG_CLICKABLE);lv_obj_remove_flag(row,LV_OBJ_FLAG_SCROLLABLE);
 const lv_font_t *row_font=th_null()?TF(USER_18):ui_font_cjk(16+2*theme_font_size());
 int text_h=lv_font_get_line_height(row_font);
 lv_obj_t *name=label(row,"",12,(ROW_STEP-4-text_h)/2,222,row_font);
 lv_obj_set_height(name,text_h);kit_keep(name); /* keep the selected row's horizontal marquee */
 lv_obj_t *unused=label(row,"",0,0,1,TF(UI_14));lv_obj_add_flag(unused,LV_OBJ_FLAG_HIDDEN);
 lv_obj_t *mark=lv_obj_create(row);lv_obj_remove_style_all(mark);lv_obj_set_size(mark,44,ROW_STEP-4);lv_obj_align(mark,LV_ALIGN_RIGHT_MID,0,0);
 lv_obj_remove_flag(mark,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_EVENT_BUBBLE);lv_obj_add_flag(mark,LV_OBJ_FLAG_CLICKABLE);
 lv_obj_add_event_cb(mark,select_cb,LV_EVENT_SHORT_CLICKED,(void*)(intptr_t)i);lv_obj_add_event_cb(mark,select_cb,LV_EVENT_LONG_PRESSED,(void*)(intptr_t)i);
 lv_obj_t *circle=lv_obj_create(mark);lv_obj_remove_style_all(circle);lv_obj_set_size(circle,30,30);lv_obj_center(circle);
 lv_obj_set_style_radius(circle,th_null()?0:LV_RADIUS_CIRCLE,0);kit_keep(circle);lv_obj_set_style_border_width(circle,2,0);lv_obj_remove_flag(circle,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
 lv_obj_t *dot=lv_label_create(circle);lv_label_set_text(dot,LV_SYMBOL_OK);lv_obj_set_style_text_font(dot,TF(UI_14),0);lv_obj_center(dot);kit_keep(dot);
 lv_obj_t*stamp=lv_label_create(circle);lv_label_set_text(stamp,LV_SYMBOL_OK);lv_obj_set_style_text_font(stamp,TF(UI_14),0);lv_obj_align(stamp,LV_ALIGN_BOTTOM_RIGHT,5,3);lv_obj_add_flag(stamp,LV_OBJ_FLAG_HIDDEN);kit_keep(stamp);
 lv_obj_add_event_cb(row,row_event,LV_EVENT_ALL,(void*)(intptr_t)i);

}
static void render_window(void){
 rendering=1;
 for(int i=0;i<PAGE;i++){
  lv_obj_t *row=row_pool[i];if(!row)continue;
  if(i>=nrows){lv_obj_add_flag(row,LV_OBJ_FLAG_HIDDEN);continue;}
  lv_obj_remove_flag(row,LV_OBJ_FLAG_HIDDEN);lv_obj_set_y(row,(offset+i)*ROW_STEP);
  if(rows[i].kind==SMB_DIR){char text[STR+2];snprintf(text,sizeof text,"%s/",rows[i].name);set_text(lv_obj_get_child(row,0),text);}
  else set_text(lv_obj_get_child(row,0),rows[i].name);
 }
 if(list_extent)lv_obj_set_y(list_extent,total_rows?total_rows*ROW_STEP-1:0);
 refresh_selection();lv_obj_update_layout(content);curve_rows();rendering=0;
}
static void render(void){
 az_close();rendering=1;
 if(music_button){if(busy&&copying)lv_obj_remove_flag(music_button,LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(music_button,LV_OBJ_FLAG_HIDDEN);}
 if(view==HOME){
  lv_obj_add_flag(brand_icon,LV_OBJ_FLAG_HIDDEN);
  lv_obj_remove_flag(landing,LV_OBJ_FLAG_HIDDEN);
  for(int k=0;k<11;k++)if(list_curve.dot[k])lv_obj_add_flag(list_curve.dot[k],LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(content,LV_OBJ_FLAG_HIDDEN);lv_obj_add_flag(heading,LV_OBJ_FLAG_HIDDEN);lv_obj_add_flag(backbtn,LV_OBJ_FLAG_HIDDEN);lv_obj_add_flag(az,LV_OBJ_FLAG_HIDDEN);lv_obj_add_flag(clearbtn,LV_OBJ_FLAG_HIDDEN);lv_obj_add_flag(note,LV_OBJ_FLAG_HIDDEN);lv_obj_add_flag(download,LV_OBJ_FLAG_HIDDEN);
  const char *status=fatal?report_message:busy?"Loading artists...":"Ready to browse";
  shelf_home_status(status,busy||fatal);rendering=0;return;
 }
 lv_obj_remove_flag(brand_icon,LV_OBJ_FLAG_HIDDEN);
 lv_obj_add_flag(landing,LV_OBJ_FLAG_HIDDEN);lv_obj_remove_flag(content,LV_OBJ_FLAG_HIDDEN);lv_obj_remove_flag(heading,LV_OBJ_FLAG_HIDDEN);lv_obj_remove_flag(backbtn,LV_OBJ_FLAG_HIDDEN);lv_obj_add_flag(note,LV_OBJ_FLAG_HIDDEN);
 char caption[STR];const char *base=busy && copying?"Downloading":view==ARTISTS?"Artists":view==ARTIST_ALBUMS||view==ALBUMS?"Albums":captions[level];
 if(current.letter && view!=TRACKS)snprintf(caption,sizeof caption,"%.480s - %c",base,current.letter);else snprintf(caption,sizeof caption,"%s",base);
 update_title();
 lv_obj_remove_flag(az,LV_OBJ_FLAG_HIDDEN);if(view==TRACKS||busy||fatal||reporting)lv_obj_add_flag(az,LV_OBJ_FLAG_HIDDEN);
 memset(row_pool,0,sizeof row_pool);lv_obj_clean(content);list_extent=NULL;progress_label=NULL;progress_bar=NULL;progress_amount=NULL;progress_percent=NULL;
 if(reporting||fatal){progress_label=label(content,report_message,44,20,230,TF(UI_20));lv_label_set_long_mode(progress_label,LV_LABEL_LONG_WRAP);}
 else if(busy){
  progress_label=label(content,"Loading...",44,16,240,TF(UI_20));kit_keep(progress_label);
  lv_obj_set_style_text_line_space(progress_label,0,0);
  lv_obj_set_height(progress_label,3*lv_font_get_line_height(TF(UI_20)));
  lv_label_set_long_mode(progress_label,LV_LABEL_LONG_DOT);
  const lv_font_t *percent_font=th_null()?TF(UI_18):TF(UI_24);
  progress_percent=label(content,"",44,108,240,percent_font);kit_keep(progress_percent);lv_obj_set_height(progress_percent,lv_font_get_line_height(percent_font));lv_label_set_long_mode(progress_percent,LV_LABEL_LONG_CLIP);lv_obj_set_style_text_align(progress_percent,LV_TEXT_ALIGN_CENTER,0);
  progress_bar=lv_bar_create(content);lv_obj_set_pos(progress_bar,52,150);lv_obj_set_size(progress_bar,240,10);lv_obj_set_style_bg_color(progress_bar,TC(CONTROL_TRACK),0);lv_obj_set_style_bg_color(progress_bar,TC(ACCENT_PRIMARY),LV_PART_INDICATOR);
  const lv_font_t *amount_font=th_null()?TF(UI_20):TF(UI_28);
  progress_amount=label(content,"",44,174,240,amount_font);kit_keep(progress_amount);lv_obj_set_height(progress_amount,lv_font_get_line_height(amount_font));lv_label_set_long_mode(progress_amount,LV_LABEL_LONG_CLIP);lv_obj_set_style_text_align(progress_amount,LV_TEXT_ALIGN_CENTER,0);
 }else{
  for(int i=0;i<PAGE;i++)row_create(i);
  list_extent=lv_obj_create(content);lv_obj_remove_style_all(list_extent);lv_obj_set_size(list_extent,1,1);lv_obj_remove_flag(list_extent,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
  render_window();lv_obj_scroll_to_y(content,current.scroll_y,LV_ANIM_OFF);lv_obj_update_layout(content);curve_rows();
  if(!total_rows){lv_obj_remove_flag(note,LV_OBJ_FLAG_HIDDEN);set_text(note,browse_message[0]?browse_message:"No matching results");}
 }
 download_text();if(music_overlay)lv_obj_move_foreground(music_overlay);rendering=0;
}

static void tick(lv_timer_t *timer){
 (void)timer;
 music_refresh();
 if(az_open())return;
 /* Keep a row under the finger stable while a viewport result is delivered. */
 for(lv_indev_t *in=lv_indev_get_next(NULL);in;in=lv_indev_get_next(in))if(lv_indev_get_state(in)==LV_INDEV_STATE_PRESSED)return;
 int redraw=0;int64_t bytes,total;char text[256];int b;unsigned transfer;
 pthread_mutex_lock(&ds_shared.mu);
 b=ds_shared.busy;bytes=ds_shared.byte_done;total=ds_shared.byte_total;transfer=ds_shared.transfer_serial;
 snprintf(text,sizeof text,"%s",ds_shared.message);
 if(ds_shared.revision!=revision){
  revision=ds_shared.revision;
  if(ds_shared.result_generation==current.generation || ds_shared.fatal){
   window_delivery=window_pending && !b && !ds_shared.done && ds_shared.view==view && ds_shared.total_count>0;
   snprintf(browse_message,sizeof browse_message,"%s",text);
   window_pending=0;fatal=ds_shared.fatal;nrows=ds_shared.count;total_rows=ds_shared.total_count;
   memcpy(rows,ds_shared.rows,nrows*sizeof *rows);offset=ds_shared.offset;current.offset=offset;
   if(current.jump){current.scroll_y=offset*ROW_STEP;current.jump=0;}
   if(view!=HOME)current.letter=ds_shared.letter;
   if(fatal)snprintf(report_message,sizeof report_message,"%s",text);
   if(ds_shared.done){reporting=1;snprintf(report_message,sizeof report_message,"%s",text);if(ds_shared.done==1){nselected=0;selecting=0;}}
   busy=b;redraw=1;
  }
 }
 pthread_mutex_unlock(&ds_shared.mu);
 if(redraw){if(window_delivery && row_pool[0])render_window();else render();}
 if(view==HOME)shelf_home_status(fatal?report_message:text,busy||fatal);
 if(progress_label && busy){
  set_text(progress_label,text);
  char amount[96];if(total>0)snprintf(amount,sizeof amount,"%.1f / %.1f MiB",bytes/1048576.0,total/1048576.0);else amount[0]=0;
  if(progress_amount)set_text(progress_amount,amount);
  if(progress_bar && total>0){
   int percent=bytes>=total?100:(int)(bytes*100/total);
   lv_bar_set_value(progress_bar,percent,LV_ANIM_OFF);
   double speed=progress_speed(transfer,bytes,lv_tick_get());char value[64];
   if(speed<0)snprintf(value,sizeof value,"%d%%",percent);
   else if(speed>=1048576)snprintf(value,sizeof value,"%d%% / %.1f MiB/s",percent,speed/1048576);
   else snprintf(value,sizeof value,"%d%% / %.0f KiB/s",percent,speed/1024);
   if(progress_percent)set_text(progress_percent,value);
  }else download_speed.active=0;
 }
 if(!busy){download_speed.active=0;if(view!=HOME)scroll_page(NULL);}
}

static void build(void) {
  root = host_root;
  lv_obj_set_style_bg_color(root, TC(CANVAS), 0);
  lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
  landing=shelf_home_create(root,home_cb,exit_cb,settings_cb);
  uint32_t before=lv_obj_get_child_count(root);
  heading=ui_header_cb(root,"Disc Shelf",back_cb);title_width=lv_obj_get_style_width(heading,0);
  int hx=140,hw=136;
  if(lv_obj_get_index(heading)>(int32_t)before){
    /* The header made its back button just before the title: one top navigation line in every
     * theme, with matched visual centres. */
    backbtn=lv_obj_get_child(root,lv_obj_get_index(heading)-1);
    lv_obj_set_pos(backbtn,92,24);lv_obj_set_size(backbtn,40,40);
    lv_obj_t *back_icon=lv_obj_get_child(backbtn,0);lv_obj_center(back_icon);
  } else {
    /* The header made its back control after the title (Zune pivot: ring + hit target). Group those in a
     * transparent box so they hide and show as one back button. The child before the title is the landing
     * group here, which must never be treated as the back button (it blanked the whole home page). */
    lv_obj_t *made[8];int n=0;
    for(uint32_t i=before;i<lv_obj_get_child_count(root)&&n<8;i++){lv_obj_t *c=lv_obj_get_child(root,i);if(c!=heading)made[n++]=c;}
    backbtn=lv_obj_create(root);lv_obj_remove_style_all(backbtn);kit_keep(backbtn);
    lv_obj_set_pos(backbtn,0,0);lv_obj_set_size(backbtn,360,100);
    lv_obj_remove_flag(backbtn,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
    for(int i=0;i<n;i++)lv_obj_set_parent(made[i],backbtn);
    hx=lv_obj_get_style_x(heading,0);hw=276-hx;
  }
  lv_obj_set_pos(heading,hx,44-lv_font_get_line_height(lv_obj_get_style_text_font(heading,0))/2);
  lv_obj_set_size(heading,hw,lv_font_get_line_height(lv_obj_get_style_text_font(heading,0)));
  lv_obj_set_style_text_align(heading,LV_TEXT_ALIGN_LEFT,0);lv_label_set_long_mode(heading,LV_LABEL_LONG_DOT);title_width=hw;
  brand_icon=shelf_brand_icon(root,170,6,20);
  content = lv_obj_create(root);
  lv_obj_remove_style_all(content);
  lv_obj_set_pos(content,8,70);
  lv_obj_set_size(content,344,252);
  lv_obj_set_style_pad_bottom(content,44,0);
  curvelist_attach(&list_curve,content,root,276);
  lv_obj_set_layout(content,LV_LAYOUT_NONE);kit_keep(content);
  lv_obj_set_scroll_dir(content, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_event_cb(content, curved, LV_EVENT_SCROLL, NULL);
  lv_obj_add_event_cb(content, scroll_page, LV_EVENT_SCROLL, NULL);
  lv_obj_add_event_cb(content, scroll_page, LV_EVENT_SCROLL_END, NULL);
  note =
      label(root, "Checking local library...", 54, 176, 252, TF(UI_20));
  lv_obj_set_height(note, 70);
  lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(note, LV_TEXT_ALIGN_CENTER, 0);
  download = button(root, "Download", 66, 282, 228, 48, download_cb, NULL);
  azjump_create(&alphabet, root, jump_to_letter, "shelf");
  az = alphabet.btn;
  letters = alphabet.grid;
  clearbtn = button(root, LV_SYMBOL_CLOSE, 270, 50, 32, 32, clear_cb, NULL);
  render();

}

void discshelf_create(lv_obj_t *r){host_root=r;}
void discshelf_landing(void){
 applanding_icon(NULL);
 applanding_show("Disc Shelf","Your music, to go.","Navidrome or SMB3.","Open Disc Shelf",APP_BUILTIN_SHELF);
}
void discshelf_open(void){
 preview_mode=0;
 if(rows||ds_context){ui_toast("Disc Shelf is still closing");return;}
 rows=calloc(PAGE,sizeof *rows);if(!rows){ui_toast("Not enough memory");return;}
 hint_requested=0;view=HOME;level=0;nrows=nselected=total_rows=offset=selecting=quit=fatal=reporting=copying=closing=modal_saved=0;revision=-1;busy=1;current=(request_t){0};row_pool[0]=NULL;choice=NULL;last_tick=0;
 if(!ds_backend_start("/usr/data/apps/disc-shelf/shelf.conf")){free(rows);rows=NULL;ui_toast("Cannot start Disc Shelf");return;}
 build();screen_show(SCR_SHELF);music_button_create();
}
int discshelf_back_consumed(void){
 if(!rows)return 0;
 if(music_overlay){music_close();return 1;}
 if(discshelf_navigation_locked())return 1;
 if(ds_setup_close())return 1;
 if(choice){close_choice();return 1;}
 if(az_open()){az_close();return 1;}
 if(view==HOME)return 0;
 back_cb(NULL);return 1;
}
int discshelf_navigation_locked(void){
 return rows && copying && !closing && (busy || discshelf_busy());
}
void discshelf_stop(void){if(!ds_context)return;closing=1;ds_backend_stop();}
int discshelf_busy(void){
 if(!ds_context||closing)return 0;pthread_mutex_lock(&ds_shared.mu);int b=ds_shared.busy;pthread_mutex_unlock(&ds_shared.mu);return b;
}
void discshelf_tick(void){
 if(!ds_context)return;
 if(!preview_mode && (!sd_io_allowed() || (screen_current()!=SCR_SHELF && screen_current()!=SCR_SAVER && screen_current()!=SCR_QUICK)))discshelf_stop();
 if(closing){
  if(!ds_backend_collect())return;
  music_close();music_button=NULL;ds_setup_close();close_choice();azjump_destroy(&alphabet);lv_obj_clean(host_root);memset(&list_curve,0,sizeof list_curve);free(rows);rows=NULL;root=NULL;closing=0;if(restart_after_close&&screen_current()==SCR_SHELF){restart_after_close=0;discshelf_open();}else {restart_after_close=0;ds_session_clear();}return;
 }
 if(lv_tick_elaps(last_tick)<200)return;last_tick=lv_tick_get();
 if(!ui_display_is_off() && screen_current()==SCR_SHELF)tick(NULL);
 if(!hint_requested&&view!=HOME&&!busy&&!fatal&&!reporting){hint_requested=1;onboarding_context(HINT_SHELF_PICK);}
}

/* Native LVGL fixture: never starts a worker or touches a server/card. */
void discshelf_preview(const char *scene){
 if(ds_context||rows)return;
 preview_mode=1;ds_context=calloc(1,sizeof *ds_context);rows=calloc(PAGE,sizeof *rows);
 if(!ds_context||!rows)return;
 pthread_mutex_init(&ds_shared.mu,NULL);pthread_cond_init(&ds_shared.wake,NULL);
 busy=fatal=reporting=selecting=nselected=level=offset=closing=0;view=ARTISTS;total_rows=140;nrows=PAGE;
 strcpy(captions[0],"Artists");
 atomic_store(&ds_source,!strncmp(scene,"smb-",4)?DS_SMB:DS_NAVIDROME);
 if(!strncmp(scene,"smb-",4)){view=SMB_BROWSER;strcpy(captions[0],"Files");}
 if(!strcmp(scene,"albums")){view=ARTIST_ALBUMS;strcpy(captions[0],"Albums");}
 if(!strcmp(scene,"tracks")||!strcmp(scene,"selected")||!strcmp(scene,"dialog")){view=TRACKS;strcpy(captions[0],"An album with a very long title");}
 const char *tracks[]={"Everything in Its Right Place","The Great Gig in the Sky","A Song with a Longer Name","How to Disappear Completely","Treefingers","Optimistic","In Limbo","Morning Bell"};
 const char *albums[]={"Kid A","The Dark Side of the Moon","Mezzanine","In Rainbows","Dummy","Moon Safari","Bloom","Music Has the Right to Children"};
 const char *names[]={"A Perfect Circle","Air","Alcest","Alice in Chains","Archive","Aurora","Beach House","Boards of Canada"};
 for(int i=0;i<PAGE;i++){
  rows[i].kind=view==TRACKS?TRACKS:view==ARTISTS?ARTISTS:ALBUMS;
  snprintf(rows[i].id,IDLEN,"fixture-%d",i);
  snprintf(rows[i].name,STR,"%s",view==TRACKS?tracks[i%8]:view==ARTIST_ALBUMS?albums[i%8]:names[i%8]);
  rows[i].local=i==1?1:i==2?2:0;
 }
 if(atomic_load(&ds_source)==DS_SMB){const char*files[]={"Albums","Björk - Live.flac","Book.m4b","Favourites","Midnight City.flac","Northern Lights.flac","Playlists","Readme.txt"};for(int i=0;i<PAGE;i++){snprintf(rows[i].name,STR,"%s",files[i%8]);rows[i].kind=i%8==0||i%8==3||i%8==6?SMB_DIR:SMB_FILE;}}
 build();screen_show(SCR_SHELF);music_button_create();
 if(!strcmp(scene,"home")||!strcmp(scene,"smb-home")){view=HOME;render();}
 if(!strcmp(scene,"selected")||!strcmp(scene,"dialog")){
  toggle(&rows[0]);toggle(&rows[1]);toggle(&rows[2]);refresh_selection();
  if(!strcmp(scene,"dialog"))download_cb(NULL);
 }
 if(!strcmp(scene,"setup")||!strcmp(scene,"smb-setup")){ds_setup_open(NULL);if(!strcmp(scene,"smb-setup")){void ds_setup_preview_source(int);ds_setup_preview_source(DS_SMB);}}
 if(!strcmp(scene,"download")||!strcmp(scene,"player")){
  view=TRACKS;busy=copying=1;download_speed.active=0;render();
  pthread_mutex_lock(&ds_shared.mu);ds_shared.busy=1;ds_shared.byte_done=37LL*1048576;ds_shared.byte_total=86LL*1048576;
  snprintf(ds_shared.message,sizeof ds_shared.message,"Downloading Everything in Its Right Place - Live at the Roundhouse, London, Special Extended Anniversary Edition\n3 saved / 2 already local");
  pthread_mutex_unlock(&ds_shared.mu);tick(NULL);
  lv_tick_inc(1000);ds_shared.byte_done+=820*1024;tick(NULL);
  assert(strstr(lv_label_get_text(progress_percent),"820 KiB/s"));
  lv_obj_update_layout(content);assert(lv_obj_get_height(progress_label)==3*lv_font_get_line_height(TF(UI_20)));
  if(!strcmp(scene,"player")){track_state_t st={.have_track=1,.duration_ms=240000,.position_ms=92000};strcpy(st.artist,"The Midnight");strcpy(st.title,"Everything in Its Right Place");ipc_seed_state(&st);music_open(NULL);}
 }
 if(!strcmp(scene,"grid")){azjump_show(&alphabet,1);lv_obj_send_event(alphabet.btn,LV_EVENT_SHORT_CLICKED,NULL);}
 if(!strcmp(scene,"wheel")){   /* Library seek = wheel: a hold must not open the wheel here, a tap opens the keypad */
  azjump_show(&alphabet,1);lv_obj_send_event(alphabet.btn,LV_EVENT_LONG_PRESSED,NULL);assert(!azjump_active());
  lv_obj_send_event(alphabet.btn,LV_EVENT_SHORT_CLICKED,NULL);assert(!azjump_active()&&!lv_obj_has_flag(letters,LV_OBJ_FLAG_HIDDEN));
  puts("PASS: Disc Shelf keeps the A-Z keypad when Library seek is the wheel");}
}

static void settings_saved(void){restart_after_close=1;discshelf_stop();}
static void settings_cb(lv_event_t*e){(void)e;if(busy){ui_toast("Wait for loading, or cancel first");return;}ds_setup_open(settings_saved);}

int discshelf_test_selected(void){return nselected;}
lv_obj_t *discshelf_test_list(void){return content;}
void discshelf_test_reset(void){
 /* Preview-only cleanup; real lifetimes are owned by the backend completion handshake. */
 if(!preview_mode)return;music_close();music_button=NULL;ds_setup_close();close_choice();azjump_destroy(&alphabet);
 lv_obj_clean(host_root);memset(&list_curve,0,sizeof list_curve);free(rows);rows=NULL;
 pthread_cond_destroy(&ds_shared.wake);pthread_mutex_destroy(&ds_shared.mu);free(ds_context);ds_context=NULL;
 root=NULL;preview_mode=0;
}
