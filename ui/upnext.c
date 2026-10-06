/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "screens.h"
#include "theme.h"
#include "theme_kit.h"
#include "config.h"
#include "musicdb.h"
#include "ipc.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---- Up Next (SCR_UPNEXT): reached from the Now Playing right-hub. The stock player owns the queue (LIST_SONG_0,
 * shuffle order in LIST_SONG_3); this screen only reads it (mdb_upnext) and jumps within it (ui_queue_jump). It
 * shows the current song and what plays after it, follows track changes while open, and never shows a guessed
 * order: while the player is rebuilding the queue it says so and retries. */
#define UN_SHOW   100                  /* upcoming rows shown; the rest is summarised in a footer */
#define UN_FIRST  12                   /* rows built at once (a screenful), the rest a few per tick */
#define UN_RETRY  8                    /* 1 s retries while the queue is being rebuilt */
static lv_obj_t   *g_un_list;
static lv_timer_t *g_un_fill, *g_un_watch;
static mdb_qrow_t  g_un_rows[UN_SHOW + 1];
static int         g_un_n, g_un_fill_i, g_un_more, g_un_retries, g_un_retry;   /* g_un_retry: last read was transient */
static int         g_un_mode = -1;     /* the play mode the list was read in */
static char        g_un_path[256];     /* the song that was current when the list was read */
static int         g_un_pos;           /* its pos_id then (0 = unknown) */
static unsigned    g_un_seq;           /* its path_seq then: a track change since means the list is stale */

static void un_rebuild(void);

static void un_fmt_dur(int ms, char *out, size_t n){
    if(ms <= 0){ out[0] = 0; return; }
    long long t = ((long long)ms + 500) / 1000; snprintf(out, n, "%lld:%02lld", t / 60, t % 60);   /* no int overflow near INT32_MAX */
}

static void un_note(const char *msg){
    lv_obj_t *l = lv_label_create(g_un_list);
    lv_label_set_text(l, msg);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, 260);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(l, TC(TEXT_MUTED), 0);
    lv_obj_set_style_text_font(l, TF(UI_14), 0);
}

/* The list must still describe what is playing: same track (identity epoch), same queue row (an unknown pos_id on
 * either side counts only if it is unknown on both - a zero-to-known change means the list was read blind), same
 * play mode. */
static int un_still_current(void){
    track_state_t st; ipc_get_state(&st);
    if(!st.have_track || st.path_seq != g_un_seq || strcmp(st.path, g_un_path) != 0) return 0;
    return st.pos_id == g_un_pos && ui_play_mode_now() == g_un_mode;
}

static void un_row_cb(lv_event_t *e){
    if(lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if(i < 0 || i >= g_un_n) return;
    if(i == 0){ screen_show(SCR_NOWPLAYING); return; }             /* the current song */
    if(!un_still_current()){ un_rebuild(); ui_toast("Queue changed"); return; }   /* never jump from a stale list */
    /* and the target row itself must still be that song at that position (a rebuild can keep the current song) */
    int v = mdb_queue_row_valid(g_un_rows[i].base_id, g_un_rows[i].ord, g_un_rows[i].path);
    if(v < 0){ ui_toast("Queue is busy - try again"); return; }
    if(v == 0){ un_rebuild(); ui_toast("Queue changed"); return; }
    if(ui_queue_jump(g_un_rows[i].ord) == 0) screen_show(SCR_NOWPLAYING);
}

static void un_add_row(int i){
    const mdb_qrow_t *q = &g_un_rows[i];
    int cur = (i == 0);
    lv_obj_t *r = lv_button_create(g_un_list);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, 288, 62);
    lv_obj_set_style_radius(r, th_disco() ? LV_RADIUS_CIRCLE : TH_R_ROW, 0);
    lv_obj_set_style_bg_color(r, (cur ? TC(SURFACE_RAISED) : TC(SURFACE)), 0);
    lv_obj_set_style_bg_opa(r, cur ? LV_OPA_COVER : LV_OPA_50, 0);
    lv_obj_set_style_bg_color(r, TC(RAISED_PRESSED), LV_STATE_PRESSED);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    ui_on(r, un_row_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i, "upnext.un_row", UI_CORE);

    lv_obj_t *t = lv_label_create(r);                           /* title (may be non-Latin) */
    lv_label_set_text(t, q->title[0] ? q->title : "Untitled");
    lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(t, 20, 8); lv_obj_set_size(t, 196, 26);
    lv_obj_set_style_text_font(t, ui_font_user(20), 0);
    lv_obj_set_style_text_color(t, TC(TEXT_PRIMARY), 0);

    lv_obj_t *s = lv_label_create(r);                           /* artist; "Now Playing" on the current row */
    lv_label_set_text(s, cur ? "Now Playing" : q->artist);
    lv_label_set_long_mode(s, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(s, 20, 35); lv_obj_set_size(s, 196, 21);
    lv_obj_set_style_text_font(s, cur ? TF(UI_16) : ui_font_user(16), 0);
    lv_obj_set_style_text_color(s, (cur ? TC(STATUS_INFO) : TC(TEXT_MUTED)), 0);

    char dur[12]; un_fmt_dur(q->dur_ms, dur, sizeof dur);
    if(dur[0]){
        lv_obj_t *d = lv_label_create(r);                       /* length, right-aligned */
        lv_label_set_text(d, dur);
        lv_obj_set_pos(d, 214, 20); lv_obj_set_size(d, 56, 20);
        lv_obj_set_style_text_align(d, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_style_text_font(d, TF(UI_14), 0);
        lv_obj_set_style_text_color(d, TC(TEXT_MUTED), 0);
    }
}

static void un_footer(void){
    if(g_un_mode == 2) return;                                  /* Repeat One: the caption already says it */
    if(g_un_n == 1 && !g_un_more){
        un_note(g_un_mode == 3 ? "End of the queue - then it starts again" : "Nothing after this song");
        return;
    }
    if(g_un_more > 0){
        char b[48]; snprintf(b, sizeof b, "+ %d more song%s", g_un_more, g_un_more == 1 ? "" : "s");
        un_note(b);
    }
}

static void un_fill_stop(void){ if(g_un_fill){ lv_timer_delete(g_un_fill); g_un_fill = NULL; } }
static void un_fill_cb(lv_timer_t *t){
    (void)t;
    int end = g_un_fill_i + 20; if(end > g_un_n) end = g_un_n;
    for(; g_un_fill_i < end; g_un_fill_i++) un_add_row(g_un_fill_i);
    if(g_un_fill_i >= g_un_n){ un_fill_stop(); un_footer(); }
}

/* A caption for the play modes where "what plays next" is not simply the next row. */
static void un_mode_caption(int mode){
    const char *c = mode == 1 ? "Shuffle is on - this is the shuffled order"
                  : mode == 2 ? "Repeat One - this song repeats"
                  : mode == 3 ? "Repeat All - the queue starts again after the last song"
                  : mode == 4 ? "Single - playback stops after this song"
                  : NULL;
    if(c) un_note(c);
}

static void un_rebuild(void){
    if(!g_un_list) return;
    un_fill_stop();
    lv_obj_clean(g_un_list);
    lv_obj_scroll_to_y(g_un_list, 0, LV_ANIM_OFF);
    g_un_n = 0; g_un_more = 0; g_un_path[0] = 0; g_un_pos = 0; g_un_retry = 0; g_un_mode = -1;
    track_state_t st; ipc_get_state(&st);
    g_un_seq = st.path_seq;
    g_un_pos = st.pos_id;                                        /* what the list was read against, even when empty, */
    int mode = ui_play_mode_now();                               /* so the watcher only rebuilds on a real change   */
    g_un_mode = mode;
    if(!st.have_track || !st.path[0]){ un_note("Nothing playing"); return; }
    if(mdb_is_book_path(st.path)){ un_note("Audiobooks play one book at a time - use Chapters"); return; }
    snprintf(g_un_path, sizeof g_un_path, "%s", st.path);
    int n = mode < 0 ? MDB_UPNEXT_NOTFOUND : mdb_upnext(mode == 1, st.pos_id, st.path, g_un_rows, UN_SHOW + 1, &g_un_more);
    if(n == MDB_UPNEXT_NOTFOUND || n == MDB_UPNEXT_BUSY){           /* transient: the watch timer retries */
        g_un_retry = 1;
        un_note(g_un_retries < UN_RETRY ? "Queue updating..." : "The queue isn't available right now");
        return;
    }
    if(n == MDB_UPNEXT_ERROR){ un_note("Couldn't read the queue"); return; }
    if(n == MDB_UPNEXT_EMPTY){ un_note("The queue is empty"); return; }
    g_un_retries = 0;
    g_un_n = n;
    un_mode_caption(mode);
    g_un_fill_i = 0;
    int first = n < UN_FIRST ? n : UN_FIRST;
    for(; g_un_fill_i < first; g_un_fill_i++) un_add_row(g_un_fill_i);
    if(g_un_fill_i < g_un_n) g_un_fill = lv_timer_create(un_fill_cb, 16, NULL);
    else un_footer();
}

/* While open: follow track changes, and retry a queue that was being rebuilt. */
static void un_watch_cb(lv_timer_t *t){
    (void)t;
    if(screen_current() != SCR_UPNEXT){ lv_timer_delete(g_un_watch); g_un_watch = NULL; return; }
    if(g_un_retry && g_un_retries < UN_RETRY){ g_un_retries++; un_rebuild(); return; }
    /* a new track, a new queue row for the same file (CUE), a pos_id that became known, or a new play mode */
    track_state_t st; ipc_get_state(&st);
    if(st.path_seq != g_un_seq || st.pos_id != g_un_pos || ui_play_mode_now() != g_un_mode){ g_un_retries = 0; un_rebuild(); }
}

void upnext_create(lv_obj_t *root){
    lv_obj_set_style_bg_color(root, TC(CANVAS), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    ui_header(root, "Up Next");

    g_un_list = lv_obj_create(root);
    lv_obj_remove_style_all(g_un_list);
    lv_obj_set_pos(g_un_list, 30, 72); lv_obj_set_size(g_un_list, 300, 272);
    lv_obj_set_style_pad_bottom(g_un_list, 44, 0);                 /* last row scrolls clear of the round bezel */
    lv_obj_set_style_bg_opa(g_un_list, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(g_un_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(g_un_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(g_un_list, 6, 0);
    lv_obj_set_scroll_dir(g_un_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(g_un_list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(g_un_list, LV_OBJ_FLAG_SCROLL_MOMENTUM);
}

/* Called on every SCR_UPNEXT show (screenmgr), so a deep link or a back-navigation also reads the queue fresh. */
void upnext_refresh(void){
    g_un_retries = 0;
    un_rebuild();
    if(!g_un_watch) g_un_watch = lv_timer_create(un_watch_cb, 1000, NULL);
}

void upnext_open(void){ screen_show(SCR_UPNEXT); }
