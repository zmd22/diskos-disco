#include "simple_theme.h"
/* SPDX-License-Identifier: GPL-3.0-or-later
 * Null: shared pixel system, persistent horizontal menu, Music and vertical QS.
 * All player/radio/library actions use existing diskOS dispatchers. No
 * background I/O here.
 */
#include "null_theme.h"
#include "config.h"
#include "onboarding.h"
#include "ipc.h"
#include "null_menu.h"
#include "null_transport_icons.h"
#include "quicksettings.h"
#include "scanner.h"
#include "screens.h"
#include "theme.h"
#include "anim.h"
#include "theme_kit.h"
#include <math.h>
#include <time.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int disco_catalog_load(void);
const char *disco_catalog_id(int);
const char *disco_catalog_name(int);
void disco_catalog_open(int);
void quicksettings_null_action(int id, int hold);
extern const lv_font_t null_pixel_14;
static lv_font_t pixel14;
extern const lv_font_t null_pixel_18, null_pixel_28, null_dot_40, null_jersey_46, null_pixelify_24, null_handjet_42, null_vt323_30;
extern const lv_font_t null_fine_20;
extern const lv_font_t null_pixel_16, null_pixel_24, null_dot_31, null_mono_27;
extern const lv_font_t null_fine_16, null_fine_18, diskos_mono_16, diskos_mono_18;
extern const lv_font_t null_jersey_42, null_pixelify_22, null_pixelify_20;
extern const lv_font_t null_handjet_38, null_vt323_27, null_vt323_24, null_vt323_21, null_dot_36;
static lv_font_t music_fonts[4][5];
static lv_obj_t *qdate, *qbicon, *qsun, *track_hit, *album_hit, *track_more, *gesture_feedback, *feedback_icon;
static lv_timer_t *track_tap_timer;
enum { TRACK_MENU = 2 };
static ui_np_link_press_t track_press, track_pending;
static lv_point_t track_start;
static int track_armed, track_cancel, track_moved, track_target, pending_target;
static lv_obj_t *music_weather,*saver_bg,*saver_wash;
static char music_clock_text[32]="--:--";
static const lv_font_t *music_font(int role)
{
    int set=simple_cfg_get_int("null_music_fonts",0);if(set<0 || set>3)set=0;
    return &music_fonts[set][role];
}
const lv_font_t *null_track_font(void){return music_font(0);}
static lv_obj_t *home, *bg, *wash, *title, *artist, *album, *seek, *fill, *playhead, *times, *mode,
    *buttons[3], *icons[3], *gesture;
static lv_obj_t *nav, *band, *edge, *edge_line[5];
static lv_obj_t *clock_box, *music_clock, *music_status;
void disco_track_overlay_open(void);
void dhome_overlay_close(void);
int disco_track_overlay_visible(void);
static lv_point_precise_t edge_points[3];
static null_menu_t *menu;
static char names[10][24];
static const char *name_ptrs[10];
static lv_obj_t *qroot, *qpanel, *qcover, *qart, *qpp, *qprev, *qnext, *qclose, *qbright, *qicons[3],
    *qfields[8], *qcaptions[8];
static int qids[8], qcount;
static lv_obj_t *qbattery, *qbfill, *qbhead, *qbflow;
static lv_timer_t *qb_timer;
static void battery_motion_sync(void);
static int qbpercent = -1, qbcharging;
static lv_obj_t *volume_panel, *volume_slider, *volume_value;
static lv_timer_t *volume_timer;
static int volume_pending, volume_suppress;
static uint32_t volume_sent;
static int volume_progress=1000, volume_closing;
static const void *last_sharp;
static lv_image_dsc_t pixel_dsc;
static uint8_t *pixel_buf;
static int nav_open, nav_value, have, playing, qs_open, seek_drag, ctl_shown = 1, wake_guard,
                                                                   last_ctl_mode = -1, last_ctl_visible = -1;
static long seek_duration,seek_target;
static int seek_zoom;
static track_state_t seek_track;
static lv_point_t seek_start, gz_start, edge_start;
static uint32_t gz_t0, gz_last, last_touch;
static int edge_drag;
static lv_timer_t *clock_timer;
/* Only Home, QS and an open drawer consume the heartbeat. */
static void heartbeat_sync(void){
    if(!clock_timer)return;
    int cur=screen_current();
    if(cur==SCR_HOME || cur==SCR_QUICK || nav_open)lv_timer_resume(clock_timer);
    else lv_timer_pause(clock_timer);
}
static char last_title[192], last_artist[192], last_album[192], last_times[64];
static int left(void) { return 0; }
static lv_text_align_t alignment(void)
{
    return simple_cfg_get_int("null_text_align", 0) == 1 ? LV_TEXT_ALIGN_LEFT : LV_TEXT_ALIGN_CENTER;
}
static lv_obj_t *box(lv_obj_t *p, int x, int y, int w, int h)
{
    lv_obj_t *o = lv_obj_create(p);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, TC(CANVAS), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return o;
}
static void keep_tree(lv_obj_t *o)
{
    kit_keep(o);
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++)
        keep_tree(lv_obj_get_child(o, i));
}
static void text_set(lv_obj_t *l, const char *s)
{
    if (l && strcmp(lv_label_get_text(l), s ? s : ""))
        lv_label_set_text(l, s ? s : "");
}
static lv_obj_t *text(lv_obj_t *p, const char *s, int x, int y, int w, const lv_font_t *f, lv_color_t c)
{
    lv_obj_t *l = lv_label_create(p);
    lv_label_set_text(l, s);
    lv_obj_set_pos(l, x, y);
    lv_obj_set_size(l, w, LV_SIZE_CONTENT);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
    return l;
}
static void icon_replace(lv_obj_t *p, lv_obj_t **old, int x, int y, int kind, lv_color_t c)
{
    if (*old) {
        lv_obj_delete(*old);
        *old = NULL;
    }
    *old = null_transport_icon_create(p, x, y, (null_transport_icon_t)kind, c);
    keep_tree(*old);
}
static void controls_paint(void);
static void transport_event(lv_event_t *e)
{
    int which = (int)(intptr_t)lv_event_get_user_data(e);
    lv_event_code_t c = lv_event_get_code(e);
    lv_obj_t *b = lv_event_get_current_target(e);
    lv_obj_t *i = lv_obj_get_child(b, 0);
    if (c == LV_EVENT_PRESSED) {
        last_touch = lv_tick_get();
        if (i)
            null_transport_icon_set_colour(i, TC(TEXT_PRIMARY));
    } else if (c == LV_EVENT_RELEASED || c == LV_EVENT_PRESS_LOST) {
        if (i)
            null_transport_icon_set_colour(i, which == 1 && playing ? TC(TEXT_PRIMARY) : TC(TEXT_SECONDARY));
    } else if (c == LV_EVENT_CLICKED) {
        static const char *cmd[] = {"0201000C0002", "0201000C0000", "0201000C0001"};
        if (ui_transport_command(cmd[which]) == 0 && which == 1) {
            playing = ui_pp_icon_playing(ui_is_playing());
            controls_paint();
        }
    }
}
static lv_obj_t *transport_button(lv_obj_t *p, int x, int y, int size, int which, lv_obj_t **icon)
{
    lv_obj_t *b = box(p, x, y, size, size);
    lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    *icon = null_transport_icon_create(b, (size - 22) / 2, (size - 22) / 2,
                                       which == 0   ? NULL_ICON_PREVIOUS
                                       : which == 2 ? NULL_ICON_NEXT
                                       : playing    ? NULL_ICON_PAUSE
                                                    : NULL_ICON_PLAY,
                                       which == 1 && playing ? TC(TEXT_PRIMARY) : TC(TEXT_SECONDARY));
    ui_on(b, transport_event, LV_EVENT_CLICKED, (void *)(intptr_t)which,
          which == 0   ? "np.prev"
          : which == 1 ? "np.pp"
                       : "np.next",
          UI_CORE);
    lv_obj_add_event_cb(b, transport_event, LV_EVENT_PRESSED, (void *)(intptr_t)which);
    lv_obj_add_event_cb(b, transport_event, LV_EVENT_RELEASED, (void *)(intptr_t)which);
    lv_obj_add_event_cb(b, transport_event, LV_EVENT_PRESS_LOST, (void *)(intptr_t)which);
    return b;
}
static void controls_paint(void)
{
    static int was = -1;
    if (was != playing) {
        was = playing;
        if (buttons[1])
            icon_replace(buttons[1], &icons[1], 13, 13, playing ? NULL_ICON_PAUSE : NULL_ICON_PLAY,
                         playing ? TC(TEXT_PRIMARY) : TC(TEXT_SECONDARY));
        if (qpp)
            icon_replace(qpp, &qicons[1], 13, 13, playing ? NULL_ICON_PAUSE : NULL_ICON_PLAY,
                         playing ? TC(TEXT_PRIMARY) : TC(TEXT_SECONDARY));
    }
    if (qcover) {
        lv_obj_set_style_shadow_opa(qcover, playing ? 85 : 0, 0);
    }
}
static void mode_event(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        ui_np_mode_next();
        last_touch = lv_tick_get();
    }
}
static void gesture_event(lv_event_t *e);
static void cancel_track_tap(void)
{
    if(track_tap_timer){lv_timer_delete(track_tap_timer);track_tap_timer=NULL;}
}
static int track_contains(lv_obj_t *o,const lv_point_t *p)
{
    if(!o||lv_obj_has_flag(o,LV_OBJ_FLAG_HIDDEN))return 0;
    lv_area_t a;lv_obj_get_coords(o,&a);
    return p->x>=a.x1&&p->x<=a.x2&&p->y>=a.y1&&p->y<=a.y2;
}
static int track_link_at(const lv_point_t *p)
{
    if(track_contains(artist,p))return UI_NP_LINK_ARTIST;
    if(track_contains(title,p))return UI_NP_LINK_ALBUM;
    return TRACK_MENU;
}
static void track_begin(const lv_point_t *p,int target)
{
    if(track_tap_timer&&(!ui_np_link_current(&track_pending)||lv_tick_elaps(gz_last)>=400))gz_last=0;
    cancel_track_tap(); /* a second press owns the pending single until release */
    track_start=*p;track_cancel=track_moved=0;track_armed=1;track_target=target;
    ui_np_link_capture(&track_press);
}
static void track_open(const ui_np_link_press_t *press,int target)
{
    if(screen_current()!=SCR_HOME||!ui_np_link_current(press))return;
    if(!press->track.have_track){screen_show(SCR_LIBRARY);return;}
    if(target==TRACK_MENU)disco_track_overlay_open();else ui_np_link_open(press,target);
}
static void track_tap_later(lv_timer_t *t)
{
    (void)t;track_tap_timer=NULL;gz_last=0;
    track_open(&track_pending,pending_target);
}
static void track_event(lv_event_t *e)
{
    if(screen_current()!=SCR_HOME)return;
    lv_event_code_t c=lv_event_get_code(e);
    if(c!=LV_EVENT_PRESSED&&c!=LV_EVENT_PRESSING&&c!=LV_EVENT_RELEASED&&c!=LV_EVENT_PRESS_LOST&&
       c!=LV_EVENT_CLICKED&&c!=LV_EVENT_LONG_PRESSED)return;
    lv_indev_t *in=lv_indev_active();lv_point_t p={0,0};if(in)lv_indev_get_point(in,&p);
    int more=lv_event_get_current_target(e)==track_more;
    if(c==LV_EVENT_PRESSED){track_begin(&p,more?TRACK_MENU:track_link_at(&p));if(more)gz_last=0;}
    if(c==LV_EVENT_PRESS_LOST){track_cancel=1;cancel_track_tap();gz_last=0;return;}
    if(in&&(abs(p.x-track_start.x)>18||abs(p.y-track_start.y)>18))track_moved=1;
    if(!track_armed)return;
    if(c==LV_EVENT_LONG_PRESSED){
        cancel_track_tap();gz_last=0;track_cancel=1;
        if(more||track_moved||!have||!ui_np_link_current(&track_press))return;
        int action=simple_cfg_get_int("null_title_hold",0);
        if(action==0)disco_open_np_immersive();else if(action==1)ui_np_fav_toggle();
        return;
    }
    if(!more&&have&&simple_cfg_get_int("null_controls",0)==2){gesture_event(e);return;}
    if(c==LV_EVENT_CLICKED && !screen_press_was_long()){
        track_armed=0;if(!track_cancel&&!track_moved)track_open(&track_press,track_target);
    }
}
static void feedback_opa(void *o,int32_t v){lv_obj_set_style_opa(o,v,0);}
static void feedback_done(lv_anim_t *a){lv_obj_add_flag(a->var,LV_OBJ_FLAG_HIDDEN);}
static void gesture_show_icon(int kind)
{
    if(!gesture_feedback)return;
    icon_replace(gesture_feedback,&feedback_icon,13,13,kind,TC(TEXT_PRIMARY));
    lv_anim_delete(gesture_feedback,NULL);lv_obj_remove_flag(gesture_feedback,LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(gesture_feedback);lv_obj_set_style_opa(gesture_feedback,255,0);
    lv_anim_t a;lv_anim_init(&a);lv_anim_set_var(&a,gesture_feedback);lv_anim_set_exec_cb(&a,feedback_opa);
    lv_anim_set_values(&a,255,0);lv_anim_set_delay(&a,220);lv_anim_set_duration(&a,140);
    lv_anim_set_completed_cb(&a,feedback_done);lv_anim_start(&a);
}
static void seek_finish(void){
    seek_drag=seek_zoom=0;ui_seek_preview(0,0,0);
    if(playhead)lv_obj_set_size(playhead,7,7);
}
static void seek_event(lv_event_t *e){
    lv_indev_t *in=lv_indev_active();if(!in)return;
    lv_point_t p;lv_indev_get_point(in,&p);lv_event_code_t c=lv_event_get_code(e);
    if(c==LV_EVENT_PRESSED){
        seek_finish();ipc_get_state(&seek_track);seek_duration=seek_track.have_track?seek_track.duration_ms:0;
        seek_drag=seek_duration>0;seek_start=p;last_touch=lv_tick_get();
    }
    if(c==LV_EVENT_PRESS_LOST){seek_finish();return;}
    if(!seek_drag)return;
    if(c==LV_EVENT_LONG_PRESSED){seek_zoom=1;lv_obj_set_size(playhead,11,11);}
    if(c==LV_EVENT_PRESSED || c==LV_EVENT_PRESSING || c==LV_EVENT_LONG_PRESSED || c==LV_EVENT_RELEASED){
        lv_area_t a;lv_obj_get_coords(seek,&a);int width=lv_obj_get_width(seek),x=p.x-a.x1;
        if(x<0)x=0;if(x>width)x=width;
        seek_target=width>0?(long)((long long)seek_duration*x/width):0;
        lv_obj_set_width(fill,x?x:1);lv_obj_set_x(playhead,lv_obj_get_x(seek)+x-(seek_zoom?5:3));
        if(seek_zoom)ui_seek_preview(seek_target,seek_duration,1);
        if(c==LV_EVENT_RELEASED){
            track_state_t now;ipc_get_state(&now);
            int same=now.have_track&&!strcmp(now.path,seek_track.path)&&!strcmp(now.title,seek_track.title)&&
                !(now.pos_id>0&&seek_track.pos_id>0&&now.pos_id!=seek_track.pos_id);
            if(abs(p.y-seek_start.y)<70 && same){if(ui_seek_to(seek_target)<0)ui_toast("Couldn't seek - try again");}
            seek_finish();
        }
    }
}
static void gesture_event(lv_event_t *e)
{
    lv_indev_t *in = lv_indev_active();
    if (!in)
        return;
    lv_point_t p;
    lv_indev_get_point(in, &p);
    lv_event_code_t c = lv_event_get_code(e);
    uint32_t now = lv_tick_get();
    last_touch = now;
    if (c == LV_EVENT_PRESSED) {
        if(lv_event_get_current_target(e)==gesture)track_begin(&p,TRACK_MENU);
        gz_start = p;
        gz_t0 = now;
        if (simple_cfg_get_int("null_controls", 0) == 1 && !ctl_shown) {
            ctl_shown = 1;
            wake_guard = 1;
            lv_obj_move_foreground(gesture);
            return;
        }
    }
    if (c == LV_EVENT_RELEASED && wake_guard) {
        wake_guard = 0;
        return;
    }
    if(c==LV_EVENT_PRESS_LOST){track_cancel=1;cancel_track_tap();gz_last=0;return;}
    if(c==LV_EVENT_PRESSING&&(abs(p.x-gz_start.x)>18||abs(p.y-gz_start.y)>18)){
        track_moved=1;cancel_track_tap();gz_last=0;
    }
    if (c != LV_EVENT_RELEASED || simple_cfg_get_int("null_controls", 0) != 2)
        return;
    if(track_cancel||!ui_np_link_current(&track_press)){cancel_track_tap();gz_last=0;return;}
    int dx = p.x - gz_start.x, dy = p.y - gz_start.y;
    if (abs(dx) >= 40 && abs(dx) > 2 * abs(dy) && now - gz_t0 < 700) {
        gz_last = 0;
        cancel_track_tap();
        if(ui_transport_command(dx > 0 ? "0201000C0001" : "0201000C0002")==0)gesture_show_icon(dx>0?NULL_ICON_NEXT:NULL_ICON_PREVIOUS);
    } else if (!track_moved && abs(dx) < 18 && abs(dy) < 18 && now - gz_t0 < 400) {
        if (gz_last && now - gz_last < 400) {
            gz_last = 0;
            cancel_track_tap();
            if(ui_transport_command("0201000C0000")==0){
                playing=ui_pp_icon_playing(ui_is_playing());controls_paint();
                gesture_show_icon(playing?NULL_ICON_PAUSE:NULL_ICON_PLAY);
            }
        } else{
            gz_last=now;cancel_track_tap();track_pending=track_press;pending_target=track_target;
            track_tap_timer=lv_timer_create(track_tap_later,400,NULL);
            if(track_tap_timer)lv_timer_set_repeat_count(track_tap_timer,1);
        }
    } else {cancel_track_tap();gz_last=0;}
}
static void layout_music(void)
{
    if (!home)
        return;
    /* Split at the artist / album boundary, keeping both halves clear of the 90px
     * menu band. */
    int lift = nav_value * 46 / 1000, drop = nav_value * 51 / 1000;
    lv_obj_set_y(title, 108 - lift);
    lv_obj_set_y(artist, 150 - lift);
    lv_obj_set_y(album, 180 + drop);
    lv_obj_set_y(seek, 204 + drop);
    lv_obj_set_y(playhead, 216 + drop);
    lv_obj_set_y(times, 227 + drop);
    lv_obj_set_y(mode, 198 + drop);
    for (int i = 0; i < 3; i++)
        lv_obj_set_y(buttons[i], 254 + drop);
    lv_obj_set_y(gesture, 248 + drop);
    if(track_hit){lv_obj_set_y(track_hit,nav_value>0?64:106);lv_obj_set_size(track_hit,240,nav_value>0?68:100);}
    if(track_more)lv_obj_set_y(track_more,110-lift);
    if(album_hit)lv_obj_set_y(album_hit,174+drop);
}
static void nav_paint(void)
{
    {   /* the title lifts into the clock's space as the menu opens: the clock fades out with the weather */
        int o = nav_value >= 400 ? 0 : 255 * (400 - nav_value) / 400;
        if(music_weather)lv_obj_set_style_opa(music_weather,o,0);
        if(music_clock)lv_obj_set_style_opa(music_clock,o,0);
    }
    if (!band)
        return;
    lv_obj_set_x(band, left() ? -360 + nav_value * 360 / 1000 : 360 - nav_value * 360 / 1000);
    layout_music();
    int cx = left() ? 24 : 336, dir = left() ? 1 : -1;
    if (nav_open)
        dir = -dir;
    edge_points[0] = (lv_point_precise_t){cx - dir * 5, 168};
    edge_points[1] = (lv_point_precise_t){cx + dir * 5, 180};
    edge_points[2] = (lv_point_precise_t){cx - dir * 5, 192};
    lv_color_t col =
        nav_open && simple_cfg_get_int("null_drawer_state_colours", 1) ? TC(TEXT_PRIMARY) : TC(TEXT_SECONDARY);
    for (int i = 0; i < 5; i++) {
        lv_line_set_points(edge_line[i], edge_points, 3);
        lv_obj_set_style_line_color(edge_line[i], col, 0);
    }
    lv_obj_set_x(edge, left() ? 0 : 312);
}
static void nav_anim(void *u, int32_t v)
{
    (void)u;
    nav_value = v;
    nav_paint();
}
static void nav_set(int on)
{
    cancel_track_tap();gz_last=0;track_cancel=1;
    nav_open = !!on;
    heartbeat_sync();
    onboarding_screen(screen_current(),nav_open);
    if(on)onboarding_discovered(HINT_MENU);
    if (menu)
        null_menu_set_active(menu, on);
    last_touch = lv_tick_get();
    lv_anim_delete(nav, nav_anim);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, nav);
    lv_anim_set_exec_cb(&a, nav_anim);
    lv_anim_set_values(&a, nav_value, on ? 1000 : 0);
    lv_anim_set_duration(&a, screen_get_anim() ? 200 : 1);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
    nav_paint();
}
static void edge_event(lv_event_t *e)
{
    lv_indev_t *in = lv_indev_active();
    if (!in)
        return;
    lv_point_t p;
    lv_indev_get_point(in, &p);
    lv_event_code_t c = lv_event_get_code(e);
    if (c == LV_EVENT_PRESSED) {
        edge_start = p;
        edge_drag = 0;
        last_touch = lv_tick_get();
    } else if (c == LV_EVENT_PRESSING) {
        int dx = (p.x - edge_start.x) * (left() ? 1 : -1);
        if (abs(dx) > 7) {
            edge_drag = 1;
            lv_anim_delete(nav, nav_anim);
            nav_value = nav_open ? 1000 + dx * 4 : dx * 4;
            if (nav_value < 0)
                nav_value = 0;
            if (nav_value > 1000)
                nav_value = 1000;
            nav_paint();
        }
    } else if (c == LV_EVENT_RELEASED) {
        nav_set(edge_drag ? nav_value >= 500 : !nav_open);
    } else if (c == LV_EVENT_PRESS_LOST)
        nav_set(nav_value >= 500);
}
static void menu_selected(int index, void *u)
{
    (void)u;
    nav_set(0);
    disco_catalog_open(index);
}
static void menu_shortcut(int hold){
    const char *key=hold?"null_nav_hold":"null_nav_double";
    int action=simple_cfg_get_int(key,hold?DISCO_NAV_MUSIC:DISCO_NAV_SEARCH);
    if(action==DISCO_NAV_NONE)return;
    nav_set(0);disco_nav_action_run(action);
}
void null_menu_refresh(void)
{
    if (!band)
        return;
    if (menu) {
        null_menu_destroy(menu);
        menu = NULL;
    }
    int n = disco_catalog_load();
    if (n > 10)
        n = 10;
    static const char *ids[] = {"music",  "library", "settings",  "modes",   "shortcuts", "eq",
                                "search", "albums",  "queue",     "folders", "books",     "artists",
                                "songs",  "myqueue", "weather",   "ma",      "roulette",  "boomshine",
                                "stats",  "apps",    "songsphere"};
    static const char *caps[] = {"MUSIC", "LIBRY",  "STNGS", "MODES", "SHRTS", "EQ",    "SRCH",
                                 "ALBMS", "UP NXT", "FLDRS", "BOOKS", "ARTST", "SONGS", "QUEUE",
                                 "WTHR",  "SNDSPN", "ROULT", "BOOM",  "STATS", "APPS",  "SPHERE"};
    for (int i = 0; i < n; i++) {
        const char *s = disco_catalog_name(i);
        for (int k = 0; k < 21; k++)
            if (!strcmp(disco_catalog_id(i), ids[k])) {
                s = caps[k];
                break;
            }
        theme_upper(names[i], sizeof names[i], s);
        name_ptrs[i] = names[i];
    }
    menu = null_menu_create(band, name_ptrs, n, TC(TEXT_SECONDARY), TC(TEXT_PRIMARY), menu_selected, NULL);
    null_menu_set_shortcut(menu,menu_shortcut);
    null_menu_set_active(menu, nav_open);
    lv_obj_update_layout(band);
}
static void font_setup(void)
{
    pixel14 = null_pixel_14;
    pixel14.fallback = ui_font_cjk(14);
    const lv_font_t *sets[4][5]={
        {&null_pixel_28,&null_pixel_18,&null_fine_20,&null_fine_18,&null_pixel_28},
        {&null_jersey_46,&null_pixelify_24,&null_pixelify_22,&null_pixelify_22,&null_jersey_46},
        {&null_handjet_42,&null_vt323_30,&null_vt323_27,&null_vt323_27,&null_handjet_42},
        {&null_dot_40,&null_vt323_30,&null_vt323_27,&null_vt323_24,&null_dot_40}
    };
    for(int i=0;i<4;i++)for(int j=0;j<5;j++){
        music_fonts[i][j]=*sets[i][j];music_fonts[i][j].fallback=ui_font_cjk(j==0 || j==4?20:16);
    }
}
static void clock_event(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_LONG_PRESSED) {
        int on = lv_obj_has_flag(clock_box, LV_OBJ_FLAG_HIDDEN);
        if (simple_cfg_set_int("null_clock", on) < 0) {
            ui_toast("Couldn't save settings");
            return;
        }
        null_options_changed();
    } else if (lv_event_get_code(e) == LV_EVENT_SHORT_CLICKED &&
               !lv_obj_has_flag(clock_box, LV_OBJ_FLAG_HIDDEN) && simple_cfg_get_int("clock_weather", 1))
        weather_app_open();                                   /* weather hidden: the clock isn't a button */
}
void null_home_clock(const char *t) {
    snprintf(music_clock_text,sizeof music_clock_text,"%s",t?t:"--:--");
    text_set(music_clock,music_clock_text);text_set(qdate,music_clock_text);
}
static int nst_batt = -1, nst_chg, nst_wifi, nst_bt;      /* the last status, for redrawing on a setting change */
void null_home_status(int batt, int charging, int wifi, int bt)
{
    nst_batt = batt; nst_chg = charging; nst_wifi = wifi; nst_bt = bt;
    char b[64] = "";
    size_t n = 0;
    if (wifi)
        n += (size_t)snprintf(b + n, sizeof b - n, LV_SYMBOL_WIFI "  ");
    if (bt)
        n += (size_t)snprintf(b + n, sizeof b - n, LV_SYMBOL_BLUETOOTH "  ");
    const char *bs = charging     ? LV_SYMBOL_CHARGE
                     : batt >= 90 ? LV_SYMBOL_BATTERY_FULL
                     : batt >= 65 ? LV_SYMBOL_BATTERY_3
                     : batt >= 40 ? LV_SYMBOL_BATTERY_2
                     : batt >= 15 ? LV_SYMBOL_BATTERY_1
                                  : LV_SYMBOL_BATTERY_EMPTY;
    if (batt >= 0)
        n += (size_t)snprintf(b + n, sizeof b - n, "%s", bs);
    if (batt >= 0 && n < sizeof b && simple_cfg_get_int("clock_batt_pct", 0))   /* Clock and info > Battery percentage */
        snprintf(b + n, sizeof b - n, " %d%%", batt > 100 ? 100 : batt);
    text_set(music_status, b);
}
/* Clock and info changed: the percentage and the weather line (and with it the clock's tap) */
void null_clockinfo_apply(void)
{
    if (!th_null() || !music_status)
        return;
    if (nst_batt >= 0 || nst_wifi || nst_bt)
        null_home_status(nst_batt, nst_chg, nst_wifi, nst_bt);
    if (music_weather) {
        if (simple_cfg_get_int("clock_weather", 1))
            lv_obj_remove_flag(music_weather, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(music_weather, LV_OBJ_FLAG_HIDDEN);
    }
}
void null_home_create(lv_obj_t *r)
{
    home = r;
    font_setup();
    bg = lv_image_create(r);
    lv_obj_remove_flag(bg, LV_OBJ_FLAG_CLICKABLE);
    wash = box(r, 0, 0, 360, 360);
    lv_obj_set_style_bg_opa(wash, 153, 0);
    lv_obj_t *clock_hit = box(r, 72, 10, 216, 94);
    lv_obj_set_style_bg_opa(clock_hit, 0, 0);
    lv_obj_add_flag(clock_hit, LV_OBJ_FLAG_CLICKABLE);
    ui_on(clock_hit, clock_event, LV_EVENT_LONG_PRESSED, NULL, "null.clock.toggle", UI_REACHABLE);
    lv_obj_add_event_cb(clock_hit, clock_event, LV_EVENT_SHORT_CLICKED, NULL);
    clock_box = box(r, 72, 10, 216, 94);
    lv_obj_set_style_bg_opa(clock_box, 0, 0);
    music_status = text(clock_box, "", 0, 5, 216, TF(UI_16), TC(TEXT_SECONDARY));   /* icons a touch larger */
    music_clock = text(clock_box, "--:--", 0, 34, 216, music_font(4), TC(TEXT_PRIMARY));

    lv_obj_set_style_text_align(music_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_align(music_clock, LV_TEXT_ALIGN_CENTER, 0);
    music_weather=text(clock_box,"",0,70,216,TF(UI_16),TC(TEXT_SECONDARY));
    static lv_font_t weather_font;
    extern const lv_font_t font_weather16;
    weather_font=*TF(UI_16);weather_font.fallback=&font_weather16;   /* a touch larger */
    lv_obj_set_style_text_font(music_weather,&weather_font,0);
    title = text(r, "NOT PLAYING", 36, 116, 240, TF(HEADER), TC(TEXT_PRIMARY));
    artist = text(r, "", 36, 157, 240, &pixel14, TC(TEXT_SECONDARY));
    album = text(r, "", 36, 180, 288, TF(UI_16), TC(TEXT_SECONDARY));
    lv_label_set_long_mode(title, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_long_mode(artist, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_long_mode(album, LV_LABEL_LONG_SCROLL_CIRCULAR);
    track_hit=box(r,36,106,240,100);album_hit=box(r,36,174,288,30);
    lv_obj_t *hits[]={track_hit,album_hit};
    for(int k=0;k<2;k++){
        lv_obj_set_style_bg_opa(hits[k],0,0);lv_obj_add_flag(hits[k],LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_PRESS_LOCK);
        ui_on(hits[k],track_event,LV_EVENT_CLICKED,NULL,k?"null.album.menu":"null.track.menu",UI_REACHABLE);
        ui_on(hits[k],track_event,LV_EVENT_LONG_PRESSED,NULL,k?"null.album.immersive":"null.track.immersive",UI_REACHABLE);
        lv_obj_add_event_cb(hits[k],track_event,LV_EVENT_PRESSED,NULL);
        lv_obj_add_event_cb(hits[k],track_event,LV_EVENT_PRESSING,NULL);
        lv_obj_add_event_cb(hits[k],track_event,LV_EVENT_RELEASED,NULL);
        lv_obj_add_event_cb(hits[k],track_event,LV_EVENT_PRESS_LOST,NULL);
    }
    track_more=box(r,284,110,40,40);
    lv_obj_set_style_bg_color(track_more,TC(SURFACE),0);lv_obj_set_style_bg_opa(track_more,160,0);
    lv_obj_set_style_bg_color(track_more,TC(SURFACE_RAISED),LV_STATE_PRESSED);
    lv_obj_set_style_border_width(track_more,1,0);lv_obj_set_style_border_color(track_more,TC(TEXT_SECONDARY),0);
    lv_obj_add_flag(track_more,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_HIDDEN);
    for(int k=0;k<3;k++){
        lv_obj_t *dot=box(track_more,11+k*7,18,3,3);
        lv_obj_set_style_bg_color(dot,TC(TEXT_PRIMARY),0);
    }
    ui_on(track_more,track_event,LV_EVENT_ALL,NULL,"null.track.more",UI_REACHABLE);
    seek = box(r, 62, 204, 236, 32);
    lv_obj_set_style_bg_opa(seek, 0, 0);
    lv_obj_add_flag(seek, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_t *line = box(seek, 0, 14, 236, 3);
    lv_obj_set_style_bg_color(line, TC(TEXT_SECONDARY), 0);
    fill = box(seek, 0, 14, 1, 3);
    lv_obj_set_style_bg_color(fill, TC(TEXT_PRIMARY), 0);
    playhead = box(r, 59, 253, 7, 7);
    lv_obj_set_style_bg_color(playhead, TC(TEXT_PRIMARY), 0);
    ui_on(seek, seek_event, LV_EVENT_ALL, NULL, "np.seek", UI_CORE);
    times = text(r, "00:00/00:00/--", 62, 227, 236, music_font(3), TC(TEXT_PRIMARY));
    mode = box(r, 18, 198, 36, 40);
    lv_obj_set_style_bg_opa(mode, 0, 0);
    lv_obj_add_flag(mode, LV_OBJ_FLAG_CLICKABLE);
    text(mode, "", 6, 10, 24, TF(UI_16), TC(TEXT_SECONDARY));
    ui_on(mode, mode_event, LV_EVENT_CLICKED, NULL, "np.mode", UI_CORE);
    gesture = box(r, 76, 282, 208, 62);
    lv_obj_set_style_bg_opa(gesture, 0, 0);
    lv_obj_add_flag(gesture, LV_OBJ_FLAG_CLICKABLE);
    ui_on(gesture, gesture_event, LV_EVENT_ALL, NULL, "null.music.gestures", UI_CORE);
    for (int i = 0; i < 3; i++)
        buttons[i] = transport_button(r, 96 + i * 60, 288, 48, i, &icons[i]);
    gesture_feedback=box(r,156,260,48,48);lv_obj_set_style_bg_opa(gesture_feedback,210,0);
    lv_obj_add_flag(gesture_feedback,LV_OBJ_FLAG_HIDDEN);
    keep_tree(r);
    null_options_changed();
    null_clockinfo_apply();
}
/* Nothing playing: the size of the library under the title, and Shuffle all / Browse where the progress line was */
static lv_obj_t *idle_row;
static int idle_shown = -1;
static char idle_count[48];
static void idle_apply(void)
{
    if (!home || !title)
        return;
    int idle = !have;
    if (idle == idle_shown)
        return;
    idle_shown = idle;
    if(track_more){if(idle)lv_obj_add_flag(track_more,LV_OBJ_FLAG_HIDDEN);else lv_obj_remove_flag(track_more,LV_OBJ_FLAG_HIDDEN);}
    if (idle) {   /* the wide face: "28,619 SONGS", without "on the card" */
        snprintf(idle_count, sizeof idle_count, "%s", ui_idle_count_text());
        char *cut = strstr(idle_count, " on the card");
        if (cut && idle_count[0] != 'N')
            *cut = 0;
        else if (cut)
            snprintf(idle_count, sizeof idle_count, "No music yet");
    }
    if (!idle_row) {
        idle_row = ui_idle_actions(lv_obj_get_parent(title));
        keep_tree(idle_row);
    }
    lv_obj_t *gone[] = {seek, times, playhead};
    for (int i = 0; i < 3; i++) {
        if (idle)
            lv_obj_add_flag(gone[i], LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_remove_flag(gone[i], LV_OBJ_FLAG_HIDDEN);
    }
    if (idle || !simple_cfg_get_int("null_music_album", 1))   /* no album line without a track */
        lv_obj_add_flag(album, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_remove_flag(album, LV_OBJ_FLAG_HIDDEN);
    ui_idle_actions_show(idle_row, idle);
    if (idle) {
        lv_obj_update_layout(idle_row);
        lv_obj_align(idle_row, LV_ALIGN_TOP_MID, 0, 204);
        lv_obj_move_foreground(idle_row);
    }
}
void null_home_update(const char *t, const char *a, int p)
{
    have = t != NULL;
    if(track_tap_timer&&!ui_np_link_current(&track_pending)){cancel_track_tap();gz_last=0;}
    if(track_armed&&!ui_np_link_current(&track_press))track_cancel=1;
    playing = !!p;
    idle_apply();
    if (!t)
        a = idle_count;
    if (title) {
        char b[192];
        theme_upper(b, sizeof b, t ? t : "NOT PLAYING");   /* the wide face: "nothing playing" would have to scroll */
        if (strcmp(last_title, b)) {
            snprintf(last_title, sizeof last_title, "%s", b);
            text_set(title, b);ui_music_scroll(title);ui_music_scroll(artist);ui_music_scroll(album);
        }
        theme_upper(b, sizeof b, a ? a : "");
        if (strcmp(last_artist, b)) {
            snprintf(last_artist, sizeof last_artist, "%s", b);
            text_set(artist, b);ui_music_scroll(artist);
        }
    }
    controls_paint();
}
/* The cover decoder owns source pixels. Copy/average once per art publication
 * into this theme's cache. */
void null_art_changed(void)
{
    if (!home)
        return;
    const void *src = ui_current_sharp_img();
    last_sharp = src;
    int kind = simple_cfg_get_int("null_music_background", 0);
    if (src && lv_image_src_get_type(src) == LV_IMAGE_SRC_VARIABLE && kind == 2) {
        const lv_image_dsc_t *d = src;
        int bpp = d->header.cf == LV_COLOR_FORMAT_RGB888                                                 ? 3
                  : d->header.cf == LV_COLOR_FORMAT_XRGB8888 || d->header.cf == LV_COLOR_FORMAT_ARGB8888 ? 4
                                                                                                         : 0;
        if (bpp && d->data && d->header.w >= 20 && d->header.h >= 20) {
            if (!pixel_buf)
                pixel_buf = malloc(360 * 360 * 3);
            if (pixel_buf) {
                int size = d->header.w < d->header.h ? d->header.w : d->header.h,
                    ox = (d->header.w - size) / 2, oy = (d->header.h - size) / 2;
                for (int cy = 0; cy < 20; cy++)
                    for (int cx = 0; cx < 20; cx++) {
                        unsigned sums[3] = {0}, n = 0;
                        for (int y = cy * size / 20; y < (cy + 1) * size / 20; y++)
                            for (int x = cx * size / 20; x < (cx + 1) * size / 20; x++) {
                                const uint8_t *p = d->data + (y + oy) * d->header.stride + (x + ox) * bpp;
                                for (int c = 0; c < 3; c++)
                                    sums[c] += p[c];
                                n++;
                            }
                        for (int y = cy * 18; y < (cy + 1) * 18; y++)
                            for (int x = cx * 18; x < (cx + 1) * 18; x++)
                                for (int c = 0; c < 3; c++)
                                    pixel_buf[(y * 360 + x) * 3 + c] = (uint8_t)(sums[c] / n);
                    }
                pixel_dsc = (lv_image_dsc_t){.header = {.magic = LV_IMAGE_HEADER_MAGIC,
                                                        .cf = LV_COLOR_FORMAT_RGB888,
                                                        .w = 360,
                                                        .h = 360,
                                                        .stride = 1080},
                                             .data_size = 388800,
                                             .data = pixel_buf};
                lv_image_set_src(bg, NULL);
                lv_image_set_scale(bg, 256);
                lv_image_set_src(bg, &pixel_dsc);
                lv_obj_set_pos(bg, 0, 0);
            }
        }
    } else if (src && kind == 1) {
        lv_image_set_src(bg, NULL);
        lv_image_set_src(bg, src);
        const lv_image_dsc_t *d = src;
        if (lv_image_src_get_type(src) == LV_IMAGE_SRC_VARIABLE) {
            lv_image_set_scale(bg, 360 * 256 / d->header.w);
            lv_obj_center(bg);
        }
    }
    if (kind == 0 || !src)
        lv_obj_add_flag(bg, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_remove_flag(bg, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_opa(wash, kind == 2 ? 164 : 153, 0);
    if(saver_bg){
        lv_image_set_src(saver_bg,NULL);
        lv_image_set_src(saver_bg,lv_image_get_src(bg));
        lv_image_set_scale(saver_bg,lv_image_get_scale_x(bg));
        lv_obj_update_layout(bg);lv_obj_set_pos(saver_bg,lv_obj_get_x(bg),lv_obj_get_y(bg));
        if(lv_obj_has_flag(bg,LV_OBJ_FLAG_HIDDEN))lv_obj_add_flag(saver_bg,LV_OBJ_FLAG_HIDDEN);
        else lv_obj_remove_flag(saver_bg,LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_opa(saver_wash,(kind==2?164:153)+26,0);
    }
    if (qart) {
        const void *cover = ui_current_cover_dsc();
        lv_image_set_src(qart, cover);
        if (cover) {
            const lv_image_dsc_t *d = cover;
            lv_image_set_scale(qart, 72 * 256 / d->header.w);
            lv_obj_center(qart);
        } else
            lv_image_set_src(qart, NULL);
    }
}
static void qs_field_event(lv_event_t *e)
{
    int slot = (int)(intptr_t)lv_event_get_user_data(e);
    if (slot < 0 || slot >= qcount)
        return;
    lv_event_code_t c = lv_event_get_code(e);
    if (c == LV_EVENT_PRESSED)
        lv_obj_set_style_text_color(qcaptions[slot], TC(TEXT_PRIMARY), 0);
    else if (c == LV_EVENT_RELEASED || c == LV_EVENT_PRESS_LOST)
        lv_obj_set_style_text_color(qcaptions[slot], TC(TEXT_SECONDARY), 0);
    else if (c == LV_EVENT_LONG_PRESSED)
        quicksettings_null_action(qids[slot], 1);
    else if (c == LV_EVENT_CLICKED && !screen_press_was_long())
        quicksettings_null_action(qids[slot], 0);
}
static void qs_close_event(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED)
        screen_back();
}
static void qs_bright_event(lv_event_t *e)
{
    int value = lv_slider_get_value(lv_event_get_target(e));
    if (lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED)
        ui_backlight(value);
    else
        ui_set_brightness(value);
}
static void qs_outside(lv_event_t *e)
{
    if (lv_event_get_target(e) != qroot || lv_event_get_code(e) != LV_EVENT_CLICKED)
        return;
    screen_back();
}
static void qpos(void *u, int32_t v)
{
    lv_obj_set_y(u, v);
    if (qclose) {
        if (v == 0 && qs_open)
            lv_obj_remove_flag(qclose, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(qclose, LV_OBJ_FLAG_HIDDEN);
    }
}
static void qhidden(lv_anim_t *a)
{
    (void)a;
    if (!qs_open && qroot)
        lv_obj_add_flag(qroot, LV_OBJ_FLAG_HIDDEN);
}
void null_qs_slide(int open)
{
    if (!qpanel)
        return;
    qs_open = !!open;
    battery_motion_sync();
    lv_anim_delete(qpanel, qpos);
    if (open && lv_obj_get_y(qpanel) == 0)
        lv_obj_set_y(qpanel, -360);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, qpanel);
    lv_anim_set_exec_cb(&a, qpos);
    lv_anim_set_values(&a, lv_obj_get_y(qpanel), open ? 0 : -360);
    lv_anim_set_duration(&a, screen_get_anim() ? 200 : 1);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_completed_cb(&a, qhidden);
    lv_anim_start(&a);
}
static void qdeleted(lv_event_t *e)
{
    (void)e;
    qpanel = qcover = qart = qpp = qprev = qnext = qclose = qbright = NULL;
    memset(qicons, 0, sizeof qicons);
    memset(qfields, 0, sizeof qfields);
    memset(qcaptions, 0, sizeof qcaptions);
}
static void qs_construct(void)
{
    if (qpanel)
        return;
    qpanel = box(qroot, 72, -360, 216, 360);
    lv_obj_add_event_cb(qpanel, qdeleted, LV_EVENT_DELETE, NULL);
    /* An inexpensive, static halo separates the black column from the black Music
     * background. */
    lv_obj_set_style_shadow_color(qpanel, TC(TEXT_SECONDARY), 0);
    lv_obj_set_style_shadow_width(qpanel, 14, 0);
    lv_obj_set_style_shadow_opa(qpanel, 65, 0);
    lv_obj_set_style_border_side(qpanel, LV_BORDER_SIDE_LEFT | LV_BORDER_SIDE_RIGHT, 0);
    lv_obj_set_style_border_width(qpanel, 1, 0);
    lv_obj_set_style_border_color(qpanel, TC(TEXT_SECONDARY), 0);
    lv_obj_set_style_border_opa(qpanel, 45, 0);
        qdate = text(qpanel, "", 8, 8, 200, TF(HEADER), TC(TEXT_PRIMARY));
    lv_obj_set_style_text_align(qdate, LV_TEXT_ALIGN_CENTER, 0);
    qcover = box(qpanel, 72, 246, 72, 72);
    lv_obj_set_style_shadow_color(qcover, TC(TEXT_PRIMARY), 0);
    lv_obj_set_style_shadow_width(qcover, 18, 0);
    lv_obj_set_style_shadow_opa(qcover, playing ? 85 : 0, 0);
    qart = lv_image_create(qcover);
    lv_obj_remove_flag(qart, LV_OBJ_FLAG_CLICKABLE);
    qprev = transport_button(qpanel, 2, 260, 44, 0, &qicons[0]);
    qpp = transport_button(qcover, 12, 12, 48, 1, &qicons[1]);
    lv_obj_set_style_bg_opa(qpp, 160, 0);
    qnext = transport_button(qpanel, 170, 260, 44, 2, &qicons[2]);
    /* Four fields above and four below. Lower field hit boxes end before the
     * close handle. */
    for (int k = 0; k < 8; k++) {
        int col = k % 2, row = k / 2, y = 44 + row * 50;
        qfields[k] = box(qpanel, 4 + col * 104, y, 104, 50);
        lv_obj_set_style_bg_opa(qfields[k], 0, 0);
        lv_obj_add_flag(qfields[k], LV_OBJ_FLAG_CLICKABLE);
        qcaptions[k] = text(qfields[k], "", 0, 17, 104, &pixel14, TC(TEXT_SECONDARY));
        lv_obj_set_style_text_align(qcaptions[k], LV_TEXT_ALIGN_CENTER, 0);
        ui_on(qfields[k], qs_field_event, LV_EVENT_CLICKED, (void *)(intptr_t)k, "qs.tile", UI_CORE);
        ui_on(qfields[k], qs_field_event, LV_EVENT_LONG_PRESSED, (void *)(intptr_t)k, "qs.tile.hold",
              UI_CORE);
        lv_obj_add_event_cb(qfields[k], qs_field_event, LV_EVENT_PRESSED, (void *)(intptr_t)k);
        lv_obj_add_event_cb(qfields[k], qs_field_event, LV_EVENT_RELEASED, (void *)(intptr_t)k);
        lv_obj_add_event_cb(qfields[k], qs_field_event, LV_EVENT_PRESS_LOST, (void *)(intptr_t)k);
    }
    qclose = box(qpanel, 84, 326, 48, 32);
    lv_obj_set_style_bg_opa(qclose, 0, 0);
    lv_obj_add_flag(qclose, LV_OBJ_FLAG_CLICKABLE);
    static lv_point_precise_t points[3] = {{12, 17}, {24, 7}, {36, 17}};
    int widths[] = {19, 13, 9, 5, 3}, opas[] = {8, 17, 38, 80, 255};
    for (int i = 0; i < 5; i++) {
        lv_obj_t *l = lv_line_create(qclose);
        lv_line_set_points(l, points, 3);
        lv_obj_set_style_line_color(l, TC(TEXT_PRIMARY), 0);
        lv_obj_set_style_line_width(l, widths[i], 0);
        lv_obj_set_style_line_opa(l, opas[i], 0);
        lv_obj_set_style_line_rounded(l, 1, 0);
    }
    ui_on(qclose, qs_close_event, LV_EVENT_CLICKED, NULL, "qs.close", UI_CORE);
    lv_obj_add_flag(qclose, LV_OBJ_FLAG_HIDDEN);
    /* Keep brightness reachable without changing the eight-field composition: a
     * slim side slider. */
    for(int side=0;side<2;side++){
        lv_obj_t *strip=box(qroot,side?294:28,102,38,198);
        lv_obj_set_style_bg_color(strip,TC(FIXED_MEDIA_BLACK),0);
        lv_obj_set_style_bg_opa(strip,77,0);
    }
    qbright = lv_slider_create(qroot);
    lv_obj_set_size(qbright, 6, 160);
    lv_obj_set_pos(qbright, 310, 112);
    lv_obj_set_ext_click_area(qbright, 12);
    lv_slider_set_range(qbright, 4, 40);
    lv_slider_set_value(qbright, ui_get_brightness(), LV_ANIM_OFF);
    lv_obj_set_style_radius(qbright, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(qbright, TC(CONTROL_TRACK), LV_PART_MAIN);
    lv_obj_set_style_bg_color(qbright, TC(TEXT_PRIMARY), LV_PART_INDICATOR);
    lv_obj_set_style_radius(qbright, 0, LV_PART_INDICATOR);
    lv_obj_set_style_radius(qbright, 0, LV_PART_KNOB);
    lv_obj_set_style_bg_opa(qbright, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(qbright, TC(TEXT_SECONDARY), LV_PART_MAIN);
    lv_obj_set_style_bg_color(qbright, TC(TEXT_PRIMARY), LV_PART_KNOB);
    lv_obj_set_style_border_width(qbright, 0, LV_PART_KNOB);
    lv_obj_set_style_pad_all(qbright, 1, LV_PART_KNOB);
    if (!qbattery) {
        qbattery = box(qroot, 44, 112, 6, 160);
        lv_obj_add_flag(qbattery, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
        lv_obj_set_style_bg_color(qbattery, TC(TEXT_SECONDARY), 0);
        qbfill = box(qbattery, 0, 160, 6, 1);
        qbhead = box(qbattery, -1, 153, 8, 7);
        qbflow=box(qbattery,0,160,6,1);lv_obj_set_style_bg_color(qbflow,theme_color_mix(TC(FIXED_MEDIA_WHITE),TC(STATUS_SUCCESS),110),0);
        lv_obj_add_flag(qbflow,LV_OBJ_FLAG_HIDDEN);
    }
    if(!qbicon){
        qbicon=text(qroot,LV_SYMBOL_BATTERY_FULL,35,270,26,TF(UI_14),TC(TEXT_PRIMARY));
        extern const lv_font_t font_weather16;
        qsun=text(qroot,"\xEF\x86\x85",300,270,26,&font_weather16,TC(TEXT_PRIMARY));
        lv_obj_set_style_text_align(qbicon,LV_TEXT_ALIGN_CENTER,0);
        lv_obj_set_style_text_align(qsun,LV_TEXT_ALIGN_CENTER,0);
    }
    null_qs_battery(qbpercent, qbcharging);
    ui_on(qbright, qs_bright_event, LV_EVENT_VALUE_CHANGED, NULL, "qs.brightness", UI_CORE);
    lv_obj_add_event_cb(qbright, qs_bright_event, LV_EVENT_RELEASED, NULL);
    keep_tree(qroot);
}
void null_qs_create(lv_obj_t *r)
{
    qroot = r;
    font_setup();
    lv_obj_set_style_bg_opa(r, 0, 0);
    lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(r, qs_outside, LV_EVENT_CLICKED, NULL);
    qs_construct();
    null_qs_build();
}
void null_qs_build(void)
{
    if (!qroot)
        return;
    qs_construct();
    text_set(qdate,music_clock_text);
    int items[QS_MAX_ITEMS];
    int count = quicksettings_items_load(items);
    qcount = count;
    for(int i=0;i<8;i++)qids[i]=i<count?items[i]:-1;
    static const char *shorts[] = {"WIFI",  "BT",   "LIBRY", "SCAN",   "MODES", "STNGS",
                                   "EQ",    "SRCH", "QUEUE", "UP NXT", "SHRTS", "WTHR",
                                   "LYRCS", "INFO", "IMMRS", "FLDRS",  "FAVS", "PLY MD"};
    const lv_font_t *icons=TF(QS_ICON_28);
    int show_icons=simple_cfg_get_int("null_qs_layout",0)==1;
    for(int i=0;i<8;i++){
        if(i>=qcount){lv_obj_add_flag(qfields[i],LV_OBJ_FLAG_HIDDEN);continue;}
        lv_obj_remove_flag(qfields[i],LV_OBJ_FLAG_HIDDEN);
        int id=qids[i];
        text_set(qcaptions[i],show_icons?quicksettings_item_glyph(id):(id>=0 && id<(int)(sizeof shorts/sizeof *shorts)?shorts[id]:"ITEM"));
        lv_obj_set_style_text_font(qcaptions[i],show_icons?icons:&pixel14,0);
        lv_obj_set_y(qcaptions[i],show_icons?(50-lv_font_get_line_height(icons))/2:17);
        lv_obj_set_style_text_color(qcaptions[i],TC(TEXT_SECONDARY),0);
    }
    if (qpp) {
        icon_replace(qpp, &qicons[1], 13, 13, playing ? NULL_ICON_PAUSE : NULL_ICON_PLAY,
                     playing ? TC(TEXT_PRIMARY) : TC(TEXT_SECONDARY));
    }
    null_art_changed();
}
static void radio_state(void)
{
    if (!qs_open || !qpanel)
        return;
    for (int i = 0; i < qcount; i++) {
        if(simple_cfg_get_int("null_qs_layout",0)==1)text_set(qcaptions[i],quicksettings_item_glyph(qids[i]));
        int on = quicksettings_item_active(qids[i]);
        if (!lv_obj_has_state(qfields[i], LV_STATE_PRESSED))
        {
            lv_color_t colour=on ? TC(TEXT_PRIMARY) : TC(TEXT_SECONDARY);
            if(!lv_color_eq(lv_obj_get_style_text_color(qcaptions[i],0),colour))
                lv_obj_set_style_text_color(qcaptions[i],colour,0);
        }
    }
}
void null_qs_refresh(void){radio_state();}
void null_home_weather(const char *weather){
    char clean[128];size_t n=0;const unsigned char *p=(const unsigned char *)(weather?weather:"");
    while(*p && n+1<sizeof clean){if(p[0]==0xc2 && p[1]==0xb0){p+=2;continue;}clean[n++]=*p++;}
    clean[n]=0;if(music_weather)text_set(music_weather,clean);
}
static void time_fmt(char *b, size_t n, long ms)
{
    long s = ms > 0 ? ms / 1000 : 0;
    snprintf(b, n, "%02ld:%02ld", s / 60, s % 60);
}
static void ctl_opa(void *o, int32_t value) { lv_obj_set_style_opa(o, (lv_opa_t)value, 0); }
static void ctl_done(lv_anim_t *a)
{
    if (!ctl_shown)
        lv_obj_add_flag(a->var, LV_OBJ_FLAG_HIDDEN);
}
static void controls_visibility(int cm, int visible)
{
    ctl_shown = visible;
    if (last_ctl_mode == cm && last_ctl_visible == visible)
        return;
    int animate = last_ctl_mode == cm && cm == 1;
    last_ctl_mode = cm;
    last_ctl_visible = visible;
    for (int i = 0; i < 4; i++) {
        lv_obj_t *o = i < 3 ? buttons[i] : mode;
        lv_anim_delete(o, ctl_opa);
        if (cm == 2) {
            lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        if (!animate) {
            if (visible)
                lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
            else
                lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_opa(o, visible ? 255 : 0, 0);
        } else {
            lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
            lv_anim_t a;
            lv_anim_init(&a);
            lv_anim_set_var(&a, o);
            lv_anim_set_exec_cb(&a, ctl_opa);
            lv_anim_set_values(&a, lv_obj_get_style_opa(o, 0), visible ? 255 : 0);
            lv_anim_set_duration(&a, visible ? 180 : 400);
            lv_anim_set_completed_cb(&a, ctl_done);
            lv_anim_start(&a);
        }
        if (visible)
            lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
        else
            lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    }
    if (cm == 2 && visible) {
        lv_obj_remove_flag(mode, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(mode, LV_OBJ_FLAG_CLICKABLE);
    }
}
static void heartbeat(lv_timer_t *t)
{
    (void)t;
    if (!th_null() || ui_display_is_off())
        return;
    int cur = screen_current();
    track_state_t st = {0};
    if (cur == SCR_HOME)
        ipc_get_state(&st);
    if (cur == SCR_HOME && home) {
        char b[192], a[32], z[32];
        if (strcmp(last_album, st.have_track ? st.album : "")) {
            snprintf(last_album, sizeof last_album, "%s", st.have_track ? st.album : "");
            text_set(album, last_album);ui_music_scroll(album);
        }
        time_fmt(a, sizeof a, st.position_ms);
        time_fmt(z, sizeof z, st.duration_ms);
        char format[10]="--";
        if(st.have_track){
            const char *name=strrchr(st.path,'/');name=name?name+1:st.path;
            const char *ext=strrchr(name,'.');
            if(ext && ext[1]){int k=0;for(ext++;*ext && *ext!='?' && *ext!='#' && k<8;ext++)format[k++]=(char)toupper((unsigned char)*ext);format[k]=0;}
        }
        snprintf(b, sizeof b, "%s/%s/%s", a, z, format);
        if (strcmp(b, last_times)) {
            snprintf(last_times, sizeof last_times, "%.63s", b);
            text_set(times, b);
        }
        if (!seek_drag) {
            int v = st.have_track && st.duration_ms > 0
                        ? (int)((long long)st.position_ms * 236 / st.duration_ms)
                        : 0;
            if (v < 0)
                v = 0;
            if (v > 236)
                v = 236;
            if (lv_obj_get_width(fill) != (v ? v : 1))
                lv_obj_set_width(fill, v ? v : 1);
            if (lv_obj_get_x(playhead) != 59 + v)
                lv_obj_set_x(playhead, 59 + v);
        }
        static const char *md[] = {LV_SYMBOL_LOOP, LV_SYMBOL_LOOP, LV_SYMBOL_SHUFFLE, LV_SYMBOL_LOOP, "1"};
        int m = ui_play_mode_now();
        if (m < 0 || m > 4)
            m = 0;
        text_set(lv_obj_get_child(mode, 0), md[m]);
        int cm = simple_cfg_get_int("null_controls", 0),
            shown = cm != 1 || lv_display_get_inactive_time(NULL) < 5000;
        controls_visibility(cm, shown);
        if (cm == 1 && (!shown || wake_guard)) {
            lv_obj_remove_flag(gesture, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(gesture);
        } else
            lv_obj_add_flag(gesture, LV_OBJ_FLAG_HIDDEN);
    }
    radio_state();
    if (nav_open) {
        static const int secs[] = {0, 5, 10, 30};
        int a = simple_cfg_get_int("null_autohide", 2);
        if (a < 0 || a > 3)
            a = 2;
        uint32_t idle = lv_display_get_inactive_time(NULL);
        if (idle < 300)
            last_touch = lv_tick_get();
        if (secs[a] && lv_tick_get() - last_touch >= (uint32_t)secs[a] * 1000)
            nav_set(0);
    }
}
int null_owns_point(int x, int y)
{
    if (!th_null())
        return 0;
    if (volume_panel && !lv_obj_has_flag(volume_panel, LV_OBJ_FLAG_HIDDEN) && y >= 135 && y <= 225)
        return 1;
    if (screen_current() == SCR_QUICK || disco_track_overlay_visible())
        return 1;
    if (nav && !lv_obj_has_flag(nav,LV_OBJ_FLAG_HIDDEN) && nav_value > 0 && y >= 135 && y <= 225)
        return 1;
    if (nav && !lv_obj_has_flag(nav,LV_OBJ_FLAG_HIDDEN) && y >= 152 && y <= 208 && (left() ? x <= 48 : x >= 312))
        return 1;
    if (screen_current() == SCR_HOME) {
        if (y >= 198 && y <= 289 && x >= 18 && x < 58)
            return 1;
        if (y >= 188 && y < 310 && x >= 60 && x <= 302)
            return 1;
        if (y >= 248 && y < 360 && x >= 76 && x <= 284)
            return 1;
        if (y >= 10 && y < 68 && x >= 72 && x <= 288)
            return 1;
        if (y >= 64 && y <= 214 && x >= 36 && x <= 324)
            return 1;
    }
    return 0;
}
void null_screen_leaving(void)
{
    if (!th_null())
        return;
    dhome_overlay_close();
    cancel_track_tap();gz_last=0;track_cancel=1;track_armed=0;
    if(gesture_feedback){lv_anim_delete(gesture_feedback,NULL);lv_obj_add_flag(gesture_feedback,LV_OBJ_FLAG_HIDDEN);}
    if (nav)
        nav_set(0);
    seek_finish();
}
void null_screen_entered(int which)
{
    if (!th_null() || !nav)
        return;
    heartbeat_sync();
    lv_obj_t *root = screen_get_root(which);
    if (root && which != SCR_QUICK && which != SCR_HOME && which != SCR_EQ && which != SCR_EQ_EDITOR && which != SCR_APPLAND && which != SCR_STATS && which != SCR_SAVER && which != SCR_BT && which != SCR_WIFI && which != SCR_SONGINFO && which != SCR_APPS && which != SCR_WORKMODE && which != SCR_SHELF)
        kit_restyle(root);
    if(which==SCR_SAVER)null_art_changed();
    onboarding_screen(which,nav_open);
    int hidden = which == SCR_SHELF || which == SCR_QUICK || which == SCR_SAVER || which == SCR_NOWPLAYING ||
                 which == SCR_SPHERE || which == SCR_SETTINGS || which == SCR_SETLIST ||
                 which == SCR_SETTING_DETAIL || which == SCR_QSCONFIG || which == SCR_SCCONFIG ||
                 which == SCR_COLORPICK || which == SCR_DATETIME || which == SCR_EQ || which == SCR_EQ_EDITOR;
    if (menu)
        null_menu_set_active(menu, !hidden && nav_open);
    if (hidden)
        lv_obj_add_flag(nav, LV_OBJ_FLAG_HIDDEN);
    else {
        lv_obj_remove_flag(nav, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(nav);
        int main = which == SCR_HOME || which == SCR_LIBRARY || which == SCR_SETTINGS || which == SCR_APPS ||
                   which == SCR_APPSHELF;
        lv_obj_set_style_opa(nav, main ? 255 : 145, 0);
    }
}
void null_init(lv_obj_t *parent)
{
    lv_timer_set_period(lv_display_get_refr_timer(lv_display_get_default()), 16);
    for (lv_indev_t *in = lv_indev_get_next(NULL); in; in = lv_indev_get_next(in))
        if (lv_indev_get_type(in) == LV_INDEV_TYPE_POINTER)
            lv_timer_set_period(lv_indev_get_read_timer(in), 16);
    nav = box(parent, 0, 0, 360, 360);
    lv_obj_set_style_bg_opa(nav, 0, 0);
    band = box(nav, 360, 135, 360, 90);
    lv_obj_set_style_border_side(band, LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(band, 1, 0);
    lv_obj_set_style_border_color(band, TC(BORDER), 0);
    null_menu_refresh();
    edge = box(nav, left() ? 0 : 312, 152, 48, 56);
    lv_obj_set_style_bg_opa(edge, 0, 0);
    lv_obj_add_flag(edge, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK);
    ui_on(edge, edge_event, LV_EVENT_ALL, NULL, "null.nav.drawer", UI_CORE);
    int widths[] = {19, 13, 9, 5, 3}, opas[] = {8, 17, 38, 80, 255};
    for (int i = 0; i < 5; i++) {
        edge_line[i] = lv_line_create(nav);
        lv_obj_set_style_line_width(edge_line[i], widths[i], 0);
        lv_obj_set_style_line_opa(edge_line[i], opas[i], 0);
        lv_obj_set_style_line_rounded(edge_line[i], 1, 0);
        lv_obj_remove_flag(edge_line[i], LV_OBJ_FLAG_CLICKABLE);
    }
    nav_paint();
    keep_tree(nav);
    if (!clock_timer)
        clock_timer = lv_timer_create(heartbeat, 100, NULL);
    null_art_changed();
    null_screen_entered(screen_current());
}
void null_options_changed(void)
{
    if (!th_null() || !home)
        return;
    if (clock_box) {
        if (simple_cfg_get_int("null_clock", 1))
            lv_obj_remove_flag(clock_box, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(clock_box, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_set_style_text_font(title,music_font(0),0);
    lv_obj_set_style_text_font(artist,music_font(1),0);
    lv_obj_set_style_text_font(album,music_font(2),0);
    lv_obj_set_style_text_font(times,music_font(3),0);
    lv_obj_set_style_text_font(music_clock,music_font(4),0);
    lv_obj_set_y(music_clock,simple_cfg_get_int("null_music_fonts",0)==3?30:34);
    if(music_weather)lv_obj_set_y(music_weather,simple_cfg_get_int("null_music_fonts",0)==3?80:70);
    last_ctl_mode = -1;
    last_ctl_visible = -1;
    wake_guard = 0;
    lv_obj_set_style_text_align(title, alignment(), 0);
    lv_obj_set_style_text_align(artist, alignment(), 0);
    lv_obj_set_style_text_align(album, alignment(), 0);
    lv_obj_set_style_text_align(times, alignment(), 0);
    if (simple_cfg_get_int("null_music_album", 1) && idle_shown != 1)
        lv_obj_remove_flag(album, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(album, LV_OBJ_FLAG_HIDDEN);
    int cm = simple_cfg_get_int("null_controls", 0);
    lv_obj_add_flag(gesture, LV_OBJ_FLAG_HIDDEN);
    cancel_track_tap();gz_last=0;
    for (int i = 0; i < 3; i++) {
        if (cm == 2)
            lv_obj_add_flag(buttons[i], LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_remove_flag(buttons[i], LV_OBJ_FLAG_HIDDEN);
    }
    if (nav)
        nav_paint();
    null_art_changed();
}
static lv_obj_t *sclock, *sdate, *sweather, *sweather_icon, *strack, *sartist;
extern const lv_font_t null_pixel_70, null_fine_24, null_weather_32;
static lv_font_t saver_weather_font;
void null_saver_create(lv_obj_t *r)
{
    saver_bg=lv_image_create(r);lv_obj_remove_flag(saver_bg,LV_OBJ_FLAG_CLICKABLE);
    saver_wash=box(r,0,0,360,360);lv_obj_set_style_bg_color(saver_wash,TC(CANVAS),0);
    lv_obj_set_style_bg_opa(saver_wash,179,0);
    saver_weather_font=null_fine_24;saver_weather_font.fallback=ui_font_cjk(20);
    sweather_icon=text(r,"",56,62,40,&null_weather_32,TC(TEXT_SECONDARY));
    sweather=text(r,"",104,64,204,&saver_weather_font,TC(TEXT_SECONDARY));
    lv_obj_set_style_text_align(sweather,LV_TEXT_ALIGN_LEFT,0);
    lv_label_set_long_mode(sweather,LV_LABEL_LONG_SCROLL_CIRCULAR);
    sclock=text(r,"--:--",20,108,320,&null_pixel_70,TC(TEXT_PRIMARY));
    sdate=text(r,"",44,196,272,TF(UI_20),TC(TEXT_SECONDARY));
    lv_label_set_long_mode(sdate,LV_LABEL_LONG_SCROLL_CIRCULAR);
    strack=text(r,"",40,237,280,&music_fonts[0][0],TC(TEXT_PRIMARY));
    sartist=text(r,"",60,279,240,TF(UI_20),TC(TEXT_SECONDARY));
    lv_label_set_long_mode(strack,LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_long_mode(sartist,LV_LABEL_LONG_SCROLL_CIRCULAR);
    keep_tree(r);null_art_changed();
}
void null_saver_clock(const char *t,const char *d)
{
    char clock[32],date[80];snprintf(clock,sizeof clock,"%s",t?t:"--:--");
    char *suffix=strchr(clock,' ');if(suffix)*suffix++=0;
    text_set(sclock,clock);
    snprintf(date,sizeof date,"%s%s%s",d?d:"",suffix?"  ":"",suffix?suffix:"");text_set(sdate,date);
}
void null_saver_weather(const char *w)
{
    const unsigned char *p=(const unsigned char *)(w?w:"");while(*p==' ')p++;
    char icon[4]="";
    if(p[0]>=0xe0 && p[0]<=0xef && p[1] && p[2]){
        unsigned cp=((p[0]&15)<<12)|((p[1]&63)<<6)|(p[2]&63);lv_font_glyph_dsc_t glyph;
        if(lv_font_get_glyph_dsc(&null_weather_32,&glyph,cp,0) && !glyph.is_placeholder){memcpy(icon,p,3);p+=3;}
    }
    while(*p==' ')p++;
    text_set(sweather_icon,icon);text_set(sweather,(const char *)p);
    if(icon[0]){lv_obj_set_x(sweather,104);lv_obj_set_width(sweather,204);lv_obj_set_style_text_align(sweather,LV_TEXT_ALIGN_LEFT,0);}
    else {lv_obj_set_x(sweather,44);lv_obj_set_width(sweather,272);lv_obj_set_style_text_align(sweather,LV_TEXT_ALIGN_CENTER,0);}
}
void null_saver_track(const char *t,const char *a)
{
    char upper[192];theme_upper(upper,sizeof upper,t?t:"");text_set(strack,upper);text_set(sartist,a?a:"");
}
/* Shared screen grammar: black canvas, square controls, pixel headers, fine
 * pixel reading text. No screen is rebuilt here, so its action registry, list
 * model, menus and keyboard remain intact. */
static void null_style(lv_obj_t *o, kit_role_t role)
{
    if (!th_null())
        return;
    if (role == KIT_SCREEN) {
        if (o != qroot) {
            lv_obj_set_style_bg_color(o, TC(CANVAS), 0);
            lv_obj_set_style_bg_opa(o, 255, 0);
            lv_obj_set_style_bg_image_src(o, NULL, 0);
        }
        return;
    }
    if (role == KIT_ROW || role == KIT_BUTTON || role == KIT_ICON_BUTTON) {
        lv_obj_set_style_radius(o, 0, 0);
        lv_obj_set_style_shadow_width(o, 0, 0);
        lv_obj_set_style_bg_color(o, TC(CANVAS), 0);
        lv_obj_set_style_bg_opa(o, 0, 0);
        lv_obj_set_style_bg_color(o, TC(SURFACE_STRONG), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(o, 70, LV_STATE_PRESSED);
        lv_obj_set_style_border_width(o, role == KIT_ROW ? 1 : 0, 0);
        lv_obj_set_style_border_side(o, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_color(o, TC(BORDER), 0);
        lv_obj_set_style_text_color(o, TC(TEXT_SECONDARY), 0);
        lv_obj_set_style_text_color(o, TC(TEXT_PRIMARY), LV_STATE_PRESSED);
        return;
    }
    if (role == KIT_CARD || role == KIT_POPUP || role == KIT_SCRIM) {
        lv_obj_set_style_radius(o, 0, 0);
        lv_obj_set_style_shadow_width(o, 0, 0);
        lv_obj_set_style_bg_color(o, TC(CANVAS), 0);
        lv_obj_set_style_bg_opa(
            o,
            role == KIT_CARD ? ((lv_obj_get_width(o) >= 340 && lv_obj_get_height(o) >= 300) ? 255 : 0) : 245,
            0);
        if (role == KIT_POPUP) {
            lv_obj_set_style_border_width(o, 1, 0);
            lv_obj_set_style_border_color(o, TC(TEXT_SECONDARY), 0);
        }
        return;
    }
    if (lv_obj_check_type(o, &lv_label_class)) {
        const char *s = lv_label_get_text(o);
        int glyph = s && ((unsigned char)s[0] >= 0xE0);
        extern const lv_font_t font_weather16;
        if (lv_obj_get_style_text_font(o, 0)->fallback == &font_weather16)
            glyph = 1;
        if (role == KIT_TITLE) {
            lv_obj_set_style_text_color(o, TC(TEXT_PRIMARY), 0);
            if (!lv_obj_has_flag(o, THEME_TITLE_STEPPED))
                lv_obj_set_style_text_font(o, TF(HEADER), 0);
        } else if (role == KIT_ROW_TEXT || role == KIT_ROW_CHEVRON || role == KIT_BACK) {
            lv_obj_set_style_text_color(o, TC(TEXT_SECONDARY), 0);
        } else if (role == KIT_ROW_SUB)
            lv_obj_set_style_text_color(o, TC(TEXT_PRIMARY), 0);
        lv_obj_t *parent=lv_obj_get_parent(o);
        lv_color_t ink=lv_obj_get_style_text_color(o,0);
        if(parent && lv_obj_get_style_bg_opa(parent,0)<50 && ink.red+ink.green+ink.blue<96)
            lv_obj_set_style_text_color(o,TC(TEXT_PRIMARY),0);
        if (!glyph && s && s[0] && role != KIT_TITLE) {
            const lv_font_t *cur = lv_obj_get_style_text_font(o, 0);
            static const theme_font_role_t roles[]={THEME_FONT_UI_12,THEME_FONT_UI_14,THEME_FONT_UI_16,THEME_FONT_UI_18,THEME_FONT_UI_20,THEME_FONT_USER_14,THEME_FONT_USER_16,THEME_FONT_USER_18,THEME_FONT_USER_20};
            for(size_t i=0;i<sizeof roles/sizeof roles[0];i++)if(cur==theme_font(roles[i]))return;
            int h = lv_font_get_line_height(cur);
            const lv_font_t *f = h >= 28   ? TF(UI_28)
                                 : h >= 24 ? TF(UI_20)
                                 : h >= 21 ? TF(UI_18)
                                 : h >= 19 ? TF(UI_16)
                                 : h >= 16 ? TF(UI_14)
                                           : TF(UI_12);
            lv_obj_set_style_text_font(o, f, 0);
        }

        return;
    }
    if (role == KIT_SLIDER || role == KIT_BAR || role == KIT_TOGGLE) {
        lv_obj_set_style_radius(o, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(o, 0, LV_PART_INDICATOR);
        lv_obj_set_style_radius(o, 0, LV_PART_KNOB);
    }
    if (role == KIT_ARC) {
        lv_obj_set_style_arc_rounded(o, 0, LV_PART_MAIN);
        lv_obj_set_style_arc_rounded(o, 0, LV_PART_INDICATOR);
    }
}
static lv_obj_t *null_header(lv_obj_t *r, const char *t, lv_event_cb_t cb)
{
    if (!th_null())
        return theme_kit_default.header(r, t, cb);
    lv_obj_t *l = theme_kit_default.header(r, t, cb);
    char b[192];
    theme_upper(b, sizeof b, t);
    text_set(l, b);
    const lv_font_t *f = TF(HEADER);
    lv_point_t size;
    lv_text_get_size(&size, b, f, 0, 0, LV_COORD_MAX, 0);
    if (size.x > 152) {
        f = &pixel14;
        lv_obj_add_flag(l, THEME_TITLE_STEPPED);
    }
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_t *back = lv_obj_get_child(r, lv_obj_get_index(l) - 1);
    if (back) {
        lv_obj_set_x(back, left() ? 244 : 72);
        lv_obj_t *ic = lv_obj_get_child(back, 0);
        if (ic)
            text_set(ic, left() ? ">" : "<");
    }
    return l;
}
const theme_kit_t theme_kit_null = {
    .id = "null", .header = null_header, .style = null_style, .back_text = "<"};

void null_nav_set_open(int open)
{
    if (nav)
        nav_set(open);
}

/* The three parts reuse the former dots so existing list lifecycle remains unchanged. */
void null_scroll_line(lv_obj_t **parts, int count, int position, int total)
{
    if (count < 3 || !parts[0])
        return;
    for (int i = 0; i < count; i++) {
        if (total <= 24 || i >= 3)
            lv_obj_add_flag(parts[i], LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_remove_flag(parts[i], LV_OBJ_FLAG_HIDDEN);
    }
    if (total <= 24)
        return;
    if (position < 0)
        position = 0;
    if (position > total)
        position = total;
    int x = left() ? 327 : 30, value = (int)((long long)position * 140 / total);
    for (int i = 0; i < 3; i++) {
        if(lv_obj_get_style_radius(parts[i],0)!=0)lv_obj_set_style_radius(parts[i], 0, 0);
        if(lv_obj_get_style_bg_opa(parts[i],0)!=LV_OPA_COVER)lv_obj_set_style_bg_opa(parts[i], LV_OPA_COVER, 0);
        kit_keep(parts[i]);
    }
    lv_obj_set_pos(parts[0], x, 108);
    lv_obj_set_size(parts[0], 3, 144);
    if(!lv_color_eq(lv_obj_get_style_bg_color(parts[0],0),TC(TEXT_SECONDARY)))lv_obj_set_style_bg_color(parts[0], TC(TEXT_SECONDARY), 0);
    lv_obj_set_pos(parts[1], x, 108);
    lv_obj_set_size(parts[1], 3, value ? value : 1);
    if(!lv_color_eq(lv_obj_get_style_bg_color(parts[1],0),TC(TEXT_PRIMARY)))lv_obj_set_style_bg_color(parts[1], TC(TEXT_PRIMARY), 0);
    lv_obj_set_pos(parts[2], x - 2, 108 + value - 2);
    lv_obj_set_size(parts[2], 7, 7);
    if(!lv_color_eq(lv_obj_get_style_bg_color(parts[2],0),TC(TEXT_PRIMARY)))lv_obj_set_style_bg_color(parts[2], TC(TEXT_PRIMARY), 0);
}
void null_qs_battery(int percent, int charging)
{
    if (percent >= 0) {
        qbpercent = percent > 100 ? 100 : percent;
        qbcharging = !!charging;
    }
    if (!qbattery || qbpercent < 0)
        return;
    int h = qbpercent * 160 / 100, y = 160 - h;
    lv_color_t c = qbcharging ? TC(STATUS_SUCCESS) : qbpercent < 15 ? TC(STATUS_DANGER) : TC(TEXT_PRIMARY);
    lv_obj_set_pos(qbfill, 0, y);
    lv_obj_set_size(qbfill, 6, h ? h : 1);
    if (h)
        lv_obj_remove_flag(qbfill, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(qbfill, LV_OBJ_FLAG_HIDDEN);
    if (y > 153)
        y = 153;
    lv_obj_set_y(qbhead, y);
    lv_obj_set_style_bg_color(qbfill, c, 0);
    lv_obj_set_style_bg_color(qbhead, c, 0);
    battery_motion_sync();
    if(qbicon){text_set(qbicon,qbcharging?LV_SYMBOL_CHARGE:qbpercent>80?LV_SYMBOL_BATTERY_FULL:qbpercent>55?LV_SYMBOL_BATTERY_3:qbpercent>30?LV_SYMBOL_BATTERY_2:qbpercent>10?LV_SYMBOL_BATTERY_1:LV_SYMBOL_BATTERY_EMPTY);lv_obj_set_style_text_color(qbicon,c,0);}
}
static void battery_motion(lv_timer_t *t)
{
    (void)t;
    if(ui_display_is_off())return;
    if(!qs_open || !qbcharging || screen_current()!=SCR_QUICK){battery_motion_sync();return;}
    int h=qbpercent>0?qbpercent*160/100:0;
    if(qbflow && h){
        int floor=160-h, y=160-(int)((lv_tick_get()%1800)*(h+18)/1800),end=y+18;
        if(y<floor)y=floor;if(end>160)end=160;
        if(end>y){lv_obj_set_pos(qbflow,0,y);lv_obj_set_height(qbflow,end-y);lv_obj_remove_flag(qbflow,LV_OBJ_FLAG_HIDDEN);}
        else lv_obj_add_flag(qbflow,LV_OBJ_FLAG_HIDDEN);
    }
    if(qbicon)lv_obj_set_style_text_opa(qbicon,(lv_tick_get()/500)%2?65:255,0);
}
static void battery_motion_sync(void)
{
    if(qbcharging && qs_open){
        if(!qb_timer)qb_timer=lv_timer_create(battery_motion,50,NULL);
        else lv_timer_resume(qb_timer);
    }else{
        if(qb_timer)lv_timer_pause(qb_timer);
        if(qbflow)lv_obj_add_flag(qbflow,LV_OBJ_FLAG_HIDDEN);
        if(qbicon)lv_obj_set_style_text_opa(qbicon,255,0);
    }
}
static void volume_x(void *o,int32_t progress) {
    volume_progress=progress;lv_obj_set_x(o,24*(1000-progress)/1000);
    lv_obj_set_style_opa(o,255*progress/1000,0);
}
static void volume_hidden(lv_anim_t *a) { volume_closing=0;lv_obj_add_flag(a->var, LV_OBJ_FLAG_HIDDEN); }
static void volume_close(void)
{
    if (!volume_panel || volume_closing || lv_obj_has_flag(volume_panel, LV_OBJ_FLAG_HIDDEN))
        return;
    if (volume_timer)
        lv_timer_pause(volume_timer);
    lv_anim_delete(volume_panel, NULL);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, volume_panel);
    lv_anim_set_exec_cb(&a, volume_x);
    volume_closing=1;
    lv_anim_set_values(&a,volume_progress,0);
    lv_anim_set_duration(&a,screen_get_anim()?70:1);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
    lv_anim_set_completed_cb(&a, volume_hidden);
    lv_anim_start(&a);
}
static void volume_timeout(lv_timer_t *t)
{
    if (lv_obj_has_state(volume_slider, LV_STATE_PRESSED)) {
        lv_timer_reset(t);
        return;
    }
    volume_close();
}
static void volume_dismiss(lv_event_t *e)
{
    (void)e;
    volume_close();
}
static void volume_event(lv_event_t *e)
{
    if (volume_suppress)
        return;
    lv_event_code_t code = lv_event_get_code(e);
    int v = lv_slider_get_value(volume_slider);
    char b[12];
    snprintf(b, sizeof b, "%d", v);
    text_set(volume_value, b);
    if (code == LV_EVENT_VALUE_CHANGED) {
        if (lv_tick_elaps(volume_sent) >= 120) {
            ui_set_volume(v);
            volume_sent = lv_tick_get();
            volume_pending = 0;
        } else
            volume_pending = 1;
    } else if ((code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) && volume_pending) {
        ui_set_volume(v);
        volume_sent = lv_tick_get();
        volume_pending = 0;
    }
    lv_timer_reset(volume_timer);
    lv_timer_resume(volume_timer);
}
void null_volume_show(int value)
{
    if (!th_null())
        return;
    if (!volume_panel) {
        volume_panel = box(lv_layer_top(), 360, 135, 360, 90);
        lv_obj_add_flag(volume_panel, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_border_side(volume_panel, LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_width(volume_panel, 1, 0);
        lv_obj_set_style_border_color(volume_panel, TC(BORDER), 0);
        /* the screen above and below the band recedes (an outline as wide as the screen, in the canvas colour), so
         * half-hidden text never peeks out from behind the band */
        lv_obj_set_style_outline_width(volume_panel, 180, 0); lv_obj_set_style_outline_pad(volume_panel, 0, 0);
        lv_obj_set_style_outline_color(volume_panel, TC(CANVAS), 0); lv_obj_set_style_outline_opa(volume_panel, 190, 0);
        ui_on(volume_panel, volume_dismiss, LV_EVENT_CLICKED, NULL, "null.volume.dismiss", UI_CORE);
        lv_obj_t *badge = box(volume_panel, 140, 5, 80, 34);
        lv_obj_set_style_bg_opa(badge, 0, 0);
        volume_value = text(badge, "0", 0, 1, 80, TF(UI_28), TC(TEXT_PRIMARY));
        volume_slider = lv_slider_create(volume_panel);
        lv_obj_remove_style_all(volume_slider);
        lv_obj_set_pos(volume_slider, 62, 55);
        lv_obj_set_size(volume_slider, 236, 3);
        lv_obj_set_ext_click_area(volume_slider, 18);
        lv_slider_set_range(volume_slider, 0, VOL_MAX);
        lv_obj_set_style_bg_color(volume_slider, TC(TEXT_SECONDARY), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(volume_slider, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_bg_color(volume_slider, TC(TEXT_PRIMARY), LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(volume_slider, LV_OPA_COVER, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(volume_slider, TC(TEXT_PRIMARY), LV_PART_KNOB);
        lv_obj_set_style_bg_opa(volume_slider, LV_OPA_COVER, LV_PART_KNOB);
        lv_obj_set_style_pad_all(volume_slider, 2, LV_PART_KNOB);
        ui_on(volume_slider, volume_event, LV_EVENT_VALUE_CHANGED, NULL, "null.volume.adjust", UI_CORE);
        lv_obj_add_event_cb(volume_slider, volume_event, LV_EVENT_RELEASED, NULL);
        lv_obj_add_event_cb(volume_slider, volume_event, LV_EVENT_PRESS_LOST, NULL);
        volume_timer = lv_timer_create(volume_timeout, 1800, NULL);
        lv_timer_pause(volume_timer);
        lv_obj_add_flag(volume_panel, LV_OBJ_FLAG_HIDDEN);
        keep_tree(volume_panel);
    }
    if (value < 0)
        value = 0;
    if (value > VOL_MAX)
        value = VOL_MAX;
    if (!lv_obj_has_state(volume_slider, LV_STATE_PRESSED)) {
        volume_suppress = 1;
        lv_slider_set_value(volume_slider, value, LV_ANIM_OFF);
        volume_suppress = 0;
        char b[12];
        snprintf(b, sizeof b, "%d", value);
        text_set(volume_value, b);
    }
    int hidden=lv_obj_has_flag(volume_panel,LV_OBJ_FLAG_HIDDEN),reopening=volume_closing;
    if(lv_obj_has_flag(volume_panel,LV_OBJ_FLAG_HIDDEN))anim_pop_in(volume_panel);
    lv_obj_remove_flag(volume_panel,LV_OBJ_FLAG_HIDDEN);lv_obj_move_foreground(volume_panel);
    if(hidden || reopening){
        lv_anim_delete(volume_panel,NULL);volume_closing=0;
        lv_anim_t a;lv_anim_init(&a);lv_anim_set_var(&a,volume_panel);lv_anim_set_exec_cb(&a,volume_x);
        lv_anim_set_values(&a,hidden?0:volume_progress,1000);lv_anim_set_duration(&a,screen_get_anim()?96:1);
        lv_anim_set_path_cb(&a,lv_anim_path_ease_out);lv_anim_start(&a);
    }
    lv_timer_reset(volume_timer);
    lv_timer_resume(volume_timer);
}

/* Shared radio layout. Widgets retain the original scan/toggle/list dispatchers. */
void null_radio_skin(lv_obj_t *root,const char *name,lv_obj_t *ring,lv_obj_t *glyph,lv_obj_t *device,lv_obj_t *status,lv_obj_t *toggle,lv_obj_t *scan,lv_obj_t *list){
    if(!th_null())return;
    lv_obj_add_flag(ring,LV_OBJ_FLAG_HIDDEN);lv_obj_add_flag(glyph,LV_OBJ_FLAG_HIDDEN);
    text(root,name,46,28,268,TF(HEADER),TC(TEXT_PRIMARY));
    lv_obj_set_style_text_font(device,TF(UI_20),0);lv_obj_set_width(device,268);lv_obj_align(device,LV_ALIGN_TOP_MID,0,81);
    lv_obj_set_style_text_font(status,TF(UI_16),0);lv_obj_set_width(status,268);lv_obj_align(status,LV_ALIGN_TOP_MID,0,111);
    lv_obj_t *rule=box(root,48,140,264,2);lv_obj_set_style_bg_color(rule,TC(TEXT_SECONDARY),0);
    lv_obj_set_size(scan,112,46);lv_obj_align(scan,LV_ALIGN_TOP_MID,-68,151);
    lv_obj_set_style_radius(scan,0,0);lv_obj_set_style_bg_opa(scan,0,0);
    lv_obj_set_style_border_width(scan,1,0);lv_obj_set_style_border_color(scan,TC(TEXT_SECONDARY),0);
    lv_obj_t *caption=lv_obj_get_child(scan,0);lv_label_set_text(caption,"SCAN");lv_obj_set_style_text_font(caption,TF(UI_18),0);lv_obj_set_style_text_color(caption,TC(TEXT_SECONDARY),0);lv_obj_center(caption);
    lv_obj_set_size(toggle,54,28);lv_obj_align(toggle,LV_ALIGN_TOP_MID,84,160);lv_obj_set_ext_click_area(toggle,10);
    lv_obj_set_style_bg_color(toggle,TC(TEXT_SECONDARY),LV_PART_MAIN);
    lv_obj_set_style_bg_color(toggle,TC(TEXT_PRIMARY),LV_PART_INDICATOR|LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(toggle,TC(TEXT_PRIMARY),LV_PART_KNOB);
    lv_obj_set_pos(list,46,210);lv_obj_set_size(list,268,134);lv_obj_set_style_pad_row(list,2,0);
    keep_tree(root);
}

int null_volume_visible(void){return volume_panel && !lv_obj_has_flag(volume_panel,LV_OBJ_FLAG_HIDDEN);}

/* Native interaction checks inspect the real tiles, including empty slots. */
lv_obj_t *null_test_qs_field(int slot){return slot>=0 && slot<8?qfields[slot]:NULL;}
int null_test_qs_item(int slot){return slot>=0 && slot<qcount?qids[slot]:-1;}

/* Host regression probe; unreferenced device section is removed by the linker. */
int null_test_heartbeat_paused(void){return !clock_timer || lv_timer_get_paused(clock_timer);}

lv_obj_t *null_volume_root(void){return volume_panel;}
