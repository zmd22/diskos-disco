/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "screens.h"
#include "braun.h"
#include <string.h>
#include "orbit.h"
#include "theme.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include "ipc.h"
#include "theme_kit.h"
#include "modes.h"
#include "config.h"
#include <unistd.h>
#include <glob.h>

/* Working Mode (audio source) picker - mirrors stock's "Working mode" list. Tapping a mode replays
 * the captured V2.28 switch sequence via ui_set_source_mode() and marks it selected.
 * 0=Local 1=USB-DAC 2=BT-Receiving 3=USB-Storage. The fifth row, USB Audio (stock's "USB AUDIO"), is an OUTPUT
 * route of the Local source (the Disc feeds an external USB DAC), not a source mode: see the output section. */

typedef struct { const char *l1, *l2, *glyph; int mode; } modeinfo_t;
/* 2x3 grid. `mode` is the index ui_set_source_mode() takes; grid order is presentation only.
 * Row 1: Local / USB DAC / BT DAC   Row 2: BT streaming / AirPlay / USB storage
 * BT streaming (4) and AirPlay (5) use the V2.40-verified 0657 values 07 and 0A. */
static const modeinfo_t MODES[] = {
    { "Playback", "",         LV_SYMBOL_SD_CARD,    0 },
    { "USB",     "DAC",       LV_SYMBOL_USB,        1 },
    { "BT",      "DAC",       LV_SYMBOL_BLUETOOTH,  2 },
    { "BT",      "streaming", LV_SYMBOL_VOLUME_MAX, 4 },
    { "AirPlay", "",          LV_SYMBOL_WIFI,       5 },
    { "USB",     "storage",   LV_SYMBOL_DRIVE,      3 },
};
#define N_MODES ((int)(sizeof(MODES)/sizeof(MODES[0])))
#define ROW_USB_AUDIO 100 /* only present in DISKOS_TEST_OUTPUTS builds; not 4 (= BT streaming's mode) */

/* OUTPUT-ROUTE-BEGIN */
/* Output route: Internal DAC / SPDIF / USB Audio (SPDIF row + USB Audio picker row: -DDISKOS_TEST_OUTPUTS builds only).
 * Stock V2.57 (mq_ui_257.dis):
 *   Settings SPDIF toggle (0x480510 -> 0x464f0c):  0666 <6|4>, 0657 8                       (no 0642, no sleep)
 *   Working Mode callback 0x46bbac (200 ms one-shot): internal/SPDIF (0x46bd28): 0666 <6|4>, usleep 65 ms, 0642 0, 0657 8
 *                                                    USB AUDIO (0x46bd90):       0666 3, 0642 5, 0657 8
 * 0666 payloads are the player's out modes (mq_player_257 set_out_device 0x47bf24): 6 local analog, 4 SPDIF_TX, 3
 * USB_HOST. The player's 0666 handler (0x4f1294) closes the player and re-inits the output; 0657 8 then sets
 * LOCALPLAYER again, so a 0666 not followed by 0657 8 leaves NO_WORK_MODE (dead player). Stock sends no pause;
 * diskOS does (0666 into a live stream killed mq_player, scratch/device-runs/sacd-hang-0929): a switch is pause ->
 * PCM proven quiet for OUT_QUIET_MS -> route -> work mode -> resume. Leaving USB audio always uses the full sequence
 * (0642 0 drops the host request). USB_HOST needs the USB DAC card (else the player uses a silent USB_HOST_NULL
 * sink, 0x47c040), so diskOS re-checks it right before the frames and refuses instead.
 * If a send fails after the route moved, the internal sequence is retried until it fully lands on a live player
 * connection (recovery-pending: every play/route/source command is refused meanwhile). */
#ifndef OUT_PCM_GLOB
#define OUT_PCM_GLOB "/proc/asound/card*/pcm*p/sub*/status"
#endif
#ifndef OUT_USB_CARD
#define OUT_USB_CARD "/proc/asound/card1"   /* the USB DAC card; its usbid node exists for USB audio cards only (UNVERIFIED on this kernel) */
#endif
#define OUT_QUIET_MS 500
#define OUT_WAIT_MS  3000
#define OUT_WORKMODE "0657000C0008"
#define OUT_PAUSE    "0201000C0000"
typedef struct { const char *route, *gadget; unsigned settle_us; } out_seq_t;
static const out_seq_t OUT_SEQ[3] = {
    { "0666000C0006", "0642000C0000", 65000 },   /* OUT_INTERNAL */
    { "0666000C0004", "0642000C0000", 65000 },   /* OUT_SPDIF */
    { "0666000C0003", "0642000C0005", 0 },       /* OUT_USB */
};
static void modes_ui_refresh(void);
static int g_out_route = OUT_INTERNAL;
static unsigned g_out_gen = 0;
static struct { int target, was_playing, full; unsigned gen; char path[256]; long pos; uint32_t t0, quiet0; lv_timer_t *tm; } g_osw;
static int g_rec = 0; static unsigned g_rec_gen = 0; static lv_timer_t *g_rec_tm = NULL;

static void out_set_state(int r){
    g_out_route = r; g_out_gen = ipc_generation();
    if(cfg_get_int("spdif", 0) != (r == OUT_SPDIF)) cfg_set_int("spdif", r == OUT_SPDIF);   /* the Settings toggle mirrors the real route, never a wish */
}
int modes_output_route(void){
    if(g_out_route != OUT_INTERNAL && g_out_gen != ipc_generation()){ out_set_state(OUT_INTERNAL); }   /* a restarted player is back on the internal DAC */
    return g_out_route;
}
void modes_output_reset(void){ out_set_state(OUT_INTERNAL); }
int modes_output_busy(void){ return g_osw.tm != NULL || g_rec; }
/* Route-aware local init. with_gadget (the boot timer): internal/SPDIF send 0642 0 first; USB always pairs 0666 3 with 0642 5. */
int modes_local_init(int with_gadget){
    int r = modes_output_route();
    if(r != OUT_USB && with_gadget && ipc_send_cmd(OUT_SEQ[r].gadget) < 0) return -1;
    int rc = ipc_send_cmd(OUT_SEQ[r].route) < 0 ? -1 : 0;
    if(rc < 0 && with_gadget) return -1;   /* the boot timer retries the whole init next tick */
    /* the play preamble (with_gadget 0) is best effort like the old inline pair: 0657 8 is attempted whatever 0666 returned */
    if(r == OUT_USB && ipc_send_cmd(OUT_SEQ[r].gadget) < 0) rc = -1;
    if(ipc_send_cmd(OUT_WORKMODE) < 0) rc = -1;
    return rc;
}
/* 1 only when every playback PCM node is readable and none is RUNNING or DRAINING; unreadable / no nodes = not proven */
static int out_pcm_quiet(void){
    glob_t g; int quiet = 1;
    if(glob(OUT_PCM_GLOB, 0, NULL, &g) != 0) return 0;
    for(size_t i = 0; i < g.gl_pathc && quiet; i++){
        FILE *f = fopen(g.gl_pathv[i], "r"); char l[96] = "";
        if(!f){ quiet = 0; break; }
        if(!fgets(l, sizeof l, f)) l[0] = 0;
        fclose(f);
        if(!l[0] || strstr(l, "RUNNING") || strstr(l, "DRAINING")) quiet = 0;
    }
    globfree(&g);
    return quiet;
}
static int out_usb_card_ok(void){
    char p[96]; snprintf(p, sizeof p, "%s/usbid", OUT_USB_CARD);
    return access(p, F_OK) == 0;
}
/* 0 = whole sequence sent; -1 = the 0666 was refused (nothing changed); -2 = the route moved but the tail failed */
static int out_seq_send(int t, int full){
    if(ipc_send_cmd(OUT_SEQ[t].route) < 0) return -1;
    if(full){
        if(OUT_SEQ[t].settle_us) usleep(OUT_SEQ[t].settle_us);
        if(ipc_send_cmd(OUT_SEQ[t].gadget) < 0) return -2;
    }
    if(ipc_send_cmd(OUT_WORKMODE) < 0) return -2;
    return 0;
}
/* recovery-pending: retry the full internal sequence until it lands on the same live player connection */
static void out_rec_clear(void){ if(g_rec_tm){ lv_timer_del(g_rec_tm); g_rec_tm = NULL; } g_rec = 0; }
static void out_recover_tick(lv_timer_t *t){
    (void)t;
    if(ipc_generation() != g_rec_gen){ out_rec_clear(); out_set_state(OUT_INTERNAL); modes_ui_refresh(); return; }   /* a new player initialises itself */
    if(!ipc_is_ready()) return;
    if(out_seq_send(OUT_INTERNAL, 1) == 0){
        out_rec_clear(); out_set_state(OUT_INTERNAL);
        ui_toast("Internal DAC restored"); modes_ui_refresh();
    }
}
static void out_rec_enter(void){
    out_set_state(OUT_INTERNAL);
    if(g_rec) return;
    g_rec = 1; g_rec_gen = ipc_generation();
    g_rec_tm = lv_timer_create(out_recover_tick, 500, NULL);
}
/* resume only the same track, and only if the seek and the play toggle were accepted */
static void out_resume(int moved){
    if(!g_osw.was_playing) return;
    track_state_t st; ipc_get_state(&st);
    if(moved && (ipc_generation() != g_osw.gen || !st.have_track || strcmp(st.path, g_osw.path) != 0)){ ui_toast("Output switched - press play"); return; }
    if(moved && g_osw.pos > 0 && ui_seek_to(g_osw.pos) < 0){ ui_toast("Couldn't resume - press play"); return; }
    if(ipc_send_cmd(OUT_PAUSE) < 0) ui_toast("Couldn't resume - press play");   /* toggle: plays again */
}
static int out_run(void){
    int target = g_osw.target;
    int rc = out_seq_send(target, g_osw.full);
    if(rc == -1){ out_set_state(modes_output_route()); ui_toast("Couldn't switch output"); out_resume(0); modes_ui_refresh(); return -1; }
    if(rc == 0){ out_set_state(target); out_resume(1); modes_ui_refresh(); return 0; }
    /* the route moved but the tail failed: back to the internal DAC; never end without 0657 8 landing */
    if(out_seq_send(OUT_INTERNAL, 1) == 0){
        out_set_state(OUT_INTERNAL);
        ui_toast("Output switch failed - internal DAC restored");
        out_resume(1); modes_ui_refresh();
        return -1;
    }
    out_rec_enter();
    ui_toast("Output state uncertain - recovering");
    modes_ui_refresh();
    return -1;
}
static void osw_stop(const char *why, int resume){
    lv_timer_del(g_osw.tm); g_osw.tm = NULL;
    out_set_state(modes_output_route());
    ui_toast(why);
    if(resume) out_resume(0);
    modes_ui_refresh();
}
static void osw_tick(lv_timer_t *t){
    (void)t;
    track_state_t st; ipc_get_state(&st);
    if(ipc_generation() != g_osw.gen){ osw_stop("Player restarted - try again", 0); return; }
    if(strcmp(st.path, g_osw.path) != 0){ osw_stop("Track changed - try again", 0); return; }
    if(lv_tick_elaps(g_osw.t0) >= OUT_WAIT_MS){ osw_stop("Player is busy - try again", 0); return; }   /* deadline first: a late tick never routes, even with the hold complete */
    if(ui_is_playing() || !out_pcm_quiet()) g_osw.quiet0 = 0;
    else if(!g_osw.quiet0) g_osw.quiet0 = lv_tick_get() ? lv_tick_get() : 1;
    if(!g_osw.quiet0 || lv_tick_elaps(g_osw.quiet0) < OUT_QUIET_MS){
        return;
    }
    const char *why = ui_output_blocked();
    if(why){ osw_stop(why, g_osw.was_playing); return; }
    if(g_osw.target == OUT_USB && !out_usb_card_ok()){ osw_stop("USB DAC not found", g_osw.was_playing); return; }   /* re-checked right before 0666 3 */
    lv_timer_del(g_osw.tm); g_osw.tm = NULL;
    out_run();
}
static int out_switch(int target, int via_mode){
    if(target < OUT_INTERNAL || target > OUT_USB) return -1;
    if(modes_output_busy()){ ui_toast("Switching..."); return -1; }
    const char *why = ui_output_blocked();
    if(why){ out_set_state(modes_output_route()); ui_toast(why); return -1; }
    if(target == OUT_USB && !out_usb_card_ok()){ out_set_state(modes_output_route()); ui_toast("Connect a USB DAC first"); return -1; }
    int was_playing = ui_is_playing();
    if(!was_playing && !out_pcm_quiet()){ out_set_state(modes_output_route()); ui_toast("Player is busy - try again"); return -1; }   /* PCM live but not "playing": a pause toggle would START it */
    track_state_t st; ipc_get_state(&st);
    memset(&g_osw, 0, sizeof g_osw);
    g_osw.target = target; g_osw.was_playing = was_playing;
    g_osw.full = via_mode || target == OUT_USB || modes_output_route() == OUT_USB;   /* Settings toggle = stock's 2-frame form, except to/from USB */
    g_osw.gen = ipc_generation(); g_osw.pos = st.have_track ? st.position_ms : 0;
    snprintf(g_osw.path, sizeof g_osw.path, "%s", st.have_track ? st.path : "");
    if(was_playing && ipc_send_cmd(OUT_PAUSE) < 0){ out_set_state(modes_output_route()); ui_toast("Player is busy - try again"); return -1; }
    g_osw.t0 = lv_tick_get();
    g_osw.tm = lv_timer_create(osw_tick, 100, NULL);
    return 0;
}
int modes_output_switch(int target){ return out_switch(target, 0); }
int modes_output_mode_switch(int target){ return out_switch(target, 1); }
/* OUTPUT-ROUTE-END */
static orbit_t g_orb;                /* six modes orbiting a hub; the hub's ring shows the state */
static int slot_of_mode(int m){ for(int i=0;i<N_MODES;i++) if(MODES[i].mode == m) return i; return -1; }

/* Disco: the modes as rows (the Settings look); BT streaming left out (it's ordinary Bluetooth playback) */
static const int DISCO_ORD[] = { 0, 5, 2, 1, 4 };            /* MODES[] slots: Local, USB storage, BT DAC, USB DAC, AirPlay */
#define N_DISCO ((int)(sizeof DISCO_ORD / sizeof DISCO_ORD[0]))
static lv_obj_t *g_drow[N_MODES];
/* while a mode changes: the turning-arrows icon spins (instead of a "Switching..." text) */
static void spin_exec(void *o, int32_t v){ lv_obj_set_style_transform_rotation((lv_obj_t *)o, v, 0); }
static void icon_spin(lv_obj_t *l, int on){
    if(!l) return;
    if(!on){ lv_anim_delete(l, spin_exec); lv_obj_set_style_transform_rotation(l, 0, 0); return; }
    if(lv_anim_get(l, spin_exec)) return;                       /* already turning */
    lv_obj_update_layout(l);
    lv_obj_set_style_transform_pivot_x(l, lv_obj_get_width(l) / 2, 0); lv_obj_set_style_transform_pivot_y(l, lv_obj_get_height(l) / 2, 0);
    lv_anim_t a; lv_anim_init(&a); lv_anim_set_var(&a, l); lv_anim_set_exec_cb(&a, spin_exec);
    lv_anim_set_values(&a, 0, 3600); lv_anim_set_duration(&a, 1000); lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);
}
static void disco_paint(int slot, int pending){
    lv_color_t acc = ui_current_accent();
    for(int i = 0; i < N_MODES; i++){
        lv_obj_t *r = g_drow[i]; if(!r) continue;
        int on = (i == slot);
        lv_obj_set_style_border_width(r, on ? 2 : 0, 0); lv_obj_set_style_border_color(r, acc, 0);
        lv_obj_set_style_text_color(lv_obj_get_child(r, 0), on ? acc : TC(TEXT_SECONDARY), 0);
        lv_obj_t *v = lv_obj_get_child(r, 2);
        lv_label_set_text(v, on ? (pending ? LV_SYMBOL_REFRESH : LV_SYMBOL_OK) : "");
        lv_obj_set_style_text_color(v, acc, 0);
        icon_spin(v, on && pending);                                    /* the arrows turn while it switches */
    }
}
static void paint(int slot, const char *glyph){   /* slot = orbit index, or -1 for none */
    if(th_disco()){ disco_paint(slot, glyph && !strcmp(glyph, LV_SYMBOL_REFRESH)); return; }
    int pending = (glyph && !strcmp(glyph, LV_SYMBOL_REFRESH));
    lv_color_t acc = TC(ACCENT_PRIMARY);
    if(pending){                                  /* the tapped mode gets a ring; the hub's ring spins */
        orbit_set_pending(&g_orb, slot, acc);
        orbit_hub_set(&g_orb, LV_SYMBOL_REFRESH, acc, "");                /* turning arrows, no text */
        icon_spin(g_orb.hub_icon, 1);
        orbit_hub_ring(&g_orb, ORBIT_RING_SPIN, acc, 0);
        return;
    }
    orbit_set_pending(&g_orb, -1, acc);
    icon_spin(g_orb.hub_icon, 0);
    for(int i = 0; i < N_MODES; i++) orbit_set_on(&g_orb, i, i == slot, acc);   /* the active mode: filled */
    if(slot >= 0){ orbit_hub_set(&g_orb, MODES[slot].glyph, acc, "Back"); orbit_hub_ring(&g_orb, ORBIT_RING_FULL, acc, 0); }
    else { orbit_hub_set(&g_orb, LV_SYMBOL_SD_CARD, TC(TEXT_PRIMARY), "Back"); orbit_hub_ring(&g_orb, ORBIT_RING_GREY, acc, 0); }
}
static void mark_selected_mode(int cur){ paint(slot_of_mode(cur), LV_SYMBOL_OK); }
static void mark_selected(void){ mark_selected_mode(ui_get_source_mode()); }
static void modes_ui_refresh(void){ mark_selected(); }


static void hub_back_cb(lv_event_t *e){ (void)e; screen_back(); }
static int  cur_mode(void);
static void modeinfo_refresh_and_show(void);
static uint32_t g_last_switch = 0;   /* debounce: a switch takes a few seconds to apply in the player */

/* Pending state: the tapped row shows a "switching" glyph (not the confirmed checkmark) while the
 * gadget switch is in flight - there is no source-mode completion readback, so after the switch
 * window we settle to the selection best-effort (matches the honest "Switching..." toast). */
static void mark_pending(int m){ paint(slot_of_mode(m), LV_SYMBOL_REFRESH); }

static lv_timer_t *g_settle = NULL;
static int g_pending_mode = -1;   /* the mode a switch is settling to (so reopening the screen keeps showing "switching") */
/* M17: after the switch window, settle to the CONFIRMED mode read from the real USB gadget state,
 * not a blind assumption. If the gadget shows the switch didn't take, reflect reality + say so. */
static void settle_cb(lv_timer_t *t){
    (void)t;
    if(g_osw.tm) return;
    if(ui_source_switch_pending()) return;
    if(lv_tick_elaps(g_last_switch) < 3200) return;
    lv_timer_del(g_settle); g_settle = NULL;
    int intended = g_pending_mode; g_pending_mode = -1;
    if(ui_source_switch_failed()){ mark_selected_mode(-1); return; }
    /* M17: DISPLAY the ACTUAL gadget state (read-only) instead of a blind timer assumption. Do NOT
     * mutate the intent mirror (g_source_mode) - it also guards coldplug, and a transient mid-transition
     * sample must not flip that guard. */
    int show = (intended >= 0) ? intended : ui_get_source_mode();
    if(intended == 0 || intended == 1 || intended == 3){   /* USB gadget modes are readback-confirmable */
        int actual = ui_detect_source_mode();
        show = actual;
        if(actual != intended) ui_toast("Mode didn't switch");
    }
    /* intended == 2 (BT receiving) is gadget-invisible and needs a phone to connect - no reliable
     * readback here, so show the intent without asserting a false confirmation. */
    mark_selected_mode(show);
    if(show != 0 && screen_current() == SCR_WORKMODE) modeinfo_refresh_and_show();   /* the mode's own screen */
}

static void row_cb(int slot){                 /* a tap inside a slice (the wheel does the hit-testing) */
    if(slot < 0 || slot >= N_MODES) return;
    int m = MODES[slot].mode;
    /* Serialise: ignore taps while the previous switch is still applying (the player's gadget
     * state-machine is asynchronous). NB we do NOT early-return on "same mode" - re-issuing must
     * always be allowed so Local works as a recover even if our cached mode is stale. */
    if(g_last_switch && lv_tick_elaps(g_last_switch) < 3000) return;   /* still switching: the arrows are turning */
    g_last_switch = lv_tick_get();
    if(m == ROW_USB_AUDIO){   /* USB Audio: an output route of the Local source, not a source mode */
        if(modes_output_mode_switch(OUT_USB) == 0){
            g_pending_mode = m; mark_pending(m);
            if(g_settle) lv_timer_del(g_settle);
            g_settle = lv_timer_create(settle_cb, 500, NULL);
            ui_toast("Switching to USB audio output");
        }
        return;
    }
    if(ui_set_source_mode(m) == 0){
        g_pending_mode = m;
        mark_pending(m);      /* async switch in flight: show "switching", not a confirmed selection */
        if(g_settle) lv_timer_del(g_settle);
        g_settle = lv_timer_create(settle_cb, 500, NULL);   /* settle to the checkmark after the switch window */
        /* honest wording: the frames are queued; the async switch completes a moment later. */
        static const char *msg[] = {
            "Switching to playback", "Switching to USB DAC",
            "Switching to Bluetooth receiving", "Switching to USB storage",
            "Bluetooth streaming on", "AirPlay on \xE2\x80\x93 pick the Disc on your device" };
        ui_toast(msg[m]);
    }
}

void modes_open(void){
    if(g_settle && g_pending_mode >= 0) mark_pending(g_pending_mode);  /* a switch is still settling - keep showing it */
    else {
        mark_selected();
        if(cur_mode() != 0){ modeinfo_refresh_and_show(); return; }       /* a mode is active: its screen first */
    }
    screen_show(SCR_WORKMODE);
}

static void disco_row_cb(lv_event_t *e){ row_cb((int)(intptr_t)lv_event_get_user_data(e)); }
void modes_create(lv_obj_t *root){
    lv_obj_set_style_bg_color(root, TC(CANVAS), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);

    /* back button (kept out of the clipped top-left corner) */
    /* no header: the wheel fills the panel and its hub is the way back */

    if(th_disco()){
        disco_title(root, "Working mode");
        static const char *NM[N_MODES] = { "Playback", "USB DAC", "Bluetooth DAC", "BT streaming", "AirPlay", "USB storage" };
        memset(g_drow, 0, sizeof g_drow);
        for(int k = 0; k < N_DISCO; k++){
            int i = DISCO_ORD[k];
            g_drow[i] = disco_row(root, 62 + k * 54, 48, MODES[i].glyph, NULL, NM[i], disco_row_cb, (void *)(intptr_t)i);
        }
        mark_selected();
        return;
    }
    /* orbit: six modes, clockwise from the upper right; the top stays open for the title */
    orbit_title(root, "Working mode");
    static char caps[N_MODES][32];
    orbit_item_t it[N_MODES];
    for(int i = 0; i < N_MODES; i++){
        snprintf(caps[i], sizeof caps[i], "%s%s%s", MODES[i].l1, MODES[i].l2[0] ? " " : "", MODES[i].l2);
        it[i].glyph = MODES[i].glyph; it[i].cap = caps[i];
    }
    orbit_create(&g_orb, root, it, N_MODES, -60, row_cb);
    orbit_hub_create(&g_orb, root, hub_back_cb, LV_SYMBOL_SD_CARD, "Back");
    mark_selected();
    if(th_braun()) br_face(root);                          /* Braun: the grille face (knobs come from orbit.c) */
}


/* ================================================================== the active mode's screen (SCR_MODEINFO)
 * One screen for USB DAC / USB storage / BT DAC / BT streaming / AirPlay: a big icon in a status ring (Ring) or
 * on a dark knob with its lamp (Braun), the mode name, one status line and one detail line, and two buttons:
 * Modes (the picker) and Local (back to normal playback). Everything shown is read from the system in
 * cheap, non-blocking ways once a second while the screen is up. */
static lv_obj_t *g_mi_ring, *g_mi_glyph, *g_mi_knob, *g_mi_title, *g_mi_status, *g_mi_detail1, *g_mi_detail2;
static lv_timer_t *g_mi_timer;
static int g_mi_mode = -1, g_mi_wait = -2;

static int cur_mode(void){                                   /* the intent mirror, or the real gadget after a UI restart */
    int m = ui_get_source_mode();
    if(m == 0 && !(g_last_switch && lv_tick_elaps(g_last_switch) < 8000)){   /* not while a switch to Local is still tearing the gadget down */
        int d = ui_detect_source_mode(); if(d) m = d;
    }
    return m;
}
static int udc_host_connected(void){                         /* the USB device controller reports "configured" once a host enumerated us */
    DIR *d = opendir("/sys/class/udc"); if(!d) return 0;
    int ok = 0; struct dirent *e;
    while(!ok && (e = readdir(d))){
        if(e->d_name[0] == '.') continue;
        char p[128], b[32] = {0}; snprintf(p, sizeof p, "/sys/class/udc/%.60s/state", e->d_name);
        FILE *f = fopen(p, "r"); if(!f) continue;
        if(fgets(b, sizeof b, f)) ok = !strncmp(b, "configured", 10);
        fclose(f);
    }
    closedir(d); return ok;
}
static long card_gb(void){                                   /* SD capacity from the block device, readable while it is exported */
    FILE *f = fopen("/sys/block/mmcblk0/size", "r"); if(!f) return 0;
    long long sec = 0; if(fscanf(f, "%lld", &sec) != 1) sec = 0; fclose(f);
    return (long)((sec * 512LL + 500000000LL) / 1000000000LL);
}
static void mi_spin_exec(void *var, int32_t v){ lv_arc_set_rotation((lv_obj_t *)var, v % 360); }
static void mi_set_wait(int wait){                           /* Ring: a turning arc while waiting, a full ring when connected */
    if(wait == g_mi_wait) return;
    g_mi_wait = wait;
    if(g_mi_knob){ br_knob_set_on(g_mi_knob, !wait); return; }
    if(!g_mi_ring) return;
    lv_anim_delete(g_mi_ring, mi_spin_exec);
    lv_obj_set_style_arc_color(g_mi_ring, ui_current_accent(), LV_PART_INDICATOR);
    if(wait){
        lv_arc_set_value(g_mi_ring, 260);
        lv_anim_t a; lv_anim_init(&a); lv_anim_set_var(&a, g_mi_ring); lv_anim_set_exec_cb(&a, mi_spin_exec);
        lv_anim_set_values(&a, 270, 270 + 360); lv_anim_set_duration(&a, 1400);
        lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE); lv_anim_start(&a);
    } else { lv_arc_set_rotation(g_mi_ring, 270); lv_arc_set_value(g_mi_ring, 1000); }
}
static void mi_text(lv_obj_t *l, const char *t){ if(l && strcmp(lv_label_get_text(l), t)) lv_label_set_text(l, t); }

static void modeinfo_fill(int m);
void modeinfo_refresh(void){
    int m = cur_mode();
    if(m == 0){ screen_back(); return; }                     /* nothing active: nothing to show */
    modeinfo_fill(m);
}
void modes_test_pending(int m){ mark_pending(m); }           /* host renders */
void modes_test_info(int m){ modeinfo_fill(m); }
static void modeinfo_fill(int m){
    if(m < 0 || m > 5) m = 1;
    int slot = slot_of_mode(m); if(slot < 0) slot = 1;       /* the SAME table the orbit menu is built from: identical icon + name */
    if(m != g_mi_mode){
        char nm[40]; snprintf(nm, sizeof nm, "%s%s%s", MODES[slot].l1, MODES[slot].l2[0] ? " " : "", MODES[slot].l2);
        g_mi_mode = m; g_mi_wait = -2; mi_text(g_mi_glyph, MODES[slot].glyph); mi_text(g_mi_title, nm);
    }
    char st[64] = "", d1[200] = "", d2[200] = ""; int wait = 1;
    track_state_t ts; ipc_get_state(&ts);
    switch(m){
        case 1: {                                            /* USB DAC */
            int host = udc_host_connected(); wait = !host;
            snprintf(st, sizeof st, host ? "Connected" : "Waiting for USB host");
            if(host){
                snprintf(d1, sizeof d1, "Volume %d", ts.volume > 0 ? ts.volume : 0);
                if(ts.sample_rate > 0) snprintf(d2, sizeof d2, "%d.%d kHz", ts.sample_rate / 1000, (ts.sample_rate % 1000) / 100);
            } else snprintf(d1, sizeof d1, "Plug the Disc into a computer");
        } break;
        case 3: {                                            /* USB storage */
            int host = udc_host_connected(); wait = !host;
            snprintf(st, sizeof st, host ? "Connected to computer" : "Waiting for USB host");
            long gb = card_gb(); if(gb > 0) snprintf(d1, sizeof d1, "SD card %ld GB", gb);
            snprintf(d2, sizeof d2, "Eject on the computer first");
        } break;
        case 2: {                                            /* BT DAC: a phone / computer plays to the Disc */
            char nm[64]; int c = bt_peer_name(nm, sizeof nm); wait = !c;
            snprintf(st, sizeof st, c ? "Connected" : "Waiting for a device");
            if(c) snprintf(d1, sizeof d1, "%s", nm); else snprintf(d1, sizeof d1, "Pair from the other device");
            snprintf(d2, sizeof d2, "Volume %d", ts.volume > 0 ? ts.volume : 0);
        } break;
        case 4: {                                            /* BT streaming: the Disc plays to a speaker */
            char nm[64]; int c = bt_peer_name(nm, sizeof nm); wait = !c;
            snprintf(st, sizeof st, c ? "Streaming to" : "No speaker connected");
            if(c) snprintf(d1, sizeof d1, "%s", nm); else snprintf(d1, sizeof d1, "Pair one in Bluetooth settings");
        } break;
        case 5: {                                            /* AirPlay */
            int has = ts.have_track && ts.title[0]; wait = !has;
            snprintf(st, sizeof st, has ? "Playing" : "Ready");
            if(has){ snprintf(d1, sizeof d1, "%s", ts.title); snprintf(d2, sizeof d2, "%s", ts.artist); }
            else snprintf(d1, sizeof d1, "Pick the Disc on your device");
        } break;
    }
    mi_set_wait(wait);
    mi_text(g_mi_status, st); mi_text(g_mi_detail1, d1); mi_text(g_mi_detail2, d2);
}
static void modeinfo_refresh_and_show(void){ screen_show(SCR_MODEINFO); }
static void mi_tick(lv_timer_t *t){ (void)t; if(screen_current() == SCR_MODEINFO) modeinfo_refresh(); }
static void mi_modes_cb(lv_event_t *e){ if(lv_event_get_code(e) == LV_EVENT_CLICKED) screen_show(SCR_WORKMODE); }
static lv_obj_t *mi_pill(lv_obj_t *root, const char *txt, int x, int primary, lv_event_cb_t cb){
    lv_obj_t *b = lv_button_create(root);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, 104, 40); lv_obj_align(b, LV_ALIGN_CENTER, x, 128);
    lv_obj_set_ext_click_area(b, 4);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    uint32_t bg = th_braun() ? (primary ? BR_ACC : BR_KNOB) : (primary ? TH_ACCENT : TH_SURF1);
    lv_obj_set_style_bg_color(b, theme_color_from_rgb(bg), 0); lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l = lv_label_create(b); lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, th_braun() ? br_font(14, 0) : TH_F_DETAIL, 0);
    lv_obj_set_style_text_color(l, th_braun() ? TC(ON_CONTROL) : TC(TEXT_PRIMARY), 0);
    lv_obj_center(l);
    return b;
}
static lv_obj_t *mi_label(lv_obj_t *root, const lv_font_t *font, uint32_t col, int y, int w){
    lv_obj_t *l = lv_label_create(root);
    lv_label_set_text(l, "");
    lv_obj_set_style_text_font(l, font, 0); lv_obj_set_style_text_color(l, theme_color_from_rgb(col), 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_set_width(l, w); lv_obj_align(l, LV_ALIGN_CENTER, 0, y);
    return l;
}
void modeinfo_create(lv_obj_t *root){
    lv_obj_set_style_bg_color(root, TC(CANVAS), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    int br = th_braun();
    if(br) br_face(root);
    if(br){
        g_mi_knob = br_knob(root, 180, 104, 56, LV_SYMBOL_USB, TF(UI_48));
        g_mi_glyph = lv_obj_get_child(g_mi_knob, 0);                        /* the icon label the knob made */
        if(g_mi_glyph && !lv_obj_check_type(g_mi_glyph, &lv_label_class)) g_mi_glyph = NULL;
    } else {
        g_mi_ring = lv_arc_create(root);
        lv_obj_remove_style(g_mi_ring, NULL, LV_PART_KNOB);
        lv_obj_remove_flag(g_mi_ring, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_size(g_mi_ring, 132, 132); lv_obj_align(g_mi_ring, LV_ALIGN_CENTER, 0, -76);
        lv_arc_set_bg_angles(g_mi_ring, 0, 360); lv_arc_set_range(g_mi_ring, 0, 1000); lv_arc_set_rotation(g_mi_ring, 270);
        lv_obj_set_style_arc_width(g_mi_ring, 6, LV_PART_MAIN); lv_obj_set_style_arc_color(g_mi_ring, TC(CONTROL_TRACK), LV_PART_MAIN);
        lv_obj_set_style_arc_width(g_mi_ring, 6, LV_PART_INDICATOR); lv_obj_set_style_arc_rounded(g_mi_ring, true, LV_PART_INDICATOR);
        g_mi_glyph = lv_label_create(root);
        lv_label_set_text(g_mi_glyph, LV_SYMBOL_USB);
        lv_obj_set_style_text_font(g_mi_glyph, TF(UI_48), 0);
        lv_obj_set_style_text_color(g_mi_glyph, TC(TEXT_PRIMARY), 0);
        lv_obj_align(g_mi_glyph, LV_ALIGN_CENTER, 0, -76);
    }
    g_mi_title   = mi_label(root, br ? br_font(22, 1) : TH_F_TITLE,  br ? BR_TXT  : TH_TXT1, 6,  260);
    g_mi_status  = mi_label(root, br ? br_font(16, 0) : TF(UI_16), br ? BR_ACC : TH_ACCENT, 36, 260);
    g_mi_detail1 = mi_label(root, br ? br_font(14, 0) : TH_F_DETAIL, br ? BR_TXT2 : TH_TXT2, 64, 250);
    g_mi_detail2 = mi_label(root, br ? br_font(14, 0) : TH_F_DETAIL, br ? BR_TXT3 : TH_TXT3, 86, 250);
    mi_pill(root, "Modes", 0, 0, mi_modes_cb);                       /* back to Playback: from the Modes list */
    g_mi_timer = lv_timer_create(mi_tick, 1000, NULL);
}
