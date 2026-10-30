#include "simple_theme.h"
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Apps (a Disco menu section): up to five apps you pick, each with its icon, opening its landing page.
 *
 * The list: the built-in apps (Boomshine, Listening Stats) and every installed app in /usr/data/apps (menu_apps.c
 * reads their app.conf). An app shows its own icon when it ships one (icon=<file> in app.conf, else icon.bmp in its
 * folder: a BMP, any size, shown at 44 px); otherwise the built-ins draw theirs and any other app gets a tile with
 * its first letter. The five choices are kept by the app's id (its folder), as apps_p1..apps_p5; until you choose,
 * the first five apps are shown. "Choose apps" (the pill under the list) opens the picker: tap to add or remove,
 * the number says the order. */
#include "screens.h"
#include "discshelf.h"
#include "theme.h"
#include "config.h"
#include "curvelist.h"
#include "menu_apps.h"
#include "md5.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

LV_FONT_DECLARE(font_inter_bold_22)
void disco_vec_icon(lv_obj_t *label, const char *icon, int size);   /* disco.c: the Boomshine / Stats vector icons */

#define NPIN 5
#define MAXA 48
#define ROW_W 304
#define ROW_H 64
#define TILE 46
typedef struct { char id[33], name[64], tagline[64], about[160], button[32], exec[256], icon[256]; const char *vec; } shelf_app_t;
static shelf_app_t g_all[MAXA];
static int g_nall, g_pin[NPIN], g_npin;
static lv_obj_t *g_shelf_root, *g_shelf_list, *g_pick_root, *g_pick_list, *g_pick_sub;
static curvelist_t g_scl, g_pcl;

/* ---- the apps ---- */
static void add_builtin(const char *key, const char *name, const char *tag, const char *about, const char *button,
                        const char *exec, const char *vec){
    shelf_app_t *a = &g_all[g_nall++]; memset(a, 0, sizeof *a);
    md5_hex(key, strlen(key), a->id);
    snprintf(a->name, sizeof a->name, "%s", name); snprintf(a->tagline, sizeof a->tagline, "%s", tag);
    snprintf(a->about, sizeof a->about, "%s", about); snprintf(a->button, sizeof a->button, "%s", button);
    snprintf(a->exec, sizeof a->exec, "%s", exec); a->vec = vec;
}
static void scan(void){
    g_nall = 0;
    add_builtin("boomshine", "Boomshine", "One tap. A chain reaction.", "Set off a single blast and\ncatch as many dots as you can.",
                "Play", APP_BUILTIN_BOOMSHINE, "@boom");
    add_builtin("builtin:stats", "Listening Stats", "Your music, in numbers.", "Top artists, albums and songs,\nby week, month or year.",
                "Show me", APP_BUILTIN_STATS, "@stats");
    add_builtin("album-roulette", "Album Roulette", "Let chance choose.", "Spin your collection and\nrediscover a favourite.",
                "I feel lucky!", APP_BUILTIN_ROULETTE, "@vinyl");
    if(sphere_ui_enabled())
        add_builtin("songsphere", "SongSphere", "Play music by mood.", "", "", APP_BUILTIN_SPHERE, "@sphere");
    menu_apps_scan();
    for(int i = 0; i < menu_apps_count() && g_nall < MAXA; i++){
        const menu_app_t *m = menu_apps_get(i);
        shelf_app_t *a = &g_all[g_nall++]; memset(a, 0, sizeof *a);
        snprintf(a->id, sizeof a->id, "%s", m->id); snprintf(a->name, sizeof a->name, "%s", m->name);
        snprintf(a->tagline, sizeof a->tagline, "%s", m->tagline); snprintf(a->about, sizeof a->about, "%s", m->about);
        snprintf(a->button, sizeof a->button, "%s", m->button); snprintf(a->exec, sizeof a->exec, "%s", m->exec);
        snprintf(a->icon, sizeof a->icon, "%s", m->icon);
        if(!strcmp(m->exec,APP_BUILTIN_SHELF))a->vec="@shelf";
    }
}
static int find_id(const char *id){ for(int i = 0; i < g_nall; i++) if(id[0] && !strcmp(g_all[i].id, id)) return i; return -1; }
static void pins_load(void){
    g_npin = 0;
    if(!simple_cfg_get_str("apps_p1", NULL)){                                            /* never chosen: the first five */
        for(int i = 0; i < g_nall && g_npin < NPIN; i++) g_pin[g_npin++] = i;
        return;
    }
    for(int s = 0; s < NPIN; s++){
        char k[12]; snprintf(k, sizeof k, "apps_p%d", s + 1);
        int i = find_id(simple_cfg_get_str(k, ""));                                      /* an app since removed: skipped */
        if(i >= 0) g_pin[g_npin++] = i;
    }
}
static int pins_save(void){
    cfg_begin();
    for(int s = 0; s < NPIN; s++){
        char k[12]; snprintf(k, sizeof k, "apps_p%d", s + 1);
        simple_cfg_set_str_deferred(k, s < g_npin ? g_all[g_pin[s]].id : "");
    }
    return cfg_commit();
}

/* An app's icon: a 24- or 32-bit BMP (up to 512 px a side) read once into RAM and box-filtered to `size`, so it draws
 * like any cover (LVGL's own BMP decoder streams lines and can't be scaled). NULL: no file, or not such a BMP. */
lv_image_dsc_t *app_icon_load(const char *path, int size){
    if(!path || !path[0] || size <= 0) return NULL;
    FILE *f = fopen(path, "rb"); if(!f) return NULL;
    uint8_t h[54]; lv_image_dsc_t *d = NULL; uint8_t *src = NULL;
    if(fread(h, 1, 54, f) != 54 || h[0] != 'B' || h[1] != 'M') goto out;
    uint32_t off = h[10] | h[11] << 8 | h[12] << 16 | (uint32_t)h[13] << 24;
    int32_t w = (int32_t)(h[18] | h[19] << 8 | h[20] << 16 | (uint32_t)h[21] << 24);
    int32_t ht = (int32_t)(h[22] | h[23] << 8 | h[24] << 16 | (uint32_t)h[25] << 24);
    int bpp = h[28] | h[29] << 8;
    uint32_t comp = h[30] | h[31] << 8 | h[32] << 16 | (uint32_t)h[33] << 24;   /* the whole 32-bit field */
    if(ht < -512 || ht > 512) goto out;                                          /* (also keeps -ht in range) */
    int down = ht > 0; if(ht < 0) ht = -ht;
    if((bpp != 24 && bpp != 32) || comp != 0 /* BI_RGB only: bitfield BMPs need their masks */ || w <= 0 || w > 512 || ht <= 0 || ht > 512) goto out;
    size_t stride = ((size_t)w * (size_t)bpp / 8 + 3) & ~(size_t)3;
    src = malloc(stride * (size_t)ht); if(!src || fseek(f, (long)off, SEEK_SET) || fread(src, 1, stride * (size_t)ht, f) != stride * (size_t)ht) goto out;
    d = calloc(1, sizeof *d + (size_t)size * size * 4); if(!d) goto out;
    uint8_t *dst = (uint8_t *)(d + 1); int bytes = bpp / 8;
    for(int y = 0; y < size; y++) for(int x = 0; x < size; x++){                 /* average the source pixels under each one */
        int x0 = x * w / size, x1 = (x + 1) * w / size, y0 = y * ht / size, y1 = (y + 1) * ht / size, acc[3] = { 0, 0, 0 }, k = 0;
        if(x1 <= x0) x1 = x0 + 1;
        if(y1 <= y0) y1 = y0 + 1;
        for(int yy = y0; yy < y1; yy++){ const uint8_t *row = src + (size_t)(down ? ht - 1 - yy : yy) * stride;
            for(int xx = x0; xx < x1; xx++){ const uint8_t *p = row + (size_t)xx * bytes; acc[0] += p[0]; acc[1] += p[1]; acc[2] += p[2]; k++; } }
        uint8_t *o = dst + ((size_t)y * size + x) * 4;
        o[0] = (uint8_t)(acc[0] / k); o[1] = (uint8_t)(acc[1] / k); o[2] = (uint8_t)(acc[2] / k); o[3] = 0xFF;
    }
    d->header.magic = LV_IMAGE_HEADER_MAGIC; d->header.cf = LV_COLOR_FORMAT_XRGB8888;
    d->header.w = d->header.h = (uint32_t)size; d->header.stride = (uint32_t)size * 4;
    d->data = dst; d->data_size = (uint32_t)size * size * 4;
out:
    free(src); fclose(f);
    return d;
}
static lv_image_dsc_t *g_icons[2][MAXA]; static int g_nicons[2];               /* each list's icons (0 Apps, 1 Choose), freed on its next build */
static void icons_forget(int pool){ for(int i = 0; i < g_nicons[pool]; i++) free(g_icons[pool][i]); g_nicons[pool] = 0; }

/* ---- the pieces ---- */
static lv_obj_t *label(lv_obj_t *p, const char *s, int x, int y, int w, const lv_font_t *f, lv_color_t c, lv_text_align_t al){
    lv_obj_t *l = lv_label_create(p); lv_label_set_text(l, s);
    lv_obj_set_style_text_font(l, f, 0); lv_obj_set_style_text_color(l, c, 0); lv_obj_set_style_text_align(l, al, 0);
    lv_obj_set_width(l, w); lv_label_set_long_mode(l, LV_LABEL_LONG_SCROLL_CIRCULAR); lv_obj_set_height(l, lv_font_get_line_height(f));
    lv_obj_set_pos(l, x, y); lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
    return l;
}
/* the app's icon in a rounded tile: its own picture, the built-in's drawing, or its first letter */
static lv_obj_t *tile(lv_obj_t *p, int x, int y, int size, const shelf_app_t *a, int pool){
    lv_obj_t *t = lv_obj_create(p); lv_obj_remove_style_all(t);
    lv_obj_set_pos(t, x, y); lv_obj_set_size(t, size, size);
    lv_obj_set_style_radius(t, size * 3 / 10, 0); lv_obj_set_style_clip_corner(t, true, 0);
    lv_obj_set_style_bg_color(t, TC(SURFACE_RAISED), 0); lv_obj_set_style_bg_opa(t, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(t, 1, 0); lv_obj_set_style_border_color(t, TC(BORDER), 0);
    lv_obj_remove_flag(t, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_image_dsc_t *ic = g_nicons[pool] < MAXA ? app_icon_load(a->icon, size) : NULL;
    if(ic){
        g_icons[pool][g_nicons[pool]++] = ic;
        lv_obj_t *im = lv_image_create(t); lv_image_set_src(im, ic); lv_obj_center(im);
        lv_obj_set_style_border_width(t, 0, 0);
        return t;
    }
    lv_obj_t *l = lv_label_create(t);
    lv_obj_set_style_text_color(l, ui_current_accent(), 0);
    if(a->vec){ disco_vec_icon(l, a->vec, size * 56 / 100); lv_obj_center(l); return t; }
    char ini[8] = "?"; unsigned char c = (unsigned char)a->name[0]; int k = 1;
    if(c >= 0xF0) k = 4; else if(c >= 0xE0) k = 3; else if(c >= 0xC0) k = 2;
    if(c) snprintf(ini, sizeof ini, "%.*s", k, a->name);
    if(k == 1 && ini[0] >= 'a' && ini[0] <= 'z') ini[0] = (char)(ini[0] - 32);
    lv_label_set_text(l, ini); lv_obj_set_style_text_font(l, &font_inter_bold_22, 0); lv_obj_center(l);
    return t;
}
static lv_obj_t *row(lv_obj_t *list, const shelf_app_t *a, lv_event_cb_t cb, intptr_t ud){
    lv_obj_t *r = lv_obj_create(list); lv_obj_remove_style_all(r);
    lv_obj_set_size(r, ROW_W, ROW_H);
    lv_obj_add_flag(r, LV_OBJ_FLAG_USER_1);                                       /* curves like a Library row */
    lv_obj_set_style_radius(r, 16, 0);
    lv_obj_set_style_bg_color(r, TC(SURFACE), LV_STATE_PRESSED); lv_obj_set_style_bg_opa(r, LV_OPA_70, LV_STATE_PRESSED);
    lv_obj_remove_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE); lv_obj_add_event_cb(r, cb, LV_EVENT_CLICKED, (void *)ud);
    tile(r, 10, (ROW_H - TILE) / 2, TILE, a, list == g_pick_list);
    const lv_font_t *ft = ui_font_cjk(20), *fs = ui_font_cjk(14);
    int h1 = lv_font_get_line_height(ft), h2 = lv_font_get_line_height(fs), y = (ROW_H - h1 - h2) / 2;
    label(r, a->name, 68, y, 180, ft, TC(TEXT_PRIMARY), LV_TEXT_ALIGN_LEFT);
    label(r, a->tagline[0] ? a->tagline : "Installed app", 68, y + h1, 180, fs, TC(TEXT_SECONDARY), LV_TEXT_ALIGN_LEFT);
    return r;
}
static lv_obj_t *block(lv_obj_t *list, int h){
    lv_obj_t *b = lv_obj_create(list); lv_obj_remove_style_all(b);
    lv_obj_set_size(b, 360, h); lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return b;
}
static lv_obj_t *list_make(lv_obj_t *root, curvelist_t *cl, int top){
    lv_obj_t *l = lv_obj_create(root); lv_obj_remove_style_all(l);
    lv_obj_set_size(l, 360, 360);
    lv_obj_set_flex_flow(l, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_top(l, top, 0); lv_obj_set_style_pad_bottom(l, 70, 0); lv_obj_set_style_pad_row(l, 4, 0);
    lv_obj_set_scroll_dir(l, LV_DIR_VER); lv_obj_set_scrollbar_mode(l, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(l, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    curvelist_attach(cl, l, root, ROW_W);
    return l;
}
/* a centred pill button (the Stats headings' look) */
static lv_obj_t *pill(lv_obj_t *p, int y, const char *s, const char *glyph, lv_event_cb_t cb){
    const lv_font_t *f = TF(UI_18);
    lv_point_t sz; lv_text_get_size(&sz, s, f, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    int gw = glyph ? 26 : 0, w = sz.x + gw + 40;
    lv_obj_t *b = lv_obj_create(p); lv_obj_remove_style_all(b);
    lv_obj_set_pos(b, 180 - w / 2, y); lv_obj_set_size(b, w, 42);
    lv_obj_set_style_radius(b, 21, 0);
    lv_obj_set_style_bg_color(b, TC(SURFACE), 0); lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, TC(SURFACE_PRESSED), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(b, 1, 0); lv_obj_set_style_border_color(b, TC(ACCENT_PRIMARY), 0); lv_obj_set_style_border_opa(b, LV_OPA_40, 0);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE); lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(b, 8);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    if(glyph) label(b, glyph, 18, (42 - lv_font_get_line_height(TF(UI_16))) / 2, 22, TF(UI_16), TC(ACCENT_PRIMARY), LV_TEXT_ALIGN_LEFT);
    label(b, s, 20 + gw, (42 - lv_font_get_line_height(f)) / 2, sz.x + 2, f, TC(ACCENT_PRIMARY), LV_TEXT_ALIGN_LEFT);
    return b;
}

/* ---- the Apps screen ---- */
static void open_cb(lv_event_t *e){
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if(i < 0 || i >= g_nall) return;
    const shelf_app_t *a = &g_all[i];
    if(!strcmp(a->exec, APP_BUILTIN_SHELF)){discshelf_landing();return;}
    if(!strcmp(a->exec, APP_BUILTIN_SPHERE)){ sphere_ui_landing(); return; }   /* Static guard; initialize SongSphere only after Open. */
    if(!strcmp(a->exec, APP_BUILTIN_ROULETTE)){ screen_show(SCR_ROULETTE); return; }   /* its own vinyl landing page */
    applanding_icon(a->icon);
    applanding_show(a->name, a->tagline, a->about, a->button, a->exec);
}
static void choose_cb(lv_event_t *e){ (void)e; appshelf_pick_open(); }
static void shelf_build(void){
    if(!g_shelf_list) return;
    lv_obj_clean(g_shelf_list); icons_forget(0);
    lv_obj_t *h = block(g_shelf_list, 56);
    label(h, "Apps", 80, 18, 200, TF(UI_22), TC(TEXT_PRIMARY), LV_TEXT_ALIGN_CENTER);
    if(!g_npin){
        lv_obj_t *b = block(g_shelf_list, 120);
        lv_obj_t *l = lv_label_create(b);
        lv_label_set_text(l, g_nall ? "No apps chosen yet.\nPick up to five to keep here." : "No apps installed.");
        lv_obj_set_style_text_font(l, TF(UI_16), 0); lv_obj_set_style_text_color(l, TC(TEXT_SECONDARY), 0);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0); lv_obj_align(l, LV_ALIGN_CENTER, 0, 10);
    }
    for(int s = 0; s < g_npin; s++) row(g_shelf_list, &g_all[g_pin[s]], open_cb, g_pin[s]);
    if(g_nall){ lv_obj_t *b = block(g_shelf_list, 62); pill(b, 12, "Choose apps", LV_SYMBOL_EDIT, choose_cb); }
    lv_obj_update_layout(g_shelf_list);
    lv_obj_scroll_to_y(g_shelf_list, 0, LV_ANIM_OFF);
    curvelist_update(&g_scl);
}
void appshelf_reload(void){ scan(); pins_load(); shelf_build(); }
void appshelf_open(void){ appshelf_reload(); screen_section(SCR_APPSHELF, +1); }
void appshelf_create(lv_obj_t *root){
    g_shelf_root = root;
    lv_obj_set_style_bg_color(root, TC(CANVAS), 0); lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    g_shelf_list = list_make(root, &g_scl, 6);
}
lv_obj_t *appshelf_scroller(void){ return screen_current() == SCR_APPPICK ? g_pick_list : g_shelf_list; }

/* ---- Choose apps: every app; a tap adds it at the end or takes it out ---- */
static void pick_build(void);
static void pick_cb(lv_event_t *e){
    int i = (int)(intptr_t)lv_event_get_user_data(e), at = -1;
    for(int s = 0; s < g_npin; s++) if(g_pin[s] == i) at = s;
    if(at >= 0){ memmove(g_pin + at, g_pin + at + 1, sizeof(int) * (size_t)(g_npin - at - 1)); g_npin--; }
    else if(g_npin >= NPIN){ ui_toast("Up to five apps: take one out first"); return; }
    else g_pin[g_npin++] = i;
    if(pins_save() != 0){ ui_toast("Couldn't save your apps"); pins_load(); }
    int y = (int)lv_obj_get_scroll_y(g_pick_list);
    pick_build(); shelf_build();
    lv_obj_scroll_to_y(g_pick_list, y, LV_ANIM_OFF); curvelist_update(&g_pcl);
}
static void pick_back_cb(lv_event_t *e){ (void)e; screen_back(); }
static void pick_build(void){
    if(!g_pick_list) return;
    lv_obj_clean(g_pick_list); icons_forget(1);
    char sub[48]; snprintf(sub, sizeof sub, "%d of 5 chosen", g_npin);
    lv_label_set_text(g_pick_sub, sub);
    for(int i = 0; i < g_nall; i++){
        lv_obj_t *r = row(g_pick_list, &g_all[i], pick_cb, i);
        int at = -1; for(int s = 0; s < g_npin; s++) if(g_pin[s] == i) at = s;
        lv_obj_t *m = lv_obj_create(r); lv_obj_remove_style_all(m);                /* the mark on the tile's corner: its place, or empty */
        lv_obj_set_size(m, 24, 24); lv_obj_set_pos(m, 10 + TILE - 16, (ROW_H - TILE) / 2 - 6);
        lv_obj_set_style_radius(m, LV_RADIUS_CIRCLE, 0);
        lv_obj_remove_flag(m, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        if(at >= 0){
            lv_obj_set_style_bg_color(m, TC(ACCENT_PRIMARY), 0); lv_obj_set_style_bg_opa(m, LV_OPA_COVER, 0);
            char n[12]; snprintf(n, sizeof n, "%d", at + 1);
            lv_obj_set_style_border_width(m, 2, 0); lv_obj_set_style_border_color(m, TC(CANVAS), 0);
            label(m, n, 0, (24 - lv_font_get_line_height(TF(UI_14))) / 2, 24, TF(UI_14), TC(ON_ACCENT), LV_TEXT_ALIGN_CENTER);
        } else {
            lv_obj_set_style_bg_color(m, TC(CANVAS), 0); lv_obj_set_style_bg_opa(m, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(m, 2, 0); lv_obj_set_style_border_color(m, TC(TEXT_MUTED), 0);
            label(m, "+", 0, (24 - lv_font_get_line_height(TF(UI_14))) / 2 - 1, 24, TF(UI_14), TC(TEXT_SECONDARY), LV_TEXT_ALIGN_CENTER);
            lv_obj_set_style_opa(lv_obj_get_child(r, 0), LV_OPA_60, 0);           /* not chosen: the tile a little quieter */
        }
    }
    lv_obj_update_layout(g_pick_list);
    curvelist_update(&g_pcl);
}
void appshelf_pick_open(void){
    scan(); pins_load(); pick_build(); shelf_build();   /* the same scan for both: Apps' indices stay right */
    lv_obj_scroll_to_y(g_pick_list, 0, LV_ANIM_OFF); curvelist_update(&g_pcl);
    screen_show(SCR_APPPICK);
}
void appshelf_pick_create(lv_obj_t *root){
    g_pick_root = root;
    lv_obj_set_style_bg_color(root, TC(CANVAS), 0); lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    g_pick_list = list_make(root, &g_pcl, 84);
    lv_obj_t *hd = lv_obj_create(root); lv_obj_remove_style_all(hd);              /* the header sits over the list */
    lv_obj_set_size(hd, 360, 84); lv_obj_set_style_bg_color(hd, TC(CANVAS), 0); lv_obj_set_style_bg_opa(hd, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_grad_color(hd, TC(CANVAS), 0); lv_obj_set_style_bg_main_opa(hd, LV_OPA_COVER, 0); lv_obj_set_style_bg_grad_opa(hd, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_main_stop(hd, 170, 0); lv_obj_set_style_bg_grad_stop(hd, 255, 0); lv_obj_set_style_bg_grad_dir(hd, LV_GRAD_DIR_VER, 0);   /* fades into the list */
    lv_obj_remove_flag(hd, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    ui_header_cb(root, "Choose apps", pick_back_cb);
    g_pick_sub = label(root, "", 80, th_braun2() ? 66 : 52, 200, TF(UI_14), TC(ACCENT_PRIMARY), LV_TEXT_ALIGN_CENTER);   /* Braun 2.0: under its lower header */
}
