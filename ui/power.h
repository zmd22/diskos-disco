/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
/* Power settings and the SD-safe shutdown, on top of the stock V2.57 player's own backends.
 *
 * Stock facts (V2.57 mq_player / mq_ui; evidence in the report and tests/release/test_parity_power.py):
 *   0806  Sleep timer, seconds, runtime only. The player's 1 s thread shuts the device down (its power-off routine at
 *         0x4e6f0c, ending in `poweroff -f`) once the timer has counted past the value.
 *   0664  Idle poweroff, seconds (SYSCONFIG.POWER_SAVE). Stock menu: Off, 5, 10, 30, 60, 90, 120 minutes.
 *         diskOS NEVER lets the player run this: the player's idle path kills mq_ui and powers off with no sync, which
 *         can corrupt the SD card (data lost 2026-09-21). diskOS runs the same idle timer itself (below) and powers off
 *         through the SD-safe sequence; at startup it records the user's stock value (cfg stock_power_save) and sends 0664 = 0.
 *   0808  Charging optimization on/off (SYSCONFIG.CHARGE_PROTECT); the player's battery task stops charging at 80%.
 * The player's power-off routine kills mq_ui and calls poweroff without any sync of its own, so diskOS closes its own
 * SD writers and syncs first, then uses 0806 as the trigger. */
#ifndef DISKOS_POWER_H
#define DISKOS_POWER_H
#include <stddef.h>
#include <stdint.h>

/* ---- option tables and frames (pure) ---- */
#define POWER_IDLE_N 7
int  power_idle_secs(int idx);                      /* option index -> seconds (0 = off), -1 if out of range */
int  power_idle_idx_from_secs(int secs);            /* SYSCONFIG.POWER_SAVE seconds -> option index, -1 if not an option */
const char *power_idle_label(int idx);
int  power_frame_idle(char *buf, size_t n, int idx);       /* "0664000C%04X"; 0 ok, -1 bad idx */
int  power_frame_charge(char *buf, size_t n, int on);      /* "0808000C000<0|1>" */
int  power_frame_sleep(char *buf, size_t n, int secs);     /* "0806000C%04X", secs 0..0xFFFF */
/* Send through `send` (ipc_send_cmd in the product); 0 = sent, -1 = not sent. */
int  power_apply_charge_via(int (*send)(const char *), int on);
/* Stock temperature state from the fuel gauge's tenths of a degree: 0 normal, 1 too high (>= 55.1 C), 2 too low (< 0 C). */
int  power_temp_state(int tenths_c);
const char *power_temp_msg(int state);

/* SYSCONFIG.POWER_SAVE / CHARGE_PROTECT, read-only: value, or -1 when unreadable (missing, locked, NULL). */
int  power_read_sysconfig(const char *db_path, const char *column);

/* ---- idle power-off, run by diskOS (never by the player) ---- */
/* Idle = not playing AND no touch/key input for limit_secs. Call often. Returns 1 exactly once per idle period, when the
 * period ends; `request` (power_shutdown_request in the product) is then called, and if it fails (-1) the period restarts
 * so it is tried again later. `last_input_ms` is a lv_tick timestamp of the latest touch or key. */
int  power_idle_tick(uint32_t now_ms, uint32_t limit_secs, int playing, uint32_t last_input_ms, int (*request)(void));
void power_idle_reset(void);
/* Takeover: the player's own idle power-off (unsynced kill) must be off before diskOS's idle timer may run. One step of the
 * retry loop, called every few seconds and reset (power_takeover_reset) on a new ipc generation:
 *   first: read POWER_SAVE; if it is on, record its seconds in *stock_secs (never touches diskOS's own setting); ALWAYS send 0664 = 0 (even when it reads 0 or
 *          is unreadable: the running player may not match the database); a failed send restores *stock_secs, returns -1.
 *   next:  re-read POWER_SAVE (0664 calls the player's config setter, so it must now read 0): 0 = CONFIRMED (returns 1);
 *          on = send again next step; unreadable = keep waiting. Returns 0 while unconfirmed. */
typedef struct { int sent; } pw_take_t;
void power_takeover_reset(pw_take_t *t);
int  power_takeover_step(pw_take_t *t, int (*read_secs)(void), int (*send)(const char *), int *stock_secs);

/* ---- sleep timer expiry decision (pure) ---- */
#define POWER_SLEEP_PAUSE 0
#define POWER_SLEEP_SHUTDOWN 1
typedef enum { PW_SLEEP_WAIT, PW_SLEEP_PAUSE, PW_SLEEP_DISARM, PW_SLEEP_SHUTDOWN } pw_sleep_act_t;
/* armed_ms 0 = not armed. `guarded` = a recent seek makes the play state unreliable: it only holds a PAUSE back (a pause is
 * a toggle), never a shutdown. A shutdown fires whether or not audio is playing, as stock's does. */
pw_sleep_act_t power_sleep_decide(uint32_t armed_ms, uint32_t elapsed_ms, int guarded, int playing, int action);

/* ---- SD-safe shutdown ---- */
/* PW_PROTECTED: the hand-off did not complete. The 0806 = 0 cancel is sent (retried until it is queued), but a queued frame
 * is not a confirmed cancel and 0806 has no read-back, so a stock power-off may still be pending: the card stays CLOSED and
 * every shutdown request is refused until the player has restarted (power_shutdown_new_generation) or the Disc reboots. */
typedef enum { PW_OFF, PW_DRAIN, PW_SYNC, PW_HANDOFF, PW_PROTECTED } pw_phase_t;
typedef struct power_ops {
    void     (*quiesce_begin)(void *ctx);        /* close SD admission (new writers/leases refused), stop scans and decoders */
    int      (*drained)(void *ctx);              /* 1 once every admitted reader and writer has finished */
    void     (*quiesce_end)(void *ctx);          /* reopen SD admission */
    int      (*sync_start)(void *ctx);           /* start `sync`; 0 ok, -1 could not */
    int      (*sync_poll)(void *ctx);            /* 0 still running, 1 exited 0, -1 failed */
    void     (*sync_kill)(void *ctx);
    int      (*send)(void *ctx, const char *frame);   /* player frame; 0 = sent */
    uint32_t (*now_ms)(void *ctx);
    void     (*toast)(void *ctx, const char *msg);
    void     *ctx;
} power_ops_t;
#define POWER_DRAIN_MS   8000u    /* writers get this long to finish */
#define POWER_SYNC_MS   15000u
#define POWER_CANCEL_RETRY_MS 2000u
#define POWER_HANDOFF_MS 20000u   /* after the player is told to power off, this long before we call it failed */
int  power_shutdown_begin(const power_ops_t *ops);   /* 0 started, -1 already running or ops incomplete */
pw_phase_t power_shutdown_tick(void);                /* call often; PW_OFF when idle or finished-with-failure */
pw_phase_t power_shutdown_phase(void);
void power_shutdown_new_generation(void);            /* the player restarted (new ipc generation): clears PW_PROTECTED, reopens the card */

#ifndef POWER_CORE_ONLY
/* product wiring (main.c / settings.c): real SD quiesce, `sync` child, ipc_send_cmd, toast */
int  power_apply_charge(int on);
int  power_shutdown_request(void);
void power_tick(uint32_t last_input_ms, int playing);   /* main loop: shutdown state machine, idle timer, temperature notice */
void power_note_input(void);    /* a physical key was pressed (volume) */
int  power_supported(void);     /* the stock backends above are mapped for this firmware (V2.57) */
void power_startup_mirror(void);/* copy the player's own values into cfg (idle_off_idx, charge_protect) */
#endif
#endif
