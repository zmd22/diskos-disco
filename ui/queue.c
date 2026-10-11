/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
/* The Queue: songs you enqueue play right after the current song (after the last enqueued one, if any), in
 * the order you enqueued them - Shuffle/Repeat paused - then playback returns to what you were playing.
 *
 * How: the player plays a COPY of a list handed to it (the playback slot) and keeps that copy's order
 * (verified on device). Enqueuing only edits the queue - the song that's playing is never touched. When it
 * ends (or you press Next), the player starts its own next song; at that moment the UI hands it a copy:
 *      [the queued songs] + [that next song and the rest of the list you were playing]
 * and plays the first queued song from its start. No mid-song reload, no seeking back. As queued songs
 * start they drop off the queue; when playback reaches your list again the UI switches back to it at that
 * song, in your own play mode. Going back (Previous) is left alone. The queue is kept in
 * /usr/data/diskos_queue.tsv. */
#include "screens.h"
#include "modes.h"
#include "braun.h"
#include "theme.h"
#include "curvelist.h"
#include "musicdb.h"
#include "ipc.h"
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#define QMAX 400
#define RMAX 4000
#ifndef QUEUE_FILE
#define QUEUE_FILE "/usr/data/diskos_queue.tsv"
#endif
typedef struct { char path[256]; char title[96]; char artist[96]; long dur; int id, track, cue, iso, have_key; } qitem_t;
static qitem_t g_q[QMAX];                         /* what's still to come, in play order */
static int g_qn, g_loaded;

/* the session: our copy is what the player is playing */
static int  g_active;

static int *g_rest; static int g_nrest;   /* the rest of the original list, after the anchor */
static int *g_ctx;  static int g_nctx;    /* the original list, whole (for the hand-back position) */
static int  g_ctx_type = -1; static char g_ctx_name[256]; static long g_ctx_pid;
static char g_ctx_label[120];                     /* "then back to ..." */
static int *g_copy; static int g_ncopy;   /* the copy the player is playing (while active) */
static int g_last_id;
static int g_prev2;                        /* the song before the last one: going back to it = Previous */
static uint32_t g_back_at;                       /* the Previous button was just pressed (the next change is a back) */
void queue_note_prev(void){ g_back_at = lv_tick_get() | 1; }
static long g_last_pos, g_last_dur;               /* where the last song was, to tell a natural end */
static int  g_skip_change;                        /* the user just started something: that start isn't ours */

static void view_reload(void);
/* ---------------------------------------------------------------- storage */
static void save(void){
    char tmp[64]; snprintf(tmp, sizeof tmp, "%s.tmp", QUEUE_FILE);
    FILE *f = fopen(tmp, "w"); if(!f) return;
    for(int i = 0; i < g_qn; i++) fprintf(f, "%s\t%s\t%s\t%ld\t%d\t%d\t%d\t%d\t%d\n", g_q[i].path, g_q[i].title, g_q[i].artist, g_q[i].dur, g_q[i].id, g_q[i].track, g_q[i].cue, g_q[i].iso, g_q[i].have_key);
    if(fclose(f) == 0) rename(tmp, QUEUE_FILE); else unlink(tmp);
}
static void load(void){
    if(g_loaded) return;
    g_loaded = 1; g_qn = 0;
    FILE *f = fopen(QUEUE_FILE, "r"); if(!f) return;
    char line[700];
    while(g_qn < QMAX && fgets(line, sizeof line, f)){
        char *nl = strchr(line, '\n'); if(nl) *nl = 0;
        char *p[9] = { line, 0, 0, 0, 0, 0, 0, 0, 0 }; int k = 1;
        for(char *c = line; *c && k < 9; c++) if(*c == '\t'){ *c = 0; p[k++] = c + 1; }
        if(!p[0][0]) continue;
        qitem_t *q = &g_q[g_qn++];
        snprintf(q->path, sizeof q->path, "%s", p[0]); snprintf(q->title, sizeof q->title, "%s", p[1] ? p[1] : "");
        snprintf(q->artist, sizeof q->artist, "%s", p[2] ? p[2] : ""); q->dur = p[3] ? atol(p[3]) : 0;
        q->id = p[4] ? atoi(p[4]) : mdb_queue_current_id(q->path,0,"");
        q->track=p[5]?atoi(p[5]):0; q->cue=p[6]?atoi(p[6]):0; q->iso=p[7]?atoi(p[7]):0; q->have_key=p[8]?atoi(p[8]):0;
        /* Keep ambiguous legacy paths until admission permits exact migration. */
    }
    fclose(f);
}
/* ---------------------------------------------------------------- changes */
static void changed(void){                         /* the queue's content changed */
    save();
    ui_queue_changed();                            /* the Now Playing badge */
    /* nothing else: the song that's playing is left alone; the queue is applied when it ends */
    if(screen_current() == SCR_QUEUE) view_reload();
}
int queue_count(void){ load(); return g_qn; }
static int add_one(int id, const char *path, const char *title, const char *artist, long dur){
    if(g_qn >= QMAX || !path || !path[0]) return 0;
    size_t pl = strlen(path); if(pl > 4 && !strcasecmp(path + pl - 4, ".m4b")) return 0;   /* never an audiobook */
    qitem_t *q = &g_q[g_qn++];
    snprintf(q->path, sizeof q->path, "%s", path); snprintf(q->title, sizeof q->title, "%s", title ? title : "");
    snprintf(q->artist, sizeof q->artist, "%s", artist ? artist : ""); q->dur = dur; q->id = id; q->have_key=0;
    return 1;
}
static int add_path_tracks(const char *path){
    int ids[QMAX],n=mdb_queue_path_ids(path,ids,QMAX),added=0;
    if(n<0 || n>QMAX-g_qn) return -1;  /* never partially enqueue an image */
    for(int i=0;i<n;i++){
        mdb_song_t song; char actual[256];
        if(!mdb_queue_song(ids[i],actual,sizeof actual,&song) || strcmp(path,actual)) return -1;
        const char *sl=strrchr(path,'/');
        if(add_one(ids[i],path,song.title[0]?song.title:(sl?sl+1:path),song.artist,song.dur_ms)){
            qitem_t *q=&g_q[g_qn-1]; q->track=song.track; q->cue=song.is_cue; q->iso=song.is_iso; q->have_key=1; added++;
        }
    }
    return added;
}
int queue_add_path(const char *path){
    load(); if(!path || !path[0]) return 0;
    int n=add_path_tracks(path); if(n>0) changed(); return n;
}
static void folder_cb(void *ud, const char *path, const char *title, const char *artist, const char *album, long dur){
    (void)title; (void)artist; (void)album; (void)dur;
    int n=add_path_tracks(path); if(n>0) *(int*)ud+=n;
}
int queue_add_folder(const char *dir){
    load(); int n = 0;
    mdb_folder_rows(dir, folder_cb, &n);           /* in file order */
    if(n > 0) changed();
    return n;
}
static void group_cb(void *ud, const char *path){ int n = add_path_tracks(path); if(n > 0) *(int *)ud += n; }
int queue_add_group(const char *col, const char *val){        /* fork: Library long-press on an album / artist / genre */
    load(); int n = 0;
    mdb_group_paths(col, val, group_cb, &n);
    if(n > 0) changed();
    return n;
}
void queue_clear(void){ load(); g_qn = 0; changed(); }
int  queue_remove_at(int i){ load(); if(i < 0 || i >= g_qn) return 0; memmove(&g_q[i], &g_q[i+1], (size_t)(g_qn - i - 1) * sizeof g_q[0]); g_qn--; changed(); return 1; }
int  queue_move(int from, int to){
    load(); if(from < 0 || from >= g_qn || to < 0 || to >= g_qn || from == to) return 0;
    qitem_t t = g_q[from];
    if(from < to) memmove(&g_q[from], &g_q[from+1], (size_t)(to - from) * sizeof t);
    else          memmove(&g_q[to+1], &g_q[to], (size_t)(from - to) * sizeof t);
    g_q[to] = t; changed(); return 1;
}
void queue_shuffle(void){
    load(); for(int i = g_qn - 1; i > 0; i--){ int j = rand() % (i + 1); qitem_t t = g_q[i]; g_q[i] = g_q[j]; g_q[j] = t; }
    if(g_qn > 1) changed();
}
void queue_touched(void){ ui_queue_changed(); }
void queue_path_moved(const char *o, const char *n){         /* file rename / move */
    load(); size_t ol = strlen(o); int hit = 0;
    for(int i = 0; i < g_qn; i++)
        if(!strncmp(g_q[i].path, o, ol) && (g_q[i].path[ol] == 0 || g_q[i].path[ol] == '/')){
            char np[256]; snprintf(np, sizeof np, "%s%s", n, g_q[i].path + ol); snprintf(g_q[i].path, sizeof g_q[i].path, "%s", np); hit = 1; }
    if(hit){ save(); if(screen_current() == SCR_QUEUE) view_reload(); }
}
void queue_path_removed(const char *p){                      /* file / folder delete */
    load(); size_t pl = strlen(p); int k = 0;
    for(int i = 0; i < g_qn; i++)
        if(!(!strncmp(g_q[i].path, p, pl) && (g_q[i].path[pl] == 0 || g_q[i].path[pl] == '/'))) g_q[k++] = g_q[i];
    if(k != g_qn){ g_qn = k; changed(); }
}
/* the user started something else: the queue stays, and re-anchors after the new song */
void queue_note_external_play(void){
    g_active = 0;                                             /* it plays after the song you just started */
    g_skip_change = 1;
}
long queue_pid(int create){ (void)create; return 0; }        /* the queue is no longer a playlist */
/* ---------------------------------------------------------------- the player's copy */
static int idx_of(const int *rows, int n, int id){ for(int i=0;i<n;i++) if(rows[i]==id) return i; return -1; }
/* capture what the player is playing (its live list), with the rest starting at `from` (inclusive) */
static int capture_context(int from, int inclusive){
    if(!g_ctx) g_ctx = malloc(sizeof(*g_ctx) * RMAX);
    if(!g_rest) g_rest = malloc(sizeof(*g_rest) * RMAX);
    if(!g_copy) g_copy = malloc(sizeof(*g_copy) * (QMAX + RMAX));
    g_nctx = g_ctx ? mdb_queue_live_ids(g_ctx, RMAX) : 0;
    if(g_nctx<0 || !g_ctx || !g_rest || !g_copy) return 0;
    ui_play_context(&g_ctx_type, g_ctx_name, sizeof g_ctx_name, &g_ctx_pid);
    int at = idx_of(g_ctx, g_nctx, from);
    if(g_ctx_type>=0 && at<0) return 0;
    g_nrest = 0;
    if(at >= 0 && g_rest) for(int i = inclusive ? at : at + 1; i < g_nctx; i++) g_rest[g_nrest++] = g_ctx[i];
    if(g_ctx_type < 0) g_nrest = 0;                           /* a single song / book: nothing to continue into */
    g_ctx_label[0] = 0;
    if(g_nrest > 0){ mdb_song_t s; char path[256]; if(mdb_queue_song(g_rest[0],path,sizeof path,&s)) snprintf(g_ctx_label, sizeof g_ctx_label, "%.110s", s.title); }
    return 1;
}
/* hand the player [queued songs from `first`] + [the rest], and start the first one from its beginning */
static int play_copy(int first){
    if(!g_copy) return 0;
    int candidate[QMAX+RMAX],n=0;
    for(int i=first;i<g_qn;i++){
        mdb_song_t song; char path[256];
        if(g_q[i].have_key) g_q[i].id=mdb_queue_resolve(g_q[i].path,g_q[i].track,g_q[i].cue,g_q[i].iso);
        else g_q[i].id=mdb_queue_current_id(g_q[i].path,0,"");
        if(!g_q[i].id || !mdb_queue_song(g_q[i].id,path,sizeof path,&song) || strcmp(path,g_q[i].path)){
            ui_toast("Queue song unavailable - choose it again"); return 0;
        }
        if(!g_q[i].have_key){ g_q[i].track=song.track; g_q[i].cue=song.is_cue; g_q[i].iso=song.is_iso; g_q[i].have_key=1; }
        if(idx_of(candidate,n,g_q[i].id)<0) candidate[n++]=g_q[i].id;
    }
    for(int i=0;i<g_nrest;i++) if(idx_of(candidate,n,g_rest[i])<0) candidate[n++]=g_rest[i];
    int pos=mdb_queue_slot_set(candidate,n,g_q[first].id);
    if(!pos || !ui_play_slot(pos)) return 0;
    memcpy(g_copy,candidate,(size_t)n*sizeof *g_copy);
    g_ncopy=n; g_active=1;
    return 1;
}
static void hand_back(int song){                   /* the queue is done: back to your list, at `song` */
    int at = idx_of(g_ctx, g_nctx, song);
    int sent;
    if(g_ctx_type >= 0 && at >= 0) sent=ui_play_restore(g_ctx_type, g_ctx_name, g_ctx_pid, at + 1);
    else sent=ui_play_restore(-1, "", 0, 0);
    if(sent) g_active=0;                       /* nothing to return to: just the play mode back */
}
static void pop_through(int k){ memmove(&g_q[0], &g_q[k+1], (size_t)(g_qn - k - 1) * sizeof g_q[0]); g_qn -= k + 1; save(); ui_queue_changed();
                                if(screen_current() == SCR_QUEUE) view_reload(); }
/* the Next button with songs queued: straight to the first one (the player never picks its own next) */
int queue_next(void){
    load();
    if(g_qn == 0) return 0;
    if(!ui_local_playback_allowed() || modes_output_busy()) return -1;
    track_state_t st; ipc_get_state(&st);
    if(st.have_track && mdb_is_book_path(st.path)) return 0;
    int current=mdb_queue_current_id(st.path,st.pos_id,st.title);
    if(!current) return -1;
    if(!g_active && !capture_context(current, 0)) return -1;                 /* you skipped this song: your list resumes after it */
    if(!play_copy(0)) return -1;
    g_prev2=current; g_last_id=g_q[0].id;
    pop_through(0);
    return 1;
}
void queue_jump(int i){                                       /* play queued song i now (the ones before it are skipped) */
    if(!ui_local_playback_allowed() || modes_output_busy()) return;
    load(); if(i < 0 || i >= g_qn) return;
    track_state_t st; ipc_get_state(&st);
    int current=mdb_queue_current_id(st.path,st.pos_id,st.title);
    if(!current) return;
    if(!g_active && !capture_context(current, 0)) return;                 /* the current song is left: your list resumes after it */
    if(play_copy(i)){ g_prev2=current; g_last_id=g_q[i].id; pop_through(i); }
}
void queue_tick(const track_state_t *st, int playing){
    (void)playing;
    if(!st || !st->have_track || !ui_local_playback_allowed() || modes_output_busy()) return;
    load();
    int current=mdb_queue_current_id(st->path,st->pos_id,st->title);
    if(!current) return;  /* unverified subtrack metadata never advances the editable queue */
    if(current != g_last_id){                        /* a new song began */
        int prev=g_last_id;
        int before=g_prev2;
        long prev_pos = g_last_pos, prev_dur = g_last_dur;
        g_prev2=prev;
        g_last_id=current;
        g_last_pos = st->position_ms; g_last_dur = st->duration_ms;
        if(!prev) return;                                  /* the first song we see: nothing to do */
        if(g_skip_change){ g_skip_change = 0; return; }       /* the user just started this one: the queue waits for it to end */
        if(mdb_is_book_path(st->path)) return;                /* a book plays alone */
        int natural = prev_dur > 0 && prev_pos >= prev_dur - 4000;
        /* forward or back? Works in any play mode (list order means nothing in Shuffle): going back = returning
         * to the song heard before the last one */
        int back = 0;
        if(g_back_at && lv_tick_elaps(g_back_at) < 4000) back = 1;          /* the on-screen Previous */
        g_back_at = 0;
        if(!natural && before && current==before) back = 1;     /* returned to the song heard before */
        if(!natural && !back && cfg_get_int("work_mode", 0) != 1){           /* not Shuffle: the list order says it */
            if(g_active) back = idx_of(g_copy, g_ncopy, current) >= 0 && idx_of(g_copy, g_ncopy, current) < idx_of(g_copy, g_ncopy, prev);
            else { int *rows = malloc(sizeof(*rows) * RMAX); int nr = rows ? mdb_queue_live_ids(rows, RMAX) : 0;
                   int a = idx_of(rows, nr, current), b = idx_of(rows, nr, prev); back = a >= 0 && b >= 0 && a < b; free(rows); }
        }
        int forward = !back;
        if(!g_active){
            if(g_qn == 0) return;
            if(!forward) return;                              /* Previous: left alone */
            if(!capture_context(current, 1)) return;                     /* the song the player moved to plays after the queue */
            play_copy(0);                                     /* the first queued song starts now */
            return;
        }
        /* our copy is playing */
        if(g_qn > 0 && current==g_q[0].id){ pop_through(0); return; }   /* the next queued song, as planned */
        if(!forward) return;                                  /* Previous: left alone */
        if(g_qn > 0){ play_copy(0); return; }                 /* the copy is out of date (reordered / added / a repeat): the head now */
        hand_back(idx_of(g_rest, g_nrest, current) >= 0 ? current : (g_nrest ? g_rest[0] : current));   /* queue done */
        return;
    }
    g_last_pos = st->position_ms; g_last_dur = st->duration_ms;
}
/* ================================================================ the Queue screen */
#define QW 288
static lv_obj_t *g_root, *g_list, *g_sub, *g_clear_btn, *g_play_btn, *g_shuffle_btn;
static curvelist_t g_cl;
static uint32_t g_clear_armed;
static char g_summary[64];
static void queue_hint_exec(void *obj,int32_t opa){lv_obj_set_style_text_opa(obj,(lv_opa_t)opa,0);}
static void queue_hint_done(lv_anim_t *a){
    lv_label_set_text(a->var,g_summary);lv_obj_set_style_text_opa(a->var,LV_OPA_COVER,0);
}
static int g_drag = -1, g_drag_row0;
static void open_now(void){ screen_show(SCR_QUEUE); }
void queue_open(void){ open_now(); }
static void back_cb(lv_event_t *e){ (void)e; screen_back(); }
static void play_cb(lv_event_t *e){ (void)e; if(g_qn > 0) queue_jump(0); }
static void shuffle_cb(lv_event_t *e){ (void)e; if(g_qn > 1){ queue_shuffle(); ui_toast("Queue shuffled"); } }
static void clear_cb(lv_event_t *e){
    (void)e; lv_obj_t *l = lv_obj_get_child(g_clear_btn, 1);
    if(!g_qn) return;
    if(!g_clear_armed || lv_tick_elaps(g_clear_armed) > 3000){ g_clear_armed = lv_tick_get(); if(l) lv_label_set_text(l, "Sure?"); return; }
    g_clear_armed = 0; if(l) lv_label_set_text(l, "Clear");
    queue_clear(); ui_toast("Queue cleared");
}
static void row_cb(lv_event_t *e){ int i = (int)(intptr_t)lv_event_get_user_data(e); queue_jump(i); }
/* reorder: press the handle and drag; rows move live (children reordered, nothing recreated) */
static int row_child0;                                          /* child index of the first queued row */
static void handle_cb(lv_event_t *e){
    lv_event_code_t c = lv_event_get_code(e);
    lv_obj_t *row = lv_obj_get_parent(lv_event_get_target(e));
    if(c == LV_EVENT_PRESSED){
        g_drag = (int)lv_obj_get_index(row) - row_child0; g_drag_row0 = g_drag;
        lv_obj_remove_flag(g_list, LV_OBJ_FLAG_SCROLLABLE);       /* the drag moves the row, not the list */
        lv_obj_set_style_bg_color(row, TC(QUEUE_DRAG), 0);
    } else if(c == LV_EVENT_PRESSING && g_drag >= 0){
        lv_indev_t *in = lv_indev_active(); if(!in) return;
        lv_point_t p; lv_indev_get_point(in, &p);
        int best = g_drag;
        for(int i = 0; i < g_qn; i++){
            lv_obj_t *r = lv_obj_get_child(g_list, row_child0 + i); if(!r) break;
            lv_area_t a; lv_obj_get_coords(r, &a);
            if(p.y >= a.y1 && p.y <= a.y2){ best = i; break; }
        }
        if(best != g_drag){ lv_obj_move_to_index(row, row_child0 + best); g_drag = best; }
    } else if((c == LV_EVENT_RELEASED || c == LV_EVENT_PRESS_LOST) && g_drag >= 0){
        int from = g_drag_row0, to = g_drag; g_drag = -1;
        lv_obj_add_flag(g_list, LV_OBJ_FLAG_SCROLLABLE);
        if(from != to) queue_move(from, to); else view_reload();
    }
}
static lv_obj_t *section(const char *t){
    lv_obj_t *l = lv_label_create(g_list); lv_label_set_text(l, t);
    lv_obj_set_width(l, QW - 24);
    lv_obj_set_style_text_font(l, TF(UI_16), 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(l, TC(TEXT_SECONDARY), 0);
    return l;
}
static void row_size_cb(lv_event_t *e){
    lv_obj_t *r = lv_event_get_target(e);
    int now = (int)(intptr_t)lv_event_get_user_data(e), x = now ? 48 : 22;
    int w = lv_obj_get_width(r) - x - (now ? 22 : 64);
    if(w < 32) w = 32;
    lv_obj_set_width(lv_obj_get_child(r, now ? 1 : 0), w);
    lv_obj_set_width(lv_obj_get_child(r, now ? 2 : 1), w);
}
static void separator(void){
    lv_obj_t *r = lv_obj_create(g_list); lv_obj_remove_style_all(r);
    lv_obj_set_size(r, QW - 32, 1);
    lv_obj_set_style_bg_color(r, TC(BORDER), 0); lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
}
static lv_obj_t *qrow(const char *t, const char *a, int now, int idx){
    lv_obj_t *r = lv_button_create(g_list);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, QW, 70);
    lv_obj_add_flag(r, LV_OBJ_FLAG_USER_1);
    lv_obj_set_style_radius(r, th_disco() ? LV_RADIUS_CIRCLE : TH_R_ROW, 0);
    lv_obj_set_style_bg_color(r, now ? TC(SURFACE_RAISED) : TC(SURFACE), 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(r, TC(SURFACE_RAISED), LV_STATE_PRESSED);
    if(now){ lv_obj_set_style_border_width(r, 1, 0); lv_obj_set_style_border_color(r, ui_current_accent(), 0); }
    lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    int x = 22;
    if(now){ lv_obj_t *ic = lv_label_create(r); lv_label_set_text(ic, LV_SYMBOL_VOLUME_MAX);
             lv_obj_set_pos(ic, 20, 25); lv_obj_set_style_text_color(ic, ui_current_accent(), 0); x = 48; }
    else lv_obj_add_event_cb(r, row_cb, LV_EVENT_SHORT_CLICKED, (void *)(intptr_t)idx);   /* tap: jump to it */
    lv_obj_t *tl = lv_label_create(r); lv_label_set_text(tl, t); lv_label_set_long_mode(tl,LV_LABEL_LONG_DOT);if(now)ui_reveal_title(tl);
    lv_obj_set_pos(tl, x, 10); lv_obj_set_size(tl, QW - x - (now ? 22 : 64), 28);
    lv_obj_set_style_text_font(tl, ui_font_user(20), 0); lv_obj_set_style_text_color(tl, now ? ui_current_accent() : TC(TEXT_PRIMARY), 0);
    lv_obj_t *al = lv_label_create(r); lv_label_set_text(al, a); lv_label_set_long_mode(al,LV_LABEL_LONG_DOT);if(now)ui_reveal_title(al);
    lv_obj_set_pos(al, x, 39); lv_obj_set_size(al, QW - x - (now ? 22 : 64), 22);
    lv_obj_set_style_text_font(al, ui_font_user(16), 0); lv_obj_set_style_text_color(al, TC(TEXT_SECONDARY), 0);
    if(!now){                                                 /* the drag handle */
        lv_obj_t *h = lv_obj_create(r); lv_obj_remove_style_all(h);
        lv_obj_set_size(h, 56, 70); lv_obj_align(h, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_add_flag(h, LV_OBJ_FLAG_CLICKABLE); lv_obj_clear_flag(h, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(h, handle_cb, LV_EVENT_ALL, NULL);
        lv_obj_t *g = lv_label_create(h); lv_label_set_text(g, LV_SYMBOL_BARS);
        lv_obj_set_style_text_color(g, TC(QUEUE_HANDLE), 0); lv_obj_center(g);
    }
    lv_obj_add_event_cb(r, row_size_cb, LV_EVENT_SIZE_CHANGED, (void *)(intptr_t)now);
    return r;
}
static void queue_position_cb(lv_event_t *e){
    if(g_qn<12 || !g_sub)return;
    lv_anim_delete(g_sub,queue_hint_exec);lv_obj_set_style_text_opa(g_sub,LV_OPA_COVER,0);
    if(lv_event_get_code(e)==LV_EVENT_SCROLL_END){
        lv_anim_t a;lv_anim_init(&a);lv_anim_set_var(&a,g_sub);lv_anim_set_exec_cb(&a,queue_hint_exec);
        lv_anim_set_values(&a,LV_OPA_COVER,0);lv_anim_set_delay(&a,650);lv_anim_set_duration(&a,120);
        lv_anim_set_completed_cb(&a,queue_hint_done);lv_anim_start(&a);return;
    }
    if(row_child0 >= (int)lv_obj_get_child_count(g_list))return;
    lv_obj_t *first=lv_obj_get_child(g_list,row_child0);
    int idx=(lv_obj_get_scroll_y(g_list)-lv_obj_get_y(first))/78;
    if(idx<0)idx=0;if(idx>=g_qn)idx=g_qn-1;
    char b[48];snprintf(b,sizeof b,"%d / %d up next",idx+1,g_qn);lv_label_set_text(g_sub,b);
}
static void view_reload(void){
    if(!g_list) return;
    load();
    lv_obj_clean(g_list);
    long total = 0; for(int i = 0; i < g_qn; i++) total += g_q[i].dur;
    char b[64];
    if(g_qn) snprintf(b, sizeof b, "%d up next \xC2\xB7 %ld min", g_qn, (total / 60000) ? total / 60000 : 1);
    else snprintf(b, sizeof b, "Nothing queued");
    snprintf(g_summary,sizeof g_summary,"%s",b);
    lv_anim_delete(g_sub,queue_hint_exec);lv_obj_set_style_text_opa(g_sub,LV_OPA_COVER,0);
    lv_label_set_text(g_sub, b);
    lv_obj_t *controls[] = {g_play_btn, g_shuffle_btn, g_clear_btn};
    for(int i=0;i<3;i++) if(controls[i]){
        if(g_qn > (i==1?1:0)) lv_obj_remove_state(controls[i], LV_STATE_DISABLED);
        else lv_obj_add_state(controls[i], LV_STATE_DISABLED);
    }
    track_state_t st; ipc_get_state(&st);
    if(st.have_track){
        mdb_song_t s; int have = mdb_song_by_path(st.path, &s);
        qrow(st.title[0] ? st.title : (have ? s.title : "-"), st.artist[0] ? st.artist : (have ? s.artist : ""), 1, -1);
    }
    if(st.have_track && g_qn) separator();
    row_child0 = (int)lv_obj_get_child_count(g_list);
    for(int i = 0; i < g_qn; i++) qrow(g_q[i].title[0] ? g_q[i].title : "-", g_q[i].artist, 0, i);
    if(!g_qn && !st.have_track) section("Nothing queued");
    lv_obj_update_layout(g_list); curvelist_update(&g_cl);
}
void queue_refresh(void){ g_clear_armed = 0; if(g_clear_btn){ lv_obj_t *l = lv_obj_get_child(g_clear_btn, 1); if(l) lv_label_set_text(l, "Clear"); } view_reload(); }
static lv_obj_t *pill(const char *icon, const char *t, int x, int w, lv_event_cb_t cb, int accent){
    lv_obj_t *b = lv_button_create(g_root);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, w, 44); lv_obj_set_pos(b, x, 68);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_color_t bg = accent ? (th_braun() ? TC(ACCENT_PRIMARY) : ui_current_accent()) : TC(SURFACE);
    if(accent) lv_obj_add_flag(b, LV_OBJ_FLAG_USER_2); /* keep contrast ink on the filled Play button */
    lv_obj_set_style_bg_color(b, bg, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, accent ? bg : TC(SURFACE_RAISED), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(b, 2, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(b, TC(TEXT_PRIMARY), LV_STATE_PRESSED);
    lv_obj_set_style_border_opa(b, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_opa(b, LV_OPA_40, LV_STATE_DISABLED);
    lv_obj_set_ext_click_area(b, 4);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *i = lv_label_create(b); lv_label_set_text(i, icon); lv_obj_set_style_text_font(i, TF(UI_18), 0);
    lv_obj_set_style_text_color(i, accent ? theme_on_color(bg) : TC(TEXT_PRIMARY), 0);
    if(t){ lv_obj_align(i, LV_ALIGN_LEFT_MID, 12, 0);
           lv_obj_t *l = lv_label_create(b); lv_label_set_text(l, t); lv_obj_set_style_text_font(l, TF(UI_18), 0);
           lv_obj_set_style_text_color(l, accent ? theme_on_color(bg) : TC(TEXT_PRIMARY), 0); lv_obj_align(l, LV_ALIGN_LEFT_MID, 36, 0); }
    else lv_obj_center(i);
    return b;
}
void queue_create(lv_obj_t *root){
    g_root = root;
    lv_obj_set_style_bg_color(root, TC(CANVAS), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *hb = lv_button_create(root);                      /* "< Queue": the back arrow and title */
    lv_obj_remove_style_all(hb);
    lv_obj_set_size(hb, 120, 30); lv_obj_align(hb, LV_ALIGN_TOP_MID, 0, 14);
    lv_obj_set_ext_click_area(hb, 6);
    lv_obj_add_event_cb(hb, back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *ar = lv_label_create(hb); lv_label_set_text(ar, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(ar, TC(TEXT_SECONDARY), 0); lv_obj_align(ar, LV_ALIGN_LEFT_MID, 12, 0);
    lv_obj_t *tt = lv_label_create(hb); lv_label_set_text(tt, "Queue");
    if(th_braun()){ lv_obj_set_style_text_color(ar, TC(TEXT_SECONDARY), 0); }
    lv_obj_set_style_text_font(tt, th_braun() ? br_font(18, 1) : TH_F_TITLE, 0); lv_obj_set_style_text_color(tt, th_braun() ? TC(TEXT_PRIMARY) : TC(TEXT_PRIMARY), 0); lv_obj_align(tt, LV_ALIGN_LEFT_MID, 34, 0);
    g_sub = lv_label_create(root); lv_obj_set_style_text_font(g_sub, th_braun() ? br_font(14, 0) : ui_font_cjk(16), 0);   /* has the middle dot */ lv_obj_set_style_text_color(g_sub, TC(TEXT_SECONDARY), 0);
    lv_obj_align(g_sub, LV_ALIGN_TOP_MID, 0, 44);
    g_play_btn = pill(LV_SYMBOL_PLAY, "Play", 44, 104, play_cb, 1);
    g_shuffle_btn = pill(LV_SYMBOL_SHUFFLE, NULL, 156, 48, shuffle_cb, 0);        /* Random: icon only */
    g_clear_btn = pill(LV_SYMBOL_TRASH, "Clear", 212, 104, clear_cb, 0);
    g_list = lv_obj_create(root);
    lv_obj_remove_style_all(g_list);
    lv_obj_set_pos(g_list, (360 - QW) / 2, 122); lv_obj_set_size(g_list, QW, 238);
    lv_obj_set_style_pad_bottom(g_list, 40, 0);
    lv_obj_set_style_pad_row(g_list, 8, 0);
    lv_obj_set_flex_flow(g_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(g_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(g_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(g_list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(g_list, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    curvelist_attach(&g_cl, g_list, root, QW);
    lv_obj_add_event_cb(g_list,queue_position_cb,LV_EVENT_SCROLL,NULL);
    lv_obj_add_event_cb(g_list,queue_position_cb,LV_EVENT_SCROLL_END,NULL);
}
