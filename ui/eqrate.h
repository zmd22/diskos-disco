/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef EQRATE_H
#define EQRATE_H
/* Live PEQ processing rate for the inspected V2.57 player; 0 when unverified.
 * Reads only: never attaches, pauses or writes to the player. */
/* Worker-only: performs a fresh probe. */
int eq_player_rate(void);
/* UI-safe: schedule verification and read a short-lived cached result. */
void eq_rate_request(void);
int eq_rate_cached(void);
#endif
