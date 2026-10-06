/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
/* Books screen: list audiobooks (.m4b) with saved progress; tap a book to play and resume where
 * you left off. Audiobooks are kept out of the music Songs/Albums/Artists views (mdb_load excludes
 * .m4b); this is the one place they show. */
#include "books.h"
#include "theme.h"
#include "theme_kit.h"
#include "screens.h"
#include "musicdb.h"
#include "artcache.h"
#include "theme.h"
#include "curvelist.h"
#include <math.h>
#include "scanner.h"   /* scan_read_chapters for the chapter-navigation screen */
#include "ipc.h"       /* ipc_get_state: current book + position for the chapter list */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <dirent.h>
#include <time.h>
#include <stdlib.h>

static lv_obj_t *g_list;
static curvelist_t g_bcl;                    /* rows curve with the circle, position dots on the rim */
#define BOOKS_MAX 256
static book_t g_books[BOOKS_MAX];
static int    g_nbooks;

/* Per-open generation for the row-cover temp files. Each books_open bumps it so the
 * materialised BMP paths are fresh - LVGL caches decoded images by path, so reusing a
 * path after its file changed could show a stale cover. Fresh path each open avoids that. */
static unsigned g_cover_gen;
/* Bound the RAM cost: each 42px thumb is a few KB in tmpfs and /tmp is tiny (~19MB free),
 * so only materialise covers for the first N books; the rest fall back to the glyph. */
#define BOOKS_COVER_MAX 96

/* Remove every /tmp/bthumb_*.bmp left by a previous open so the temp files never
 * accumulate across screen opens (tmpfs is RAM). Called before writing this open's set. */
static void books_covers_cleanup(void){
    DIR *d = opendir("/tmp");
    if(!d) return;
    struct dirent *e;
    char p[300];
    while((e = readdir(d))){
        if(strncmp(e->d_name, "bthumb_", 7) != 0) continue;
        int n = snprintf(p, sizeof p, "/tmp/%s", e->d_name);
        if(n > 0 && n < (int)sizeof p) unlink(p);
    }
    closedir(d);
}

/* Human progress line: "Finished", "Not started", "3h 12m in" / "7m in". */
/* "9 h 51 m left" / "Finished" / "Not started" (needs the length; falls back to time in when unknown) */
static void left_text(const book_t *b, char *out, size_t len){
    if(b->completed){ snprintf(out, len, "Finished"); return; }
    if(b->duration_ms > 0){
        long pos = b->position_ms < 0 ? 0 : b->position_ms; if(pos > b->duration_ms) pos = b->duration_ms;
        long rem = (b->duration_ms - pos) / 1000, h = rem / 3600, m = (rem / 60) % 60;
        if(pos <= 0){ snprintf(out, len, "%ld h %02ld m", h, m); return; }
        if(h > 0) snprintf(out, len, "%ld h %02ld m left", h, m); else snprintf(out, len, "%ld m left", m > 0 ? m : 1);
        return;
    }
    if(b->position_ms <= 0){ snprintf(out, len, "Not started"); return; }
    long s2 = b->position_ms / 1000, h = s2 / 3600, m = (s2 / 60) % 60;
    if(h > 0) snprintf(out, len, "%ldh %ldm in", h, m); else snprintf(out, len, "%ldm in", m > 0 ? m : 1);
}
__attribute__((unused)) static void progress_text(const book_t *b, char *out, size_t len){
    if(b->completed){ snprintf(out, len, "Finished"); return; }
    if(b->position_ms <= 0){ snprintf(out, len, "Not started"); return; }
    long s = b->position_ms / 1000, h = s / 3600, m = (s / 60) % 60;
    if(h > 0) snprintf(out, len, "%ldh %ldm in", h, m);
    else      snprintf(out, len, "%ldm in", m > 0 ? m : 1);
}

static void book_row_cb(lv_event_t *e){
    if(lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if(i < 0 || i >= g_nbooks) return;
    /* re-read the bookmark, completion AND its age now (the list snapshot may be stale if the book
     * finished or advanced since the screen was built) so we resume at the LATEST saved position, and
     * restart a finished book from the beginning rather than its last-second tail. */
    long pos = 0, updated = 0; int completed = 0;
    int r = mdb_book_progress(g_books[i].path, NULL, 0, &pos, &completed, &updated);
    if(r < 0){ ui_toast("Couldn't read progress"); return; }   /* read error -> don't start a session that could erase the bookmark */
    long resume = completed ? 0 : pos;
    if(resume > 0 && updated > 0){       /* smart rewind: back up by how long it's been idle */
        long idle = (long)time(NULL) - updated;
        long rw = ui_smart_rewind_ms(idle);
        resume -= rw; if(resume < 0) resume = 0;
    }
    if(ui_play_book(g_books[i].path, resume)) screen_show(SCR_NOWPLAYING);   /* a book tap opens Now Playing, like a song tap does (not on a refusal) */
}

/* Left-edge cover for one row: the cached 42px thumb if this book has been played (and so
 * has art in the cache), otherwise a music glyph on the accent tile. Books that have never
 * been opened have no cached art yet - they show the glyph until first play. */
static void books_add_cover(lv_obj_t *row, const book_t *b, int i){
    /* the progress ring: how far through the book (a full green ring once finished) */
    int frac = b->completed ? 1000 : (b->duration_ms > 0 ? (int)((long long)(b->position_ms < 0 ? 0 : b->position_ms) * 1000 / b->duration_ms) : 0);
    if(frac > 1000) frac = 1000;
    lv_obj_t *ring = lv_arc_create(row);
    lv_obj_remove_style(ring, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(ring, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(ring, 52, 52); lv_obj_set_pos(ring, 8, 6);
    lv_arc_set_rotation(ring, 270); lv_arc_set_bg_angles(ring, 0, 360); lv_arc_set_range(ring, 0, 1000); lv_arc_set_value(ring, frac);
    lv_obj_set_style_arc_width(ring, 3, LV_PART_MAIN); lv_obj_set_style_arc_color(ring, TC(CONTROL_TRACK), LV_PART_MAIN);
    lv_obj_set_style_arc_width(ring, 3, LV_PART_INDICATOR); lv_obj_set_style_arc_rounded(ring, true, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(ring, b->completed ? TC(STATUS_SUCCESS) : ui_current_accent(), LV_PART_INDICATOR);
    lv_obj_t *tile = lv_obj_create(row);
    lv_obj_remove_style_all(tile);
    lv_obj_set_pos(tile, 13, 11); lv_obj_set_size(tile, 42, 42);
    lv_obj_set_style_radius(tile, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_clip_corner(tile, true, 0);
    lv_obj_set_style_bg_color(tile, TC(CONTROL_TRACK), 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
    /* lv_obj_create() is CLICKABLE by default; if the tile keeps that it eats the tap
     * meant for the row's play handler. Clear both flags so taps pass through to the row. */
    lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    char tmp[64], src[72];
    int have = 0;
    if(i < BOOKS_COVER_MAX && b->path[0]){
        snprintf(tmp, sizeof tmp, "/tmp/bthumb_%u_%d.bmp", g_cover_gen, i);
        have = (artcache_get_thumb(b->path, tmp) == 0);
    }
    if(have){
        snprintf(src, sizeof src, "A:/tmp/bthumb_%u_%d.bmp", g_cover_gen, i);
        lv_obj_t *img = lv_image_create(tile);
        lv_image_set_src(img, src);              /* LVGL copies the path string */
        lv_obj_center(img);
    } else {
        lv_obj_t *g = lv_label_create(tile);     /* no cached cover yet -> glyph */
        lv_label_set_text(g, LV_SYMBOL_AUDIO);
        lv_obj_set_style_text_font(g, TF(UI_20), 0);
        lv_obj_set_style_text_color(g, TC(TEXT_LYRICS), 0);
        lv_obj_center(g);
    }
}

static void books_rebuild(void){
    if(!g_list) return;
    lv_obj_clean(g_list);
    books_covers_cleanup();
    g_cover_gen++;
    int rc = mdb_books(g_books, BOOKS_MAX);
    if(rc < 0){                                  /* DB read error (not "empty") -> a retryable failure, not "add files" */
        g_nbooks = 0;
        lv_obj_t *l = lv_label_create(g_list);
        lv_label_set_text(l, "Couldn't read audiobooks\nReopen to try again");
        lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(l, 260);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(l, TC(TEXT_MUTED), 0);
        lv_obj_set_style_text_font(l, TF(UI_16), 0);
        return;
    }
    g_nbooks = rc;
    if(g_nbooks <= 0){
        lv_obj_t *l = lv_label_create(g_list);
        lv_label_set_text(l, "No audiobooks\nAdd .m4b files to the card");
        lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(l, 260);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(l, TC(TEXT_MUTED), 0);
        lv_obj_set_style_text_font(l, TF(UI_16), 0);
        return;
    }
    for(int i = 0; i < g_nbooks; i++){
        book_t *b = &g_books[i];
        lv_obj_t *r = lv_button_create(g_list);
        lv_obj_remove_style_all(r);
        lv_obj_set_size(r, 268, 66);
        lv_obj_add_flag(r, LV_OBJ_FLAG_USER_1);                 /* curves with the circle */
        lv_obj_set_style_radius(r, TH_R_ROW, 0);
        lv_obj_set_style_bg_color(r, TC(SURFACE), 0);
        lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(r, TC(SURFACE_RAISED), LV_STATE_PRESSED);
        lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
        ui_on(r, book_row_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i, "books.book_row", UI_CORE);

        books_add_cover(r, b, i);                               /* the cover in its progress ring */

        lv_obj_t *t = lv_label_create(r);                       /* title (book names may be non-Latin) */
        lv_label_set_text(t, b->title[0] ? b->title : "Untitled");
        lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
        lv_obj_set_pos(t, 70, 6); lv_obj_set_size(t, 186, 22);
        lv_obj_set_style_text_font(t, ui_font_cjk(16), 0);
        lv_obj_set_style_text_color(t, TC(TEXT_PRIMARY), 0);

        lv_obj_t *au = lv_label_create(r);                      /* author */
        lv_label_set_text(au, b->author[0] ? b->author : " ");
        lv_label_set_long_mode(au, LV_LABEL_LONG_DOT);
        lv_obj_set_pos(au, 70, 26); lv_obj_set_size(au, 186, 18);
        lv_obj_set_style_text_font(au, ui_font_cjk(14), 0);
        lv_obj_set_style_text_color(au, TC(TEXT_SECONDARY), 0);

        char left[40]; left_text(b, left, sizeof left);
        lv_obj_t *lf = lv_label_create(r);                      /* time left: accent, green when finished */
        lv_label_set_text(lf, left);
        lv_obj_set_pos(lf, 70, 46);
        lv_obj_set_style_text_font(lf, TF(UI_12), 0);
        lv_obj_set_style_text_color(lf, b->completed ? TC(STATUS_SUCCESS) : (b->position_ms > 0 ? ui_current_accent() : TC(TEXT_DISABLED)), 0);
    }
    lv_obj_update_layout(g_list); curvelist_update(&g_bcl);
    lv_obj_scroll_to_y(g_list, 0, LV_ANIM_OFF);
}

void books_create(lv_obj_t *root){
    lv_obj_set_style_bg_color(root, TC(CANVAS), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    ui_header(root, "Books");

    g_list = lv_obj_create(root);
    lv_obj_remove_style_all(g_list);
    lv_obj_set_pos(g_list, 46, 72); lv_obj_set_size(g_list, 268, 276);
    lv_obj_set_style_pad_bottom(g_list, 30, 0);   /* last row clears the round bottom bezel */
    lv_obj_set_style_bg_opa(g_list, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(g_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(g_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(g_list, 4, 0);
    lv_obj_set_scroll_dir(g_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(g_list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(g_list, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    curvelist_attach(&g_bcl, g_list, root, 268);
}

void books_open(void){
    books_rebuild();
    screen_show(SCR_BOOKS);
}

/* ---- Chapter navigation (SCR_CHAPTERS): reached from the Now Playing right-hub for an audiobook.
 * Lists the current book's chapters; tapping one seeks to its start and returns to Now Playing. */
#define CHAPS_MAX 256
static lv_obj_t *g_chap_list;                /* the full list, inside the List overlay */
static lv_obj_t *g_chap_ov;
static chapter_t g_chlist[CHAPS_MAX];
static int       g_chlist_n;
static char      g_chlist_path[512];   /* the book these chapters belong to (a jump must target THAT book) */

static void chap_fmt_dur(long ms, char *out, size_t n){
    if(ms < 0) ms = 0;
    long s = ms / 1000, h = s / 3600, m = (s / 60) % 60;
    if(h > 0)      snprintf(out, n, "%ldh %02ldm", h, m);
    else if(m > 0) snprintf(out, n, "%ldm", m);
    else           snprintf(out, n, "%lds", s % 60);
}

static void chap_row_cb(lv_event_t *e){
    if(lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if(i < 0 || i >= g_chlist_n) return;
    /* Only seek if the track that was current when this list was built is STILL current - otherwise a
     * queue advance or a transport change since the screen opened would seek the wrong track. */
    track_state_t st; ipc_get_state(&st);
    if(!st.path[0] || strcmp(st.path, g_chlist_path) != 0){ screen_show(SCR_NOWPLAYING); return; }
    long tgt = g_chlist[i].start_ms;
    if(st.duration_ms > 0 && tgt >= st.duration_ms) tgt = st.duration_ms - 1;   /* clamp to a valid position */
    if(tgt < 0) tgt = 0;
    if(ui_seek_to(tgt) == 0) ui_book_user_seeked(tgt);  /* drop pending resume only if the jump went out */
    screen_show(SCR_NOWPLAYING);
}

static void chap_empty(const char *msg){
    lv_obj_t *l = lv_label_create(g_chap_list);
    lv_label_set_text(l, msg);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, 260);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(l, TC(TEXT_MUTED), 0);
    lv_obj_set_style_text_font(l, TF(UI_16), 0);
}

static void chapters_rebuild(void){
    if(!g_chap_list) return;
    lv_obj_clean(g_chap_list);
    g_chlist_n = 0; g_chlist_path[0] = 0;
    track_state_t st; ipc_get_state(&st);
    if(!st.path[0] || !mdb_is_book_path(st.path)){ chap_empty("No audiobook playing"); return; }
    snprintf(g_chlist_path, sizeof g_chlist_path, "%s", st.path);   /* pin this list to its book */
    g_chlist_n = scan_read_chapters(st.path, g_chlist, CHAPS_MAX);
    if(g_chlist_n <= 0){ chap_empty("This book has no chapters"); return; }
    long dur = st.duration_ms;
    int cur = 0;
    for(int i = 0; i < g_chlist_n; i++){ if(g_chlist[i].start_ms <= st.position_ms) cur = i; else break; }
    lv_obj_t *cur_row = NULL;
    for(int i = 0; i < g_chlist_n; i++){
        long endms = (i + 1 < g_chlist_n) ? g_chlist[i+1].start_ms : (dur > 0 ? dur : g_chlist[i].start_ms);
        char durbuf[16]; chap_fmt_dur(endms - g_chlist[i].start_ms, durbuf, sizeof durbuf);

        lv_obj_t *r = lv_button_create(g_chap_list);
        lv_obj_remove_style_all(r);
        lv_obj_set_size(r, 268, 52);
        lv_obj_set_style_radius(r, 10, 0);
        lv_obj_set_style_bg_color(r, (i == cur ? TC(SURFACE_RAISED) : TC(SURFACE)), 0);
        lv_obj_set_style_bg_opa(r, i == cur ? LV_OPA_COVER : LV_OPA_50, 0);
        lv_obj_set_style_bg_color(r, TC(CONTROL_TRACK), LV_STATE_PRESSED);
        lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
        ui_on(r, chap_row_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i, "books.chap_row", UI_CORE);

        lv_obj_t *num = lv_label_create(r);                     /* chapter number, accent when current */
        char nb[8]; snprintf(nb, sizeof nb, "%d", i + 1);
        lv_label_set_text(num, nb);
        lv_obj_set_pos(num, 12, 16); lv_obj_set_size(num, 34, 20);
        lv_obj_set_style_text_font(num, TF(UI_14), 0);
        lv_obj_set_style_text_color(num, i == cur ? TC(ACCENT_PRIMARY) : TC(TEXT_MUTED), 0);

        lv_obj_t *t = lv_label_create(r);                       /* chapter title (may be non-Latin) */
        lv_label_set_text(t, g_chlist[i].title);
        lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
        lv_obj_set_pos(t, 50, 16); lv_obj_set_size(t, 168, 20);
        lv_obj_set_style_text_font(t, ui_font_cjk(16), 0);
        lv_obj_set_style_text_color(t, TC(TEXT_PRIMARY), 0);

        lv_obj_t *d = lv_label_create(r);                       /* chapter duration, right-aligned */
        lv_label_set_text(d, durbuf);
        lv_obj_set_pos(d, 200, 16); lv_obj_set_size(d, 56, 20);
        lv_obj_set_style_text_align(d, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_style_text_font(d, TF(UI_12), 0);
        lv_obj_set_style_text_color(d, TC(TEXT_MUTED), 0);

        if(i == cur) cur_row = r;
    }
    if(cur_row) lv_obj_scroll_to_view(cur_row, LV_ANIM_OFF);    /* open centred on the current chapter */
}
static void chapters_paint(void);

/* ---- the ring ---------------------------------------------------------------------------------------
 * One segment per chapter round the rim: heard = light grey, the current one = accent, the rest dark. Tap
 * a segment to jump to that chapter. Books with more than 40 chapters get a continuous progress ring instead
 * (segments would be too thin to hit); the previous / next buttons and the full list work for every book. */
#define CH_R 146
#define CH_W 12
#define CH_SEG_MAX 40
static lv_obj_t *g_ch_canvas, *g_ch_hit, *g_ch_cover, *g_ch_cimg, *g_ch_cnote, *g_ch_num, *g_ch_title, *g_ch_time, *g_ch_prev, *g_ch_next, *g_ch_list_btn;
static uint8_t *g_ch_buf;
static int g_ch_cur = -1;
static long g_ch_pos, g_ch_dur;
static lv_timer_t *g_ch_tmr;

static int chap_current(long pos){ int c = 0; for(int i = 0; i < g_chlist_n; i++){ if(g_chlist[i].start_ms <= pos) c = i; else break; } return c; }
static void fmt_clock(long ms, char *o, size_t n){
    if(ms < 0) ms = 0;
    long t = ms / 1000, h = t / 3600, m = (t / 60) % 60, sec = t % 60;
    if(h > 0) snprintf(o, n, "%ld:%02ld:%02ld", h, m, sec); else snprintf(o, n, "%ld:%02ld", m, sec);
}
static void chap_jump(int i){                                    /* seek to chapter i (same guard as the list) */
    if(i < 0 || i >= g_chlist_n) return;
    track_state_t st; ipc_get_state(&st);
    if(!st.path[0] || strcmp(st.path, g_chlist_path) != 0){ screen_show(SCR_NOWPLAYING); return; }
    long tgt = g_chlist[i].start_ms;
    if(st.duration_ms > 0 && tgt >= st.duration_ms) tgt = st.duration_ms - 1;
    if(tgt < 0) tgt = 0;
    if(ui_seek_to(tgt) == 0) ui_book_user_seeked(tgt);
    screen_show(SCR_NOWPLAYING);
}
static void ch_arc(lv_layer_t *L, int r, int w, float a0, float a1, lv_color_t c){
    lv_draw_arc_dsc_t d; lv_draw_arc_dsc_init(&d);
    d.center.x = 180; d.center.y = 180; d.radius = (uint16_t)r; d.width = (uint16_t)w;
    while(a0 < 0){ a0 += 360; a1 += 360; }
    d.start_angle = (lv_value_precise_t)a0; d.end_angle = (lv_value_precise_t)a1; d.color = c; d.opa = LV_OPA_COVER;
    lv_draw_arc(L, &d);
}
static void chapters_paint(void){
    if(!g_ch_canvas) return;
    track_state_t st; ipc_get_state(&st);
    g_ch_pos = st.position_ms; g_ch_dur = st.duration_ms;
    int n = g_chlist_n, cur = n > 0 ? chap_current(g_ch_pos) : -1;
    lv_canvas_fill_bg(g_ch_canvas, TC(CANVAS), LV_OPA_COVER);
    lv_layer_t L; lv_canvas_init_layer(g_ch_canvas, &L);
    lv_color_t track = TC(CONTROL_TRACK), heard = TC(BOOK_HEARD), acc = ui_current_accent();
    if(n >= 1 && n <= CH_SEG_MAX){
        float step = 360.0f / n, gap = n > 1 ? 2.4f : 0.0f;
        for(int i = 0; i < n; i++){
            float a0 = -90.0f + i * step + gap / 2, a1 = -90.0f + (i + 1) * step - gap / 2;
            ch_arc(&L, CH_R, CH_W, a0, a1, i == cur ? acc : (i < cur ? heard : track));
        }
    } else if(n > CH_SEG_MAX){
        ch_arc(&L, CH_R, CH_W, -90.0f, 270.0f, track);
        if(g_ch_dur > 0 && g_ch_pos > 0){ float f = (float)g_ch_pos / (float)g_ch_dur; if(f > 1) f = 1; ch_arc(&L, CH_R, CH_W, -90.0f, -90.0f + 360.0f * f - 0.01f, acc); }
    }
    lv_canvas_finish_layer(g_ch_canvas, &L);
    char b[96];
    if(n <= 0){
        lv_label_set_text(g_ch_num, g_chlist_path[0] ? "No chapters" : "No audiobook playing");
        lv_label_set_text(g_ch_title, ""); lv_label_set_text(g_ch_time, "");
    } else {
        snprintf(b, sizeof b, "Chapter %d of %d", cur + 1, n); lv_label_set_text(g_ch_num, b);
        lv_label_set_text(g_ch_title, g_chlist[cur].title[0] ? g_chlist[cur].title : "-");
        long endms = (cur + 1 < n) ? g_chlist[cur + 1].start_ms : (g_ch_dur > 0 ? g_ch_dur : g_chlist[cur].start_ms);
        char e1[16], e2[16]; fmt_clock(g_ch_pos - g_chlist[cur].start_ms, e1, sizeof e1); fmt_clock(endms - g_chlist[cur].start_ms, e2, sizeof e2);
        snprintf(b, sizeof b, "%s / %s", e1, e2); lv_label_set_text(g_ch_time, b);
    }
    if(cur != g_ch_cur){ g_ch_cur = cur; }
    /* the cover (the playing book's) in the middle */
    const void *dsc = ui_current_cover_dsc();
    lv_image_set_src(g_ch_cimg, NULL);
    if(dsc){ const lv_image_dsc_t *d = dsc; lv_image_set_src(g_ch_cimg, dsc);
             if(d->header.w > 0) lv_image_set_scale(g_ch_cimg, (uint32_t)(76 * 256 / d->header.w) + 2);
             lv_obj_center(g_ch_cimg); lv_obj_remove_flag(g_ch_cimg, LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(g_ch_cnote, LV_OBJ_FLAG_HIDDEN); }
    else { lv_obj_add_flag(g_ch_cimg, LV_OBJ_FLAG_HIDDEN); lv_obj_remove_flag(g_ch_cnote, LV_OBJ_FLAG_HIDDEN); }
    lv_obj_set_style_bg_color(g_ch_cover, ui_media_accent(), 0);
}
static void ch_ring_cb(lv_event_t *e){                          /* a tap on a segment */
    if(g_chlist_n < 1 || g_chlist_n > CH_SEG_MAX) return;
    lv_indev_t *in = lv_indev_active(); if(!in) return;
    lv_point_t p; lv_indev_get_point(in, &p);
    float dx = p.x - 180.0f, dy = p.y - 180.0f, d = sqrtf(dx * dx + dy * dy);
    if(d < CH_R - CH_W - 14 || d > CH_R + 22) return;
    float a = atan2f(dx, -dy) * 57.29578f; if(a < 0) a += 360.0f;
    chap_jump((int)(a / (360.0f / g_chlist_n)) % g_chlist_n);
}
static void ch_step_cb(lv_event_t *e){
    int dir = (int)(intptr_t)lv_event_get_user_data(e);
    if(g_chlist_n < 1) return;
    int cur = chap_current(g_ch_pos), to = cur + dir;
    if(dir < 0 && g_ch_pos - g_chlist[cur].start_ms > 3000) to = cur;    /* like a player: first back to this chapter's start */
    if(to < 0) to = 0;
    if(to >= g_chlist_n){ ui_toast("Last chapter"); return; }
    chap_jump(to);
}
static void ch_list_cb(lv_event_t *e){ (void)e; if(g_chap_ov){ lv_obj_remove_flag(g_chap_ov, LV_OBJ_FLAG_HIDDEN); } }
static void ch_list_close_cb(lv_event_t *e){ (void)e; if(g_chap_ov) lv_obj_add_flag(g_chap_ov, LV_OBJ_FLAG_HIDDEN); }
static void ch_tick(lv_timer_t *t){ (void)t; if(screen_current() == SCR_CHAPTERS && g_chap_ov && lv_obj_has_flag(g_chap_ov, LV_OBJ_FLAG_HIDDEN)) chapters_paint(); }
static lv_obj_t *ch_btn(lv_obj_t *root, const char *txt, int cx, int w, int dir, lv_event_cb_t cb){
    lv_obj_t *b = lv_button_create(root);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, w, 32); lv_obj_set_pos(b, cx - w / 2, 236);
    lv_obj_set_style_radius(b, 16, 0);
    lv_obj_set_style_bg_color(b, TC(SURFACE), 0); lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, TC(SURFACE_RAISED), LV_STATE_PRESSED);
    lv_obj_set_ext_click_area(b, 4);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, (void *)(intptr_t)dir);
    lv_obj_t *l = lv_label_create(b); lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, TF(UI_14), 0); lv_obj_set_style_text_color(l, TC(TEXT_PRIMARY), 0); lv_obj_center(l);
    return b;
}
void chapters_create(lv_obj_t *root){
    lv_obj_set_style_bg_color(root, TC(CANVAS), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    g_ch_buf = malloc(LV_CANVAS_BUF_SIZE(360, 360, 32, LV_DRAW_BUF_STRIDE_ALIGN));
    if(g_ch_buf){
        g_ch_canvas = lv_canvas_create(root);
        lv_canvas_set_buffer(g_ch_canvas, g_ch_buf, 360, 360, LV_COLOR_FORMAT_XRGB8888);
        lv_obj_clear_flag(g_ch_canvas, LV_OBJ_FLAG_CLICKABLE);
    }
    g_ch_hit = lv_obj_create(root);                                     /* taps on the ring */
    lv_obj_remove_style_all(g_ch_hit);
    lv_obj_set_size(g_ch_hit, 360, 360);
    lv_obj_add_flag(g_ch_hit, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(g_ch_hit, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(g_ch_hit, ch_ring_cb, LV_EVENT_SHORT_CLICKED, NULL);
    g_ch_cover = lv_obj_create(root);
    lv_obj_remove_style_all(g_ch_cover);
    lv_obj_set_size(g_ch_cover, 76, 76); lv_obj_align(g_ch_cover, LV_ALIGN_TOP_MID, 0, 62);
    lv_obj_set_style_radius(g_ch_cover, LV_RADIUS_CIRCLE, 0); lv_obj_set_style_clip_corner(g_ch_cover, true, 0);
    lv_obj_set_style_bg_opa(g_ch_cover, LV_OPA_COVER, 0);
    lv_obj_clear_flag(g_ch_cover, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    g_ch_cnote = lv_label_create(g_ch_cover); lv_label_set_text(g_ch_cnote, LV_SYMBOL_AUDIO);
    lv_obj_set_style_text_font(g_ch_cnote, TF(UI_20), 0); lv_obj_set_style_text_color(g_ch_cnote, TC(TEXT_PRIMARY), 0); lv_obj_center(g_ch_cnote);
    g_ch_cimg = lv_image_create(g_ch_cover); lv_obj_add_flag(g_ch_cimg, LV_OBJ_FLAG_HIDDEN);
    g_ch_num = lv_label_create(root);
    lv_obj_set_style_text_font(g_ch_num, ui_font_cjk(16), 0); lv_obj_set_style_text_color(g_ch_num, TC(TEXT_PRIMARY), 0);
    lv_obj_align(g_ch_num, LV_ALIGN_TOP_MID, 0, 148);
    g_ch_title = lv_label_create(root);
    lv_label_set_long_mode(g_ch_title, LV_LABEL_LONG_DOT); lv_obj_set_width(g_ch_title, 190);
    lv_obj_set_style_text_align(g_ch_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(g_ch_title, ui_font_cjk(14), 0); lv_obj_set_style_text_color(g_ch_title, TC(TEXT_SECONDARY), 0);
    lv_obj_align(g_ch_title, LV_ALIGN_TOP_MID, 0, 172);
    g_ch_time = lv_label_create(root);
    lv_obj_set_style_text_font(g_ch_time, TF(UI_12), 0); lv_obj_set_style_text_color(g_ch_time, TC(TEXT_DISABLED), 0);
    lv_obj_align(g_ch_time, LV_ALIGN_TOP_MID, 0, 198);
    g_ch_prev = ch_btn(root, LV_SYMBOL_LEFT, 132, 40, -1, ch_step_cb);
    g_ch_list_btn = ch_btn(root, "List", 180, 48, 0, ch_list_cb);
    g_ch_next = ch_btn(root, LV_SYMBOL_RIGHT, 228, 40, +1, ch_step_cb);
    /* the full list (every chapter, any length of book), in an overlay */
    g_chap_ov = lv_obj_create(root);
    lv_obj_remove_style_all(g_chap_ov);
    lv_obj_set_size(g_chap_ov, 360, 360);
    lv_obj_set_style_bg_color(g_chap_ov, TC(CANVAS), 0); lv_obj_set_style_bg_opa(g_chap_ov, LV_OPA_COVER, 0);
    lv_obj_add_flag(g_chap_ov, LV_OBJ_FLAG_CLICKABLE); lv_obj_clear_flag(g_chap_ov, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *ht = lv_label_create(g_chap_ov); lv_label_set_text(ht, "Chapters");
    lv_obj_set_style_text_font(ht, TH_F_CAPTION, 0); lv_obj_set_style_text_color(ht, TC(TEXT_SECONDARY), 0);
    lv_obj_align(ht, LV_ALIGN_TOP_MID, -22, 22);
    lv_obj_t *dn = lv_button_create(g_chap_ov);
    lv_obj_remove_style_all(dn);
    lv_obj_set_size(dn, 50, 22); lv_obj_align(dn, LV_ALIGN_TOP_MID, 26, 18);
    lv_obj_set_style_radius(dn, 11, 0); lv_obj_set_style_bg_color(dn, TC(SURFACE), 0); lv_obj_set_style_bg_opa(dn, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(dn, ch_list_close_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *dl = lv_label_create(dn); lv_label_set_text(dl, "Done"); lv_obj_set_style_text_font(dl, TH_F_CAPTION, 0); lv_obj_set_style_text_color(dl, TC(TEXT_PRIMARY), 0); lv_obj_center(dl);
    g_chap_list = lv_obj_create(g_chap_ov);
    lv_obj_remove_style_all(g_chap_list);
    lv_obj_set_pos(g_chap_list, 30, 56); lv_obj_set_size(g_chap_list, 300, 290);
    lv_obj_set_style_pad_bottom(g_chap_list, 44, 0);
    lv_obj_set_style_bg_opa(g_chap_list, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(g_chap_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(g_chap_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(g_chap_list, 6, 0);
    lv_obj_set_scroll_dir(g_chap_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(g_chap_list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(g_chap_list, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_add_flag(g_chap_ov, LV_OBJ_FLAG_HIDDEN);
    if(!g_ch_tmr) g_ch_tmr = lv_timer_create(ch_tick, 1000, NULL);
}

void chapters_open(void){
    if(g_chap_ov) lv_obj_add_flag(g_chap_ov, LV_OBJ_FLAG_HIDDEN);
    chapters_rebuild();
    chapters_paint();
    screen_show(SCR_CHAPTERS);
}
