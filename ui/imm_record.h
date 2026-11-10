/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef IMM_RECORD_H
#define IMM_RECORD_H
#include <stdint.h>
#define IMM_RECORD_SIZE 360
/* Native XRGB8888 source, angle in tenths of a degree. Round output with a
 * stationary lyric-height fade matching the original overlay. No allocations or image decoding per frame. */
void imm_record_set_smoothing(int enabled);
void imm_record_render(const uint32_t *src, int w, int h, uint32_t *out, int angle, int fade_height);
/* fork perf: the things drawn over the record are baked into the frame here instead of being re-drawn by LVGL
 * on every frame. Overlays = small ARGB8888 snapshots (straight alpha) of the lyric labels, re-taken only when they
 * change; the ring = the thin progress arc on the top rim (fixed geometry, colours + value set ~4x/s). */
typedef struct { const uint32_t *px; int x, y, w, h, stride, slide; } imm_overlay_t;   /* stride in pixels */
#define IMM_MAX_OVERLAYS 5
#define IMM_MAX_LYRIC_CLIPS 16
typedef struct { int x1,y1,x2,y2; } imm_lyric_clip_t;
void imm_record_set_lyric_offset(int offset);
void imm_record_set_overlays(const imm_overlay_t *o, int n);
/* Move/fade cached text snapshots without reallocating or rerasterizing them. */
void imm_record_set_overlay_motion(int index,int y_offset,int opacity);
/* Clips are relative to the snapshot. Outside them current-line glyphs are dimmed. */
void imm_record_set_lyric_highlight(int index,const imm_lyric_clip_t *clips,int n,int dim_opacity);
void imm_record_set_ring(int on, int permille, uint32_t track_rgb, uint32_t ind_rgb);
void imm_record_set_ring_full(int full);   /* 1: the whole rim from 12 o'clock (Zune); 0: the top arc */
/* Disco's CD look: a still rainbow sheen over the whole disc plus faint rainbow spokes, like light on a spinning CD.
 * It does not turn with the cover; it is folded into the lyric fade, so it costs about the same per frame. */
void imm_record_set_cd(int on);
#endif
