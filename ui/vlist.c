/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "vlist.h"
#define AHEAD 9
static lv_obj_t *spacer(lv_obj_t *list){
    lv_obj_t *s = lv_obj_create(list);
    lv_obj_remove_style_all(s);
    lv_obj_set_size(s, 1, 1);
    lv_obj_clear_flag(s, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return s;
}
static void spacer_set(vlist_t *v, lv_obj_t *s, int rows){
    if(rows <= 0){ lv_obj_add_flag(s, LV_OBJ_FLAG_HIDDEN); return; }
    lv_obj_remove_flag(s, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_height(s, rows * v->pitch - v->gap);         /* the flex gap after it completes the pitch */
}
static void drop(vlist_t *v, lv_obj_t *r){ (void)v; lv_obj_delete(r); }
static void window(vlist_t *v, int first, int last){
    if(first < 0) first = 0;
    if(last > v->n) last = v->n;
    if(first >= last){ first = 0; last = v->n < 2 * AHEAD ? v->n : 2 * AHEAD; }
    if(last <= v->first || first >= v->last){               /* a jump: rebuild */
        while(v->last > v->first){ drop(v, lv_obj_get_child(v->list, lv_obj_get_index(v->top) + 1)); v->last--; }
        v->first = v->last = first;
    }
    while(v->first < first){ drop(v, lv_obj_get_child(v->list, lv_obj_get_index(v->top) + 1)); v->first++; }
    while(v->last > last){   drop(v, lv_obj_get_child(v->list, lv_obj_get_index(v->bot) - 1)); v->last--; }
    while(v->first > first){ v->first--; v->add(v->first);
        lv_obj_move_to_index(lv_obj_get_child(v->list, -1), lv_obj_get_index(v->top) + 1); }
    while(v->last < last){ v->add(v->last); v->last++; }
    lv_obj_move_to_index(v->bot, -1);
    spacer_set(v, v->top, v->first);
    spacer_set(v, v->bot, v->n - v->last);
}
static int visible(vlist_t *v){ int i = (lv_obj_get_scroll_y(v->list) - v->lead) / v->pitch; return i < 0 ? 0 : i; }
void vlist_begin(vlist_t *v, lv_obj_t *list, int n, int row_h, int gap, int lead, int view_h, vlist_add_cb add){
    v->list = list; v->n = n; v->pitch = row_h + gap; v->gap = gap; v->lead = lead; v->view_h = view_h; v->add = add;
    v->top = spacer(list); v->bot = spacer(list); v->first = v->last = 0; v->active = 1;
    window(v, 0, view_h / v->pitch + 1 + AHEAD);
    lv_obj_update_layout(list);
}
void vlist_end(vlist_t *v){ v->active = 0; v->top = v->bot = NULL; v->n = v->first = v->last = 0; }
void vlist_follow(vlist_t *v){
    if(!v->active) return;
    int i = visible(v), rows = v->view_h / v->pitch + 1;
    if(i - v->first >= 3 && v->last - (i + rows) >= 3) return;   /* comfortably inside */
    window(v, i - AHEAD, i + rows + AHEAD);
    lv_obj_update_layout(v->list);
}
void vlist_show(vlist_t *v, int i){
    if(!v->active) return;
    window(v, i - AHEAD, i + v->view_h / v->pitch + 1 + AHEAD);
    lv_obj_update_layout(v->list);
    lv_obj_scroll_to_y(v->list, v->lead + i * v->pitch, LV_ANIM_OFF);
}
