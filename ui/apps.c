#include "simple_theme.h"
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "screens.h"
#include "theme.h"
#include "theme_kit.h"
#include "anim.h"
#include "theme.h"
#include "braun.h"
#include "braun2.h"
#include "orbit.h"
#include "config.h"
#include "curvelist.h"
#include "folderbrowser.h"
#include "books.h"
void disco_vec_icon(lv_obj_t*,const char*,int);
#include "md5.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>

/* Homebrew app launcher. Lists /usr/data/apps/<name>/ entries; each may carry an
 * app.conf ("name=...", "exec=..."), else defaults to dir name + .../app. Tapping
 * a row asks main.c to fork/exec it (app owns fb0 + touch while running). */

#define APPS_DIR "/usr/data/apps"
#define MAX_APPS 40

LV_FONT_DECLARE(font_icons_28)          /* FontAwesome 28px: play-mode + app-tile glyphs */
#define LFM_ICON "\xEF\x88\x82"         /* f202 lastfm    -> Last.fm */
/* Settings/File reuse LV_SYMBOL_SETTINGS (f013) / LV_SYMBOL_FILE (f15b), now in this font */

typedef struct { char name[64]; char exec[256]; char id[33]; } app_t;
static app_t g_apps[MAX_APPS];
static int g_napps;

static void scan_apps(void){
    g_napps = 0;
    {   /* Boomshine is built in: always listed, under the id an installed copy used, so old shortcuts keep working */
        app_t *a = &g_apps[g_napps++];
        md5_hex("boomshine", 9, a->id);
        snprintf(a->name, sizeof a->name, "Boomshine"); snprintf(a->exec, sizeof a->exec, "%s", APP_BUILTIN_BOOMSHINE);
    }
    if(sphere_ui_enabled()){   /* SongSphere too (since 2.2.0): the add-on's id, so its shortcuts keep working */
        app_t *a = &g_apps[g_napps++];
        md5_hex("songsphere", 10, a->id);
        snprintf(a->name, sizeof a->name, "SongSphere"); snprintf(a->exec, sizeof a->exec, "%s", APP_BUILTIN_SPHERE);
    }
    {   /* Album Roulette too (since 2.2.0), under its old folder's id, so shortcuts and pins keep working */
        app_t *a = &g_apps[g_napps++];
        md5_hex("album-roulette", 14, a->id);
        snprintf(a->name, sizeof a->name, "Album Roulette"); snprintf(a->exec, sizeof a->exec, "%s", APP_BUILTIN_ROULETTE);
    }
    {   /* Listening Stats is built in too */
        app_t *a = &g_apps[g_napps++];
        md5_hex("builtin:stats", 13, a->id);
        snprintf(a->name, sizeof a->name, "Listening Stats"); snprintf(a->exec, sizeof a->exec, "%s", APP_BUILTIN_STATS);
    }
    {app_t*a=&g_apps[g_napps++];md5_hex("disc-shelf",10,a->id);snprintf(a->name,sizeof a->name,"Disc Shelf");snprintf(a->exec,sizeof a->exec,"%s",APP_BUILTIN_SHELF);}
    DIR *d = opendir(APPS_DIR);
    if(!d) return;
    struct dirent *de;
    while((de = readdir(d)) && g_napps < MAX_APPS){
        if(de->d_name[0] == '.' || !strcmp(de->d_name, "boomshine") || !strcmp(de->d_name, "songsphere") || !strcmp(de->d_name, "album-roulette") || !strcmp(de->d_name,"disc-shelf")) continue;   /* built in now */   /* an old separate install: the built-in one wins */
        app_t *a = &g_apps[g_napps];
        /* Stable identity is the installation directory, never the display name.
         * A compact digest also fits the 47-byte configuration value limit. */
        md5_hex(de->d_name, strlen(de->d_name), a->id);
        snprintf(a->name, sizeof a->name, "%.60s", de->d_name);
        snprintf(a->exec, sizeof a->exec, APPS_DIR "/%.200s/app", de->d_name);
        char conf[320]; snprintf(conf, sizeof conf, APPS_DIR "/%.200s/app.conf", de->d_name);
        FILE *f = fopen(conf, "r");
        if(f){
            char line[320];
            while(fgets(line, sizeof line, f)){
                char *nl = strpbrk(line, "\r\n"); if(nl) *nl = 0;   /* strip CRLF too (Windows-edited app.conf) */
                if(!strncmp(line, "name=", 5)) snprintf(a->name, sizeof a->name, "%.63s", line+5);
                else if(!strncmp(line, "exec=", 5)) snprintf(a->exec, sizeof a->exec, "%.255s", line+5);
            }
            fclose(f);
        }
        if(access(a->exec, X_OK)) continue;          /* nothing to start (a half-copied install): not listed, as in the Disco menu */
        g_napps++;
    }
    closedir(d);
}

/* ---- Shortcuts (the swipe-left panel from Home) ---------------------------------------------------------
 * Up to five shortcuts orbit a Back hub; each is chosen in Settings > Display > Shortcuts from a fixed list
 * (Weather, Immersive, Equalizer, Folders, Lyrics, Queue, Audiobooks, Search, Battery, Song Info, Last.fm,
 * All apps) plus every homebrew app. "All apps" shows the full app orbit (six to a page, the hub pages). */
#define PER_PAGE 6
#define NSC 5
extern const lv_font_t font_theme_20;
typedef struct { const char *key, *name, *glyph; int big; } scdef_t;          /* big: glyph from the large icon font role */
static const scdef_t SC[] = {
    { "weather",  "Weather",    "\xEF\x86\x85", 1 },
    { "immersive","Immersive",  LV_SYMBOL_IMAGE, 0 },
    { "eq",       "Equalizer",  LV_SYMBOL_BARS, 0 },
    { "folders",  "Folders",    LV_SYMBOL_DIRECTORY, 0 },
    { "lyrics",   "Lyrics",     "\xEF\x85\x9B", 1 },
    { "queue",    "Queue",      LV_SYMBOL_LIST, 0 },
    { "books",    "Audiobooks", LV_SYMBOL_AUDIO, 0 },
    { "search",   "Search",     "\xEF\x80\x82", 2 },                               /* 2: font_theme_20 */
    { "battery",  "Battery",    LV_SYMBOL_BATTERY_3, 0 },
    { "info",     "Song Info",  LV_SYMBOL_EYE_OPEN, 0 },
    { "lastfm",   "Last.fm",    LFM_ICON, 1 },
    { "allapps",  "All apps",   LV_SYMBOL_DRIVE, 0 },
};
#define NSCDEF ((int)(sizeof SC / sizeof SC[0]))
static const char *const SC_DEFAULT[NSC] = { "weather", "eq", "folders", "immersive", "allapps" };
typedef struct { const char *icon; char name[64]; int kind; int idx; int font; } aitem_t;  /* kind: 0 Last.fm, 1 app, 2 Settings, 3 shortcut */
static aitem_t g_items[MAX_APPS + 2];
static int g_nitems, g_page, g_all;          /* g_all: showing All apps instead of the shortcuts */
static lv_obj_t *g_box;
static orbit_t g_orb;
static char g_sckey[NSC][80];
static void build_page(void);
static const char *sc_key(int slot){                                         /* the stored choice ("" = empty) */
    char k[8]; snprintf(k, sizeof k, "sc%d", slot + 1);
    const char *v = simple_cfg_get_str(k, NULL);
    if(v) return v;
    if(th_zune() && slot < 2){   /* Zune: Weather and Equalizer are on the main menu already; offer two built-in apps */
        static char zk[2][40];
        static const char *const ZAPP[2] = { "album-roulette", "boomshine" };
        char id[33]; md5_hex(ZAPP[slot], strlen(ZAPP[slot]), id);
        snprintf(zk[slot], sizeof zk[slot], "appid:%s", id);
        return zk[slot];
    }
    return SC_DEFAULT[slot];
}
static int app_index(const char *name){
    int found = -1;
    for(int i = 0; i < g_napps; i++) if(!strcmp(g_apps[i].name, name)){
        if(found >= 0) return -2; /* legacy name shortcut is ambiguous: ask to reassign */
        found = i;
    }
    return found;
}
static int app_for_key(const char *key){
    if(!strncmp(key, "appid:", 6)){
        for(int i = 0; i < g_napps; i++) if(!strcmp(g_apps[i].id, key + 6)) return i;
        return -1;
    }
    return !strncmp(key, "app:", 4) ? app_index(key + 4) : -1;
}
void shortcut_run(const char *key){
    if(!key || !key[0]) return;
    if(!strncmp(key, "app:", 4) || !strncmp(key, "appid:", 6)){
        int i = app_for_key(key);
        if(i >= 0) app_launch(g_apps[i].exec);
        else ui_toast(i == -2 ? "Reassign this app shortcut" : "That app isn't installed");
        return;
    }
    if(!strcmp(key, "weather")) weather_app_open();
    else if(!strcmp(key, "immersive")){ if(th_disco_family() || th_null()) disco_open_np_immersive(); else { screen_show(SCR_NOWPLAYING); ui_np_fsart_open(); } }   /* Disco: Now Playing redirects to Music unless asked for */
    else if(!strcmp(key, "eq")) screen_show(SCR_EQ);
    else if(!strcmp(key, "folders")) folderbrowser_open();
    else if(!strcmp(key, "lyrics")) lyrics_open();
    else if(!strcmp(key, "queue")) queue_open();
    else if(!strcmp(key, "books")) books_open();
    else if(!strcmp(key, "search")) screen_show(SCR_SEARCH);
    else if(!strcmp(key, "battery")) screen_show(SCR_USAGE);
    else if(!strcmp(key, "info")){ songinfo_unpin(); screen_show(SCR_SONGINFO); }
    else if(!strcmp(key, "lastfm")) lastfm_open();
    else if(!strcmp(key, "allapps")){ g_all = 1; g_page = 0; apps_reload(); }
}
/* label + glyph for a key (built-in or app) */
static int sc_describe(const char *key, const char **glyph, char *name, size_t n, int *font){
    if(!strncmp(key, "app:", 4) || !strncmp(key, "appid:", 6)){
        int i = app_for_key(key);
        *glyph = i>=0&&!strcmp(g_apps[i].exec,APP_BUILTIN_SHELF)?"@shelf":LV_SYMBOL_FILE; *font = 1;
        snprintf(name, n, "%s", i >= 0 ? g_apps[i].name : !strncmp(key, "app:", 4) ? key + 4 : "App unavailable");
        return 1;
    }
    for(int i = 0; i < NSCDEF; i++) if(!strcmp(SC[i].key, key)){ *glyph = SC[i].glyph; *font = SC[i].big; snprintf(name, n, "%s", SC[i].name); return 1; }
    return 0;
}
static void apps_pick(int i){
    int k = (g_all ? g_page * ((th_disco_family() || th_null()) ? 5 : PER_PAGE) : 0) + i;
    if(k < 0 || k >= g_nitems) return;
    switch(g_items[k].kind){
        case 0: lastfm_open(); break;
        case 1: if(g_items[k].idx >= 0 && g_items[k].idx < g_napps) app_launch(g_apps[g_items[k].idx].exec); break;
        case 2: screen_show(SCR_SETTINGS); break;
        case 3: shortcut_run(g_sckey[g_items[k].idx]); break;
    }
}
static void hub_cb(lv_event_t *e){
    (void)e;
    if(!g_all){ screen_back(); return; }
    int per = (th_disco_family() || th_null()) ? 5 : PER_PAGE;
    int pages = (g_nitems + per - 1) / per;
    if(pages > 1 && g_page + 1 < pages){ g_page++; build_page(); return; }
    g_all = 0; g_page = 0; apps_reload();                                  /* All apps: the last page's hub goes back to the shortcuts */
}
static void disco_pick_cb(lv_event_t *e){ apps_pick((int)(intptr_t)lv_event_get_user_data(e)); }
static void disco_more_cb(lv_event_t *e){ (void)e; hub_cb(e); }
/* Disco: the Settings look, five rows in view (All apps pages five at a time; the title row turns the page) */
static void disco_build(void){
    int per = 5;
    int pages = g_all ? (g_nitems + per - 1) / per : 1; if(pages < 1) pages = 1;
    if(g_page >= pages) g_page = 0;
    int first = g_all ? g_page * per : 0, cnt = g_nitems - first; if(cnt > per) cnt = per;
    char t[32];
    if(g_all && pages > 1) snprintf(t, sizeof t, "All apps  %d/%d " LV_SYMBOL_RIGHT, g_page + 1, pages);
    else snprintf(t, sizeof t, "%s", g_all ? "All apps" : "Shortcuts");
    lv_obj_t *tl = disco_title(g_box, t);
    if(g_all){ lv_obj_add_flag(tl, LV_OBJ_FLAG_CLICKABLE); lv_obj_set_ext_click_area(tl, 12); lv_obj_add_event_cb(tl, disco_more_cb, LV_EVENT_CLICKED, NULL); }
    if(cnt < 1){
        lv_obj_t *l = lv_label_create(g_box); lv_label_set_text(l, "Choose shortcuts in\nSettings > Display > Shortcuts");
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0); lv_obj_set_style_text_color(l, TC(TEXT_SECONDARY), 0);
        lv_obj_center(l);
        return;
    }
    for(int i = 0; i < cnt; i++){
        int f = g_items[first + i].font;
        if(th_simple() || th_null()){
            lv_obj_t *r=squash_menu_row(g_box,i,g_items[first+i].icon,f==1?TF(ICON_28):f==2?&font_theme_20:TF(UI_24),g_items[first+i].name,disco_pick_cb,(void *)(intptr_t)i);
            if(!strcmp(g_items[first+i].icon,"@shelf"))disco_vec_icon(lv_obj_get_child(r,0),"@shelf",26);
            continue;
        }
        lv_obj_t *r = disco_row(g_box, 60 + i * 54, 50, g_items[first + i].icon, f == 1 ? TF(ICON_28) : f == 2 ? &font_theme_20 : TF(UI_24),
                                g_items[first + i].name, disco_pick_cb, (void *)(intptr_t)(g_all ? i : i));
        if(!strcmp(g_items[first+i].icon,"@shelf"))disco_vec_icon(lv_obj_get_child(r,0),"@shelf",26);
        lv_obj_update_layout(r);
        lv_obj_t *label=lv_obj_get_child(r,1);
        lv_obj_set_style_text_font(label,ui_font_cjk(20),0);
        lv_obj_set_size(label,lv_obj_get_width(r)-72,lv_font_get_line_height(ui_font_cjk(20)));
        lv_obj_set_style_border_width(r,2,LV_STATE_PRESSED);
        lv_obj_set_style_border_color(r,TC(ACCENT_PRIMARY),LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(r,LV_OPA_COVER,LV_STATE_PRESSED);
    }
}
/* Zune: a pivot ("apps" / "all apps") over the dial list, lower case, every item in one scroll (no pages) */
#include "zune_dial.h"
static void zapps_pick_async(void *u){ apps_pick((int)(intptr_t)u); }
static void zapps_pick(int i){ lv_async_call(zapps_pick_async, (void *)(intptr_t)i); }   /* "all apps" rebuilds the dial: not from its own click */
static void zapps_back_async(void *u){ (void)u; if(g_all){ g_all = 0; g_page = 0; apps_reload(); } else screen_back(); }
static void zapps_back(lv_event_t *e){ (void)e; lv_async_call(zapps_back_async, NULL); }
static void zune_build(void){
    g_page = 0;
    static char lab[ZDIAL_MAX][64]; const char *lb[ZDIAL_MAX];
    int n = g_nitems > ZDIAL_MAX ? ZDIAL_MAX : g_nitems;
    for(int i = 0; i < n; i++){
        snprintf(lab[i], sizeof lab[i], "%s", g_items[i].name);
        for(char *c = lab[i]; *c; c++) if(*c >= 'A' && *c <= 'Z') *c += 32;
        lb[i] = lab[i];
    }
    zune_pivot(g_box, g_all ? "all apps" : "apps", zapps_back);
    zune_status(g_box, SCR_APPS);
    if(n < 1){ zune_empty(g_box, "no shortcuts", "choose them in settings > display > shortcuts");
               lv_obj_align(lv_obj_get_child(g_box, -1), LV_ALIGN_TOP_MID, 0, 110); return; }
    zdial_build(g_box, lb, n, 0, zapps_pick, 100, "apps.zune");
}
/* Braun 2.0: Quick Settings keys - three on the top row, the rest centred below, either side of the centre line - each
 * with its name, a corner lamp lit while pressed and the theme's emboss. All apps pages six at a time (tap the title). */
static void b2_pick_cb(lv_event_t *e){ apps_pick((int)(intptr_t)lv_event_get_user_data(e)); }
static void b2_lamp_cb(lv_event_t *e){
    lv_event_code_t c = lv_event_get_code(e); lv_obj_t *lamp = lv_event_get_user_data(e);
    if(c == LV_EVENT_PRESSED) lv_obj_set_style_bg_color(lamp, TC(ACCENT_PRIMARY), 0);
    else if(c == LV_EVENT_RELEASED || c == LV_EVENT_PRESS_LOST) lv_obj_set_style_bg_color(lamp, TC(CONTROL_TRACK), 0);
}
static void b2_back_cb(lv_event_t *e){ (void)e; if(g_all){ g_all = 0; g_page = 0; apps_reload(); } else screen_back(); }
static void b2_more_cb(lv_event_t *e){ (void)e; int pages = (g_nitems + PER_PAGE - 1) / PER_PAGE; g_page = pages > 1 ? (g_page + 1) % pages : 0; build_page(); }
static void b2_build(void){
    int per = g_all ? PER_PAGE : NSC, pages = g_all ? (g_nitems + PER_PAGE - 1) / PER_PAGE : 1; if(pages < 1) pages = 1;
    if(g_page >= pages) g_page = 0;
    int first = g_page * per, cnt = g_nitems - first; if(cnt > per) cnt = per; if(cnt < 0) cnt = 0;
    char t[32];
    if(g_all && pages > 1) snprintf(t, sizeof t, "All apps %d/%d", g_page + 1, pages); else snprintf(t, sizeof t, "%s", g_all ? "All apps" : "Shortcuts");
    lv_obj_t *tl = ui_header_cb(g_box, t, b2_back_cb);
    if(g_all && pages > 1){ lv_obj_add_flag(tl, LV_OBJ_FLAG_CLICKABLE); lv_obj_set_ext_click_area(tl, 12); lv_obj_add_event_cb(tl, b2_more_cb, LV_EVENT_CLICKED, NULL); }
    if(cnt < 1){
        lv_obj_t *l = lv_label_create(g_box); lv_label_set_text(l, "Choose shortcuts in\nSettings > Display > Shortcuts");
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0); lv_obj_set_style_text_color(l, TC(TEXT_SECONDARY), 0); lv_obj_set_style_text_font(l, br_font(16, 0), 0);
        lv_obj_center(l); kit_keep(l); return;
    }
    int top = cnt > 3 ? 3 : cnt, bottom = cnt - top;
    for(int i = 0; i < cnt; i++){
        int row = i < top ? 0 : 1, n = row ? bottom : top, col = row ? i - top : i;
        int x = 180 + (2 * col - (n - 1)) * 48 - 44, y = cnt > 3 ? (row ? 184 : 96) : 140;
        int f = g_items[first + i].font;
        lv_obj_t *b = lv_button_create(g_box); lv_obj_remove_style_all(b);
        lv_obj_set_pos(b, x, y); lv_obj_set_size(b, 88, 80); lv_obj_set_style_radius(b, 22, 0);
        lv_obj_set_style_bg_color(b, TC(SURFACE_STRONG), 0); lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(b, TC(SURFACE_PRESSED), LV_STATE_PRESSED); br2_emboss(b, 1);
        lv_obj_t *ic = br_label(b, g_items[first + i].icon, f == 1 ? TF(ICON_28) : f == 2 ? &font_theme_20 : TF(UI_22), BR_TXT);
        lv_obj_align(ic, LV_ALIGN_CENTER, 0, -10); lv_obj_set_style_text_color(ic, TC(ACCENT_PRIMARY), 0); br2_depth(b, 0);
        if(!strcmp(g_items[first + i].icon, "@shelf")) disco_vec_icon(ic, "@shelf", 26);
        lv_obj_t *lamp = br_disc(b, 74, 12, 4, BR_SURF); lv_obj_set_style_bg_color(lamp, TC(CONTROL_TRACK), 0);
        lv_obj_t *cap = br_label(b, g_items[first + i].name, br_font(14, 0), BR_TXT2);
        lv_obj_set_width(cap, 80); lv_label_set_long_mode(cap, LV_LABEL_LONG_DOT); lv_obj_set_style_text_align(cap, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(cap, LV_ALIGN_BOTTOM_MID, 0, -8);
        lv_obj_add_event_cb(b, b2_lamp_cb, LV_EVENT_ALL, lamp);
        ui_on(b, b2_pick_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i, "apps.pick", UI_CORE); br2_keep_tree(b);
    }
}
static void build_page(void){
    if(!g_box) return;
    lv_obj_clean(g_box);
    memset(&g_orb, 0, sizeof g_orb);
    if(th_zune()){ zune_build(); return; }
    if(th_braun2()){ b2_build(); return; }
    if((th_disco_family() || th_null())){ disco_build(); return; }
    int per = g_all ? PER_PAGE : NSC;
    int pages = g_all ? (g_nitems + PER_PAGE - 1) / PER_PAGE : 1; if(pages < 1) pages = 1;
    if(g_page >= pages) g_page = 0;
    int first = g_page * per, cnt = g_nitems - first; if(cnt > per) cnt = per;
    orbit_title(g_box, g_all ? "All apps" : "Shortcuts");
    orbit_item_t it[PER_PAGE];
    for(int i = 0; i < cnt; i++){ it[i].glyph = g_items[first + i].icon; it[i].cap = g_items[first + i].name; }
    if(cnt < 1){
        orbit_hub_create(&g_orb, g_box, hub_cb, LV_SYMBOL_LEFT, "Back");
        lv_obj_t *l = lv_label_create(g_box); lv_label_set_text(l, "Choose shortcuts in\nSettings > Display > Shortcuts");
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0); lv_obj_set_style_text_color(l, TC(TEXT_SECONDARY), 0);
        lv_obj_align(l, LV_ALIGN_CENTER, 0, 90);
        return;
    }
    orbit_create(&g_orb, g_box, it, cnt, -90, apps_pick);
    for(int i = 0; i < cnt; i++){
        int f = g_items[first + i].font;
        lv_obj_set_style_text_font(g_orb.icon[i], f == 1 ? TF(ICON_28) : f == 2 ? &font_theme_20 : TF(UI_22), 0);
        lv_obj_set_style_text_color(g_orb.icon[i], ui_current_accent(), 0);
        if(!strcmp(g_items[first+i].icon,"@shelf"))disco_vec_icon(g_orb.icon[i],"@shelf",28);
    }
    orbit_cap_width(&g_orb, 96, ui_font_cjk(14));
    orbit_braun_icons(&g_orb);
    char pg[24]; snprintf(pg, sizeof pg, "%d/%d", g_page + 1, pages);
    if(g_all) orbit_hub_create(&g_orb, g_box, hub_cb, g_page + 1 < pages ? LV_SYMBOL_RIGHT : LV_SYMBOL_LEFT, g_page + 1 < pages ? pg : "Back");
    else orbit_hub_create(&g_orb, g_box, hub_cb, LV_SYMBOL_LEFT, "Back");
}

void apps_reload(void){
    if(!g_box) return;
    scan_apps();
    g_nitems = 0;
    if(!g_all){                                                            /* the shortcuts */
        for(int s2 = 0; s2 < NSC; s2++){
            const char *k = sc_key(s2); const char *gl; int f;
            snprintf(g_sckey[s2], sizeof g_sckey[s2], "%s", k);
            if(!k[0] || !sc_describe(k, &gl, g_items[g_nitems].name, sizeof g_items[0].name, &f)) continue;
            g_items[g_nitems].icon = gl; g_items[g_nitems].font = f; g_items[g_nitems].kind = 3; g_items[g_nitems].idx = s2; g_nitems++;
        }
        build_page(); return;
    }
    /* All apps: Last.fm, the homebrew apps, Settings */
    g_items[g_nitems].icon = LFM_ICON; g_items[g_nitems].font = 1; snprintf(g_items[g_nitems].name, sizeof g_items[0].name, "Last.fm"); g_items[g_nitems].kind = 0; g_items[g_nitems].idx = -1; g_nitems++;
    for(int i = 0; i < g_napps && g_nitems < MAX_APPS + 1; i++){
        g_items[g_nitems].icon = !strcmp(g_apps[i].exec,APP_BUILTIN_SHELF)?"@shelf":LV_SYMBOL_FILE; g_items[g_nitems].font = 1; snprintf(g_items[g_nitems].name, sizeof g_items[0].name, "%.63s", g_apps[i].name);
        g_items[g_nitems].kind = 1; g_items[g_nitems].idx = i; g_nitems++;
    }
    g_items[g_nitems].icon = LV_SYMBOL_SETTINGS; g_items[g_nitems].font = 1; snprintf(g_items[g_nitems].name, sizeof g_items[0].name, "Settings"); g_items[g_nitems].kind = 2; g_items[g_nitems].idx = -1; g_nitems++;
    build_page();
}

void apps_create(lv_obj_t *root){
    lv_obj_set_style_bg_color(root, TC(CANVAS), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    if(th_braun_family()) br_face(root);
    g_box = lv_obj_create(root);
    lv_obj_remove_style_all(g_box);
    lv_obj_set_size(g_box, 360, 360);
    lv_obj_clear_flag(g_box, LV_OBJ_FLAG_SCROLLABLE);
    g_all = 0;
    apps_reload();
}

lv_obj_t *apps_scroller(void){ return NULL; }      /* an orbit has no long list */

/* ---- Settings > Display > Shortcuts: five slots; tap one to pick what it opens ----------------------------- */
static lv_obj_t *g_cfg_root, *g_cfg_list, *g_cfg_title;
static int g_cfg_slot = -1;                                                  /* -1: the slot list; 0..4: picking for a slot */
static char g_opt_keys[NSCDEF + MAX_APPS + 1][80];
static void cfg_build(void);
static void cfg_row(const char *left, const char *right, lv_event_cb_t cb, intptr_t ud, int on){
    lv_obj_t *r = lv_button_create(g_cfg_list);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, 260, 46);
    lv_obj_add_flag(r, LV_OBJ_FLAG_USER_1);
    lv_obj_set_style_radius(r, TH_R_ROW, 0);
    lv_obj_set_style_bg_color(r, on ? TC(SURFACE_RAISED) : TC(SURFACE), 0); lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(r, TC(SURFACE_RAISED), LV_STATE_PRESSED);
    lv_obj_add_event_cb(r, cb, LV_EVENT_CLICKED, (void *)ud);
    lv_obj_t *l = lv_label_create(r); lv_label_set_text(l, left); lv_label_set_long_mode(l, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(l, right ? 120 : 220);
    lv_obj_set_style_text_font(l, ui_font_cjk(16), 0); lv_obj_set_style_text_color(l, on ? ui_current_accent() : TC(TEXT_PRIMARY), 0);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 16, 0);
    if(right){ lv_obj_t *v = lv_label_create(r); lv_label_set_text(v, right); lv_label_set_long_mode(v, LV_LABEL_LONG_SCROLL_CIRCULAR);
               lv_obj_set_width(v, 110); lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_RIGHT, 0);
               lv_obj_set_style_text_font(v, ui_font_cjk(14), 0); lv_obj_set_style_text_color(v, TC(TEXT_SECONDARY), 0);
               lv_obj_align(v, LV_ALIGN_RIGHT_MID, -14, 0); }
}
static void slot_cb(lv_event_t *e){ g_cfg_slot = (int)(intptr_t)lv_event_get_user_data(e); cfg_build(); }
static void opt_cb(lv_event_t *e){
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    char k[8]; snprintf(k, sizeof k, "sc%d", g_cfg_slot + 1);
    if(simple_cfg_set_str(k, i < 0 ? "" : g_opt_keys[i]) != 0){ ui_toast("Couldn't save shortcut"); return; }
    g_cfg_slot = -1; cfg_build();
    if(g_box){ g_all = 0; apps_reload(); }
}
static void cfg_back_cb(lv_event_t *e){ (void)e; if(g_cfg_slot >= 0){ g_cfg_slot = -1; cfg_build(); } else screen_back(); }
static void cfg_build(void){
    if(!g_cfg_list) return;
    lv_obj_clean(g_cfg_list);
    if(g_cfg_slot < 0){
        lv_label_set_text(g_cfg_title, "Shortcuts");
        for(int s2 = 0; s2 < NSC; s2++){
            const char *k = sc_key(s2), *gl; char nm[64] = "Empty"; int f;
            if(k[0]) sc_describe(k, &gl, nm, sizeof nm, &f);
            char left[16]; snprintf(left, sizeof left, "Shortcut %d", s2 + 1);
            cfg_row(left, nm, slot_cb, s2, 0);
        }
        return;
    }
    char t[24]; snprintf(t, sizeof t, "Shortcut %d", g_cfg_slot + 1); lv_label_set_text(g_cfg_title, t);
    scan_apps();
    const char *cur = sc_key(g_cfg_slot);
    cfg_row("Empty", NULL, opt_cb, -1, !cur[0]);
    int n = 0;
    for(int i = 0; i < NSCDEF; i++){ snprintf(g_opt_keys[n], sizeof g_opt_keys[0], "%s", SC[i].key); cfg_row(SC[i].name, NULL, opt_cb, n, !strcmp(cur, SC[i].key)); n++; }
    for(int i = 0; i < g_napps && n < (int)(sizeof g_opt_keys / sizeof g_opt_keys[0]); i++){
        snprintf(g_opt_keys[n], sizeof g_opt_keys[0], "appid:%s", g_apps[i].id);
        cfg_row(g_apps[i].name, "App", opt_cb, n, app_for_key(cur) == i); n++;
    }
    lv_obj_scroll_to_y(g_cfg_list, 0, LV_ANIM_OFF);
}
void shortcuts_config_create(lv_obj_t *root){
    g_cfg_root = root;
    lv_obj_set_style_bg_color(root, TC(CANVAS), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    g_cfg_title = ui_header_cb(root, "Shortcuts", cfg_back_cb);
    g_cfg_list = lv_obj_create(root);
    lv_obj_remove_style_all(g_cfg_list);
    lv_obj_set_pos(g_cfg_list, 50, 70); lv_obj_set_size(g_cfg_list, 260, 280);
    lv_obj_set_style_pad_row(g_cfg_list, 6, 0); lv_obj_set_style_pad_bottom(g_cfg_list, 40, 0);
    lv_obj_set_flex_flow(g_cfg_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(g_cfg_list, LV_DIR_VER); lv_obj_set_scrollbar_mode(g_cfg_list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(g_cfg_list, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    if(th_braun_family()){ br_face(root); lv_obj_move_to_index(br_segment(root, 64), 1); curvelist_braun_watch(g_cfg_list); }
    cfg_build();
}
void shortcuts_config_refresh(void){ g_cfg_slot = -1; cfg_build(); }
