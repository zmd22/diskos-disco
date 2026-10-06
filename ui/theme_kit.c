/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
/* theme_kit.c - the action registry and the active kit (see theme_kit.h). */
#include "theme_kit.h"
#include "theme.h"
#include "screens.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#ifdef DISKOS_HOST
/* host tests only: remember which object carries which action. Entries are dropped when the object is deleted, so a
 * reused address never inherits a stale name. */
typedef struct act { const lv_obj_t *obj; char name[96]; int tier; struct act *next; } act_t;
static act_t *g_acts;
static void act_deleted(lv_event_t *e){
    const lv_obj_t *o = lv_event_get_target(e);
    for(act_t **p = &g_acts; *p; ){ if((*p)->obj == o){ act_t *d = *p; *p = d->next; free(d); } else p = &(*p)->next; }
}
static void act_add(lv_obj_t *o, const char *name, int tier){
    for(act_t *a = g_acts; a; a = a->next) if(a->obj == o){
        size_t n = strlen(a->name);
        if(!strstr(a->name, name) && n + strlen(name) + 2 < sizeof a->name){ a->name[n] = '|'; strcpy(a->name + n + 1, name); }
        if(tier < a->tier) a->tier = tier;
        return;
    }
    act_t *a = calloc(1, sizeof *a); if(!a) return;
    a->obj = o; a->tier = tier; strncpy(a->name, name, sizeof a->name - 1);
    a->next = g_acts; g_acts = a;
    lv_obj_add_event_cb(o, act_deleted, LV_EVENT_DELETE, NULL);
}
const char *ui_action_of(const lv_obj_t *o){ for(act_t *a = g_acts; a; a = a->next) if(a->obj == o) return a->name; return NULL; }
lv_obj_t *ui_action_find(const char *name){   /* "name#K" = the K-th newest (0 = newest) */
    const char *h = strchr(name, '#'); size_t n = h ? (size_t)(h - name) : strlen(name); int k = h ? atoi(h + 1) : 0;
    for(act_t *a = g_acts; a; a = a->next){
        for(const char *p = a->name; ; ){   /* each '|'-separated action of this object */
            const char *q = strchr(p, '|'); size_t len = q ? (size_t)(q - p) : strlen(p);
            if(len == n && !strncmp(p, name, n) && k-- == 0) return (lv_obj_t *)a->obj;
            if(!q) break;
            p = q + 1;
        }
    }
    return NULL;
}
int ui_action_tier(const lv_obj_t *o){ for(act_t *a = g_acts; a; a = a->next) if(a->obj == o) return a->tier; return -1; }
#endif

/* A touch that moved like a swipe is not a tap. LVGL keeps a button pressed when the finger slides off it (press
 * lock), so a swipe that STARTS on a control would CLICK it on release as well as doing the swipe: the left-edge
 * back swipe went back twice from an edge back arrow, or opened/changed a left-edge row and then went back (a slow,
 * held swipe could also fire a row's long-press action). For every ui_on() CLICKED / SHORT_CLICKED / LONG_PRESSED
 * handler the guard runs first (registration order) and stops the event - and its bubbling - when the active pointer ended more than UI_CLICK_SLOP from where THIS press began. The origin is taken
 * on the object's own PRESSED event (not the main loop's gesture state, which the keyboard bypasses); key/encoder
 * clicks have no pointer travel and are never stopped. */
#define UI_CLICK_SLOP 16        /* below the swipe threshold's 20 px floor, so every recognised swipe is covered */
static lv_point_t g_click_origin;
static void click_origin_cb(lv_event_t *e){
    (void)e;
    lv_indev_t *in = lv_indev_active();
    if(in && lv_indev_get_type(in) == LV_INDEV_TYPE_POINTER) lv_indev_get_point(in, &g_click_origin);
}
static void click_swipe_guard(lv_event_t *e){
    lv_indev_t *in = lv_indev_active();
    if(!in || lv_indev_get_type(in) != LV_INDEV_TYPE_POINTER) return;
    lv_point_t p; lv_indev_get_point(in, &p);
    int32_t dx = p.x - g_click_origin.x, dy = p.y - g_click_origin.y;
    if(dx < 0) dx = -dx;
    if(dy < 0) dy = -dy;
    if((dx > dy ? dx : dy) > UI_CLICK_SLOP) lv_event_stop_processing(e);
}

void ui_on(lv_obj_t *obj, lv_event_cb_t cb, lv_event_code_t code, void *user_data, const char *action, ui_tier_t tier){
    if(!obj) return;
    if(action && !strcmp(action,"nav.back"))lv_obj_add_flag(obj,LV_OBJ_FLAG_USER_2);
    if(code == LV_EVENT_CLICKED || code == LV_EVENT_SHORT_CLICKED || code == LV_EVENT_LONG_PRESSED){
        lv_obj_add_event_cb(obj, click_origin_cb, LV_EVENT_PRESSED, NULL);
        lv_obj_add_event_cb(obj, click_swipe_guard, code, NULL);       /* before the handler: see above */
    }
    lv_obj_add_event_cb(obj, cb, code, user_data);
#ifdef DISKOS_HOST
    if(action) act_add(obj, action, (int)tier);
#else
    (void)action; (void)tier;
#endif
}

const char *ui_transport_action(const char *cmd, const char *prefix, char *buf, int cap){
    const char *what = !cmd ? "cmd" : !strcmp(cmd, "0201000C0002") ? "prev" : !strcmp(cmd, "0201000C0000") ? "pp"
                     : !strcmp(cmd, "0201000C0001") ? "next" : "cmd";
    lv_snprintf(buf, cap, "%s.%s", prefix, what);
    return buf;
}

/* ---- the styling pass ---- */
#define KIT_DONE LV_OBJ_FLAG_USER_4     /* already styled */
static int is_glyph(const char *t){    /* one symbol / icon glyph (private use area or LVGL symbol), no words */
    if(!t || !*t) return 0;
    unsigned char c = (unsigned char)t[0];
    if(c == 0xEF && strlen(t) == 3) return 1;           /* U+F000..U+FFFF: LVGL symbols + icon fonts */
    return 0;
}
static lv_obj_t *first_label(lv_obj_t *o){
    uint32_t n = lv_obj_get_child_count(o);
    for(uint32_t i = 0; i < n; i++){ lv_obj_t *c = lv_obj_get_child(o, (int32_t)i); if(lv_obj_check_type(c, &lv_label_class)) return c; }
    return NULL;
}
static kit_role_t classify(lv_obj_t *o, kit_role_t parent_role, int is_root, int on_top){
    const lv_obj_class_t *c = lv_obj_get_class(o);
    if(is_root && !on_top) return KIT_SCREEN;
    if(c == &lv_switch_class)   return KIT_TOGGLE;
    if(c == &lv_slider_class)   return KIT_SLIDER;
    if(c == &lv_roller_class)   return KIT_ROLLER;
    if(c == &lv_arc_class)      return KIT_ARC;
    if(c == &lv_bar_class)      return KIT_BAR;
    if(c == &lv_textarea_class) return KIT_TEXTAREA;
    if(c == &lv_keyboard_class) return KIT_KEYBOARD;
    if(c == &lv_image_class)    return KIT_IMAGE;
    if(c == &lv_label_class){
        const char *t = lv_label_get_text(o);
        if(parent_role == KIT_ROW){
            if(is_glyph(t) && !strcmp(t, LV_SYMBOL_RIGHT)) return KIT_ROW_CHEVRON;
            return lv_obj_get_index(o) == lv_obj_get_index(first_label(lv_obj_get_parent(o))) ? KIT_ROW_TEXT : KIT_ROW_SUB;
        }
        if(lv_obj_get_style_text_font(o, 0) == theme_font(THEME_FONT_HEADER) || lv_obj_has_flag(o, THEME_TITLE_STEPPED)
           || lv_obj_get_user_data(o) == (void *)&theme_title_tag)
            return KIT_TITLE;
        return KIT_TEXT;
    }
    int32_t w = lv_obj_get_width(o), h = lv_obj_get_height(o);
    if(is_root && on_top) return (w >= 340 && h >= 340) ? KIT_SCRIM : KIT_POPUP;
    if(lv_obj_has_flag(o, LV_OBJ_FLAG_CLICKABLE) && lv_obj_get_event_count(o) > 0){
        lv_obj_t *l = first_label(o);
        if(w >= 200 && h >= 34 && h <= 90 && l) return KIT_ROW;
        if(l && is_glyph(lv_label_get_text(l))) return KIT_ICON_BUTTON;
        if(l) return KIT_BUTTON;
    }
    /* a filled container on the top layer (a dialog's panel inside its dimmer) is part of a popup: it stays a solid
     * window in every theme, where a flat-list theme draws the cards of a screen as bare text */
    if(lv_obj_get_style_bg_opa(o, 0) > LV_OPA_10 && lv_obj_get_child_count(o) > 0) return on_top ? KIT_POPUP : KIT_CARD;
    return KIT_OTHER;
}
static lv_obj_t *media_scope;
void kit_media_scope(lv_obj_t *root){ media_scope=root; }
static void pass(lv_obj_t *o, kit_role_t parent_role, int is_root, int on_top, int force){
    if(o == media_scope) return;
    void (*style)(lv_obj_t *, kit_role_t) = theme_kit()->style;
    kit_role_t r = classify(o, parent_role, is_root, on_top);
    if(force || !lv_obj_has_flag(o, KIT_DONE)){
        lv_obj_add_flag(o, KIT_DONE);
        if(lv_obj_check_type(o, &lv_label_class) && !is_glyph(lv_label_get_text(o))){   /* no Default face left */
            const lv_font_t *f = theme_font_substitute(lv_obj_get_style_text_font(o, 0));
            if(f) lv_obj_set_style_text_font(o, f, 0);
        }
        style(o, r);
        /* a one-line label with a fixed height shorter than the theme's face: grow it to the face (accents and
         * descenders are not cut off) */
        if(lv_obj_check_type(o, &lv_label_class) && lv_label_get_long_mode(o) != LV_LABEL_LONG_WRAP){
            int32_t sh = lv_obj_get_style_height(o, 0);
            int lh = lv_font_get_line_height(lv_obj_get_style_text_font(o, 0));
            if(sh != LV_SIZE_CONTENT && !LV_COORD_IS_PCT(sh) && sh > 0 && sh < lh) lv_obj_set_height(o, lh);
        }
        /* a label in a button that no longer fits (a wider theme face): step down to the theme's smaller sizes
         * (one line: content-sized, or clipped rather than wrapped) */
        if(r == KIT_TEXT && (parent_role == KIT_BUTTON || parent_role == KIT_ICON_BUTTON) &&
           (lv_obj_get_style_width(o, 0) == LV_SIZE_CONTENT || lv_label_get_long_mode(o) != LV_LABEL_LONG_WRAP)){
            lv_obj_t *b = lv_obj_get_parent(o);
            int room = lv_obj_get_content_width(b);
            for(int k = 0; k < 3 && room > 0; k++){
                const lv_font_t *f = lv_obj_get_style_text_font(o, 0);
                lv_point_t sz; lv_text_get_size(&sz, lv_label_get_text(o), f, lv_obj_get_style_text_letter_space(o, 0), 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
                if(sz.x <= room) break;
                const lv_font_t *sm = theme_font_smaller(f);
                if(!sm) break;
                lv_obj_set_style_text_font(o, sm, 0);
            }
        }
    }
    uint32_t n = lv_obj_get_child_count(o);
    for(uint32_t i = 0; i < n; i++) pass(lv_obj_get_child(o, (int32_t)i), r, 0, on_top, force);
}
static int on_top_layer(lv_obj_t *o){ return lv_obj_get_parent(o) == lv_layer_top() || lv_obj_get_parent(o) == lv_layer_sys(); }
/* One colour epoch per screen: an album change must not relayout all hidden screens.
 * Incoming screens catch up on their next ordinary styling pass. */
static unsigned accent_epoch=1,screen_epoch[SCR_COUNT];
static int top_accent_pending;
void kit_accent_changed(void){
    if(!th_ringlike())return;   /* fork: every Ring button follows the album colour */
    if(++accent_epoch==0){accent_epoch=1;memset(screen_epoch,0,sizeof screen_epoch);}
    top_accent_pending=1;
    lv_obj_t *root=screen_get_root(screen_current());
    if(root)lv_obj_invalidate(root);
}
/* sizes decide roles (a row is wide): lay out first, or content built this frame still measures 0 */
void kit_pass(lv_obj_t *root){
    if(!root || !theme_kit()->style) return;
    int force=0;
    if(th_ringlike())for(int i=0;i<SCR_COUNT;i++)if(root==screen_get_root(i)){
        force=screen_epoch[i]!=accent_epoch;screen_epoch[i]=accent_epoch;break;
    }
    lv_obj_update_layout(root); pass(root, KIT_OTHER, 1, on_top_layer(root), force);
    if(theme_kit()->pass_end) theme_kit()->pass_end();
}
void kit_restyle(lv_obj_t *root){
    if(!root || !theme_kit()->style) return;
    lv_obj_update_layout(root); pass(root, KIT_OTHER, 1, on_top_layer(root), 1);
    if(theme_kit()->pass_end) theme_kit()->pass_end();
}

void kit_keep(lv_obj_t *obj){ if(obj) lv_obj_add_flag(obj, KIT_DONE); }

/* content can be built after a screen is shown (lists fill asynchronously, popups open on the top layer): before
 * each redraw, style whatever is new on the visible screen and the top layer. Styled widgets are skipped, and nothing
 * runs while the screen is idle. */
extern volatile uint32_t lv_obj_create_count;   /* vendored LVGL (lv_obj_class.c): objects created so far */
static void before_redraw(lv_event_t *e){
    (void)e;
    lv_obj_t *cur = screen_get_root(screen_current());
    /* fork perf: nothing created, same screen, no album-colour change since the last pass -> every object on
     * screen is already styled; skip the walk (it ran before every redraw, dozens of times a second) */
    static uint32_t seen_count = 0xFFFFFFFFu; static lv_obj_t *seen_root; static unsigned seen_epoch;
    if(lv_obj_create_count == seen_count && cur == seen_root && accent_epoch == seen_epoch && !top_accent_pending) return;
    seen_count = lv_obj_create_count; seen_root = cur; seen_epoch = accent_epoch;
    if(cur) kit_pass(cur);
    lv_obj_t *top = lv_layer_top();
    uint32_t n = lv_obj_get_child_count(top);
    int recolour=top_accent_pending;top_accent_pending=0;
    for(uint32_t i = 0; i < n; i++){
        lv_obj_t *child=lv_obj_get_child(top,(int32_t)i);
        if(recolour)kit_restyle(child);else kit_pass(child);
    }
}

/* the styling pass watches redraws; screens_init calls this once the display exists (no-op for Default) */
void kit_install(void){
    static int done;
    if(done || !theme_kit()->style || !lv_display_get_default()) return;
    done = 1;
    lv_display_add_event_cb(lv_display_get_default(), before_redraw, LV_EVENT_REFR_START, NULL);
}

/* ---- the active kit ---- */
lv_obj_t *kit_default_header(lv_obj_t *root, const char *title, lv_event_cb_t back_cb);   /* ui.c */
const theme_kit_t theme_kit_default = {
    .id = "default",
    .header = kit_default_header,
};

static theme_kit_t g_kit;
static int g_kit_ready;
const theme_kit_t *theme_kit(void){
    if(!g_kit_ready){
        const theme_kit_t *k = theme_kit_for_active();
        g_kit = *k;
        if(!g_kit.header) g_kit.header = theme_kit_default.header;
        g_kit_ready = 1;
    }
    return &g_kit;
}
