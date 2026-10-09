/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "imm_record.h"
/* Copyright (C) 2026 diskOS contributors */
#include "ui.h"
#include "lvgl/src/core/lv_obj_draw_private.h"   /* lv_obj_get_ext_draw_size: the snapshot margin (fork perf) */
#include "lvgl/lvgl.h"
#include "lvgl/src/draw/lv_image_decoder_private.h"   /* poster: decode the cover once into RAM */
#include "art.h"
#include "ipc.h"
#include "musicdb.h"   /* persistent per-song accent cache */
#include "scanner.h"   /* scan_read_chapters: Now Playing shows a book's current chapter */
#include "netart.h"
#include "artcache.h"   /* persistent decoded-cover cache on SD */
#include "config.h"
#include "screens.h"
#include "ma.h"
#include "modes.h"
#include <sys/stat.h>
#include "theme_kit.h"
#include "braun.h"
#include "theme.h"
#include "anim.h"
#include "fonts_intl.h"   /* Cyrillic/Greek/Latin-ext fallback (issue #3) */

#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <unistd.h>
#include <ctype.h>

#define C_BLACK      0x000000
#define C_LINE       0x1C1C1E
#define C_LINE_SOFT  0x2C2C2E
#define C_WHITE      0xFFFFFF
#define C_SECONDARY  0xC7C7CC
#define C_TERTIARY   0x8E8E93

LV_FONT_DECLARE(font_icons_28)
#define HEART_FILLED  "\xEF\x80\x84"   /* FA f004 solid heart */
#define HEART_OUTLINE "\xEF\x82\x8A"   /* FA f08a outline heart */
#define MOON_ICON     "\xEF\x86\x86"   /* FA f186 moon (audiobook sleep timer) */
#define MODE_ARROW    "\xEF\x85\xB8"   /* FA f178 long-arrow-right (sequential) */
#define C_ACCENT     UI_RED

#define ARC_D        332
#define ARC_SWEEP    286
#define ARC_ROT      127

#define COVER_D      148

static lv_obj_t *backdrop;
static lv_obj_t *g_np_scr, *g_br_seg, *g_br_disc;   /* Braun: the Now Playing root, its lower segment, the cover's panel */
static lv_obj_t *ring;
static lv_obj_t *cover;
static lv_obj_t *cover_img;
static lv_obj_t *cover_note;
static lv_obj_t *spindle;       /* vinyl-style centre label (hidden in Cover mode) */
/* full-screen album-art view - reuses the stock player's /usr/data/fiio/cover.png (364px sharp,
 * incl. its online-fetched art). Tap the cover to open, tap again to close. Overlay on the NP root. */
static lv_obj_t *fsart, *fsart_img, *fsart_title, *fsart_artist;
static int fsart_on;
static lv_obj_t *title;
static lv_obj_t *artist;
static lv_obj_t *album;   /* also hosts the track position: "N / M" or "Album · N/M" */
static lv_obj_t *btn_pp;
static lv_obj_t *btn_prev;
static lv_obj_t *btn_next;
static lv_obj_t *btn_fav;
static lv_obj_t *fav_icon;
static lv_obj_t *btn_sleep;    /* audiobook sleep-timer moon (occupies the heart slot for books) */
static lv_obj_t *sleep_icon;
static int g_np_fav, g_np_have, g_fav_px, g_fav_py;
/* Optimistic-favourite hold: a heart tap flips the widget + sends 0104 before the player
 * confirms via a2. Position (a1) frames trigger a full NP refresh from the OLD is_favorite,
 * which would flip the heart back. We hold the tapped value for this exact track until the
 * player confirms it, the track changes, or a timeout - so the heart doesn't visibly bounce. */
static char g_np_curpath[520];
static int  g_favp_active = 0, g_favp_val = 0;
static uint32_t g_favp_set = 0;
static char g_favp_path[520];
static lv_obj_t *btn_mode;
static lv_obj_t *btn_queue, *queue_badge, *queue_badge_lbl;   /* the Queue, under the play-mode icon */
static void np_queue_place(void);
static lv_obj_t *mode_icon;
static lv_obj_t *mode_one;
static int g_np_mode = -1, g_mode_px, g_mode_py;
static lv_obj_t *t_elapsed;
static lv_obj_t *t_remain;
static lv_obj_t *t_sep;
/* ---- "Poster" Now Playing style (np_style 2) + immersive vinyl/lyrics mode ---- */
#define SHOW(o, on) do{ if(o){ if(on) lv_obj_remove_flag((o), LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag((o), LV_OBJ_FLAG_HIDDEN); } }while(0)
static int g_bd_ok;                                /* blurred backdrop held in RAM (below) */
static int g_np_poster = 0;
static int g_np_ring = 0;                         /* np_style 3: the Ring design */
static lv_image_dsc_t g_coverdsc;                 /* defined with the vinyl decoder below */
#ifndef CBMP
#define CBMP 148                                  /* the vinyl decoder's cover size (same value below) */
#endif
static lv_font_t s_font28, s_font24, s_font20, s_font16;
static int g_arc_rot = ARC_ROT, g_arc_sweep = ARC_SWEEP;   /* poster moves the seek ring to the bottom rim */
static int g_arc_rev = 0;   /* 1: progress runs against LVGL's clockwise angles (bottom rim, left -> right) */
static lv_obj_t *poster_img, *poster_dim, *poster_top, *poster_fade, *np_scrim, *btn_imm, *vol_arc;
static lv_obj_t *imm_spin, *imm_prog, *imm_hint, *imm_lyr[3], *imm_scrim;
static lv_obj_t *imm_deco[6];  /* groove rings + label objects: only for the 148px fallback (baked otherwise) */
static lv_obj_t *title_sh, *artist_sh;     /* poster: text drop shadows */
static lv_obj_t *np_dots[2];                /* page dots (hidden in Poster: they sat on the volume arc) */
static lv_obj_t *np_mid_btn;                /* poster: tap the title -> the album, this song highlighted */
static lv_timer_t *imm_timer;
static int32_t imm_angle;
static int imm_vol_last = -1;
static lv_color_t np_acc; static int np_acc_set; static char np_acc_path[520];
static lyr_line_t g_lyr[240]; static int g_lyr_n = -1; static char g_lyr_path[520];

static lv_color_t accent;
static char last_path[256];
static int g_art_force;
void ui_art_retry(void){ last_path[0] = 0; g_art_force = 1; }
int ui_take_art_force(void){ int f = g_art_force; g_art_force = 0; return f; }
static char g_stat_path[256];   /* last track counted in play history (count once per new track) */
/* Art cache key = album title + the track's PARENT DIRECTORY (not artist). This reuses art
 * within a real album AND within a compilation/various-artists album (same folder), but does
 * NOT reuse across two genuinely-different albums that merely share a title in different
 * folders. For a flat library it degrades to album-only - no worse than before. */
static char last_art_key[420];
void ui_art_redo(void){ last_path[0] = 0; last_art_key[0] = 0; g_art_force = 1; }  /* key of the art currently shown */
static char want_art_key[420];  /* key the CURRENT track wants; a finished decode applies only if it matches */
static char last_seed_a[160];
static char last_seed_b[160];
static char cover_src[48];
static int  cover_valid;     /* 1 when cover_src points at a freshly decoded cover */
static int  coverdsc_valid;  /* 1 only when g_coverdsc holds THIS track's RAM decode (else stale) */
static char thumb_src[48]; /* tiny 42px thumb for the Home pill */
static int  thumb_valid;
static char backdrop_src[48]; /* full-screen blurred backdrop */
static int  backdrop_valid;
static int displayed_idx;   /* art buffer (0/1) currently shown; new decodes target ^1 */
static int32_t shown_progress;
static int  g_scrubbing;     /* finger on the seek arc - don't fight it */
static long g_track_dur;     /* current track duration (ms) for seek math */
static long g_seek_lo, g_seek_hi;  /* the ms window the ring spans + seeks within: a chapter for a book, the whole track otherwise */
/* After releasing a seek, the player keeps streaming the OLD position for a
 * beat before it processes the jump, which makes the arc snap back then jump
 * forward. Hold the display at the seeked target and ignore stale echoes until
 * the player's stream reaches it (or the window lapses). */
static uint32_t g_seek_hold_until = 0;
static long     g_seek_target_ms = -1;   /* ABSOLUTE ms we seeked to - window-independent, so a seek that lands on a chapter boundary (window flips) isn't mistaken for a stale echo */
static long     g_scrub_lo, g_scrub_hi;  /* the ms window LATCHED when a ring drag starts - playback advancing under the finger (a chapter boundary crossed mid-drag) must not reinterpret the arc */
static char     g_scrub_path[520];       /* the track the drag STARTED on - if the track changes before release, the release seek (computed from the old track's window) must NOT be applied to the new track */
/* A seek and a back/hub swipe can both start anywhere on the ring, so we tell
 * them apart by DIRECTION: a seek follows the ring (curved / has a vertical
 * component) while back/hub is a long, straight, horizontal slide. Once a drag
 * looks horizontal we freeze the bar (no seek preview) and let the main loop
 * navigate; otherwise the ring scrubs as normal. */
static int g_seek_sx, g_seek_sy;   /* seek gesture press origin */
static int g_seek_cand;            /* press landed on the ring band (seek candidate) */
static int g_seek_on;              /* seek confirmed & actively scrubbing */



static void mmss(long ms, char *buf, size_t len)
{
    if(ms < 0) ms = 0;
    long total = ms / 1000;
    long sec = total % 60;
    long hr  = total / 3600;
    if(hr > 0){                                  /* audiobook-length: H:MM:SS (music tracks stay M:SS) */
        long min = (total / 60) % 60;
        if(hr > 999) hr = 999;
        snprintf(buf, len, "%ld:%02ld:%02ld", hr, min, sec);
    } else {
        snprintf(buf, len, "%ld:%02ld", total / 60, sec);
    }
}

static void copy_cstr(char *dst, size_t dst_len, const char *src)
{
    if(dst_len == 0) return;
    if(src == NULL) src = "";
    strncpy(dst, src, dst_len - 1);
    dst[dst_len - 1] = '\0';
}

static void set_label_text_changed(lv_obj_t *obj, const char *txt)
{
    const char *cur;

    if(obj == NULL) return;
    if(txt == NULL) txt = "";

    cur = lv_label_get_text(obj);
    if(cur == NULL || strcmp(cur, txt) != 0) {
        lv_label_set_text(obj, txt);
    }
}

/* ---- Now Playing seek recognizer ---------------------------------------
 * The ring is DISPLAY-ONLY (not clickable); main.c feeds raw touch into these.
 * A seek is recognized only when the press lands on the ring band within the
 * progress sweep (and below the top drawer zone) AND the drag follows the ring
 * rather than being a straight horizontal nav slide -- so nav swipes and the
 * top-drawer pull never move the arc. "Classify first, move the arc second." */
#define RING_CX  g_ring_cx
#define RING_CY  g_ring_cy
#define RING_R   g_ring_r
static int g_ring_cx = 180, g_ring_cy = 180, g_ring_r = ARC_D / 2;   /* Ring style: around the cover */
static int g_tol_grab = 34, g_tol_wander = 48;                       /* band half-widths (px) */

static lv_obj_t *g_seek_bar;
static int g_np_backdrop_on = 1, g_pp_filled;
static void seek_show(int32_t v){
    if(ring) lv_arc_set_value(ring, v);
    if(g_seek_bar){
        if(lv_obj_check_type(g_seek_bar, &lv_slider_class)) lv_slider_set_value(g_seek_bar, v, LV_ANIM_OFF);
        else lv_bar_set_value(g_seek_bar, v, LV_ANIM_OFF);
    }
}
void ui_np_seek_line(lv_obj_t *bar){
    g_seek_bar = bar;
    if(!bar) return;
    if(lv_obj_check_type(bar, &lv_slider_class)) lv_slider_set_range(bar, 0, 1000);
    else lv_bar_set_range(bar, 0, 1000);
    seek_show(shown_progress);
}
void ui_np_pp_filled(int on){ g_pp_filled = on; }
void ui_np_backdrop_enabled(int on){
    g_np_backdrop_on = on;
    if(!on && backdrop) lv_obj_add_flag(backdrop, LV_OBJ_FLAG_HIDDEN);
}
static int seek_on_band(int x, int y, int tol){
    if(g_seek_bar){ lv_area_t a; lv_obj_get_coords(g_seek_bar, &a);
        return x>=a.x1-12 && x<=a.x2+12 && y>=a.y1-tol && y<=a.y2+tol; }
    int dx = x - RING_CX, dy = y - RING_CY;
    int d2 = dx*dx + dy*dy;
    int lo = RING_R - tol, hi = RING_R + tol;
    return d2 >= lo*lo && d2 <= hi*hi;
}
static double seek_rel_angle(int x, int y){
    double a = atan2((double)(y - RING_CY), (double)(x - RING_CX)) * 57.2957795;
    double rel = a - g_arc_rot;
    while(rel < 0) rel += 360.0;
    while(rel >= 360.0) rel -= 360.0;
    return rel;   /* degrees from the start of the sweep, clockwise */
}
static int32_t seek_pt_to_value(int x, int y){
    if(g_seek_bar){ lv_area_t a; lv_obj_get_coords(g_seek_bar, &a);
        int w=a.x2-a.x1; int32_t v=w>0 ? (int32_t)((long long)(x-a.x1)*1000/w) : 0;
        return v<0 ? 0 : v>1000 ? 1000 : v; }
    double rel = seek_rel_angle(x, y);
    if(rel > g_arc_sweep) rel = (rel - g_arc_sweep < 360.0 - rel) ? g_arc_sweep : 0.0;
    int32_t v = (int32_t)(rel / g_arc_sweep * 1000.0 + 0.5);
    if(g_arc_rev) v = 1000 - v;                          /* bottom rim: left end = start of the track */
    if(v < 0) v = 0;
    if(v > 1000) v = 1000;
    return v;
}

/* press: returns 1 if this touch could be a seek (and arms the recognizer) */
int ui_np_seek_press(int x, int y){
    g_seek_cand = 0; g_seek_on = 0; g_scrubbing = 0;   /* also clears a stuck scrub from a missed release */
    if(y < 40) return 0;                          /* top drawer zone */
    if(g_track_dur <= 0) return 0;                /* nothing to seek */
    if(!seek_on_band(x, y, g_tol_grab)) return 0;  /* not on the ring (grab band) */
    if(!g_seek_bar && seek_rel_angle(x, y) > g_arc_sweep) return 0;/* in the bottom gap (transport buttons), not the arc */
    g_seek_sx = x; g_seek_sy = y; g_seek_cand = 1;
    return 1;
}
/* move: returns 1 while a seek owns the gesture (so main.c skips navigation) */
int ui_np_seek_move(int x, int y){
    if(!g_seek_cand) return 0;
    int dx = x - g_seek_sx, dy = y - g_seek_sy;
    int adx = dx<0?-dx:dx, ady = dy<0?-dy:dy;
    if(!seek_on_band(x, y, g_tol_wander)){         /* wandered off the ring -> a nav swipe */
        if(g_seek_on){ g_seek_on = 0; g_scrubbing = 0; seek_show(shown_progress); }
        g_seek_cand = 0; return 0;
    }
    if(!g_seek_on){
        if(adx < 26 && ady < 26) return 0;            /* not enough travel to classify; hold the arc */
        if(!g_seek_bar && adx > ady*2){ g_seek_cand = 0; return 0; } /* straight horizontal -> nav swipe (top-of-arc is ambiguous; bias to no-glitch) */
        g_seek_on = 1; g_scrubbing = 1;               /* confirmed: a deliberate ring drag */
        g_scrub_lo = g_seek_lo; g_scrub_hi = g_seek_hi;   /* latch the window NOW so it can't shift under the finger */
        if(g_scrub_hi <= g_scrub_lo){ g_scrub_lo = 0; g_scrub_hi = g_track_dur; }
        copy_cstr(g_scrub_path, sizeof g_scrub_path, g_np_curpath);   /* latch WHICH track this drag is on */
    }
    int32_t v = seek_pt_to_value(x, y);
    seek_show(v);
    shown_progress = v;   /* keep the render cache in sync with the direct arc write, else set_progress_changed() can skip a needed reset (e.g. drag to a chapter end -> next chapter's progress 0 == a stale cached 0 -> arc stuck at 100%) */
    {
        long lo = g_scrub_lo, hi = g_scrub_hi;   /* latched window (music: whole track) */
        if(hi > lo){
            long span = hi - lo;
            long rel  = (long)((int64_t)v * span / 1000);   /* preview is window-relative (chapter for a book) */
            char b[12]; mmss(rel, b, sizeof b); set_label_text_changed(t_elapsed, b);
            char r[12]; mmss(span - rel, r, sizeof r);
            char rr[14]; snprintf(rr, sizeof rr, "-%s", r); set_label_text_changed(t_remain, rr);
        }
    }
    return 1;
}
/* release: commit the seek if one was active. returns 1 if it consumed the gesture */
int ui_np_seek_release(int x, int y){
    (void)x; (void)y;
    int consumed = 0;
    /* If the track changed while the finger was down, the drag was computed against the OLD track's
     * window - applying it to the NEW track would scramble its position and cancel its resume. Compare
     * against a FRESH player-state read (g_np_curpath is only a per-tick UI snapshot and can lag the
     * real track). Drop the seek on a mismatch. */
    if(g_seek_on && g_scrub_path[0]){
        track_state_t s; ipc_get_state(&s);
        if(strcmp(g_scrub_path, s.path) != 0){
            g_seek_cand = 0; g_seek_on = 0; g_scrubbing = 0;
            seek_show(shown_progress);   /* snap the arc back to the real position */
            return 1;                                 /* consumed the gesture, but issued NO seek */
        }
    }
    if(g_seek_on && g_track_dur > 0){
        int32_t v = lv_arc_get_value(ring);
        long lo = g_scrub_lo, hi = g_scrub_hi;   /* the window latched at drag start */
        if(hi <= lo){ lo = 0; hi = g_track_dur; }   /* fall back to the whole track */
        long ms = (hi > lo) ? lo + (long)((int64_t)v * (hi - lo) / 1000) : 0;   /* latched window-relative v -> absolute ms */
        g_seek_target_ms = ms;
        g_seek_hold_until = lv_tick_get() + 2500;   /* suppress stale echo ~2.5s */
        /* only hand control away from a pending book-resume if the seek actually goes out; a failed
         * send must leave the resume pending so the saved position isn't lost. */
        if(ui_seek_to(ms) == 0) ui_book_user_seeked(ms);
        consumed = 1;
    }
    g_seek_cand = 0; g_seek_on = 0; g_scrubbing = 0;
    return consumed;
}

static void set_progress_changed(int32_t value)
{
    if(g_scrubbing) return;   /* the finger owns the arc while scrubbing */
    if(value < 0) value = 0;
    if(value > 1000) value = 1000;

    if(ring && shown_progress != value) {
        seek_show(value);
        shown_progress = value;
    }
}

static void style_text(lv_obj_t *obj, const lv_font_t *font, lv_color_t color)
{
    lv_obj_set_style_text_font(obj, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(obj, color, LV_PART_MAIN);
    lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(obj, 0, LV_PART_MAIN);
}

void ui_pp_glyph(lv_obj_t *label,int playing){
    if(label) set_label_text_changed(label,playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
}
int ui_transport_command(const char *cmd)
{
    if(!cmd) return -1;
    int is_next = !strcmp(cmd, "0201000C0001");
    int is_prev = !strcmp(cmd, "0201000C0002");
    int is_toggle = !strcmp(cmd, "0201000C0000");
    if(!is_next && !is_prev && !is_toggle) return -1;
    if(ma_controls()){ ma_send(is_toggle ? "toggle" : is_next ? "next" : "previous"); return 0; }   /* MA Sendspin */
    if(modes_output_busy()){ ui_toast("Switching output - try again"); return -1; }
    if(!ui_local_playback_allowed()){
        ui_toast("Return to local playback first"); return -1;
    }
    ui_defer_sleep();
    if(is_next || is_prev){
        track_state_t st; ipc_get_state(&st);
        if(mdb_is_book_path(st.path)){
            long tgt = st.position_ms + (is_next ? 30000 : -15000);
            if(tgt < 0) tgt = 0;
            if(st.duration_ms > 0 && tgt > st.duration_ms) tgt = st.duration_ms;
            if(ui_seek_to(tgt) < 0){ ui_toast("Couldn't seek - try again"); return -1; }
            g_seek_target_ms = tgt;
            g_seek_hold_until = lv_tick_get() + 2500;
            ui_book_user_seeked(tgt);
            return 0;
        }
    }
    if(is_next){
        int q=queue_next();
        if(q<0){ ui_toast("Couldn't start queued song - try again"); return -1; }
        if(q>0){ ui_cancel_book_resume(); ui_disarm_book_eoc(); return 0; }
    }
    ui_note_transport_sent();
    if(ipc_send_cmd(cmd) < 0){ ui_toast("Player busy - try again"); return -1; }
    if(is_toggle){
        ui_pp_tap_hint();
        ui_pp_glyph(btn_pp, ui_pp_icon_playing(ui_is_playing()));
    } else {
        if(is_prev) queue_note_prev();
        ui_cancel_book_resume();
        ui_disarm_book_eoc();
    }
    return 0;
}
static void transport_cb(lv_event_t *e)
{
    ui_transport_command((const char *)lv_event_get_user_data(e));
}

/* The cover opens Song Info on a TAP, but it is also where a horizontal
 * slide-to-the-side (-> Now Playing hub) often starts.  A clickable-but-not-
 * scrollable object still fires CLICKED after a slide, which used to open Song
 * Info instead of letting the swipe reach the hub.  So only treat it as a tap
 * when the finger barely moved; a slide is ignored here and handled by the
 * main-loop swipe gesture (-> SCR_NPHUB). */
static int g_cover_px, g_cover_py;
/* ---- full-screen album art (stock cover.png reuse) ---------------------------------------- */
static char fsart_path[256];
static void fsart_reload_img(void){
    if(!fsart_img) return;
    lv_image_set_src(fsart_img, NULL);                 /* cache off -> force a fresh decode */
    /* Use the stock player's cover.JPG (364px sharp) - LVGL's JPEG decoder (TJPGD) is enabled;
     * PNG (lodepng) is NOT, so cover.png can't be loaded. Fall back to our own BMP art. */
    if(access("/usr/data/fiio/cover.jpg", 0) == 0)
        lv_image_set_src(fsart_img, "A:/usr/data/fiio/cover.jpg");
    else if(backdrop_valid)
        lv_image_set_src(fsart_img, backdrop_src);     /* fallback: our own blurred backdrop (BMP) */
    else if(cover_valid)
        lv_image_set_src(fsart_img, cover_src);        /* last resort: the 148px cover (BMP) */
}
/* The poster background is decoded ONCE per track into RAM. As a file source, LVGL (image cache off)
 * re-decoded the JPEG behind every small repaint - the once-a-second ring/time update cost ~40x the
 * Cover style's. From RAM it is a plain blit. */
#define PSZ 364
static uint8_t g_posterbuf[PSZ * PSZ * 3];
static int g_sharp_valid;
static lv_image_dsc_t g_posterdsc;
/* Decode any image the decoders can stream (JPEG blocks, BMP rows) into an XRGB8888 buffer, once. With the
 * image cache off, a file-sourced picture is re-read from its file under every small repaint. */
static int img_to_xrgb(const char *src, uint8_t *dst, int maxw, int maxh, int *ow, int *oh){
    lv_image_decoder_dsc_t d; memset(&d, 0, sizeof d);
    if(lv_image_decoder_open(&d, src, NULL) != LV_RESULT_OK) return -1;
    int w = d.header.w, h = d.header.h, ok = 0;
    if(w <= 0 || h <= 0 || w > maxw || h > maxh){ lv_image_decoder_close(&d); return -1; }
    lv_area_t full = { 0, 0, w - 1, h - 1 }, ar = { LV_COORD_MIN, LV_COORD_MIN, LV_COORD_MIN, LV_COORD_MIN };
    for(int guard = 0; guard < 100000; guard++){
        if(lv_image_decoder_get_area(&d, &full, &ar) != LV_RESULT_OK) break;
        const lv_draw_buf_t *db = d.decoded;
        if(!db || !db->data) break;
        int bw = lv_area_get_width(&ar), bh = lv_area_get_height(&ar), cf = db->header.cf;
        if(ar.x1 < 0 || ar.x1 + bw > w) break;
        for(int y = 0; y < bh; y++){
            int dy = ar.y1 + y; if(dy < 0 || dy >= h) continue;
            const uint8_t *sp = (const uint8_t *)db->data + (size_t)y * db->header.stride;
            uint8_t *dp = dst + ((size_t)dy * w + ar.x1) * 4;
            for(int x = 0; x < bw; x++, dp += 4){
                if(cf == LV_COLOR_FORMAT_RGB888){ dp[0] = sp[0]; dp[1] = sp[1]; dp[2] = sp[2]; sp += 3; }
                else if(cf == LV_COLOR_FORMAT_RGB565){ uint16_t c = (uint16_t)(sp[0] | (sp[1] << 8)); sp += 2;
                    dp[0] = (uint8_t)((c & 0x1F) << 3); dp[1] = (uint8_t)(((c >> 5) & 0x3F) << 2); dp[2] = (uint8_t)((c >> 11) << 3); }
                else { dp[0] = sp[0]; dp[1] = sp[1]; dp[2] = sp[2]; sp += 4; }      /* XRGB/ARGB8888 */
                dp[3] = 0xFF;
            }
        }
        if(ar.y2 >= h - 1 && ar.x2 >= w - 1){ ok = 1; break; }
    }
    lv_image_decoder_close(&d);
    if(!ok) return -1;
    *ow = w; *oh = h; return 0;
}
#define BDW 360
static uint8_t g_bdbuf[BDW * BDW * 4];
static lv_image_dsc_t g_bddsc;
static int g_bd_ok;
static void backdrop_to_ram(void){
    g_bd_ok = 0;
    if(!backdrop_valid) return;
    int w, h;
    if(img_to_xrgb(backdrop_src, g_bdbuf, BDW, BDW, &w, &h) != 0) return;
    memset(&g_bddsc, 0, sizeof g_bddsc);
    g_bddsc.header.magic = LV_IMAGE_HEADER_MAGIC; g_bddsc.header.cf = LV_COLOR_FORMAT_XRGB8888;
    g_bddsc.header.w = w; g_bddsc.header.h = h; g_bddsc.header.stride = w * 4;
    g_bddsc.data = g_bdbuf; g_bddsc.data_size = (uint32_t)(w * h * 4);
    g_bd_ok = 1;
}
const void *ui_current_backdrop_img(void){
    if(g_bd_ok) return &g_bddsc;
    return backdrop_valid ? (const void *)backdrop_src : NULL;
}
static uint8_t g_posterx[PSZ * PSZ * 4];               /* poster: cover + its static shading, XRGB8888 */
static lv_image_dsc_t g_posterxdsc;
static int g_poster_baked;
static int g_poster_stale = 1;           /* cover.jpg not decoded for this track yet */
/* Every overlay on the Poster that never moves - the 22% dim, the top whisper, the soft band behind the
 * title and the heavy bottom fade - is drawn into the picture once per track, in the display's native
 * pixel format. Live, those were five extra blend layers under every repaint (seek ring, times, a button
 * press) plus an RGB888->XRGB conversion; baked, a repaint is one straight copy. */
static float grad_a(float y, float y0, float h, float a_top, float a_bot, float stop0, float stop1){
    float t = (y - y0) / h; if(t < 0 || t > 1) return 0;
    float s0 = stop0 / 255.0f, s1 = stop1 / 255.0f;
    float k = t <= s0 ? 0 : t >= s1 ? 1 : (t - s0) / (s1 - s0);
    return (a_top + (a_bot - a_top) * k) / 255.0f;
}
static void poster_bake(int w, int h){
    g_poster_baked = 0;
    if(w <= 0 || h <= 0) return;
    int ox = (w - 360) / 2, oy = (h - 360) / 2;             /* the image is centred on the 360 panel */
    for(int y = 0; y < h; y++){
        float sy = (float)(y - oy);                         /* screen row */
        float keep = 1.0f - 56 / 255.0f;                    /* poster_dim */
        keep *= 1.0f - grad_a(sy, 0, 90, 130, 0, 0, 255);                            /* poster_top */
        if(sy < 203) keep *= 1.0f - grad_a(sy, 128, 75, 0, 105, 0, 255);            /* band, upper (seam row belongs to the lower half) */
        keep *= 1.0f - grad_a(sy, 203, 75, 105, 0, 0, 255);                          /* band, lower */
        keep *= 1.0f - (sy >= 200 ? grad_a(sy, 200, 160, 0, 245, 0, 150) : 0);       /* bottom fade */
        if(sy > 200 + 160 * 150 / 255.0f) keep = (1.0f - 56 / 255.0f) * (1.0f - 245 / 255.0f);
        int m = (int)(keep * 256 + 0.5f);
        const uint8_t *src = g_posterbuf + (size_t)y * w * 3; uint8_t *dst = g_posterx + (size_t)y * w * 4;
        for(int x = 0; x < w; x++){ dst[0] = (uint8_t)((src[0] * m) >> 8); dst[1] = (uint8_t)((src[1] * m) >> 8);
                                    dst[2] = (uint8_t)((src[2] * m) >> 8); dst[3] = 0xFF; src += 3; dst += 4; }
    }
    (void)ox;
    memset(&g_posterxdsc, 0, sizeof g_posterxdsc);
    g_posterxdsc.header.magic = LV_IMAGE_HEADER_MAGIC; g_posterxdsc.header.cf = LV_COLOR_FORMAT_XRGB8888;
    g_posterxdsc.header.w = w; g_posterxdsc.header.h = h; g_posterxdsc.header.stride = w * 4;
    g_posterxdsc.data = g_posterx; g_posterxdsc.data_size = (uint32_t)(w * h * 4);
    g_poster_baked = 1;
}
static void poster_shading_live(int on){                    /* the live layers, only when nothing is baked */
    SHOW(poster_dim, on); SHOW(poster_top, on); SHOW(poster_fade, on); SHOW(np_scrim, on);
}
static uint8_t g_spinbuf[PSZ * PSZ * 4];               /* immersive: the same cover as XRGB8888 */
static lv_image_dsc_t g_spindsc;
static int g_spin_ok;
static uint32_t *imm_frame;
static lv_image_dsc_t imm_frame_dsc;
static int imm_fast;
static uint32_t imm_frame_period=41;
static int imm_fade_height = 170;
static int imm_playing;                                   /* defined below (tentative declaration) */
/* fork perf: how long LVGL takes to compose and send a whole frame to the display (render + flush), averaged x8.
 * The record's own drawing is only part of a frame; pacing on the whole frame lets it settle on a rate the
 * device can hold every time instead of bursts and stalls. */
static uint32_t imm_refr_avg8, imm_refr_t0;
static void imm_refr_cb(lv_event_t *e){
    if(!fsart_on) return;
    if(lv_event_get_code(e) == LV_EVENT_REFR_START) imm_refr_t0 = lv_tick_get();
    else if(imm_refr_t0){ uint32_t d = lv_tick_elaps(imm_refr_t0); imm_refr_avg8 = imm_refr_avg8 ? imm_refr_avg8 - imm_refr_avg8 / 8 + d : d * 8; }
}
static void imm_render_frame(void){
    if(!imm_fast || !g_spin_ok) return;
    static int hooked;
    if(!hooked && lv_display_get_default()){
        lv_display_add_event_cb(lv_display_get_default(), imm_refr_cb, LV_EVENT_REFR_START, NULL);
        lv_display_add_event_cb(lv_display_get_default(), imm_refr_cb, LV_EVENT_REFR_READY, NULL);
        hooked = 1;
    }
    uint32_t started=lv_tick_get();
    imm_record_render((const uint32_t *)g_spinbuf,g_spindsc.header.w,g_spindsc.header.h,imm_frame,imm_angle,imm_fade_height);
    uint32_t cost=lv_tick_elaps(started);
    /* Pace on running averages (x8 fixed point), never on one slow frame: a single hiccup must not lock the
     * record at 1 fps. Smoothing goes off only once the drawing average stays high. Rotation remains time-based. */
    static uint32_t avg8;
    avg8=avg8?avg8-avg8/8+cost:cost*8;
    uint32_t avg=avg8/8, frame=avg+imm_refr_avg8/8;      /* drawing + composing/sending the frame */
    if(avg>15)imm_record_set_smoothing(0);
    uint32_t period=frame+frame/4;                       /* a quarter of headroom for touch, lyrics and the player */
    if(period<41)period=41;
    if(period>120)period=120;                            /* never slower than ~8 fps: it must visibly turn */
    if(period!=imm_frame_period){
        imm_frame_period=period;
        if(imm_timer && imm_playing)lv_timer_set_period(imm_timer,imm_frame_period);
    }
    lv_obj_invalidate(imm_spin);
}
/* fork perf: in the fast renderer the lyric labels, the hint and the progress arc are baked into the frame
 * (imm_record.c) instead of being re-drawn by LVGL on every frame. The LVGL objects stay (layout, text) but are
 * made invisible; their pixels come from snapshots taken only when they change. */
static lv_color_t np_col(void);
static lv_draw_buf_t *imm_snap[4];
static void imm_overlay_mode(int baked){
    lv_opa_t op = baked ? LV_OPA_TRANSP : LV_OPA_COVER;
    if(imm_prog) lv_obj_set_style_opa(imm_prog, op, 0);
    if(imm_hint) lv_obj_set_style_opa(imm_hint, op, 0);
    for(int j = 0; j < 3; j++) if(imm_lyr[j]) lv_obj_set_style_opa(imm_lyr[j], op, 0);
    if(!baked){ imm_record_set_overlays(NULL, 0); imm_record_set_ring(0, 0, 0, 0); }
}
static void imm_overlays_refresh(void){
    if(!imm_fast) return;
    imm_overlay_t ov[4]; int n = 0;
    for(int k = 0; k < 4; k++){ if(imm_snap[k]){ lv_draw_buf_destroy(imm_snap[k]); imm_snap[k] = NULL; } }
    lv_obj_t *src[4] = { imm_lyr[0], imm_lyr[1], imm_lyr[2], imm_hint };
    for(int k = 0; k < 4; k++){
        lv_obj_t *o = src[k];
        if(!o || lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) continue;
        const char *t = lv_label_get_text(o); if(!t || !t[0]) continue;
        lv_opa_t fo = fsart ? lv_obj_get_style_opa(fsart, 0) : LV_OPA_COVER;   /* the overlay may still be fading in: */
        if(fsart) lv_obj_set_style_opa(fsart, LV_OPA_COVER, 0);                  /* the snapshot must be full strength */
        lv_obj_set_style_opa(o, LV_OPA_COVER, 0);
        lv_obj_update_layout(o);
        lv_draw_buf_t *b = lv_snapshot_take(o, LV_COLOR_FORMAT_ARGB8888);
        lv_obj_set_style_opa(o, LV_OPA_TRANSP, 0);
        if(fsart) lv_obj_set_style_opa(fsart, fo, 0);
        if(!b) continue;
        lv_area_t a; lv_obj_get_coords(o, &a);
        int ext = lv_obj_get_ext_draw_size(o); a.x1 -= ext; a.y1 -= ext;      /* the snapshot includes the draw margin */
        imm_snap[k] = b;
        ov[n].px = (const uint32_t *)b->data; ov[n].x = a.x1; ov[n].y = a.y1;
        ov[n].w = b->header.w; ov[n].h = b->header.h; ov[n].stride = (int)(b->header.stride / 4); n++;
    }
    imm_record_set_overlays(ov, n);
}
static void imm_ring_update(int permille){
    if(!imm_fast) return;
    uint32_t ind = lv_color_to_u32(np_col()) & 0xFFFFFFu;
    imm_record_set_ring(1, permille, theme_rgb(THEME_CLR_FIXED_MEDIA_TRACK) & 0xFFFFFFu, ind);
}
static void imm_bind_record(void){
    imm_fast=0;
    if(!imm_spin || !g_spin_ok) return;
    if(!imm_frame) imm_frame=calloc(360*360,sizeof *imm_frame);
    if(imm_frame){
        imm_frame_dsc=(lv_image_dsc_t){0};
        imm_frame_dsc.header.magic=LV_IMAGE_HEADER_MAGIC;
        imm_frame_dsc.header.cf=LV_COLOR_FORMAT_XRGB8888;
        imm_frame_dsc.header.w=imm_frame_dsc.header.h=360;
        imm_frame_dsc.header.stride=360*4;
        imm_frame_dsc.data=(const uint8_t *)imm_frame;
        imm_frame_dsc.data_size=360*360*4;
        imm_fast=1;imm_overlay_mode(1);imm_overlays_refresh();
        { track_state_t st0; ipc_get_state(&st0); imm_ring_update(st0.duration_ms > 0 ? (int)((long long)st0.position_ms * 1000 / st0.duration_ms) : 0); }
        imm_render_frame();
        lv_image_set_src(imm_spin,&imm_frame_dsc);
        lv_image_set_rotation(imm_spin,0);lv_image_set_scale(imm_spin,256);
        lv_image_set_antialias(imm_spin,false);lv_obj_center(imm_spin);
    }else{ /* Allocation failure keeps the existing generic renderer available. */
        lv_image_set_src(imm_spin,&g_spindsc);
        lv_image_set_rotation(imm_spin,imm_angle);lv_image_set_scale(imm_spin,256);
        lv_obj_center(imm_spin);
    }
    if(imm_scrim){if(imm_fast)lv_obj_add_flag(imm_scrim,LV_OBJ_FLAG_HIDDEN);else lv_obj_remove_flag(imm_scrim,LV_OBJ_FLAG_HIDDEN);}
}
/* Disco Options > Immersive: 0 Vinyl (grooves + label), 1 CD (clear hub, still rainbow sheen + spokes). Disco only. */
static int imm_cd_mode(void){ return th_disco() && cfg_get_int("disco_imm", 0) == 1; }
static void spin_from_poster(void);
void ui_imm_style_apply(void){                             /* re-bake the record (the cover itself is unchanged) */
    if(!g_posterdsc.data) return;
    spin_from_poster();
    if(imm_fast) imm_render_frame();
}
static void spin_from_poster(void){                       /* RGB888 -> the display's native XRGB8888, once */
    g_spin_ok = 0;
    int w = g_posterdsc.header.w, h = g_posterdsc.header.h;
    if(!g_posterdsc.data || w <= 0 || h <= 0) return;
    const uint8_t *s = g_posterbuf; uint8_t *d = g_spinbuf;
    for(int i = 0; i < w * h; i++){ d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = 0xFF; s += 3; d += 4; }
    memset(&g_spindsc, 0, sizeof g_spindsc);
    g_spindsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    g_spindsc.header.cf = LV_COLOR_FORMAT_XRGB8888;
    g_spindsc.header.w = w; g_spindsc.header.h = h; g_spindsc.header.stride = w * 4;
    g_spindsc.data = g_spinbuf; g_spindsc.data_size = (uint32_t)(w * h * 4);
    /* Bake the record's grooves and centre label into the picture. They are perfectly round, so they look
     * the same spinning; drawn as live objects they cost ~30% of every frame (anti-aliased full-size circle
     * masks), baked they cost nothing per frame. */
    static const float groove[4] = { 170.f, 148.f, 126.f, 104.f };
    const float cx = w / 2.0f - 0.5f, cy = h / 2.0f - 0.5f;
    int cdm = imm_cd_mode();
    imm_record_set_cd(cdm);                                  /* Disco Options > Immersive: CD (the still sheen + spokes) */
    if(cdm){                                                 /* the CD's clear hub instead of the label: round, so baked */
        for(int y = 0; y < h; y++){
            float dy = y - cy;
            for(int x = 0; x < w; x++){
                float dx = x - cx, r = sqrtf(dx * dx + dy * dy);
                if(r > 38.5f) continue;
                uint8_t *p = g_spinbuf + ((size_t)y * w + x) * 4;
                int wa = 0, dark = 0;                               /* white over the cover, or the dark hole */
                if(r < 12.5f){ dark = r < 11.5f ? 255 : (int)(255 * (12.5f - r)); if(r >= 11.5f) wa = 70; }
                else {
                    wa = 46;                                        /* clear plastic */
                    float d1 = fabsf(r - 22.5f), d2 = fabsf(r - 37.0f);   /* the stacking ring, the hub's edge */
                    if(d1 < 1.6f) wa = 46 + (int)(44 * (1.6f - d1) / 1.6f);
                    if(d2 < 1.4f) wa = 46 + (int)(64 * (1.4f - d2) / 1.4f);
                    if(r > 37.0f) wa = (int)(wa * (38.5f - r) / 1.5f);
                }
                for(int c = 0; c < 3; c++){
                    int v = p[c] + (255 - p[c]) * wa / 255;
                    v = v * (255 - dark) / 255 + 0x0C * dark / 255;
                    p[c] = (uint8_t)v;
                }
            }
        }
        g_spin_ok = 1;
        return;
    }
    for(int y = 0; y < h; y++){
        float dy = y - cy;
        for(int x = 0; x < w; x++){
            float dx = x - cx, r = sqrtf(dx * dx + dy * dy);
            uint8_t *p = g_spinbuf + ((size_t)y * w + x) * 4;
            if(r < 4.0f){ uint32_t hole=theme_rgb(THEME_CLR_FIXED_MEDIA_SPINDLE); p[0]=hole>>16; p[1]=hole>>8; p[2]=hole; continue; }            /* spindle hole */
            if(r < 27.0f){ float k = r > 26.0f ? (27.0f - r) : 1.0f;                   /* dark label, soft edge */
                           int a = (int)(235 * k); for(int c = 0; c < 3; c++) p[c] = (uint8_t)((p[c] * (255 - a) + 0x0D * a) / 255); continue; }
            for(int g = 0; g < 4; g++){ float d = fabsf(r - groove[g]);
                if(d < 1.0f){ int a = (int)(40 * (1.0f - d)); for(int c = 0; c < 3; c++) p[c] = (uint8_t)(p[c] * (255 - a) / 255); } }
        }
    }
    g_spin_ok = 1;
}
static int poster_decode_ram(const char *src){
    lv_image_decoder_dsc_t d; memset(&d, 0, sizeof d);
    if(lv_image_decoder_open(&d, src, NULL) != LV_RESULT_OK) return -1;
    int w = d.header.w, h = d.header.h;
    if(d.header.cf != LV_COLOR_FORMAT_RGB888 || w <= 0 || h <= 0 || w > PSZ || h > PSZ){ lv_image_decoder_close(&d); return -1; }
    lv_area_t full = { 0, 0, w - 1, h - 1 }, ar = { LV_COORD_MIN, LV_COORD_MIN, LV_COORD_MIN, LV_COORD_MIN };
    int ok = 0;
    for(int guard = 0; guard < 40000; guard++){                     /* one MCU block per call */
        if(lv_image_decoder_get_area(&d, &full, &ar) != LV_RESULT_OK) break;
        const lv_draw_buf_t *db = d.decoded;
        if(!db || !db->data) break;
        int bw = lv_area_get_width(&ar), bh = lv_area_get_height(&ar);
        if(ar.x1 < 0 || ar.x1 + bw > w) break;
        for(int y = 0; y < bh; y++){
            int dy = ar.y1 + y; if(dy < 0 || dy >= h) continue;
            memcpy(g_posterbuf + ((size_t)dy * w + ar.x1) * 3, (const uint8_t *)db->data + (size_t)y * db->header.stride, (size_t)bw * 3);
        }
        if(ar.y2 >= h - 1 && ar.x2 >= w - 1){ ok = 1; break; }
    }
    lv_image_decoder_close(&d);
    if(!ok) return -1;
    memset(&g_posterdsc, 0, sizeof g_posterdsc);
    g_posterdsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    g_posterdsc.header.cf = LV_COLOR_FORMAT_RGB888;
    g_posterdsc.header.w = w; g_posterdsc.header.h = h; g_posterdsc.header.stride = w * 3;
    g_posterdsc.data = g_posterbuf; g_posterdsc.data_size = (uint32_t)(w * h * 3);
    return 0;
}
static void poster_reload(void){
    if(!poster_img) return;
    g_poster_stale = 0;
    lv_image_set_src(poster_img, NULL);
    g_spin_ok = 0; g_poster_baked = 0;
    if(g_sharp_valid){
        spin_from_poster();                              /* the immersive record uses the sharp cover too */
        poster_bake(g_posterdsc.header.w, g_posterdsc.header.h);
        lv_image_set_src(poster_img, g_poster_baked ? (const void *)&g_posterxdsc : (const void *)&g_posterdsc);
    }
    else if(backdrop_valid)
        lv_image_set_src(poster_img, backdrop_src);     /* fallback: our own backdrop BMP */
    if(g_np_poster) poster_shading_live(!g_poster_baked);
}
static void fsart_refresh_text(void){
    /* immersive: like Home - the title in the album colour, the artist under it, centred under the arch */
    if(fsart_title) lv_label_set_text(fsart_title, "");     /* immersive: cover + lyrics only, no title/artist */
    if(fsart_artist) lv_label_set_text(fsart_artist, "");
}
/* ---- immersive: spinning cover + synced lyrics ---- */
static void imm_lyrics_reload(void){
    static unsigned revision;
    unsigned now;
    int n=lyrics_immersive_load(g_lyr,(int)(sizeof g_lyr/sizeof g_lyr[0]),&now);
    if(revision==now && g_lyr_n>=0) return;
    revision=now; g_lyr_n=n>0 ? n : 0;
    copy_cstr(g_lyr_path,sizeof g_lyr_path,g_np_curpath);
    if(imm_hint){ if(g_lyr_n>0) lv_obj_add_flag(imm_hint,LV_OBJ_FLAG_HIDDEN); else lv_obj_remove_flag(imm_hint,LV_OBJ_FLAG_HIDDEN); }
    for(int i=0;i<3;i++) if(imm_lyr[i]) lv_label_set_text(imm_lyr[i],"");
    imm_overlays_refresh();                                    /* fork perf: re-bake the hint / cleared lines */
}
static uint32_t imm_last_spin, imm_last_lyr, imm_el_avg, imm_spin_ms;
static int imm_playing = 1;
static int g_np_is_playing = 1;   /* the NORMALISED play state from ui_update (raw ipc state reads 0 while playing) */
static int imm_frames;
static void imm_tick(lv_timer_t *t){
    (void)t;
    /* Time-based: the angle follows the clock (one turn per 20 s), so the record turns at the right
     * speed whatever frame rate the renderer manages - the 41 ms timer asks for 24 fps, and a busy
     * frame just makes the next step a little bigger instead of slowing the record down. */
    /* fork perf: screen dimmed (saver) or off - nobody sees the record, so draw nothing and poll slowly; it picks up
     * at the right angle when the screen wakes (the angle is time-based) */
    { int ui_main_is_idle(void); static int was_idle;
      if(ui_main_is_idle()){ imm_last_spin = lv_tick_get(); if(!was_idle){ was_idle = 1; if(imm_timer) lv_timer_set_period(imm_timer, 500); } return; }
      if(was_idle){ was_idle = 0; if(imm_timer) lv_timer_set_period(imm_timer, imm_playing ? imm_frame_period : 250); } }
    uint32_t el = lv_tick_elaps(imm_last_spin);
    if(!imm_playing) imm_last_spin = lv_tick_get();      /* paused: the record stands still (and costs nothing) */
    else if(imm_spin && !lv_obj_has_flag(imm_spin, LV_OBJ_FLAG_HIDDEN) && el >= 38){
        imm_last_spin = lv_tick_get();
        imm_spin_ms = (imm_spin_ms + el % 20000) % 20000;
        imm_angle = (int32_t)(imm_spin_ms * 3600 / 20000);
        if(imm_fast) imm_render_frame(); else lv_image_set_rotation(imm_spin, imm_angle);
        /* Smoothing (bilinear) doubles the cost of a frame. Keep it while the device holds ~18 fps or
         * better (~18 fps); if the average interval stays above 55 ms, drop it once - the same frame then costs
         * about what the old 148px spin did, and the record keeps turning smoothly. */
        imm_el_avg = imm_el_avg ? (imm_el_avg * 7) / 8 + el : el * 8;              /* x8 fixed-point EMA */
        if(!imm_fast && g_spin_ok && lv_image_get_antialias(imm_spin) && ++imm_frames > 24 && imm_el_avg / 8 > 55)
            lv_image_set_antialias(imm_spin, false);
    }
    if(lv_tick_elaps(imm_last_lyr) < 250) return;                        /* lyrics + progress ~4x/s */
    imm_last_lyr = lv_tick_get();
    track_state_t st; ipc_get_state(&st);
    int pl = g_np_is_playing;                             /* never the raw ipc state: it reports 0 while playing */
    if(pl != imm_playing){ imm_playing = pl; if(imm_timer) lv_timer_set_period(imm_timer, pl ? imm_frame_period : 250); }
    imm_lyrics_reload();
    if(imm_prog && st.duration_ms > 0) lv_arc_set_value(imm_prog, (int32_t)((long long)st.position_ms * 1000 / st.duration_ms));
    if(st.duration_ms > 0) imm_ring_update((int)((long long)st.position_ms * 1000 / st.duration_ms));   /* fork perf: baked ring */
    if(g_lyr_n <= 0) return;
    int cur = -1;
    for(int i = 0; i < g_lyr_n && g_lyr[i].ms <= st.position_ms + 200; i++) cur = i;   /* +200: show a hair early */
    int changed = 0;
    for(int j = 0; j < 3; j++){
        int i = cur - 1 + j;
        const char *t = (i >= 0 && i < g_lyr_n) ? g_lyr[i].text : "";
        if(strcmp(lv_label_get_text(imm_lyr[j]), t)){ lv_label_set_text(imm_lyr[j], t); changed = 1; }
    }
    if(changed){                                   /* next at the bottom, then current, then previous */
        int y = 336;
        for(int j = 2; j >= 0; j--){
            lv_obj_update_layout(imm_lyr[j]);
            int h = lv_obj_get_height(imm_lyr[j]);
            y -= h; lv_obj_align(imm_lyr[j], LV_ALIGN_TOP_MID, 0, y); y -= (j == 2 ? 6 : 4);
        }
        int need = 360 - y + 72;
        imm_fade_height = need > 170 ? need : 170;
        if(imm_scrim) lv_obj_set_height(imm_scrim, imm_fade_height);
        if(imm_fast){ imm_overlays_refresh(); imm_render_frame(); }   /* fork perf: re-bake the changed lines */
    }
}
void ui_np_fsart_open(void){
    if(!fsart || fsart_on || !g_np_have) return;       /* nothing to show if no track */
    lv_anim_delete(fsart, NULL); /* reopening must cancel the previous close callback */
    imm_fast = 0;imm_frame_period=41;imm_record_set_smoothing(1);imm_overlay_mode(0);
    if(imm_scrim) lv_obj_remove_flag(imm_scrim,LV_OBJ_FLAG_HIDDEN);
    fsart_path[0] = '\0';
    fsart_reload_img();
    fsart_refresh_text();
    lv_obj_set_style_opa(fsart, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(fsart, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(fsart);
    if(th_disco()) lv_obj_set_style_opa(fsart, LV_OPA_COVER, 0);
    else anim_fade(fsart, LV_OPA_TRANSP, LV_OPA_COVER, 240, NULL);
    fsart_on = 1;
    if(g_np_poster || g_np_ring){                       /* Ring and Braun share this immersive path */
        if(g_poster_stale) poster_reload();               /* decode the sharp cover now, not on every track */
        if(imm_spin){
            if(g_spin_ok){                              /* sharp 364px cover, native format, 1:1 - best quality */
                lv_image_set_src(imm_spin, &g_spindsc);
                lv_image_set_pivot(imm_spin, g_spindsc.header.w / 2, g_spindsc.header.h / 2);
                lv_image_set_scale(imm_spin, 256); lv_image_set_antialias(imm_spin, true);
                lv_obj_center(imm_spin);
                lv_obj_remove_flag(imm_spin, LV_OBJ_FLAG_HIDDEN);
                for(int k = 0; k < 6; k++) if(imm_deco[k]) lv_obj_add_flag(imm_deco[k], LV_OBJ_FLAG_HIDDEN);   /* baked in */
                if(fsart_img) lv_obj_add_flag(fsart_img, LV_OBJ_FLAG_HIDDEN);
                imm_bind_record(); }
            else if(coverdsc_valid){
                for(int k = 0; k < 6; k++) if(imm_deco[k]) lv_obj_remove_flag(imm_deco[k], LV_OBJ_FLAG_HIDDEN);                    /* fallback: the 148px cover, upscaled */
                lv_image_set_src(imm_spin, &g_coverdsc);
                lv_image_set_pivot(imm_spin, CBMP / 2, CBMP / 2);
                lv_image_set_scale(imm_spin, (uint32_t)(364 * 256 / CBMP)); lv_image_set_antialias(imm_spin, false);
                lv_obj_center(imm_spin);
                lv_obj_remove_flag(imm_spin, LV_OBJ_FLAG_HIDDEN);
                if(fsart_img) lv_obj_add_flag(fsart_img, LV_OBJ_FLAG_HIDDEN); }
            else { lv_obj_add_flag(imm_spin, LV_OBJ_FLAG_HIDDEN); if(fsart_img) lv_obj_remove_flag(fsart_img, LV_OBJ_FLAG_HIDDEN); }
        }
        g_lyr_n = -1; imm_lyrics_reload();
        imm_last_spin = imm_last_lyr = lv_tick_get(); imm_el_avg = 0; imm_frames = 0;
        imm_spin_ms=(uint32_t)imm_angle*20000/3600;
        imm_playing = g_np_is_playing;
        if(!imm_timer) imm_timer = lv_timer_create(imm_tick, imm_frame_period, NULL);   /* initial render already measured its cost */
    }
}
static void fsart_hidden_cb(lv_anim_t *a){ (void)a; if(fsart) lv_obj_add_flag(fsart, LV_OBJ_FLAG_HIDDEN); }
void ui_np_fsart_close(void){
    if(!fsart || !fsart_on) return;
    fsart_on = 0;
    if(imm_timer){ lv_timer_delete(imm_timer); imm_timer = NULL; }
    if(th_disco() && screen_current() == SCR_NOWPLAYING){
        /* The shared NP root uses Ring's layout. Keep it covered throughout
         * the 240ms return slide; hiding/fading first exposes that layout. */
        lv_obj_set_style_opa(fsart, LV_OPA_COVER, 0);
        screen_home();
        anim_fade(fsart, LV_OPA_COVER, LV_OPA_COVER, 300, fsart_hidden_cb);
    } else anim_fade(fsart, LV_OPA_COVER, LV_OPA_TRANSP, 200, fsart_hidden_cb);
}
int ui_np_fsart_active(void){ return fsart_on; }
static void fsart_click_cb(lv_event_t *e){
    /* LVGL is the sole closer. The opening tap's CLICKED targets the cover (the press
     * target), not this overlay, so raising fsart mid-click can't retarget it here -
     * no timing guard needed. main.c only swallows the raw gesture; it never mutates fsart. */
    if(lv_event_get_code(e)==LV_EVENT_CLICKED) ui_np_fsart_close();
}

static void imm_btn_cb(lv_event_t *e){ (void)e; ui_np_fsart_open(); }
static void np_mid_cb(lv_event_t *e){              /* Poster: the title opens this song's album in the Library */
    (void)e;
    track_state_t st; ipc_get_state(&st);
    if(!st.have_track){ screen_show(SCR_LIBRARY); return; }
    if(st.album[0])       library_open_album_focus(st.album, st.artist, g_np_curpath);
    else if(st.artist[0]) library_open_artist(st.artist);
    screen_show(SCR_LIBRARY);
}

void ui_np_open_album(void){                       /* Disco title popup: Album */
    track_state_t st; ipc_get_state(&st);
    if(!st.have_track || !st.album[0]){ ui_toast("No album tag"); return; }
    library_open_album_focus(st.album, st.artist, g_np_curpath);
    screen_show(SCR_LIBRARY);
}
void ui_np_open_artist(void){                      /* Disco title popup: Artist, this song's album focused */
    track_state_t st; ipc_get_state(&st);
    if(!st.have_track || !st.artist[0]){ ui_toast("No artist tag"); return; }
    library_open_artist_focus(st.artist, st.album);
    screen_show(SCR_LIBRARY);
}
int ui_np_cover_hit(int x, int y){   /* main.c: a press on the (visible) cover is a cover tap, not a seek */
    if(!cover || lv_obj_has_flag(cover, LV_OBJ_FLAG_HIDDEN)) return 0;
    lv_area_t a; lv_obj_get_coords(cover, &a);
    return x >= a.x1 && x <= a.x2 && y >= a.y1 && y <= a.y2;
}
static void cover_click_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_CLICKED && g_np_ring){ ui_np_fsart_open(); return; }   /* Ring: artwork -> immersive */
    lv_indev_t *id = lv_indev_active();
    if(code == LV_EVENT_PRESSED) {
        if(id){ lv_point_t p; lv_indev_get_point(id, &p); g_cover_px = p.x; g_cover_py = p.y; }
    }
    /* Cover-tap no longer opens full-screen art (removed per product decision - the
     * vinyl screensaver is the album-art showcase; full-screen art stays available via
     * Options -> Full-screen Art). The PRESSED tracking above + main.c's cover-tap
     * seek-skip remain so a tap on the cover can't accidentally seek. */
}
static void make_clickable(lv_obj_t *o, const char *cmd)
{
    lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(o, 18);
    char name[40]; ui_on(o,transport_cb,LV_EVENT_CLICKED,(void *)cmd,ui_transport_action(cmd,"np",name,sizeof name),UI_CORE);
}

/* The Poster style paints its controls in the ALBUM's colour even when the global accent is fixed.
 * Dull picks (near-black / grey) fall back to the global accent so controls never go muddy. */
static int np_rgb_vivid(int rgb){
    int r = (rgb >> 16) & 255, g = (rgb >> 8) & 255, b = rgb & 255;
    int mx = r > g ? (r > b ? r : b) : (g > b ? g : b), mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
    return mx >= 90 && (mx - mn) >= 48;
}
static void np_accent_from_track(const char *path){
    if(!path) path = "";
    if(!strcmp(np_acc_path, path)) return;           /* once per track: this is a DB lookup */
    copy_cstr(np_acc_path, sizeof np_acc_path, path);
    int rgb = path[0] ? mdb_song_accent(path) : 0;
    np_acc_set = (rgb && np_rgb_vivid(rgb));
    if(np_acc_set) np_acc = theme_color_from_rgb(rgb);
}
int ui_accent_is_static(void);   /* fork: a colour picked in Accent wins over the album colour everywhere */
static lv_color_t np_col(void){ if(th_braun()) return TC(ACCENT_PRIMARY); return ((g_np_poster || g_np_ring) && np_acc_set && !ui_accent_is_static()) ? np_acc : accent; }
lv_color_t ui_media_accent(void){ if(th_braun()) return TC(ACCENT_PRIMARY); return (np_acc_set && !ui_accent_is_static()) ? np_acc : accent; }   /* theme.h: media surfaces (Braun: orange) */
static void apply_accent(void)
{
    if(ring) {
        lv_obj_set_style_arc_color(ring, np_col(), LV_PART_INDICATOR);
    }
    if(vol_arc) lv_obj_set_style_arc_color(vol_arc, np_col(), LV_PART_INDICATOR);
    if(imm_prog) lv_obj_set_style_arc_color(imm_prog, np_col(), LV_PART_INDICATOR);

    if(cover_note) {
        lv_obj_set_style_text_color(cover_note, TC(FIXED_MEDIA_WHITE), LV_PART_MAIN);
    }

    if(btn_pp && g_pp_filled){
        lv_obj_set_style_bg_color(btn_pp, np_col(), 0);
        lv_obj_set_style_text_color(btn_pp, theme_on_color(np_col()), 0);
    } else if(btn_pp) {
        lv_obj_set_style_text_color(btn_pp, th_braun() ? TC(ON_ACCENT) : np_col(), LV_PART_MAIN);   /* Braun: white on the orange button */
    }

    if(fav_icon && g_np_fav) {
        lv_obj_set_style_text_color(fav_icon, np_col(), LV_PART_MAIN);
    }

    if(mode_icon && g_np_mode > 0) {
        lv_obj_set_style_text_color(mode_icon, np_col(), LV_PART_MAIN);
    }

    if(spindle) {
        lv_obj_set_style_bg_color(spindle, accent, LV_PART_MAIN);
    }

    /* keep the other accent-bearing surfaces in lockstep so no screen shows a stale
     * (hardcoded-pink) accent: Home's now-playing capsule + the saver decorations. */
    home_set_accent(accent);
    quicksettings_refresh(ui_is_playing());
    saver_set_accent(accent);
    kit_accent_changed();
}

/* the live accent (user-picked static, or the album-derived dynamic colour). Other
 * modules paint with this instead of a hardcoded constant. */
/* Disco: an accent that reads on the cover - a too-dark one is lifted on the dark theme, a too-pale one darkened on the
 * light theme (hue kept; only the brightness moves, in a few small steps toward white / black) */
lv_color_t ui_disco_fit_accent(lv_color_t c){
    if(!th_disco()) return c;
    int light = theme_variant();
    for(int k = 0; k < 12; k++){
        int l = (c.red * 77 + c.green * 150 + c.blue * 29) >> 8;
        if(light ? l <= 120 : l >= 150) break;
        int f = light ? 0 : 255;                                       /* 1/6 of the way toward black / white */
        c = theme_color_from_rgb((uint32_t)((c.red + (f - c.red) / 6) << 16 | (c.green + (f - c.green) / 6) << 8 | (c.blue + (f - c.blue) / 6)));
    }
    return c;
}
lv_color_t ui_current_accent(void){ return th_braun() ? TC(ACCENT_PRIMARY) : ui_disco_fit_accent(accent); }   /* Braun: orange everywhere */

/* ---- shared standard header (back chevron + centred title) ------------------------------------- */
static void ui_header_home_cb(lv_event_t *e){ if(lv_event_get_code(e)==LV_EVENT_CLICKED && screen_press_was_long()) screen_home(); }
static void ui_header_back_cb(lv_event_t *e){ if(lv_event_get_code(e)==LV_EVENT_CLICKED) screen_back(); }
/* full form: custom back handler (e.g. Library pops its view stack before leaving the screen). */
static void header_back_raise(void *o){ lv_obj_move_foreground((lv_obj_t *)o); }

static void header_back_deleted(lv_event_t *e){ lv_async_call_cancel(header_back_raise, lv_event_get_target(e)); }

int ui_round_width(int y, int margin){
    int dy = y < 180 ? 180 - y : y - 180;
    if(dy >= 180) return 0;
    int w = 2 * (int)sqrtf((float)(180 * 180 - dy * dy)) - 2 * margin;
    return w > 0 ? w : 0;
}

static void title_fit(lv_obj_t *t, const char *text, const lv_font_t *f, int cy, int pad_top,
                      const lv_font_t *(*next)(const lv_font_t *)){
    for(int k = 0; k < 3; k++){
        int h = lv_font_get_line_height(f) + pad_top, w = ui_round_width(cy - h / 2 + pad_top + 2, 10);
        if(w > 250) w = 250;
        lv_point_t sz; lv_text_get_size(&sz, text, f, lv_obj_get_style_text_letter_space(t, 0), 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        const lv_font_t *sm = next ? next(f) : NULL;
        if(sz.x <= w || !sm || k == 2){
            lv_obj_set_style_text_font(t, f, 0);
            lv_obj_set_user_data(t, (void *)&theme_title_tag);   /* a title to the theme kit, whatever face it fits in */
            lv_obj_set_pos(t, 180 - w / 2, cy - h / 2); lv_obj_set_size(t, w, h);
            lv_obj_set_style_pad_top(t, pad_top, 0);
            return;
        }
        f = sm;
    }
}

void ui_back_glyph(lv_obj_t *label){   /* the edge Back's face: the theme's text, else the arrow icon */
    const char *t = theme_kit()->back_text;
    lv_label_set_text(label, t ? t : "\xEF\x81\xA0");     /* f060 arrow-left */
    lv_obj_set_style_text_font(label, t ? TF(UI_20) : TF(ICON_20), 0);
    lv_obj_set_style_text_color(label, TC(TEXT_PRIMARY), 0);
}

int ui_edge_nav(void){   /* 1 = this theme puts Back on the screen's left edge (screens with their own headers follow) */
    return theme_kit()->header != theme_kit_default.header || theme_trait(THEME_TRAIT_DOT_TITLES);
}

lv_obj_t *kit_edge_header(lv_obj_t *root, const char *title, lv_event_cb_t back_cb)
{
    lv_obj_t *back = lv_button_create(root);
    lv_obj_remove_style_all(back);
    /* 30 px wide with no extra reach: it stops where the screen's rows and sliders start (x 31), so a tap on a row's
     * left edge never goes Back; 80 px tall keeps the target easy to hit along the rim */
    lv_obj_set_pos(back, 0, 140); lv_obj_set_size(back, 30, 80);
    lv_obj_set_style_radius(back, 20, 0);
    lv_obj_set_style_bg_color(back, TC(LIST_PRESSED), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(back, LV_OPA_70, LV_STATE_PRESSED);
    ui_on(back, back_cb ? back_cb : ui_header_back_cb, LV_EVENT_CLICKED, NULL, "nav.back", UI_CORE);
    lv_obj_add_event_cb(back, ui_header_home_cb, LV_EVENT_CLICKED, NULL);
    lv_async_call(header_back_raise, back);
    lv_obj_add_event_cb(back, header_back_deleted, LV_EVENT_DELETE, NULL);
    lv_obj_t *ic = lv_label_create(back);
    ui_back_glyph(ic);
    lv_obj_align(ic, LV_ALIGN_CENTER, 0, -2);
    lv_obj_t *t = lv_label_create(root);
    lv_obj_set_style_text_color(t, TC(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
    theme_title_text(t, title);                       /* the theme's form first: that is the text that must fit */
    char shown[160]; lv_snprintf(shown, sizeof shown, "%s", lv_label_get_text(t));
    /* the box is as wide as the glass at the title's height (never past the round edge); a long title steps down
     * to the theme's smaller faces; the height follows the face, so its caps never clip */
    title_fit(t, shown, TF(HEADER), 46, 0, theme_title_smaller);
    theme_title_text(t, title);                       /* laid out again at the final size */
    return t;
}

lv_obj_t *kit_default_header(lv_obj_t *root, const char *title, lv_event_cb_t back_cb)
{
    lv_obj_t *back = lv_button_create(root);
    lv_obj_remove_style_all(back);
    lv_obj_set_pos(back, 72, 24); lv_obj_set_size(back, 44, 40);   /* inset from the clipped corner */
    lv_obj_set_ext_click_area(back, 10);                            /* easier near the round bezel */
    lv_obj_set_style_radius(back, 20, 0);
    lv_obj_set_style_bg_color(back, TC(SURFACE), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(back, LV_OPA_70, LV_STATE_PRESSED);
    ui_on(back, back_cb ? back_cb : ui_header_back_cb, LV_EVENT_CLICKED, NULL, "nav.back", UI_CORE);
    lv_obj_add_event_cb(back, ui_header_home_cb, LV_EVENT_CLICKED, NULL);   /* runs AFTER the screen's own back: a long press then goes Home (covers back arrows that step up inside a screen, e.g. Library, Files) */
    lv_obj_t *ic = lv_label_create(back);
    lv_label_set_text(ic, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_font(ic, TF(UI_20), 0);
    lv_obj_set_style_text_color(ic, TC(TEXT_LYRICS), 0);
    lv_obj_align(ic, LV_ALIGN_CENTER, 0, -2);

    lv_obj_t *t = lv_label_create(root);
    lv_obj_set_user_data(t, (void *)&theme_title_tag);
    theme_title_text(t, title);
    /* Screen-centred (label centre = 104+76 = 180) but bounded to a zone that clears the back chevron on
     * both sides, so a long album/playlist name truncates with an ellipsis instead of sliding under it. */
    lv_obj_set_pos(t, 104, 30); lv_obj_set_size(t, 152, 26);
    lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(t, ui_font_cjk(18), 0);   /* headers show folder/album/playlist names: chain (issue #3) */
    lv_obj_set_style_text_color(t, TC(TEXT_PRIMARY), 0);
    if(th_braun()){                                           /* Braun: dark title, grey arrow, on the grille */
        lv_obj_set_style_text_color(ic, TC(TEXT_SECONDARY), 0);
        lv_obj_set_style_bg_color(back, TC(SURFACE), LV_STATE_PRESSED);
        lv_obj_set_style_text_color(t, TC(TEXT_PRIMARY), 0); lv_obj_set_style_text_font(t, br_font(18, 1), 0);
    }
    return t;
}
lv_obj_t *ui_header_cb(lv_obj_t *root, const char *title, lv_event_cb_t back_cb){
    return theme_kit()->header(root, title, back_cb);
}
lv_obj_t *ui_header(lv_obj_t *root, const char *title){ return ui_header_cb(root, title, NULL); }

/* Favorites heart on Now Playing: filled+accent when the current track is a
 * favorite, outline+grey otherwise. Tap toggles it (movement-guarded so a swipe
 * doesn't accidentally favorite). The 0104 command is sent via ui_set_favorite. */
static void fav_refresh(int on, int have)
{
    g_np_fav = on; g_np_have = have;
    if(!btn_fav || !fav_icon) return;
    if(have) lv_obj_remove_flag(btn_fav, LV_OBJ_FLAG_HIDDEN);
    else     lv_obj_add_flag(btn_fav, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(fav_icon, HEART_FILLED);                     /* theme: solid, colour carries the state */
    lv_obj_set_style_text_color(fav_icon, on ? np_col() : th_braun() ? TC(TEXT_SECONDARY) : (g_np_poster || g_np_ring) ? TC(TEXT_PRIMARY) : TC(TEXT_MUTED), LV_PART_MAIN);
}

static void fav_click_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *id = lv_indev_active();
    if(code == LV_EVENT_PRESSED) {
        if(id) { lv_point_t p; lv_indev_get_point(id, &p); g_fav_px = p.x; g_fav_py = p.y; }
    } else if(code == LV_EVENT_CLICKED) {
        if(!g_np_have) return;
        if(id) {
            lv_point_t p; lv_indev_get_point(id, &p);
            int dx = p.x - g_fav_px, dy = p.y - g_fav_py;
            if((dx<0?-dx:dx) > 18 || (dy<0?-dy:dy) > 18) return;   /* was a swipe (cst816t wobble tolerance) */
        }
        fav_refresh(!g_np_fav, 1);      /* g_np_fav now holds the NEW (toggled) value */
        ui_set_favorite(g_np_fav);
        /* hold this value for the current track until the player confirms via a2 */
        g_favp_active = 1; g_favp_val = g_np_fav; g_favp_set = lv_tick_get();
        snprintf(g_favp_path, sizeof g_favp_path, "%s", g_np_curpath);
        ui_toast(g_np_fav ? "Added to Favourites" : "Removed from Favourites");
    }
}

/* Play-mode toggle on Now Playing: cycles work_mode 0..4 via 0102 (ui_set_workmode).
 * Stock order (ground-truth captured 2026-06-25): 0=Sequential, 1=Shuffle,
 * 2=Repeat One, 3=Repeat All, 4=Single (play one, stop). Icons (existing glyphs):
 *   0 arrow         (grey/inactive base)
 *   1 shuffle
 *   2 loop + "1"
 *   3 loop
 *   4 arrow + "1"   (single = play-one)
 * The "1" overlay (mode_one) is shown for Repeat One and Single. */
#define WORKMODE_COUNT 5
static void mode_refresh(int wm)
{
    if(!btn_mode || !mode_icon) return;
    if(wm == g_np_mode) return;
    g_np_mode = wm;
    const char *icon = (wm == 1) ? LV_SYMBOL_SHUFFLE
                     : (wm == 2 || wm == 3) ? LV_SYMBOL_LOOP
                     : MODE_ARROW;                       /* 0 and 4 use the arrow */
    lv_label_set_text(mode_icon, icon);
    lv_obj_set_style_text_color(mode_icon, wm ? (th_braun() ? TC(ACCENT_PRIMARY) : accent) : th_braun() ? TC(TEXT_SECONDARY) : TC(TEXT_MUTED), LV_PART_MAIN);
    if(wm == 2 || wm == 4) lv_obj_remove_flag(mode_one, LV_OBJ_FLAG_HIDDEN);
    else                   lv_obj_add_flag(mode_one, LV_OBJ_FLAG_HIDDEN);
}

static void mode_click_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *id = lv_indev_active();
    if(code == LV_EVENT_PRESSED) {
        if(id) { lv_point_t p; lv_indev_get_point(id, &p); g_mode_px = p.x; g_mode_py = p.y; }
    } else if(code == LV_EVENT_CLICKED) {
        if(id) {
            lv_point_t p; lv_indev_get_point(id, &p);
            int dx = p.x - g_mode_px, dy = p.y - g_mode_py;
            if((dx<0?-dx:dx) > 18 || (dy<0?-dy:dy) > 18) return;   /* was a swipe (cst816t wobble tolerance) */
        }
        int wm = (cfg_get_int("work_mode", 0) + 1) % WORKMODE_COUNT;
        cfg_set_int("work_mode", wm);
        ui_set_workmode(wm);
        mode_refresh(wm);
        static const char *const MODE_NAMES[WORKMODE_COUNT] =
            { "Sequential", "Shuffle", "Repeat One", "Repeat All", "Single" };
        if(wm >= 0 && wm < WORKMODE_COUNT) ui_toast(MODE_NAMES[wm]);
    }
}

static int g_np_vinyl = 0;
static int g_spinning  = 0;

static void spin_cb(void *var, int32_t v){ lv_image_set_rotation((lv_obj_t *)var, v % 3600); }

/* Start/stop the vinyl spin. Idempotent - main.c calls this every loop with the
 * live condition (vinyl style && playing && Now Playing visible && screen on),
 * so the disc only spins when you're actually watching it. On pause it FREEZES
 * at the current angle (no reset) and resumes from there. */
void ui_vinyl_spin(int want)
{
    want = want && g_np_vinyl && cover_img;
    if(want == g_spinning) return;
    g_spinning = want;
    if(want){
        int32_t cur = lv_image_get_rotation(cover_img);   /* resume from here */
        lv_anim_t a; lv_anim_init(&a);
        lv_anim_set_var(&a, cover_img);
        lv_anim_set_exec_cb(&a, spin_cb);
        lv_anim_set_values(&a, cur, cur + 3600);   /* +one turn; spin_cb wraps %3600 */
        lv_anim_set_time(&a, 9000);                /* ~9s/rev - relaxed */
        lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
        lv_anim_start(&a);
    } else {
        lv_anim_delete(cover_img, spin_cb);        /* freeze at current angle */
    }
}

/* Now Playing style: 0 = album cover (rounded square), 1 = vinyl disc. */
static void np_layout(int poster);
static void np_layout_ring(int on);
void kit_fork_np_braun(void);
static lv_timer_t *g_settle_t;
static void np_settle_cb(lv_timer_t *t){                  /* after 15 s: stop scrolling, end with "..." */
    (void)t;
    if(title && lv_label_get_long_mode(title) == LV_LABEL_LONG_SCROLL_CIRCULAR) lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    if(artist && lv_label_get_long_mode(artist) == LV_LABEL_LONG_SCROLL_CIRCULAR) lv_label_set_long_mode(artist, LV_LABEL_LONG_DOT);
    lv_timer_pause(g_settle_t);
}
/* A long title scrolls when the track changes or Now Playing appears, then settles: an endless scroll
 * redraws its strip ~30x/s for as long as the screen is on. */
static void queue_click_cb(lv_event_t *e){ (void)e; queue_open(); }
static void queue_long_cb(lv_event_t *e){ (void)e; if(cfg_get_int("up_next",0)) screen_show(SCR_UPNEXT); else queue_open(); }
static void np_queue_place(void){ if(btn_queue && btn_mode) lv_obj_align_to(btn_queue, btn_mode, LV_ALIGN_OUT_BOTTOM_MID, 0, 0); }
void ui_queue_changed(void){                                  /* the badge: the queue's size, hidden when empty */
    if(!queue_badge) return;
    int n = queue_count();
    if(n <= 0){ lv_obj_add_flag(queue_badge, LV_OBJ_FLAG_HIDDEN); return; }
    char b[8]; if(n > 99) snprintf(b, sizeof b, "99"); else snprintf(b, sizeof b, "%d", n);
    if(strcmp(lv_label_get_text(queue_badge_lbl), b)) lv_label_set_text(queue_badge_lbl, b);
    lv_obj_set_style_bg_color(queue_badge, ui_current_accent(), 0);
    lv_obj_remove_flag(queue_badge, LV_OBJ_FLAG_HIDDEN);
}
void ui_np_tags_changed(void){ lyrics_invalidate_current(); g_lyr_n = -1; g_lyr_path[0] = 0; }   /* immersive re-reads the new lyrics */
void ui_np_rescroll(void){
    ui_queue_changed();                                       /* Now Playing shown: the queue's count */
    if(!title || g_np_poster) return;                     /* the Poster wraps its title instead */
    lv_label_set_long_mode(title, LV_LABEL_LONG_SCROLL_CIRCULAR);
    if(g_np_ring && artist) lv_label_set_long_mode(artist, LV_LABEL_LONG_SCROLL_CIRCULAR);
    if(!g_settle_t) g_settle_t = lv_timer_create(np_settle_cb, 15000, NULL);
    else { lv_timer_reset(g_settle_t); lv_timer_resume(g_settle_t); }
}
void ui_set_np_style(int style)
{
    if(!cover) return;
    if(th_ringlike())style=3;
    int vinyl = (style == 1);
    g_np_poster = (style == 2);
    g_np_ring = (style == 3);
    g_np_vinyl = vinyl;
    lv_obj_set_style_radius(cover, vinyl ? (COVER_D/2) : 14, LV_PART_MAIN);
    if(spindle) {
        if(vinyl) lv_obj_remove_flag(spindle, LV_OBJ_FLAG_HIDDEN);
        else      lv_obj_add_flag(spindle, LV_OBJ_FLAG_HIDDEN);
    }
    if(!vinyl){
        ui_vinyl_spin(0);                                  /* stop the spin */
        if(cover_img) lv_image_set_rotation(cover_img, 0); /* Cover must sit upright */
    }
    if(th_braun()){ g_np_poster = 0; g_np_ring = 1; g_np_vinyl = 0; }   /* Braun: its own layout, built on Ring's structure */
    np_layout(g_np_poster);
    np_layout_ring(g_np_ring);
    if(theme_kit()->fork_np_layout) theme_kit()->fork_np_layout();
    np_queue_place();
    apply_accent();
    ui_np_rescroll();
}
/* Poster: full-bleed sharp cover, big title in the middle, heart + play-mode on the right, immersive on
 * the left, glyph-only transport, seek ring on the top rim, volume on the bottom rim, a heavy fade over the
 * bottom third. Cover/Vinyl: the original layout, restored exactly. */
/* Ring (np_style 3): a round cover with the progress ring hugging it (drag round the ring to seek), the
 * heart at the ring's left apex and play mode at its right apex, title and artist below (scrolling when
 * long; tap to open the album with this song focused), a large play/pause in the album colour with the
 * times beside Previous / Next. Tapping the artwork opens immersive mode. Runs after np_layout(0), which
 * has already restored the classic layout, so only the differences are set here. */
#define RING_Y   124                      /* ring centre */
#define RING_RR  84                       /* ring radius */
static void np_layout_ring(int on){
    if(!on) return;
    SHOW(cover, 1); SHOW(album, 0); SHOW(btn_imm, 0); SHOW(vol_arc, 0); SHOW(np_mid_btn, 1);
    if(cover){ lv_obj_set_style_radius(cover, COVER_D / 2, LV_PART_MAIN);
               lv_obj_align(cover, LV_ALIGN_TOP_MID, 0, RING_Y - COVER_D / 2); }
    if(spindle) lv_obj_add_flag(spindle, LV_OBJ_FLAG_HIDDEN);
    if(ring){
        lv_obj_set_size(ring, 2 * RING_RR, 2 * RING_RR);
        lv_obj_align(ring, LV_ALIGN_TOP_MID, 0, RING_Y - RING_RR);
        lv_arc_set_mode(ring, LV_ARC_MODE_NORMAL);
        lv_arc_set_rotation(ring, 270); lv_arc_set_bg_angles(ring, 0, 360);          /* from 12 o'clock */
        lv_obj_set_style_arc_width(ring, TH_ARC_IND, LV_PART_MAIN);
        lv_obj_set_style_arc_width(ring, TH_ARC_IND, LV_PART_INDICATOR);
    }
    g_arc_rot = 270; g_arc_sweep = 360; g_arc_rev = 0;
    g_ring_cx = 180; g_ring_cy = RING_Y; g_ring_r = RING_RR; g_tol_grab = 18; g_tol_wander = 32;  /* clear of the title */
    if(btn_fav)   lv_obj_align(btn_fav,   LV_ALIGN_TOP_MID, -(RING_RR + 34), RING_Y - 22);   /* left apex */
    if(btn_sleep) lv_obj_align(btn_sleep, LV_ALIGN_TOP_MID, -(RING_RR + 34), RING_Y - 22);
    if(btn_mode)  lv_obj_align(btn_mode,  LV_ALIGN_TOP_MID,  (RING_RR + 34), RING_Y - 22);   /* right apex */
    if(title){
        lv_obj_set_style_text_font(title, &s_font28, 0);                   /* fork: as large as Braun's title */
        lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(title, TC(TEXT_PRIMARY), 0);
        lv_obj_set_size(title, 272, LV_SIZE_CONTENT);
        lv_label_set_long_mode(title, LV_LABEL_LONG_SCROLL_CIRCULAR);     /* scrolls only when too long */
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 218);
    }
    if(artist){
        lv_obj_set_style_text_font(artist, &s_font20, 0);
        lv_obj_set_style_text_color(artist, TC(TEXT_SECONDARY), 0);
        lv_obj_set_width(artist, 272);
        lv_obj_set_style_text_align(artist, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(artist, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_align(artist, LV_ALIGN_TOP_MID, 0, 254);
    }
    if(np_mid_btn){ lv_obj_set_size(np_mid_btn, 272, 64); lv_obj_align(np_mid_btn, LV_ALIGN_TOP_MID, 0, 214); }
    lv_color_t w = TC(TEXT_PRIMARY);
    if(btn_prev){ lv_obj_set_style_text_color(btn_prev, w, 0); lv_obj_align(btn_prev, LV_ALIGN_TOP_MID, -64, 290); }
    if(btn_next){ lv_obj_set_style_text_color(btn_next, w, 0); lv_obj_align(btn_next, LV_ALIGN_TOP_MID,  64, 290); }
    if(btn_pp){ lv_obj_set_style_text_font(btn_pp, TF(UI_36), 0); lv_obj_align(btn_pp, LV_ALIGN_TOP_MID, 0, 286); }
    lv_color_t tc = TC(TEXT_SECONDARY);
    if(t_elapsed){ lv_obj_set_style_text_font(t_elapsed, TH_F_CAPTION, 0); lv_obj_set_style_text_color(t_elapsed, tc, 0);
                   lv_obj_align(t_elapsed, LV_ALIGN_TOP_MID, -108, 294); }
    if(t_remain){  lv_obj_set_style_text_font(t_remain, TH_F_CAPTION, 0); lv_obj_set_style_text_color(t_remain, tc, 0);
                   lv_obj_align(t_remain, LV_ALIGN_TOP_MID, 108, 294); }
    SHOW(t_sep, 0);
}
/* Braun Now Playing: the grille face, the cover on its own panel disc inside an orange progress ring, and
 * the lower segment holding the title, artist, times and controls (Previous / Next as flat discs, play in
 * orange). Everything else keeps its Ring position; colours follow the Braun palette. */
void kit_fork_np_braun(void){
    if(!th_braun() || !g_np_scr) return;
    if(!g_br_seg){
        br_face(g_np_scr);
        if(backdrop) lv_obj_add_flag(backdrop, LV_OBJ_FLAG_HIDDEN);
        g_br_disc = br_disc(g_np_scr, 180, RING_Y, RING_RR + 8, BR_PANEL);
        g_br_seg = br_segment(g_np_scr, 214);
        lv_obj_move_to_index(g_br_disc, 1); lv_obj_move_to_index(g_br_seg, 2);   /* above the grille, below everything else */
    }
    if(ring){ lv_obj_set_style_arc_color(ring, TC(SURFACE), LV_PART_MAIN); lv_obj_set_style_arc_width(ring, 5, LV_PART_MAIN);
              lv_obj_set_style_arc_width(ring, 5, LV_PART_INDICATOR); }
    if(title){ lv_obj_set_style_text_font(title, br_font(24, 1), 0); lv_obj_set_style_text_color(title, TC(TEXT_PRIMARY), 0);
               lv_obj_set_width(title, 260); lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 216); }
    if(artist){ lv_obj_set_style_text_font(artist, br_font(20, 0), 0); lv_obj_set_style_text_color(artist, TC(TEXT_SECONDARY), 0);
                lv_obj_set_width(artist, 240); lv_obj_align(artist, LV_ALIGN_TOP_MID, 0, 247); }
    if(np_mid_btn){ lv_obj_set_size(np_mid_btn, 260, 64); lv_obj_align(np_mid_btn, LV_ALIGN_TOP_MID, 0, 212); }
    lv_obj_t *side[2] = { btn_prev, btn_next };
    for(int i = 0; i < 2; i++) if(side[i]){
        lv_obj_set_style_text_color(side[i], TC(TEXT_PRIMARY), 0); lv_obj_set_style_text_font(side[i], TF(UI_16), 0);
        lv_obj_set_style_bg_color(side[i], TC(SURFACE), 0); lv_obj_set_style_bg_opa(side[i], LV_OPA_COVER, 0);
        lv_obj_set_style_radius(side[i], LV_RADIUS_CIRCLE, 0); lv_obj_set_style_pad_all(side[i], 8, 0);
        lv_obj_align(side[i], LV_ALIGN_TOP_MID, i ? 54 : -54, 284);
    }
    if(btn_pp){ lv_obj_set_style_text_font(btn_pp, TF(UI_24), 0); lv_obj_set_style_text_color(btn_pp, TC(ON_ACCENT), 0);
                lv_obj_set_style_bg_color(btn_pp, TC(ACCENT_PRIMARY), 0); lv_obj_set_style_bg_opa(btn_pp, LV_OPA_COVER, 0);
                lv_obj_set_style_radius(btn_pp, LV_RADIUS_CIRCLE, 0); lv_obj_set_style_pad_all(btn_pp, 0, 0);
                lv_obj_set_size(btn_pp, 50, 50); lv_obj_set_style_text_align(btn_pp, LV_TEXT_ALIGN_CENTER, 0);
                lv_obj_set_style_pad_top(btn_pp, (50 - lv_font_get_line_height(TF(UI_24))) / 2, 0);   /* a round button, glyph centred */
                lv_obj_align(btn_pp, LV_ALIGN_TOP_MID, 0, 276); }
    lv_color_t tc = TC(TEXT_SECONDARY);
    if(t_elapsed){ lv_obj_set_style_text_font(t_elapsed, br_font(12, 0), 0); lv_obj_set_style_text_color(t_elapsed, tc, 0); lv_obj_align(t_elapsed, LV_ALIGN_TOP_MID, -104, 294); }
    if(t_remain){  lv_obj_set_style_text_font(t_remain,  br_font(12, 0), 0); lv_obj_set_style_text_color(t_remain,  tc, 0); lv_obj_align(t_remain,  LV_ALIGN_TOP_MID,  104, 294); }
    for(int i = 0; i < 2; i++) if(np_dots[i]){ lv_obj_set_style_bg_color(np_dots[i], i == 0 ? TC(TEXT_PRIMARY) : TC(TEXT_DISABLED), 0); lv_obj_set_style_bg_opa(np_dots[i], LV_OPA_COVER, 0); }   /* dark page dots */
    if(btn_sleep){ lv_obj_t *sl = lv_obj_get_child(btn_sleep, 0); if(sl) lv_obj_set_style_text_color(sl, TC(TEXT_SECONDARY), 0); }
    if(fav_icon && !g_np_fav) lv_obj_set_style_text_color(fav_icon, TC(TEXT_SECONDARY), 0);
    if(mode_icon && g_np_mode == 0) lv_obj_set_style_text_color(mode_icon, TC(TEXT_SECONDARY), 0);
    if(btn_queue){ for(uint32_t k = 0; k < lv_obj_get_child_count(btn_queue); k++){ lv_obj_t *c = lv_obj_get_child(btn_queue, k);
                     if(lv_obj_check_type(c, &lv_label_class) && lv_obj_get_child_count(c) == 0 && k == 0) lv_obj_set_style_text_color(c, TC(TEXT_SECONDARY), 0); } }
}
static void np_layout(int poster){
    SHOW(poster_img, poster); poster_shading_live(poster && !g_poster_baked);
    SHOW(btn_imm, poster); SHOW(vol_arc, 0);   /* volume arc retired: the bottom rim is the seek bar */
    SHOW(cover, !poster); SHOW(album, !poster); SHOW(np_dots[0], !poster); SHOW(np_dots[1], !poster);
    if(backdrop){ if(poster) lv_obj_add_flag(backdrop, LV_OBJ_FLAG_HIDDEN); else if(backdrop_valid) lv_obj_remove_flag(backdrop, LV_OBJ_FLAG_HIDDEN); }
    g_arc_rot = poster ? 28 : ARC_ROT; g_arc_sweep = poster ? 124 : ARC_SWEEP; g_arc_rev = poster;
    g_ring_cx = 180; g_ring_cy = 180; g_ring_r = ARC_D / 2; g_tol_grab = 34; g_tol_wander = 48;
    if(ring){ lv_obj_set_size(ring, ARC_D, ARC_D); lv_obj_align(ring, LV_ALIGN_CENTER, 0, 0); }
    if(cover) lv_obj_align(cover, LV_ALIGN_TOP_MID, 0, 42);
    if(btn_pp) lv_obj_set_style_text_font(btn_pp, TF(UI_32), 0);
    if(artist){ lv_obj_set_style_text_font(artist, &s_font16, 0); lv_label_set_long_mode(artist, LV_LABEL_LONG_DOT); }
    if(ring){ lv_arc_set_bg_angles(ring, 0, g_arc_sweep); lv_arc_set_rotation(ring, g_arc_rot);
              lv_arc_set_mode(ring, poster ? LV_ARC_MODE_REVERSE : LV_ARC_MODE_NORMAL);   /* fills from the left end */
              lv_obj_set_style_arc_width(ring, poster ? 4 : 6, LV_PART_MAIN); lv_obj_set_style_arc_width(ring, poster ? 4 : 6, LV_PART_INDICATOR); }
    if(btn_fav)   lv_obj_align(btn_fav,   LV_ALIGN_TOP_MID, poster ? 142 : 118, poster ? 144 : 126);
    if(btn_sleep) lv_obj_align(btn_sleep, LV_ALIGN_TOP_MID, poster ? 142 : 118, poster ? 144 : 126);
    if(btn_mode)  lv_obj_align(btn_mode,  LV_ALIGN_TOP_MID, poster ? 140 : -118, poster ? 186 : 126);
    if(title){
        lv_obj_set_style_text_font(title, poster ? &s_font28 : &s_font20, 0);
        lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(title, poster ? 236 : 260);
        lv_obj_set_height(title, poster ? 70 : LV_SIZE_CONTENT);          /* two lines, then ... */
        lv_label_set_long_mode(title, poster ? LV_LABEL_LONG_DOT : LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, poster ? 146 : 198);
    }
    SHOW(title_sh, poster); SHOW(artist_sh, poster); SHOW(np_mid_btn, poster);
    if(title) lv_obj_set_style_text_color(title, TC(TEXT_PRIMARY), 0);
    if(title_sh){ lv_obj_set_style_text_font(title_sh, &s_font28, 0); lv_obj_set_size(title_sh, 236, 70);
                  lv_label_set_long_mode(title_sh, LV_LABEL_LONG_DOT); lv_obj_align(title_sh, LV_ALIGN_TOP_MID, 1, 148); }
    if(artist_sh){ lv_obj_set_style_text_font(artist_sh, &s_font16, 0); lv_obj_set_width(artist_sh, 220);
                   lv_label_set_long_mode(artist_sh, LV_LABEL_LONG_DOT); lv_obj_align(artist_sh, LV_ALIGN_TOP_MID, 1, 224); }
    if(artist){ lv_obj_set_width(artist, poster ? 220 : 238);
                lv_obj_set_style_text_color(artist, poster ? TC(POSTER_SUBTITLE) : TC(TEXT_LYRICS), 0);
                lv_obj_align(artist, LV_ALIGN_TOP_MID, 0, poster ? 222 : 226); }
    lv_color_t side = poster ? TC(POSTER_SIDE) : TC(TEXT_MUTED);
    if(btn_prev){ lv_obj_set_style_text_color(btn_prev, side, 0); lv_obj_align(btn_prev, LV_ALIGN_TOP_MID, poster ? -62 : -60, poster ? 272 : 284); }
    if(btn_next){ lv_obj_set_style_text_color(btn_next, side, 0); lv_obj_align(btn_next, LV_ALIGN_TOP_MID, poster ?  62 :  60, poster ? 272 : 284); }
    if(btn_pp)    lv_obj_align(btn_pp, LV_ALIGN_TOP_MID, 0, poster ? 268 : 280);
    lv_color_t tc = poster ? TC(TEXT_PRIMARY) : TC(TEXT_MUTED);
    /* poster: elapsed beside Previous and remaining beside Next, out towards the arc's two ends; no slash */
    /* y 252: the label's middle sits level with the arc's ends (180 + 170*sin(152 deg) ~ 260), clear of the stroke */
    if(t_elapsed) lv_obj_set_style_text_font(t_elapsed, poster ? TH_F_CAPTION : TF(UI_14), 0);
    if(t_remain)  lv_obj_set_style_text_font(t_remain,  poster ? TH_F_CAPTION : TF(UI_14), 0);
    if(t_elapsed){ lv_obj_set_style_text_color(t_elapsed, tc, 0); lv_obj_align(t_elapsed, LV_ALIGN_TOP_MID, poster ? -104 : -46, poster ? 252 : 318); }
    if(t_sep){     lv_obj_align(t_sep, LV_ALIGN_TOP_MID, 0, 318); SHOW(t_sep, !poster); }
    if(t_remain){  lv_obj_set_style_text_color(t_remain, tc, 0); lv_obj_align(t_remain, LV_ALIGN_TOP_MID, poster ? 104 : 46, poster ? 252 : 318); }
    if(poster) poster_reload();
}

int ui_np_cover_shown(void){ return cover_img && !lv_obj_has_flag(cover_img, LV_OBJ_FLAG_HIDDEN); }   /* host tests */
static void set_cover_fallback(bool show)
{
    if(show) {
        if(cover_img) lv_obj_add_flag(cover_img, LV_OBJ_FLAG_HIDDEN);
        if(cover_note) lv_obj_remove_flag(cover_note, LV_OBJ_FLAG_HIDDEN);
    } else {
        if(cover_img) lv_obj_remove_flag(cover_img, LV_OBJ_FLAG_HIDDEN);
        if(cover_note) lv_obj_add_flag(cover_note, LV_OBJ_FLAG_HIDDEN);
    }
}

/* Decode the 148px cover BMP into a RAM ARGB8888 descriptor. A file-sourced
 * lv_bmp image can't be rotated by the SW renderer (renders black) because the
 * image cache is off; a true-colour RAM buffer transforms fine, so the vinyl
 * can spin. The BMP is 24-bit BGR, bottom-up, 148*3=444 bytes/row (no padding). */
#ifndef CBMP
#define CBMP 148
#endif
static uint8_t g_coverbuf[CBMP*CBMP*4];
static lv_image_dsc_t g_coverdsc;
static int load_cover_dsc(const char *path)
{
    FILE *f = fopen(path, "rb"); if(!f) return -1;
    uint8_t hdr[54];
    if(fread(hdr,1,54,f)!=54){ fclose(f); return -1; }
    uint32_t off = hdr[10]|(hdr[11]<<8)|(hdr[12]<<16)|((uint32_t)hdr[13]<<24);
    int w = hdr[18]|(hdr[19]<<8), h = hdr[22]|(hdr[23]<<8);
    int bpp = hdr[28]|(hdr[29]<<8);
    uint32_t comp = hdr[30]|(hdr[31]<<8)|(hdr[32]<<16)|((uint32_t)hdr[33]<<24);
    if(w!=CBMP || h!=CBMP || bpp!=24 || comp!=0){ fclose(f); return -1; }
    static uint8_t row[CBMP*3];
    if(fseek(f, off, SEEK_SET)!=0){ fclose(f); return -1; }
    for(int yy=0; yy<CBMP; yy++){
        if(fread(row,1,CBMP*3,f)!=(size_t)(CBMP*3)){ fclose(f); return -1; }
        uint8_t *d = g_coverbuf + (CBMP-1-yy)*CBMP*4;   /* bottom-up -> top-down */
        for(int x=0;x<CBMP;x++){
            d[x*4+0]=row[x*3+0]; d[x*4+1]=row[x*3+1];   /* B, G */
            d[x*4+2]=row[x*3+2]; d[x*4+3]=0xFF;          /* R, A */
        }
    }
    fclose(f);
    g_coverdsc.header.magic  = LV_IMAGE_HEADER_MAGIC;
    g_coverdsc.header.cf     = LV_COLOR_FORMAT_ARGB8888;
    g_coverdsc.header.w      = CBMP;
    g_coverdsc.header.h      = CBMP;
    g_coverdsc.header.stride = CBMP*4;
    g_coverdsc.data          = g_coverbuf;
    g_coverdsc.data_size     = sizeof g_coverbuf;
    return 0;
}

/* The accent path (OKLab helpers + read_cover_bmp + accent_from_buf) is compiled
 * at -O0: at -Os this float-heavy code SIGSEGVs on the worker/prewarm thread
 * (heisenbug - adding any logging hid it; bounds/indices verified clean by two
 * static-analysis passes). -O0 makes the codegen deterministic. It runs off the UI thread
 * so the speed cost is irrelevant. */
#pragma GCC push_options
#pragma GCC optimize ("O0")

/* ---- Apple-grade album accent: OKLab k-means clustering ->
 * Vibrant-on-dark swatch scoring -> tone-map -> WCAG contrast-lift vs the dark UI
 * -> neutral fallbacks. Runs once per cover decode. */
static float g_lin_lut[256]; static int g_lin_lut_init = 0;
static void lin_lut_init(void){
    for(int i=0;i<256;i++){ float x=i/255.0f; g_lin_lut[i]= x<=0.04045f ? x/12.92f : powf((x+0.055f)/1.055f, 2.4f); }
    g_lin_lut_init=1;
}
static float a_clamp01(float x){ return x<0?0:(x>1?1:x); }
static float a_smooth(float a,float b,float x){ if(b<=a) return x>=b?1.0f:0.0f; float t=a_clamp01((x-a)/(b-a)); return t*t*(3.0f-2.0f*t); }
static void rgb2oklab(int R,int G,int B, float *L,float *a,float *bb){
    float r=g_lin_lut[R], g=g_lin_lut[G], bl=g_lin_lut[B];
    float l=0.4122214708f*r+0.5363325363f*g+0.0514459929f*bl;
    float m=0.2119034982f*r+0.6806995451f*g+0.1073969566f*bl;
    float s=0.0883024619f*r+0.2817188376f*g+0.6299787005f*bl;
    float l_=cbrtf(l), m_=cbrtf(m), s_=cbrtf(s);
    *L =0.2104542553f*l_+0.7936177850f*m_-0.0040720468f*s_;
    *a =1.9779984951f*l_-2.4285922050f*m_+0.4505937099f*s_;
    *bb=0.0259040371f*l_+0.7827717662f*m_-0.8086757660f*s_;
}
/* OKLab -> sRGB (Ottosson inverse). Returns 1 if in gamut; always fills R/G/B (clamped). */
static int oklab2rgb(float L,float a,float b, int *R,int *G,int *B){
    float l_=L+0.3963377774f*a+0.2158037573f*b;
    float m_=L-0.1055613458f*a-0.0638541728f*b;
    float s_=L-0.0894841775f*a-1.2914855480f*b;
    float l=l_*l_*l_, m=m_*m_*m_, s=s_*s_*s_;
    float lin[3];
    lin[0]= 4.0767416621f*l-3.3077115913f*m+0.2309699292f*s;
    lin[1]=-1.2684380046f*l+2.6097574011f*m-0.3413193965f*s;
    lin[2]=-0.0041960863f*l-0.7034186147f*m+1.7076147010f*s;
    int in = 1; int *o[3]={R,G,B};
    for(int i=0;i<3;i++){
        float x=lin[i];
        if(x<-0.001f||x>1.001f) in=0;
        if(x<0)x=0; if(x>1)x=1;
        float v = x<=0.0031308f ? 12.92f*x : 1.055f*powf(x,1.0f/2.4f)-0.055f;
        *o[i] = (int)(v*255.0f+0.5f);
    }
    return in;
}
static float wcag_lum(int R,int G,int B){ return 0.2126f*g_lin_lut[R]+0.7152f*g_lin_lut[G]+0.0722f*g_lin_lut[B]; }

/* Read a 148x148 BGR24 BMP (as written by art_make_all) into a caller-owned
 * CBMP*CBMP*4 BGRA buffer. Thread-safe (no globals) so the art worker / prewarm
 * thread can compute accents off the UI thread. Returns 0 on success. */
static int read_cover_bmp(const char *path, uint8_t *buf)
{
    FILE *f = fopen(path, "rb"); if(!f) return -1;
    uint8_t hdr[54];
    if(fread(hdr,1,54,f)!=54){ fclose(f); return -1; }
    uint32_t off = hdr[10]|(hdr[11]<<8)|(hdr[12]<<16)|((uint32_t)hdr[13]<<24);
    int w = hdr[18]|(hdr[19]<<8), h = hdr[22]|(hdr[23]<<8);
    int bpp = hdr[28]|(hdr[29]<<8);
    uint32_t comp = hdr[30]|(hdr[31]<<8)|(hdr[32]<<16)|((uint32_t)hdr[33]<<24);
    if(w!=CBMP || h!=CBMP || bpp!=24 || comp!=0){ fclose(f); return -1; }   /* only the tightly-packed 24bpp BMPs we write */
    uint8_t row[CBMP*3];
    if(fseek(f, off, SEEK_SET)!=0){ fclose(f); return -1; }
    for(int yy=0; yy<CBMP; yy++){
        if(fread(row,1,CBMP*3,f)!=(size_t)(CBMP*3)){ fclose(f); return -1; }
        uint8_t *d = buf + (CBMP-1-yy)*CBMP*4;       /* bottom-up -> top-down */
        for(int x=0;x<CBMP;x++){
            d[x*4+0]=row[x*3+0]; d[x*4+1]=row[x*3+1];
            d[x*4+2]=row[x*3+2]; d[x*4+3]=0xFF;
        }
    }
    fclose(f);
    return 0;
}

/* Core accent computation from a 148x148 BGRA buffer. Reentrant: heap scratch,
 * reads only the passed buffer + the read-only g_lin_lut (seeded once at startup),
 * so the UI thread, art worker, and prewarm thread can all call it concurrently.
 * Returns 1 and fills *out, or 0 if no usable colour. */
static int accent_from_buf(const uint8_t *cbuf, uint32_t *out_rgb)
{
    if(!cbuf) return 0;
    float *pL=malloc(2025*sizeof(float)), *pA=malloc(2025*sizeof(float)),
          *pB=malloc(2025*sizeof(float)), *pW=malloc(2025*sizeof(float));
    if(!pL||!pA||!pB||!pW){ free(pL);free(pA);free(pB);free(pW); return 0; }
    int rc = 0;
    int n=0; float wsum=0,csum=0;
    for(int y=8;y<140;y+=3){
        const uint8_t *rp=cbuf+(size_t)y*CBMP*4;
        for(int x=8;x<140 && n<2025;x+=3){
            const uint8_t *p=rp+x*4; int B=p[0],G=p[1],R=p[2];   /* B,G,R,A */
            float L,a,b; rgb2oklab(R,G,B,&L,&a,&b);
            float C=sqrtf(a*a+b*b), w=1.0f;
            if(C<0.025f||L<0.08f||L>0.96f) w=0.35f;             /* de-weight near grey/black/white */
            pL[n]=L;pA[n]=a;pB[n]=b;pW[n]=w; wsum+=w; csum+=C*w; n++;
        }
    }
    if(n<32) goto done;
    if(wsum>0 && csum/wsum < 0.025f){ *out_rgb=0xAEB4BE; rc=1; goto done; }   /* greyscale -> soft cool neutral */

    /* k-means (k=8), farthest-point seeding (deterministic) */
    { const int K=8; float cL[8],cA[8],cB[8],pop[8];
    { int bi=0; float bc=-1; for(int i=0;i<n;i++){ float c=pA[i]*pA[i]+pB[i]*pB[i]; if(c>bc){bc=c;bi=i;} }
      cL[0]=pL[bi];cA[0]=pA[bi];cB[0]=pB[bi]; }
    for(int k=1;k<K;k++){ int bi=0; float bd=-1;
        for(int i=0;i<n;i++){ float md=1e9f;
            for(int j=0;j<k;j++){ float dL=pL[i]-cL[j],da=pA[i]-cA[j],db=pB[i]-cB[j]; float d=1.25f*dL*dL+da*da+db*db; if(d<md)md=d; }
            if(md>bd){bd=md;bi=i;} }
        cL[k]=pL[bi];cA[k]=pA[bi];cB[k]=pB[bi]; }
    for(int it=0;it<10;it++){
        float sL[8]={0},sA[8]={0},sB[8]={0},sw[8]={0};
        for(int i=0;i<n;i++){ int bj=0; float bd=1e9f;
            for(int j=0;j<K;j++){ float dL=pL[i]-cL[j],da=pA[i]-cA[j],db=pB[i]-cB[j]; float d=1.25f*dL*dL+da*da+db*db; if(d<bd){bd=d;bj=j;} }
            float w=pW[i]; sL[bj]+=pL[i]*w; sA[bj]+=pA[i]*w; sB[bj]+=pB[i]*w; sw[bj]+=w; }
        for(int j=0;j<K;j++){ if(sw[j]>0){cL[j]=sL[j]/sw[j];cA[j]=sA[j]/sw[j];cB[j]=sB[j]/sw[j];} pop[j]=sw[j]; }
    }
    /* score clusters against a Vibrant-on-dark target (L 0.72, C 0.16) */
    float best=-1; int bk=-1;
    for(int j=0;j<K;j++){
        float L=cL[j],a=cA[j],b=cB[j]; float C=sqrtf(a*a+b*b);
        float pf = wsum>0 ? pop[j]/wsum : 0;
        if(pf<0.008f||C<0.035f||L<0.18f||L>0.92f) continue;
        float popS=sqrtf(pf);
        float lS=1.0f-a_clamp01(fabsf(L-0.72f)/0.32f);
        float cS=1.0f-a_clamp01(fabsf(C-0.16f)/0.14f);
        float vivid=a_smooth(0.07f,0.18f,C);
        float darkP=a_smooth(0.18f,0.38f,L);
        float lightP=1.0f-a_smooth(0.88f,0.98f,L);
        float sc=(0.42f*popS+0.28f*cS+0.22f*lS+0.08f*vivid)*darkP*lightP;
        if(pf<0.02f && C<0.12f) sc*=0.5f;                  /* stability: distrust small low-chroma specks */
        if(sc>best){best=sc;bk=j;}
    }
    if(bk<0){ *out_rgb=0xAEB4BE; rc=1; goto done; }    /* no usable colour -> neutral */

    float L=cL[bk],a=cA[bk],b=cB[bk]; float C=sqrtf(a*a+b*b), H=atan2f(b,a);
    /* tone-map (preserve hue) */
    if(L<0.58f)L=0.58f; else if(L>0.84f)L=0.84f;
    if(C<0.075f)C=0.075f; else if(C>0.22f)C=0.22f;
    if(C>0.18f&&L>0.72f) C*=0.92f;
    if(C<0.10f) C=C+(0.10f-C)*0.35f;
    a=C*cosf(H); b=C*sinf(H);
    /* WCAG contrast lift vs #1C1C1E (>=4.5:1 for glyphs); shrink C only if out of gamut */
    float Ybg=wcag_lum(0x1C,0x1C,0x1E); int R=0,G=0,Bo=0;
    for(int iter=0;iter<48;iter++){
        a=C*cosf(H); b=C*sinf(H);
        int in=oklab2rgb(L,a,b,&R,&G,&Bo);
        if(!in && C>0.04f){ C*=0.94f; continue; }
        float Y=wcag_lum(R,G,Bo); float hi=Y>Ybg?Y:Ybg, lo=Y>Ybg?Ybg:Y;
        if((hi+0.05f)/(lo+0.05f) >= 4.5f || L>=0.88f) break;
        L+=0.015f;
    }
    a=C*cosf(H); b=C*sinf(H); oklab2rgb(L,a,b,&R,&G,&Bo);
    *out_rgb = ((uint32_t)R<<16)|((uint32_t)G<<8)|(uint32_t)Bo;
    rc=1;
    }
done:
    free(pL);free(pA);free(pB);free(pW);
    return rc;
}
#pragma GCC pop_options

static int g_accent_gray = 0;   /* currently showing the idle/no-track neutral */
/* Accent config, mirrored from settings (read on the UI thread + art workers).
 * mode 0 = dynamic (album-art OKLab); 1 = static (fixed user colour: no OKLab
 * compute, no SONG.ACCENT read/write). g_accent_color is 0xRRGGBB. */
static _Atomic int g_accent_mode = 0;   /* read by art worker/prewarm threads -> atomic */
static uint32_t    g_accent_color = UI_RED;     /* UI-thread only */
static uint32_t    g_static_last  = 0xFFFFFFFFu; /* last static accent actually applied (skip redundant re-applies) */
int ui_accent_is_static(void){ return g_accent_mode == 1; }   /* worker/prewarm gate */

static void update_accent_seed(const track_state_t *st)
{
    if(st && st->have_track){ char was[520]; copy_cstr(was, sizeof was, np_acc_path);
        np_accent_from_track(st->path); if(strcmp(was, np_acc_path)) apply_accent(); }
    if(g_accent_mode == 1){            /* static: fixed colour; repaint only when it actually changed -
                                        * the 332x332 arc invalidation is ~85% of the screen every tick */
        if(g_static_last != g_accent_color){
            accent = theme_color_from_rgb(g_accent_color);
            apply_accent();
            g_static_last = g_accent_color;
        }
        return;
    }
    if(!st || !st->have_track){
        /* no track (idle, or the ~1s while playback is starting): show a neutral light
         * grey instead of a stale/weird accent from the last track. Apply once. */
        if(!g_accent_gray){
            accent = TC(ACCENT_IDLE);
            g_accent_gray = 1;
            last_seed_a[0] = 0; last_seed_b[0] = 0;
            apply_accent();
        }
        return;
    }
    g_accent_gray = 0;
    const char *seed_a = st->album[0] ? st->album : st->title;
    const char *seed_b = st->artist;

    if(strcmp(last_seed_a, seed_a) == 0 && strcmp(last_seed_b, seed_b) == 0) {
        return;
    }

    copy_cstr(last_seed_a, sizeof(last_seed_a), seed_a);
    copy_cstr(last_seed_b, sizeof(last_seed_b), seed_b);
    /* Use the cached per-song album-art accent if we have it (instant, consistent). If not,
     * DON'T flash an arbitrary text-hash colour (that looked "random" between tracks) - keep
     * the current accent until apply_art() applies the real OKLab colour when the cover
     * finishes decoding. So the accent only ever shows a real, album-derived colour. */
    int rgb = mdb_song_accent(st->path);
    if(rgb){ accent = theme_color_from_rgb(rgb); apply_accent(); }
}

/* set by clear_art_state / apply_art (main thread); main.c re-pushes the
 * art-dependent surfaces (Home pill / Saver backdrop / Options thumb) when set. */
static int g_art_applied = 0;

/* Drop all album-art state to the no-art fallback (used for no-track, empty
 * path, and failed decode) so stale art isn't republished to Home/Saver. */
static void clear_art_state(void)
{
    g_sharp_valid = 0;
    g_spin_ok = 0; imm_fast = 0;
    if(fsart_on){
        if(imm_spin) lv_obj_add_flag(imm_spin,LV_OBJ_FLAG_HIDDEN);
        if(fsart_img){lv_image_set_src(fsart_img,NULL);lv_obj_add_flag(fsart_img,LV_OBJ_FLAG_HIDDEN);}
        for(int k=0;k<6;k++) if(imm_deco[k]) lv_obj_add_flag(imm_deco[k],LV_OBJ_FLAG_HIDDEN);
    }
    if(poster_img) lv_image_set_src(poster_img, NULL);   /* no art: plain dark poster */
    g_poster_stale = 1; g_bd_ok = 0;
    cover_valid = thumb_valid = backdrop_valid = 0;
    coverdsc_valid = 0;
    last_art_key[0] = '\0';
    if(backdrop) lv_obj_add_flag(backdrop, LV_OBJ_FLAG_HIDDEN);
    if(cover_note) lv_label_set_text(cover_note, LV_SYMBOL_AUDIO);  /* no-art / failed decode = music note */
    set_cover_fallback(true);
    g_art_applied = 1;   /* re-push: clears Home/Saver since validity flags are now 0 */
}

/* ---- album-art worker ---------------------------------------------------
 * art_make_all() forks ffmpeg (hundreds of ms) - running it inline stalled
 * touch/render on every cross-album track change. It now runs on a detached
 * worker thread that ONLY writes the /tmp BMPs (touches no LVGL / g_coverbuf
 * state); the main thread applies the result in ui_art_poll() (LVGL is not
 * thread-safe). A new decode ALWAYS targets the non-displayed buffer
 * (displayed_idx ^ 1), so an in-flight decode can never overwrite the /tmp BMP the
 * shown art (Home pill / Saver backdrop) may still be rendering from. */
typedef struct {
    char track[256];
    int  idx;
    char out[40], tout[40], bout[40], sharp[40];
    char art_key[420];       /* album+dir reuse/staleness key (see last_art_key) */
    char artist[160], album[160], title[160];
    int online, from_net;
    int  is_clear;           /* "no track" - handled on the main thread; worker skips */
} art_req_t;

/* Serializes the two ffmpeg art decoders (the live NP worker below + the background cover prewarm) so
 * they never run two ~19MB image pipelines at once (OOM/stall). Held only around the ffmpeg call. */
static pthread_mutex_t g_decode_mu = PTHREAD_MUTEX_INITIALIZER;
/* Shared by ALL ffmpeg artwork decoders (live NP, prewarm, AND the vinyl saver) so at most one runs at
 * once - two ~19MB pipelines together risk OOM. saver.c wraps its decode with these. */
void ui_decode_lock(void){ pthread_mutex_lock(&g_decode_mu); }
void ui_decode_unlock(void){ pthread_mutex_unlock(&g_decode_mu); }

/* Album-cover prewarm work queue. The MAIN thread enqueues representative track paths (built with the
 * in-memory album/track caches, which are NOT thread-safe); the prewarm worker only pops + decodes, so
 * no mdb in-memory cache is ever touched off the UI thread. Bounded ring: enqueue fails (0) when full,
 * and the enumerator then pauses rather than dropping albums. */
#define PWQ_MAX 48   /* the worker drains at ~2/s; the seed self-throttles on a full queue (resumes the same album), so a small ring is plenty */
static char            pwq[PWQ_MAX][512];
static int             pwq_head, pwq_tail;
static pthread_mutex_t pwq_mu = PTHREAD_MUTEX_INITIALIZER;
int ui_prewarm_enqueue(const char *path){          /* MAIN thread; returns 1 if queued, 0 if full/dup */
    if(!path || !path[0]) return 1;
    int ok = 0;
    pthread_mutex_lock(&pwq_mu);
    int nt = (pwq_tail+1) % PWQ_MAX;
    if(nt != pwq_head){
        int dup = 0;                                /* cheap dedup: same path already pending */
        for(int i=pwq_head; i!=pwq_tail; i=(i+1)%PWQ_MAX) if(!strcmp(pwq[i], path)){ dup=1; break; }
        if(dup) ok = 1;
        else { copy_cstr(pwq[pwq_tail], sizeof pwq[pwq_tail], path); pwq_tail = nt; ok = 1; }
    }
    pthread_mutex_unlock(&pwq_mu);
    return ok;
}
static int pwq_pop(char *out, int cap){            /* worker thread; 1 if an item was dequeued */
    int got = 0;
    pthread_mutex_lock(&pwq_mu);
    if(pwq_head != pwq_tail){ copy_cstr(out, cap, pwq[pwq_head]); pwq_head = (pwq_head+1)%PWQ_MAX; got = 1; }
    pthread_mutex_unlock(&pwq_mu);
    return got;
}
static int pwq_pending(void){ pthread_mutex_lock(&pwq_mu); int p = (pwq_head != pwq_tail); pthread_mutex_unlock(&pwq_mu); return p; }
static void pw_sleep(int secs){                    /* idle sleep, but wake early the moment cover work is queued */
    for(int i=0;i<secs;i++){ if(pwq_pending()) return; sleep(1); }
}

static pthread_mutex_t g_art_mu = PTHREAD_MUTEX_INITIALIZER;
static art_req_t g_art_target;      /* latest requested decode          (guarded) */
static unsigned  g_art_req      = 0;/* bumps on each request            (guarded) */
static int       g_art_inflight = 0;/* a worker is running              (guarded) */
static int       g_art_ready    = 0;/* a result is waiting for the main thread (guarded) */
static int       g_art_rc       = -1;
static unsigned  g_art_done_req = 0;
static art_req_t g_art_result;      /* the req the waiting result is for (guarded) */
/* g_art_applied is declared above clear_art_state (main-thread only) */

/* main thread only: push a finished decode onto the NP cover + validity flags */
/* The helper supplies A's original 364px geometry without relying on stock cover.jpg or ffmpeg. */
static int poster_load_raw(const char *path){
    struct stat st;
    if(stat(path, &st) || !S_ISREG(st.st_mode) || st.st_size != PSZ * PSZ * 4) return -1;
    FILE *f = fopen(path, "rb"); if(!f) return -1;
    uint8_t row[PSZ * 4]; int ok = 1;
    for(int y=0; y<PSZ; y++){
        if(fread(row, 1, sizeof row, f) != sizeof row){ ok=0; break; }
        for(int x=0; x<PSZ; x++) memcpy(g_posterbuf + ((size_t)y*PSZ+x)*3, row+x*4, 3);
    }
    fclose(f); if(!ok) return -1;
    memset(&g_posterdsc, 0, sizeof g_posterdsc);
    g_posterdsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    g_posterdsc.header.cf = LV_COLOR_FORMAT_RGB888;
    g_posterdsc.header.w = g_posterdsc.header.h = PSZ;
    g_posterdsc.header.stride = PSZ * 3;
    g_posterdsc.data = g_posterbuf; g_posterdsc.data_size = sizeof g_posterbuf;
    return 0;
}

static void apply_art(const art_req_t *job)
{
    snprintf(cover_src,    sizeof cover_src,    "A:/tmp/cover%d.bmp",    job->idx);
    snprintf(thumb_src,    sizeof thumb_src,    "A:/tmp/thumb%d.bmp",    job->idx);
    snprintf(backdrop_src, sizeof backdrop_src, "A:/tmp/backdrop%d.bmp", job->idx);
    coverdsc_valid = (load_cover_dsc(job->out) == 0);   /* track whether g_coverdsc is THIS track's */
    if(coverdsc_valid) lv_image_set_src(cover_img, &g_coverdsc);
    else               lv_image_set_src(cover_img, cover_src);
    lv_image_set_inner_align(cover_img, LV_IMAGE_ALIGN_CENTER);
    lv_image_set_pivot(cover_img, COVER_D/2, COVER_D/2);
    set_cover_fallback(false);
    cover_valid = thumb_valid = backdrop_valid = 1;
    /* accent was computed + cached off the UI thread (art worker / prewarm), so just
     * read it - never run OKLab on the UI thread. If absent (no art, or no usable
     * colour), keep the text-hash accent set by update_accent_seed().
     * In static mode the user picked a fixed accent - never let cached art override it. */
    if(g_accent_mode != 1){
        int acc_rgb = mdb_song_accent(job->track);
        if(acc_rgb){ accent = theme_color_from_rgb(acc_rgb); apply_accent(); }
    }
    np_acc_path[0] = '\0'; np_accent_from_track(job->track); apply_accent();   /* poster: album colour */
    copy_cstr(last_art_key, sizeof last_art_key, job->art_key);   /* cache key (album+dir) */
    if(backdrop) {
        lv_image_set_src(backdrop, backdrop_src);
        lv_obj_remove_flag(backdrop, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_background(backdrop);
        if(g_np_poster || th_braun() || !g_np_backdrop_on) lv_obj_add_flag(backdrop, LV_OBJ_FLAG_HIDDEN);   /* the sharp poster replaces it; Braun: the grille */
    }
    g_sharp_valid = poster_load_raw(job->sharp) == 0;
    g_spin_ok = 0;
    if(g_np_poster) poster_reload();
    else g_poster_stale = 1;
    if(fsart_on){
        if(g_poster_stale) poster_reload();
        if(g_spin_ok){imm_bind_record();lv_obj_remove_flag(imm_spin,LV_OBJ_FLAG_HIDDEN);lv_obj_add_flag(fsart_img,LV_OBJ_FLAG_HIDDEN);}
    }
    backdrop_to_ram();                                    /* the blurred backdrop, once, into RAM */
    if(backdrop && backdrop_valid){ lv_image_set_src(backdrop, NULL); lv_image_set_src(backdrop, ui_current_backdrop_img()); }
    displayed_idx = job->idx;   /* this buffer is now the one on screen */
    g_art_applied = 1;          /* main.c re-pushes Home/Saver/npmenu art next loop */
}

static void *art_worker(void *arg)
{
    (void)arg;
    for(;;){
        art_req_t job; unsigned my_req, my_gen;
        pthread_mutex_lock(&g_art_mu);
        job = g_art_target; my_req = g_art_req;
        my_gen = art_cancel_gen();   /* capture the cancel token WITH the request: art_cancel() is only called
                                      * by the setter under g_art_mu, so it can't interleave this snapshot */
        pthread_mutex_unlock(&g_art_mu);

        char sharp_src[512]; copy_cstr(sharp_src,sizeof sharp_src,job.track);
        int rc, from_net = 0;                             /* from_net: the picture came from Online Album Art */
        char fp0[24] = "";                                /* the cover's fingerprint (for the accent's freshness) */
        if(job.is_clear) rc = -1;
        else if(artcache_get(job.track, job.out, job.tout, job.bout) == 0){ rc = 0;   /* cached -> fast copy, no decode */
               if(artcache_key(job.track, fp0, sizeof fp0) != 0) fp0[0] = 0; }
        else { int have_fp = artcache_key(job.track, fp0, sizeof fp0) == 0;   /* identity BEFORE decoding */
               pthread_mutex_lock(&g_decode_mu);                                     /* miss -> decode (killable), then cache */
               /* Pass OUR captured token: if a skip supersedes this decode any time after the snapshot (it
                * always bumps g_cancel_gen via art_cancel), the per-child gen check trips and no obsolete
                * decode runs - closing the window between request-validation and the decode. */
               rc = art_make_all_ex_gen(job.track, job.out, job.tout, job.bout, 1, my_gen);
               /* no cover of its own: a picture saved by Online Album Art (netart.c), decoded the same way */
               char np[300];
               if(rc != 0 && rc != -2 && job.online && netart_have(job.artist, job.album, job.title, np, sizeof np)){
                   if(netart_touch(np)){                      /* in use: the prune leaves it alone (and it is still there) */
                       rc = art_make_all_ex_gen(np, job.out, job.tout, job.bout, 1, my_gen);
                       from_net = rc == 0;
                       if(from_net) copy_cstr(sharp_src,sizeof sharp_src,np);
                       int ex = art_last_exit();              /* the helper itself said: no picture / unsupported / malformed */
                       if(rc != 0 && (ex == 3 || ex == 4 || ex == 5)) netart_reject(np);   /* never offer it again */
                   }
               }
               pthread_mutex_unlock(&g_decode_mu);
               /* an online picture is never cached as the song's own cover: turning the setting off must drop it */
               if(rc == 0 && have_fp && !from_net) artcache_put_if(job.track, fp0, job.out, job.tout, job.bout); }

        unlink(job.sharp);
        if(rc == 0 && !job.is_clear){
            pthread_mutex_lock(&g_decode_mu);
            art_make_poster(sharp_src, job.sharp, my_gen);
            pthread_mutex_unlock(&g_decode_mu);
        }

        /* compute + cache the OKLab accent HERE (off the UI thread) so apply_art()
         * never blocks: by the time the main thread applies this decode, the accent
         * is already in the DB and is just read back. Skipped in static-accent mode. */
        if(rc == 0 && !from_net && !job.is_clear && !ui_accent_is_static() && fp0[0] && !mdb_song_accent_fresh(job.track, fp0)){
            uint8_t *buf = malloc(CBMP*CBMP*4);
            if(buf){
                uint32_t rgb;
                if(read_cover_bmp(job.out, buf) == 0 && accent_from_buf(buf, &rgb))
                    mdb_set_song_accent(job.track, (int)(rgb & 0xFFFFFF), fp0);
                free(buf);
            }
        }

        job.from_net = from_net;
        pthread_mutex_lock(&g_art_mu);
        if(g_art_req == my_req){            /* still the latest request -> publish + stop */
            g_art_rc = rc; g_art_result = job; g_art_done_req = my_req;
            g_art_ready = 1; g_art_inflight = 0;
            pthread_mutex_unlock(&g_art_mu);
            return NULL;
        }
        pthread_mutex_unlock(&g_art_mu);    /* a newer request arrived -> loop, serve it */
    }
}

/* main thread: clear art NOW + invalidate any in-flight decode (so a track that
 * was mid-decode can't publish stale art after a no-track / clear). */
static void art_request_clear(void)
{
    clear_art_state();
    pthread_mutex_lock(&g_art_mu);
    g_art_req++;
    memset(&g_art_target, 0, sizeof g_art_target);
    g_art_target.is_clear = 1;
    pthread_mutex_unlock(&g_art_mu);
    art_cancel();   /* kill any in-flight decode so it can't publish stale art */
}

/* ---- audiobook chapters: Now Playing shows the current chapter instead of a meaningless album ----
 * A book's chapters are read once per book (chpl parse), then the current one is picked by position. */
#define NP_MAXCHAP 256   /* a chpl count is a single byte (<=255) - cover every declared chapter */
static chapter_t g_chaps[NP_MAXCHAP];
static int       g_nchaps;
static char      g_chap_path[256];

/* Build the Now Playing secondary line for a BOOK. Returns 1 if st is a book (out is set - possibly
 * empty when the book carries no chapters), 0 if it isn't a book (caller shows the album line). */
static int book_meta_line(const track_state_t *st, char *out, int cap)
{
    if(!mdb_is_book_path(st->path)) return 0;
    if(strcmp(g_chap_path, st->path) != 0){          /* new book -> load its chapters once (udta chpl parse) */
        g_nchaps = scan_read_chapters(st->path, g_chaps, NP_MAXCHAP);
        copy_cstr(g_chap_path, sizeof g_chap_path, st->path);
    }
    if(g_nchaps <= 0){ out[0] = '\0'; return 1; }    /* a book without chapters: show nothing, not an album */
    int cur = 0;
    for(int i = 0; i < g_nchaps; i++){ if(g_chaps[i].start_ms <= st->position_ms) cur = i; else break; }
    /* The chpl reader fills untitled chapters with "Chapter N"; if this chapter has only that
     * auto-name, show "Chapter N/M" rather than the redundant "N/M · Chapter N". */
    char autoname[24]; snprintf(autoname, sizeof autoname, "Chapter %d", cur + 1);
    if(strcmp(g_chaps[cur].title, autoname) == 0)
        snprintf(out, cap, "Chapter %d/%d", cur + 1, g_nchaps);
    else
        snprintf(out, cap, "%d/%d  \xC2\xB7  %s", cur + 1, g_nchaps, g_chaps[cur].title);   /* 2/3 · Into the Future */
    return 1;
}

/* Swap the prev/next transport glyphs between music (track skip) and audiobook (-15s/+30s). Only
 * touches the widgets when the mode actually changes, so it's cheap to call every update. */
static int g_np_bookmode = -1;   /* -1 unknown, 0 music, 1 book */
static void np_transport_glyphs(int book)
{
    if(book == g_np_bookmode) return;
    g_np_bookmode = book;
    lv_obj_set_style_text_font(btn_prev, book ? TF(UI_22) : TF(UI_28), LV_PART_MAIN);
    lv_obj_set_style_text_font(btn_next, book ? TF(UI_22) : TF(UI_28), LV_PART_MAIN);
    lv_label_set_text(btn_prev, book ? "-15" : LV_SYMBOL_PREV);
    lv_label_set_text(btn_next, book ? "+30" : LV_SYMBOL_NEXT);
}

/* ---- audiobook sleep timer (moon on Now Playing) --------------------------------------------- */
/* End of the chapter that currently contains the playback position (in book ms), for "End of chapter"
 * sleep. Reuses the g_chaps cache loaded by book_meta_line. */
static long book_cur_chapter_end(const track_state_t *st)
{
    if(strcmp(g_chap_path, st->path) != 0){
        g_nchaps = scan_read_chapters(st->path, g_chaps, NP_MAXCHAP);
        copy_cstr(g_chap_path, sizeof g_chap_path, st->path);
    }
    if(g_nchaps <= 0) return st->duration_ms;
    int cur = 0;
    for(int i = 0; i < g_nchaps; i++){ if(g_chaps[i].start_ms <= st->position_ms) cur = i; else break; }
    if(cur + 1 < g_nchaps) return g_chaps[cur+1].start_ms;
    return st->duration_ms > 0 ? st->duration_ms : g_chaps[cur].start_ms;   /* last chapter ends at the book end */
}

/* The [start,end] ms window of the chapter currently under the position, for a book with chapters.
 * Returns 1 (window set) or 0 (not a chaptered book -> caller uses the whole track). Reuses g_chaps. */
static int book_chapter_window(const track_state_t *st, long *lo, long *hi)
{
    if(!mdb_is_book_path(st->path)) return 0;
    if(strcmp(g_chap_path, st->path) != 0){
        g_nchaps = scan_read_chapters(st->path, g_chaps, NP_MAXCHAP);
        copy_cstr(g_chap_path, sizeof g_chap_path, st->path);
    }
    if(g_nchaps <= 0) return 0;
    int cur = 0;
    for(int i = 0; i < g_nchaps; i++){ if(g_chaps[i].start_ms <= st->position_ms) cur = i; else break; }
    long a = g_chaps[cur].start_ms;
    long b = (cur + 1 < g_nchaps) ? g_chaps[cur+1].start_ms
                                  : (st->duration_ms > 0 ? st->duration_ms : a);
    if(b <= a) return 0;                 /* degenerate chapter -> fall back to the whole track */
    *lo = a; *hi = b;
    return 1;
}

static lv_obj_t *g_sleep_dlg;
void ui_np_close_overlays(void){ if(g_sleep_dlg){ lv_obj_del(g_sleep_dlg); g_sleep_dlg = NULL; } }
int  ui_np_overlay_active(void){ return g_sleep_dlg != NULL; }   /* 1 while the sleep popover is up (main loop suppresses the ring/nav gesture) */

static void sleep_opt_cb(lv_event_t *e)
{
    if(lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    int v = (int)(intptr_t)lv_event_get_user_data(e);   /* minutes; 0 = off; -1 = end of chapter */
    if(v == -1){
        track_state_t st; ipc_get_state(&st);
        if(!mdb_is_book_path(st.path)){ ui_set_sleep_timer(0); ui_np_close_overlays(); return; }  /* context changed away from a book */
        long tgt = book_cur_chapter_end(&st);
        int at_end = (st.duration_ms > 0 && tgt >= st.duration_ms);   /* book_cur_chapter_end returns EXACTLY the duration only for the last / chapterless chapter -> the target is the file end (a rollover may fulfil it); a mid-book boundary is < duration */
        if(tgt > st.position_ms){ ui_set_sleep_eoc(tgt, st.path, at_end); ui_toast("Sleep at end of chapter"); }
        else { ui_set_sleep_timer(0); ui_toast("Sleep timer off"); }
    } else if(v > 0){
        ui_set_sleep_timer(v);
        char b[24]; snprintf(b, sizeof b, "Sleep in %d min", v); ui_toast(b);
    } else {
        ui_set_sleep_timer(0); ui_toast("Sleep timer off");
    }
    ui_np_close_overlays();
}
static void sleep_scrim_cb(lv_event_t *e){ if(lv_event_get_code(e) == LV_EVENT_CLICKED) ui_np_close_overlays(); }

static void sleep_click_cb(lv_event_t *e)
{
    if(lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if(g_sleep_dlg){ ui_np_close_overlays(); return; }              /* tap the moon again to dismiss */
    lv_obj_t *scrim = lv_obj_create(lv_layer_top());               /* full-screen dim; tap outside closes */
    g_sleep_dlg = scrim;
    lv_obj_remove_style_all(scrim);
    lv_obj_set_size(scrim, 360, 360); lv_obj_center(scrim);
    lv_obj_set_style_bg_color(scrim, TC(SCRIM), 0);
    lv_obj_set_style_bg_opa(scrim, LV_OPA_60, 0);
    lv_obj_add_flag(scrim, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(scrim, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(scrim, sleep_scrim_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *panel = lv_obj_create(scrim);
    lv_obj_remove_style_all(panel);
    lv_obj_set_size(panel, 224, 296); lv_obj_center(panel);
    lv_obj_set_style_radius(panel, 16, 0);
    lv_obj_set_style_bg_color(panel, TC(SURFACE), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_border_color(panel, TC(SURFACE_RAISED), 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(panel, 6, 0);
    lv_obj_set_style_pad_ver(panel, 14, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *hdr = lv_label_create(panel);
    lv_label_set_text(hdr, "Sleep Timer");
    lv_obj_set_style_text_font(hdr, TF(UI_16), 0);
    lv_obj_set_style_text_color(hdr, TC(TEXT_PRIMARY), 0);

    static const char *OPT_T[] = { "15 min", "30 min", "45 min", "60 min", "End of chapter", "Off" };
    static const int   OPT_V[] = { 15, 30, 45, 60, -1, 0 };
    for(unsigned i = 0; i < sizeof OPT_V / sizeof OPT_V[0]; i++){
        lv_obj_t *b = lv_button_create(panel);
        lv_obj_remove_style_all(b);
        lv_obj_set_size(b, 196, 34);
        lv_obj_set_style_radius(b, 10, 0);
        lv_obj_set_style_bg_color(b, TC(SURFACE_RAISED), 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(b, TC(SURFACE_STRONG), LV_STATE_PRESSED);
        lv_obj_add_event_cb(b, sleep_opt_cb, LV_EVENT_CLICKED, (void *)(intptr_t)OPT_V[i]);
        lv_obj_t *l = lv_label_create(b);
        lv_label_set_text(l, OPT_T[i]);
        lv_obj_set_style_text_font(l, TF(UI_16), 0);
        lv_obj_set_style_text_color(l, OPT_V[i] == -1 ? accent : TC(TEXT_PRIMARY), 0);
        lv_obj_center(l);
    }
}

/* art reuse/staleness key: album title + the track's parent directory. Audiobooks (.m4b) each carry
 * their own cover, so key them by full path - two books sharing an album tag in one folder must not
 * reuse each other's cover. */
static void make_art_key(const track_state_t *st, char *out, int cap)
{
    if(mdb_is_book_path(st->path)){ snprintf(out, cap, "%s", st->path); return; }
    char dir[256]; snprintf(dir, sizeof dir, "%s", st->path);
    char *slash = strrchr(dir, '/'); if(slash) *slash = '\0'; else dir[0] = '\0';
    snprintf(out, cap, "%s\n%s", st->album, dir);
}

/* main thread: decide whether art needs (re)decoding and dispatch to the worker */
static void update_cover_for_path(const track_state_t *st)
{
    if(strcmp(last_path, st->path) == 0) return;
    char art_key[420]; make_art_key(st, art_key, sizeof art_key);
    copy_cstr(want_art_key, sizeof want_art_key, art_key);   /* what the live track wants now */

    /* Art is shared within an album/folder: same key + valid art -> reuse, skip ffmpeg. */
    if(st->album[0] && cover_valid && coverdsc_valid && strcmp(last_art_key, art_key) == 0) {
        copy_cstr(last_path, sizeof(last_path), st->path);
        /* art is already valid for this album - ensure the cover is shown (recovers
         * if a previous in-flight load had swapped in the loading glyph and then a
         * same-album track arrived before that decode landed). If a different album
         * is loading, coverdsc_valid is false: queue the current album below so that
         * old request is superseded instead of leaving it able to clear this art. */
        if(cover_note) lv_label_set_text(cover_note, LV_SYMBOL_AUDIO);
        set_cover_fallback(false);
        return;
    }

    copy_cstr(last_path, sizeof(last_path), st->path);

    if(!st->path[0]) { art_request_clear(); return; }

    /* New album/art incoming: hide the now-wrong NP cover and show a loading glyph
     * while the worker decodes, so the previous album's art doesn't masquerade as
     * this track's. apply_art() (success) or clear_art_state() (fail) restores it.
     * NB: do NOT touch cover_valid here - it's read by ui_current_cover_dsc() for the
     * vinyl saver, which (like the Home thumb/backdrop) keeps last-good art until the
     * new decode lands; only this prominent NP cover shows the loading state. */
    if(cover_note) lv_label_set_text(cover_note, LV_SYMBOL_REFRESH);
    set_cover_fallback(true);
    coverdsc_valid = 0;   /* the RAM decode is now the PREVIOUS album's - don't let the accent use stale art */

    /* ALWAYS target the non-displayed buffer so an in-flight decode never
     * overwrites the /tmp BMP the shown art may still reference. */
    int nidx = displayed_idx ^ 1;
    art_req_t job; memset(&job, 0, sizeof job);
    job.idx = nidx;
    copy_cstr(job.track, sizeof job.track, st->path);
    copy_cstr(job.art_key, sizeof job.art_key, art_key);
    copy_cstr(job.artist,sizeof job.artist,st->artist);
    copy_cstr(job.album,sizeof job.album,st->album);
    copy_cstr(job.title,sizeof job.title,st->title);
    job.online=cfg_get_int("online_art",0);
    snprintf(job.out,  sizeof job.out,  "/tmp/cover%d.bmp",    nidx);
    snprintf(job.tout, sizeof job.tout, "/tmp/thumb%d.bmp",    nidx);
    snprintf(job.bout, sizeof job.bout, "/tmp/backdrop%d.bmp", nidx);
    snprintf(job.sharp, sizeof job.sharp, "/tmp/sharp%d.bgra", nidx);

    int launch = 0;
    pthread_mutex_lock(&g_art_mu);
    g_art_req++;
    g_art_target = job;
    if(!g_art_inflight){ g_art_inflight = 1; launch = 1; }
    else art_cancel();   /* a worker is mid-decode on a now-stale track -> kill its ffmpeg so it serves this
                          * one. Done UNDER g_art_mu (not after unlocking): otherwise the worker could read
                          * the NEW target and start its ffmpeg in the gap, and this cancel would kill THAT
                          * valid decode instead (leaving art missing). Deadlock-free: no path holds g_pid_mu
                          * (which art_cancel takes) while acquiring g_art_mu, and kill() doesn't block. */
    pthread_mutex_unlock(&g_art_mu);

    if(launch){
        pthread_t th;
        if(pthread_create(&th, NULL, art_worker, NULL) == 0){
            pthread_detach(th);
        } else {                            /* memory pressure: never block the UI on a decode */
            pthread_mutex_lock(&g_art_mu); g_art_inflight = 0; pthread_mutex_unlock(&g_art_mu);
            clear_art_state();
        }
    }
    /* else: a worker is already running; it will pick up g_art_target when it loops */
}

/* main-thread timer: apply a finished decode (or re-clear) */
void ui_art_poll(lv_timer_t *t)
{
    (void)t;
    { char got[512];                        /* Online Album Art saved a picture: redo that song's cover if it's on */
      if(netart_take_ready(got, sizeof got) && cfg_get_int("online_art", 0) && strcmp(got, last_path) == 0) ui_art_retry(); }
    int ready = 0, rc = -1; art_req_t res; unsigned done_req = 0, cur_req = 0;
    pthread_mutex_lock(&g_art_mu);
    if(g_art_ready){
        ready = 1; g_art_ready = 0;
        rc = g_art_rc; res = g_art_result; done_req = g_art_done_req; cur_req = g_art_req;
    }
    pthread_mutex_unlock(&g_art_mu);
    if(!ready) return;
    if(done_req != cur_req) return;        /* superseded - a newer decode is in flight */
    if(res.is_clear)      clear_art_state();/* main already cleared; idempotent */
    else if(rc == 0 && res.from_net && !cfg_get_int("online_art", 0)){
        clear_art_state();                  /* an online picture that landed after the setting was turned off */
    }
    else if(rc == 0){
        /* only apply if the live track still wants this album - guards the rare
         * A(X)->B(Y, decoding)->C(X) race where Y would otherwise stomp correct X art. */
        if(strcmp(res.art_key, want_art_key) == 0){
            apply_art(&res);
            /* an online picture may be one song's own (not its album's) and must disappear when the setting goes off:
             * never reuse it for the next track of the album */
            if(res.from_net) last_art_key[0] = '\0';
        }
    }
    else if(strcmp(res.art_key, want_art_key) == 0) {
        /* Failure belongs to the requested album just as success does. A late
         * obsolete failure must never clear the current album's artwork. */
        clear_art_state();
        /* no cover of its own: look it up online in the background (Settings > Display > Online Album Art) */
        if(rc != -2 && res.online && cfg_get_int("online_art", 0) && strcmp(res.art_key, want_art_key) == 0)
            netart_request(res.track, res.artist, res.album, res.title);
    }
}

/* main.c calls this each loop: returns 1 once after art was (re)applied so the
 * dependent surfaces (Home pill / Saver backdrop / Options thumb) get re-pushed. */
int ui_take_art_applied(void)
{
    if(!g_art_applied) return 0;
    g_art_applied = 0;
    return 1;
}

/* Settings hook: apply the chosen accent mode/colour immediately (main thread). */
void ui_set_accent_config(int mode, int rgb){
    g_accent_mode  = mode ? 1 : 0;           /* fork: Ring follows the chosen accent too (the merge had forced the album colour) */
    g_accent_color = (uint32_t)rgb & 0xFFFFFF;
    if(g_accent_mode){
        accent = theme_color_from_rgb(g_accent_color);   /* paint the fixed colour now */
        g_accent_gray = 0;
    } else {
        last_seed_a[0] = '\0'; last_seed_b[0] = '\0';   /* force dynamic recompute next ui_update */
    }
    apply_accent();
    g_static_last = g_accent_mode ? g_accent_color : 0xFFFFFFFFu;   /* config just repainted; keep the guard in sync */
    g_art_applied = 1;   /* re-push Home/Saver/npmenu accent surfaces */
}

/* ---- background art prewarm (user-gated, heat-safe) ------------------------
 * Fills the SD cover cache (+ DB accent, dynamic mode only) so song-switching is
 * instant even on first play. The earlier always-on version overheated the device
 * to 50C, so this only works while the user-chosen window holds AND the battery is
 * cool, and it paces hard. Modes: 0=off 1=when idle 2=when charging 3=idle|charging. */
static _Atomic int g_prewarm_mode = 0;   /* cfg mirror: settings (main) writes, worker reads */
void ui_set_prewarm_mode(int m){ g_prewarm_mode = (m<0)?0:(m>2?2:m); }   /* 0=off 1=idle 2=idle&charging */

static int pw_temp_dc(void){            /* battery temp, tenths-C; -1 on read failure */
    FILE *f=fopen("/sys/class/power_supply/cw221X-bat/temp","r"); if(!f) return -1;
    int t=-1; if(fscanf(f,"%d",&t)!=1) t=-1; fclose(f); return t;
}
static int pw_charging(void){
    /* cw221X exposes NO `status` node - charging shows via current_now (1=charging, 0=battery on
     * this driver). The old `status` read always failed -> prewarm mode "idle & charging" never ran. */
    long cur=0;
    FILE *f=fopen("/sys/class/power_supply/cw221X-bat/current_now","r"); if(!f) return 0;
    if(fscanf(f,"%ld",&cur)!=1) cur=0; fclose(f);
    return cur > 0;
}
/* 1 if the user-selected window currently allows background work. IDLE is mandatory
 * for every active mode: never decode while the user is actively looking at the screen
 * (heat-/UX-safety), even on charge. m1=when idle, m2=idle AND charging. */
static int pw_window_open(void){
    int m=g_prewarm_mode; if(m==0) return 0;
    if(!ui_main_is_idle()) return 0;   /* screen dimmed/off required */
    if(m==2) return pw_charging();     /* idle & charging */
    return 1;                          /* m==1: when idle */
}

/* Block until heat + the live decode allow another ffmpeg. Shared by both prewarm phases. */
static void pw_wait_window(int *hot){
    for(;;){
        int t = pw_temp_dc();                       /* battery temp, tenths-C; fail-closed if unreadable */
        if(t < 0){ sleep(15); continue; }
        if(*hot){ if(t < 400) *hot=0; else { sleep(15); continue; } }   /* hysteresis: pause >=42C, resume <40C */
        else if(t >= 420){ *hot=1; sleep(15); continue; }
        int busy; pthread_mutex_lock(&g_art_mu); busy=g_art_inflight; pthread_mutex_unlock(&g_art_mu);
        if(busy){ usleep(300*1000); continue; }     /* never a 2nd ffmpeg while the user's decode runs */
        return;
    }
}

/* PHASE 1 (boot): proactively decode ONE cover per album so the Album (cover flow) view is populated
 * without playing every album first. Per-album (hundreds) not per-track (thousands), and strictly
 * battery-temp throttled, so it fills in a couple of minutes on first boot and is near-instant on later
 * boots (the SD cache persists). Runs regardless of the idle gate - users want covers ready while they
 * browse - but yields to the live decode and to heat. DB access is safe: g_db is opened FULLMUTEX. */
static void *art_prewarm_worker(void *arg)
{
    (void)arg;
    nice(15);                 /* lowest CPU priority */
    const char *PC="/tmp/pw_cover.bmp", *PT="/tmp/pw_thumb.bmp", *PB="/tmp/pw_backdrop.bmp";
    int id = 0, hot = 0, did_work = 0;   /* hot = temp hysteresis latch */
    for(;;){
        /* PHASE 1: album-cover prewarm. The MAIN thread enqueues representative track paths (built with
         * the non-thread-safe album/track caches); the worker only pops + decodes here, so no mdb
         * in-memory cache is touched off the UI thread. Runs regardless of the idle gate (users want
         * covers while browsing) but stays temp-throttled and serialized with the live NP decoder. */
        char qpath[512];
        if(pwq_pop(qpath, sizeof qpath)){
            pw_wait_window(&hot);                        /* heat gate + yields to the live decode */
            if(artcache_has(qpath)){ usleep(15*1000); continue; }   /* already cached (the UI thread no longer checks) */
            char fp0[24];
            if(!artcache_has(qpath) && artcache_key(qpath, fp0, sizeof fp0) == 0){
                pthread_mutex_lock(&g_decode_mu);
                int r = art_make_all(qpath, PC, PT, PB);
                pthread_mutex_unlock(&g_decode_mu);
                if(r == 0) artcache_put_if(qpath, fp0, PC, PT, PB);
            }
            usleep(120*1000);                            /* light pace; the temp gate is the real limiter */
            continue;
        }
        /* PHASE 2: per-track idle sweep (accents + any remaining covers), gated on the user setting. */
        if(!pw_window_open()){ id=0; did_work=0; pw_sleep(5); continue; }   /* not allowed now -> wait (wake for queued covers) */

        /* temp throttle: fail-closed if unreadable; hysteresis pause >=42C, resume <40C. */
        int t = pw_temp_dc();
        if(t < 0){ sleep(15); continue; }
        if(hot){ if(t < 400) hot=0; else { sleep(15); continue; } }
        else if(t >= 420){ hot=1; sleep(15); continue; }

        /* never run a 2nd decoder while the live (user) decode is in flight */
        { int busy; pthread_mutex_lock(&g_art_mu); busy=g_art_inflight; pthread_mutex_unlock(&g_art_mu);
          if(busy){ usleep(300*1000); continue; } }

        char path[300];
        if(!mdb_prewarm_next(id, &id, path, sizeof path)){   /* swept the whole library */
            id=0; pw_sleep(did_work?60:1800); did_work=0; continue;   /* rest longer if nothing pending (wake for queued covers) */
        }
        if(!path[0]) continue;

        int have_art = artcache_has(path);
        char acc_fp[24];
        int need_acc = !ui_accent_is_static() && artcache_key(path, acc_fp, sizeof acc_fp) == 0
                       && !mdb_song_accent_fresh(path, acc_fp);
        if(have_art && !need_acc){ usleep(15*1000); continue; }   /* already done -> light skip */

        if(!have_art){
            char fp0[24];
            if(artcache_key(path, fp0, sizeof fp0) != 0){ usleep(300*1000); continue; }
            pthread_mutex_lock(&g_decode_mu);
            int r = art_make_all(path, PC, PT, PB);
            pthread_mutex_unlock(&g_decode_mu);
            if(r == 0) artcache_put_if(path, fp0, PC, PT, PB);
            else { usleep(300*1000); continue; }                  /* undecodable -> skip */
        }
        if(need_acc){
            /* Materialise THIS path's cover before reading it. If the cached read fails (SD vanished mid-
             * sweep), PC still holds the PREVIOUS track's image; computing+storing an accent from it would
             * mislabel this path permanently (a nonzero accent skips future correction). Only proceed on a
             * confirmed fresh cover for this path (a just-decoded !have_art cover is already in PC). */
            int cover_ready = have_art ? (artcache_get(path, PC, PT, PB) == 0) : 1;
            if(cover_ready){
                uint8_t *buf = malloc(CBMP*CBMP*4);
                if(buf){
                    uint32_t rgb;
                    if(read_cover_bmp(PC, buf) == 0 && accent_from_buf(buf, &rgb))
                        mdb_set_song_accent(path, (int)(rgb & 0xFFFFFF), acc_fp);
                    free(buf);
                }
            }
        }
        did_work = 1;
        sleep(3);     /* hard pace between decodes: keep heat low */
    }
    return NULL;
}

/* main.c calls this once at startup. The worker self-gates on g_prewarm_mode, so
 * it's safe to always spawn (it just sleeps while mode==off). */
void ui_start_art_prewarm(void)
{
    pthread_t th;
    if(pthread_create(&th, NULL, art_prewarm_worker, NULL) == 0) pthread_detach(th);
}

/* Expose the current decoded cover to other surfaces (e.g. the Home pill).
 * Returns the "A:/tmp/coverN.bmp" path, or NULL when there's no usable art. */
const char *ui_current_cover_src(void)
{
    return cover_valid ? cover_src : NULL;
}

/* Rotatable RAM cover (ARGB8888 dsc) for the vinyl screensaver - the file-BMP
 * renders black when rotated, this one transforms fine. NULL if no art. */
const void *ui_current_cover_dsc(void)
{
    return coverdsc_valid ? (const void *)&g_coverdsc : NULL;   /* NULL if RAM decode failed (no stale art) */
}

/* Native-size 42px thumb path for the Home pill, or NULL when no art. */
const char *ui_current_thumb_src(void)
{
    return thumb_valid ? thumb_src : NULL;
}

/* Full-screen blurred backdrop path (for the screensaver), or NULL. */
const char *ui_current_backdrop_src(void)
{
    return backdrop_valid ? backdrop_src : NULL;
}

/* Montserrat covers Latin + Latin-Extended only; it has no Cyrillic/Greek (issue #3:
 * "Ленинград" rendered as boxes) and no CJK ("北京"). Build a per-size fallback chain:
 *   Montserrat(size) -> font_intl(size) [Cyrillic/Greek/Latin-ext, Noto, uncompressed]
 *                    -> Source Han 16 [CJK] -> LVGL placeholder.
 * The intl fonts are generated at matched ascent/descent (base_line 4/5/6 == Montserrat's)
 * so fallback glyphs sit on the same baseline; LVGL uses the PRIMARY font's line-height, and
 * each intl line-height (20/23/28) fits within Montserrat's (20/24/28) so nothing clips.
 * We keep mutable copies because .fallback must be set on non-const fonts. */
static lv_font_t s_cjk; /* upstream music-metadata supplement */
static lv_font_t s_font28, s_font24, s_font20, s_font18, s_font16, s_font14;      /* Montserrat, chain heads */
static lv_font_t s_intl20, s_intl18, s_intl16, s_intl14;      /* intl link (fallback -> Source Han) */
extern const lv_font_t font_inter_medium_14, font_inter_medium_16, font_inter_bold_18, font_inter_bold_22;
static void ui_fonts_init(void)
{
    if(s_font20.get_glyph_dsc) return;   /* once */
    s_cjk = lv_font_source_han_16_cjk; s_cjk.fallback = &font_cjk_extra_16;
    s_intl20 = font_intl_20; s_intl20.fallback = &s_cjk;
    s_intl18 = font_intl_18; s_intl18.fallback = &s_cjk;
    s_intl16 = font_intl_16; s_intl16.fallback = &s_cjk;
    s_intl14 = font_intl_14; s_intl14.fallback = &s_cjk;
    /* Inter sits between Montserrat and the international fonts: it has the typographic punctuation that
     * track titles and lyrics often carry (\u2019 \u2018 \u201C \u201D \u2026 \u2013 \u2014 \u00B7), which the other two lack. */
    static lv_font_t p22, p18, p16, p14;
    p22 = font_inter_bold_22;   p22.fallback = &s_intl20;
    p18 = font_inter_bold_18;   p18.fallback = &s_intl18;
    p16 = font_inter_medium_16; p16.fallback = &s_intl16;
    p14 = font_inter_medium_14; p14.fallback = &s_intl14;
    s_font28 = *theme_font_original(28); s_font28.fallback = &p22;   /* poster title */
    s_font24 = *theme_font_original(24); s_font24.fallback = &p22;   /* ring title */
    s_font20 = *theme_font_original(20); s_font20.fallback = &p22;
    s_font18 = *theme_font_original(18); s_font18.fallback = &p18;
    s_font16 = *theme_font_original(16); s_font16.fallback = &p16;
    s_font14 = *theme_font_original(14); s_font14.fallback = &p14;
}

/* Public accessor for the fallback-chained text font (Montserrat -> intl -> CJK) so other screens
 * render user text (track/album/artist names) with full glyph coverage. px = 14/16/18/20. */
const lv_font_t *ui_text_font(int px){
    ui_fonts_init();
    switch(px){ case 20: return &s_font20; case 18: return &s_font18; case 14: return &s_font14; default: return &s_font16; }
}

/* Shared CJK-capable user-text font (montserrat + Source Han Sans fallback) for any screen
 * that shows track/artist/album/lyrics text. Idempotently initialises on first use. */
const lv_font_t *ui_font_cjk(int size)
{
    ui_fonts_init();
    if(size >= 28) return &s_font28;                   /* 24/28 carry the same non-Latin fallback */
    if(size >= 24) return &s_font24;
    if(size >= 20) return &s_font20;
    if(size >= 18) return &s_font18;
    if(size >= 16) return &s_font16;
    return &s_font14;
}

/* fork: user text in lists (song / album / artist / file names) follows Settings > Display > Font Size like the rest
 * of the list text: Small one step down, Large one step up (titles and clocks keep their size, they don't use this) */
const lv_font_t *ui_font_user(int size){
    int fs = theme_font_size();
    if(fs == 0 && size > 14) size -= 2;
    else if(fs == 2 && size < 20) size += 2;
    return ui_font_cjk(size);
}
void ui_create(lv_obj_t *root)
{
    if(th_ringlike())lv_obj_add_flag(root,LV_OBJ_FLAG_USER_2);
    lv_obj_t *scr = root;

    ui_fonts_init();
    lin_lut_init();   /* seed the OKLab sRGB LUT once, up front: dynamic accent compute
                       * (art worker) needs it whether or not the prewarm ever runs. */
    /* settings_apply_startup() ran before us and already loaded g_accent_mode/g_accent_color
     * from cfg, so honor a saved STATIC accent here - otherwise Home/Saver (created right
     * after, and now reading the live accent) would paint the default until the first
     * ui_update() corrected them. Dynamic mode keeps the default until an album accent lands. */
    accent = (g_accent_mode == 1) ? theme_color_from_rgb(g_accent_color) : TC(ACCENT_PRIMARY);
    last_path[0] = '\0';
    last_art_key[0] = '\0';
    want_art_key[0] = '\0';
    last_seed_a[0] = '\0';
    last_seed_b[0] = '\0';
    cover_src[0] = '\0';
    cover_valid = 0;
    coverdsc_valid = 0;
    thumb_src[0] = '\0';
    thumb_valid = 0;
    backdrop_valid = 0;
    displayed_idx = 0;
    shown_progress = 0;

    lv_obj_clean(scr);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, theme_ring_light()?TC(CANVAS):TC(SCRIM), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);

    g_np_scr = scr;
    /* full-screen blurred album-art backdrop (created first = behind everything) */
    backdrop_src[0] = '\0';
    backdrop = lv_image_create(scr);
    lv_obj_set_size(backdrop, 360, 360);
    lv_obj_align(backdrop, LV_ALIGN_CENTER, 0, 0);
    /* darken the blurred art so foreground text/controls stay readable */
    lv_obj_set_style_image_recolor(backdrop,theme_ring_light()?TC(CANVAS):TC(SCRIM),LV_PART_MAIN);
    lv_obj_set_style_image_recolor_opa(backdrop,theme_ring_light()?85:150,LV_PART_MAIN);
    lv_obj_add_flag(backdrop, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(backdrop, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    /* ---- Poster layers (hidden unless np_style == 2): sharp cover, dim, top whisper, bottom-third fade ---- */
    poster_img = lv_image_create(scr);
    lv_obj_set_size(poster_img, 364, 364);
    lv_obj_align(poster_img, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(poster_img, LV_OBJ_FLAG_CLICKABLE);
    poster_dim = lv_obj_create(scr);
    lv_obj_remove_style_all(poster_dim);
    lv_obj_set_size(poster_dim, 360, 360);
    lv_obj_set_style_bg_color(poster_dim, TC(SCRIM), 0);
    lv_obj_set_style_bg_opa(poster_dim, 56, 0);                     /* ~22%: keeps the art rich */
    lv_obj_clear_flag(poster_dim, LV_OBJ_FLAG_CLICKABLE);
    poster_top = lv_obj_create(scr);                                 /* legibility for the time at the top */
    lv_obj_remove_style_all(poster_top);
    lv_obj_set_size(poster_top, 360, 90);
    lv_obj_align(poster_top, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(poster_top, TC(SCRIM), 0);
    lv_obj_set_style_bg_grad_color(poster_top, TC(SCRIM), 0);
    lv_obj_set_style_bg_grad_dir(poster_top, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_main_opa(poster_top, 130, 0);
    lv_obj_set_style_bg_grad_opa(poster_top, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(poster_top, LV_OPA_COVER, 0);
    lv_obj_clear_flag(poster_top, LV_OBJ_FLAG_CLICKABLE);
    poster_fade = lv_obj_create(scr);                                /* bottom third, ramps in hard */
    lv_obj_remove_style_all(poster_fade);
    lv_obj_set_size(poster_fade, 360, 160);
    lv_obj_align(poster_fade, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(poster_fade, TC(SCRIM), 0);
    lv_obj_set_style_bg_grad_color(poster_fade, TC(SCRIM), 0);
    lv_obj_set_style_bg_grad_dir(poster_fade, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_main_opa(poster_fade, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_grad_opa(poster_fade, 245, 0);
    lv_obj_set_style_bg_main_stop(poster_fade, 0, 0);
    lv_obj_set_style_bg_grad_stop(poster_fade, 150, 0);             /* full strength ~60% of the way down */
    lv_obj_set_style_bg_opa(poster_fade, LV_OPA_COVER, 0);
    lv_obj_clear_flag(poster_fade, LV_OBJ_FLAG_CLICKABLE);
    np_scrim = lv_obj_create(scr);                                   /* soft dark band behind the title */
    lv_obj_remove_style_all(np_scrim);
    lv_obj_set_size(np_scrim, 360, 150);
    lv_obj_align(np_scrim, LV_ALIGN_TOP_MID, 0, 128);
    lv_obj_clear_flag(np_scrim, LV_OBJ_FLAG_CLICKABLE);
    for(int h = 0; h < 2; h++){                                      /* fade in, then fade out: no edges */
        lv_obj_t *g = lv_obj_create(np_scrim); lv_obj_remove_style_all(g);
        lv_obj_set_size(g, 360, 75); lv_obj_set_pos(g, 0, h * 75); lv_obj_clear_flag(g, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(g, TC(SCRIM), 0); lv_obj_set_style_bg_grad_color(g, TC(SCRIM), 0);
        lv_obj_set_style_bg_grad_dir(g, LV_GRAD_DIR_VER, 0);
        lv_obj_set_style_bg_main_opa(g, h ? 105 : 0, 0); lv_obj_set_style_bg_grad_opa(g, h ? 0 : 105, 0);
        lv_obj_set_style_bg_opa(g, LV_OPA_COVER, 0);
    }
    SHOW(poster_img, 0); SHOW(poster_dim, 0); SHOW(poster_top, 0); SHOW(poster_fade, 0); SHOW(np_scrim, 0);

    ring = lv_arc_create(scr);
    lv_obj_set_size(ring, ARC_D, ARC_D);
    lv_obj_align(ring, LV_ALIGN_CENTER, 0, 0);
    lv_arc_set_range(ring, 0, 1000);
    lv_arc_set_value(ring, 0);
    lv_arc_set_bg_angles(ring, 0, ARC_SWEEP);
    lv_arc_set_rotation(ring, ARC_ROT);
    lv_arc_set_mode(ring, LV_ARC_MODE_NORMAL);
    lv_obj_set_style_arc_width(ring, 6, LV_PART_MAIN);
    lv_obj_set_style_arc_color(ring, TC(SURFACE_RAISED), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(ring, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_arc_width(ring, 6, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(ring, accent, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(ring, LV_OPA_COVER, LV_PART_INDICATOR);
    /* Poster: volume shown on the bottom rim (display-only - the hardware buttons set it, and a touch
     * arc here would fight the page swipe) */
    vol_arc = lv_arc_create(scr);
    lv_obj_remove_style(vol_arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(vol_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(vol_arc, ARC_D, ARC_D);
    lv_obj_align(vol_arc, LV_ALIGN_CENTER, 0, 0);
    lv_arc_set_rotation(vol_arc, 0);
    lv_arc_set_bg_angles(vol_arc, 52, 128);
    lv_arc_set_mode(vol_arc, LV_ARC_MODE_REVERSE);
    lv_arc_set_range(vol_arc, 0, VOL_MAX);
    lv_obj_set_style_arc_width(vol_arc, 4, LV_PART_MAIN);
    lv_obj_set_style_arc_color(vol_arc, TC(SURFACE_RAISED), LV_PART_MAIN);
    lv_obj_set_style_arc_width(vol_arc, 4, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(vol_arc, accent, LV_PART_INDICATOR);
    SHOW(vol_arc, 0);
    /* draggable seek: a small knob thumb + scrub handler */
    lv_obj_set_style_bg_color(ring, TC(FIXED_MEDIA_WHITE), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(ring, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_pad_all(ring, 5, LV_PART_KNOB);
    lv_obj_clear_flag(ring, LV_OBJ_FLAG_SCROLLABLE);
    /* display-only: seek is driven by the NP recognizer (ui_np_seek_*) fed from
     * main.c's raw touch loop, so nav swipes / drawer pulls never grab the arc */
    lv_obj_clear_flag(ring, LV_OBJ_FLAG_CLICKABLE);

    cover = lv_obj_create(scr);
    lv_obj_remove_style_all(cover);
    lv_obj_set_size(cover, COVER_D, COVER_D);
    lv_obj_align(cover, LV_ALIGN_TOP_MID, 0, 42);
    lv_obj_set_style_radius(cover, 14, LV_PART_MAIN);
    lv_obj_set_style_clip_corner(cover, true, LV_PART_MAIN);
    lv_obj_set_style_bg_color(cover, TC(ART_PLACEHOLDER), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(cover, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(cover, 0, LV_PART_MAIN);
    lv_obj_clear_flag(cover, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(cover, LV_OBJ_FLAG_CLICKABLE);   /* tap art -> Song Info */
    lv_obj_add_event_cb(cover, cover_click_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(cover, cover_click_cb, LV_EVENT_CLICKED, NULL);

    cover_img = lv_image_create(cover);
    lv_obj_set_size(cover_img, COVER_D, COVER_D);
    lv_obj_center(cover_img);
    /* CENTER (not STRETCH): the BMP is already COVER_D, and STRETCH forces the
     * pivot to (0,0) which would break the vinyl spin. Pivot at centre. */
    lv_image_set_inner_align(cover_img, LV_IMAGE_ALIGN_CENTER);
    lv_image_set_pivot(cover_img, COVER_D/2, COVER_D/2);
    lv_obj_add_flag(cover_img, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(cover_img, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    cover_note = lv_label_create(cover);
    style_text(cover_note, TF(UI_28), TC(FIXED_MEDIA_WHITE));
    lv_label_set_text(cover_note, LV_SYMBOL_AUDIO);
    lv_obj_set_style_text_opa(cover_note, LV_OPA_90, LV_PART_MAIN);
    lv_obj_center(cover_note);
    lv_obj_clear_flag(cover_note, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    /* vinyl centre label: accent disc with a small spindle hole, on top of the
     * art. Shown only in Vinyl style. */
    spindle = lv_obj_create(cover);
    lv_obj_remove_style_all(spindle);
    lv_obj_set_size(spindle, 34, 34);
    lv_obj_center(spindle);
    lv_obj_set_style_radius(spindle, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(spindle, accent, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(spindle, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(spindle, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *hole = lv_obj_create(spindle);
    lv_obj_remove_style_all(hole);
    lv_obj_set_size(hole, 8, 8);
    lv_obj_center(hole);
    lv_obj_set_style_radius(hole, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(hole, TC(SCRIM), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(hole, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_add_flag(spindle, LV_OBJ_FLAG_HIDDEN);

    /* The Playback (tuning) and Options menus moved off this screen - they're
     * reached by a right-edge swipe (-> SCR_NPHUB); back is a left-edge swipe.
     * The seek ring fills the screen, so the ring previews a seek anywhere you
     * drag, but the actual seek is only COMMITTED on release if the gesture
     * wasn't a deliberate edge swipe (see ui_seek_commit/ui_seek_cancel, driven
     * by the main loop). So seek works everywhere except a bezel-edge flick. */

    /* Favorites heart - upper-right, just outside the cover's right edge */
    btn_fav = lv_button_create(scr);
    lv_obj_remove_style_all(btn_fav);
    lv_obj_set_size(btn_fav, 44, 44);
    lv_obj_align(btn_fav, LV_ALIGN_TOP_MID, 118, 126);
    lv_obj_add_flag(btn_fav, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(btn_fav, fav_click_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(btn_fav, fav_click_cb, LV_EVENT_CLICKED, NULL);
    fav_icon = lv_label_create(btn_fav);
    lv_obj_set_style_text_font(fav_icon, TF(ICON_28), LV_PART_MAIN);
    lv_obj_set_style_text_color(fav_icon, TC(TEXT_MUTED), LV_PART_MAIN);
    lv_label_set_text(fav_icon, HEART_OUTLINE);
    lv_obj_center(fav_icon);
    lv_obj_add_flag(btn_fav, LV_OBJ_FLAG_HIDDEN);   /* shown once a track loads */

    /* Audiobook sleep-timer moon - occupies the heart slot for books (heart is hidden for them). */
    btn_sleep = lv_button_create(scr);
    lv_obj_remove_style_all(btn_sleep);
    lv_obj_set_size(btn_sleep, 44, 44);
    lv_obj_align(btn_sleep, LV_ALIGN_TOP_MID, 118, 126);
    lv_obj_add_flag(btn_sleep, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(btn_sleep, sleep_click_cb, LV_EVENT_CLICKED, NULL);
    sleep_icon = lv_label_create(btn_sleep);
    lv_obj_set_style_text_font(sleep_icon, TF(ICON_28), LV_PART_MAIN);
    lv_obj_set_style_text_color(sleep_icon, TC(TEXT_MUTED), LV_PART_MAIN);
    lv_label_set_text(sleep_icon, MOON_ICON);
    lv_obj_center(sleep_icon);
    lv_obj_add_flag(btn_sleep, LV_OBJ_FLAG_HIDDEN);   /* shown only for audiobooks */

    /* Play-mode toggle - left side, mirrors the heart */
    btn_mode = lv_button_create(scr);
    lv_obj_remove_style_all(btn_mode);
    lv_obj_set_size(btn_mode, 44, 44);
    lv_obj_align(btn_mode, LV_ALIGN_TOP_MID, -118, 126);
    lv_obj_add_flag(btn_mode, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(btn_mode, mode_click_cb, LV_EVENT_PRESSED, NULL);
    btn_queue = lv_button_create(scr);                      /* the Queue: a playlist icon with a count */
    lv_obj_remove_style_all(btn_queue);
    lv_obj_set_size(btn_queue, 44, 40);
    lv_obj_set_ext_click_area(btn_queue, 4);
    ui_on(btn_queue, queue_click_cb, LV_EVENT_SHORT_CLICKED, NULL, "np.queue", UI_REACHABLE);
    ui_on(btn_queue, queue_long_cb, LV_EVENT_LONG_PRESSED, NULL, "np.queue.long", UI_REACHABLE);
    { lv_obj_t *ql = lv_label_create(btn_queue); lv_label_set_text(ql, LV_SYMBOL_LIST);
      lv_obj_set_style_text_font(ql, TF(UI_18), 0); lv_obj_set_style_text_color(ql, TC(ON_ACCENT), 0); lv_obj_center(ql); }
    queue_badge = lv_obj_create(btn_queue);
    lv_obj_remove_style_all(queue_badge);
    lv_obj_set_size(queue_badge, 17, 17);
    lv_obj_align(queue_badge, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_set_style_radius(queue_badge, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(queue_badge, TC(ACCENT_PRIMARY), 0);
    lv_obj_set_style_bg_opa(queue_badge, LV_OPA_COVER, 0);
    lv_obj_clear_flag(queue_badge, LV_OBJ_FLAG_CLICKABLE);
    queue_badge_lbl = lv_label_create(queue_badge);
    lv_obj_set_style_text_font(queue_badge_lbl, TF(UI_10), 0);
    lv_obj_set_style_text_color(queue_badge_lbl, TC(ON_ACCENT), 0);
    lv_obj_center(queue_badge_lbl);
    lv_obj_add_flag(queue_badge, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(btn_mode, mode_click_cb, LV_EVENT_CLICKED, NULL);
    mode_icon = lv_label_create(btn_mode);
    lv_obj_set_style_text_font(mode_icon, TF(ICON_28), LV_PART_MAIN);
    lv_label_set_text(mode_icon, MODE_ARROW);
    lv_obj_center(mode_icon);
    mode_one = lv_label_create(btn_mode);
    lv_obj_set_style_text_font(mode_one, TF(UI_10), LV_PART_MAIN);
    lv_obj_set_style_text_color(mode_one, TC(TEXT_PRIMARY), LV_PART_MAIN);
    lv_label_set_text(mode_one, "1");
    lv_obj_center(mode_one);   /* sits between the loop arrows */
    lv_obj_add_flag(mode_one, LV_OBJ_FLAG_HIDDEN);
    mode_refresh(cfg_get_int("work_mode", 0));

    /* Poster: immersive (spinning cover + synced lyrics) shortcut, left side opposite the heart */
    btn_imm = lv_button_create(scr);
    lv_obj_remove_style_all(btn_imm);
    lv_obj_set_size(btn_imm, 44, 44);
    lv_obj_align(btn_imm, LV_ALIGN_TOP_MID, -142, 144);
    lv_obj_add_event_cb(btn_imm, imm_btn_cb, LV_EVENT_CLICKED, NULL);
    {   lv_obj_t *g = lv_label_create(btn_imm);                         /* theme: one icon family */
        lv_label_set_text(g, TH_IC_RECORD);
        lv_obj_set_style_text_font(g, &font_theme_20, 0);
        lv_obj_set_style_text_color(g, TC(TEXT_PRIMARY), 0);
        lv_obj_center(g); lv_obj_clear_flag(g, LV_OBJ_FLAG_CLICKABLE); }
    SHOW(btn_imm, 0);

    title_sh = lv_label_create(scr);                                 /* poster drop shadows (behind) */
    artist_sh = lv_label_create(scr);
    for(int k = 0; k < 2; k++){ lv_obj_t *o = k ? artist_sh : title_sh;
        lv_obj_set_style_text_color(o, TC(SCRIM), 0); lv_obj_set_style_text_opa(o, 170, 0);
        lv_obj_set_style_text_align(o, LV_TEXT_ALIGN_CENTER, 0); lv_label_set_text(o, "");
        lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE); lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN); }

    title = lv_label_create(scr);
    lv_obj_set_width(title, 260);
    style_text(title, &s_font20, TC(TEXT_PRIMARY));
    lv_label_set_long_mode(title, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(title, "No Track");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 198);

    artist = lv_label_create(scr);
    lv_obj_set_width(artist, 238);
    style_text(artist, &s_font16, TC(TEXT_LYRICS));
    lv_label_set_long_mode(artist, LV_LABEL_LONG_DOT);
    lv_label_set_text(artist, "");
    lv_obj_align(artist, LV_ALIGN_TOP_MID, 0, 226);

    album = lv_label_create(scr);
    lv_obj_set_width(album, 218);
    style_text(album, &s_font14, TC(TEXT_MUTED));
    lv_label_set_long_mode(album, LV_LABEL_LONG_DOT);
    lv_label_set_text(album, "");
    lv_obj_align(album, LV_ALIGN_TOP_MID, 0, 246);

    /* Poster: tap anywhere on the title / artist block to open this song's album, highlighted */
    np_mid_btn = lv_button_create(scr);
    lv_obj_remove_style_all(np_mid_btn);
    lv_obj_set_size(np_mid_btn, 236, 108);
    lv_obj_align(np_mid_btn, LV_ALIGN_TOP_MID, 0, 140);
    lv_obj_add_event_cb(np_mid_btn, np_mid_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(np_mid_btn, LV_OBJ_FLAG_HIDDEN);

    btn_prev = lv_label_create(scr);
    style_text(btn_prev, TF(UI_28), TC(TEXT_MUTED));
    lv_label_set_text(btn_prev, LV_SYMBOL_PREV);
    lv_obj_align(btn_prev, LV_ALIGN_TOP_MID, -60, 284);

    btn_pp = lv_label_create(scr);
    style_text(btn_pp, TF(UI_32), accent);
    lv_label_set_text(btn_pp, LV_SYMBOL_PLAY);
    lv_obj_align(btn_pp, LV_ALIGN_TOP_MID, 0, 280);

    btn_next = lv_label_create(scr);
    style_text(btn_next, TF(UI_28), TC(TEXT_MUTED));
    lv_label_set_text(btn_next, LV_SYMBOL_NEXT);
    lv_obj_align(btn_next, LV_ALIGN_TOP_MID, 60, 284);

    make_clickable(btn_prev, "0201000C0002");
    make_clickable(btn_pp,   "0201000C0000");
    make_clickable(btn_next, "0201000C0001");

    /* Times sit as a centered pair below the transport buttons, inside the
     * arc's bottom gap so they never clash with the green progress fill. */
    t_elapsed = lv_label_create(scr);
    lv_obj_set_width(t_elapsed, 80);   /* fits -999:59:59 at Montserrat 14 */
    style_text(t_elapsed, TF(UI_14), TC(TEXT_MUTED));
    lv_label_set_text(t_elapsed, "0:00");
    lv_obj_align(t_elapsed, LV_ALIGN_TOP_MID, -46, 318);

    t_sep = lv_label_create(scr);
    style_text(t_sep, TF(UI_14), TC(SURFACE_RAISED));
    lv_label_set_text(t_sep, "/");
    lv_obj_align(t_sep, LV_ALIGN_TOP_MID, 0, 318);

    t_remain = lv_label_create(scr);
    lv_obj_set_width(t_remain, 80);   /* fits -999:59:59 at Montserrat 14 */
    style_text(t_remain, TF(UI_14), TC(TEXT_MUTED));
    lv_label_set_text(t_remain, "0:00");
    lv_obj_align(t_remain, LV_ALIGN_TOP_MID, 46, 318);

    /* page dots - Now Playing is the left page, the hub (right swipe) the right */
    for(int i=0;i<2;i++){
        lv_obj_t *dot = lv_obj_create(scr);
        np_dots[i] = dot;
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, 7, 7);
        lv_obj_align(dot, LV_ALIGN_BOTTOM_MID, i==0 ? -8 : 8, -14);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_bg_color(dot, i==0 ? TC(TEXT_PRIMARY) : TC(SURFACE_RAISED), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(dot, i==0 ? LV_OPA_COVER : LV_OPA_60, LV_PART_MAIN);
    }

    /* ---- full-screen album-art overlay (hidden until the cover is tapped) ---- */
    fsart = lv_obj_create(scr);
    kit_media_scope(fsart);
    lv_obj_remove_style_all(fsart);
    lv_obj_set_size(fsart, 360, 360);
    lv_obj_set_pos(fsart, 0, 0);
    lv_obj_set_style_bg_color(fsart, TC(FIXED_MEDIA_BLACK), 0);
    lv_obj_set_style_bg_opa(fsart, LV_OPA_COVER, 0);
    lv_obj_add_flag(fsart, LV_OBJ_FLAG_CLICKABLE);          /* tap anywhere -> close */
    lv_obj_clear_flag(fsart, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(fsart, fsart_click_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(fsart, LV_OBJ_FLAG_HIDDEN);

    fsart_img = lv_image_create(fsart);                    /* the 364px stock cover, centred (bezel crops) */
    lv_obj_center(fsart_img);
    lv_obj_clear_flag(fsart_img, LV_OBJ_FLAG_CLICKABLE);
    /* immersive: the RAM-decoded cover, scaled to fill the screen and slowly spun like a record
     * (a file-sourced image can't be rotated by the SW renderer - see load_cover_dsc) */
    imm_spin = lv_image_create(fsart);
    lv_obj_center(imm_spin);
    lv_obj_clear_flag(imm_spin, LV_OBJ_FLAG_CLICKABLE);
    lv_image_set_pivot(imm_spin, CBMP / 2, CBMP / 2);
    lv_image_set_scale(imm_spin, (uint32_t)(364 * 256 / CBMP));
    lv_image_set_antialias(imm_spin, false);               /* cheaper per frame; soft art hides it */
    lv_obj_add_flag(imm_spin, LV_OBJ_FLAG_HIDDEN);
    for(int g = 0; g < 4; g++){                            /* faint grooves (rotation-symmetric: never redrawn) */
        lv_obj_t *gr = lv_obj_create(fsart); lv_obj_remove_style_all(gr); imm_deco[g] = gr;
        int d = 340 - g * 44; lv_obj_set_size(gr, d, d); lv_obj_center(gr);
        lv_obj_set_style_radius(gr, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(gr, 1, 0); lv_obj_set_style_border_color(gr, TC(FIXED_MEDIA_BLACK), 0);
        lv_obj_set_style_border_opa(gr, 40, 0); lv_obj_clear_flag(gr, LV_OBJ_FLAG_CLICKABLE);
    }
    {   lv_obj_t *lbl = lv_obj_create(fsart); lv_obj_remove_style_all(lbl); imm_deco[4] = lbl;   /* label + spindle */
        lv_obj_set_size(lbl, 54, 54); lv_obj_center(lbl); lv_obj_clear_flag(lbl, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_radius(lbl, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(lbl, TC(FIXED_MEDIA_LABEL), 0); lv_obj_set_style_bg_opa(lbl, 235, 0);
        lv_obj_t *h = lv_obj_create(fsart); lv_obj_remove_style_all(h); imm_deco[5] = h;
        lv_obj_set_size(h, 8, 8); lv_obj_center(h); lv_obj_clear_flag(h, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_radius(h, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(h, TC(FIXED_MEDIA_HUB), 0); lv_obj_set_style_bg_opa(h, LV_OPA_COVER, 0); }
    imm_prog = lv_arc_create(fsart);                       /* thin progress on the top rim */
    lv_obj_remove_style(imm_prog, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(imm_prog, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(imm_prog, 344, 344); lv_obj_center(imm_prog);
    lv_arc_set_rotation(imm_prog, 208); lv_arc_set_bg_angles(imm_prog, 0, 124); lv_arc_set_range(imm_prog, 0, 1000);
    lv_obj_set_style_arc_width(imm_prog, 3, LV_PART_MAIN); lv_obj_set_style_arc_color(imm_prog, TC(FIXED_MEDIA_TRACK), LV_PART_MAIN);
    lv_obj_set_style_arc_width(imm_prog, 3, LV_PART_INDICATOR); lv_obj_set_style_arc_color(imm_prog, accent, LV_PART_INDICATOR);

    /* bottom scrim so title/artist stay legible over any cover */
    lv_obj_t *scrim = lv_obj_create(fsart);
    imm_scrim = scrim;
    lv_obj_remove_style_all(scrim);
    lv_obj_set_size(scrim, 360, 170);
    lv_obj_align(scrim, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_main_stop(scrim, 0, 0);
    lv_obj_set_style_bg_grad_stop(scrim, 140, 0);
    lv_obj_set_style_bg_color(scrim, TC(FIXED_MEDIA_BLACK), 0);
    lv_obj_set_style_bg_grad_color(scrim, TC(FIXED_MEDIA_BLACK), 0);
    lv_obj_set_style_bg_grad_dir(scrim, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_main_opa(scrim, LV_OPA_TRANSP, 0);  /* transparent at top */
    lv_obj_set_style_bg_grad_opa(scrim, 205, 0);
    lv_obj_set_style_bg_opa(scrim, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scrim, LV_OBJ_FLAG_CLICKABLE);

    /* synced lyrics: previous (dim) / current (bright) / next (dim). They wrap instead of being cut, and
     * the block is anchored to the bottom, so a long current line pushes the previous one up. */
    for(int j = 0; j < 3; j++){
        imm_lyr[j] = lv_label_create(fsart);
        lv_obj_set_width(imm_lyr[j], j == 1 ? 272 : (j == 0 ? 250 : 226));  /* the circle narrows downward */
        style_text(imm_lyr[j], j == 1 ? &s_font24 : &s_font18, j == 1 ? TC(FIXED_MEDIA_LYRIC_CURRENT) : TC(FIXED_MEDIA_LYRIC_ADJACENT));
        lv_obj_set_style_text_align(imm_lyr[j], LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(imm_lyr[j], LV_LABEL_LONG_WRAP);             /* reflow, never cut mid-line */
        lv_obj_set_style_max_height(imm_lyr[j], j == 1 ? 4 * 26 : 2 * 21, 0);   /* current up to 4 lines */
        lv_label_set_text(imm_lyr[j], "");
        lv_obj_clear_flag(imm_lyr[j], LV_OBJ_FLAG_CLICKABLE);
    }
    imm_hint = lv_label_create(fsart);
    style_text(imm_hint, &s_font14, TC(FIXED_MEDIA_HINT));
    lv_label_set_text(imm_hint, "No synced lyrics for this track");
    lv_obj_align(imm_hint, LV_ALIGN_TOP_MID, 0, 266);
    lv_obj_clear_flag(imm_hint, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(imm_hint, LV_OBJ_FLAG_HIDDEN);

    fsart_title = lv_label_create(fsart);
    lv_obj_set_width(fsart_title, 200);   /* two rows under the top arch, like Home */
    style_text(fsart_title, &s_font18, TC(FIXED_MEDIA_TITLE));
    lv_obj_set_style_text_align(fsart_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(fsart_title, LV_LABEL_LONG_DOT);
    lv_obj_align(fsart_title, LV_ALIGN_TOP_MID, 0, 50);   /* clear of the arch: at x=+-100 it sits at y~40 */
    lv_obj_clear_flag(fsart_title, LV_OBJ_FLAG_CLICKABLE);

    fsart_artist = lv_label_create(fsart);
    lv_obj_set_width(fsart_artist, 190);
    style_text(fsart_artist, &s_font16, TC(FIXED_MEDIA_ARTIST));
    lv_obj_set_style_text_align(fsart_artist, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(fsart_artist, LV_LABEL_LONG_DOT);
    lv_obj_align(fsart_artist, LV_ALIGN_TOP_MID, 0, 75);
    lv_obj_clear_flag(fsart_artist, LV_OBJ_FLAG_CLICKABLE);

    apply_accent();
    ui_set_np_style(cfg_get_int("np_style", 0));
}

static void np_shadow_sync(void){                                    /* keep the drop shadows' text in step */
    if(!g_np_poster) return;
    if(title_sh && strcmp(lv_label_get_text(title_sh), lv_label_get_text(title))) lv_label_set_text(title_sh, lv_label_get_text(title));
    if(artist_sh && strcmp(lv_label_get_text(artist_sh), lv_label_get_text(artist))) lv_label_set_text(artist_sh, lv_label_get_text(artist));
}
void ui_update(const track_state_t *st)
{
    if(st) g_np_is_playing = (st->state == 2);        /* main.c normalises state before calling us */
    np_shadow_sync();
    if(vol_arc && g_np_poster && st && st->volume != imm_vol_last){ imm_vol_last = st->volume; lv_arc_set_value(vol_arc, st->volume); }
    char elapsed_buf[12];   /* room for H:MM:SS (audiobook-length) - mmss now emits hours */
    char remain_core[12];
    char remain_buf[14];    /* "-" + H:MM:SS */
    long dur;
    long pos;
    long remain;
    int32_t progress;

    mode_refresh(cfg_get_int("work_mode", 0));   /* keep in sync with Tune menu */

    if(st == NULL || !st->have_track) {
        last_path[0] = '\0';
        g_stat_path[0] = '\0';   /* clear so a later replay of the same track counts as a new play */
        update_accent_seed(st);   /* no-track -> neutral light grey (resets seed + applies once via the gate) */

        set_label_text_changed(title, "No Track");
        set_label_text_changed(artist, "");
        set_label_text_changed(album, "");
        set_label_text_changed(t_elapsed, "0:00");
        set_label_text_changed(t_remain, "0:00");
        set_label_text_changed(btn_pp, LV_SYMBOL_PLAY);
        art_request_clear();   /* clear art + invalidate any in-flight decode (was a bare fallback) */
        g_np_curpath[0] = '\0'; g_favp_active = 0;   /* no track -> drop any optimistic-favourite hold */
        fav_refresh(0, 0);
        if(btn_sleep) lv_obj_add_flag(btn_sleep, LV_OBJ_FLAG_HIDDEN);   /* no track -> hide the sleep moon too */
        ui_np_close_overlays();   /* no track -> dismiss any open sleep popover */
        g_track_dur = 0;
        set_progress_changed(0);
        return;
    }

    update_cover_for_path(st);   /* first: invalidates stale RAM art on a new album */
    update_accent_seed(st);      /* then: art-accent if THIS track's cover is ready, else text-hash */

    /* count a play once per new track (diskOS play history -> Most-Played / Recently-Played), but
     * only while actually PLAYING (st->state==2). Otherwise a boot-seeded paused track, or skipping
     * through tracks while paused, would inflate play counts + recency for songs never really played. */
    if(st->state==2 && st->path[0] && strcmp(g_stat_path, st->path) != 0){
        copy_cstr(g_stat_path, sizeof g_stat_path, st->path);          /* track identity always (so a music replay after a book still counts) */
        if(!mdb_is_book_path(st->path)) mdb_record_play(st->path);     /* but audiobooks are excluded from play history */
    }

    { const char *nt = st->title[0] ? st->title : "Untitled";
      int changed = strcmp(lv_label_get_text(title), nt) != 0;
      set_label_text_changed(title, nt);
      if(changed) ui_np_rescroll(); }                      /* a new track may scroll again */
    set_label_text_changed(artist, st->artist[0] ? st->artist : "");
    /* keep the full-screen art view fresh while it's open: text every update, image only on a
     * genuine track change (a PNG decode is too costly to do every position tick). */
    if(fsart_on){
        fsart_refresh_text();
        if(strcmp(fsart_path, st->path) != 0){ copy_cstr(fsart_path, sizeof fsart_path, st->path); fsart_reload_img(); }
    }
    /* Secondary line. For a BOOK it shows the current chapter (an album tag is meaningless for an
     * audiobook, and the all-songs queue position is noise); for music it's the album, carrying the
     * track position as "Album · N/M". */
    {
        char ab[240];
        if(!book_meta_line(st, ab, sizeof ab)){        /* not a book -> album + position */
            const char *alb = st->album;
            if(alb[0] && st->playing_num[0])
                snprintf(ab, sizeof ab, "%s  \xC2\xB7  %s", alb, st->playing_num);          /* Album · 3/19 */
            else if(alb[0])
                snprintf(ab, sizeof ab, "%s", alb);                                         /* Album */
            else if(st->playing_num[0]){
                const char *sl = strchr(st->playing_num, '/');
                if(sl && sl[1]) snprintf(ab, sizeof ab, "%.*s / %s", (int)(sl - st->playing_num), st->playing_num, sl + 1);  /* 3 / 19 */
                else            snprintf(ab, sizeof ab, "%s", st->playing_num);
            } else ab[0] = '\0';
        }
        set_label_text_changed(album, ab);
    }
    /* honour an optimistic-favourite hold so a1 position frames don't bounce the heart back */
    snprintf(g_np_curpath, sizeof g_np_curpath, "%s", st->path);
    int fav_show = st->is_favorite;
    if(g_favp_active){
        if(strcmp(g_favp_path, st->path) != 0)          g_favp_active = 0;   /* different track -> drop hold */
        else if(st->is_favorite == g_favp_val)          g_favp_active = 0;   /* player confirmed the value */
        else if(lv_tick_elaps(g_favp_set) > 4000)       g_favp_active = 0;   /* no confirm in 4s -> give up */
        else                                            fav_show = g_favp_val;
    }
    fav_refresh(fav_show, 1);

    /* Audiobook transport: -15/+30 skips instead of track prev/next, and no shuffle or favourite
     * (they make no sense for a book; the freed space is reserved for the sleep control later). */
    {
        int is_book = mdb_is_book_path(st->path);
        np_transport_glyphs(is_book);
        if(is_book){
            lv_obj_add_flag(btn_fav, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(btn_mode, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(btn_sleep, LV_OBJ_FLAG_HIDDEN);   /* moon replaces the heart for books */
            int armed = ui_sleep_state(NULL);                   /* accent the moon while a timer is set */
            lv_obj_set_style_text_color(sleep_icon, armed ? accent : TC(TEXT_MUTED), LV_PART_MAIN);
        } else {
            lv_obj_remove_flag(btn_mode, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(btn_sleep, LV_OBJ_FLAG_HIDDEN);
            ui_np_close_overlays();   /* not a book -> dismiss any open sleep popover (its context is gone) */
        }
    }

    dur = st->duration_ms;
    g_track_dur = dur;
    pos = st->position_ms;
    if(dur < 0) dur = 0;
    if(pos < 0) pos = 0;
    if(dur > 0 && pos > dur) pos = dur;

    /* Seek/progress window: a book's ring + times track the CURRENT CHAPTER (the whole-book
     * ring is unusable - 1 degree can be minutes on a long book). Music uses the whole track. */
    if(!book_chapter_window(st, &g_seek_lo, &g_seek_hi)){ g_seek_lo = 0; g_seek_hi = dur; }
    long span = g_seek_hi - g_seek_lo;
    long rel  = pos - g_seek_lo;
    if(rel < 0) rel = 0;
    if(span > 0 && rel > span) rel = span;

    progress = (span > 0) ? (int32_t)(((long long)rel * 1000LL) / (long long)span) : 0;

    /* seek echo-suppression: while the post-seek hold is active and the player
     * is still streaming a stale (far-from-target) position, keep the arc and
     * times pinned at the seeked target instead of snapping back. */
    if(g_seek_hold_until) {
        long d = pos - g_seek_target_ms; if(d < 0) d = -d;   /* ABSOLUTE ms gap - unaffected by a chapter-window flip at a seek-to-boundary */
        if(lv_tick_get() < g_seek_hold_until && d > 3000) {
            ui_pp_glyph(btn_pp,ui_pp_icon_playing(st->state==2));
            return;   /* ignore this stale echo */
        }
        g_seek_hold_until = 0; g_seek_target_ms = -1;   /* caught up or window lapsed */
    }

    mmss(rel, elapsed_buf, sizeof(elapsed_buf));   /* elapsed within the window (chapter for a book) */
    set_label_text_changed(t_elapsed, elapsed_buf);

    if(span > 0) {
        remain = span - rel;
        if(remain < 0) remain = 0;
        mmss(remain, remain_core, sizeof(remain_core));
        snprintf(remain_buf, sizeof(remain_buf), "-%s", remain_core);
    } else {
        snprintf(remain_buf, sizeof(remain_buf), "0:00");
    }
    set_label_text_changed(t_remain, remain_buf);
    set_progress_changed(progress);

    ui_pp_glyph(btn_pp,ui_pp_icon_playing(st->state==2));
}

/* ---- volume overlay: a draggable arc on lv_layer_top (shows over any screen).
 * It appears on a hardware-volume change (a714) and is also touch-adjustable -
 * dragging the arc calls ui_set_volume() so you can set the level on-screen
 * instead of hammering the buttons (which double-press into next/prev). */
static lv_obj_t *g_vol_panel, *g_vol_arc, *g_vol_num;
static lv_timer_t *g_vol_timer;
static int g_vol_suppress;   /* 1 while we set the arc programmatically (no echo loop) */
static uint32_t g_vol_last_send;  /* throttle: tick of the last 0715 we sent */
static int g_vol_pending;         /* 1 if a value changed but was throttled (commit on release) */
static void vol_hide_cb(lv_timer_t *t){
    if(g_vol_panel) lv_obj_add_flag(g_vol_panel, LV_OBJ_FLAG_HIDDEN);
    lv_timer_pause(t);
}
/* Throttle volume commits to <=1 per 120ms while dragging (the arc fires
 * VALUE_CHANGED every step - sending each one hammers the player). The on-screen
 * number tracks live; the final value is always committed on RELEASED. */
static lv_obj_t *g_vol_needle; static lv_point_precise_t g_vol_np[2];
static void vol_needle(int v){                               /* Braun: an orange line across the band at the level */
    if(!g_vol_needle) return;
    if(v < 0) v = 0; if(v > VOL_MAX) v = VOL_MAX;
    float an = (48.0f - 96.0f * v / VOL_MAX) * 0.0174533f;    /* 0 at the bottom end, 120 at the top (the scale's 48..-48 degrees) */
    g_vol_np[0].x = 180 + 131 * cosf(an); g_vol_np[0].y = 180 + 131 * sinf(an);
    g_vol_np[1].x = 180 + 169 * cosf(an); g_vol_np[1].y = 180 + 169 * sinf(an);
    lv_line_set_points(g_vol_needle, g_vol_np, 2);
}
static void dvol_paint(int v);
static void vol_arc_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if(g_vol_suppress) return;                       /* programmatic update, ignore */
    int v = lv_arc_get_value(g_vol_arc);
    dvol_paint(v);
    char b[12]; snprintf(b, sizeof b, "%d", v); lv_label_set_text(g_vol_num, b);
    vol_needle(v);
    if(code == LV_EVENT_VALUE_CHANGED){
        if(lv_tick_elaps(g_vol_last_send) >= 120){
            ui_set_volume(v); g_vol_last_send = lv_tick_get(); g_vol_pending = 0;
        } else {
            g_vol_pending = 1;                       /* defer to release */
        }
        if(g_vol_timer){ lv_timer_reset(g_vol_timer); lv_timer_resume(g_vol_timer); }
    } else if(code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST){
        /* commit the throttled final value on release OR press-lost (LVGL clears
         * LV_STATE_PRESSED on both - without this a deferred value never sends). */
        if(g_vol_pending){ ui_set_volume(v); g_vol_pending = 0; g_vol_last_send = lv_tick_get(); }
    }
}
/* Disco: the hub field pops in with the value; the right-rim arc fills in the CD rainbow (or the accent: Disco Options >
 * Volume). The rainbow is ten fixed segments over the track, each shown up to the level; the track arc underneath still
 * takes the drag. The whole overlay sits on the top layer and closes on a tap anywhere off the arc. */
#define DV_N 10
#define DV_A0 40                                       /* bottom end (fills upward) */
#define DV_SPAN 80
static lv_obj_t *g_dv_seg[DV_N], *g_dv_cap;
static void dvol_paint(int v){
    if(!g_dv_seg[0]) return;
    static const uint32_t IRI[DV_N] = { 0xFF5AA0, 0xFF8A78, 0xFFB45A, 0xFFE070, 0x9AF07A, 0x62F2B0, 0x4AD8FF, 0x5AA8FF, 0x7A8CFF, 0xB36BFF };
    int acc = cfg_get_int("disco_vol", 0) == 1;
    float lvl = DV_A0 - DV_SPAN * (float)v / VOL_MAX;   /* the level's angle */
    for(int k = 0; k < DV_N; k++){
        float s0 = DV_A0 - DV_SPAN * (k + 1) / (float)DV_N, s1 = DV_A0 - DV_SPAN * k / (float)DV_N;   /* s0 above s1 */
        if(lvl >= s1 - 0.01f){ lv_obj_add_flag(g_dv_seg[k], LV_OBJ_FLAG_HIDDEN); continue; }
        float st = lvl > s0 ? lvl : s0;
        int a = (int)lroundf(st), b = (int)lroundf(s1) + (k ? 1 : 0);
        lv_arc_set_angles(g_dv_seg[k], (a + 360) % 360, (b + 360) % 360);
        lv_obj_set_style_arc_color(g_dv_seg[k], acc ? ui_current_accent() : theme_color_from_rgb(IRI[k]), LV_PART_INDICATOR);
        lv_obj_remove_flag(g_dv_seg[k], LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_set_style_bg_color(g_dv_cap, acc ? ui_current_accent() : theme_color_from_rgb(IRI[0]), 0);
    if(v > 0) lv_obj_remove_flag(g_dv_cap, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(g_dv_cap, LV_OBJ_FLAG_HIDDEN);
    int kc = v * DV_N / (VOL_MAX + 1);                                  /* the knob wears the colour at the level */
    lv_obj_set_style_bg_color(g_vol_arc, acc ? ui_current_accent() : theme_color_from_rgb(IRI[kc]), LV_PART_KNOB);
}
static void vol_tap_close_cb(lv_event_t *e){ (void)e; if(g_vol_panel) lv_obj_add_flag(g_vol_panel, LV_OBJ_FLAG_HIDDEN); if(g_vol_timer) lv_timer_pause(g_vol_timer); }
void ui_show_volume(int vol)
{
    if(!g_vol_panel){
        /* the rim arc: a level on the right rim filling bottom -> top in the current accent, rounded ends,
         * a white handle, and the value at clock size in a badge beside it. The screen stays visible,
         * lightly dimmed; only the arc and the number redraw while it's up. Drag the arc to set the level. */
        lv_obj_t *top = lv_layer_top();
        g_vol_panel = lv_obj_create(top);
        lv_obj_remove_style_all(g_vol_panel);
        lv_obj_set_size(g_vol_panel, 360, 360);
        lv_obj_set_style_bg_color(g_vol_panel, TC(SCRIM), 0);
        lv_obj_set_style_bg_opa(g_vol_panel, 115, 0);
        lv_obj_clear_flag(g_vol_panel, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);   /* taps pass through */

        g_vol_arc = lv_arc_create(g_vol_panel);
        lv_obj_set_size(g_vol_arc, 332, 332);
        lv_obj_center(g_vol_arc);
        lv_obj_add_flag(g_vol_arc, LV_OBJ_FLAG_ADV_HITTEST);     /* only the band itself is draggable */
        lv_arc_set_rotation(g_vol_arc, 0);
        lv_arc_set_bg_angles(g_vol_arc, 320, 40);                 /* the right rim */
        lv_arc_set_mode(g_vol_arc, LV_ARC_MODE_REVERSE);          /* fills from the bottom end upward */
        lv_arc_set_range(g_vol_arc, 0, VOL_MAX);
        lv_obj_set_style_arc_width(g_vol_arc, 12, LV_PART_MAIN);
        lv_obj_set_style_arc_width(g_vol_arc, 12, LV_PART_INDICATOR);
        lv_obj_set_style_arc_rounded(g_vol_arc, true, LV_PART_MAIN);
        lv_obj_set_style_arc_rounded(g_vol_arc, true, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(g_vol_arc, TC(VOLUME_TRACK), LV_PART_MAIN);
        lv_obj_set_style_arc_color(g_vol_arc, ui_current_accent(), LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(g_vol_arc, ui_current_accent(), LV_PART_KNOB);         /* white ring, accent dot */
        lv_obj_set_style_bg_opa(g_vol_arc, LV_OPA_COVER, LV_PART_KNOB);
        lv_obj_set_style_border_color(g_vol_arc, TC(TEXT_PRIMARY), LV_PART_KNOB);
        lv_obj_set_style_border_width(g_vol_arc, 3, LV_PART_KNOB);
        lv_obj_set_style_pad_all(g_vol_arc, 2, LV_PART_KNOB);
        lv_obj_add_event_cb(g_vol_arc, vol_arc_cb, LV_EVENT_VALUE_CHANGED, NULL);
        lv_obj_add_event_cb(g_vol_arc, vol_arc_cb, LV_EVENT_RELEASED, NULL);
        lv_obj_add_event_cb(g_vol_arc, vol_arc_cb, LV_EVENT_PRESS_LOST, NULL);

        lv_obj_t *badge = lv_obj_create(g_vol_panel);             /* speaker + value, clock-sized */
        lv_obj_remove_style_all(badge);
        lv_obj_set_size(badge, LV_SIZE_CONTENT, 60);
        lv_obj_set_style_radius(badge, 30, 0);
        lv_obj_set_style_bg_color(badge, TC(VOLUME_BADGE), 0);
        lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_hor(badge, 22, 0);
        lv_obj_set_style_pad_column(badge, 10, 0);
        lv_obj_set_flex_flow(badge, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(badge, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        lv_obj_align(badge, LV_ALIGN_RIGHT_MID, -46, 0);
        lv_obj_t *spk = lv_label_create(badge);
        lv_label_set_text(spk, LV_SYMBOL_VOLUME_MAX);
        lv_obj_set_style_text_font(spk, TF(UI_28), 0);
        lv_obj_set_style_text_color(spk, ui_current_accent(), 0);
        g_vol_num = lv_label_create(badge);
        lv_obj_set_style_text_font(g_vol_num, TF(UI_46), 0);
        lv_obj_set_style_text_color(g_vol_num, TC(TEXT_PRIMARY), 0);

        g_vol_timer = lv_timer_create(vol_hide_cb, 1800, NULL);
        lv_timer_pause(g_vol_timer);
        lv_obj_t *mid = lv_obj_create(g_vol_panel);               /* a tap in the middle closes it */
        lv_obj_remove_style_all(mid);
        lv_obj_set_size(mid, 220, 220);
        lv_obj_center(mid);
        lv_obj_set_style_radius(mid, LV_RADIUS_CIRCLE, 0);
        lv_obj_add_flag(mid, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_ADV_HITTEST);
        lv_obj_add_event_cb(mid, vol_tap_close_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_move_to_index(mid, 0);                             /* under the arc and the badge */
        if(th_braun()){
            /* Braun: a dark scale band along the right rim with light markings, an orange needle, and a
             * bigger dark number box. Same arc underneath, so dragging / auto-hide / tap-to-close are unchanged. */
            lv_obj_set_style_bg_color(g_vol_panel, TC(CANVAS), 0);
            lv_obj_set_style_bg_opa(g_vol_panel, 150, 0);
            lv_obj_set_size(g_vol_arc, 340, 340); lv_obj_center(g_vol_arc);
            lv_arc_set_bg_angles(g_vol_arc, 312, 48);                 /* exactly the scale: the needle lands on the marks */
            lv_obj_set_style_arc_width(g_vol_arc, 40, LV_PART_MAIN);
            lv_obj_set_style_arc_rounded(g_vol_arc, false, LV_PART_MAIN);
            int bdark = theme_variant() == 0;                        /* fork: Braun Dark - a charcoal band like the body, not aluminium */
            lv_obj_set_style_arc_color(g_vol_arc, bdark ? TC(SURFACE_RAISED) : TC(CONTROL_KNOB), LV_PART_MAIN);
            lv_obj_set_style_arc_opa(g_vol_arc, LV_OPA_TRANSP, LV_PART_INDICATOR);   /* no fill: a needle reads the level */
            lv_obj_set_style_bg_color(g_vol_arc, TC(ACCENT_PRIMARY), LV_PART_KNOB);
            lv_obj_set_style_border_color(g_vol_arc, TC(ACCENT_PRIMARY), LV_PART_KNOB);
            lv_obj_set_style_border_width(g_vol_arc, 0, LV_PART_KNOB);
            lv_obj_set_style_pad_all(g_vol_arc, -14, LV_PART_KNOB);                 /* a small orange dot on the band */
            /* the scale: 25 marks, every 4th long, 0 / 60 / 120 in light type */
            static lv_point_precise_t mk[25][2];
            for(int k = 0; k < 25; k++){
                float an = (48 - k * 4) * 0.0174533f; int major = (k % 4 == 0);
                float r1 = major ? 155 : 159, r2 = 166;
                mk[k][0].x = 180 + r1 * cosf(an); mk[k][0].y = 180 + r1 * sinf(an);
                mk[k][1].x = 180 + r2 * cosf(an); mk[k][1].y = 180 + r2 * sinf(an);
                lv_obj_t *l = lv_line_create(g_vol_panel); lv_line_set_points(l, mk[k], 2);
                lv_obj_set_style_line_width(l, major ? 2 : 1, 0);
                lv_obj_set_style_line_color(l, bdark ? (major ? TC(TEXT_PRIMARY) : TC(TEXT_MUTED)) : major ? TC(VOLUME_TICK_MAJOR) : TC(VOLUME_TICK_MINOR), 0);
                lv_obj_clear_flag(l, LV_OBJ_FLAG_CLICKABLE);
            }
            static const char *const LB[3] = { "0", "60", "120" }; static const int LK[3] = { 0, 12, 24 };
            for(int i = 0; i < 3; i++){
                float an = (48 - LK[i] * 4 + (i == 0 ? -5 : i == 2 ? 5 : 0)) * 0.0174533f;   /* the end labels a little inward, onto the band */
                lv_obj_t *l = br_label(g_vol_panel, LB[i], br_font(12, 0), theme_rgb(bdark ? THEME_CLR_TEXT_PRIMARY : THEME_CLR_VOLUME_TICK_MAJOR));
                lv_obj_align(l, LV_ALIGN_CENTER, (int32_t)(145 * cosf(an)), (int32_t)(145 * sinf(an)));   /* fully on the band */
            }
            /* the number box */
            lv_obj_t *badge = lv_obj_get_parent(g_vol_num);
            lv_obj_set_flex_flow(badge, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_size(badge, 118, 88);
            lv_obj_set_style_radius(badge, 20, 0);
            lv_obj_set_style_bg_color(badge, bdark ? TC(SURFACE_RAISED) : TC(CONTROL_KNOB), 0);
            lv_obj_set_style_border_color(badge, TC(CONTROL_FILL), 0); lv_obj_set_style_border_width(badge, 1, 0);
            lv_obj_set_style_pad_all(badge, 0, 0); lv_obj_set_style_pad_row(badge, 0, 0);
            lv_obj_align(badge, LV_ALIGN_CENTER, 13, 0);
            lv_obj_t *spk = lv_obj_get_child(badge, 0);
            lv_obj_add_flag(spk, LV_OBJ_FLAG_IGNORE_LAYOUT);
            lv_obj_set_style_text_font(spk, TF(UI_14), 0); lv_obj_set_style_text_color(spk, TC(ACCENT_PRIMARY), 0);
            lv_obj_align(spk, LV_ALIGN_TOP_LEFT, 12, 8);
            lv_obj_set_style_text_font(g_vol_num, br_font(44, 1), 0); lv_obj_set_style_text_color(g_vol_num, bdark ? TC(TEXT_PRIMARY) : TC(VOLUME_DIGITS), 0);
            lv_obj_t *cap = br_label(badge, "VOLUME", br_font(12, 0), theme_rgb(bdark ? THEME_CLR_TEXT_SECONDARY : THEME_CLR_VOLUME_CAPTION));
            (void)cap;
            g_vol_needle = lv_line_create(g_vol_panel);                /* the orange needle across the band */
            lv_obj_set_style_line_width(g_vol_needle, 3, 0); lv_obj_set_style_line_rounded(g_vol_needle, true, 0);
            lv_obj_set_style_line_color(g_vol_needle, TC(ACCENT_PRIMARY), 0);
            lv_obj_clear_flag(g_vol_needle, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_move_foreground(g_vol_needle);                      /* over the band and its marks (the number box is clear of it) */
        }
        if(th_disco()){
            lv_obj_add_flag(g_vol_panel, LV_OBJ_FLAG_CLICKABLE);               /* over everything; a tap off the arc closes it */
            lv_obj_add_event_cb(g_vol_panel, vol_tap_close_cb, LV_EVENT_CLICKED, NULL);
            lv_obj_set_style_bg_opa(g_vol_panel, 90, 0);
            lv_obj_t *f = lv_obj_create(g_vol_panel); lv_obj_remove_style_all(f);   /* the hub field, like Weather's */
            lv_obj_set_pos(f, 196, 90); lv_obj_set_size(f, 230, 180);
            lv_obj_set_style_radius(f, LV_RADIUS_CIRCLE, 0); lv_obj_set_style_bg_color(f, TC(SURFACE), 0); lv_obj_set_style_bg_opa(f, 225, 0);
            lv_obj_clear_flag(f, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_move_to_index(f, 0);
            lv_arc_set_bg_angles(g_vol_arc, 360 - DV_SPAN + DV_A0, DV_A0);
            lv_obj_set_style_arc_opa(g_vol_arc, LV_OPA_TRANSP, LV_PART_INDICATOR);   /* the segments are the fill */
            lv_obj_set_style_arc_opa(g_vol_arc, LV_OPA_TRANSP, LV_PART_MAIN);           /* input + knob only; the track is drawn below */
            {   lv_obj_t *tr = lv_arc_create(g_vol_panel);
                lv_obj_remove_style(tr, NULL, LV_PART_KNOB); lv_obj_clear_flag(tr, LV_OBJ_FLAG_CLICKABLE);
                lv_obj_set_size(tr, 332, 332); lv_obj_center(tr); lv_arc_set_bg_angles(tr, 360 - DV_SPAN + DV_A0, DV_A0);
                lv_obj_set_style_arc_width(tr, 12, LV_PART_MAIN); lv_obj_set_style_arc_rounded(tr, true, LV_PART_MAIN);
                lv_obj_set_style_arc_color(tr, TC(VOLUME_TRACK), LV_PART_MAIN); lv_obj_set_style_arc_opa(tr, 150, LV_PART_MAIN);
                lv_obj_set_style_arc_opa(tr, 0, LV_PART_INDICATOR); }
            for(int k = 0; k < DV_N; k++){
                lv_obj_t *a2 = lv_arc_create(g_vol_panel); g_dv_seg[k] = a2;
                lv_obj_remove_style(a2, NULL, LV_PART_KNOB); lv_obj_clear_flag(a2, LV_OBJ_FLAG_CLICKABLE);
                lv_obj_set_size(a2, 332, 332); lv_obj_center(a2); lv_arc_set_bg_angles(a2, 0, 0);
                lv_obj_set_style_arc_opa(a2, 0, LV_PART_MAIN);
                lv_obj_set_style_arc_width(a2, 12, LV_PART_INDICATOR); lv_obj_set_style_arc_rounded(a2, false, LV_PART_INDICATOR);
            }
            g_dv_cap = lv_obj_create(g_vol_panel); lv_obj_remove_style_all(g_dv_cap);   /* the rounded bottom end */
            lv_obj_set_size(g_dv_cap, 12, 12); lv_obj_set_style_radius(g_dv_cap, LV_RADIUS_CIRCLE, 0); lv_obj_set_style_bg_opa(g_dv_cap, LV_OPA_COVER, 0);
            lv_obj_set_pos(g_dv_cap, 180 + (int)lroundf(160 * cosf(DV_A0 * 0.0174533f)) - 6, 180 + (int)lroundf(160 * sinf(DV_A0 * 0.0174533f)) - 6);
            lv_obj_clear_flag(g_dv_cap, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_move_foreground(g_vol_arc);                                  /* the knob over the segments; drag still works */
            lv_obj_t *badge = lv_obj_get_parent(g_vol_num);                     /* the value in the field: icon, number, caption */
            lv_obj_set_flex_flow(badge, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_size(badge, 140, 150); lv_obj_set_style_bg_opa(badge, 0, 0);
            lv_obj_set_style_pad_all(badge, 0, 0); lv_obj_set_style_pad_row(badge, 2, 0);
            lv_obj_align(badge, LV_ALIGN_TOP_LEFT, 262 - 70, 105);
            lv_obj_t *spk = lv_obj_get_child(badge, 0); lv_obj_set_style_text_font(spk, TF(UI_24), 0);
            lv_obj_set_style_text_font(g_vol_num, TF(UI_46), 0);
            lv_obj_t *cap = lv_label_create(badge); lv_label_set_text(cap, "Volume");
            lv_obj_set_style_text_font(cap, TF(UI_14), 0); lv_obj_set_style_text_color(cap, TC(TEXT_SECONDARY), 0);
            lv_obj_t *mid = lv_obj_get_child(g_vol_panel, 1); (void)mid;
        }
    }
    if(!th_braun()){ lv_color_t acc = ui_current_accent();       /* follow an accent change (Braun: always orange) */
      lv_obj_set_style_arc_color(g_vol_arc, acc, LV_PART_INDICATOR);
      lv_obj_set_style_bg_color(g_vol_arc, acc, LV_PART_KNOB);
      lv_obj_set_style_text_color(lv_obj_get_child(lv_obj_get_parent(g_vol_num), 0), acc, 0); }
    if(vol < 0) vol = 0; if(vol > VOL_MAX) vol = VOL_MAX;
    /* Don't yank the arc out from under an active finger drag: while the arc is
     * pressed, vol_arc_cb already tracks the live value, and a lagging a714 echo
     * would jump it back. Skip the programmatic value/number set in that case. */
    if(!(lv_obj_get_state(g_vol_arc) & LV_STATE_PRESSED)){
        g_vol_suppress = 1;
        lv_arc_set_value(g_vol_arc, vol);
        vol_needle(vol);
        dvol_paint(vol);
        g_vol_suppress = 0;
        char b[12]; snprintf(b, sizeof b, "%d", vol); lv_label_set_text(g_vol_num, b);
    }
    lv_obj_remove_flag(g_vol_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(g_vol_panel);
    lv_timer_reset(g_vol_timer);
    lv_timer_resume(g_vol_timer);
}

/* ---- fork: the Disco theme's Music screen and title overlay use these (disco.c) ------------------------ */
const void *ui_current_sharp_img(void){ return g_sharp_valid ? (const void *)&g_posterdsc : NULL; }   /* 364 px, RGB888 */
int ui_np_fav_state(void){ return g_np_have ? g_np_fav : -1; }
void ui_np_fav_toggle(void){                       /* as the Now Playing heart */
    if(!g_np_have) return;
    fav_refresh(!g_np_fav, 1);
    ui_set_favorite(g_np_fav);
    g_favp_active = 1; g_favp_val = g_np_fav; g_favp_set = lv_tick_get();
    snprintf(g_favp_path, sizeof g_favp_path, "%s", g_np_curpath);
    ui_toast(g_np_fav ? "Added to Favourites" : "Removed from Favourites");
}
int ui_np_mode_next(void){                          /* as the Now Playing play-mode button; returns the new mode */
    int wm = (cfg_get_int("work_mode", 0) + 1) % WORKMODE_COUNT;
    cfg_set_int("work_mode", wm);
    ui_set_workmode(wm);
    mode_refresh(wm);
    static const char *const MODE_NAMES[WORKMODE_COUNT] = { "Sequential", "Shuffle", "Repeat One", "Repeat All", "Single" };
    ui_toast(MODE_NAMES[wm]);
    return wm;
}
