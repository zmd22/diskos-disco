/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Render actual LVGL screens without starting diskOS's hardware main loop. */
#include "screens.h"
#include "ui.h"
#include "theme.h"
#include "theme_kit.h"
#include "fork_theme.h"
#include "config.h"
#include "ota.h"
#include "system.h"
#include "sdio.h"
#include "musicdb.h"
#include "battery.h"
#include <assert.h>
#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <stdatomic.h>

/* Host-only controlled failure: hold album B's load while the UI returns to A. */
static atomic_int race_waiting, race_release;
static const char *race_path="/tmp/diskos-art-race-unreadable.mp3";
int __real_art_make_all_ex_gen(const char *,const char *,const char *,const char *,int,unsigned);
int __wrap_art_make_all_ex_gen(const char *path,const char *c,const char *t,const char *b,int cancel,unsigned gen){
    if(!strcmp(path,race_path)){
        atomic_store(&race_waiting,1);
        while(!atomic_load(&race_release)) usleep(1000);
        return -1;
    }
    return __real_art_make_all_ex_gen(path,c,t,b,cancel,gen);
}

/* A fixed clock makes repeated frames comparable; this symbol exists only in the host executable. */
time_t time(time_t *out){ time_t t=1791022170; if(out) *out=t; return t; }

static unsigned char pixels[360 * 360 * 3];
static void flush(lv_display_t *d, const lv_area_t *a, unsigned char *p){
    int width = a->x2 - a->x1 + 1;
    for(int y=a->y1; y<=a->y2; y++)
        memcpy(pixels + ((size_t)y*360 + a->x1)*3, p + (size_t)(y-a->y1)*width*3, (size_t)width*3);
    lv_display_flush_ready(d);
}
static void assert_round_target(lv_obj_t *o){
    lv_area_t a; lv_obj_get_coords(o, &a);
    int x[2]={a.x1,a.x2}, y[2]={a.y1,a.y2};
    for(int i=0;i<2;i++) for(int j=0;j<2;j++){
        int dx=x[i]-180, dy=y[j]-180;
        assert(dx*dx + dy*dy < 180*180);
    }
    /* Clear the complete orbit of A's 38px cover on the 108px progress ring. */
    double dx=(a.x1+a.x2)/2.0-180, dy=(a.y1+a.y2)/2.0-180;
    assert(hypot(dx,dy) - lv_obj_get_width(o)/2.0 > 108+19);
}
int ui_np_cover_shown(void);
static int media_local(void){ return 1; }
static int find_sharp_image(lv_obj_t *root){
    if(lv_obj_check_type(root,&lv_image_class) && !lv_obj_has_flag(root,LV_OBJ_FLAG_HIDDEN)){
        const void *src=lv_image_get_src(root);
        if(src && lv_image_src_get_type(src)==LV_IMAGE_SRC_VARIABLE){
            const lv_image_dsc_t *d=src;
            if(d->header.w==364 && d->header.h==364) return 1;
        }
    }
    for(uint32_t i=0;i<lv_obj_get_child_count(root);i++)
        if(find_sharp_image(lv_obj_get_child(root,i))) return 1;
    return 0;
}
/* Fixture seams exist only in the host executable; device firmware gates remain intact. */
static int fixture, send_result, send_count;
int __real_ipc_send_cmd(const char *frame);
int __wrap_ipc_send_cmd(const char *frame){send_count++;return fixture?send_result:__real_ipc_send_cmd(frame);}
int __real_fw_custom_peq_writable(void);
int __wrap_fw_custom_peq_writable(void){return fixture?1:__real_fw_custom_peq_writable();}
int __real_fw_custom_peq_curve_writable(const int *hz,int count);
int __wrap_fw_custom_peq_curve_writable(const int *hz,int count){return fixture?1:__real_fw_custom_peq_curve_writable(hz,count);}
int __real_mdb_get_peq_ex(int p,double *m,int *g,int *hz,int *editable,double *q);
int __wrap_mdb_get_peq_ex(int p,double *m,int *g,int *hz,int *editable,double *q){
 if(!fixture)return __real_mdb_get_peq_ex(p,m,g,hz,editable,q);
 static const int freq[]={32,64,125,250,500,1000,2000,4000,8000,16000};
 *m=0;*editable=1;for(int i=0;i<10;i++){g[i]=(i%3-1)*20;hz[i]=freq[i];if(q)q[i]=0.7;}return 1;
}
int __real_mdb_song_accent(const char *p);
int __wrap_mdb_song_accent(const char *p){return fixture?0xCE713E:__real_mdb_song_accent(p);}
static void assert_no_tokens(lv_obj_t *o){
 if(lv_obj_check_type(o,&lv_label_class))assert(lv_label_get_text(o)[0]!='@');
 for(uint32_t i=0;i<lv_obj_get_child_count(o);i++)assert_no_tokens(lv_obj_get_child(o,i));
}
static int count_text(lv_obj_t *o,const char *text){
 int n=lv_obj_check_type(o,&lv_label_class)&&!strcmp(lv_label_get_text(o),text);
 for(uint32_t i=0;i<lv_obj_get_child_count(o);i++)n+=count_text(lv_obj_get_child(o,i),text);return n;
}
static lv_obj_t *find_text(lv_obj_t *o,const char *text){
    if(lv_obj_check_type(o,&lv_label_class) && !strcmp(lv_label_get_text(o),text)) return o;
    for(uint32_t i=0;i<lv_obj_get_child_count(o);i++){
        lv_obj_t *found=find_text(lv_obj_get_child(o,i),text); if(found) return found;
    }
    return NULL;
}
int main(int argc, char **argv){
    if(argc != 6){ fprintf(stderr,"usage: host-render PRESET VARIANT home|np|actions UP_NEXT OUT.ppm\n"); return 2; }
    setenv("TZ","UTC",1); tzset();
    lv_init();
    lv_display_t *d=lv_display_create(360,360); assert(d);
    static unsigned char buf[360*360*3];
    lv_display_set_color_format(d, LV_COLOR_FORMAT_RGB888);
    lv_display_set_buffers(d,buf,NULL,sizeof buf,LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(d,flush);
    cfg_load();
    if(!strcmp(argv[3],"migration") || !strcmp(argv[3],"reset")){
        int prior=atoi(argv[1]), variant=atoi(argv[2]);
        assert(!cfg_set_int_deferred("volume",37));
        if(prior==-2) assert(!cfg_set_int_deferred("ui_theme",variant));
        if(prior>=0){
            assert(!cfg_set_int_deferred("theme_preset",prior));
            if(variant>=0) assert(!cfg_set_int_deferred("theme_variant",variant));
        }
        int want=prior>=0 ? prior : (prior==-2 && variant==0 ? 8 : 9);
        int light=prior>=0 ? (variant>=0 ? variant : 0) :
                  (prior==-2 && (variant==0 || variant==2) ? 0 : 1);
        if(!strcmp(argv[3],"reset")){
            cfg_set_int_deferred("fork_theme_migrated",1);
            cfg_set_int_deferred("theme_auto",1);
            cfg_set_int_deferred("up_next",1);
            cfg_set_int_deferred("np_poster_v1",1);
            cfg_set_int_deferred("saver_ring_v1",1);
            cfg_set_int_deferred("accent_red_v1",1);
            assert(!sys_reset_settings());
            assert(cfg_get_int("theme_auto",0)==0 && cfg_get_int("up_next",0)==0);
            assert(cfg_get_int("ui_theme",-1)==-1);
            assert(cfg_get_int("np_poster_v1",0)==0);
            assert(cfg_get_int("saver_ring_v1",0)==0);
            assert(cfg_get_int("accent_red_v1",0)==0);
            want=9; light=1;
        }
        theme_init();
        assert(theme_preset()==want && theme_variant()==light);
        assert(cfg_get_int("volume",0)==37);
        assert(cfg_get_int("fork_theme_migrated",0)==1);
        assert(!cfg_flush());
        FILE *f=fopen(CFG_PATH,"r"); assert(f);
        char body[8192]; size_t n=fread(body,1,sizeof body-1,f); body[n]=0; fclose(f);
        char key[64];
        /* Reset preserves the completed migration marker and uses fresh defaults. */
        if(strcmp(argv[3],"reset")){
            snprintf(key,sizeof key,"theme_preset=%d",want); assert(strstr(body,key));
            snprintf(key,sizeof key,"theme_variant=%d",light); assert(strstr(body,key));
        }
        assert(strstr(body,"volume=37") && strstr(body,"fork_theme_migrated=1"));
        puts("PASS: persisted theme migration/reset preserves unrelated preferences");
        return 0;
    }
    assert(!cfg_set_int_deferred("fork_theme_migrated",1));
    assert(!cfg_set_int_deferred("theme_preset",atoi(argv[1])));
    assert(!cfg_set_int_deferred("theme_variant",atoi(argv[2])));
    assert(!cfg_set_int_deferred("theme_auto",0));
    assert(!cfg_set_int_deferred("up_next",atoi(argv[4])));
    theme_init();
    if(!strcmp(argv[3],"shutdown")){
        screen_set_anim(0); screens_init(); ui_shutdown_screen();
        goto render;
    }
    if(!strncmp(argv[3],"shortcuts-",10)){
        screen_set_anim(0); screens_init();
        setlist_open("Display");
        lv_obj_t *root=screen_get_root(SCR_SETLIST);
        lv_obj_t *label=find_text(root,"Shortcuts"); assert(label);
        assert(!find_text(root,"Quick Settings"));
        lv_obj_t *row=lv_obj_get_parent(label), *list=lv_obj_get_parent(row);
        lv_obj_update_layout(root);
        lv_obj_scroll_to_y(list,lv_obj_get_y(row)-72,LV_ANIM_OFF);
        if(strcmp(argv[3],"shortcuts-display")){
            lv_obj_send_event(row,LV_EVENT_CLICKED,NULL);
            assert(screen_current()==SCR_SCCONFIG);
            root=screen_get_root(SCR_SCCONFIG);
            if(!strcmp(argv[3],"shortcuts-options")){
                lv_obj_t *slot_list=NULL;
                for(uint32_t i=0;i<lv_obj_get_child_count(root);i++){
                    lv_obj_t *child=lv_obj_get_child(root,i);
                    if(lv_obj_get_child_count(child)==5 && lv_obj_check_type(lv_obj_get_child(child,0),&lv_button_class)) slot_list=child;
                }
                assert(slot_list);
                lv_obj_send_event(lv_obj_get_child(slot_list,0),LV_EVENT_CLICKED,NULL);
            } else if(!strcmp(argv[3],"shortcuts-orbit")) screen_show(SCR_APPS);
        }
        for(int i=0;i<30;i++){lv_tick_inc(10);lv_timer_handler();}
        goto render;
    }
    if(!strncmp(argv[3],"detail-",7)){            /* open Settings > Display > <row> detail, e.g. detail-Theme */
        screen_set_anim(0); screens_init(); setlist_open("Display"); screen_show(SCR_SETLIST);
        lv_obj_t *root=screen_get_root(SCR_SETLIST); lv_obj_t *label=find_text(root,argv[3]+7); assert(label);
        lv_obj_send_event(lv_obj_get_parent(label),LV_EVENT_CLICKED,NULL);
        for(int i=0;i<30;i++){lv_tick_inc(10);lv_timer_handler();}
        goto render;
    }
    if(!strcmp(argv[3],"stress")){              /* quick skips + album switches, then check art still loads */
        fixture=1;sd_io_init(media_local);assert(sd_io_resume());
        cfg_set_int_deferred("online_lyrics",0);cfg_set_int_deferred("online_art",0);
        screen_set_anim(0);screens_init();ui_set_np_style(3);screen_show(SCR_NOWPLAYING);
        const char *dir=getenv("STRESS_DIR"); assert(dir);
        int rounds=atoi(getenv("STRESS_ROUNDS")?getenv("STRESS_ROUNDS"):"40");
        unsigned seed=1; int fails=0;
        for(int r=0;r<rounds;r++){
            int alb=(int)((seed=seed*1103515245u+12345u)>>16)%6, trk=(int)(seed>>8)%3;
            int wait=(int)((seed>>4)%5)*60;      /* 0..240 ms between skips */
            track_state_t st={0};
            snprintf(st.path,sizeof st.path,"%s/a%d/t%d.png",dir,alb,trk);
            snprintf(st.album,sizeof st.album,"Album %d",alb);snprintf(st.title,sizeof st.title,"T%d",trk);snprintf(st.artist,sizeof st.artist,"X");
            st.have_track=1;st.state=2;st.duration_ms=200000;ipc_seed_state(&st);ui_update(&st);
            for(int i=0;i<wait/10;i++){ui_art_poll(NULL);lv_tick_inc(10);lv_timer_handler();usleep(10000);}
            if(r%8==7){                          /* settle: the current album must load */
                int ok=0;for(int i=0;i<1500;i++){ui_art_poll(NULL);lv_tick_inc(10);lv_timer_handler();if(ui_current_cover_dsc()&&ui_np_cover_shown()){ok=1;break;}usleep(10000);}
                { int nfd=0; DIR *dd=opendir("/proc/self/fd"); if(dd){ while(readdir(dd)) nfd++; closedir(dd); }
                  long rss=0; FILE *sf=fopen("/proc/self/statm","r"); if(sf){ long a; if(fscanf(sf,"%ld %ld",&a,&rss)!=2) rss=0; fclose(sf); }
                  printf("settle %d (album %d): %s fds=%d rss_kb=%ld\n",r,alb,ok?"cover":"MISSING",nfd,rss*4); } if(!ok) fails++;
            }
        }
        printf("%s: %d settles missing\n",fails?"FAIL":"PASS",fails); return fails?1:0;
    }
    if(!strcmp(argv[3],"fb-long")){              /* folder browser: hold a row -> its action menu */
        fixture=1;sd_io_init(media_local);assert(sd_io_resume());
        screen_set_anim(0);screens_init();
        void folderbrowser_open(void); folderbrowser_open();
        for(int i=0;i<60;i++){lv_tick_inc(10);lv_timer_handler();usleep(2000);}
        lv_obj_t *root=screen_get_root(SCR_FOLDER); lv_obj_t *lbl=find_text(root,"Album"); assert(lbl);
        lv_obj_t *row=lv_obj_get_parent(lbl);
        lv_obj_send_event(row,LV_EVENT_PRESSED,NULL);
        lv_obj_send_event(row,LV_EVENT_LONG_PRESSED,NULL);
        lv_obj_send_event(row,LV_EVENT_RELEASED,NULL);
        lv_obj_send_event(row,LV_EVENT_CLICKED,NULL);
        for(int i=0;i<60;i++){lv_tick_inc(10);lv_timer_handler();}
        goto render;
    }
    if(!strcmp(argv[3],"group-menu")){           /* Library long-press on an album: its action menu */
        screen_set_anim(0);screens_init();screen_show(SCR_LIBRARY);
        songmenu_open_group("ALBUM","Mother's Milk","Mother's Milk",NULL);
        for(int i=0;i<30;i++){lv_tick_inc(10);lv_timer_handler();}
        goto render;
    }
    if(!strncmp(argv[3],"lib-",4)){                 /* Library: open a category (lib-Albums, lib-Songs...), scroll a bit */
        fixture=1;sd_io_init(media_local);assert(sd_io_resume());
        screen_set_anim(0);screens_init();screen_show(SCR_LIBRARY);
        for(int i=0;i<20;i++){lv_tick_inc(10);lv_timer_handler();}
        lv_obj_t *root=screen_get_root(SCR_LIBRARY);
        char cat[40]; snprintf(cat,sizeof cat,"%s",argv[3]+4); char *sub=strchr(cat,'/'); if(sub) *sub++=0;
        lv_obj_t *l=find_text(root,cat); assert(l); lv_obj_send_event(lv_obj_get_parent(l),LV_EVENT_CLICKED,NULL);
        if(sub){ for(int i=0;i<20;i++){lv_tick_inc(10);lv_timer_handler();} l=find_text(root,sub); assert(l); lv_obj_send_event(lv_obj_get_parent(l),LV_EVENT_SHORT_CLICKED,NULL); }
        for(int i=0;i<40;i++){lv_tick_inc(10);lv_timer_handler();}
        const char *sc=getenv("LIB_SCROLL"); if(sc){ lv_obj_t *lst=NULL;
            for(uint32_t k=0;k<lv_obj_get_child_count(root);k++){ lv_obj_t *c=lv_obj_get_child(root,k); if(lv_obj_get_child_count(c)>5 && lv_obj_get_scroll_bottom(c)>0) lst=c; }
            if(lst){ lv_obj_scroll_by(lst,0,-atoi(sc),LV_ANIM_OFF); lv_obj_send_event(lst,LV_EVENT_SCROLL,NULL); } }
        if(getenv("LIB_HOLD")){                      /* fork: hold a track row -> its action menu, never playback */
            for(int i=0;i<60;i++){lv_tick_inc(10);lv_timer_handler();}
            lv_obj_t *t=ui_action_find("library.song.long"); if(!t) t=ui_action_find("library.scope_song"); assert(t);
            int before=send_count; lv_obj_send_event(t,LV_EVENT_LONG_PRESSED,NULL); assert(send_count==before);
        }
        for(int i=0;i<40;i++){lv_tick_inc(10);lv_timer_handler();}
        goto render;
    }
    if(!strncmp(argv[3],"search",6)){              /* fork: search-<query>[-go]: type, optionally press Search */
        fixture=1;sd_io_init(media_local);assert(sd_io_resume());
        screen_set_anim(0);screens_init();screen_show(SCR_SEARCH);
        for(int i=0;i<20;i++){lv_tick_inc(10);lv_timer_handler();}
        lv_obj_t *root=screen_get_root(SCR_SEARCH);
        const char *q=argv[3][6]=='-'?argv[3]+7:""; int go=strstr(q,"-go")!=NULL;
        if(q[0]){ void search_landing_leave(void); search_landing_leave(); }   /* Disco: past the landing page */
        for(const char *c=q;*c && *c!='-';c++){                 /* tap the keys by their label */
            char lab[2]={(char)(*c>='a'&&*c<='z'?*c-32:*c),0}; lv_obj_t *l=find_text(root,*c==' '?"space":lab); assert(l);
            lv_obj_send_event(lv_obj_get_parent(l),LV_EVENT_SHORT_CLICKED,NULL);
        }
        assert(search_scroller()==NULL);                       /* typing never searches */
        if(go){ int before=send_count; lv_obj_send_event(ui_action_find("search.go"),LV_EVENT_CLICKED,NULL);
                assert(search_scroller()!=NULL); assert(send_count==before);
                if(getenv("SEARCH_BACK")){ assert(search_back_consumed()); assert(search_scroller()==NULL); } }
        for(int i=0;i<40;i++){lv_tick_inc(10);lv_timer_handler();}
        goto render;
    }
    if(!strcmp(argv[3],"scan-orbit")){             /* fork: the rescan runner on the rim (over Home) */
        screen_set_anim(0);screens_init();screen_show(SCR_HOME);
        ui_scan_orbit(1);
        for(int i=0;i<50;i++){lv_tick_inc(10);lv_timer_handler();}
        goto render;
    }
    if(!strcmp(argv[3],"volume")){                 /* the volume popup over Home */
        screen_set_anim(0);screens_init();screen_show(SCR_HOME);
        void ui_show_volume(int); ui_show_volume(74);
        for(int i=0;i<30;i++){lv_tick_inc(10);lv_timer_handler();}
        goto render;
    }
    if(!strncmp(argv[3],"disco-",6)){              /* "Disco" theme concept mockups (tests/disco_mock.c) */
        void disco_mock(lv_obj_t *root,int which);
        lv_obj_t *r=lv_obj_create(lv_screen_active()); lv_obj_remove_style_all(r); lv_obj_set_size(r,360,360);
        lv_obj_set_style_radius(r,LV_RADIUS_CIRCLE,0); lv_obj_set_style_clip_corner(r,true,0);
        disco_mock(r,atoi(argv[3]+6));
        for(int i=0;i<10;i++){lv_tick_inc(10);lv_timer_handler();}
        goto render;
    }
    if(!strcmp(argv[3],"checks")){
        fixture=1;sd_io_init(media_local);assert(sd_io_resume());screen_set_anim(0);screens_init();
        battery_arc_t battery; battery_arc_create(&battery,lv_screen_active(),328,3,236,304,0);
        int values[]={0,10,25,50,67,100,120};
        for(int i=0;i<7;i++){
            battery_arc_set(&battery,values[i],0);assert(lv_arc_get_value(battery.arc)==(values[i]>100?100:values[i]));
            lv_color_t want=values[i]<=15?TC(STATUS_DANGER):TC(TEXT_PRIMARY);
            assert(lv_color_eq(lv_obj_get_style_arc_color(battery.arc,LV_PART_INDICATOR),want));
            battery_arc_set(&battery,values[i],1);assert(lv_color_eq(lv_obj_get_style_arc_color(battery.arc,LV_PART_INDICATOR),TC(STATUS_SUCCESS)));
            assert(lv_obj_get_style_shadow_width(battery.arc,0)==0);
        }
        battery_arc_set(&battery,-1,0);assert(lv_arc_get_value(battery.arc)==100);
        screen_show(SCR_SETTINGS);setlist_open("Display");screen_show(SCR_SETLIST);
        lv_obj_t *r=screen_get_root(SCR_SETLIST);assert_no_tokens(r);assert(count_text(r,"100%")<=1);
        const char *groups[]={"Playback","Audio","Display","Network","System"};
        for(int i=0;i<5;i++){setlist_open(groups[i]);assert_no_tokens(r);}
        cfg_set_int_deferred("eq_preset",11);cfg_set_int_deferred("eq_last",11);
        assert(cfg_get_int("eq_preset",-1)==11);
        screen_show(SCR_EQ);assert(screen_current()==SCR_EQ);assert(ui_action_find("eq.edit"));
        lv_obj_send_event(ui_action_find("eq.edit"),LV_EVENT_CLICKED,NULL);assert(screen_current()==SCR_EQ_EDITOR);
        screen_back();assert(screen_current()==SCR_EQ);
        assert(cfg_get_int("eq_preset",-1)==11);
        lv_obj_send_event(ui_action_find("eq.toggle"),LV_EVENT_CLICKED,NULL);assert(cfg_get_int("eq_preset",-1)==0);
        lv_obj_send_event(ui_action_find("eq.toggle"),LV_EVENT_CLICKED,NULL);assert(cfg_get_int("eq_preset",-1)==11);
        send_result=-1;lv_obj_send_event(ui_action_find("eq.next"),LV_EVENT_CLICKED,NULL);assert(cfg_get_int("eq_preset",-1)==11);
        if(theme_preset()==THEME_PRESET_RING){        /* fork: Ring buttons - accent ring, album-colour glyph */
            lv_obj_t *edit=ui_action_find("eq.edit"), *lbl=lv_obj_get_child(edit,0);
            lv_obj_set_style_text_color(lbl,TC(STATUS_SUCCESS),0);
            screen_home();kit_accent_changed();lv_refr_now(d);
            assert(lv_color_eq(lv_obj_get_style_text_color(lbl,0),TC(STATUS_SUCCESS)));
            screen_show(SCR_EQ);
            assert(lv_color_eq(lv_obj_get_style_text_color(lbl,0),ui_media_accent()));
            assert(lv_color_eq(lv_obj_get_style_border_color(edit,0),TC(ACCENT_PRIMARY)) && lv_obj_get_style_border_width(edit,0)==2);
            puts("PASS: hidden screen styling deferred; current album colour applied on entry");
        }
        puts("PASS: battery 0..100/clamping/colour/no-glow; settings tokens; selector -> editor/back and EQ restore/failure");return 0;
    }
    if(!strncmp(argv[3],"preview-",8)){
        fixture=1;sd_io_init(media_local);assert(sd_io_resume());
        cfg_set_int_deferred("online_lyrics",0);cfg_set_int_deferred("online_art",0);
        cfg_set_int_deferred("eq_preset",11);cfg_set_int_deferred("eq_last",11);cfg_set_str("eq_name11","Warm analogue");
        if(getenv("DISCO_QS"))cfg_set_int_deferred("disco_qs",atoi(getenv("DISCO_QS")));
        if(getenv("DISCO_PROG"))cfg_set_int_deferred("disco_progress",atoi(getenv("DISCO_PROG")));
        if(getenv("DISCO_IMMSTYLE"))cfg_set_int_deferred("disco_imm",atoi(getenv("DISCO_IMMSTYLE")));
        if(getenv("DISCO_SHAPE"))cfg_set_int_deferred("disco_prog_shape",atoi(getenv("DISCO_SHAPE")));
        if(getenv("DISCO_TAL"))cfg_set_int_deferred("disco_title_al",atoi(getenv("DISCO_TAL")));
        if(getenv("DISCO_SHEEN"))cfg_set_int_deferred("disco_sheen",atoi(getenv("DISCO_SHEEN")));
        if(getenv("FONTSZ"))cfg_set_int_deferred("font_size",atoi(getenv("FONTSZ")));
        if(getenv("DISCO_MNU"))cfg_set_str_deferred("disco_mnu",getenv("DISCO_MNU"));
        screen_set_anim(0);screens_init();ui_set_accent_config(getenv("ACCENT")?1:0,getenv("ACCENT")?(int)strtol(getenv("ACCENT"),NULL,16):0);ui_set_np_style(theme_preset()==THEME_PRESET_RING?3:1);
        track_state_t st={0};snprintf(st.path,sizeof st.path,"%s",argv[5]);
        char *dot=strrchr(st.path,'.');assert(dot);strcpy(dot,".png");
        snprintf(st.title,sizeof st.title,"Northern Lights");snprintf(st.artist,sizeof st.artist,"The Midnight");snprintf(st.album,sizeof st.album,"Afterglow");
        if(getenv("NP_ARTIST"))snprintf(st.artist,sizeof st.artist,"%s",getenv("NP_ARTIST"));if(getenv("NP_ALBUM"))snprintf(st.album,sizeof st.album,"%s",getenv("NP_ALBUM"));
        int long_names=!strncmp(argv[3],"preview-long-",13);
        if(long_names){
            snprintf(st.title,sizeof st.title,"The Last Train Home Through Northern Lights");
            snprintf(st.artist,sizeof st.artist,"The Midnight & The Northern Lights Orchestra");
        }
        st.have_track=1;st.state=2;st.duration_ms=240000;st.position_ms=92000;ipc_seed_state(&st);
        if(getenv("MA_TRACK")){                      /* MA Sendspin: Music shows Music Assistant's track + its sidecar cover */
            struct timespec tn; clock_gettime(CLOCK_MONOTONIC,&tn);
            ipc_set_external("Midnight City","M83","Hurry Up, We're Dreaming",getenv("MA_TRACK"),243000,61000,tn.tv_sec*1000LL+tn.tv_nsec/1000000,1000);
            ipc_get_state(&st); st.state=2;
        }
        ui_update(&st);
        int loaded=0;for(int i=0;i<500;i++){ui_art_poll(NULL);if(ui_current_cover_dsc()){loaded=1;break;}usleep(10000);}assert(loaded);
        int art_race=!strncmp(argv[3],"preview-art-race-",17);
        if(art_race){
            track_state_t other=st;
            snprintf(other.path,sizeof other.path,"%s",race_path);
            snprintf(other.album,sizeof other.album,"Other album");
            ipc_seed_state(&other);ui_update(&other);
            for(int i=0;i<500 && !atomic_load(&race_waiting);i++)usleep(1000);
            assert(atomic_load(&race_waiting));
            ipc_seed_state(&st);ui_update(&st);
            atomic_store(&race_release,1);
            /* Wait past the obsolete failure so the final current-track state is tested. */
            for(int i=0;i<200;i++){ui_art_poll(NULL);usleep(10000);}
            loaded=ui_current_cover_dsc()!=NULL;
            assert(loaded == (getenv("DISKOS_ART_RACE_EXPECT_MISSING")==NULL));
            printf("PASS: controlled A -> B (blocked load) -> A; final artwork=%s\n",loaded?"present":"missing (baseline reproduction)");
        }
        ui_publish_art_surfaces(&st,1);
        home_set_now_playing(st.title,st.artist,ui_current_accent(),true);home_set_clock("10:09","Saturday, 3 October");
        home_set_weather("\xEF\x86\x85  18\xC2\xB0C  Clear");
        home_set_status(67,0,1,1);quicksettings_set_battery(67,0);
        quicksettings_set_now_playing(st.title,st.artist,1);
        const char *page=argv[3]+(art_race?17:(long_names?13:8));int scr=SCR_HOME;
        if(!strcmp(page,"quick"))scr=SCR_QUICK;else if(!strcmp(page,"np") || !strncmp(page,"immersive",9))scr=SCR_NOWPLAYING;else if(!strcmp(page,"eq"))scr=SCR_EQ;
        else if(!strcmp(page,"bands"))scr=SCR_EQ_EDITOR;else if(!strcmp(page,"display")){setlist_open("Display");scr=SCR_SETLIST;}else if(!strcmp(page,"network")){setlist_open("Network");scr=SCR_SETLIST;}else if(!strcmp(page,"system")){setlist_open("System");scr=SCR_SETLIST;}
        else if(!strcmp(page,"library") || !strcmp(page,"albums"))scr=SCR_LIBRARY;else if(!strcmp(page,"settings"))scr=SCR_SETTINGS;
        else if(!strcmp(page,"upnext"))scr=SCR_UPNEXT;else if(!strcmp(page,"modes"))scr=SCR_WORKMODE;
        else if(!strcmp(page,"shortcuts")){apps_reload();scr=SCR_APPS;}
        else if(!strcmp(page,"search")){if(getenv("SEARCH_RECENT")){cfg_set_str("search_r1","boris");cfg_set_str("search_r2","alpha band");cfg_set_str("search_r3","northern");}scr=SCR_SEARCH;}
        else if(!strcmp(page,"battery")){void usage_demo(int);scr=SCR_USAGE;screen_show(scr);usage_demo(getenv("CHG")!=NULL);}
        else if(!strcmp(page,"discoopts")){void settings_open_disco(void);setlist_open("Display");scr=SCR_SETLIST;screen_show(scr);settings_open_disco();}
        else if(!strncmp(page,"scr",3)){scr=atoi(page+3);}
        else if(!strcmp(page,"weather")){void weather_demo(void);scr=SCR_WEATHER;screen_show(scr);weather_demo();}
        else if(!strcmp(page,"low"))home_set_status(10,0,1,1);else if(!strcmp(page,"charging"))home_set_status(67,1,1,1);
        else if(!strncmp(page,"saver",5)){
            cfg_set_int_deferred("saver_style",5);
            saver_set_track(st.title,st.artist,NULL,st.path);
            saver_set_clock("10:09","Saturday, 3 October");
            saver_set_weather("Clear 18°");
            scr=SCR_SAVER;
        }
        screen_show(scr);
        if(!strncmp(page,"immersive",9)){
            ui_np_fsart_open();assert(ui_np_fsart_active());
            for(int i=0;i<100;i++){lyrics_poll(NULL);usleep(1000);}
        }
        for(int i=0;i<120;i++){lv_tick_inc(10);lv_timer_handler();}kit_pass(screen_get_root(scr));
        if(!strcmp(page,"albums")){                  /* Library > Albums */
            lv_obj_t *l=find_text(screen_get_root(SCR_LIBRARY),"Albums"); assert(l); lv_obj_send_event(lv_obj_get_parent(l),LV_EVENT_CLICKED,NULL);
            for(int i=0;i<60;i++){lv_tick_inc(10);lv_timer_handler();}
        }
        if(getenv("DISCO_OV")){                      /* fork: Disco - tap the title on Music: the track overlay */
            lv_obj_t *t=find_text(screen_get_root(SCR_HOME),st.title); assert(t); lv_obj_send_event(lv_obj_get_parent(t),LV_EVENT_SHORT_CLICKED,NULL);
            for(int i=0;i<20;i++){lv_tick_inc(10);lv_timer_handler();}
        }
        if(getenv("DISCO_HOLD")){                    /* fork: Disco - hold the title (Title Hold: DISCO_HOLD=0 Immersive, 1 Favourite) */
            cfg_set_int_deferred("disco_title_hold",atoi(getenv("DISCO_HOLD")));
            lv_obj_t *t=find_text(screen_get_root(SCR_HOME),st.title); assert(t); lv_obj_send_event(lv_obj_get_parent(t),LV_EVENT_LONG_PRESSED,NULL);
            for(int i=0;i<60;i++){lv_tick_inc(10);lv_timer_handler();}
        }
        if(getenv("DISCO_IMM")){ void ui_imm_style_apply(void); ui_imm_style_apply(); disco_open_np_immersive(); for(int i=0;i<60;i++){lv_tick_inc(10);lv_timer_handler();} assert(ui_np_fsart_active()); }
        if(getenv("DISCO_NAV")){                 /* DISCO_NAV=1 open; =o<ms> opening after ms; =c<ms> closing after ms */
            const char *nv=getenv("DISCO_NAV"); int ms=atoi(nv+1);
            disco_nav_set_open(1);
            if(nv[0]=='o'){ for(int i=0;i<ms/5;i++){lv_tick_inc(5);lv_timer_handler();} }
            else { for(int i=0;i<40;i++){lv_tick_inc(10);lv_timer_handler();}
                   if(nv[0]=='c'){ disco_nav_set_open(0); for(int i=0;i<ms/5;i++){lv_tick_inc(5);lv_timer_handler();} } }
        }
        if(getenv("OPTS_SCROLL")){ void settings_test_scroll(int); settings_test_scroll(atoi(getenv("OPTS_SCROLL"))); for(int i=0;i<30;i++){lv_tick_inc(10);lv_timer_handler();} }
        if(getenv("PERF")){                          /* fork: full-screen redraw cost of this page (ms per frame, host) */
            struct timespec t0,t1; int n=40; clock_gettime(CLOCK_MONOTONIC,&t0);
            for(int i=0;i<n;i++){ lv_obj_invalidate(lv_screen_active()); lv_obj_invalidate(lv_layer_top()); lv_refr_now(NULL); }
            clock_gettime(CLOCK_MONOTONIC,&t1);
            fprintf(stderr,"PERF full %.2f ms/frame\n",((t1.tv_sec-t0.tv_sec)*1e3+(t1.tv_nsec-t0.tv_nsec)/1e6)/n);
            clock_gettime(CLOCK_MONOTONIC,&t0);
            for(int i=0;i<200;i++){ lv_tick_inc(1000); lv_timer_handler(); }   /* 200 idle seconds: what the timers redraw */
            clock_gettime(CLOCK_MONOTONIC,&t1);
            fprintf(stderr,"PERF idle %.2f ms/s\n",((t1.tv_sec-t0.tv_sec)*1e3+(t1.tv_nsec-t0.tv_nsec)/1e6)/200);
        }
        if(getenv("MODES_PENDING")){ void modes_test_pending(int); modes_test_pending(atoi(getenv("MODES_PENDING"))); for(int i=0;i<25;i++){lv_tick_inc(10);lv_timer_handler();} }
        if(getenv("KB")){ int pg=atoi(getenv("KB")); kbinput_open("Password for Home", pg==9?"Sn0wsky!":"", NULL); for(int i=0;i<pg && pg<9;i++){ extern void kbinput_test_page(void); kbinput_test_page(); } for(int i=0;i<30;i++){lv_tick_inc(10);lv_timer_handler();} }
        if(getenv("MA_STATE")){ void ma_test_state(int); scr=SCR_MA; screen_show(scr); ma_test_state(atoi(getenv("MA_STATE"))); for(int i=0;i<30;i++){lv_tick_inc(10);lv_timer_handler();} }
        if(getenv("MA_DELAY")){ void settings_open_ma_delay(void); cfg_set_int("ma_delay_ms",atoi(getenv("MA_DELAY"))); setlist_open("Network"); settings_open_ma_delay(); scr=SCR_SETTING_DETAIL; for(int i=0;i<40;i++){lv_tick_inc(10);lv_timer_handler();} }
        if(getenv("VK_SET")){ void settings_open_volkeys(void); setlist_open("System"); scr=SCR_SETLIST; screen_show(scr); settings_open_volkeys(); for(int i=0;i<40;i++){lv_tick_inc(10);lv_timer_handler();} }
        if(getenv("MA_SET")){ void settings_open_ma(void); setlist_open("Network"); scr=SCR_SETLIST; screen_show(scr); settings_open_ma(); for(int i=0;i<40;i++){lv_tick_inc(10);lv_timer_handler();} }
        if(getenv("MODES_INFO")){ void modes_test_info(int); scr=SCR_MODEINFO; screen_show(scr); modes_test_info(atoi(getenv("MODES_INFO"))); for(int i=0;i<30;i++){lv_tick_inc(10);lv_timer_handler();} }
        if(getenv("BACK_HINT")){ ui_back_hint(180, atoi(getenv("BACK_HINT"))); for(int i=0;i<10;i++){lv_tick_inc(10);lv_timer_handler();} }
        if(getenv("HOME_HINT")){ ui_home_hint(180, atoi(getenv("HOME_HINT"))); for(int i=0;i<10;i++){lv_tick_inc(10);lv_timer_handler();} }
        if(getenv("DISCO_MENU")){ disco_menu_open(); for(int i=0;i<20;i++){lv_tick_inc(10);lv_timer_handler();} }
        if(getenv("NP_GO")){ if(!strcmp(getenv("NP_GO"),"artist")) ui_np_open_artist(); else ui_np_open_album(); scr=SCR_LIBRARY; for(int i=0;i<60;i++){lv_tick_inc(10);lv_timer_handler();} }
        if(getenv("DISCO_FUZZ")){ void disco_test_tap(int); srand(atoi(getenv("DISCO_FUZZ"))); for(int it=0;it<600;it++){ int r=rand()%10; if(r<2) disco_nav_set_open(1); else if(r<3) disco_nav_set_open(0); else disco_test_tap(rand()%4); int t=rand()%30; for(int i=0;i<t;i++){lv_tick_inc(10);lv_timer_handler();} } }
        if(getenv("DISCO_EQ"))cfg_set_int_deferred("disco_eq",atoi(getenv("DISCO_EQ")));
        if(getenv("DISCO_VOL"))cfg_set_int_deferred("disco_vol",atoi(getenv("DISCO_VOL")));
        if(getenv("EQSEL")){ void eqcustom_test_select(int); eqcustom_test_select(atoi(getenv("EQSEL"))); for(int i=0;i<10;i++){lv_tick_inc(10);lv_timer_handler();} }
        if(getenv("VOLSHOW")){ void ui_show_volume(int); ui_show_volume(atoi(getenv("VOLSHOW"))); for(int i=0;i<20;i++){lv_tick_inc(10);lv_timer_handler();} }
        if(getenv("TICKS")){ int ms=atoi(getenv("TICKS")); for(int i=0;i<ms/20;i++){lv_tick_inc(20);lv_timer_handler();} }
        if(!strncmp(page,"immersive",9)){
            int extra=atoi(page+9);
            for(int i=0;i<extra/10;i++){lv_tick_inc(10);lv_timer_handler();}
        }
        if(scr==SCR_SAVER){
            lv_obj_t *title=find_text(screen_get_root(scr),st.title), *artist=find_text(screen_get_root(scr),st.artist);
            assert(title && artist);
            assert(lv_label_get_long_mode(title)==LV_LABEL_LONG_SCROLL_CIRCULAR);
            assert(lv_label_get_long_mode(artist)==LV_LABEL_LONG_SCROLL_CIRCULAR);
            int extra=atoi(page+5);
            for(int i=0;i<extra/10;i++){lv_tick_inc(10);lv_timer_handler();}
        }
        goto render;
    }
    if(!strcmp(argv[3],"actions")){
        cfg_set_int_deferred("ota_allow",1);
        assert(!ota_allowed());
        ota_prog_t pg={0};
        assert(ota_stage(NULL,"1.2.0","v9.9.9",&pg)==OTA_E_OFF);
        screen_set_anim(0); screens_init();
        assert(screen_current()==SCR_HOME);
        lv_obj_t *fav=ui_action_find("home.favourites"), *up=ui_action_find("home.upnext");
        lv_obj_t *queue=ui_action_find("np.queue");
        assert(!fav && up && queue);
        for(int enabled=0; enabled<=1; enabled++){
            cfg_set_int_deferred("up_next",enabled); home_shortcuts_refresh();
            lv_obj_send_event(up,LV_EVENT_CLICKED,NULL);
            assert(screen_current()==(enabled ? SCR_UPNEXT : SCR_HOME));
            if(enabled) screen_back();
            screen_show(SCR_NOWPLAYING);
            lv_obj_send_event(queue,LV_EVENT_SHORT_CLICKED,NULL); assert(screen_current()==SCR_QUEUE);
            screen_back(); assert(screen_current()==SCR_NOWPLAYING);
            lv_obj_send_event(queue,LV_EVENT_LONG_PRESSED,NULL);
            assert(screen_current()==(enabled ? SCR_UPNEXT : SCR_QUEUE));
            /* LVGL emits CLICKED after a hold; it must not undo the chosen long-press route. */
            lv_obj_send_event(queue,LV_EVENT_CLICKED,NULL);
            assert(screen_current()==(enabled ? SCR_UPNEXT : SCR_QUEUE));
            screen_back(); screen_back(); assert(screen_current()==SCR_HOME);
        }
        puts("PASS: Home toggle/navigation and Now Playing queue/Up Next event routing");
        return 0;
    }
    lv_obj_t *root=lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(root); lv_obj_set_size(root,360,360);
    lv_obj_set_style_radius(root,LV_RADIUS_CIRCLE,0); lv_obj_set_style_clip_corner(root,true,0);
    if(!strcmp(argv[3],"home")){
        home_create(root); home_set_clock("10:09","Saturday, 3 October");
        home_set_now_playing("Long title to check the round panel", "Artist", TC(ACCENT_PRIMARY), false);
        home_set_status(67,0,1,1);
        lv_obj_update_layout(root);
        lv_obj_t *fav=ui_action_find("home.favourites"), *up=ui_action_find("home.upnext");
        assert(!fav && up); assert_round_target(up);
        int enabled=cfg_get_int("up_next",0);
        assert(lv_obj_has_flag(up,LV_OBJ_FLAG_HIDDEN)==!enabled);
        cfg_set_int_deferred("up_next",!enabled); home_shortcuts_refresh();
        assert(lv_obj_has_flag(up,LV_OBJ_FLAG_HIDDEN)==enabled);
        cfg_set_int_deferred("up_next",enabled); home_shortcuts_refresh();
    } else if(!strcmp(argv[3],"np") || !strcmp(argv[3],"immersive")){
        ui_create(root);
        assert(ui_action_find("np.queue")); assert(ui_action_find("np.queue.long"));
    } else return 2;
    if(!strcmp(argv[3],"immersive")){
        sd_io_init(media_local); assert(sd_io_resume());
        cfg_set_int_deferred("online_lyrics",0);
        cfg_set_int_deferred("online_art",0);
        cfg_set_int_deferred("accent_mode",1);
        ui_set_np_style(2);
        track_state_t st={0};
        snprintf(st.path,sizeof st.path,"%s",argv[5]);
        char *dot=strrchr(st.path,'.'); assert(dot); strcpy(dot,".png");
        snprintf(st.title,sizeof st.title,"Immersive cover test");
        snprintf(st.artist,sizeof st.artist,"Artist");
        st.have_track=1; st.state=1; st.duration_ms=180000; st.position_ms=1500;
        ipc_seed_state(&st);
        ui_take_art_applied(); ui_update(&st);
        int loaded=0;
        for(int i=0;i<500;i++){
            ui_art_poll(NULL);
            if(ui_current_cover_dsc()){ loaded=1; break; }
            usleep(10000);
        }
        assert(loaded);
        ui_np_fsart_open(); assert(ui_np_fsart_active());
        for(int i=0;i<80;i++){
            lv_tick_inc(10); lv_timer_handler(); lyrics_poll(NULL); usleep(1000);
        }
        assert(find_sharp_image(root));
        lyr_line_t lines[8]; unsigned rev;
        assert(lyrics_immersive_load(lines,8,&rev)==2);
        assert(!strcmp(lines[0].text,"First lyric"));
        ui_np_fsart_close(); assert(!ui_np_fsart_active());
        ui_np_fsart_open(); assert(ui_np_fsart_active());
        for(int i=0;i<30;i++){ lv_tick_inc(10); lv_timer_handler(); }
        puts("PASS: real bounded decoder feeds 364px immersive image and async timed lyrics; close/reopen works");
    }
render:
    lv_refr_now(d);
    FILE *f=fopen(argv[5],"wb"); if(!f) return 1;
    fprintf(f,"P6\n360 360\n255\n");
    for(size_t i=0;i<sizeof pixels;i+=3){
        unsigned char rgb[3]={pixels[i+2],pixels[i+1],pixels[i]}; fwrite(rgb,1,3,f);
    }
    if(fclose(f)) return 1;
    printf("PASS: %s preset=%s variant=%s up_next=%s\n",argv[3],argv[1],argv[2],argv[4]);
    return 0;
}
#include "disco_mock.c"
