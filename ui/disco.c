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
#include "theme.h"
#include "theme_kit.h"
#include "config.h"
int search_landing_active(void);
#include "folderbrowser.h"
#include "books.h"
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

extern const lv_font_t font_theme_20;

/* ---- the menu items ------------------------------------------------------------------------------- */
typedef struct { const char *id, *name, *icon; int screen; void (*open)(void); int prot; } dsec_t;
static void open_books(void){ books_open(); }
static void open_folders(void){ folderbrowser_open(); }
#define IC_MIC "\xEF\x84\xB0"                     /* f130: Artists (the icon font) */
static void open_artists(void){ library_open_artists(); screen_section(SCR_LIBRARY, +1); }
static void open_songs(void){ library_open_songs(); screen_section(SCR_LIBRARY, +1); }
static void open_queue(void){ queue_open(); }
static const dsec_t SEC[] = {
    { "music",     "Music",     LV_SYMBOL_AUDIO,     SCR_HOME,      NULL,              1 },
    { "library",   "Library",   LV_SYMBOL_LIST,      SCR_LIBRARY,   NULL,              0 },
    { "settings",  "Settings",  LV_SYMBOL_SETTINGS,  SCR_SETTINGS,  NULL,              1 },
    { "modes",     "Modes",     LV_SYMBOL_USB,       SCR_WORKMODE,  NULL,              0 },
    { "shortcuts", "Shortcuts", LV_SYMBOL_BARS,      SCR_APPS,      NULL,              0 },
    { "eq",        "EQ",        "EQ",                SCR_EQ,        NULL,              0 },
    { "search",    "Search",    TH_IC_SEARCH,        SCR_SEARCH,    NULL,              0 },
    { "albums",    "Albums",    LV_SYMBOL_IMAGE,     SCR_ALBUMWALL, NULL,              0 },
    { "queue",     "Up Next",   LV_SYMBOL_NEXT,      SCR_UPNEXT,    NULL,              0 },
    { "folders",   "Folders",   LV_SYMBOL_DIRECTORY, SCR_FOLDER,    open_folders,      0 },
    { "books",     "Books",     LV_SYMBOL_FILE,      SCR_BOOKS,     open_books,        0 },
    { "artists",   "Artists",   IC_MIC,              SCR_LIBRARY,   open_artists,      0 },
    { "songs",     "Songs",     LV_SYMBOL_PLAY,      SCR_LIBRARY,   open_songs,        0 },
    { "myqueue",   "Queue",     LV_SYMBOL_PASTE,     SCR_QUEUE,     open_queue,        0 },
    { "weather",   "Weather",   LV_SYMBOL_IMAGE,     SCR_WEATHER,   weather_app_open,  0 },
};
#define NSEC ((int)(sizeof SEC / sizeof SEC[0]))
#define MAXM 10                                    /* the picker holds at most ten (the dots must fit) */
static int g_menu[MAXM], g_nmenu, g_cur;           /* active items (indices into SEC), the current one */

static int sec_find(const char *id, size_t n){ for(int i = 0; i < NSEC; i++) if(strlen(SEC[i].id) == n && !strncmp(SEC[i].id, id, n)) return i; return -1; }
static int menu_has_screen(int scr);
static int in_menu(int s){ for(int i = 0; i < g_nmenu; i++) if(g_menu[i] == s) return i; return -1; }
static int menu_has_screen(int scr){ for(int i = 0; i < g_nmenu; i++) if(SEC[g_menu[i]].screen == scr) return 1; return 0; }
/* parse cfg "disco_menu": unknown and duplicate ids are skipped, missing protected ones come back (never locked out) */
static const char CODE[] = "mlsoceFaudbwrgq";                 /* one letter per SEC[] entry, same order */
static void menu_load(void){
    char buf[256]; snprintf(buf, sizeof buf, "%s", cfg_get_str("disco_menu", "music,library,settings,modes,shortcuts,eq"));
    g_nmenu = 0;
    const char *codes = cfg_get_str("disco_mnu", "");
    if(codes && codes[0]){                                        /* the compact form (since 1.2.5) */
        for(const char *p = codes; *p && g_nmenu < MAXM; p++){ const char *c = strchr(CODE, *p); int s = c ? (int)(c - CODE) : -1;
            if(s >= 0 && s < NSEC && in_menu(s) < 0) g_menu[g_nmenu++] = s; }
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
static void menu_save(void){
    char buf[MAXM + 1]; int n = 0;
    for(int i = 0; i < g_nmenu && n < MAXM; i++) buf[n++] = CODE[g_menu[i]];
    buf[n] = 0;
    cfg_set_str("disco_mnu", buf);
}

/* ---- backdrop: the blurred cover, tinted once per album ------------------------------------------- */
#define BDW 360
static uint8_t g_bd[BDW * BDW * 4];
static lv_image_dsc_t g_bd_dsc;
static int g_bd_ok;
static lv_obj_t *g_parent;
static int no_backdrop(int which){
    return which == SCR_HOME || which == SCR_NOWPLAYING || which == SCR_SAVER || which == SCR_QUICK;
}
static void backdrop_apply(void){
    for(int i = 0; i < SCR_COUNT; i++){
        lv_obj_t *r = screen_get_root(i);
        if(!r || no_backdrop(i)) continue;
        lv_obj_set_style_bg_image_src(r, NULL, 0);                    /* same buffer, new pixels: force a redraw */
        if(g_bd_ok) lv_obj_set_style_bg_image_src(r, &g_bd_dsc, 0);
    }
}
static void backdrop_bake(void){
    const lv_image_dsc_t *src = (const lv_image_dsc_t *)ui_current_backdrop_img();
    g_bd_ok = 0;
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
    if((g_sheen_scr == SCR_HOME || g_sheen_scr == SCR_PLVIEW) && cfg_get_int("disco_sheen", 1)){ lv_obj_remove_flag(g_sheen, LV_OBJ_FLAG_HIDDEN); lv_obj_move_foreground(g_sheen); }
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
static int g_press_y, g_long;
static int picker_wanted(int which){
    switch(which){
        case SCR_HOME: case SCR_LIBRARY: case SCR_SETTINGS: case SCR_SETLIST: case SCR_WORKMODE: case SCR_APPS:
        case SCR_PLVIEW: case SCR_FOLDER: case SCR_BOOKS: case SCR_ALBUMWALL: case SCR_UPNEXT: case SCR_WIFI:
        case SCR_BT: case SCR_WEATHER: case SCR_MODEINFO: case SCR_USAGE: case SCR_EQ: return 1;
        case SCR_SEARCH: return search_landing_active();             /* the landing page has the menu; the keyboard doesn't */
        default: return 0;
    }
}
static void picker_paint(void){
    if(!g_pk || g_nmenu <= 0) return;
    const dsec_t *s = &SEC[g_menu[g_cur]];
    lv_color_t acc = ui_current_accent();
    lv_label_set_text(g_pk_icon, s->icon);
    lv_obj_set_style_text_font(g_pk_icon, !strcmp(s->icon, "EQ") ? TF(UI_28) : !strcmp(s->icon, IC_MIC) ? TF(ICON_28) : (!strcmp(s->icon, TH_IC_SEARCH) ? &font_theme_24 : TF(UI_36)), 0);
    lv_obj_set_style_outline_color(g_pk_glow, acc, 0); lv_obj_set_style_border_color(g_pk_glow, acc, 0);   /* the glow follows the accent */
    lv_obj_set_style_bg_color(g_pk_glow, acc, 0);
    lv_label_set_text(g_pk_name, s->name);
    if(g_tab_icon){ lv_label_set_text(g_tab_icon, s->icon);
                    lv_obj_set_style_text_font(g_tab_icon, !strcmp(s->icon, "EQ") ? TF(UI_12) : !strcmp(s->icon, IC_MIC) ? TF(ICON_20) : (!strcmp(s->icon, TH_IC_SEARCH) ? &font_theme_20 : TF(UI_18)), 0);
                    lv_obj_set_style_border_color(g_tab, acc, 0); }
    for(int i = 0; i < MAXM; i++){
        if(!g_pk_dot[i]) continue;
        if(i >= g_nmenu){ lv_obj_add_flag(g_pk_dot[i], LV_OBJ_FLAG_HIDDEN); continue; }
        lv_obj_remove_flag(g_pk_dot[i], LV_OBJ_FLAG_HIDDEN);
        float span = g_nmenu > 1 ? 64.0f : 0.0f;                         /* curved along the circle's left edge */
        float a = (180.0f + span / 2 - (g_nmenu > 1 ? span * i / (g_nmenu - 1) : 0)) * 0.0174533f;   /* first item (Music) at the top */
        int on = i == g_cur, sz = on ? 10 : 7, r = PK_D / 2 - 16;
        lv_obj_set_size(g_pk_dot[i], sz, sz);
        lv_obj_set_pos(g_pk_dot[i], PK_D / 2 + (int)lroundf(r * cosf(a)) - sz / 2, PK_D / 2 + (int)lroundf(r * sinf(a)) - sz / 2);
        lv_obj_set_style_bg_color(g_pk_dot[i], on ? acc : TC(TEXT_MUTED), 0);
    }
}
static void go_section(int idx, int dir){
    if(g_nmenu <= 0) return;
    idx = (idx % g_nmenu + g_nmenu) % g_nmenu;
    g_cur = idx; picker_paint();
    const dsec_t *s = &SEC[g_menu[idx]];
    if(s->open){ s->open(); return; }                                  /* screens that need their own refresh */
    screen_section(s->screen, dir);
}
static void picker_cb(lv_event_t *e){
    lv_event_code_t c = lv_event_get_code(e);
    lv_indev_t *in = lv_indev_active(); lv_point_t p = { 0, 0 };
    if(in) lv_indev_get_point(in, &p);
    if(c == LV_EVENT_PRESSED){ g_press_y = p.y; g_long = 0; return; }
    if(c == LV_EVENT_LONG_PRESSED){ g_long = 1; g_cur = 0; for(int i = 0; i < g_nmenu; i++) if(g_menu[i] == 0) g_cur = i;
                                     picker_paint(); screen_home(); return; }   /* hold: back to Music */
    if(c != LV_EVENT_RELEASED || g_long) return;
    int dy = p.y - g_press_y;
    if(dy < -24){ go_section(g_cur + 1, +1); return; }                 /* swipe up: next */
    if(dy > 24){ go_section(g_cur - 1, -1); return; }                  /* swipe down: previous */
    int rel = g_press_y - PK_Y;
    if(rel < PK_D / 3) go_section(g_cur - 1, -1);                      /* top: previous */
    else if(rel > PK_D * 2 / 3) go_section(g_cur + 1, +1);             /* bottom: next */
    else go_section(g_cur, +1);                                         /* middle: the section's top screen */
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
#define PK_SLIDE_MS 150
static void slide_cb(void *o, int32_t v){ lv_obj_set_x((lv_obj_t *)o, v); }
static void slide_in_done(lv_anim_t *a){ (void)a; if(!g_open) nav_show(); }   /* closed: hide the oval, show the shard */
void disco_nav_set_open(int open){
    if(!g_pk) return;
    lv_anim_delete(g_pk, slide_cb);
    int was = g_open; g_open = open ? 1 : 0;
    lv_anim_t a; lv_anim_init(&a); lv_anim_set_var(&a, g_pk); lv_anim_set_exec_cb(&a, slide_cb);
    lv_anim_set_duration(&a, PK_SLIDE_MS);
    if(g_open){
        nav_show();
        lv_anim_set_values(&a, was ? lv_obj_get_x(g_pk) : 360, PK_X); lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
        lv_anim_start(&a);
    } else {
        lv_obj_add_flag(g_catch, LV_OBJ_FLAG_HIDDEN);                    /* the screen answers touches again at once */
        if(!was || lv_obj_has_flag(g_pk, LV_OBJ_FLAG_HIDDEN)){ nav_show(); return; }
        lv_anim_set_values(&a, lv_obj_get_x(g_pk), 360); lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
        lv_anim_set_completed_cb(&a, slide_in_done);
        lv_anim_start(&a);
    }
}
static void catch_cb(lv_event_t *e){ (void)e; disco_nav_set_open(0); }
static void tab_cb(lv_event_t *e){ (void)e; disco_nav_set_open(1); }
static void picker_create(void){
    g_pk = lv_obj_create(g_parent);
    lv_obj_remove_style_all(g_pk);
    lv_obj_set_pos(g_pk, PK_X, PK_Y); lv_obj_set_size(g_pk, PK_W, PK_D);
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
    lv_obj_set_size(g_pk_glow, 50, 50); lv_obj_align(g_pk_glow, LV_ALIGN_CENTER, -38, -16);
    lv_obj_set_style_radius(g_pk_glow, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(g_pk_glow, 55, 0);
    lv_obj_set_style_outline_width(g_pk_glow, 10, 0); lv_obj_set_style_outline_opa(g_pk_glow, 30, 0); lv_obj_set_style_outline_pad(g_pk_glow, 0, 0);
    lv_obj_set_style_border_width(g_pk_glow, 6, 0); lv_obj_set_style_border_opa(g_pk_glow, 25, 0);
    lv_obj_clear_flag(g_pk_glow, LV_OBJ_FLAG_CLICKABLE);
    g_pk_icon = lv_label_create(g_pk); lv_obj_set_style_text_color(g_pk_icon, white, 0); lv_obj_align(g_pk_icon, LV_ALIGN_CENTER, -38, -16);
    g_pk_name = lv_label_create(g_pk); lv_obj_set_style_text_color(g_pk_name, white, 0); lv_obj_set_style_text_font(g_pk_name, TF(UI_20), 0);
    lv_obj_align(g_pk_name, LV_ALIGN_CENTER, -38, 26);
    lv_obj_t *u = lv_label_create(g_pk); lv_label_set_text(u, LV_SYMBOL_UP); lv_obj_set_style_text_font(u, TF(UI_16), 0);
    lv_obj_set_style_text_color(u, grey, 0); lv_obj_align(u, LV_ALIGN_TOP_MID, -38, 12);
    {   /* a collapse arrow at the far right, against the rim: tapping it closes the circle (as a tap outside does) */
        lv_obj_t *cb = lv_obj_create(g_pk); lv_obj_remove_style_all(cb);
        lv_obj_set_size(cb, 26, 76); lv_obj_set_pos(cb, 360 - PK_X - 27, PK_D / 2 - 38);
        lv_obj_set_style_radius(cb, 17, 0); lv_obj_set_style_bg_color(cb, white, 0); lv_obj_set_style_bg_opa(cb, 0, 0);
        lv_obj_set_style_bg_opa(cb, 40, LV_STATE_PRESSED);
        lv_obj_add_flag(cb, LV_OBJ_FLAG_CLICKABLE); lv_obj_clear_flag(cb, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_add_event_cb(cb, catch_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_t *cl = lv_label_create(cb); lv_label_set_text(cl, LV_SYMBOL_RIGHT); lv_obj_set_style_text_font(cl, TF(UI_16), 0);
        lv_obj_set_style_text_color(cl, grey, 0); lv_obj_align(cl, LV_ALIGN_CENTER, 1, 0);
    }
    lv_obj_t *d = lv_label_create(g_pk); lv_label_set_text(d, LV_SYMBOL_DOWN); lv_obj_set_style_text_font(d, TF(UI_16), 0);
    lv_obj_set_style_text_color(d, grey, 0); lv_obj_align(d, LV_ALIGN_BOTTOM_MID, -38, -12);
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
    lv_obj_set_pos(g_tab, TAB_X, TAB_Y); lv_obj_set_size(g_tab, TAB_D, TAB_D);
    lv_obj_set_style_radius(g_tab, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(g_tab, TC(SCRIM), 0); lv_obj_set_style_bg_opa(g_tab, 190, 0);
    lv_obj_set_style_bg_opa(g_tab, 235, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(g_tab, 2, 0); lv_obj_set_style_border_opa(g_tab, 130, 0);   /* a thin accent rim (a shadow is redrawn uncached on every frame) */
    lv_obj_set_ext_click_area(g_tab, 8);
    lv_obj_clear_flag(g_tab, LV_OBJ_FLAG_SCROLLABLE); lv_obj_add_flag(g_tab, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(g_tab, tab_cb, LV_EVENT_CLICKED, NULL);
    g_tab_icon = lv_label_create(g_tab); lv_obj_set_style_text_color(g_tab_icon, white, 0);
    lv_obj_align(g_tab_icon, LV_ALIGN_LEFT_MID, 10, 0);                 /* in the visible sliver */
    picker_paint();
}

/* lists keep their rows clear of the circle: a row whose height overlaps it starts right of the circle's edge there */
int disco_clear_left(int y1, int y2){
    if(!g_pk) return 0;
    return 0;                                                           /* the circle is now a pop-in overlay: lists keep their width */
    int cy = PK_Y + PK_D / 2, r = PK_D / 2;
    int dy = (y1 <= cy && y2 >= cy) ? 0 : (y2 < cy ? cy - y2 : y1 - cy);
    if(dy >= r) return 0;
    return PK_X + r + (int)sqrtf((float)(r * r - dy * dy)) + 8;
}
/* lists keep their rows' right end clear of the closed sliver (the open circle is an overlay: it doesn't count) */
int disco_clear_right(int y1, int y2){
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
void disco_open_np_immersive(void){ g_np_want = 1; screen_show(SCR_NOWPLAYING); g_np_want = 0; ui_np_fsart_open(); }

void disco_init(lv_obj_t *parent){
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
void disco_nav_refresh(void){ if(g_pk) nav_show(); }
static void ed_close(void);
void dhome_overlay_close(void);
int screen_popping(void);
void disco_screen_entering(int which){
    if(which == SCR_SEARCH && !screen_popping()) search_landing_reset();  /* Menu > Search: the landing page first; back keeps results */
    dhome_overlay_close(); ed_close();                                    /* top-layer panels never outlive their screen (standby, keys) */
    if(!g_pk) return;
    g_cur_scr = which; nav_show();
}
void disco_screen_entered(int which, lv_obj_t *root){
    if(!g_parent) return;
    int sec_scr = which;                                                /* sub-screens keep their section's heading */
    if(which == SCR_SETLIST || which == SCR_SETTING_DETAIL) sec_scr = SCR_SETTINGS;
    else if(which == SCR_PLVIEW || ((which == SCR_ALBUMWALL || which == SCR_FOLDER || which == SCR_BOOKS) && !menu_has_screen(which))) sec_scr = SCR_LIBRARY;
    else if(which == SCR_MODEINFO) sec_scr = SCR_WORKMODE;
    if(!(g_cur < g_nmenu && SEC[g_menu[g_cur]].screen == sec_scr))      /* Library / Artists / Songs share a screen: keep the one picked */
        for(int i = 0; i < g_nmenu; i++) if(SEC[g_menu[i]].screen == sec_scr){ if(g_cur != i){ g_cur = i; picker_paint(); } break; }
    if(root && picker_wanted(which)){ lv_obj_update_layout(root); recurve(root, 0); }
    if(root && !no_backdrop(which)){
        lv_opa_t o = theme_variant() ? 170 : 150;
        glass_walk(root, theme_rgb(THEME_CLR_SURFACE), theme_rgb(THEME_CLR_SURFACE_RAISED), o, 0);
    }
    g_sheen_scr = which;
    if(g_sheen){ lv_obj_add_flag(g_sheen, LV_OBJ_FLAG_HIDDEN);
                 if((which == SCR_HOME || which == SCR_PLVIEW) && cfg_get_int("disco_sheen", 1) && !g_sh_tm){
                     g_sh_tm = lv_timer_create(sheen_show_cb, 260, NULL); if(g_sh_tm) lv_timer_set_repeat_count(g_sh_tm, 1); } }
    if(g_pk){ g_cur_scr = which; picker_paint(); nav_show(); }
}
void disco_art_changed(void){
    if(!th_disco() || !g_parent) return;
    backdrop_bake(); backdrop_apply();
    dhome_art_changed();
    picker_paint();                                                     /* the current dot follows the accent */
}

/* ---- Settings > Display > Disco Menu: choose, order, add and remove the picker's sections ----------- */
static lv_obj_t *g_ed, *g_ed_list;
static void ed_build(void);
static void ed_build_async(void *p){ (void)p; ed_build(); }
static void ed_close(void){ if(g_ed){ lv_obj_delete(g_ed); g_ed = NULL; g_ed_list = NULL; picker_paint(); } }
static void ed_close_cb(lv_event_t *e){ if(lv_event_get_code(e) == LV_EVENT_CLICKED) ed_close(); }
static void ed_act_cb(lv_event_t *e){
    if(lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    intptr_t v = (intptr_t)lv_event_get_user_data(e);
    int s = (int)(v & 0xFF), act = (int)(v >> 8), i = in_menu(s);
    if(act == 1 && i > 0){ int t = g_menu[i - 1]; g_menu[i - 1] = g_menu[i]; g_menu[i] = t; }             /* up */
    else if(act == 2 && i >= 0 && i < g_nmenu - 1){ int t = g_menu[i + 1]; g_menu[i + 1] = g_menu[i]; g_menu[i] = t; }   /* down */
    else if(act == 3 && i >= 0 && !SEC[s].prot){ memmove(g_menu + i, g_menu + i + 1, sizeof(int) * (size_t)(g_nmenu - i - 1)); g_nmenu--; }   /* remove */
    else if(act == 4 && i < 0){ if(g_nmenu >= MAXM){ ui_toast("The menu holds up to ten"); return; } g_menu[g_nmenu++] = s; }   /* add */
    else return;
    if(g_cur >= g_nmenu) g_cur = 0;
    menu_save();
    lv_async_call(ed_build_async, NULL);                        /* rebuild after this click finishes */
}
static lv_obj_t *ed_btn(lv_obj_t *row, int x, const char *glyph, int s, int act, int dim){
    lv_obj_t *b = lv_button_create(row); lv_obj_remove_style_all(b);
    lv_obj_set_size(b, 34, 34); lv_obj_set_pos(b, x, 6);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(b, TC(SURFACE_RAISED), 0); lv_obj_set_style_bg_opa(b, dim ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, TC(CONTROL_TRACK), LV_STATE_PRESSED);
    lv_obj_t *l = lv_label_create(b); lv_label_set_text(l, glyph); lv_obj_set_style_text_font(l, TF(UI_14), 0);
    lv_obj_set_style_text_color(l, dim ? TC(TEXT_DISABLED) : TC(TEXT_PRIMARY), 0); lv_obj_center(l);
    if(!dim) lv_obj_add_event_cb(b, ed_act_cb, LV_EVENT_CLICKED, (void *)(intptr_t)(s | (act << 8)));
    else lv_obj_clear_flag(b, LV_OBJ_FLAG_CLICKABLE);
    return b;
}
static void ed_row(int s, int pos){
    lv_obj_t *r = lv_obj_create(g_ed_list); lv_obj_remove_style_all(r);
    lv_obj_set_size(r, 276, 46);
    lv_obj_set_style_radius(r, TH_R_ROW, 0);
    lv_obj_set_style_bg_color(r, TC(SURFACE), 0); lv_obj_set_style_bg_opa(r, pos >= 0 ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *ic = lv_label_create(r); lv_label_set_text(ic, SEC[s].icon);
    lv_obj_set_style_text_font(ic, !strcmp(SEC[s].icon, TH_IC_SEARCH) ? &font_theme_20 : !strcmp(SEC[s].icon, IC_MIC) ? TF(ICON_20) : (!strcmp(SEC[s].icon, "EQ") ? TF(UI_14) : TF(UI_18)), 0);
    lv_obj_set_style_text_color(ic, pos >= 0 ? ui_current_accent() : TC(TEXT_MUTED), 0); lv_obj_align(ic, LV_ALIGN_LEFT_MID, 12, 0);
    lv_obj_t *nm = lv_label_create(r); lv_label_set_text(nm, SEC[s].name);
    lv_obj_set_style_text_font(nm, TF(UI_16), 0);
    lv_obj_set_style_text_color(nm, pos >= 0 ? TC(TEXT_PRIMARY) : TC(TEXT_SECONDARY), 0); lv_obj_align(nm, LV_ALIGN_LEFT_MID, 44, 0);
    if(pos < 0){ ed_btn(r, 234, LV_SYMBOL_PLUS, s, 4, 0); return; }
    ed_btn(r, 150, LV_SYMBOL_UP, s, 1, pos == 0);
    ed_btn(r, 188, LV_SYMBOL_DOWN, s, 2, pos == g_nmenu - 1);
    if(SEC[s].prot){ lv_obj_t *lk = lv_label_create(r); lv_label_set_text(lk, LV_SYMBOL_EYE_OPEN); lv_obj_set_style_text_font(lk, TF(UI_14), 0);
                     lv_obj_set_style_text_color(lk, TC(TEXT_DISABLED), 0); lv_obj_set_pos(lk, 244, 14); }   /* always shown */
    else ed_btn(r, 234, LV_SYMBOL_CLOSE, s, 3, 0);
}
static void ed_build(void){
    if(!g_ed) return;
    int32_t sy = lv_obj_get_scroll_y(g_ed_list);
    lv_obj_clean(g_ed_list);
    for(int i = 0; i < g_nmenu; i++) ed_row(g_menu[i], i);
    lv_obj_t *h = lv_label_create(g_ed_list); lv_label_set_text(h, "Add to the menu");
    lv_obj_set_style_text_font(h, TF(UI_14), 0); lv_obj_set_style_text_color(h, TC(TEXT_MUTED), 0);
    for(int s = 0; s < NSEC; s++) if(in_menu(s) < 0) ed_row(s, -1);
    lv_obj_t *pad = lv_obj_create(g_ed_list); lv_obj_remove_style_all(pad); lv_obj_set_size(pad, 10, 40);
    lv_obj_update_layout(g_ed_list); lv_obj_scroll_to_y(g_ed_list, sy, LV_ANIM_OFF);
}
void disco_menu_open(void){
    if(!th_disco()){ ui_toast("Disco Menu is part of the Disco theme"); return; }
    if(g_ed) return;                                                    /* already open */
    menu_load();
    g_ed = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(g_ed);
    lv_obj_set_size(g_ed, 360, 360);
    lv_obj_set_style_bg_color(g_ed, TC(CANVAS), 0); lv_obj_set_style_bg_opa(g_ed, LV_OPA_COVER, 0);
    if(g_bd_ok) lv_obj_set_style_bg_image_src(g_ed, &g_bd_dsc, 0);
    lv_obj_add_flag(g_ed, LV_OBJ_FLAG_CLICKABLE); lv_obj_clear_flag(g_ed, LV_OBJ_FLAG_SCROLLABLE);
    ui_header_cb(g_ed, "Disco Menu", ed_close_cb);
    lv_obj_t *hint = lv_label_create(g_ed); lv_label_set_text(hint, "Music and Settings always stay");
    lv_obj_set_style_text_font(hint, TF(UI_12), 0); lv_obj_set_style_text_color(hint, TC(TEXT_MUTED), 0);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 64);
    g_ed_list = lv_obj_create(g_ed); lv_obj_remove_style_all(g_ed_list);
    lv_obj_set_pos(g_ed_list, 42, 86); lv_obj_set_size(g_ed_list, 276, 274);
    lv_obj_set_flex_flow(g_ed_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(g_ed_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(g_ed_list, 6, 0);
    lv_obj_set_scroll_dir(g_ed_list, LV_DIR_VER); lv_obj_set_scrollbar_mode(g_ed_list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(g_ed_list, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    ed_build();
    kit_pass(g_ed);
}

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
    int sl = sliver_left(y, y + h) - 8; if(right > sl) right = sl;
    lv_obj_t *r = lv_obj_create(root); lv_obj_remove_style_all(r);
    lv_obj_set_pos(r, left, y); lv_obj_set_size(r, right - left, h);
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
    lv_label_set_long_mode(n, LV_LABEL_LONG_DOT); lv_obj_set_width(n, right - left - 96);
    lv_obj_align(n, LV_ALIGN_LEFT_MID, 52, 0);
    lv_obj_t *v = lv_label_create(r); lv_label_set_text(v, "");
    lv_obj_set_style_text_font(v, TH_F_DETAIL, 0); lv_obj_set_style_text_color(v, TC(TEXT_SECONDARY), 0);
    lv_obj_align(v, LV_ALIGN_RIGHT_MID, -16, 0);
    return r;
}
lv_obj_t *disco_title(lv_obj_t *root, const char *t){
    lv_obj_t *l = lv_label_create(root); lv_label_set_text(l, t);
    lv_obj_set_style_text_font(l, TF(UI_20), 0); lv_obj_set_style_text_color(l, TC(TEXT_PRIMARY), 0);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 28);
    return l;
}
/* host tests only: a tap on the open circle (zone 0 top / 1 middle / 2 bottom, 3 = hold) */
void disco_test_tap(int zone){
    if(zone == 3){ g_long = 1; g_cur = 0; for(int i = 0; i < g_nmenu; i++) if(g_menu[i] == 0) g_cur = i; picker_paint(); screen_home(); g_long = 0; return; }
    g_long = 0; g_press_y = PK_Y + (zone == 0 ? 20 : zone == 1 ? PK_D / 2 : PK_D - 20);
    int rel = g_press_y - PK_Y;
    if(rel < PK_D / 3) go_section(g_cur - 1, -1); else if(rel > PK_D * 2 / 3) go_section(g_cur + 1, +1); else go_section(g_cur, +1);
}
