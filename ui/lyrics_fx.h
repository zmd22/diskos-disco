/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DISKOS_LYRICS_FX_H
#define DISKOS_LYRICS_FX_H
#include "lvgl/lvgl.h"
#include "lyrics_timing.h"

/* Geometry is measured only when a lyric/font/width changes. Frames only change
 * the clip rectangles; no new labels, fonts, snapshots or heap allocations. */
#define LYRICS_FX_FRAGMENTS 128
#define LYRICS_FX_CLIPS 16
typedef struct { lv_area_t area; int word; } lyrics_fx_fragment_t;
typedef struct {
    lv_obj_t *label;
    const lrc_word_t *words;
    int word_count, fragment_count, draw_enabled;
    long position;
    lv_color_t color;
    lyrics_fx_fragment_t fragments[LYRICS_FX_FRAGMENTS];
    lv_area_t clips[LYRICS_FX_CLIPS];
    int clip_count;
} lyrics_fx_t;

/* Caller keeps words alive until unbind, and paints the base label muted.
 * Returns zero if the text cannot be represented safely (line-only fallback). */
int lyrics_fx_bind(lyrics_fx_t *fx, lv_obj_t *label, const lrc_word_t *words,
                   int count, lv_color_t color);
void lyrics_fx_unbind(lyrics_fx_t *fx);
/* Update cached clips for a playback timestamp; return 1 only on pixel change. */
int lyrics_fx_update(lyrics_fx_t *fx, long position);
/* Geometry relative to the label object's top-left (including content padding).
 * The fast Immersive compositor can reuse these with its full-bright snapshot. */
int lyrics_fx_get_clips(const lyrics_fx_t *fx, lv_area_t *out, int capacity);
void lyrics_fx_set_draw(lyrics_fx_t *fx, int enabled);
#endif
