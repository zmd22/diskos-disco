/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef DISKOS_LYRICS_TIMING_H
#define DISKOS_LYRICS_TIMING_H

/* One explicitly timestamped lyric span. Byte offsets address the normalized,
 * NUL-terminated UTF-8 line, not codepoint indices. end_byte is exclusive.
 * A final span without a following inline timestamp has end_ms == -1: callers
 * must not invent a sung duration for it. */
typedef struct {
    long start_ms, end_ms;
    int start_byte, end_byte;
} lrc_word_t;

#endif
