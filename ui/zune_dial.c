/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The Zune dial list - see zune_dial.h. The same geometry as Home's main menu (zune_home.c): 60 px pitch, the focus
 * line at y 180, Light 46 / 32 / 24 by distance, opacity 1 -> 0.3, every row inset from the bezel curve. */
#include "zune_dial.h"
#include "screens.h"
#include "ui.h"
#include "theme.h"
#include "theme_kit.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

LV_FONT_DECLARE(font_zune_light_24)
LV_FONT_DECLARE(font_zune_regular_16)
LV_FONT_DECLARE(font_zune_light_50)
LV_FONT_DECLARE(font_zune_regular_13)
#define ZD_PITCH 60
#define ZD_FOCUS 180
#define ZD_INSET 34

static struct {
    lv_obj_t *dial, *tick, *row[ZDIAL_ROWS], *lbl[ZDIAL_ROWS], *icon[ZDIAL_ROWS], *badge[ZDIAL_ROWS], *sub[ZDIAL_ROWS];
    signed char face[ZDIAL_ROWS][3]; /* fitted font per row/distance band; -1 until measured */
    int n, ln, copies, recentring, size[ZDIAL_ROWS], top, bottom;   /* n: rows built; ln: items (n = ln * copies) */
    void (*pick)(int);
    char action[ZDIAL_ROWS][40];
} Z;


/* scroll-time setters that do nothing when the value is already set: every LVGL style write re-resolves the object's
 * styles and repaints it, and these run for every row on every scroll step */
static inline void zg_hide(lv_obj_t *o, int h){ if(o && lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN) != (h != 0)){ if(h) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN); else lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN); } }
static inline void zg_opa(lv_obj_t *o, lv_opa_t v){ v = v >= 248 ? 255 : (lv_opa_t)(v & 0xF8); if(o && lv_obj_get_style_text_opa(o, 0) != v) lv_obj_set_style_text_opa(o, v, 0); }
static inline void zg_align(lv_obj_t *o, int x, int y){
    if(!o) return;
    if(lv_obj_get_style_align(o, 0) == LV_ALIGN_LEFT_MID && lv_obj_get_style_x(o, 0) == x && lv_obj_get_style_y(o, 0) == y) return;
    lv_obj_align(o, LV_ALIGN_LEFT_MID, x, y);
}
static const lv_font_t *step_font(int s){ return s == 0 ? TF(UI_46) : s == 1 ? TF(UI_32) : TF(UI_24); }
/* Scroll positions are integer pixels. Keep the exact old float rounding, but calculate
 * it once per distance rather than doing square roots in every animation callback.
 * Shared by Home, action dials and the library; 360 bytes, no heap or extra timer. */
int zune_curve_inset(int distance, int nearest){
    static uint8_t inset[2][180];static int ready;
    if(!ready){
        for(int d=0;d<180;d++){
            float span=sqrtf(32400.0f-(float)(d*d));
            inset[0][d]=(uint8_t)(180-(int)span);
            inset[1][d]=(uint8_t)lroundf(180.0f-span);
        }
        ready=1;
    }
    if(distance<-179||distance>179)distance=179;
    else if(distance<0)distance=-distance;
    return inset[!!nearest][distance];
}
static int edge_x(int y){return zune_curve_inset(y-180,1)+ZD_INSET;}
static void layout(void){
    if(!Z.dial) return;
    lv_area_t da; lv_obj_get_coords(Z.dial, &da);
    for(int i = 0; i < Z.n; i++){
        lv_area_t a; lv_obj_get_coords(Z.row[i], &a);
        int yc = (a.y1 + a.y2) / 2 - da.y1, d = abs(yc - ZD_FOCUS);
        int off = d > 200 || yc < Z.top || (Z.bottom && yc > Z.bottom);
        lv_obj_t *part[3] = { Z.lbl[i], Z.icon[i], Z.badge[i] };
        if(Z.sub[i]) zg_hide(Z.sub[i], off || d >= ZD_PITCH / 2);      /* the line under the item: only on the focus line */
        for(int k = 0; k < 3; k++) zg_hide(part[k], off);
        if(off) continue;
        int s = d < ZD_PITCH / 2 ? 0 : d < ZD_PITCH * 3 / 2 ? 1 : 2;
        if(s != Z.size[i]){
            Z.size[i]=s;int face=Z.face[i][s];
            if(face<0){
                face=Z.sub[i]&&s==0?1:s;lv_point_t text;
                while(face<2){lv_text_get_size(&text,lv_label_get_text(Z.lbl[i]),step_font(face),0,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);if(text.x<=286)break;face++;}
                Z.face[i][s]=(signed char)face;
            }
            const lv_font_t *font=step_font(face);
            if(lv_obj_get_style_text_font(Z.lbl[i],0)!=font)lv_obj_set_style_text_font(Z.lbl[i],font,0);
        }
        float o = 1.0f - (float)d / ZD_PITCH * 0.4f; if(o < 0.3f) o = 0.3f;
        zg_opa(Z.lbl[i], (lv_opa_t)(o * 255));
        int x = edge_x(yc);
        if(Z.icon[i]){                                     /* an icon before the label, sized and faded with it */
            static const int px[3] = { 22, 16, 14 };          /* Figma 16: 22 / 16 / 14 px beside 40 / 26 / 20 */
            int sc = px[s] * 256 / 28;
            if(lv_obj_get_style_transform_scale_x(Z.icon[i], 0) != sc) lv_obj_set_style_transform_scale(Z.icon[i], sc, 0);
            zg_opa(Z.icon[i], (lv_opa_t)(o * 255));
            zg_align(Z.icon[i], x + px[s] / 2 - 14, 0);
            if(Z.badge[i]){ zg_opa(Z.badge[i], (lv_opa_t)(o * 255)); zg_align(Z.badge[i], x + px[s] / 2 - 3, 0); }
            x += px[s] + (s == 0 ? 12 : 10);
        }
        int lift = (Z.sub[i] && s == 0) ? -12 : 0;
        zg_align(Z.lbl[i], x, lift);
        if(Z.sub[i] && s == 0) zg_align(Z.sub[i], x + 3, 23);
    }
}
static void scroll_cb(lv_event_t *e){ (void)e; layout(); }
static void scroll_end_cb(lv_event_t *e){                 /* a looping dial settled in an outer copy: hop to the middle one */
    (void)e; if(!Z.dial || Z.copies < 3 || Z.recentring) return;
    int y = lv_obj_get_scroll_y(Z.dial), f = (y + ZD_PITCH / 2) / ZD_PITCH;
    if(f >= Z.ln && f < 2 * Z.ln) return;
    Z.recentring = 1;
    lv_obj_scroll_to_y(Z.dial, y + (f < Z.ln ? Z.ln : -Z.ln) * ZD_PITCH, LV_ANIM_OFF);
    Z.recentring = 0;
    layout();
}
static int g_wrap_next;
void zdial_wrap_next(void){ g_wrap_next = 1; }
static void del_cb(lv_event_t *e){ if(lv_event_get_target(e) == Z.dial){ Z.dial = NULL; Z.n = 0; } }
static void item_cb(lv_event_t *e){ int i = (int)(intptr_t)lv_event_get_user_data(e); if(Z.pick && i >= 0 && i < Z.ln) Z.pick(i); }

void zdial_build(lv_obj_t *parent, const char *const *labels, int n, int focus, void (*pick)(int), int top, const char *action){
    if(n > ZDIAL_MAX) n = ZDIAL_MAX;
    memset(&Z, 0, sizeof Z);
    memset(Z.face,-1,sizeof Z.face);
    Z.copies = (g_wrap_next && n >= 3) ? 3 : 1; g_wrap_next = 0;
    Z.ln = n; Z.n = n * Z.copies; Z.pick = pick; Z.top = top;
    Z.dial = lv_obj_create(parent);
    lv_obj_remove_style_all(Z.dial); kit_keep(Z.dial);
    lv_obj_set_size(Z.dial, 360, 360); lv_obj_set_pos(Z.dial, 0, 0);
    lv_obj_set_flex_flow(Z.dial, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_top(Z.dial, ZD_FOCUS - ZD_PITCH / 2, 0);
    lv_obj_set_style_pad_bottom(Z.dial, 360 - ZD_FOCUS - ZD_PITCH / 2, 0);
    lv_obj_set_scroll_dir(Z.dial, LV_DIR_VER);
    lv_obj_set_scroll_snap_y(Z.dial, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_scrollbar_mode(Z.dial, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(Z.dial, LV_OBJ_FLAG_SCROLL_MOMENTUM);          /* free-flowing: a flick runs on and settles on an item */
    lv_obj_remove_flag(Z.dial, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_add_event_cb(Z.dial, scroll_cb, LV_EVENT_SCROLL, NULL);
    lv_obj_add_event_cb(Z.dial, scroll_end_cb, LV_EVENT_SCROLL_END, NULL);
    lv_obj_add_event_cb(Z.dial, del_cb, LV_EVENT_DELETE, NULL);
    int mid = Z.copies == 3 ? 1 : 0;                                   /* the copy that carries the action names */
    for(int i = 0; i < Z.n; i++){
        int li = i % n, c = i / n;
        lv_obj_t *r = lv_obj_create(Z.dial);
        lv_obj_remove_style_all(r); kit_keep(r);
        lv_obj_set_size(r, 360, ZD_PITCH);
        lv_obj_remove_flag(r, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(r, LV_OBJ_FLAG_SNAPPABLE);
        if(c == mid){
            snprintf(Z.action[i], sizeof Z.action[i], "%s.%d", action ? action : "zdial", li);
            ui_on(r, item_cb, LV_EVENT_CLICKED, (void *)(intptr_t)li, Z.action[i], UI_CORE);
        } else lv_obj_add_event_cb(r, item_cb, LV_EVENT_CLICKED, (void *)(intptr_t)li);
        Z.row[i] = r;
        lv_obj_t *l = lv_label_create(r); kit_keep(l);
        lv_label_set_text(l, labels[li]);
        lv_obj_set_style_text_color(l, TC(TEXT_PRIMARY), 0);
        lv_obj_set_style_text_font(l, TF(UI_32), 0);
        lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
        Z.lbl[i] = l; Z.size[i] = -1;
    }
    Z.tick = lv_arc_create(parent); kit_keep(Z.tick);                     /* the focus tick on the left rim */
    lv_obj_remove_style(Z.tick, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(Z.tick, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(Z.tick, 352, 352); lv_obj_center(Z.tick);
    lv_arc_set_bg_angles(Z.tick, 174, 186); lv_arc_set_value(Z.tick, 0);
    lv_obj_set_style_arc_width(Z.tick, 4, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(Z.tick, true, LV_PART_MAIN);
    lv_obj_set_style_arc_color(Z.tick, TC(ACCENT_PRIMARY), LV_PART_MAIN);
    lv_obj_set_style_arc_width(Z.tick, 0, LV_PART_INDICATOR);
    lv_obj_update_layout(Z.dial);
    if(focus < 0) focus = 0; if(focus >= n) focus = n - 1;
    lv_obj_scroll_to_y(Z.dial, ZD_PITCH * (focus + mid * n), LV_ANIM_OFF);
    layout();
}
void zdial_set_bottom(int y){ Z.bottom = y; layout(); }
static void zdial_set_icon1(int i, const char *glyph, const char *badge);
void zdial_set_icon(int i, const char *glyph, const char *badge){
    if(!Z.dial || i < 0 || i >= Z.ln || !glyph) return;
    for(int c = 0; c < Z.copies; c++) zdial_set_icon1(i + c * Z.ln, glyph, badge);
}
static void zdial_set_icon1(int i, const char *glyph, const char *badge){
    lv_obj_t *g = lv_label_create(Z.row[i]); kit_keep(g);
    lv_label_set_text(g, glyph);
    lv_obj_set_style_text_font(g, TF(ICON_28), 0); lv_obj_set_style_text_color(g, lv_obj_get_style_text_color(Z.lbl[i], 0), 0);
    lv_obj_set_style_transform_pivot_x(g, LV_PCT(50), 0); lv_obj_set_style_transform_pivot_y(g, LV_PCT(50), 0);
    lv_obj_remove_flag(g, LV_OBJ_FLAG_CLICKABLE);
    Z.icon[i] = g;
    if(badge){ lv_obj_t *b = lv_label_create(Z.row[i]); kit_keep(b); lv_label_set_text(b, badge);
               lv_obj_set_style_text_font(b, TF(UI_10), 0); lv_obj_set_style_text_color(b, lv_obj_get_style_text_color(Z.lbl[i], 0), 0);
               lv_obj_remove_flag(b, LV_OBJ_FLAG_CLICKABLE); Z.badge[i] = b; }
    Z.size[i] = -1; layout();
}
void zdial_set_color(int li, lv_color_t c){
    if(!Z.dial || li < 0 || li >= Z.ln) return;
    for(int k = 0; k < Z.copies; k++){ int i = li + k * Z.ln;
        lv_obj_set_style_text_color(Z.lbl[i], c, 0);
        if(Z.icon[i]) lv_obj_set_style_text_color(Z.icon[i], c, 0);
        if(Z.badge[i]) lv_obj_set_style_text_color(Z.badge[i], c, 0); }
}
static void zdial_set_sub1(int i, const char *t);
void zdial_set_sub(int i, const char *t){
    if(!Z.dial || i < 0 || i >= Z.ln || !t || !t[0]) return;
    for(int c = 0; c < Z.copies; c++) zdial_set_sub1(i + c * Z.ln, t);
}
static void zdial_set_sub1(int i, const char *t){
    if(!Z.sub[i]){ Z.sub[i] = lv_label_create(Z.row[i]); kit_keep(Z.sub[i]);
                   lv_obj_set_style_text_font(Z.sub[i], zune_font(&font_zune_regular_16), 0); lv_obj_set_style_text_color(Z.sub[i], TC(TEXT_MUTED), 0);
                   lv_obj_remove_flag(Z.sub[i], LV_OBJ_FLAG_CLICKABLE); lv_obj_add_flag(Z.sub[i], LV_OBJ_FLAG_IGNORE_LAYOUT); }
    lv_label_set_text(Z.sub[i], t);
    memset(Z.face[i],-1,sizeof Z.face[i]);
    Z.size[i] = -1; layout();
}
int zdial_focus(void){                                    /* the item on the focus line now */
    if(!Z.dial) return -1;
    int i = (lv_obj_get_scroll_y(Z.dial) + ZD_PITCH / 2) / ZD_PITCH;
    i = i < 0 ? 0 : i >= Z.n ? Z.n - 1 : i;
    return i % Z.ln;
}
void zdial_set_text(int i, const char *t){
    if(!Z.dial||i<0||i>=Z.ln||!t)return;
    int changed=0;
    for(int c=0;c<Z.copies;c++){
        int row=i+c*Z.ln;
        if(!strcmp(lv_label_get_text(Z.lbl[row]),t))continue;
        lv_label_set_text(Z.lbl[row],t);memset(Z.face[row],-1,sizeof Z.face[row]);Z.size[row]=-1;changed=1;
    }
    if(changed)layout();
}

/* ---- shared Zune pieces ---- */
#include "ipc.h"
const lv_font_t *zune_font(const lv_font_t *base){
    static struct { const lv_font_t *base; lv_font_t copy; } cache[40];
    for(int i = 0; i < 40; i++){
        if(cache[i].base == base) return &cache[i].copy;
        if(!cache[i].base){
            cache[i].base = base; cache[i].copy = *base;
            cache[i].copy.fallback = TF(USER_20);              /* the theme's user font: its fallback is the CJK chain */
            return &cache[i].copy;
        }
    }
    return base;
}

typedef struct zrim { lv_obj_t *arc; int scr; struct zrim *next; } zrim_t;
static zrim_t *z_rims;
static void rim_tick(zrim_t *r){
    if(!r||!r->arc||screen_current()!=r->scr||(r->scr==SCR_NOWPLAYING&&ui_np_fsart_active()))return;
    track_state_t st; ipc_get_state(&st);
    int v = (st.have_track && st.duration_ms > 0) ? (int)((long long)st.position_ms * 1000 / st.duration_ms) : 0;
    if(v < 0) v = 0; if(v > 1000) v = 1000;
    if(v != lv_arc_get_value(r->arc)) lv_arc_set_value(r->arc, v);
}
static void rim_del(lv_event_t *e){ zrim_t *r=lv_event_get_user_data(e);zrim_t **p=&z_rims;while(*p&&*p!=r)p=&(*p)->next;if(*p)*p=r->next;free(r); }
lv_obj_t *zune_rim(lv_obj_t *parent, int scr, int knob){
    lv_obj_t *a = lv_arc_create(parent); kit_keep(a);
    lv_obj_set_size(a, 352, 352); lv_obj_center(a);
    lv_obj_remove_flag(a, LV_OBJ_FLAG_CLICKABLE);
    lv_arc_set_rotation(a, 270); lv_arc_set_bg_angles(a, 0, 360); lv_arc_set_range(a, 0, 1000); lv_arc_set_value(a, 0);
    lv_obj_set_style_arc_width(a, 3, LV_PART_MAIN); lv_obj_set_style_arc_width(a, 3, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(a, TC(CONTROL_TRACK), LV_PART_MAIN);
    lv_obj_set_style_arc_color(a, TC(ACCENT_PRIMARY), LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(a, false, LV_PART_INDICATOR);
    if(knob){ lv_obj_set_style_bg_color(a, TC(ACCENT_PRIMARY), LV_PART_KNOB); lv_obj_set_style_bg_opa(a, LV_OPA_COVER, LV_PART_KNOB);
              lv_obj_set_style_pad_all(a, 4, LV_PART_KNOB); }
    else lv_obj_remove_style(a, NULL, LV_PART_KNOB);
    zrim_t *r = calloc(1, sizeof *r);
    if(r){ r->arc = a; r->scr = scr; r->next=z_rims;z_rims=r;
           lv_obj_add_event_cb(a, rim_del, LV_EVENT_DELETE, r); rim_tick(r); }
    return a;
}

#define ZP_X 100   /* the pivot title's left edge: its back ring (x 52..88) then sits inside the round glass at that height */
static void pivot_back_cb(lv_event_t *e){ (void)e; screen_back(); }
lv_obj_t *zune_pivot(lv_obj_t *root, const char *title, lv_event_cb_t back_cb){
    /* Secondary headings stay inside the glass; library pivots set their own face. */
    LV_FONT_DECLARE(font_zune_light_40) LV_FONT_DECLARE(font_zune_light_32) LV_FONT_DECLARE(font_zune_light_28) LV_FONT_DECLARE(font_zune_light_24)
    const lv_font_t *faces[]={&font_zune_light_40,&font_zune_light_32,&font_zune_light_28,&font_zune_light_24};
    const lv_font_t *f=zune_font(faces[3]);
    for(int i=0;i<4;i++){lv_point_t sz;lv_text_get_size(&sz,title,zune_font(faces[i]),-1,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);if(sz.x<=224){f=zune_font(faces[i]);break;}}
    lv_obj_t *t = lv_label_create(root); kit_keep(t);
    lv_label_set_text(t, title);
    lv_label_set_long_mode(t, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(t, f, 0);
    lv_obj_set_style_text_color(t, TC(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_letter_space(t, -1, 0);                  /* -3% tracking at 50 px */
    lv_obj_set_width(t,224);lv_obj_set_pos(t, ZP_X, 40);                                       /* glyph box: the cap line lands at y ~40 */
    lv_obj_remove_flag(t, LV_OBJ_FLAG_CLICKABLE);
    /* the ring is drawn by a plain, non-clickable object; the tap lands on a small invisible target with a wide hit
     * area (the theme's styling pass would restyle a clickable 36 px circle as one of its own buttons) */
    lv_obj_t *ring = lv_obj_create(root); kit_keep(ring); lv_obj_remove_style_all(ring);
    lv_obj_set_size(ring, 36, 36); lv_obj_set_pos(ring, ZP_X - 48, 40 + 26 - 18);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(ring, 2, 0); lv_obj_set_style_border_color(ring, TC(TEXT_PRIMARY), 0);
    lv_obj_remove_flag(ring, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *g = lv_label_create(ring); kit_keep(g);
    lv_label_set_text(g, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_font(g, TF(UI_14), 0); lv_obj_set_style_text_color(g, TC(TEXT_PRIMARY), 0);
    lv_obj_center(g);
    lv_obj_t *hit = lv_obj_create(root); kit_keep(hit); lv_obj_remove_style_all(hit);
    lv_obj_set_size(hit, 20, 20); lv_obj_set_pos(hit, ZP_X - 48 + 8, 40 + 26 - 18 + 8);
    lv_obj_add_flag(hit, LV_OBJ_FLAG_CLICKABLE); lv_obj_remove_flag(hit, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_ext_click_area(hit, 18);
    ui_on(hit, back_cb ? back_cb : pivot_back_cb, LV_EVENT_CLICKED, NULL, "zune.pivot.back", UI_CORE);
    return t;
}

void zhome_status_parts(char *t, size_t n, int *pct, int *chg);
int ui_sleep_state(int *secs_left);
int usage_plugged_in(void);
LV_FONT_DECLARE(font_zune_bold_14)
/* the status line (Figma 66): [moon + minutes of a running sleep timer] time [bolt] [battery]. The battery is drawn: an
 * outline with a fill to the level (red at 15% and under, unless on the charger) and a small nub; on the charger a bolt
 * in the accent stands before it. */
#define ZSTAT_MAX 32
static struct { lv_obj_t *row, *moon, *sleep, *l, *bolt, *batt, *fill; int scr; } g_zst[ZSTAT_MAX];

static void show_if(lv_obj_t *o, int on){ if(!o || lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN) == !on) return; if(on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN); }   /* only on a change: each flip re-lays the flex row */
static void zst_paint(int k){
    if(!g_zst[k].row) return;
    char t[24]; int pct = -1, chg = 0; zhome_status_parts(t, sizeof t, &pct, &chg);
    if(g_zst[k].scr==SCR_QUICK&&pct>=0){char clock[24];snprintf(clock,sizeof clock,"%s",t);snprintf(t,sizeof t,"%.12s   %d%%",clock,pct>100?100:pct);}
    if(strcmp(lv_label_get_text(g_zst[k].l), t)) lv_label_set_text(g_zst[k].l, t);
    int on_power = chg || usage_plugged_in() == 1;
    show_if(g_zst[k].batt, pct >= 0&&g_zst[k].scr!=SCR_QUICK); show_if(g_zst[k].bolt, pct >= 0 && on_power);
    if(pct >= 0){
        int p = pct > 100 ? 100 : pct, w = (18 * p + 50) / 100, low = !on_power && p <= 15; if(w < 3 && p > 0) w = 3;
        if(lv_obj_get_style_width(g_zst[k].fill,0)!=w)lv_obj_set_width(g_zst[k].fill,w);                         /* (a no-op when unchanged) */
        lv_color_t c = low ? TC(STATUS_DANGER) : TC(TEXT_PRIMARY);
        if(!lv_color_eq(lv_obj_get_style_bg_color(g_zst[k].fill, 0), c)){   /* every 2 s on every status bar: write only on a change */
            lv_obj_set_style_bg_color(g_zst[k].fill, c, 0);
            lv_obj_set_style_border_color(lv_obj_get_parent(g_zst[k].fill), c, 0);
        }
    }
    int secs = 0, on = ui_sleep_state(&secs) == 1;                 /* a running sleep timer: moon + "38m" in the accent */
    if(on){ char m[16]; snprintf(m, sizeof m, "%dm", (secs + 59) / 60);
            if(strcmp(lv_label_get_text(g_zst[k].sleep), m)) lv_label_set_text(g_zst[k].sleep, m); }
    show_if(g_zst[k].sleep, on); show_if(g_zst[k].moon, on);
}
void zune_status_refresh(void){ for(int k = 0; k < ZSTAT_MAX; k++) if(g_zst[k].row && !(g_zst[k].scr==SCR_NOWPLAYING&&ui_np_fsart_active()) && lv_obj_is_visible(g_zst[k].row)) zst_paint(k); }   /* all of them: a screen shown next is current already */
void zune_ui_tick(void){
 static uint32_t at;if(ui_display_is_off() || !th_zune() || lv_tick_elaps(at)<1000)return;at=lv_tick_get();
 zune_status_refresh();for(zrim_t *r=z_rims;r;r=r->next)rim_tick(r);
 if(screen_current()==SCR_SAVER){void zsaver_progress_tick(void);zsaver_progress_tick();}
}
static void zst_del(lv_event_t *e){ lv_obj_t *o = lv_event_get_target(e); for(int k = 0; k < ZSTAT_MAX; k++) if(g_zst[k].row == o) g_zst[k].row = NULL; }
static lv_obj_t *zplain(lv_obj_t *p, int w, int h){
    lv_obj_t *o = lv_obj_create(p); lv_obj_remove_style_all(o); kit_keep(o);
    lv_obj_set_size(o, w, h); lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE); return o;
}
lv_obj_t *zune_status(lv_obj_t *root, int scr){
    lv_obj_t *row = zplain(root, 220, LV_SIZE_CONTENT); lv_obj_align(row, LV_ALIGN_TOP_MID, 0, 14);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 5, 0);
    /* the moon: an accent disc with a black one bitten out of it */
    lv_obj_t *moon = zplain(row, 10, 10); lv_obj_set_style_clip_corner(moon, true, 0); lv_obj_set_style_radius(moon, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(moon, TC(ACCENT_PRIMARY), 0); lv_obj_set_style_bg_opa(moon, LV_OPA_COVER, 0);
    lv_obj_t *bite = zplain(moon, 9, 9); lv_obj_set_pos(bite, 4, -2); lv_obj_set_style_radius(bite, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(bite, TC(CANVAS), 0); lv_obj_set_style_bg_opa(bite, LV_OPA_COVER, 0);
    lv_obj_t *sl = lv_label_create(row); kit_keep(sl); lv_label_set_text(sl, "");
    lv_obj_set_style_text_font(sl, TF(UI_12), 0); lv_obj_set_style_text_color(sl, TC(ACCENT_PRIMARY), 0);
    lv_obj_set_style_margin_right(sl, 3, 0);
    lv_obj_t *l = lv_label_create(row); kit_keep(l); lv_label_set_text(l, "");
    lv_obj_set_style_text_font(l,TF(UI_16),0);lv_obj_set_style_text_color(l,TC(TEXT_PRIMARY),0);
    lv_obj_t *bolt = lv_label_create(row); kit_keep(bolt); lv_label_set_text(bolt, LV_SYMBOL_CHARGE);
    lv_obj_set_style_text_font(bolt, TF(UI_10), 0); lv_obj_set_style_text_color(bolt, TC(ACCENT_PRIMARY), 0);
    lv_obj_set_style_margin_left(bolt, 3, 0); lv_obj_set_style_margin_right(bolt, -3, 0);
    lv_obj_t *batt = zplain(row, 27, 14);                          /* body 18 x 10 + a 2 x 4 nub */
    lv_obj_t *body = zplain(batt, 22, 12); lv_obj_set_style_radius(body, 2, 0);
    lv_obj_set_style_border_width(body, 1, 0); lv_obj_set_style_border_color(body, TC(TEXT_PRIMARY), 0);
    lv_obj_set_style_border_opa(body, LV_OPA_70, 0);
    lv_obj_t *fill = zplain(body, 18, 8); lv_obj_set_pos(fill, 2, 2); lv_obj_set_style_radius(fill, 1, 0);
    lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);
    lv_obj_t *nub = zplain(batt, 3, 6); lv_obj_set_pos(nub, 23, 3); lv_obj_set_style_radius(nub, 1, 0);
    lv_obj_set_style_bg_color(nub, TC(TEXT_PRIMARY), 0); lv_obj_set_style_bg_opa(nub, LV_OPA_70, 0);
    int k = 0; while(k < ZSTAT_MAX && g_zst[k].row) k++;
    if(k == ZSTAT_MAX) k = 0;
    g_zst[k].row = row; g_zst[k].moon = moon; g_zst[k].sleep = sl; g_zst[k].l = l; g_zst[k].bolt = bolt;
    g_zst[k].batt = batt; g_zst[k].fill = fill; g_zst[k].scr = scr;
    lv_obj_add_event_cb(row, zst_del, LV_EVENT_DELETE, NULL);
    zst_paint(k);
    return row;
}

/* ---- confirm (Figma 07): a question, a quiet line, and the lower glass split into cancel | <yes> (the accent) ---- */
LV_FONT_DECLARE(font_zune_light_32) LV_FONT_DECLARE(font_zune_regular_14) LV_FONT_DECLARE(font_zune_regular_19)
static lv_obj_t *g_zc; static void (*g_zc_yes)(void);
static void zc_close(void){ if(g_zc){ lv_obj_delete_async(g_zc); g_zc = NULL; } }
static void zc_no_cb(lv_event_t *e){ if(lv_event_get_code(e) == LV_EVENT_CLICKED) zc_close(); }
static void zc_yes_cb(lv_event_t *e){
    if(lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    void (*f)(void) = g_zc_yes; zc_close(); if(f) f();
}
static void zc_lower(char *o, size_t n, const char *s){             /* the Zune voice, unless it quotes a name */
    snprintf(o, n, "%s", s ? s : "");
    if(strchr(o, '"')){ if(o[0] >= 'A' && o[0] <= 'Z') o[0] += 32; return; }
    for(char *c = o; *c; c++) if(*c >= 'A' && *c <= 'Z') *c += 32;
}
static lv_obj_t *zc_half(lv_obj_t *o, int x, int w, lv_color_t bg, lv_color_t fg, const char *txt, lv_event_cb_t cb, const char *act){
    lv_obj_t *b = lv_obj_create(o); lv_obj_remove_style_all(b); kit_keep(b);
    lv_obj_set_size(b, w, 128); lv_obj_set_pos(b, x, 232);
    lv_obj_set_style_bg_color(b, bg, 0); lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_opa(b, LV_OPA_70, LV_STATE_PRESSED);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE); lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *l = lv_label_create(b); kit_keep(l); lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, zune_font(&font_zune_regular_19), 0); lv_obj_set_style_text_color(l, fg, 0);
    lv_obj_align(l, LV_ALIGN_TOP_MID, x ? -8 : 8, 32);                 /* nudged in, away from the rim */
    ui_on(b, cb, LV_EVENT_CLICKED, NULL, act, UI_CORE);
    return b;
}
void zune_confirm(const char *title, const char *body, const char *no, const char *yes, void (*on_yes)(void)){
    zc_close(); g_zc_yes = on_yes;
    lv_obj_t *o = lv_obj_create(lv_layer_top()); lv_obj_remove_style_all(o); kit_keep(o);
    lv_obj_set_size(o, 360, 360);
    lv_obj_set_style_bg_color(o, TC(CANVAS), 0); lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE); lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    g_zc = o;
    char b[200];
    zc_lower(b, sizeof b, title);
    lv_obj_t *t = lv_label_create(o); kit_keep(t); lv_label_set_text(t, b);
    LV_FONT_DECLARE(font_zune_light_28)
    const lv_font_t *faces[]={&font_zune_light_32,&font_zune_light_28,&font_zune_light_24};const lv_font_t*f=zune_font(faces[2]);
    for(int i=0;i<3;i++){lv_point_t text;lv_text_get_size(&text,b,zune_font(faces[i]),0,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);if(text.x<=280){f=zune_font(faces[i]);break;}}
    lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);lv_obj_set_size(t,280,lv_font_get_line_height(f));
    lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(t, f, 0); lv_obj_set_style_text_color(t, TC(TEXT_PRIMARY), 0);
    lv_obj_align(t, LV_ALIGN_BOTTOM_MID, 0, -(360 - 120));               /* its last line sits on y 120 */
    if(body && body[0]){
        zc_lower(b, sizeof b, body);
        lv_obj_t *area=lv_obj_create(o);lv_obj_remove_style_all(area);kit_keep(area);lv_obj_set_pos(area,46,132);lv_obj_set_size(area,268,88);
        lv_obj_set_scroll_dir(area,LV_DIR_VER);lv_obj_set_scrollbar_mode(area,LV_SCROLLBAR_MODE_AUTO);
        lv_obj_t *d = lv_label_create(area); kit_keep(d); lv_label_set_text(d, b);
        lv_label_set_long_mode(d, LV_LABEL_LONG_WRAP); lv_obj_set_width(d, 260);
        lv_obj_set_style_text_align(d, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(d, zune_font(&font_zune_regular_16), 0); lv_obj_set_style_text_color(d, TC(TEXT_MUTED), 0);
        lv_obj_align(d, LV_ALIGN_TOP_MID, 0, 0);
    }
    char nb[40], yb[40]; zc_lower(nb, sizeof nb, no && no[0] ? no : "cancel"); zc_lower(yb, sizeof yb, yes && yes[0] ? yes : "ok");
    zc_half(o, 0, 178, TC(SURFACE), TC(TEXT_PRIMARY), nb, zc_no_cb, "zune.confirm.no");
    zc_half(o, 182, 178, TC(ACCENT_PRIMARY), TC(ON_ACCENT), yb, zc_yes_cb, "zune.confirm.yes");
}
int zune_confirm_open(void){ return g_zc != NULL; }
void zune_confirm_close(void){ zc_close(); }

/* ---- Wi-Fi / Bluetooth (Figma 44 / 46): the pivot header, "<radio> on" with its switch, the list below hugging the
 * glass, a quiet accent "scan" link at the foot; the stock status ring and big name step aside ---- */
LV_FONT_DECLARE(font_zune_regular_12)
void zune_radio_skin(lv_obj_t *root, const char *title, const char *onlabel, const char *scanlabel, lv_obj_t *ring, lv_obj_t *glyph,
                     lv_obj_t *name, lv_obj_t *sub, lv_obj_t *sw, lv_obj_t *rb, lv_obj_t *list){
    lv_obj_add_flag(ring, LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(glyph, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(name, LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(sub, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(list, 0, 136); lv_obj_set_size(list, 360, 176); lv_obj_set_style_pad_bottom(list, 48, 0);   /* ends above the scan link: no row runs under it */
    for(int k = 0; k < 2; k++){
        lv_obj_t *f = lv_obj_create(root); lv_obj_remove_style_all(f); kit_keep(f);
        lv_obj_set_size(f, 360, k ? 96 : 140); lv_obj_align(f, k ? LV_ALIGN_BOTTOM_MID : LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_set_style_bg_color(f, TC(CANVAS), 0); lv_obj_set_style_bg_grad_color(f, TC(CANVAS), 0);
        lv_obj_set_style_bg_grad_dir(f, LV_GRAD_DIR_VER, 0); lv_obj_set_style_bg_opa(f, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_main_opa(f, k ? LV_OPA_TRANSP : LV_OPA_COVER, 0); lv_obj_set_style_bg_grad_opa(f, k ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_main_stop(f, k ? 0 : 200, 0); lv_obj_set_style_bg_grad_stop(f, k ? 150 : 255, 0);
        lv_obj_remove_flag(f, LV_OBJ_FLAG_CLICKABLE);
    }
    zune_pivot(root, title, NULL);
    zune_status(root, -1);
    lv_obj_t *l = lv_label_create(root); kit_keep(l); lv_label_set_text(l, onlabel);
    lv_obj_set_style_text_font(l, zune_font(&font_zune_regular_14), 0); lv_obj_set_style_text_color(l, TC(TEXT_PRIMARY), 0);
    lv_obj_set_pos(l, 56, 106);
    lv_obj_move_foreground(sw); kit_keep(sw);
    lv_obj_set_size(sw, 40, 22); lv_obj_align_to(sw, l, LV_ALIGN_OUT_RIGHT_MID, 14, 0);
    lv_obj_set_style_bg_color(sw, TC(CONTROL_TRACK), LV_PART_MAIN); lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(sw, TC(ACCENT_PRIMARY), (lv_style_selector_t)LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(sw, TC(FIXED_MEDIA_WHITE), LV_PART_KNOB); lv_obj_set_style_pad_all(sw, -3, LV_PART_KNOB);
    lv_obj_set_style_border_width(sw, 0, LV_PART_MAIN);
    lv_obj_move_foreground(rb); kit_keep(rb);
    lv_obj_set_style_bg_opa(rb, LV_OPA_TRANSP, 0); lv_obj_set_style_bg_opa(rb, LV_OPA_TRANSP, LV_STATE_PRESSED);
    lv_obj_set_size(rb, 160, 28); lv_obj_align(rb, LV_ALIGN_TOP_MID, 0, 314);
    lv_obj_t *rl = lv_obj_get_child(rb, 0);
    if(rl){ kit_keep(rl); lv_label_set_text(rl, scanlabel); lv_obj_set_style_text_font(rl, zune_font(&font_zune_regular_14), 0);
            lv_obj_set_style_text_color(rl, TC(ACCENT_PRIMARY), 0); lv_obj_center(rl); }
}
/* one list row: transparent, the name in Regular 19 and its line under it (accent when connected) */
void zune_radio_row(lv_obj_t *r, lv_obj_t *name, const char *line, int on){
    lv_obj_set_style_bg_opa(r, LV_OPA_TRANSP, 0); lv_obj_set_style_bg_opa(r, LV_OPA_TRANSP, LV_STATE_PRESSED);
    lv_obj_set_style_opa(r, LV_OPA_70, LV_STATE_PRESSED); kit_keep(r);
    lv_obj_set_width(r, 300); lv_obj_set_height(r, 50);
    lv_obj_set_style_text_font(name, zune_font(&font_zune_regular_19), 0); lv_obj_set_style_text_color(name, TC(TEXT_PRIMARY), 0);
    lv_obj_set_pos(name, 12, 4); kit_keep(name);
    if(line){
        lv_obj_t *s = lv_label_create(r); kit_keep(s); lv_label_set_text(s, line);
        lv_obj_set_style_text_font(s, zune_font(&font_zune_regular_16), 0);
        lv_obj_set_style_text_color(s, on ? TC(ACCENT_PRIMARY) : TC(TEXT_MUTED), 0); lv_obj_set_pos(s, 14, 30);
    }
}

LV_FONT_DECLARE(font_zune_regular_17)
/* ---- details pages (Figma 73 / 75): the pivot header, then a column of "name over value" rows; actions in the accent ---- */
lv_obj_t *zune_info_page(lv_obj_t *root, const char *title, lv_event_cb_t back_cb){
    lv_obj_t *list = lv_obj_create(root); lv_obj_remove_style_all(list); kit_keep(list);
    lv_obj_set_pos(list, 0, 104); lv_obj_set_size(list, 360, 256);
    lv_obj_set_style_pad_left(list, 46, 0); lv_obj_set_style_pad_bottom(list, 90, 0); lv_obj_set_style_pad_row(list, 6, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(list, LV_DIR_VER); lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_t *f = lv_obj_create(root); lv_obj_remove_style_all(f); kit_keep(f);
    lv_obj_set_size(f, 360, 90); lv_obj_align(f, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(f, TC(CANVAS), 0); lv_obj_set_style_bg_grad_color(f, TC(CANVAS), 0);
    lv_obj_set_style_bg_grad_dir(f, LV_GRAD_DIR_VER, 0); lv_obj_set_style_bg_opa(f, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_main_opa(f, LV_OPA_TRANSP, 0); lv_obj_set_style_bg_grad_opa(f, LV_OPA_COVER, 0);
    lv_obj_remove_flag(f, LV_OBJ_FLAG_CLICKABLE);
    zune_pivot(root, title, back_cb);
    zune_status(root, -1);
    return list;
}
static void zlow_buf(char *o, size_t n, const char *s){ size_t i = 0; for(; s && s[i] && i + 1 < n; i++) o[i] = (s[i] >= 'A' && s[i] <= 'Z') ? s[i] + 32 : s[i]; o[i] = 0; }
void zune_info_row(lv_obj_t *list, const char *key, const char *val){
    lv_obj_t *r = lv_obj_create(list); lv_obj_remove_style_all(r); kit_keep(r);
    lv_obj_set_size(r, 270, 56); lv_obj_remove_flag(r, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    char k[48]; zlow_buf(k, sizeof k, key);
    lv_obj_t *kl = lv_label_create(r); kit_keep(kl); lv_label_set_text(kl, k);
    lv_obj_set_style_text_font(kl, zune_font(&font_zune_regular_19), 0); lv_obj_set_style_text_color(kl, TC(TEXT_PRIMARY), 0);
    lv_obj_t *vl = lv_label_create(r); kit_keep(vl); lv_label_set_text(vl, val && val[0] ? val : "-");
    lv_label_set_long_mode(vl, LV_LABEL_LONG_SCROLL_CIRCULAR); lv_obj_set_width(vl, 260);
    lv_obj_set_style_text_font(vl, zune_font(&font_zune_regular_16), 0); lv_obj_set_style_text_color(vl, TC(TEXT_MUTED), 0);
    lv_obj_set_pos(vl, 2, 29);
}
lv_obj_t *zune_info_action(lv_obj_t *list, const char *label, lv_event_cb_t cb, const char *action){
    lv_obj_t *r = lv_obj_create(list); lv_obj_remove_style_all(r); kit_keep(r);
    lv_obj_set_size(r, 270, 40); lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE); lv_obj_remove_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_opa(r, LV_OPA_60, LV_STATE_PRESSED);
    ui_on(r, cb, LV_EVENT_CLICKED, NULL, action, UI_CORE);
    char k[48]; zlow_buf(k, sizeof k, label);
    lv_obj_t *l = lv_label_create(r); kit_keep(l); lv_label_set_text(l, k);
    lv_obj_set_style_text_font(l, zune_font(&font_zune_regular_17), 0); lv_obj_set_style_text_color(l, TC(ACCENT_PRIMARY), 0);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
    return r;
}

/* ---- a retype pass for the busier stock screens (stats, Last.fm, apps...): every label set in a theme font is moved to the
 * nearest Open Sans cut (Regular up to 21 px, Light above), lower-cased where it is a plain caption. Icon glyphs and text
 * already in a Zune cut are left alone. ---- */
LV_FONT_DECLARE(font_zune_regular_13) LV_FONT_DECLARE(font_zune_regular_16) LV_FONT_DECLARE(font_zune_light_24) LV_FONT_DECLARE(font_zune_light_28)
LV_FONT_DECLARE(font_zune_light_40) LV_FONT_DECLARE(font_zune_light_46) LV_FONT_DECLARE(font_zune_light_50)
static const lv_font_t *const ZRAW[] = { &font_zune_regular_12, &font_zune_regular_13, &font_zune_regular_14, &font_zune_regular_16,
    &font_zune_regular_17, &font_zune_regular_19, &font_zune_light_24, &font_zune_light_28, &font_zune_light_32, &font_zune_light_40,
    &font_zune_light_46, &font_zune_light_50 };
static const int ZRAW_PX[] = { 12, 13, 14, 16, 17, 19, 24, 28, 32, 40, 46, 50 };
static int zis_zune_font(const lv_font_t *f){
    if(!f) return 1;
    if(f->fallback == TF(USER_20)) return 1;
    for(unsigned i = 0; i < sizeof ZRAW / sizeof ZRAW[0]; i++) if(f == ZRAW[i]) return 1;
    return 0;
}
static void zretype_one(lv_obj_t *o){
    if(lv_obj_check_type(o, &lv_label_class)){
        const char *t = lv_label_get_text(o);
        if(!t || !t[0] || (unsigned char)t[0] == 0xEF) return;             /* an icon */
        const lv_font_t *f = lv_obj_get_style_text_font(o, 0);
        if(zis_zune_font(f)) return;
        int px = (int)(lv_font_get_line_height(f) / 1.22f + 0.5f), best = 0;
        for(unsigned i = 0; i < sizeof ZRAW_PX / sizeof ZRAW_PX[0]; i++) if(abs(ZRAW_PX[i] - px) < abs(ZRAW_PX[best] - px)) best = (int)i;
        if(px > 22) return;                                                  /* headlines and figures keep their own cut */
        lv_obj_set_style_text_font(o, zune_font(ZRAW[best]), 0);
        kit_keep(o);
    }
    for(uint32_t i = 0; i < lv_obj_get_child_count(o); i++) zretype_one(lv_obj_get_child(o, (int32_t)i));
}
void zune_retype(lv_obj_t *root){ if(root) zretype_one(root); }

/* ---- empty states (Figma 35): one big lower-case statement in Light, a line of help under it ---- */
void zune_empty(lv_obj_t *parent, const char *msg, const char *help){
    lv_obj_t *box = lv_obj_create(parent); lv_obj_remove_style_all(box); kit_keep(box);
    lv_obj_set_size(box, 280, LV_SIZE_CONTENT); lv_obj_remove_flag(box, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN); lv_obj_set_flex_align(box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_top(box, 24, 0); lv_obj_set_style_pad_row(box, 8, 0);
    char b[96]; zlow_buf(b, sizeof b, msg);
    lv_obj_t *t = lv_label_create(box); kit_keep(t); lv_label_set_text(t, b);
    lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP); lv_obj_set_width(t, 260); lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(t, zune_font(&font_zune_light_28), 0); lv_obj_set_style_text_color(t, TC(TEXT_PRIMARY), 0);
    if(help){
        lv_obj_t *h = lv_label_create(box); kit_keep(h); lv_label_set_text(h, help);
        lv_label_set_long_mode(h, LV_LABEL_LONG_WRAP); lv_obj_set_width(h, 220); lv_obj_set_style_text_align(h, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(h, zune_font(&font_zune_regular_16), 0); lv_obj_set_style_text_color(h, TC(TEXT_MUTED), 0);
    }
}

/* ---- library scan (Figma 34): full glass, a turning accent arc, the file count in Light, then a summary ---- */
#include "scanner.h"
#include "musicdb.h"
static struct { lv_obj_t *ov, *arc, *title, *num, *sub, *link; lv_timer_t *t; int rot, done_ms; } ZS;
static void zscan_close(void){
    if(ZS.t){ lv_timer_delete(ZS.t); ZS.t = NULL; }
    if(ZS.ov){ lv_obj_t *o = ZS.ov; ZS.ov = NULL; lv_obj_delete(o); }
}
static void zscan_link_cb(lv_event_t *e){
    (void)e;
    if(scanner_active()){ if(scanner_cancel()) lv_label_set_text(ZS.sub, "cancelling..."); return; }
    zscan_close();
}
static void zscan_tick(lv_timer_t *t){
    if(ui_display_is_off())return;
    (void)t; if(!ZS.ov) return;
    char b[48];
    if(scanner_active()){
        ZS.rot = (ZS.rot + 12) % 360; lv_arc_set_rotation(ZS.arc, ZS.rot);
        int d = 0, n = 0; scanner_progress(&d, &n);
        snprintf(b,sizeof b,"%d",d);if(strcmp(lv_label_get_text(ZS.num),b))lv_label_set_text(ZS.num,b);
        return;
    }
    if(!ZS.done_ms){
        ZS.done_ms = 1;
        lv_arc_set_angles(ZS.arc, 0, 360);
        int songs = mdb_total_song_count(); if(songs < 0) songs = 0;
        lv_label_set_text(ZS.title, scanner_no_sd() ? "NO CARD FOUND" : "LIBRARY UPDATED");
        snprintf(b, sizeof b, "%d", songs); lv_label_set_text(ZS.num, b);
        int sk = scanner_skipped() + scanner_unsupported();
        if(sk) snprintf(b, sizeof b, "songs  \xC2\xB7  %d skipped", sk); else snprintf(b, sizeof b, "songs");
        lv_label_set_text(ZS.sub, b); lv_label_set_text(ZS.link, "done");
        lv_timer_set_period(ZS.t, 3500);
        return;
    }
    zscan_close();
}
void zune_scan_show(void){
    if(ZS.ov) return;
    memset(&ZS, 0, sizeof ZS);
    lv_obj_t *ov = lv_obj_create(lv_layer_top()); ZS.ov = ov; lv_obj_remove_style_all(ov); kit_keep(ov);
    lv_obj_set_size(ov, LV_PCT(100), LV_PCT(100)); lv_obj_set_style_bg_color(ov, TC(CANVAS), 0); lv_obj_set_style_bg_opa(ov, LV_OPA_COVER, 0);
    lv_obj_add_flag(ov, LV_OBJ_FLAG_CLICKABLE); lv_obj_remove_flag(ov, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *a = lv_arc_create(ov); ZS.arc = a; kit_keep(a);
    lv_obj_set_size(a, 300, 300); lv_obj_center(a); lv_obj_remove_flag(a, LV_OBJ_FLAG_CLICKABLE);
    lv_arc_set_bg_angles(a, 0, 360); lv_arc_set_angles(a, 0, 70);
    lv_obj_set_style_arc_width(a, 4, LV_PART_MAIN); lv_obj_set_style_arc_color(a, TC(TEXT_MUTED), LV_PART_MAIN); lv_obj_set_style_arc_opa(a, LV_OPA_30, LV_PART_MAIN);
    lv_obj_set_style_arc_width(a, 4, LV_PART_INDICATOR); lv_obj_set_style_arc_color(a, TC(ACCENT_PRIMARY), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(a, LV_OPA_TRANSP, LV_PART_KNOB); lv_obj_set_style_pad_all(a, 0, LV_PART_KNOB);
    ZS.title = lv_label_create(ov); kit_keep(ZS.title); lv_label_set_text(ZS.title, "SCANNING LIBRARY");
    lv_obj_set_style_text_font(ZS.title, zune_font(&font_zune_regular_16), 0); lv_obj_set_style_text_color(ZS.title, TC(TEXT_MUTED), 0);
    lv_obj_align(ZS.title, LV_ALIGN_CENTER, 0, -58);
    ZS.num = lv_label_create(ov); kit_keep(ZS.num); lv_label_set_text(ZS.num, "0");
    lv_obj_set_style_text_font(ZS.num, zune_font(&font_zune_light_50), 0); lv_obj_set_style_text_color(ZS.num, TC(TEXT_PRIMARY), 0);
    lv_obj_align(ZS.num, LV_ALIGN_CENTER, 0, -10);
    ZS.sub = lv_label_create(ov); kit_keep(ZS.sub); lv_label_set_text(ZS.sub, "files");
    lv_obj_set_style_text_font(ZS.sub, zune_font(&font_zune_regular_16), 0); lv_obj_set_style_text_color(ZS.sub, TC(TEXT_MUTED), 0);
    lv_obj_align(ZS.sub, LV_ALIGN_CENTER, 0, 30);
    ZS.link = lv_label_create(ov); kit_keep(ZS.link); lv_label_set_text(ZS.link, "cancel");
    lv_obj_set_style_text_font(ZS.link, zune_font(&font_zune_regular_16), 0); lv_obj_set_style_text_color(ZS.link, TC(ACCENT_PRIMARY), 0);
    lv_obj_set_ext_click_area(ZS.link, 16); lv_obj_add_flag(ZS.link, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(ZS.link, zscan_link_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_align(ZS.link, LV_ALIGN_CENTER, 0, 80);
    ZS.t = lv_timer_create(zscan_tick, 60, NULL);
}

/* ---- takeovers (Figma 45 / 63 / 74): one full-glass state with a rim, an icon, a Light title and text links.
 * Used for wi-fi connecting / failed, bluetooth pairing / failed and the 2% shutdown countdown. One at a time. ---- */
static struct { lv_obj_t *ov, *rim, *title, *big, *sub; lv_timer_t *t; int rot; void (*cb[2])(void); } ZT;
static void ztake_spin(lv_timer_t *t){ (void)t; if(!ZT.rim||ui_display_is_off()||!lv_obj_is_visible(ZT.rim))return;ZT.rot = (ZT.rot + 15) % 360; lv_arc_set_rotation(ZT.rim, ZT.rot); }
void zune_takeover_close(void){
    if(ZT.t){ lv_timer_delete(ZT.t); ZT.t = NULL; }
    if(ZT.ov){ lv_obj_t *o = ZT.ov; ZT.ov = NULL; lv_obj_delete_async(o); }
    ZT.rim = ZT.title = ZT.big = ZT.sub = NULL;
}
int zune_takeover_open(void){ return ZT.ov != NULL; }
static void ztake_link_cb(lv_event_t *e){
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    void (*f)(void) = ZT.cb[i];
    zune_takeover_close();
    if(f) f();
}
static lv_obj_t *ztake_label(lv_obj_t *p, const char *txt, const lv_font_t *f, lv_color_t c, int y, int w){
    lv_obj_t *l = lv_label_create(p); kit_keep(l); lv_label_set_text(l, txt ? txt : "");
    lv_obj_set_style_text_font(l, f, 0); lv_obj_set_style_text_color(l, c, 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    if(w){ lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP); lv_obj_set_width(l, w); }
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, y);
    return l;
}
void zune_takeover(const ztake_t *s){
    zune_takeover_close();
    memset(&ZT, 0, sizeof ZT);
    ZT.cb[0] = s->cb1; ZT.cb[1] = s->cb2;
    lv_obj_t *ov = lv_obj_create(lv_layer_top()); ZT.ov = ov; lv_obj_remove_style_all(ov); kit_keep(ov);
    lv_obj_set_size(ov, LV_PCT(100), LV_PCT(100)); lv_obj_set_style_bg_color(ov, TC(CANVAS), 0); lv_obj_set_style_bg_opa(ov, LV_OPA_COVER, 0);
    lv_obj_add_flag(ov, LV_OBJ_FLAG_CLICKABLE); lv_obj_remove_flag(ov, LV_OBJ_FLAG_SCROLLABLE);
    /* the rim: a grey track, an accent arc that turns (spin) or holds a level (ring_pct) */
    lv_obj_t *a = lv_arc_create(ov); ZT.rim = a; kit_keep(a);
    lv_obj_set_size(a, 352, 352); lv_obj_center(a); lv_obj_remove_flag(a, LV_OBJ_FLAG_CLICKABLE);
    lv_arc_set_bg_angles(a, 0, 360); lv_arc_set_rotation(a, 270);
    if(s->spin) lv_arc_set_angles(a, 0, 90);
    else if(s->ring_pct >= 0) lv_arc_set_angles(a, 0, (uint16_t)(s->ring_pct * 360 / 100));
    else lv_arc_set_angles(a, 0, 0);
    lv_obj_set_style_arc_width(a, 4, LV_PART_MAIN); lv_obj_set_style_arc_color(a, TC(TEXT_MUTED), LV_PART_MAIN); lv_obj_set_style_arc_opa(a, LV_OPA_30, LV_PART_MAIN);
    lv_obj_set_style_arc_width(a, 4, LV_PART_INDICATOR); lv_obj_set_style_arc_color(a, TC(ACCENT_PRIMARY), LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(a, 0, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(a, LV_OPA_TRANSP, LV_PART_KNOB); lv_obj_set_style_pad_all(a, 0, LV_PART_KNOB);
    if(s->spin) ZT.t = lv_timer_create(ztake_spin, 60, NULL);   /* 16 fps: a full-rim repaint each step is costly here */
    /* the icon: bare (connecting) or in an accent ring (failed / pairing) */
    int ring = s->icon_ring;                 /* 0 = bare, else the ring's diameter */
    int ty = ring ? 66 + ring + 14 : 160;    /* title top */
    if(s->icon){
        if(ring){
            lv_obj_t *c = lv_obj_create(ov); lv_obj_remove_style_all(c); kit_keep(c);
            lv_obj_set_size(c, ring, ring); lv_obj_align(c, LV_ALIGN_TOP_MID, 0, ring >= 80 ? 66 : 84);
            lv_obj_set_style_radius(c, LV_RADIUS_CIRCLE, 0); lv_obj_set_style_border_width(c, 2, 0);
            lv_obj_set_style_border_color(c, TC(ACCENT_PRIMARY), 0); lv_obj_remove_flag(c, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_t *g = lv_label_create(c); kit_keep(g); lv_label_set_text(g, s->icon);
            lv_obj_set_style_text_font(g, ring >= 80 ? TF(UI_28) : TF(UI_20), 0);
            lv_obj_set_style_text_color(g, ring >= 80 ? TC(TEXT_PRIMARY) : TC(ACCENT_PRIMARY), 0); lv_obj_center(g);
            ty = (ring >= 80 ? 66 : 84) + ring + (ring >= 80 ? 14 : 16);
        } else {
            lv_obj_t *g = ztake_label(ov, s->icon, TF(UI_28), s->icon_accent ? TC(ACCENT_PRIMARY) : TC(TEXT_PRIMARY), s->big ? 74 : 104, 0);
            (void)g;
            ty = s->big ? 116 : 160;
        }
    }
    ZT.title = ztake_label(ov, s->title, zune_font(&font_zune_light_32), TC(TEXT_PRIMARY), ty, 300);
    int y = ty + 42;
    if(s->big){ ZT.big = ztake_label(ov, s->big, zune_font(&font_zune_light_50), TC(TEXT_PRIMARY), y, 0); y += 74; }
    if(s->sub){ ZT.sub = ztake_label(ov, s->sub, zune_font(&font_zune_regular_16), TC(TEXT_SECONDARY), y, s->note ? 0 : 230); y += s->note ? 34 : 50; }
    if(s->note){ ztake_label(ov, s->note, zune_font(&font_zune_regular_16), TC(TEXT_MUTED), y, 220); y += 34; }
    int ly = y < 270 ? 272 : y + 8; if(ly > 300) ly = 300;
    const char *ln[2] = { s->l1, s->l2 };
    int two = s->l1 && s->l2;
    for(int i = 0; i < 2; i++){
        if(!ln[i]) continue;
        lv_obj_t *l = lv_label_create(ov); kit_keep(l); lv_label_set_text(l, ln[i]);
        lv_obj_set_style_text_font(l, zune_font(&font_zune_regular_16), 0);
        lv_obj_set_style_text_color(l, (two && i == 0) ? TC(ACCENT_PRIMARY) : TC(TEXT_SECONDARY), 0);
        lv_obj_add_flag(l, LV_OBJ_FLAG_CLICKABLE); lv_obj_set_ext_click_area(l, 16);
        lv_obj_add_event_cb(l, ztake_link_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_align(l, LV_ALIGN_TOP_MID, two ? (i == 0 ? -46 : 46) : 0, ly);
    }
}
void zune_takeover_big(const char *txt){if(ZT.big&&strcmp(lv_label_get_text(ZT.big),txt?txt:""))lv_label_set_text(ZT.big,txt?txt:"");}
void zune_takeover_ring(int pct){ if(ZT.rim && !ZT.t) lv_arc_set_angles(ZT.rim, 0, (uint16_t)(pct * 360 / 100)); }

/* ---- a turning accent arc on a screen's rim while something is being looked for (Figma 74 scanning) ---- */
static lv_obj_t *g_rim_arc; static lv_timer_t *g_rim_t; static int g_rim_rot;
static void zrim_tick(lv_timer_t *t){ (void)t; if(g_rim_arc&&!ui_display_is_off()&&lv_obj_is_visible(g_rim_arc)){g_rim_rot = (g_rim_rot + 15) % 360; lv_arc_set_rotation(g_rim_arc, g_rim_rot); } }
static void zrim_del_cb(lv_event_t *e){ (void)e; g_rim_arc = NULL; if(g_rim_t){ lv_timer_delete(g_rim_t); g_rim_t = NULL; } }
void zune_rim_spin(lv_obj_t *scr, int on){
    if(!on){
        if(g_rim_t){ lv_timer_delete(g_rim_t); g_rim_t = NULL; }
        if(g_rim_arc){ lv_obj_t *a = g_rim_arc; g_rim_arc = NULL; lv_obj_remove_event_cb(a, zrim_del_cb); lv_obj_delete(a); }
        return;
    }
    if(g_rim_arc || !scr) return;
    lv_obj_t *a = lv_arc_create(scr); g_rim_arc = a; kit_keep(a);
    lv_obj_add_flag(a, LV_OBJ_FLAG_IGNORE_LAYOUT | LV_OBJ_FLAG_FLOATING);
    lv_obj_set_size(a, 352, 352); lv_obj_center(a); lv_obj_remove_flag(a, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_arc_set_bg_angles(a, 0, 0); lv_arc_set_angles(a, 0, 90);
    lv_obj_set_style_arc_opa(a, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_width(a, 4, LV_PART_INDICATOR); lv_obj_set_style_arc_color(a, TC(ACCENT_PRIMARY), LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(a, 0, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(a, LV_OPA_TRANSP, LV_PART_KNOB); lv_obj_set_style_pad_all(a, 0, LV_PART_KNOB);
    lv_obj_add_event_cb(a, zrim_del_cb, LV_EVENT_DELETE, NULL);
    g_rim_t = lv_timer_create(zrim_tick, 60, NULL);
}

/* ---- a toast with an action (Figma 82): the toast pill plus an accent "switch" pill; stays a few seconds ---- */
static lv_obj_t *g_zat; static lv_timer_t *g_zat_t; static void (*g_zat_cb)(void);
static void zat_close(void){ if(g_zat_t){ lv_timer_delete(g_zat_t); g_zat_t = NULL; } if(g_zat){ lv_obj_t *o = g_zat; g_zat = NULL; lv_obj_delete_async(o); } }
static void zat_timeout(lv_timer_t *t){ (void)t; g_zat_t = NULL; if(g_zat){ lv_obj_t *o = g_zat; g_zat = NULL; lv_obj_delete_async(o); } }
static void zat_btn_cb(lv_event_t *e){ (void)e; void (*f)(void) = g_zat_cb; zat_close(); if(f) f(); }
void zune_toast_action(const char *icon, const char *title, const char *sub, const char *action, void (*cb)(void), int ms){
    zat_close(); g_zat_cb = cb;
    lv_obj_t *p = lv_obj_create(lv_layer_top()); g_zat = p; lv_obj_remove_style_all(p); kit_keep(p);
    lv_obj_set_style_bg_color(p, TC(SURFACE), 0); lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(p, TC(TEXT_MUTED), 0); lv_obj_set_style_border_width(p, 1, 0); lv_obj_set_style_border_opa(p, LV_OPA_60, 0);
    lv_obj_set_style_radius(p, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_left(p, 10, 0); lv_obj_set_style_pad_right(p, 8, 0); lv_obj_set_style_pad_ver(p, 8, 0); lv_obj_set_style_pad_column(p, 10, 0);
    lv_obj_set_size(p, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_ROW); lv_obj_set_flex_align(p, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align(p, LV_ALIGN_TOP_MID, 0, 36); lv_obj_remove_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *dot = lv_obj_create(p); lv_obj_remove_style_all(dot); kit_keep(dot);
    lv_obj_set_size(dot, 26, 26); lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, TC(ACCENT_PRIMARY), 0); lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *g = lv_label_create(dot); kit_keep(g); lv_label_set_text(g, icon ? icon : LV_SYMBOL_USB);
    lv_obj_set_style_text_font(g, TF(UI_12), 0); lv_obj_set_style_text_color(g, TC(ON_ACCENT), 0); lv_obj_center(g);
    lv_obj_t *col = lv_obj_create(p); lv_obj_remove_style_all(col); kit_keep(col);
    lv_obj_set_size(col, LV_SIZE_CONTENT, LV_SIZE_CONTENT); lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(col, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *t = lv_label_create(col); kit_keep(t); lv_label_set_text(t, title);
    lv_obj_set_style_text_font(t, zune_font(&font_zune_regular_14), 0);
    { lv_point_t sz; lv_text_get_size(&sz, title, zune_font(&font_zune_regular_14), 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
      lv_label_set_long_mode(t, LV_LABEL_LONG_DOT); lv_obj_set_width(t, sz.x + 2 < 130 ? sz.x + 2 : 130); } lv_obj_set_style_text_color(t, TC(TEXT_PRIMARY), 0);
    if(sub){ lv_obj_t *s = lv_label_create(col); kit_keep(s); lv_label_set_text(s, sub);
        lv_obj_set_style_text_font(s, zune_font(&font_zune_regular_16), 0); lv_obj_set_style_text_color(s, TC(TEXT_MUTED), 0); }
    if(action){
        lv_obj_t *b = lv_obj_create(p); lv_obj_remove_style_all(b); kit_keep(b);
        lv_obj_set_size(b, LV_SIZE_CONTENT, 26); lv_obj_set_style_pad_hor(b, 12, 0);
        lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0); lv_obj_set_style_bg_color(b, TC(ACCENT_PRIMARY), 0); lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_remove_flag(b, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);   /* the whole pill takes the tap */
        lv_obj_t *bl = lv_label_create(b); kit_keep(bl); lv_label_set_text(bl, action);
        lv_obj_set_style_text_font(bl, zune_font(&font_zune_regular_14), 0); lv_obj_set_style_text_color(bl, TC(ON_ACCENT), 0); lv_obj_center(bl);
        lv_obj_add_flag(p, LV_OBJ_FLAG_CLICKABLE); lv_obj_set_ext_click_area(p, 8);
        lv_obj_add_event_cb(p, zat_btn_cb, LV_EVENT_CLICKED, NULL);
    } else lv_obj_remove_flag(p, LV_OBJ_FLAG_CLICKABLE);
    g_zat_t = lv_timer_create(zat_timeout, ms > 0 ? ms : 6000, NULL); lv_timer_set_repeat_count(g_zat_t, 1);
}

/* Theme-owned top-layer panels must never survive navigation to another screen. */
void zune_overlays_close(void){zc_close();zune_takeover_close();zscan_close();zat_close();}
int zune_overlay_back(void){
 if(zune_confirm_open()){zc_close();return 1;}
 if(zune_takeover_open()){zune_takeover_close();return 1;}
 if(ZS.ov){zscan_close();return 1;}
 return 0;
}
