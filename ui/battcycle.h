/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Charge cycles from what this Disc's gauge really reports (measured 2026-11-08, cw221X + SGM41513):
 * - current_now is a 0/1 flag, and it is 1 only for the first part of a charge: it drops to 0 at about 86 % while the
 *   level keeps rising about 1 % a minute to the end of the charge.
 * - the charger sometimes finishes below 100 % on its own; that is a full charge too.
 * - no charger appears under power_supply (the gauge itself says type=Mains), so being plugged in is inferred.
 * So: a charger session starts when charging is reported or the level rises; it is still on power while the level
 * rises or the battery is held at the charger's voltage; it ends when the level falls below the session's peak (only
 * the battery lowers it). A session that reached full - the top of the scale, or a charge that stopped by itself
 * on the charger - restarts the "since full charge" totals at the moment it was unplugged. Pure logic, no LVGL. */
#ifndef DISKOS_BATTCYCLE_H
#define DISKOS_BATTCYCLE_H
#include <stdint.h>

/* "USC1". Older firmware wrote only the first five fields and reads only those, so the record keeps that start and
 * that magic: a file written here still loads its totals after a downgrade, and an old file loads here. */
#define BC_MAGIC1 0x31435355u
#define BC_NONE   0xFFFFFFFFu

typedef struct {
    uint32_t magic, full_at, play_min, scr_min, full_seen;   /* the USC1 layout, kept first */
    uint32_t pwr;        /* in a charger session */
    uint32_t peak;       /* highest level of the session */
    uint32_t peak_at;    /* when it first reached that level */
    uint32_t low;        /* lowest level on battery since the last session (BC_NONE = unknown) */
    uint32_t last_pct;   /* the previous reading (BC_NONE = none yet) */
    uint32_t power_at;   /* last time there was evidence of external power (charging, a rise, the charger's voltage) */
    uint32_t pad[3];
} bcycle_t;

typedef struct {
    uint32_t now;        /* unix seconds */
    int pct;             /* 0..100 */
    int charging;        /* current_now > 0 */
    int mv;              /* battery voltage, mV (0 = unknown) */
    int limit;           /* Charging Limit (80 %) on */
} bc_reading_t;

enum { BC_EV_NONE = 0, BC_EV_START = 1, BC_EV_FULL = 2, BC_EV_END = 4, BC_EV_END_FULL = 8 };

/* Measured 2026-11-09: on the charger the battery sits at 4.33-4.35 V from about 78 % up, also once the charge has
 * stopped (4.345 V held at 99 %); unplugged at 99 % it falls to 4.28 V within 20 s and 4.26 V within a minute. */
#define BC_HOLD_MV     4300   /* at or above: the charger is holding the battery */
#define BC_STOP_S      (10 * 60)   /* no rise this long at the charger's voltage: the charge is done (near the top a
                                    * point took up to 2.5 min) */
#define BC_RECENT_S    (3 * 60)    /* "on power now" needs evidence this recent: an unplug at the top shows at once in
                                    * the voltage, long before the level falls and ends the session */

void bc_init(bcycle_t *c);
int  bc_load(bcycle_t *c, const void *buf, unsigned len);   /* 1 if buf held a USC1 record, short or full */
int  bc_full_level(int limit);                               /* 95, or 79 with Charging Limit */
/* on external power right now: in a session with recent evidence. With Charging Limit the charger parks the battery
 * at 80 % at a voltage not measured yet, so a full session there counts as on power until it ends. */
int  bc_on_power(const bcycle_t *c, uint32_t now, int limit);
int  bc_power_evidence(const bcycle_t *c, const bc_reading_t *r);   /* this reading shows external power */
int  bc_step(bcycle_t *c, const bc_reading_t *r);           /* BC_EV_* bits; the caller saves on any event */
/* The Disc was off or asleep: the first reading after start. A level well above the last recorded one means it
 * charged meanwhile (a session that may already be full). */
int  bc_resume(bcycle_t *c, const bc_reading_t *r, int last_recorded_pct, int last_recorded_power);
#endif
