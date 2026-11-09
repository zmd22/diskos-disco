/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
/* theme_kits.c - each unique theme's component kit (theme_kit.h): its styler and the builders it replaces. */
#include "theme.h"
#include "theme_kit.h"
#include "screens.h"
#include <string.h>
#include "theme_fonts.h"

lv_obj_t *kit_edge_header(lv_obj_t *root, const char *title, lv_event_cb_t back_cb);   /* ui.c */


/* big words and numbers set in Default's large faces (the volume readout, the album-wall letter): the theme's own
 * largest text face instead. Glyph-only labels (icons) keep the icon-bearing face. */
static int is_large_default_text(lv_obj_t *o){
    const char *t = lv_label_get_text(o);
    if(!t || !*t || ((unsigned char)t[0] == 0xEF && strlen(t) == 3)) return 0;
    const lv_font_t *f = lv_obj_get_style_text_font(o, 0);
    return f == theme_font(THEME_FONT_UI_28) || f == theme_font(THEME_FONT_UI_32) || f == theme_font(THEME_FONT_UI_36)
        || f == theme_font(THEME_FONT_UI_40);
}

/* a clock string ("11:33", "5:53 PM") as its time and its meridiem ("" in 24-hour mode) */
static void clock_parts(const char *t, char *hm, int hcap, char *mer, int mcap){
    const char *sp = t ? strchr(t, ' ') : NULL;
    int n = sp ? (int)(sp - t) : (t ? (int)strlen(t) : 0);
    if(n >= hcap) n = hcap - 1;
    memcpy(hm, t ? t : "", (size_t)n); hm[n] = 0;
    lv_snprintf(mer, mcap, "%s", sp ? sp + 1 : "");
}
static lv_obj_t *mer_label(lv_obj_t *parent, const lv_font_t *f, lv_color_t c){
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, "");
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    kit_keep(l);
    return l;
}
/* Every theme with the round seek ring puts Now Playing's play-order and heart (or moon) buttons exactly where the
 * default theme does (ui.c: TOP_MID +-118, y 126, glyph not re-centred) - owner's rule 2026-09-29; the times lifted
 * clear of the rim */
#define NP_SIDES_Y 126
static void np_sides_on_cover(const np_parts_t *p, int times_y){
    lv_obj_update_layout(p->root);
    lv_obj_t *k[3] = { p->mode, p->fav, p->sleep };
    for(int i = 0; i < 3; i++) lv_obj_set_y(k[i], NP_SIDES_Y);
    if(times_y > 0){ lv_obj_set_y(p->elapsed, times_y); lv_obj_set_y(p->sep, times_y); lv_obj_set_y(p->remain, times_y); }
}
/* Now Playing's title/artist/album rows moved as a block (y of each label's top): used where a theme's play key sits
 * higher than the default's, so the album line clears it (tests/host/np_overlap.py checks the ink clearance) */
static void np_text_rows(const np_parts_t *p, int title_y, int artist_y, int album_y){
    lv_obj_set_y(p->title, title_y);
    lv_obj_set_y(p->artist, artist_y);
    lv_obj_set_y(p->album, album_y);
}
/* the unique themes keep their own backgrounds: no blurred cover behind Home and Now Playing */
static void own_backgrounds(void){ home_set_backdrop_enabled(0); ui_np_backdrop_enabled(0); }

/* Home's Search: the drawn magnifier replaced by the icon glyph in one colour (for filled or small buttons) */
static void home_search_glyph(lv_obj_t *btn, lv_color_t c){
    for(uint32_t i = 0; i < lv_obj_get_child_count(btn); i++) lv_obj_add_flag(lv_obj_get_child(btn, (int32_t)i), LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *mg = lv_label_create(btn);
    lv_label_set_text(mg, "\xEF\x80\x82");                                   /* f002 search */
    lv_obj_set_style_text_font(mg, TF(ICON_20), 0);
    lv_obj_set_style_text_color(mg, c, 0);
    lv_obj_center(mg);
}
/* Home's Library pill resized by a theme: its icon and word laid out again inside the new width */
static void home_library_fit(lv_obj_t *btn){
    lv_obj_t *icon = NULL, *word = NULL;
    for(uint32_t i = 0; i < lv_obj_get_child_count(btn); i++){
        lv_obj_t *c = lv_obj_get_child(btn, (int32_t)i);
        if(!lv_obj_check_type(c, &lv_label_class)) continue;
        if(strlen(lv_label_get_text(c)) == 3) icon = c; else word = c;
    }
    if(icon) lv_obj_align(icon, LV_ALIGN_LEFT_MID, 16, 0);
    if(word){ lv_obj_align(word, LV_ALIGN_LEFT_MID, 46, 0); }
}

/* ================================================================ stone
 * Soft and tactile: everything is a rounded pill sitting on the surface, with a solid 3 px edge underneath in the
 * shadow colour (DOT_GRID). The edge is below the pill, not inside it: the pill keeps its whole shape and its content
 * stays centred. It is drawn as the pill's own shape, filled and dropped 3 px, just before the pill (one more rounded
 * fill: LVGL's shadow path would rebuild a corner buffer on every draw here, with no shadow cache in this build).
 * Chunky rounded controls; the Home time as two overlapping pills, hours and minutes. */
static void stone_edge_cb(lv_event_t *e){
    lv_obj_t *o = lv_event_get_target(e);
    int drop = lv_obj_has_state(o, LV_STATE_PRESSED) ? 1 : (int)(intptr_t)lv_event_get_user_data(e);   /* pressed: sinks */
    if(lv_event_get_code(e) == LV_EVENT_REFR_EXT_DRAW_SIZE){ lv_event_set_ext_draw_size(e, 4); return; }
    lv_draw_rect_dsc_t d; lv_draw_rect_dsc_init(&d);
    d.radius = lv_obj_get_style_radius(o, 0);
    d.bg_color = TC(DOT_GRID);
    d.bg_opa = lv_obj_get_style_opa_recursive(o, 0);
    lv_area_t a; lv_obj_get_coords(o, &a);
    lv_area_move(&a, 0, drop);
    lv_draw_rect(lv_event_get_layer(e), &d, &a);
}
static void stone_edge(lv_obj_t *o, int drop){
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_remove_event_cb_with_user_data(o, stone_edge_cb, (void *)(intptr_t)3);   /* restyled: one edge, not two */
    lv_obj_remove_event_cb_with_user_data(o, stone_edge_cb, (void *)(intptr_t)4);
    lv_obj_add_event_cb(o, stone_edge_cb, LV_EVENT_DRAW_MAIN_BEGIN, (void *)(intptr_t)drop);
    lv_obj_add_event_cb(o, stone_edge_cb, LV_EVENT_REFR_EXT_DRAW_SIZE, (void *)(intptr_t)drop);
    lv_obj_refresh_ext_draw_size(o);
}
static void stone_pill(lv_obj_t *o, lv_color_t bg){
    kit_keep(o);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(o, bg, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    stone_edge(o, 3);
    lv_obj_set_style_translate_y(o, 2, LV_STATE_PRESSED);            /* pressed: it sinks into the surface */
}
static void stone_style(lv_obj_t *o, kit_role_t r){
    switch(r){
    case KIT_ROW:
        stone_pill(o, TC(SURFACE));
        lv_obj_set_style_bg_color(o, TC(SURFACE_RAISED), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(o, LV_OPA_COVER, LV_STATE_PRESSED);
        break;
    case KIT_ROW_CHEVRON:
        lv_obj_set_style_text_color(o, TC(ACCENT_PRIMARY), 0);
        break;
    case KIT_BUTTON: case KIT_ICON_BUTTON:
        if(lv_obj_get_style_bg_opa(o, 0) > LV_OPA_10) stone_pill(o, lv_obj_get_style_bg_color(o, 0));
        else lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
        break;
    case KIT_CARD:
        if(lv_obj_get_style_bg_opa(o, 0) > LV_OPA_10) lv_obj_set_style_radius(o, 22, 0);
        break;
    case KIT_POPUP:
        lv_obj_set_style_radius(o, 26, 0);
        if(lv_obj_get_style_bg_opa(o, 0) > LV_OPA_10) stone_edge(o, 4);
        break;
    case KIT_SLIDER: case KIT_BAR:
        lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
        lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, LV_PART_KNOB);
        break;
    case KIT_ROLLER: case KIT_TEXTAREA:
        lv_obj_set_style_radius(o, 18, 0);
        lv_obj_set_style_radius(o, 14, LV_PART_SELECTED);
        break;
    case KIT_KEYBOARD:
        lv_obj_set_style_radius(o, 12, LV_PART_ITEMS);
        lv_obj_set_style_border_side(o, LV_BORDER_SIDE_BOTTOM, LV_PART_ITEMS);
        lv_obj_set_style_border_width(o, 2, LV_PART_ITEMS);
        lv_obj_set_style_border_color(o, TC(DOT_GRID), LV_PART_ITEMS);
        lv_obj_set_style_border_opa(o, LV_OPA_COVER, LV_PART_ITEMS);
        break;
    case KIT_TEXT:
        if(is_large_default_text(o)) lv_obj_set_style_text_font(o, TF(UI_24), 0);
        break;
    default: break;
    }
}
/* a clock drawn as separate hours and minutes (12-hour: AM/PM is dropped here; the date line keeps the day) */
static void split_clock(const char *t, lv_obj_t *hl, lv_obj_t *ml){
    if(!hl || !ml) return;
    char hh[8] = "--", mm[16] = "--";
    const char *c = t ? strchr(t, ':') : NULL;
    if(c){ int n = (int)(c - t); if(n > 0 && n < 4){ memcpy(hh, t, n); hh[n] = 0; } lv_snprintf(mm, sizeof mm, "%s", c + 1); }
    char *sp = strchr(mm, ' '); if(sp) *sp = 0;
    lv_label_set_text(hl, hh);
    lv_label_set_text(ml, mm);
}
static lv_obj_t *g_stone_hh, *g_stone_mm, *g_stone_mer;
static void stone_clock(const char *t){
    split_clock(t, g_stone_hh, g_stone_mm);
    char hm[16], mer[8]; clock_parts(t, hm, sizeof hm, mer, sizeof mer);
    char low[8]; if(g_stone_mer) lv_label_set_text(g_stone_mer, theme_lower(low, sizeof low, mer));
}
static lv_obj_t *stone_clock_pill(lv_obj_t *root, lv_color_t bg, lv_color_t fg, int x, int y){
    lv_obj_t *p = lv_obj_create(root);
    lv_obj_remove_style_all(p);
    lv_obj_clear_flag(p, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(p, 130, 76);
    lv_obj_set_pos(p, x, y);
    stone_pill(p, bg);
    lv_obj_t *l = lv_label_create(p);
    lv_obj_set_style_text_font(l, TF(CLOCK), 0);
    lv_obj_set_style_text_color(l, fg, 0);
    lv_obj_center(l);
    return l;
}
static void stone_home(const home_parts_t *p){
    own_backgrounds();
    lv_obj_set_pos(p->status, 0, 30);
    lv_obj_add_flag(p->clock, LV_OBJ_FLAG_HIDDEN);                /* the time is the two pills */
    /* the two pills together are centred (62..298); their vertical step is the design */
    g_stone_hh = stone_clock_pill(p->root, TC(SURFACE_RAISED), TC(TEXT_PRIMARY), 62, 56);
    g_stone_mm = stone_clock_pill(p->root, TC(ACCENT_PRIMARY), TC(ON_ACCENT), 168, 84);
    g_stone_mer = mer_label(p->root, TF(UI_16), TC(TEXT_SECONDARY));   /* "pm" just past the minutes pill */
    lv_obj_align_to(g_stone_mer, lv_obj_get_parent(g_stone_mm), LV_ALIGN_OUT_RIGHT_BOTTOM, 4, -6);
    stone_clock(lv_label_get_text(p->clock));
    lv_obj_set_pos(p->date, 0, 168);
    lv_obj_set_style_text_color(p->date, TC(TEXT_SECONDARY), 0);
    lv_obj_set_pos(p->weather, 0, 188);
    /* Library + 8 + Search share the now-playing pill's edges (62..298) */
    lv_obj_set_pos(p->library, 62, 222); lv_obj_set_size(p->library, 186, 42);
    stone_pill(p->library, TC(SURFACE));
    home_library_fit(p->library);
    lv_obj_set_pos(p->search, 256, 222); lv_obj_set_size(p->search, 42, 42);
    stone_pill(p->search, TC(SURFACE));
    lv_obj_set_pos(p->np, 62, 274); lv_obj_set_size(p->np, 236, 48);
    stone_pill(p->np, TC(SURFACE));
    lv_obj_set_style_bg_opa(p->np, LV_OPA_COVER, 0);
    lv_obj_set_pos(p->np_thumb, 8, 6); lv_obj_set_size(p->np_thumb, 38, 38);
    lv_obj_set_pos(p->np_title, 54, 6);  lv_obj_set_width(p->np_title, 130);
    lv_obj_set_pos(p->np_artist, 54, 26); lv_obj_set_width(p->np_artist, 130);
    lv_obj_set_pos(p->pp, 184, 1); lv_obj_set_size(p->pp, 44, 44);   /* 8 px from the pill's end, as the art is from its start */
    lv_obj_set_style_bg_color(p->pp, TC(ACCENT_PRIMARY), 0);
    lv_obj_set_style_bg_opa(p->pp, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(p->pp_label, TC(ON_ACCENT), 0);
}
static void stone_np(const np_parts_t *p){
    own_backgrounds();
    lv_obj_set_style_arc_width(p->ring, 12, LV_PART_MAIN);
    lv_obj_set_style_arc_width(p->ring, 12, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(p->ring, true, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(p->ring, true, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(p->ring, TC(SURFACE_RAISED), LV_PART_MAIN);
    lv_obj_set_style_pad_all(p->ring, 2, LV_PART_KNOB);
    lv_obj_set_style_radius(p->cover, 26, 0);
    lv_obj_set_size(p->pp, 64, 44);
    stone_pill(p->pp, TC(ACCENT_PRIMARY));
    ui_np_pp_filled(1);                                      /* accent changes recolour the fill, not the glyph */
    lv_obj_set_style_text_color(p->pp, TC(ON_ACCENT), 0);
    lv_obj_set_style_text_align(p->pp, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_top(p->pp, 5, 0);
    lv_obj_set_style_text_font(p->pp, TF(UI_28), 0);
    lv_obj_align(p->pp, LV_ALIGN_TOP_MID, 0, 266);
    lv_obj_set_ext_click_area(p->pp, 4);
    lv_obj_align(p->prev, LV_ALIGN_TOP_MID, -70, 274);
    lv_obj_align(p->next, LV_ALIGN_TOP_MID, 70, 274);
    np_sides_on_cover(p, 0);                                 /* times stay at 318: the play pill ends at 310 */
}
static void stone_qs_tile(lv_obj_t *c, lv_obj_t *g, lv_obj_t *cap, int on, int toggle){
    (void)toggle;
    lv_obj_set_size(c, 70, 50);
    stone_pill(c, on ? TC(ACCENT_PRIMARY) : TC(SURFACE));
    if(g) lv_obj_set_style_text_color(g, on ? TC(ON_ACCENT) : TC(TEXT_PRIMARY), 0);
    if(cap) lv_obj_set_style_text_color(cap, TC(TEXT_SECONDARY), 0);
}
/* the screensaver keeps Home's two-pill clock (in every style that shows a clock); the date and weather sit under it */
static lv_obj_t *g_stone_shh, *g_stone_smm, *g_stone_smer;
static void stone_saver_clock(const char *t){
    split_clock(t, g_stone_shh, g_stone_smm);
    char hm[16], mer[8]; clock_parts(t, hm, sizeof hm, mer, sizeof mer);
    char low[8]; if(g_stone_smer) lv_label_set_text(g_stone_smer, theme_lower(low, sizeof low, mer));
}
static void stone_saver(const saver_parts_t *p){
    int shown = p->style != 1 && p->style != 4;               /* analog and vinyl have no digital clock */
    if(!g_stone_shh && shown){
        g_stone_shh = stone_clock_pill(p->root, TC(SURFACE_RAISED), TC(TEXT_PRIMARY), 62, 0);
        g_stone_smm = stone_clock_pill(p->root, TC(ACCENT_PRIMARY), TC(ON_ACCENT), 168, 0);
        g_stone_smer = mer_label(p->root, TF(UI_16), TC(TEXT_SECONDARY));
        stone_saver_clock(lv_label_get_text(p->clock));
    }
    if(!g_stone_shh) return;
    lv_obj_t *ph = lv_obj_get_parent(g_stone_shh), *pm = lv_obj_get_parent(g_stone_smm);
    if(!shown){ lv_obj_add_flag(ph, LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(pm, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(g_stone_smer, LV_OBJ_FLAG_HIDDEN); return; }
    int y = p->style == 2 ? 112 : 78;                         /* Minimal: lower, nothing under the date */
    lv_obj_remove_flag(ph, LV_OBJ_FLAG_HIDDEN); lv_obj_remove_flag(pm, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(g_stone_smer, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_y(ph, y); lv_obj_set_y(pm, y + 28);            /* Home's step between the pills */
    lv_obj_align_to(g_stone_smer, pm, LV_ALIGN_OUT_RIGHT_BOTTOM, 4, -6);
    lv_obj_add_flag(p->clock, LV_OBJ_FLAG_HIDDEN);
    if(p->date){ lv_obj_align(p->date, LV_ALIGN_TOP_MID, 0, y + 28 + 76 + 10); lv_obj_update_layout(p->date); }
    if(p->weather && p->date) lv_obj_align(p->weather, LV_ALIGN_TOP_MID, 0, lv_obj_get_y(p->date) + 26);
}
const theme_kit_t theme_kit_stone = { .id = "stone", .style = stone_style, .home = stone_home, .home_clock = stone_clock,
                                      .nowplaying = stone_np, .qs_tile = stone_qs_tile, .saver = stone_saver,
                                      .saver_clock = stone_saver_clock };

/* A's builders participate in upstream's registry while retaining their original geometry. */
void bhome_create(lv_obj_t *root);
void kit_fork_np_braun(void);
/* Ring buttons (fork): every "real" button - a small tappable button or glyph button that isn't floating over a
 * list - wears a 2 px ring in the interface accent (red in Ring, lime in Ring Light) and shows its glyph / text in
 * the album colour. Re-run on every album change (kit_accent_changed). */
static void ring_btn_ink(lv_obj_t *o, lv_color_t c){
 for(uint32_t k=0;k<lv_obj_get_child_count(o);k++){lv_obj_t *ch=lv_obj_get_child(o,(int32_t)k);
  if(lv_obj_check_type(ch,&lv_label_class))lv_obj_set_style_text_color(ch,c,0);}
 if(lv_obj_check_type(o,&lv_label_class))lv_obj_set_style_text_color(o,c,0);
}
/* the ring's colour: the theme's own (red / lime), or the Accent colour you picked in Settings > Display */
lv_color_t ring_border_color(void){ return ui_accent_is_static() ? ui_current_accent() : TC(ACCENT_PRIMARY); }
void ring_button_style(lv_obj_t *b){            /* fork: one Ring button - accent ring, glyph in the album colour */
 if(!b || !th_ringlike()) return;
 if(th_disco_family()){                                 /* Disco: glass buttons - a fine light ring, glyphs in the text colour */
  lv_obj_set_style_border_width(b,1,0);lv_obj_set_style_border_opa(b,60,0);lv_obj_set_style_border_color(b,TC(TEXT_PRIMARY),0);
  lv_obj_set_style_border_side(b,LV_BORDER_SIDE_FULL,0);return; }
 lv_obj_set_style_border_width(b,2,0);lv_obj_set_style_border_opa(b,LV_OPA_COVER,0);
 lv_obj_set_style_border_color(b,ring_border_color(),0);lv_obj_set_style_border_side(b,LV_BORDER_SIDE_FULL,0);
 /* a button already filled with an accent (Date & Time's Set, the Equalizer's On) keeps text readable on that fill:
  * the album colour on an accent fill vanished */
 lv_color_t bg=lv_obj_get_style_bg_color(b,0);
 if(lv_obj_get_style_bg_opa(b,0)>=LV_OPA_50 && (lv_color_eq(bg,ui_current_accent())||lv_color_eq(bg,TC(ACCENT_PRIMARY))||lv_color_eq(bg,ui_media_accent())))
  ring_btn_ink(b,theme_on_color(bg));
 else ring_btn_ink(b,ui_media_accent());
}
static void ring_style(lv_obj_t *o,kit_role_t role){
 if(role==KIT_TITLE){lv_obj_set_style_border_width(o,0,0);lv_obj_set_style_outline_width(o,0,0);}
 if(!lv_obj_has_flag(o,LV_OBJ_FLAG_CLICKABLE))return;
 int w=lv_obj_get_width(o),h=lv_obj_get_height(o),floating=0;
 for(lv_obj_t *p=o;p;p=lv_obj_get_parent(p))if(lv_obj_has_flag(p,LV_OBJ_FLAG_USER_2))floating=1;
 lv_obj_set_style_shadow_width(o,0,0);
 if(!floating && (role==KIT_BUTTON || role==KIT_ICON_BUTTON) && w>=28 && h>=28 && w<=120 && h<=80){
  if(lv_obj_get_style_radius(o,0)<w/4)lv_obj_set_style_radius(o,LV_RADIUS_CIRCLE,0);
  ring_button_style(o);
 }
}
const theme_kit_t theme_kit_ring={ .id="ring", .style=ring_style };
lv_obj_t *zune_pivot(lv_obj_t *,const char *,lv_event_cb_t);
void zhome_create(lv_obj_t *root);
void kit_fork_np_zune(void);
/* Zune: no button rings at all - its type and the accent carry everything. Ring's pass used to put a 2 px accent ring
 * on any 28-120 px tap target it met (stock pages, links, hit areas), the stray "outline" on some taps. */
static void zune_style(lv_obj_t *o,kit_role_t role){
 if(role==KIT_TITLE){lv_obj_set_style_border_width(o,0,0);lv_obj_set_style_outline_width(o,0,0);}
 if(!lv_obj_has_flag(o,LV_OBJ_FLAG_CLICKABLE))return;
 lv_obj_set_style_shadow_width(o,0,0);
 lv_obj_set_style_outline_width(o,0,LV_STATE_ANY);
}
const theme_kit_t theme_kit_zune={ .id="zune", .header=zune_pivot, .style=zune_style, .home_build=zhome_create, .fork_np_layout=kit_fork_np_zune };
const theme_kit_t theme_kit_braun={ .id="braun", .home_build=bhome_create, .fork_np_layout=kit_fork_np_braun };
