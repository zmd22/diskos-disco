/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef ART_H
#define ART_H
/* Decode the track's embedded cover and write three BMPs in one ffmpeg pass:
 * cover_bmp (148px), thumb_bmp (42px Home thumb), backdrop_bmp (360px blurred).
 * Returns 0 on success, non-zero if there's no usable cover. */
int art_make_all(const char *track, const char *cover_bmp,
                 const char *thumb_bmp, const char *backdrop_bmp);
/* Same, but cancellable=1 registers the child so art_cancel() can kill it. */
int art_make_all_ex(const char *track, const char *cover_bmp,
                    const char *thumb_bmp, const char *backdrop_bmp, int cancellable);
/* As above, but the caller supplies the cancel token (captured atomically with its request identity), so a
 * skip landing between request-validation and here still supersedes this decode. */
int art_make_all_ex_gen(const char *track, const char *cover_bmp,
                    const char *thumb_bmp, const char *backdrop_bmp, int cancellable, unsigned gen0);
/* Current cancel generation, for capturing a token to pass to art_make_all_ex_gen. */
unsigned art_cancel_gen(void);
/* Kill the in-flight cancellable (live) decode, if any. */
void art_cancel(void);
int  art_last_exit(void);   /* the helper's exit status from this thread's last run, -1 if interrupted */
void art_kill_all(void);   /* kill every decoder child, incl. non-cancellable prewarm/fallback */
/* 1 if path is exactly the n x n 24-bit BMP layout the artwork readers expect (every header field checked). */
int art_bmp_valid(const char *path, int n);
/* Mix the track's sidecar cover files (name/size/mtime) into a hash, for cache invalidation. */
#include <stdint.h>
void art_sidecar_signature(const char *track, uint64_t *h);
/* Vinyl saver cover: 360x360 top-down BGRA for the track (embedded, else sidecar). Blocking - worker threads only. */
int art_make_saver(const char *track, const char *out_raw, int64_t deadline_ms);   /* CLOCK_MONOTONIC ms */
/* Fork sharp cover: 364x364 BGRA, skip-cancellable. Worker threads only. */
int art_make_poster(const char *track, const char *out_raw, unsigned gen0);
#endif
