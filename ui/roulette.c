/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "screens.h"
#include "theme.h"
#include "theme_kit.h"
#include "discshelf.h"
#include "discshelf/landing.h"
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static void null_landing_button(lv_obj_t *button,lv_obj_t *label){
    if(!th_null())return;
    lv_obj_set_size(button,190,48);lv_obj_align(button,LV_ALIGN_TOP_MID,0,284);
    lv_obj_set_style_radius(button,0,0);lv_obj_set_style_bg_opa(button,0,0);
    lv_obj_set_style_bg_color(button,TC(TEXT_SECONDARY),LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(button,70,LV_STATE_PRESSED);
    lv_obj_set_style_border_width(button,1,0);lv_obj_set_style_border_color(button,TC(TEXT_SECONDARY),0);
    lv_obj_set_style_text_font(label,TF(HEADER_SM),0);lv_obj_set_style_text_color(label,TC(TEXT_PRIMARY),0);
    lv_obj_set_width(label,168);lv_label_set_long_mode(label,LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(label,LV_TEXT_ALIGN_CENTER,0);lv_obj_center(label);
    kit_keep(button);kit_keep(label);
}
static void launch_cb(lv_event_t *e){ (void)e; roulette_spin_open(); }   /* built in since 2.2.0 */
static lv_obj_t *text(lv_obj_t *root, const char *s, int y, int width, const lv_font_t *font, lv_color_t color){
    lv_obj_t *l = lv_label_create(root);
    lv_label_set_text(l, s); lv_obj_set_width(l, width);
    lv_obj_set_style_text_font(l, font, 0); lv_obj_set_style_text_color(l, color, 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, y);
    return l;
}
void roulette_create(lv_obj_t *root){
    lv_obj_set_style_bg_color(root, TC(CANVAS), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    /* Static vector vinyl: no bitmap, rotation timer or idle redraws. */
    lv_obj_t *disc = lv_obj_create(root); lv_obj_remove_style_all(disc);
    lv_obj_set_size(disc, 128, 128); lv_obj_align(disc, LV_ALIGN_TOP_MID, 0, 38);
    lv_obj_set_style_radius(disc, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(disc, TC(SURFACE), 0); lv_obj_set_style_bg_opa(disc, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(disc, 2, 0); lv_obj_set_style_border_color(disc, TC(TEXT_SECONDARY), 0);
    lv_obj_remove_flag(disc, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    for(int n = 0; n < 4; n++){
        lv_obj_t *ring = lv_obj_create(disc); lv_obj_remove_style_all(ring);
        lv_obj_set_size(ring, 110 - n * 14, 110 - n * 14); lv_obj_center(ring);
        lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(ring, 1, 0); lv_obj_set_style_border_color(ring, TC(BORDER_STRONG), 0);
        lv_obj_remove_flag(ring, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    }
    lv_obj_t *hub = lv_obj_create(disc); lv_obj_remove_style_all(hub);
    lv_obj_set_size(hub, 42, 42); lv_obj_center(hub); lv_obj_set_style_radius(hub, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(hub, TC(ACCENT_PRIMARY), 0); lv_obj_set_style_bg_opa(hub, LV_OPA_COVER, 0);
    lv_obj_remove_flag(hub, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *hole = lv_obj_create(hub); lv_obj_remove_style_all(hole);
    lv_obj_set_size(hole, 8, 8); lv_obj_center(hole); lv_obj_set_style_radius(hole, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(hole, TC(CANVAS), 0); lv_obj_set_style_bg_opa(hole, LV_OPA_COVER, 0);
    lv_obj_remove_flag(hole, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    text(root, "Album Roulette", 181, 272, TF(UI_22), TC(TEXT_PRIMARY));
    text(root, "Let chance choose.", 215, 260, TF(UI_16), TC(ACCENT_PRIMARY));
    text(root, "Rediscover an album.", 243, 258, TF(UI_16), TC(TEXT_SECONDARY));
    lv_obj_t *button = lv_button_create(root); lv_obj_remove_style_all(button);
    lv_obj_set_size(button, 174, 42); lv_obj_align(button, LV_ALIGN_TOP_MID, 0, 296);
    lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(button, TC(ACCENT_PRIMARY), 0); lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(button, TC(PRIMARY_PRESSED), LV_STATE_PRESSED);
    lv_obj_add_event_cb(button, launch_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l = lv_label_create(button); lv_label_set_text(l, "I feel lucky!");
    lv_obj_set_style_text_font(l, TF(UI_18), 0); lv_obj_set_style_text_color(l, TC(ON_ACCENT), 0); lv_obj_center(l);
    null_landing_button(button,l);
}

/* ---- App landing page: Boomshine, and any installed app (text from its app.conf, or plain defaults). */
static lv_obj_t *g_land_root, *g_land_art, *g_land_title, *g_land_tag, *g_land_about, *g_land_btn_l;
static char g_land_exec[256], g_land_icon[264];
static lv_obj_t *g_shelf_preview,*g_sphere_preview;
void applanding_icon(const char *bmp_path){ snprintf(g_land_icon, sizeof g_land_icon, "%s", bmp_path ? bmp_path : ""); }
static void land_cb(lv_event_t *e){
    (void)e;
    if(!strcmp(g_land_exec, APP_BUILTIN_SHELF)){discshelf_open();return;}
    if(!strcmp(g_land_exec, APP_BUILTIN_STATS)){ stats_open(); return; }
    if(!strcmp(g_land_exec, APP_BUILTIN_RUNNER)){ runner_open(); return; }
    if(!strcmp(g_land_exec, APP_BUILTIN_SPHERE)){ sphere_ui_open(); return; }   /* built into mq_ui: a screen, not a program */
    if(strncmp(g_land_exec, "builtin:", 8) && access(g_land_exec, X_OK)){ ui_toast("That app is not installed"); return; }
    app_launch_direct(g_land_exec);
}
static lv_obj_t *dot(lv_obj_t *parent, int d){
    lv_obj_t *o = lv_obj_create(parent); lv_obj_remove_style_all(o);
    lv_obj_set_size(o, d, d); lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return o;
}
static void land_art(const char *name, int boom){
    lv_obj_clean(g_land_art);
    if(boom == 2){                                               /* Listening Stats: a little rising bar chart */
        static const signed char H[5] = { 22, 38, 30, 52, 64 };
        for(int i = 0; i < 5; i++){
            lv_obj_t *b = lv_obj_create(g_land_art); lv_obj_remove_style_all(b);
            lv_obj_set_size(b, 10, H[i]); lv_obj_set_pos(b, 26 + i * 15, 92 - H[i]);
            lv_obj_set_style_radius(b, 3, 0); lv_obj_set_style_bg_color(b, TC(ACCENT_PRIMARY), 0);
            lv_obj_set_style_bg_opa(b, i == 4 ? LV_OPA_COVER : (lv_opa_t)(110 + i * 30), 0);
            lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        }
        lv_obj_t *base = lv_obj_create(g_land_art); lv_obj_remove_style_all(base);
        lv_obj_set_size(base, 80, 2); lv_obj_set_pos(base, 22, 96); lv_obj_set_style_radius(base, 1, 0);
        lv_obj_set_style_bg_color(base, TC(TEXT_SECONDARY), 0); lv_obj_set_style_bg_opa(base, LV_OPA_60, 0);
        lv_obj_remove_flag(base, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        return;
    }
    if(boom){                                                    /* a blast ring, its core, dots caught in it */
        lv_obj_t *ring = dot(g_land_art, 86); lv_obj_align(ring, LV_ALIGN_CENTER, -10, 10);
        lv_obj_set_style_border_width(ring, 3, 0); lv_obj_set_style_border_color(ring, TC(ACCENT_PRIMARY), 0);
        lv_obj_set_style_bg_color(ring, TC(ACCENT_PRIMARY), 0); lv_obj_set_style_bg_opa(ring, LV_OPA_20, 0);
        lv_obj_t *core = dot(g_land_art, 40); lv_obj_align(core, LV_ALIGN_CENTER, -10, 10);
        lv_obj_set_style_bg_color(core, TC(ACCENT_PRIMARY), 0); lv_obj_set_style_bg_opa(core, LV_OPA_COVER, 0);
        static const signed char P[4][3] = { { 40, -38, 14 }, { 14, -48, 10 }, { 46, -10, 10 }, { -38, -30, 8 } };
        for(int i = 0; i < 4; i++){
            lv_obj_t *d = dot(g_land_art, P[i][2]); lv_obj_align(d, LV_ALIGN_CENTER, P[i][0], P[i][1]);
            lv_obj_set_style_bg_color(d, TC(TEXT_SECONDARY), 0); lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
        }
        return;
    }
    char letter[8] = "?";                                        /* any other app: its first letter */
    if(name && name[0]){ int n = 1; unsigned char c = (unsigned char)name[0];
        if(c >= 0xF0) n = 4; else if(c >= 0xE0) n = 3; else if(c >= 0xC0) n = 2;
        snprintf(letter, sizeof letter, "%.*s", n, name);
        if(n == 1 && letter[0] >= 'a' && letter[0] <= 'z') letter[0] = (char)(letter[0] - 32); }
    lv_obj_t *l = lv_label_create(g_land_art); lv_label_set_text(l, letter);
    lv_obj_set_style_text_font(l, TF(UI_48), 0); lv_obj_set_style_text_color(l, TC(ACCENT_PRIMARY), 0); lv_obj_center(l);
}
void applanding_create(lv_obj_t *root){
    g_land_root = root;
    g_shelf_preview = g_sphere_preview = NULL;
    lv_obj_set_style_bg_color(root, TC(CANVAS), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    g_land_art = dot(root, 128); lv_obj_align(g_land_art, LV_ALIGN_TOP_MID, 0, 38);
    lv_obj_set_style_bg_color(g_land_art, TC(SURFACE), 0); lv_obj_set_style_bg_opa(g_land_art, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_land_art, 2, 0); lv_obj_set_style_border_color(g_land_art, TC(TEXT_SECONDARY), 0);
    lv_obj_set_style_clip_corner(g_land_art, true, 0);
    g_land_title = text(root, "", 181, 272, TF(UI_22), TC(TEXT_PRIMARY));
    lv_label_set_long_mode(g_land_title, LV_LABEL_LONG_SCROLL_CIRCULAR);
    g_land_tag = text(root, "", 215, 260, TF(UI_16), TC(ACCENT_PRIMARY));
    lv_label_set_long_mode(g_land_tag, LV_LABEL_LONG_SCROLL_CIRCULAR);
    g_land_about = text(root, "", 243, 258, TF(UI_16), TC(TEXT_SECONDARY));
    lv_label_set_long_mode(g_land_about, LV_LABEL_LONG_SCROLL_CIRCULAR);    /* at most two lines: they fit above the button */
    lv_obj_set_height(g_land_about, 2 * lv_font_get_line_height(TF(UI_16)) + lv_obj_get_style_text_line_space(g_land_about, 0));
    lv_obj_t *button = lv_button_create(root); lv_obj_remove_style_all(button);
    lv_obj_set_size(button, 174, 42); lv_obj_align(button, LV_ALIGN_TOP_MID, 0, 296);
    lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(button, TC(ACCENT_PRIMARY), 0); lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(button, TC(PRIMARY_PRESSED), LV_STATE_PRESSED);
    lv_obj_add_event_cb(button, land_cb, LV_EVENT_CLICKED, NULL);
    g_land_btn_l = lv_label_create(button);
    lv_obj_set_style_text_font(g_land_btn_l, TF(UI_18), 0); lv_obj_set_style_text_color(g_land_btn_l, TC(ON_ACCENT), 0);
    lv_obj_center(g_land_btn_l);
    null_landing_button(button,g_land_btn_l);
}
void applanding_show(const char *name, const char *tagline, const char *about, const char *button, const char *exec){
    if(!g_land_root) return;
    snprintf(g_land_exec, sizeof g_land_exec, "%s", exec ? exec : "");
    int shelf=!strcmp(g_land_exec,APP_BUILTIN_SHELF),sphere=!strcmp(g_land_exec,APP_BUILTIN_SPHERE);
    lv_obj_t *generic[]={g_land_art,g_land_title,g_land_tag,g_land_about,lv_obj_get_parent(g_land_btn_l)};
    for(unsigned i=0;i<sizeof generic/sizeof *generic;i++){
        if(shelf||sphere)lv_obj_add_flag(generic[i],LV_OBJ_FLAG_HIDDEN);
        else lv_obj_remove_flag(generic[i],LV_OBJ_FLAG_HIDDEN);
    }
    if(g_shelf_preview)lv_obj_add_flag(g_shelf_preview,LV_OBJ_FLAG_HIDDEN);
    if(g_sphere_preview)lv_obj_add_flag(g_sphere_preview,LV_OBJ_FLAG_HIDDEN);
    if(shelf||sphere){
        lv_obj_t **guard=sphere?&g_sphere_preview:&g_shelf_preview;
        if(!*guard)*guard=app_guard_create(g_land_root,land_cb,sphere?APP_GUARD_SPHERE:APP_GUARD_SHELF);
        lv_obj_remove_flag(*guard,LV_OBJ_FLAG_HIDDEN);g_land_icon[0]=0;
        screen_show(SCR_APPLAND);return;
    }
    land_art(name, !strcmp(g_land_exec, APP_BUILTIN_BOOMSHINE) ? 1 : !strcmp(g_land_exec, APP_BUILTIN_STATS) ? 2 : 0);
    if(g_land_icon[0] && strncmp(g_land_exec, "builtin:", 8) && !access(g_land_icon, R_OK)){   /* the app's own icon */
        static lv_image_dsc_t *ic;
        lv_image_dsc_t *nw = app_icon_load(g_land_icon, 88);
        if(nw){ lv_obj_clean(g_land_art); free(ic); ic = nw;
            lv_obj_t *im = lv_image_create(g_land_art); lv_image_set_src(im, ic); lv_obj_center(im);
            lv_obj_set_style_radius(im, 22, 0); lv_obj_set_style_clip_corner(im, true, 0); }
    }
    g_land_icon[0] = 0;                                          /* one show only */
    lv_label_set_text(g_land_title, name && name[0] ? name : "App");
    lv_label_set_text(g_land_tag, tagline && tagline[0] ? tagline : "Installed app");
    lv_label_set_text(g_land_about, about && about[0] ? about : "Opens full screen. Your music\nkeeps playing.");
    lv_label_set_text(g_land_btn_l, button && button[0] ? button : "Launch");
    lv_obj_center(g_land_btn_l);
    screen_show(SCR_APPLAND);
}
void boomshine_landing(void){
    applanding_show("Boomshine", "One tap. A chain reaction.",
                    "One spark. Catch them all.", "Play", APP_BUILTIN_BOOMSHINE);
}
