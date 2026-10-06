/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
/* Screen rotation and volume-key actions, on top of the stock V2.57 player's own backends.
 *
 * Stock facts (V2.57; evidence in tests/release/test_parity_controls.py and the report):
 *   Screen rotation: SYSCONFIG.SCREEN_ROT (CFG[31]) is set by player tag 0646 (handler 0x4f1978: stores the first
 *     parameter byte, then CFG set 31). Stock mq_ui (0x420e24) maps the stored value 0,1,2,3 to lv_display_set_rotation
 *     2,3,0,1, i.e. the frame is turned 180, 270, 0, 90 degrees clockwise. The panel is mounted upside down, so stock's
 *     default 0 is exactly diskOS's old fixed 180 turn.
 *   Volume keys: SYSCONFIG.KEY_SINGLE_CLICK_SLE / KEY_DOUBLE_CLICK_SLE / KEY_LONG_PRESS_SLE (CFG[53..55]) are set by
 *     tags 0820 / 0821 / 0822 (handlers 0x4f1a9c / 0x4f1b30 / 0x4f1bc4, named player_handle_set_key_*_function_status).
 *     The player's own key dispatcher (0x4e72f0) reads them: value 1 = the volume key adjusts the volume, anything
 *     else = it switches track. Stock's menu labels: 0 "Switch track", 1 "Adjust volume".
 *     The player owns the keys (it holds /dev/input/event0), so diskOS only writes the setting. */
#ifndef DISKOS_CONTROLS_H
#define DISKOS_CONTROLS_H
#include <stddef.h>
#include <stdint.h>

#define CTL_ROT_N 4                    /* stock values 0..3 */
typedef enum { CTL_KEY_SINGLE, CTL_KEY_DOUBLE, CTL_KEY_LONG, CTL_KEY_N } ctl_key_t;
enum { CTL_ACT_TRACK = 0, CTL_ACT_VOLUME = 1 };
typedef enum { CTL_DO_TRACK, CTL_DO_VOLUME } ctl_effect_t;

/* Firmware gate: the tags/handlers above are RE-verified on V2.57 only (other firmware: never send). */
int  ctl_supported_ver(int os_ver);
int  ctl_supported(void);

/* ---- rotation (pure) ---- */
int  ctl_rot_deg(int v);                /* stock value -> clockwise degrees applied to the frame (180,270,0,90); bad -> 180 */
/* Logical (upright UI) pixel -> raw framebuffer pixel on a w x h frame; the quarter turns need w == h (else 180). */
void ctl_rot_map(int v, int w, int h, int x, int y, int *rx, int *ry);
/* Raw touch -> logical, as lv_evdev_set_swap_axes / lv_evdev_set_calibration arguments (size = panel edge in px). */
void ctl_rot_touch_cal(int v, int size, int *swap, int *min_x, int *min_y, int *max_x, int *max_y);
/* Copy the logical area [x1..x2] x [y1..y2] (row-major XRGB8888 in src, stride = area width) into the raw frame. */
void ctl_blit(uint8_t *base, uint32_t line_bytes, int w, int h, int v, const uint32_t *src, int x1, int y1, int x2, int y2);
/* The default rotation's flush (value 0 = the panel's 180): the ORIGINAL fb_pan flush loop, unchanged, kept inline so the
 * hot path costs exactly what it always did. fb_pan.c uses it for value 0 and ctl_blit for the other rotations. */
static inline void ctl_flush_default(uint8_t *base, uint32_t line, int W, int H, const uint32_t *src, int x1, int y1, int x2, int y2){
    int aw = x2 - x1 + 1;
    for(int sy = y1; sy <= y2; sy++){
        const uint32_t *srcrow = src + (size_t)(sy - y1) * aw;
        uint32_t *dstrow = (uint32_t*)(base + (size_t)(H - 1 - sy) * line); /* 180deg row flip */
        for(int sx = x1; sx <= x2; sx++)
            dstrow[W - 1 - sx] = srcrow[sx - x1];                          /* + column flip */
    }
}
int  ctl_rot_get(void);                 /* the rotation the display is using */
void ctl_rot_set(int v);                /* out-of-range values are ignored */

/* ---- keys (pure) ---- */
const char *ctl_key_tag(int key);       /* "0820" / "0821" / "0822", NULL if out of range */
const char *ctl_key_column(int key);    /* SYSCONFIG column */
/* What a volume key does for a stored action (the player's dispatcher, 0x4e72f0: value 1 -> the volume routines, any
 * other value -> next/previous track). Which key is next and which is previous is not decoded here (UNVERIFIED). */
ctl_effect_t ctl_key_effect(int action);

/* ---- frames and writes ---- */
int  ctl_frame_rot(char *buf, size_t n, int v);            /* "0646000C000<v>"; 0 ok, -1 bad value */
int  ctl_frame_key(char *buf, size_t n, int key, int act); /* "082<k>000C000<a>"; 0 ok, -1 bad */
/* Send through `send` (ipc_send_cmd in the product); 0 = sent, -1 = not sent (bad value, no sender, refused send). */
int  ctl_apply_rot_via(int (*send)(const char *), int v);
int  ctl_apply_key_via(int (*send)(const char *), int key, int act);

/* ---- the player's stored values (read-only SYSCONFIG) ---- */
typedef struct { int rot, key[CTL_KEY_N]; } ctl_state_t;   /* -1 = unreadable */
int  ctl_read_state(const char *db_path, ctl_state_t *st); /* 0 = row read, -1 = nothing readable */
/* Copy into diskOS's setting mirror (cfg keys screen_rot, key_single, key_double, key_long): the rotation APPLIED (the
 * stored one, or the default when it could not be read), and each key value that is exactly 0 or 1. */
void ctl_mirror_via(const ctl_state_t *st, void (*set_int)(const char *key, int v));
/* Rotation rollback: a new orientation is kept only if confirmed within CTL_ROLLBACK_MS (pure state machine). */
#define CTL_ROLLBACK_MS 10000
typedef struct { int pending, prev, cur; uint32_t start; } ctl_rb_t;
void ctl_rb_begin(ctl_rb_t *r, int prev, int cur, uint32_t now_ms);   /* a second begin keeps the first prev */
int  ctl_rb_left_ms(const ctl_rb_t *r, uint32_t now_ms);              /* 0 when not pending or due */
int  ctl_rb_expired(ctl_rb_t *r, uint32_t now_ms, int *revert_to);    /* 1 once, on timeout: revert_to = old value */
int  ctl_rb_keep(ctl_rb_t *r, int *value);                            /* 1 if pending: value = the new rotation */
int  ctl_rb_revert(ctl_rb_t *r, int *value);                          /* 1 if pending: value = the old rotation */
#define CTL_SYSCONFIG "/usr/data/fiio/db/sysconfig.db"
#define CTL_CFG_ROT "screen_rot"

#ifndef CTL_CORE_ONLY
void controls_boot_rotation(void);      /* fbpan_create: use the player's stored rotation from the first frame */
void controls_startup_mirror(void);     /* read SYSCONFIG into cfg (settings_apply_startup) */
int  ui_set_screen_rot(int v);          /* turn the display now and ask "Keep this orientation?"; 0 ok, -1 not. 0646 is sent on Keep. */
void controls_tick(void);               /* rollback timeout (also driven by the prompt's own timer) */
int  ctl_rotation_pending(void);
void ctl_prompt_keep(void);
void ctl_prompt_revert(void);
int  ui_set_volume_key(int key, int act);
#endif
#endif
