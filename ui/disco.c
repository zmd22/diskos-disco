#include "simple_theme.h"
#include "null_theme.h"
#include "onboarding.h"
#include "quicksettings.h"
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The Disco theme (fork): the Ring interface on the album cover.
 *   - Backdrop: every screen except Music, Now Playing, the saver and Quick Settings shows the blurred cover, darkened
 *     (dark) or lightened (light). The cover decoder already makes the blurred 360 px picture; it is tinted ONCE per
 *     album into one RAM buffer here and set as each root's background image (a plain blit, no live blur), so a
 *     sliding screen carries its own copy and nothing shows through.
 *   - Glass: on entering a screen, opaque cards and rows in the surface colours become translucent.
 *   - Sheen: a faint CD rainbow on the rim, Music and Playlist only; one fixed overlay, never redrawn per frame.
 *   - Menu picker: the black disc bottom-left. It shows the current section; tap its top / bottom (or swipe) for the
 *     previous / next section, tap its middle for the section's top screen, hold it for Music. The sections and their
 *     order come from cfg "disco_menu" (Settings > Display > Disco Menu); Music and Settings can't be removed.
 *     Hidden on full-screen tools (EQ, Search, keyboard, Quick Settings, saver, Now Playing...).
 * Music itself (the sharp cover, clock, transport) is disco_home.c. */
#include "screens.h"
#include "discshelf.h"
#include "orbit.h"
#include "theme.h"
#include "theme_kit.h"
#include "config.h"
#include "menu_apps.h"
#include "ipc.h"
int search_landing_active(void);
#include "folderbrowser.h"
#include "books.h"
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <unistd.h>

extern const lv_font_t font_theme_20;

/* ---- the menu items ------------------------------------------------------------------------------- */
typedef struct { const char *id, *name, *icon; int screen; void (*open)(void); int prot; } dsec_t;
static void open_books(void){ books_open(); }
static void open_folders(void){ folderbrowser_open(); }
#define IC_MIC "\xEF\x84\xB0"                     /* f130: Artists (the icon font) */
static void open_artists(void){ library_open_artists(); screen_section(SCR_LIBRARY, +1); }
static void open_songs(void){ library_open_songs(); screen_section(SCR_LIBRARY, +1); }
static void open_queue(void){ queue_open(); }
#define IC_ROULETTE "@vinyl"
#define IC_BOOM "@boom"
#define IC_STATS "@stats"
#define IC_APPS "@apps"
#define IC_SPHERE "@sphere"
/* Theme-coloured vector icons (no new font glyph): Album Roulette's vinyl (three rings) and Boomshine's burst (a
 * filled dot inside a ring, with three small dots flying off). */
static void stats_icon(lv_obj_t *label, int size){                 /* Listening Stats: four rising bars */
    if(lv_obj_get_child_count(label) && (intptr_t)lv_obj_get_user_data(label) == 3){   /* already drawn: clear the "@..." */
        lv_label_set_text(label, ""); lv_obj_set_size(label, size, size); return; }      /* name the caller just set as text */
    lv_obj_clean(label); lv_obj_set_user_data(label, (void *)(intptr_t)3);
    lv_label_set_text(label, ""); lv_obj_set_size(label, size, size);
    lv_color_t color = lv_obj_get_style_text_color(label, 0);
    static const signed char H[4] = { 40, 70, 55, 100 };          /* % of size */
    int bw = size / 6 > 2 ? size / 6 : 2, gap = (size - 4 * bw) / 5;
    for(int i = 0; i < 4; i++){
        lv_obj_t *b = lv_obj_create(label); lv_obj_remove_style_all(b);
        int h = H[i] * size * 85 / 10000; if(h < 3) h = 3;
        lv_obj_set_size(b, bw, h); lv_obj_set_pos(b, gap + i * (bw + gap), size - size / 10 - h);
        lv_obj_set_style_radius(b, bw / 3, 0); lv_obj_set_style_bg_color(b, color, 0); lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_clear_flag(b, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    }
}
static void apps_icon(lv_obj_t *label, int size){                  /* Apps: four rounded tiles */
    if(lv_obj_get_child_count(label) && (intptr_t)lv_obj_get_user_data(label) == 4){   /* already drawn: clear the "@..." */
        lv_label_set_text(label, ""); lv_obj_set_size(label, size, size); return; }      /* name the caller just set as text */
    lv_obj_clean(label); lv_obj_set_user_data(label, (void *)(intptr_t)4);
    lv_label_set_text(label, ""); lv_obj_set_size(label, size, size);
    lv_color_t color = lv_obj_get_style_text_color(label, 0);
    int g = size / 7 > 2 ? size / 7 : 2, t = (size - g) / 2;
    for(int i = 0; i < 4; i++){
        lv_obj_t *b = lv_obj_create(label); lv_obj_remove_style_all(b);
        lv_obj_set_size(b, t, t); lv_obj_set_pos(b, (i & 1) * (t + g), (i >> 1) * (t + g));
        lv_obj_set_style_radius(b, t / 3, 0); lv_obj_set_style_bg_color(b, color, 0);
        lv_obj_set_style_bg_opa(b, i == 3 ? LV_OPA_50 : LV_OPA_COVER, 0);
        lv_obj_clear_flag(b, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    }
}
static void sphere_icon(lv_obj_t *label, int size){                /* SongSphere: a ball with its marker (a dot in a ring) */
    if(lv_obj_get_child_count(label) && (intptr_t)lv_obj_get_user_data(label) == 5){   /* already drawn: clear the "@..." */
        lv_label_set_text(label, ""); lv_obj_set_size(label, size, size); return; }      /* name the caller just set as text */
    lv_obj_clean(label); lv_obj_set_user_data(label, (void *)(intptr_t)5);
    lv_label_set_text(label, ""); lv_obj_set_size(label, size, size);
    lv_color_t color = lv_obj_get_style_text_color(label, 0);
    for(int i = 0; i < 3; i++){
        lv_obj_t *o = lv_obj_create(label); lv_obj_remove_style_all(o);
        lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
        if(i == 0){ lv_obj_set_size(o, size, size); lv_obj_center(o); lv_obj_set_style_border_width(o, size >= 24 ? 2 : 1, 0); lv_obj_set_style_border_color(o, color, 0);
                    lv_obj_set_style_bg_color(o, color, 0); lv_obj_set_style_bg_opa(o, LV_OPA_20, 0); }
        else { int d = i == 1 ? size * 3 / 8 : size / 6; if(d < 3) d = 3; lv_obj_set_size(o, d, d);
               lv_obj_align(o, LV_ALIGN_CENTER, size / 6, -size / 6);
               if(i == 1){ lv_obj_set_style_border_width(o, size >= 24 ? 2 : 1, 0); lv_obj_set_style_border_color(o, color, 0); }
               else { lv_obj_set_style_bg_color(o, color, 0); lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0); } }
    }
}
/* A jewel case with a visible spine and CD: shared by Apps and navigation. */
static void shelf_icon(lv_obj_t *label,int size){
    if((intptr_t)lv_obj_get_user_data(label)!=6){lv_obj_clean(label);lv_obj_set_user_data(label,(void*)(intptr_t)6);}
    lv_label_set_text(label,"");lv_obj_set_size(label,size,size);
    lv_color_t color=lv_obj_get_style_text_color(label,0);
    for(int i=0;i<4;i++){
        lv_obj_t *o=lv_obj_get_child(label,i);
        if(!o){o=lv_obj_create(label);lv_obj_remove_style_all(o);lv_obj_remove_flag(o,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);}
        int border=size>=28?2:1;
        lv_obj_set_style_border_color(o,color,0);lv_obj_set_style_border_width(o,border,0);
        lv_obj_set_style_radius(o,i>=2?LV_RADIUS_CIRCLE:1,0);
        if(i==0){lv_obj_set_size(o,size,size*9/10);lv_obj_center(o);}
        if(i==1){lv_obj_set_size(o,size/6,size*9/10);lv_obj_align(o,LV_ALIGN_LEFT_MID,0,0);}
        if(i==2){lv_obj_set_size(o,size*2/3,size*2/3);lv_obj_align(o,LV_ALIGN_CENTER,size/12,0);}
        if(i==3){int d=size/6;if(d<3)d=3;lv_obj_set_size(o,d,d);lv_obj_align(o,LV_ALIGN_CENTER,size/12,0);}
    }
}
/* A microphone: a rounded head, the U of its holder, a short stem and a foot (3.2.0 track menus: artist) */
static void mic_icon(lv_obj_t *label, int size){
    if((intptr_t)lv_obj_get_user_data(label) != 7){ lv_obj_clean(label); lv_obj_set_user_data(label, (void *)(intptr_t)7); }
    lv_label_set_text(label, ""); lv_obj_set_size(label, size, size);
    lv_color_t color = lv_obj_get_style_text_color(label, 0);
    int bw = size >= 24 ? 2 : 1;
    for(int i = 0; i < 4; i++){
        lv_obj_t *o = lv_obj_get_child(label, i);
        if(!o){ o = i == 1 ? lv_arc_create(label) : lv_obj_create(label);
                if(i == 1){ lv_obj_remove_style(o, NULL, LV_PART_KNOB); lv_obj_remove_style(o, NULL, LV_PART_MAIN); }
                else lv_obj_remove_style_all(o);
                lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE); }
        if(i == 0){   /* the head: filled capsule */
            lv_obj_set_size(o, size * 2 / 5, size * 3 / 5); lv_obj_align(o, LV_ALIGN_TOP_MID, 0, 0);
            lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0); lv_obj_set_style_bg_color(o, color, 0); lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
        } else if(i == 1){   /* the U holder: the lower half of a ring */
            lv_obj_set_size(o, size * 2 / 3, size * 2 / 3); lv_obj_align(o, LV_ALIGN_TOP_MID, 0, size / 8);
            lv_arc_set_bg_angles(o, 0, 180); lv_arc_set_value(o, 0);
            lv_obj_set_style_arc_width(o, bw, LV_PART_MAIN); lv_obj_set_style_arc_color(o, color, LV_PART_MAIN);
            lv_obj_set_style_arc_opa(o, LV_OPA_COVER, LV_PART_MAIN); lv_obj_set_style_arc_opa(o, LV_OPA_TRANSP, LV_PART_INDICATOR);
        } else if(i == 2){   /* the stem */
            lv_obj_set_size(o, bw, size / 6); lv_obj_align(o, LV_ALIGN_TOP_MID, 0, size * 19 / 24);
            lv_obj_set_style_bg_color(o, color, 0); lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
        } else {             /* the foot */
            lv_obj_set_size(o, size * 2 / 5, bw); lv_obj_align(o, LV_ALIGN_TOP_MID, 0, size * 23 / 24 - bw);
            lv_obj_set_style_bg_color(o, color, 0); lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0); lv_obj_set_style_radius(o, 1, 0);
        }
    }
}
static void roulette_icon(lv_obj_t *label, const char *icon, int size){
    if(!strcmp(icon,"@shelf")){shelf_icon(label,size);return;}
    if(!strcmp(icon,"@mic")){mic_icon(label,size);return;}
    if(!strcmp(icon, IC_STATS)){ stats_icon(label, size); return; }
    if(!strcmp(icon, IC_SPHERE)){ sphere_icon(label, size); return; }
    if(!strcmp(icon, IC_APPS)){ apps_icon(label, size); return; }
    int vinyl = !strcmp(icon, IC_ROULETTE), boom = !strcmp(icon, IC_BOOM);
    if(lv_obj_get_child_count(label) && (!(vinyl || boom) || (intptr_t)lv_obj_get_user_data(label) != (boom ? 2 : 1))) lv_obj_clean(label);
    if(!vinyl && !boom){
        lv_obj_set_size(label, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        return;
    }
    lv_obj_set_user_data(label, (void *)(intptr_t)(boom ? 2 : 1));
    lv_label_set_text(label, "");
    lv_obj_set_size(label, size, size);
    lv_color_t color = lv_obj_get_style_text_color(label, 0);
    int n = boom ? 5 : 3;
    for(int i = 0; i < n; i++){
        lv_obj_t *ring = lv_obj_get_child(label, i);
        if(!ring){ ring = lv_obj_create(label); lv_obj_remove_style_all(ring);
            lv_obj_clear_flag(ring, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE); }
        lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
        if(!boom){
            int d = i == 0 ? size : i == 1 ? size * 3 / 4 : size / 4;
            lv_obj_set_size(ring, d, d); lv_obj_center(ring);
            lv_obj_set_style_border_width(ring, i == 0 && size >= 24 ? 2 : 1, 0);
            lv_obj_set_style_border_color(ring, color, 0);
            continue;
        }
        if(i == 0){                                               /* the burst: a ring */
            int d = size * 3 / 4; lv_obj_set_size(ring, d, d); lv_obj_align(ring, LV_ALIGN_CENTER, -size / 10, size / 10);
            lv_obj_set_style_border_width(ring, size >= 24 ? 2 : 1, 0); lv_obj_set_style_border_color(ring, color, 0);
        } else if(i == 1){                                        /* its glowing core */
            int d = size * 3 / 8; lv_obj_set_size(ring, d, d); lv_obj_align(ring, LV_ALIGN_CENTER, -size / 10, size / 10);
            lv_obj_set_style_bg_color(ring, color, 0); lv_obj_set_style_bg_opa(ring, LV_OPA_COVER, 0);
        } else {                                                  /* dots caught up in it */
            static const signed char POS[3][2] = { { 42, -40 }, { 18, -48 }, { 46, -14 } };   /* % of size from centre */
            int d = size >= 24 ? size / 6 : 3; lv_obj_set_size(ring, d, d);
            lv_obj_align(ring, LV_ALIGN_CENTER, POS[i - 2][0] * size / 100, POS[i - 2][1] * size / 100);
            lv_obj_set_style_bg_color(ring, color, 0); lv_obj_set_style_bg_opa(ring, LV_OPA_COVER, 0);
        }
    }
}
/* Boomshine is built into mq_ui: the menu opens its landing page; Play starts it (it owns the screen until you Exit) */
static void open_boomshine(void){
    boomshine_landing();
}
static void open_albums(void){void library_open_pivot(int);library_open_pivot(1);}
static const dsec_t SEC[] = {
    { "music",     "Music",     LV_SYMBOL_AUDIO,     SCR_HOME,      NULL,              1 },
    { "library",   "Library",   LV_SYMBOL_LIST,      SCR_LIBRARY,   NULL,              0 },
    { "settings",  "Settings",  LV_SYMBOL_SETTINGS,  SCR_SETTINGS,  NULL,              1 },
    { "modes",     "Modes",     LV_SYMBOL_USB,       SCR_WORKMODE,  NULL,              0 },
    { "shortcuts", "Shortcuts", LV_SYMBOL_BARS,      SCR_APPS,      NULL,              0 },
    { "eq",        "EQ",        "EQ",                SCR_EQ,        NULL,              0 },
    { "search",    "Search",    TH_IC_SEARCH,        SCR_SEARCH,    NULL,              0 },
    { "albums",    "Albums",    LV_SYMBOL_IMAGE,     SCR_ALBUMWALL, open_albums,       0 },
    { "queue",     "Up Next",   LV_SYMBOL_NEXT,      SCR_UPNEXT,    NULL,              0 },
    { "folders",   "Folders",   LV_SYMBOL_DIRECTORY, SCR_FOLDER,    open_folders,      0 },
    { "books",     "Books",     LV_SYMBOL_FILE,      SCR_BOOKS,     open_books,        0 },
    { "artists",   "Artists",   IC_MIC,              SCR_LIBRARY,   open_artists,      0 },
    { "songs",     "Songs",     LV_SYMBOL_PLAY,      SCR_LIBRARY,   open_songs,        0 },
    { "myqueue",   "Queue",     LV_SYMBOL_PASTE,     SCR_QUEUE,     open_queue,        0 },
    { "weather",   "Weather",   LV_SYMBOL_IMAGE,     SCR_WEATHER,   weather_app_open,  0 },
    { "ma",        "Sendspin",  "MA",               SCR_MA,       NULL,              0 },
    { "roulette",  "Album Roulette", IC_ROULETTE,    SCR_ROULETTE, NULL,              0 },
    { "boomshine", "Boomshine", IC_BOOM,            SCR_APPLAND,   open_boomshine,    0 },
    { "stats",     "Stats",     IC_STATS,           SCR_APPLAND,   stats_landing,     0 },
    { "apps",      "Apps",      IC_APPS,            SCR_APPSHELF,  appshelf_open,     0 },
    { "songsphere","SongSphere", IC_SPHERE,         SCR_APPLAND,   sphere_ui_landing,    0 },
};
#define NSEC ((int)(sizeof SEC / sizeof SEC[0]))
#define MAXM 10                                    /* the picker holds at most ten (the dots must fit) */
static int g_menu[MAXM], g_nmenu, g_cur;           /* active items (indices into SEC), the current one */

static dsec_t menu_section(int id){
    if(id>=0&&id<NSEC){dsec_t s=SEC[id];if(!strcmp(s.id,"albums")&&simple_cfg_get_int("album_view",0)!=2)s.screen=SCR_LIBRARY;return s;}
    const menu_app_t *app=menu_apps_get(id-NSEC);
    return (dsec_t){app?app->id:"",app?app->name:"App unavailable",app&&!strcmp(app->exec,APP_BUILTIN_SHELF)?"@shelf":LV_SYMBOL_FILE,SCR_APPLAND,NULL,0};
}
static int sec_find(const char *id, size_t n){ for(int i = 0; i < NSEC; i++) if(strlen(SEC[i].id) == n && !strncmp(SEC[i].id, id, n)) return i; return -1; }
static int menu_has_screen(int scr);
static int in_menu(int s){ for(int i = 0; i < g_nmenu; i++) if(g_menu[i] == s) return i; return -1; }
static int menu_has_screen(int scr){ for(int i = 0; i < g_nmenu; i++) if(menu_section(g_menu[i]).screen == scr) return 1; return 0; }
/* parse cfg "disco_menu": unknown and duplicate ids are skipped, missing protected ones come back (never locked out) */
static const char CODE[] = "mlsoceFaudbwrgqMRBSAP";                /* one letter per SEC[] entry, same order (digits: apps) */
static void menu_load(void){
    menu_apps_scan();
    char buf[256]; snprintf(buf, sizeof buf, "%s", simple_cfg_get_str(th_null()?"null_menu":"disco_menu", "music,library,settings,modes,shortcuts,eq"));
    g_nmenu = 0;
    const char *codes = simple_cfg_get_str(th_null()?"null_mnu":"disco_mnu", "");
    if(codes && codes[0]){                                        /* the compact form (since 1.2.5) */
        for(const char *p = codes; *p && g_nmenu < MAXM; p++){ int s = menu_apps_decode_named(*p,CODE,th_null()?"nm_app":"dm_app");
            if(s >= 0 && in_menu(s) < 0) g_menu[g_nmenu++] = s; }
        buf[0] = 0;
    }
    for(const char *p = buf; *p && g_nmenu < MAXM; ){
        const char *e = strchr(p, ','); size_t n = e ? (size_t)(e - p) : strlen(p);
        int s = sec_find(p, n);
        if(s >= 0 && in_menu(s) < 0) g_menu[g_nmenu++] = s;
        p += n; if(*p == ',') p++;
    }
    for(int s = 0; s < NSEC; s++) if(SEC[s].prot && in_menu(s) < 0){
        if(g_nmenu >= MAXM) g_nmenu = MAXM - 1;
        if(s == 0){ memmove(g_menu + 1, g_menu, sizeof(int) * (size_t)g_nmenu); g_menu[0] = s; g_nmenu++; }   /* Music first */
        else g_menu[g_nmenu++] = s;
    }
    int mi = in_menu(0);                                           /* Music always first: the top of the dots */
    if(mi > 0){ memmove(g_menu + 1, g_menu, sizeof(int) * (size_t)mi); g_menu[0] = 0; }
    if(g_cur >= g_nmenu) g_cur = 0;
}

/* ---- backdrop: the blurred cover, tinted once per album ------------------------------------------- */
#define BDW 360
static uint8_t g_bd[BDW * BDW * 4];
static lv_image_dsc_t g_bd_dsc;
static int g_bd_ok;
static lv_obj_t *g_parent;
static int no_backdrop(int which){
    return which == SCR_HOME || which == SCR_NOWPLAYING || which == SCR_SAVER || which == SCR_QUICK || which == SCR_SPHERE;   /* SongSphere: its ball is drawn on the plain canvas */
}
static void backdrop_apply(void){
    for(int i = 0; i < SCR_COUNT; i++){
        lv_obj_t *r = screen_get_root(i);
        if(!r || no_backdrop(i)) continue;
        lv_obj_set_style_bg_image_src(r, NULL, 0);                    /* same buffer, new pixels: force a redraw */
        if(g_bd_ok) lv_obj_set_style_bg_image_src(r, &g_bd_dsc, 0);
    }
}
static unsigned g_baked_revision;
static uint32_t g_baked_canvas;
static int g_baked_variant, g_baked_idle, g_baked_seen;
extern unsigned ui_art_revision(void);
void disco_backdrop_prepared(const uint8_t *pixels, uint32_t canvas, int variant){
    if(!th_disco_family() || canvas != theme_rgb(THEME_CLR_CANVAS) || variant != theme_variant() || dhome_idle()) return;
    memcpy(g_bd, pixels, sizeof g_bd);
    memset(&g_bd_dsc, 0, sizeof g_bd_dsc);
    g_bd_dsc.header.magic = LV_IMAGE_HEADER_MAGIC; g_bd_dsc.header.cf = LV_COLOR_FORMAT_XRGB8888;
    g_bd_dsc.header.w = g_bd_dsc.header.h = BDW; g_bd_dsc.header.stride = BDW * 4;
    g_bd_dsc.data = g_bd; g_bd_dsc.data_size = sizeof g_bd; g_bd_ok = 1;
    g_baked_revision = ui_art_revision(); g_baked_canvas = canvas;
    g_baked_variant = variant; g_baked_idle = 0; g_baked_seen = 1;
}
static void backdrop_bake(void){
    unsigned revision = ui_art_revision(); uint32_t canvas = theme_rgb(THEME_CLR_CANVAS);
    int variant = theme_variant(), idle = dhome_idle();
    if(g_baked_seen && revision == g_baked_revision && canvas == g_baked_canvas &&
       variant == g_baked_variant && idle == g_baked_idle) return;
    g_baked_seen = 1; g_baked_revision = revision; g_baked_canvas = canvas;
    g_baked_variant = variant; g_baked_idle = idle;
    const lv_image_dsc_t *src = (const lv_image_dsc_t *)ui_current_backdrop_img();
    g_bd_ok = 0;
    if(dhome_idle()) src = (const lv_image_dsc_t *)disco_idle_art();   /* nothing playing: the CD */
    if(!src || (uintptr_t)src < 4096 || src->header.magic != LV_IMAGE_HEADER_MAGIC) return;   /* a path, not a RAM picture */
    if(src->header.cf != LV_COLOR_FORMAT_XRGB8888 || src->header.w != BDW || src->header.h != BDW || !src->data) return;
    uint32_t c = theme_rgb(THEME_CLR_CANVAS);
    int cb = (int)(c & 0xFF), cg = (int)((c >> 8) & 0xFF), cr = (int)((c >> 16) & 0xFF);
    int light = theme_variant() == 1;
    int keep = light ? 84 : 112;                                      /* of 256: how much of the cover shows */
    const uint8_t *s = src->data;
    for(int i = 0; i < BDW * BDW; i++){
        const uint8_t *p = s + (size_t)i * 4; uint8_t *o = g_bd + (size_t)i * 4;
        o[0] = (uint8_t)((p[0] * keep + cb * (256 - keep)) >> 8);
        o[1] = (uint8_t)((p[1] * keep + cg * (256 - keep)) >> 8);
        o[2] = (uint8_t)((p[2] * keep + cr * (256 - keep)) >> 8);
        o[3] = 255;
    }
    memset(&g_bd_dsc, 0, sizeof g_bd_dsc);
    g_bd_dsc.header.magic = LV_IMAGE_HEADER_MAGIC; g_bd_dsc.header.cf = LV_COLOR_FORMAT_XRGB8888;
    g_bd_dsc.header.w = BDW; g_bd_dsc.header.h = BDW; g_bd_dsc.header.stride = BDW * 4;
    g_bd_dsc.data = g_bd; g_bd_dsc.data_size = sizeof g_bd;
    g_bd_ok = 1;
}

/* ---- long-press menus (song / album / file / folder holds): Disco's own look - the tinted cover backdrop, glass
 * buttons with a fine light ring (as on Music), icons in the accent, a glass Cancel hub. Called once a menu is built. */
void disco_menu_glass(orbit_t *o, lv_obj_t *ov){
    if(!th_disco_family() || !o || !ov) return;
    if(g_bd_ok){ lv_obj_set_style_bg_image_src(ov, &g_bd_dsc, 0); lv_obj_set_style_bg_opa(ov, LV_OPA_COVER, 0); }
    lv_color_t acc = ui_current_accent();
    for(int i = 0; i < o->n; i++){
        lv_obj_t *b = o->btn[i]; if(!b) continue;
        if(lv_color_eq(lv_obj_get_style_bg_color(b, 0), acc)) continue;          /* an "on" button (Favourite) keeps its fill */
        lv_obj_set_style_bg_color(b, TC(SURFACE), 0); lv_obj_set_style_bg_opa(b, theme_variant() ? 170 : 150, 0);
        lv_obj_set_style_bg_color(b, TC(SURFACE_RAISED), LV_STATE_PRESSED);
        if(o->icon[i]) lv_obj_set_style_text_color(o->icon[i], acc, 0);
        if(o->cap[i]) lv_obj_set_style_text_color(o->cap[i], TC(TEXT_PRIMARY), 0);
    }
    if(o->hub){ lv_obj_set_style_bg_color(o->hub, TC(SURFACE), 0); lv_obj_set_style_bg_opa(o->hub, theme_variant() ? 190 : 170, 0);
                lv_obj_set_style_border_width(o->hub, 1, 0); lv_obj_set_style_border_color(o->hub, TC(TEXT_PRIMARY), 0); lv_obj_set_style_border_opa(o->hub, 60, 0); }
    if(o->ring) lv_obj_add_flag(o->ring, LV_OBJ_FLAG_HIDDEN);                    /* the hub's own ring says it */
}
/* ---- shading: Disco's buttons get a little depth - lighter at the top, a fine light edge along the top and a soft
 * shadow under them - so they stand off the artwork behind. Pressed, the shading flattens. */
void disco_button_shade(lv_obj_t *b){
    if(!b || !th_disco()) return;
    lv_color_t bg = lv_obj_get_style_bg_color(b, 0);
    lv_obj_set_style_bg_grad_color(b, bg, 0);
    lv_obj_set_style_bg_color(b, theme_color_mix(TC(FIXED_MEDIA_WHITE), bg, 34), 0);     /* lighter at the top */
    lv_obj_set_style_bg_grad_dir(b, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_grad_dir(b, LV_GRAD_DIR_NONE, LV_STATE_PRESSED);
    lv_obj_set_style_border_side(b, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_width(b, 1, 0); lv_obj_set_style_border_color(b, TC(FIXED_MEDIA_WHITE), 0); lv_obj_set_style_border_opa(b, 46, 0);
    lv_obj_set_style_shadow_width(b, 10, 0); lv_obj_set_style_shadow_offset_y(b, 3, 0); lv_obj_set_style_shadow_spread(b, 0, 0);
    lv_obj_set_style_shadow_color(b, TC(FIXED_MEDIA_BLACK), 0); lv_obj_set_style_shadow_opa(b, 90, 0);
    lv_obj_set_style_shadow_opa(b, 30, LV_STATE_PRESSED);
}
/* ---- glass: opaque surface-coloured cards become translucent (once per screen entry) -------------- */
static void glass_walk(lv_obj_t *o, uint32_t s1, uint32_t s2, lv_opa_t opa, int depth){
    uint32_t n = lv_obj_get_child_count(o);
    for(uint32_t i = 0; i < n; i++){
        lv_obj_t *c = lv_obj_get_child(o, i);
        if(lv_obj_has_flag(c, LV_OBJ_FLAG_HIDDEN)) continue;
        lv_opa_t co = lv_obj_get_style_bg_opa(c, 0);
        if(co > 0 && lv_obj_get_width(c) < 340){
            uint32_t bg = lv_color_to_u32(lv_obj_get_style_bg_color(c, 0)) & 0xFFFFFF;
            if(bg == s1 || bg == s2){
                if(co == LV_OPA_COVER) lv_obj_set_style_bg_opa(c, opa, 0);
                /* Disco's rounded look for every row / pill-sized glass box still drawn with square-ish corners
                 * (playlist picker, Shortcuts setup, Date & Time boxes, other screens' cards) */
                int32_t w = lv_obj_get_width(c), h = lv_obj_get_height(c), r = lv_obj_get_style_radius(c, 0);
                if(h >= 26 && h <= 72 && w >= 60 && r < h / 2) lv_obj_set_style_radius(c, LV_RADIUS_CIRCLE, 0);
                if(lv_obj_check_type(c, &lv_button_class) && h >= 26 && h <= 72 && !lv_obj_has_flag(c, LV_OBJ_FLAG_USER_1)) disco_button_shade(c);   /* buttons, not list rows */
                else if(h > 72 && w >= 40 && r < 18) lv_obj_set_style_radius(c, 22, 0);
            }
        }
        if(depth < 6) glass_walk(c, s1, s2, opa, depth + 1);
    }
}

/* ---- sheen: a faint CD rainbow on the rim ---------------------------------------------------------- */
static lv_obj_t *g_sheen;
static int g_sheen_scr = -1;
void disco_sheen_apply(void){                                          /* Settings > Display > Rim Sheen */
    if(!g_sheen) return;
    if((g_sheen_scr == SCR_HOME || g_sheen_scr == SCR_PLVIEW) && simple_cfg_get_int("disco_sheen", 1)){ lv_obj_remove_flag(g_sheen, LV_OBJ_FLAG_HIDDEN); lv_obj_move_foreground(g_sheen); }
    else lv_obj_add_flag(g_sheen, LV_OBJ_FLAG_HIDDEN);
}
/* The sheen is drawn ONCE into eight small images (one per 45-degree slice of the rim, each just the size of its
 * slice), not as 48 arc objects: a redraw anywhere on Music then only blends the few slices that touch it. */
#define SH_R1 180
#define SH_R0 170
static lv_image_dsc_t g_sh_dsc[8];
static void sheen_slice(int k, int *x0, int *y0, int *w, int *h){
    int xmin = 360, ymin = 360, xmax = -1, ymax = -1;
    for(int d = 0; d <= 45; d += 3) for(int r = SH_R0; r <= SH_R1; r += SH_R1 - SH_R0){
        float a = (k * 45 + d) * 0.0174533f; int x = 180 + (int)lroundf(r * cosf(a)), y = 180 + (int)lroundf(r * sinf(a));
        if(x < xmin) xmin = x; if(x > xmax) xmax = x; if(y < ymin) ymin = y; if(y > ymax) ymax = y;
    }
    xmin -= 2; ymin -= 2; xmax += 2; ymax += 2;
    if(xmin < 0) xmin = 0; if(ymin < 0) ymin = 0; if(xmax > 359) xmax = 359; if(ymax > 359) ymax = 359;
    *x0 = xmin; *y0 = ymin; *w = xmax - xmin + 1; *h = ymax - ymin + 1;
}
static void sheen_create(void){
    static const uint32_t IRI[8] = { 0xFF5AA0, 0xFFB45A, 0xFFF06A, 0x62F28C, 0x4AD8FF, 0x6A8CFF, 0xB36BFF, 0xFF5AA0 };
    g_sheen = lv_obj_create(g_parent);
    lv_obj_remove_style_all(g_sheen);
    lv_obj_set_size(g_sheen, 360, 360);
    lv_obj_clear_flag(g_sheen, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    for(int k = 0; k < 8; k++){
        int x0, y0, w, h; sheen_slice(k, &x0, &y0, &w, &h);
        uint32_t *px = calloc((size_t)w * h, 4); if(!px) continue;
        for(int y = 0; y < h; y++) for(int x = 0; x < w; x++){
            float dx = x0 + x + 0.5f - 180, dy = y0 + y + 0.5f - 180, r = sqrtf(dx * dx + dy * dy);
            if(r < SH_R0 - 1 || r > SH_R1 + 1) continue;
            float deg = atan2f(dy, dx) * 57.29578f; if(deg < 0) deg += 360;
            if(deg < k * 45 || deg >= k * 45 + 45) continue;                 /* each pixel belongs to exactly one slice */
            int outer = r >= (SH_R0 + SH_R1) / 2.0f;                          /* outer band 60, inner band 26 (offset 20 deg) */
            float d2 = outer ? deg : deg - 20; if(d2 < 0) d2 += 360;
            int seg = (int)(d2 / 15) % 24, band = !outer;
            uint32_t c = IRI[(seg + band) % 8];
            float lo = outer ? (SH_R0 + SH_R1) / 2.0f : SH_R0, hi = outer ? SH_R1 : (SH_R0 + SH_R1) / 2.0f;
            float cov = fminf(fminf(r - lo + 0.5f, hi - r + 0.5f), 1.0f); if(cov <= 0) continue;
            uint32_t aa = (uint32_t)((outer ? 60 : 26) * cov);
            px[y * w + x] = aa << 24 | c;
        }
        lv_image_dsc_t *d = &g_sh_dsc[k];
        memset(d, 0, sizeof *d);
        d->header.magic = LV_IMAGE_HEADER_MAGIC; d->header.cf = LV_COLOR_FORMAT_ARGB8888;
        d->header.w = (uint32_t)w; d->header.h = (uint32_t)h; d->header.stride = (uint32_t)w * 4;
        d->data = (const uint8_t *)px; d->data_size = (uint32_t)w * h * 4;
        lv_obj_t *im = lv_image_create(g_sheen); lv_image_set_src(im, d); lv_obj_set_pos(im, x0, y0);
        lv_obj_clear_flag(im, LV_OBJ_FLAG_CLICKABLE);
    }
    lv_obj_add_flag(g_sheen, LV_OBJ_FLAG_HIDDEN);
}
/* show it only once the slide into Music has finished (it must never draw over the screen that is leaving) */
static lv_timer_t *g_sh_tm;
static void sheen_show_cb(lv_timer_t *t){ (void)t; g_sh_tm = NULL; disco_sheen_apply(); }

int disco_mirror(void){
    static int m = -1;
    if(m < 0) m = th_null()?0:th_disco_family() && simple_cfg_get_int("disco_side", 0) == 1;
    return m;
}
/* ---- the navigation circle ------------------------------------------------------------------------- */
/* At 270 degrees (9 o'clock): its left edge just touches the screen's edge, radius ~25% of the screen width. */
#define PK_D 180
#define PK_W 230                                                     /* open: a stadium whose right end the rim cuts off */
#define PK_X 196                                                     /* popped in at 90 degrees too: it touches the right rim */
#define PK_Y 90
static lv_obj_t *g_pk, *g_pk_icon, *g_pk_glow, *g_pk_name, *g_pk_dot[MAXM];
/* The circle pops in and out: closed, it is a small oval tab with the section icon on the right edge (the left edge
 * stays free for the back swipe); a tap on the tab opens the circle (80% opaque) over the screen; a tap anywhere else
 * closes it again. */
static lv_obj_t *g_tab, *g_tab_icon, *g_catch;
static int g_open, g_cur_scr = -1;
/* closed: the same circle, parked at 90 degrees (3 o'clock) almost entirely outside the screen - the round bezel
 * leaves a pointed sliver, as if a chunk were cut out of the edge, level with the open circle's middle */
#define TAB_D PK_D
#define TAB_IN 38                                                   /* how far the sliver reaches into the screen */
#define TAB_X (360 - TAB_IN)
#define TAB_Y (PK_Y + PK_D / 2 - TAB_D / 2)
static int g_press_y, g_long, g_second_tap;
static lv_timer_t *g_single_tap;
static int g_single_section;
enum { NAV_DOUBLE_MS = 260 };
static void nav_cancel_tap(void){
    if(g_single_tap){lv_timer_delete(g_single_tap);g_single_tap=NULL;}
    g_second_tap=0;
}
/* 1.4.1 inserted Volume, Screen off, Play/Pause and Next track before Nothing: a saved 6 meant Nothing then */
void disco_nav_migrate(void){
    if(simple_cfg_get_int("disco_nav_v",1)>=2)return;
    const char *keys[2]={"disco_nav_double","disco_nav_hold"};
    for(int i=0;i<2;i++)if(simple_cfg_get_int(keys[i],-1)==6)simple_cfg_set_int(keys[i],DISCO_NAV_NONE);
    simple_cfg_set_int("disco_nav_v",2);
}
static int nav_action(const char *key,int fallback){
    disco_nav_migrate();
    int action=simple_cfg_get_int(key,fallback);
    return action>=0 && action<DISCO_NAV_COUNT?action:fallback;
}
void disco_nav_action_run(int action){
    if(action<0 || action>=DISCO_NAV_COUNT || action==DISCO_NAV_NONE)return;
    switch(action){
        case DISCO_NAV_MUSIC:screen_home();break;
        case DISCO_NAV_SEARCH:screen_show(SCR_SEARCH);break;
        case DISCO_NAV_QUEUE:queue_open();break;
        case DISCO_NAV_UPNEXT:upnext_open();break;
        case DISCO_NAV_SHORTCUTS:apps_reload();screen_show(SCR_APPS);break;
        case DISCO_NAV_QUICK:screen_show(SCR_QUICK);break;
        case DISCO_NAV_VOLUME:{track_state_t st;ipc_get_state(&st);ui_show_volume(st.volume);}break;   /* the rim arc, ready to drag */
        case DISCO_NAV_SCREENOFF:ui_request_sleep();break;                   /* as the old Screen off tile: dims, then off */
        case DISCO_NAV_PLAYPAUSE:ui_transport_command("0201000C0000");break;
        case DISCO_NAV_NEXT:ui_transport_command("0201000C0001");break;
        case DISCO_NAV_SHUFFLE:
            if(library_shuffle_all())ui_toast_icon(LV_SYMBOL_SHUFFLE,TC(ACCENT_PRIMARY),"Happy listening!");
            break;
    }
}
static void nav_run(int action){
    if(!g_open || action==DISCO_NAV_NONE)return;
    nav_cancel_tap();disco_nav_set_open(0);disco_nav_action_run(action);
}
static int picker_wanted(int which){
    switch(which){
        case SCR_HOME: case SCR_LIBRARY: case SCR_SETTINGS: case SCR_SETLIST: case SCR_WORKMODE: case SCR_APPS:
        case SCR_PLVIEW: case SCR_FOLDER: case SCR_BOOKS: case SCR_ALBUMWALL: case SCR_UPNEXT: case SCR_WIFI:
        case SCR_ROULETTE: case SCR_BT: case SCR_WEATHER: case SCR_MODEINFO: case SCR_USAGE: case SCR_EQ: case SCR_MA: case SCR_APPLAND: case SCR_APPSHELF: return 1;
        case SCR_SEARCH: return search_landing_active();             /* the landing page has the menu; the keyboard doesn't */
        default: return 0;
    }
}
static void picker_paint(void){
    if(!g_pk || g_nmenu <= 0) return;
    const dsec_t section=menu_section(g_menu[g_cur]);const dsec_t *s=&section;
    lv_color_t acc = ui_current_accent();
    lv_label_set_text(g_pk_icon, s->icon);
    lv_obj_set_style_text_font(g_pk_icon, (!strcmp(s->icon, "EQ") || !strcmp(s->icon, "MA")) ? TF(UI_28) : !strcmp(s->icon, IC_MIC) ? TF(ICON_28) : (!strcmp(s->icon, TH_IC_SEARCH) ? &font_theme_24 : TF(UI_36)), 0);
    roulette_icon(g_pk_icon, s->icon, 32);
    lv_obj_set_style_outline_color(g_pk_glow, acc, 0); lv_obj_set_style_border_color(g_pk_glow, acc, 0);   /* the glow follows the accent */
    lv_obj_set_style_bg_color(g_pk_glow, acc, 0);
    lv_label_set_text(g_pk_name, s->name);
    if(g_tab_icon){ lv_label_set_text(g_tab_icon, s->icon);
                    lv_obj_set_style_text_font(g_tab_icon, (!strcmp(s->icon, "EQ") || !strcmp(s->icon, "MA")) ? TF(UI_12) : !strcmp(s->icon, IC_MIC) ? TF(ICON_20) : (!strcmp(s->icon, TH_IC_SEARCH) ? &font_theme_20 : TF(UI_18)), 0);
                    roulette_icon(g_tab_icon, s->icon, 18);
                    lv_obj_set_style_border_color(g_tab, acc, 0); }
    for(int i = 0; i < MAXM; i++){
        if(!g_pk_dot[i]) continue;
        if(i >= g_nmenu){ lv_obj_add_flag(g_pk_dot[i], LV_OBJ_FLAG_HIDDEN); continue; }
        lv_obj_remove_flag(g_pk_dot[i], LV_OBJ_FLAG_HIDDEN);
        float span = g_nmenu > 1 ? 64.0f : 0.0f;                         /* curved along the circle's left edge */
        float a = (180.0f + span / 2 - (g_nmenu > 1 ? span * i / (g_nmenu - 1) : 0)) * 0.0174533f;   /* first item (Music) at the top */
        int on = i == g_cur, sz = on ? 10 : 7, r = PK_D / 2 - 16;
        lv_obj_set_size(g_pk_dot[i], sz, sz);
        int dx = PK_D / 2 + (int)lroundf(r * cosf(a)) - sz / 2;
        lv_obj_set_pos(g_pk_dot[i], disco_mirror() ? PK_W - dx - sz : dx, PK_D / 2 + (int)lroundf(r * sinf(a)) - sz / 2);
        lv_obj_set_style_bg_color(g_pk_dot[i], on ? acc : TC(TEXT_MUTED), 0);
    }
}
static void go_section(int idx, int dir){
    if(g_nmenu <= 0) return;
    idx = (idx % g_nmenu + g_nmenu) % g_nmenu;
    g_cur = idx; picker_paint();
    const dsec_t section=menu_section(g_menu[idx]);const dsec_t *s=&section;
    if(g_menu[idx] >= NSEC){                                         /* an installed app: its landing page, never a straight launch */
        const menu_app_t *app=menu_apps_get(g_menu[idx]-NSEC);
        if(!app){ ui_toast("That app is not installed"); return; }
        if(!strcmp(app->exec,APP_BUILTIN_SHELF)){discshelf_landing();onboarding_discovered(HINT_NAV);return;}
        applanding_icon(app->icon);
        applanding_show(app->name, app->tagline, app->about, app->button, app->exec);
        if(screen_current()==SCR_APPLAND)onboarding_discovered(HINT_NAV);
        return;
    }
    if(s->open){ s->open(); if(screen_current()==s->screen)onboarding_discovered(HINT_NAV); return; }                                  /* screens that need their own refresh */
    screen_section(s->screen, dir);
    if(screen_current()==s->screen)onboarding_discovered(HINT_NAV);
}
static void nav_single_cb(lv_timer_t *timer){
    int section=g_single_section;
    g_single_tap=NULL;lv_timer_delete(timer);
    if(g_open && g_cur==section)go_section(section,+1);
}
static void picker_cb(lv_event_t *e){
    lv_event_code_t c = lv_event_get_code(e);
    if(!g_open){nav_cancel_tap();return;}
    lv_indev_t *in = lv_indev_active(); lv_point_t p = { 0, 0 };
    if(in) lv_indev_get_point(in, &p);
    if(c == LV_EVENT_PRESS_LOST || ((c == LV_EVENT_PRESSING || c == LV_EVENT_LONG_PRESSED || c == LV_EVENT_RELEASED) && !lv_obj_hit_test(g_pk,&p))){
        nav_cancel_tap();g_long=1;return;
    }
    if(c == LV_EVENT_PRESSED){
        int second=g_single_tap && p.y>=PK_Y+PK_D/3 && p.y<=PK_Y+PK_D*2/3;
        nav_cancel_tap();g_second_tap=second;g_press_y=p.y;g_long=0;return;
    }
    if(c == LV_EVENT_LONG_PRESSED){if(g_long)return;g_long=1;nav_cancel_tap();nav_run(nav_action("disco_nav_hold",DISCO_NAV_MUSIC));return;}
    if(c != LV_EVENT_RELEASED || g_long) return;
    int dy = p.y - g_press_y;
    if(dy < -24){ nav_cancel_tap(); go_section(g_cur + 1, +1); return; }                 /* swipe up: next */
    if(dy > 24){ nav_cancel_tap(); go_section(g_cur - 1, -1); return; }                  /* swipe down: previous */
    int rel = g_press_y - PK_Y;
    if(rel < PK_D / 3){nav_cancel_tap();go_section(g_cur - 1,-1);}     /* arrows remain immediate */
    else if(rel > PK_D * 2 / 3){nav_cancel_tap();go_section(g_cur + 1,+1);}
    else if(g_second_tap){nav_run(nav_action("disco_nav_double",DISCO_NAV_SEARCH));}
    else if(nav_action("disco_nav_double",DISCO_NAV_SEARCH)==DISCO_NAV_NONE)go_section(g_cur,+1);
    else{
        g_single_section=g_cur;
        g_single_tap=lv_timer_create(nav_single_cb,NAV_DOUBLE_MS,NULL);
        if(!g_single_tap)go_section(g_cur,+1);
    }
}
static void nav_show(void){                                             /* show the right state for the current screen */
    int want = g_cur_scr >= 0 && picker_wanted(g_cur_scr);
    if(want && g_open){ lv_obj_remove_flag(g_catch, LV_OBJ_FLAG_HIDDEN); lv_obj_move_foreground(g_catch);
                        lv_obj_remove_flag(g_pk, LV_OBJ_FLAG_HIDDEN); lv_obj_move_foreground(g_pk); }
    else { lv_obj_add_flag(g_catch, LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(g_pk, LV_OBJ_FLAG_HIDDEN); }
    if(want && !g_open){ lv_obj_remove_flag(g_tab, LV_OBJ_FLAG_HIDDEN); lv_obj_move_foreground(g_tab); }
    else lv_obj_add_flag(g_tab, LV_OBJ_FLAG_HIDDEN);
}
/* open / close: the oval slides out of the right edge and back in (a plain move, no scaling: cheap to draw) */
#define PK_OPEN_MS 180
#define PK_CLOSE_MS 140
#define PK_OPEN_X disco_mx(PK_X, PK_W)                                  /* open / parked off-screen, on the hub's side */
#define PK_OFF_X  (disco_mirror() ? -PK_W : 360)
static void slide_cb(void *o, int32_t v){ lv_obj_set_x((lv_obj_t *)o, v); }
static void slide_in_done(lv_anim_t *a){ (void)a; if(!g_open) nav_show(); }   /* closed: hide the oval, show the shard */
void disco_nav_set_open(int open){
    if(th_simple()){simple_nav_open(open);return;}
    if(!open)nav_cancel_tap();
    if(!g_pk || g_open == !!open) return;
    int visible = !lv_obj_has_flag(g_pk, LV_OBJ_FLAG_HIDDEN);
    int start = visible ? lv_obj_get_x(g_pk) : PK_OFF_X;
    lv_anim_delete(g_pk, slide_cb);
    int was = g_open; g_open = open ? 1 : 0;
    onboarding_screen(g_cur_scr,g_open);
    lv_anim_t a; lv_anim_init(&a); lv_anim_set_var(&a, g_pk); lv_anim_set_exec_cb(&a, slide_cb);
    int end = g_open ? PK_OPEN_X : PK_OFF_X;
    int duration = (g_open ? PK_OPEN_MS : PK_CLOSE_MS) * abs(end - start) / (360 - PK_X);
    if(duration < 60) duration = 60;
    lv_anim_set_duration(&a, duration);
    if(g_open){
        nav_show();
        lv_anim_set_values(&a, start, PK_OPEN_X); lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
        lv_anim_start(&a);
    } else {
        lv_obj_add_flag(g_catch, LV_OBJ_FLAG_HIDDEN);                    /* the screen answers touches again at once */
        if(!was || lv_obj_has_flag(g_pk, LV_OBJ_FLAG_HIDDEN)){ nav_show(); return; }
        lv_anim_set_values(&a, lv_obj_get_x(g_pk), PK_OFF_X); lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
        lv_anim_set_completed_cb(&a, slide_in_done);
        lv_anim_start(&a);
    }
}
static void catch_cb(lv_event_t *e){ (void)e; disco_nav_set_open(0); }
static void tab_cb(lv_event_t *e){ (void)e; disco_nav_set_open(1); onboarding_discovered(HINT_MENU); onboarding_screen(g_cur_scr,g_open); }
static void picker_create(void){
    g_pk = lv_obj_create(g_parent);
    lv_obj_remove_style_all(g_pk);
    int m = disco_mirror() ? -1 : 1;                                     /* Left: the circle's inner offsets point the other way */
    lv_obj_set_pos(g_pk, PK_OPEN_X, PK_Y); lv_obj_set_size(g_pk, PK_W, PK_D);
    lv_obj_set_style_radius(g_pk, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(g_pk, TC(SCRIM), 0);                     /* black in both variants */
    lv_obj_set_style_bg_opa(g_pk, 204, 0);                              /* 80% */
    lv_obj_set_style_bg_opa(g_pk, 235, LV_STATE_PRESSED);                /* pressed: a touch darker, never a light flash */
    lv_obj_clear_flag(g_pk, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_add_flag(g_pk, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(g_pk, picker_cb, LV_EVENT_ALL, NULL);
    lv_color_t white = theme_color(THEME_CLR_FIXED_MEDIA_WHITE);       /* white text on the black circle in both variants */
    lv_color_t grey = theme_color(THEME_CLR_FIXED_MEDIA_ARTIST);
    g_pk_glow = lv_obj_create(g_pk); lv_obj_remove_style_all(g_pk_glow);   /* a soft accent glow behind the icon */
    /* a 40 px blurred shadow was redrawn (uncached) on every frame of the circle's slide: now two soft rings around a
     * faint disc - plain fills, the same feel */
    lv_obj_set_size(g_pk_glow, 50, 50); lv_obj_align(g_pk_glow, LV_ALIGN_CENTER, -38 * m, -16);
    lv_obj_set_style_radius(g_pk_glow, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(g_pk_glow, 55, 0);
    lv_obj_set_style_outline_width(g_pk_glow, 10, 0); lv_obj_set_style_outline_opa(g_pk_glow, 30, 0); lv_obj_set_style_outline_pad(g_pk_glow, 0, 0);
    lv_obj_set_style_border_width(g_pk_glow, 6, 0); lv_obj_set_style_border_opa(g_pk_glow, 25, 0);
    lv_obj_clear_flag(g_pk_glow, LV_OBJ_FLAG_CLICKABLE);
    g_pk_icon = lv_label_create(g_pk); lv_obj_set_style_text_color(g_pk_icon, white, 0); lv_obj_align(g_pk_icon, LV_ALIGN_CENTER, -38 * m, -16);
    g_pk_name = lv_label_create(g_pk); lv_obj_set_style_text_color(g_pk_name, white, 0); lv_obj_set_style_text_font(g_pk_name, TF(UI_20), 0);
    lv_obj_align(g_pk_name, LV_ALIGN_CENTER, -38 * m, 26);
    lv_obj_t *u = lv_label_create(g_pk); lv_label_set_text(u, LV_SYMBOL_UP); lv_obj_set_style_text_font(u, TF(UI_16), 0);
    lv_obj_set_style_text_color(u, grey, 0); lv_obj_align(u, LV_ALIGN_TOP_MID, -38 * m, 12);
    {   /* a collapse arrow at the far right, against the rim: tapping it closes the circle (as a tap outside does) */
        lv_obj_t *cb = lv_obj_create(g_pk); lv_obj_remove_style_all(cb);
        lv_obj_set_size(cb, 26, 76); lv_obj_set_pos(cb, disco_mirror() ? PK_W - (360 - PK_X - 27) - 26 : 360 - PK_X - 27, PK_D / 2 - 38);
        lv_obj_set_style_radius(cb, 17, 0); lv_obj_set_style_bg_color(cb, white, 0); lv_obj_set_style_bg_opa(cb, 0, 0);
        lv_obj_set_style_bg_opa(cb, 40, LV_STATE_PRESSED);
        lv_obj_add_flag(cb, LV_OBJ_FLAG_CLICKABLE); lv_obj_clear_flag(cb, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_add_event_cb(cb, catch_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_t *cl = lv_label_create(cb); lv_label_set_text(cl, disco_mirror() ? LV_SYMBOL_LEFT : LV_SYMBOL_RIGHT); lv_obj_set_style_text_font(cl, TF(UI_16), 0);
        lv_obj_set_style_text_color(cl, grey, 0); lv_obj_align(cl, LV_ALIGN_CENTER, m, 0);
    }
    lv_obj_t *d = lv_label_create(g_pk); lv_label_set_text(d, LV_SYMBOL_DOWN); lv_obj_set_style_text_font(d, TF(UI_16), 0);
    lv_obj_set_style_text_color(d, grey, 0); lv_obj_align(d, LV_ALIGN_BOTTOM_MID, -38 * m, -12);
    for(int i = 0; i < MAXM; i++){
        g_pk_dot[i] = lv_obj_create(g_pk); lv_obj_remove_style_all(g_pk_dot[i]);
        lv_obj_set_style_radius(g_pk_dot[i], LV_RADIUS_CIRCLE, 0); lv_obj_set_style_bg_opa(g_pk_dot[i], LV_OPA_COVER, 0);
        lv_obj_clear_flag(g_pk_dot[i], LV_OBJ_FLAG_CLICKABLE);
    }
    lv_obj_add_flag(g_pk, LV_OBJ_FLAG_HIDDEN);
    /* the catcher: while open, a tap anywhere outside the circle closes it */
    g_catch = lv_obj_create(g_parent); lv_obj_remove_style_all(g_catch);
    lv_obj_set_size(g_catch, 360, 360); lv_obj_add_flag(g_catch, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(g_catch, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(g_catch, catch_cb, LV_EVENT_CLICKED, NULL);
    /* the closed tab: an oval shade on the right edge with the section's icon */
    g_tab = lv_obj_create(g_parent); lv_obj_remove_style_all(g_tab);
    lv_obj_set_pos(g_tab, disco_mx(TAB_X, TAB_D), TAB_Y); lv_obj_set_size(g_tab, TAB_D, TAB_D);
    lv_obj_set_style_radius(g_tab, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(g_tab, TC(SCRIM), 0); lv_obj_set_style_bg_opa(g_tab, 190, 0);
    lv_obj_set_style_bg_opa(g_tab, 235, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(g_tab, 2, 0); lv_obj_set_style_border_opa(g_tab, 130, 0);   /* a thin accent rim (a shadow is redrawn uncached on every frame) */
    lv_obj_set_ext_click_area(g_tab, 8);
    lv_obj_clear_flag(g_tab, LV_OBJ_FLAG_SCROLLABLE); lv_obj_add_flag(g_tab, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(g_tab, tab_cb, LV_EVENT_CLICKED, NULL);
    g_tab_icon = lv_label_create(g_tab); lv_obj_set_style_text_color(g_tab_icon, white, 0);
    lv_obj_align(g_tab_icon, disco_mirror() ? LV_ALIGN_RIGHT_MID : LV_ALIGN_LEFT_MID, disco_mirror() ? -10 : 10, 0);   /* in the visible sliver */
    picker_paint();

}

/* lists keep their rows clear of the circle: a row whose height overlaps it starts right of the circle's edge there */
static int sliver_edge(int y1, int y2);
int disco_clear_left(int y1, int y2){
    if(!g_pk) return 0;
    if(disco_mirror()){ int e = sliver_edge(y1, y2); return e ? 360 - e : 0; }   /* Left: rows start right of the sliver */
    return 0;                                                           /* the circle is now a pop-in overlay: lists keep their width */
    int cy = PK_Y + PK_D / 2, r = PK_D / 2;
    int dy = (y1 <= cy && y2 >= cy) ? 0 : (y2 < cy ? cy - y2 : y1 - cy);
    if(dy >= r) return 0;
    return PK_X + r + (int)sqrtf((float)(r * r - dy * dy)) + 8;
}
/* lists keep their rows' right end clear of the closed sliver (the open circle is an overlay: it doesn't count) */
int disco_clear_right(int y1, int y2){
    if(disco_mirror()) return 0;                                         /* Left: nothing on the right to clear */
    return sliver_edge(y1, y2);
}
static int sliver_edge(int y1, int y2){                                 /* the right-hand sliver's clearance (Left: mirrored by the caller) */
    if(!g_tab || lv_obj_has_flag(g_tab, LV_OBJ_FLAG_HIDDEN)) return 0;
    int cx = TAB_X + TAB_D / 2, cy = TAB_Y + TAB_D / 2, r = TAB_D / 2, best = 360;
    for(int y = y1; y <= y2; y += 4){                                    /* the sliver's left edge, nearest point in the row */
        int dy = y - cy; if(dy < 0) dy = -dy;
        if(dy >= r) continue;
        int x = cx - (int)sqrtf((float)(r * r - dy * dy));
        if(x < best) best = x;
    }
    return best < 360 ? best - 8 : 0;
}
int disco_nav_right(void){ return PK_X + PK_D; }                       /* the circle's right edge (screen layouts) */
/* ---- the screen manager's hooks --------------------------------------------------------------------- */
static int g_np_want;
int disco_np_wanted(void){ return g_np_want; }
void dhome_imm_leave(void (*done)(void)); void dhome_imm_return(void);
static void np_immersive_now(void){
    /* Ask the overlay itself before navigating: it can refuse when the UI
     * has no track, even if IPC still carries the previous track. */
    ui_np_fsart_open();
    if(!ui_np_fsart_active()){ ui_toast("Play a track first"); if(th_disco_family() && screen_current() == SCR_HOME) dhome_imm_return(); return; }
    if(screen_get_anim()) screen_skip_anim_once();                    /* the record drops in instead of the slide */
    g_np_want = 1; screen_show(SCR_NOWPLAYING); g_np_want = 0;
}
void disco_open_np_immersive(void){
    if(th_disco_family() && !th_simple() && screen_current() == SCR_HOME && screen_get_anim()) dhome_imm_leave(np_immersive_now);   /* Music's pieces step aside first */
    else np_immersive_now();
}

void disco_init(lv_obj_t *parent){
    if(th_simple()){g_parent=parent;menu_load();simple_nav_init(parent);return;}
    disco_nav_migrate();                                           /* before Settings reads the gesture choices */
    g_parent = parent;
    menu_load();
    backdrop_bake(); backdrop_apply();
    sheen_create();
    picker_create();
}
/* lists curve their rows on scroll: nudge every list on the incoming screen once, so they clear the picker now */
static void recurve(lv_obj_t *o, int depth){
    uint32_t n = lv_obj_get_child_count(o);
    for(uint32_t i = 0; i < n; i++){
        lv_obj_t *c = lv_obj_get_child(o, i);
        if(lv_obj_has_flag(c, LV_OBJ_FLAG_HIDDEN)) continue;
        if(lv_obj_get_child_count(c) > 1 && (lv_obj_get_scroll_dir(c) & LV_DIR_VER) && lv_obj_has_flag(c, LV_OBJ_FLAG_SCROLLABLE))
            lv_obj_send_event(c, LV_EVENT_SCROLL, NULL);
        if(depth < 3) recurve(c, depth + 1);
    }
}
int search_landing_active(void); void search_landing_reset(void);
void disco_nav_refresh(void){ if(th_simple()){simple_nav_refresh();return;} if(g_pk) nav_show(); }
static void ed_close(void);
void dhome_overlay_close(void);
int screen_popping(void);
void disco_screen_entering(int which){
    if(th_simple()){onboarding_dismiss();if(which==SCR_SEARCH&&!screen_popping())search_landing_reset();simple_nav_screen(which);dhome_overlay_close();ed_close();return;}
    onboarding_dismiss();
    nav_cancel_tap();
    if(which == SCR_SEARCH && !screen_popping()) search_landing_reset();  /* Menu > Search: the landing page first; back keeps results */
    dhome_overlay_close(); ed_close();                                    /* top-layer panels never outlive their screen (standby, keys) */
    if(!g_pk) return;
    g_cur_scr = which; nav_show();
}
void disco_screen_entered(int which, lv_obj_t *root){
    if(th_simple()){simple_nav_screen(which);return;}
    if(!g_parent) return;
    int sec_scr = which;                                                /* sub-screens keep their section's heading */
    if(which == SCR_SETLIST || which == SCR_SETTING_DETAIL) sec_scr = SCR_SETTINGS;
    else if(which == SCR_PLVIEW || ((which == SCR_ALBUMWALL || which == SCR_FOLDER || which == SCR_BOOKS) && !menu_has_screen(which))) sec_scr = SCR_LIBRARY;
    else if(which == SCR_MODEINFO) sec_scr = SCR_WORKMODE;
    if(!(g_cur < g_nmenu && menu_section(g_menu[g_cur]).screen == sec_scr))      /* Library / Artists / Songs share a screen: keep the one picked */
        for(int i = 0; i < g_nmenu; i++) if(menu_section(g_menu[i]).screen == sec_scr){ if(g_cur != i){ g_cur = i; picker_paint(); } break; }
    if(root && picker_wanted(which)){ lv_obj_update_layout(root); recurve(root, 0); }
    if(root && !no_backdrop(which)){
        lv_opa_t o = theme_variant() ? 170 : 150;
        glass_walk(root, theme_rgb(THEME_CLR_SURFACE), theme_rgb(THEME_CLR_SURFACE_RAISED), o, 0);
    }
    g_sheen_scr = which;
    if(g_sheen){ lv_obj_add_flag(g_sheen, LV_OBJ_FLAG_HIDDEN);
                 if((which == SCR_HOME || which == SCR_PLVIEW) && simple_cfg_get_int("disco_sheen", 1) && !g_sh_tm){
                     g_sh_tm = lv_timer_create(sheen_show_cb, 260, NULL); if(g_sh_tm) lv_timer_set_repeat_count(g_sh_tm, 1); } }
    if(g_pk){ g_cur_scr = which; picker_paint(); nav_show(); }
    onboarding_screen(which,g_open);
}
void disco_art_changed(void){
    if(th_simple()){dhome_art_changed();return;}
    if(!th_disco_family() || !g_parent) return;
    /* Track identity and position frames can republish unchanged art. Re-bake
     * only when pixels, appearance or idle state actually changed. */
    extern unsigned ui_art_revision(void);
    static unsigned last_revision;
    static uint32_t last_canvas;
    static int last_variant, last_idle, seen;
    static lv_color_t last_accent;
    unsigned revision = ui_art_revision();
    uint32_t canvas = theme_rgb(THEME_CLR_CANVAS);
    int variant = theme_variant(), idle = dhome_idle();
    lv_color_t accent = ui_current_accent();
    if(seen && revision == last_revision && canvas == last_canvas &&
       variant == last_variant && idle == last_idle && lv_color_eq(accent, last_accent)) return;
    seen = 1; last_revision = revision; last_canvas = canvas;
    last_variant = variant; last_idle = idle; last_accent = accent;
    backdrop_bake(); backdrop_apply();
    dhome_art_changed();
    picker_paint();                                                     /* the current dot follows the accent */
}

/* ---- Settings > Display > Disco Menu: choose, order, add and remove the picker's sections ----------- */
static lv_obj_t *g_ed, *g_ed_list;
static int g_edit[10],g_edit_n,g_edit_qs;
static void ed_build(void);
static int ed_in(int id){for(int i=0;i<g_edit_n;i++)if(g_edit[i]==id)return i;return -1;}
static int ed_catalog_count(void){return g_edit_qs?quicksettings_item_count():NSEC+menu_apps_count();}
static const char *ed_name(int id){return g_edit_qs?quicksettings_item(id)->name:menu_section(id).name;}
static const char *ed_icon(int id){return g_edit_qs?quicksettings_item(id)->icon:menu_section(id).icon;}
static int ed_protected(int id){return g_edit_qs?0:menu_section(id).prot;}
static void ed_build_async(void *p){(void)p;ed_build();}
static void ed_close(void){if(g_ed){lv_obj_delete(g_ed);g_ed=NULL;g_ed_list=NULL;picker_paint();if(th_simple())simple_nav_refresh();if(g_edit_qs)quicksettings_build();if(th_null())null_menu_refresh();}}
int disco_editor_back(void){if(!g_ed)return 0;ed_close();return 1;}
static void ed_close_cb(lv_event_t *e){if(lv_event_get_code(e)==LV_EVENT_CLICKED)ed_close();}
static int ed_save(void){
    if(g_edit_qs)return quicksettings_items_save(g_edit,g_edit_n);
    if(menu_apps_save_named(g_edit,g_edit_n,CODE,th_null()?"null_mnu":"disco_mnu",th_null()?"nm_app":"dm_app")<0)return -1;
    memcpy(g_menu,g_edit,sizeof(int)*(size_t)g_edit_n);g_nmenu=g_edit_n;
    if(g_cur>=g_nmenu)g_cur=0;
    return 0;
}
static void ed_act_cb(lv_event_t *e){
    if(lv_event_get_code(e)!=LV_EVENT_CLICKED)return;
    intptr_t v=(intptr_t)lv_event_get_user_data(e);int id=(int)(v&0xffff),act=(int)(v>>16),i=ed_in(id);
    if(id<0 || id>=ed_catalog_count())return;
    int old[10],old_n=g_edit_n;memcpy(old,g_edit,sizeof old);
    if(act==1 && i>0){int t=g_edit[i-1];g_edit[i-1]=g_edit[i];g_edit[i]=t;}
    else if(act==2 && i>=0 && i<g_edit_n-1){int t=g_edit[i+1];g_edit[i+1]=g_edit[i];g_edit[i]=t;}
    else if(act==3 && i>=0 && !ed_protected(id)){memmove(g_edit+i,g_edit+i+1,sizeof(int)*(size_t)(g_edit_n-i-1));g_edit_n--;}
    else if(act==4 && i<0){
        if(g_edit_n>=(g_edit_qs?quicksettings_item_limit():MAXM)){ui_toast(g_edit_qs?((th_null()||th_simple())?"Remove an item first (eight maximum)":th_disco()?"Remove an item first (seven maximum)":"Remove an item first (six maximum)"):"The menu holds up to ten");return;}
        g_edit[g_edit_n++]=id;
    }else return;
    if(ed_save()<0){memcpy(g_edit,old,sizeof old);g_edit_n=old_n;ui_toast("Couldn't save settings");return;}
    lv_async_call(ed_build_async,NULL);
}
static lv_obj_t *ed_btn(lv_obj_t *row,int x,const char *glyph,int id,int act,int dim){
    lv_obj_t *b=lv_button_create(row);lv_obj_remove_style_all(b);lv_obj_set_size(b,th_simple()?40:34,th_simple()?40:34);lv_obj_set_pos(b,th_simple()?(x==150?144:x==234?232:x):x,th_simple()?3:6);if(th_simple())lv_obj_set_ext_click_area(b,2);
    lv_obj_set_style_radius(b,LV_RADIUS_CIRCLE,0);lv_obj_set_style_bg_color(b,TC(SURFACE_RAISED),0);
    lv_obj_set_style_bg_opa(b,dim?LV_OPA_TRANSP:LV_OPA_COVER,0);lv_obj_set_style_bg_color(b,TC(CONTROL_TRACK),LV_STATE_PRESSED);
    lv_obj_t *l=lv_label_create(b);lv_label_set_text(l,glyph);lv_obj_set_style_text_font(l,TF(UI_14),0);
    lv_obj_set_style_text_color(l,dim?TC(TEXT_DISABLED):TC(TEXT_PRIMARY),0);lv_obj_center(l);
    if(!dim)lv_obj_add_event_cb(b,ed_act_cb,LV_EVENT_CLICKED,(void*)(intptr_t)(id|(act<<16)));else lv_obj_clear_flag(b,LV_OBJ_FLAG_CLICKABLE);
    return b;
}
static void ed_row(int id,int pos){
    lv_obj_t *r=lv_obj_create(g_ed_list);lv_obj_remove_style_all(r);lv_obj_set_size(r,276,46);
    lv_obj_set_style_radius(r,TH_R_ROW,0);lv_obj_set_style_bg_color(r,TC(SURFACE),0);
    lv_obj_set_style_bg_opa(r,pos>=0?LV_OPA_COVER:LV_OPA_TRANSP,0);lv_obj_clear_flag(r,LV_OBJ_FLAG_SCROLLABLE);
    const char *icon=ed_icon(id);lv_obj_t *ic=lv_label_create(r);lv_label_set_text(ic,icon);
    const lv_font_t *font=!strcmp(icon,TH_IC_SEARCH)?&font_theme_20:!strcmp(icon,IC_MIC)?TF(ICON_20):(!strcmp(icon,"EQ") || !strcmp(icon,"MA"))?TF(UI_14):TF(UI_18);
    if(g_edit_qs && (!strcmp(quicksettings_item(id)->code,"h") || !strcmp(quicksettings_item(id)->code,"a")))font=TF(ICON_20);
    lv_obj_set_style_text_font(ic,font,0);lv_obj_set_style_text_color(ic,pos>=0?ui_current_accent():TC(TEXT_MUTED),0);lv_obj_align(ic,LV_ALIGN_LEFT_MID,12,0);
    if(!g_edit_qs)roulette_icon(ic,icon,20);
    lv_obj_t *nm=lv_label_create(r);lv_label_set_text(nm,ed_name(id));lv_obj_set_style_text_font(nm,TF(UI_16),0);
    lv_obj_set_width(nm,th_simple()?94:102);lv_label_set_long_mode(nm,LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_color(nm,pos>=0?TC(TEXT_PRIMARY):TC(TEXT_SECONDARY),0);lv_obj_align(nm,LV_ALIGN_LEFT_MID,44,0);
    if(pos<0){ed_btn(r,234,LV_SYMBOL_PLUS,id,4,0);return;}
    ed_btn(r,150,LV_SYMBOL_UP,id,1,pos==0);ed_btn(r,188,LV_SYMBOL_DOWN,id,2,pos==g_edit_n-1);
    if(ed_protected(id)){lv_obj_t *lock=lv_label_create(r);lv_label_set_text(lock,LV_SYMBOL_EYE_OPEN);lv_obj_set_style_text_font(lock,TF(UI_14),0);lv_obj_set_style_text_color(lock,TC(TEXT_DISABLED),0);lv_obj_set_pos(lock,244,14);}
    else ed_btn(r,234,LV_SYMBOL_CLOSE,id,3,0);
}
static void ed_build(void){
    if(!g_ed)return;
    int32_t sy=lv_obj_get_scroll_y(g_ed_list);lv_obj_clean(g_ed_list);
    for(int i=0;i<g_edit_n;i++)ed_row(g_edit[i],i);
    lv_obj_t *h=lv_label_create(g_ed_list);lv_label_set_text(h,g_edit_qs?"Available items":"Add to the menu");
    lv_obj_set_style_text_font(h,TF(UI_14),0);lv_obj_set_style_text_color(h,TC(TEXT_MUTED),0);
    for(int id=0;id<ed_catalog_count();id++)if(ed_in(id)<0)ed_row(id,-1);
    lv_obj_t *pad=lv_obj_create(g_ed_list);lv_obj_remove_style_all(pad);lv_obj_set_size(pad,10,40);
    lv_obj_update_layout(g_ed_list);lv_obj_scroll_to_y(g_ed_list,sy,LV_ANIM_OFF);
}
static void editor_open(int qs){
    if(!qs&&!th_disco_family()&&!th_null()){ui_toast("This editor is part of Disco or Null");return;}if(g_ed)return;
    onboarding_dismiss();g_edit_qs=qs;
    if(qs)g_edit_n=quicksettings_items_load(g_edit);else{menu_load();g_edit_n=g_nmenu;memcpy(g_edit,g_menu,sizeof(int)*(size_t)g_nmenu);}
    g_ed=lv_obj_create(lv_layer_top());lv_obj_remove_style_all(g_ed);lv_obj_set_size(g_ed,360,360);
    lv_obj_set_style_bg_color(g_ed,TC(CANVAS),0);lv_obj_set_style_bg_opa(g_ed,LV_OPA_COVER,0);
    if(g_bd_ok&&!th_simple())lv_obj_set_style_bg_image_src(g_ed,&g_bd_dsc,0);
    lv_obj_add_flag(g_ed,LV_OBJ_FLAG_CLICKABLE);lv_obj_clear_flag(g_ed,LV_OBJ_FLAG_SCROLLABLE);
    ui_header_cb(g_ed,qs?"Quick Items":th_simple()?"Menu items":th_null()?"Null Menu":"Disco Menu",ed_close_cb);
    lv_obj_t *hint=lv_label_create(g_ed);lv_label_set_text(hint,qs?((th_null()||th_simple())?"Choose up to eight; arrows change order":th_disco()?"Choose up to seven; arrows change order":"Choose up to six; arrows change order"):"Music and Settings always stay");
    lv_obj_set_style_text_font(hint,TF(UI_12),0);lv_obj_set_style_text_color(hint,TC(TEXT_MUTED),0);lv_obj_align(hint,LV_ALIGN_TOP_MID,0,64);
    g_ed_list=lv_obj_create(g_ed);lv_obj_remove_style_all(g_ed_list);lv_obj_set_pos(g_ed_list,42,86);lv_obj_set_size(g_ed_list,276,274);
    lv_obj_set_flex_flow(g_ed_list,LV_FLEX_FLOW_COLUMN);lv_obj_set_flex_align(g_ed_list,LV_FLEX_ALIGN_START,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(g_ed_list,6,0);lv_obj_set_scroll_dir(g_ed_list,LV_DIR_VER);lv_obj_set_scrollbar_mode(g_ed_list,LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(g_ed_list,LV_OBJ_FLAG_SCROLL_MOMENTUM);ed_build();kit_pass(g_ed);
}
void disco_menu_open(void){editor_open(0);}
void disco_qs_menu_open(void){editor_open(1);}

/* ---- Disco list rows for hand-built screens (Modes, Shortcuts, ...): the Settings look, all rows in view ----
 * A rounded row at height y, as wide as the circle allows there, its right end clear of the closed sliver.
 * Children: [0] icon, [1] name, [2] value (right-aligned). */
static int sliver_left(int y1, int y2){
    int cx = TAB_X + TAB_D / 2, cy = TAB_Y + TAB_D / 2, r = TAB_D / 2, best = 360;
    for(int y = y1; y <= y2; y += 4){
        int dy = y - cy; if(dy < 0) dy = -dy;
        if(dy >= r) continue;
        int x = cx - (int)sqrtf((float)(r * r - dy * dy));
        if(x < best) best = x;
    }
    return best;
}
lv_obj_t *disco_row(lv_obj_t *root, int y, int h, const char *glyph, const lv_font_t *gfont, const char *name, lv_event_cb_t cb, void *ud){
    int dy = y + h / 2 - 180; if(dy < 0) dy = -dy;
    int reach = dy + h / 2; if(reach > 170) reach = 170;
    int half = (int)sqrtf((float)(176 * 176 - reach * reach));
    int left = 180 - half + 14, right = 180 + half - 14;
    int sl = th_null()?360:sliver_left(y,y+h)-8; if(right>sl)right=sl;
    lv_obj_t *r = lv_obj_create(root); lv_obj_remove_style_all(r);
    lv_obj_set_pos(r, disco_mx(left, right - left), y); lv_obj_set_size(r, right - left, h);
    lv_obj_set_style_radius(r, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(r, TC(SURFACE), 0); lv_obj_set_style_bg_opa(r, LV_OPA_70, 0);
    lv_obj_set_style_bg_color(r, TC(SURFACE_RAISED), LV_STATE_PRESSED);
    lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_USER_2); lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    if(cb) lv_obj_add_event_cb(r, cb, LV_EVENT_CLICKED, ud);
    lv_obj_t *ic = lv_label_create(r); lv_label_set_text(ic, glyph ? glyph : "");
    lv_obj_set_style_text_font(ic, gfont ? gfont : TH_F_LIST, 0); lv_obj_set_style_text_color(ic, ui_current_accent(), 0);
    lv_obj_align(ic, LV_ALIGN_LEFT_MID, 18, 0);
    lv_obj_t *n = lv_label_create(r); lv_label_set_text(n, name ? name : "");
    lv_obj_set_style_text_font(n, TH_F_LIST, 0); lv_obj_set_style_text_color(n, TC(TEXT_PRIMARY), 0);
    lv_label_set_long_mode(n, LV_LABEL_LONG_SCROLL_CIRCULAR); lv_obj_set_width(n, right - left - 96);
    lv_obj_align(n, LV_ALIGN_LEFT_MID, 52, 0);
    lv_obj_t *v = lv_label_create(r); lv_label_set_text(v, "");
    lv_obj_set_style_text_font(v, TH_F_DETAIL, 0); lv_obj_set_style_text_color(v, TC(TEXT_SECONDARY), 0);
    lv_obj_align(v, LV_ALIGN_RIGHT_MID, -16, 0);
    return r;
}
/* Squash and Null share fixed five-row geometry for Shortcuts and Working mode. */
lv_obj_t *squash_menu_row(lv_obj_t *root,int slot,const char *glyph,const lv_font_t *font,const char *name,lv_event_cb_t cb,void *ud){
    lv_obj_t *r=disco_row(root,60+slot*54,50,glyph,font,name,cb,ud);
    int y=60+slot*54,h=50,dy=abs(y+h/2-180),radius=h/2;
    int half=(int)sqrtf((float)((176-radius)*(176-radius)-dy*dy))+radius;
    int left=24+(int)(0.5f*(180-sqrtf((float)(180*180-dy*dy))));
    if(left<180-half)left=180-half;
    int right=180+half;if(right>300)right=300;
    lv_obj_set_pos(r,left,y);lv_obj_set_size(r,right-left,h);
    lv_obj_align(lv_obj_get_child(r,0),LV_ALIGN_LEFT_MID,12,0);
    lv_obj_t *label=lv_obj_get_child(r,1);
    lv_obj_align(label,LV_ALIGN_LEFT_MID,46,0);
    const lv_font_t *face=th_null()?TF(UI_20):ui_font_cjk(20);
    lv_obj_set_style_text_font(label,face,0);
    lv_obj_set_size(label,right-left-72,lv_font_get_line_height(face));
    if(th_null()){
        lv_obj_set_style_radius(r,0,0);
        lv_obj_set_style_text_color(lv_obj_get_child(r,0),TC(TEXT_SECONDARY),0);
        kit_keep(label);
    }else simple_keep_label(label);
    lv_obj_set_style_border_width(r,2,LV_STATE_PRESSED);
    lv_obj_set_style_border_color(r,TC(ACCENT_PRIMARY),LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(r,LV_OPA_COVER,LV_STATE_PRESSED);
    return r;
}
lv_obj_t *disco_title(lv_obj_t *root, const char *t){
    lv_obj_t *l = lv_label_create(root); lv_label_set_text(l, t);
    lv_obj_set_style_text_font(l, th_simple()?TF(UI_26):th_null()?TF(HEADER):TF(UI_20), 0); lv_obj_set_style_text_color(l, TC(TEXT_PRIMARY), 0);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, (th_null()?36:28) - library_lift());   /* on the raised header line */
    if(th_simple()) lv_obj_set_style_text_color(l, TC(ACCENT_PRIMARY), 0);           /* Squash titles are orange */
    if(th_simple())simple_keep_label(l);
    if(th_null()){char upper[192];theme_upper(upper,sizeof upper,t);lv_label_set_text(l,upper);lv_obj_add_flag(l,THEME_TITLE_STEPPED);kit_keep(l);}
    return l;
}
/* the vector icons for other screens (Apps' tiles): "@boom", "@stats", "@vinyl", "@apps" */
void disco_vec_icon(lv_obj_t *label, const char *icon, int size){ roulette_icon(label, icon, size); }
/* host tests only: a tap on the open circle (zone 0 top / 1 middle / 2 bottom, 3 = hold) */
void disco_test_tap(int zone){
    if(zone == 3){ g_long = 1; g_cur = 0; for(int i = 0; i < g_nmenu; i++) if(g_menu[i] == 0) g_cur = i; picker_paint(); screen_home(); g_long = 0; return; }
    g_long = 0; g_press_y = PK_Y + (zone == 0 ? 20 : zone == 1 ? PK_D / 2 : PK_D - 20);
    int rel = g_press_y - PK_Y;
    if(rel < PK_D / 3) go_section(g_cur - 1, -1); else if(rel > PK_D * 2 / 3) go_section(g_cur + 1, +1); else go_section(g_cur, +1);
}

void disco_test_nav_points(lv_point_t *icon,lv_point_t *tab){
    icon->x=disco_mirror()?87:273;icon->y=164;tab->x=disco_mirror()?20:340;tab->y=180;
}
int disco_test_nav_open(void){return g_open;}

void disco_test_editor_close(void){ed_close();}

/* Null shares this action catalog and installed-app resolution, not Disco geometry. */
int disco_catalog_load(void){menu_load();return g_nmenu;}
const char *disco_catalog_id(int index){return index>=0&&index<g_nmenu?menu_section(g_menu[index]).id:"";}
const char *disco_catalog_name(int index){return index>=0&&index<g_nmenu?menu_section(g_menu[index]).name:"";}
void disco_catalog_open(int index){go_section(index,1);}

const char *disco_catalog_icon(int index){return index>=0&&index<g_nmenu?menu_section(g_menu[index]).icon:LV_SYMBOL_FILE;}
