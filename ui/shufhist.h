/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Shuffle history: the songs that actually played, so Previous on shuffle goes back to them (FiiO's player picks
 * another song there) and Next after that retraces them forward before shuffling on. Pure logic, no LVGL/IPC. */
#ifndef SHUFHIST_H
#define SHUFHIST_H
#include <stddef.h>
#define SH_MAX 50
void sh_reset(void);
void sh_observe(int pos_id, const char *path);                /* the song playing now (call on every state) */
int  sh_back(int *pos_id, char *path, size_t n);              /* 1: the song before; it becomes the expected one */
int  sh_forward(int *pos_id, char *path, size_t n);           /* 1: the song after (only after going back) */
void sh_cancel(void);                                         /* a jump failed: forget the expected song */
#endif
