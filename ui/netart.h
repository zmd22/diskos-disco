/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef NETART_H
#define NETART_H
/* Online album art: a cover picture from the iTunes Search API for a song that has none of its own. The lookup runs
 * on its own lowest-priority thread in diskOS (never in the player); pictures live on the SD card. See netart.c. */
#ifndef NETART_DIR
#define NETART_DIR "/tmp/sdcard/.diskos/netart"
#endif

/* Path of the saved picture for this song (its album's, else its own), 1 if there is one. Any thread (SD-guarded). */
int  netart_have(const char *artist, const char *album, const char *title, char *out, int cap);
/* Main thread: look this song up in the background (no-op if already saved, recently missed, or backing off). */
void netart_request(const char *track, const char *artist, const char *album, const char *title);
/* Main thread: drop queued work and ignore anything still in flight (the setting was turned off). */
void netart_cancel(void);
/* Art worker: about to decode this saved picture (keeps the prune off it) / it could not be decoded (drop it). */
int  netart_touch(const char *path);   /* 1 = stamped (and still there) */
void netart_reject(const char *path);
/* Main thread: 1 (and the song's path in `track`) when a picture became available for it, so its cover can be redone. */
int  netart_take_ready(char *track, int cap);
/* The iTunes storefront for the player's LANGUAGE setting (stock's table), "US" when unknown. */
const char *netart_country(int language);
#endif
