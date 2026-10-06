/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
/* The round equalizer. Ten bands as pizza slices around a centre, each filled out to its gain:
 * a soft accent fill up to the dashed 0 dB ring, a bright one for boosts beyond it, a faint line per dB.
 *   - range -6..+6 dB in 0.1 dB steps; each band's centre frequency is free (Q preserves stock 0.7/0.71, peaking filter)
 *   - tap a slice to select it; drag outward / inward to set its gain roughly
 *   - the centre shows the band's frequency and gain: tap one to choose what - / + change
 *     (gain: 0.1 dB a tap; frequency: 1/12 octave a tap; hold to repeat); tap the active value to type it;
 *     long-press the frequency to reset that band to its standard frequency
 *   - the preset button cycles Off -> the player's built-ins -> USER1..USER10
 * Built-in presets (and Off) are the player's own: shown in grey, read-only. USER presets are written to the
 * player's PEQ table in the stock format (per band: filterType 0, frequency, gain "x.y", qValue "0.70" or "0.71") and
 * selected with 0689. A preset whose filters the round editor can't represent (another filter type or Q)
 * is shown and never overwritten. Selecting a preset only selects it - writes happen on an edit. */
#include "screens.h"
#include "fwcaps.h"
#include "eqrate.h"
#include "theme_kit.h"
#include "braun.h"
#include "theme.h"
#include "config.h"
#include "musicdb.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdatomic.h>

#define NB       10
#define NPRESET  21                       /* 0 Off, 1..10 built-in, 11..20 USER1..10 */
#define USER_MIN 11
#define CX 180
#define CY 180
#define RMIN 72                           /* -6 dB */
#define R0   115                          /*  0 dB */
#define RMAX 158                          /* +6 dB */
#define GMAX 60                           /* tenths of a dB */
#define HALF 16.0f                        /* slice half-width in degrees (36 per band, 4 deg gaps) */

static const int STDF[NB] = { 32, 64, 125, 250, 500, 1000, 2000, 4000, 8000, 16000 };
static const char *const BUILTIN[11] = { "Off", "Jazz", "Rock", "R&B", "Hip-Hop", "Pop", "Dance", "Classical", "Retro", "Sibilance 1", "Sibilance 2" };

static int g_preset, g_editable, g_parametric, g_readfail;
static int g_t[NB], g_f[NB];              /* gain in tenths, frequency in Hz */
static double g_master, g_q[NB];
static int g_save_busy;
static _Atomic int g_save_done;
static struct {int preset,hz[NB],gains[NB],result;double master;char json[1200];} g_save;
static void *save_worker(void *unused){
    (void)unused;
    g_save.result=mdb_set_peq(g_save.preset,g_save.master,g_save.json,g_save.hz);
    atomic_store_explicit(&g_save_done,1,memory_order_release);return NULL;
}
static int g_sel = -1, g_mode_freq = 0, g_dirty_draw, g_changed;
static lv_obj_t *g_root, *g_canvas, *g_hit, *g_rim[NB], *g_pill_lbl, *g_flbl, *g_glbl, *g_minus, *g_plus, *g_modecap, *g_note, *g_lock;
static uint8_t *g_buf;
static lv_timer_t *g_draw_tmr;
static int g_press_d, g_dragging, g_drag_ok;
static lv_point_t g_press_pt;                      /* where the touch began (a tap barely moves) */   /* g_drag_ok: this touch began on the ALREADY selected band */

/* ------------------------------------------------------------------ helpers */
static float rr(int tenths){ return R0 + (tenths / 10.0f) * ((RMAX - R0) / 6.0f); }
static void fmt_gain(char *b, size_t n, int t){ snprintf(b, n, "%s%d.%d dB", t < 0 ? "-" : "+", abs(t) / 10, abs(t) % 10); }
static void fmt_freq(char *b, size_t n, int hz, int with_unit){
    if(hz < 1000) snprintf(b, n, with_unit ? "%d Hz" : "%d", hz);
    else if(hz % 1000 == 0) snprintf(b, n, with_unit ? "%d kHz" : "%dk", hz / 1000);
    else { int k = hz / 1000, d = (hz % 1000 + 50) / 100; if(d == 10){ k++; d = 0; }
           if(d == 0) snprintf(b, n, with_unit ? "%d kHz" : "%dk", k); else snprintf(b, n, with_unit ? "%d.%d kHz" : "%d.%dk", k, d); }
}
static void preset_name(int p, char *b, size_t n){
    char key[24];snprintf(key,sizeof key,"eq_name%d",p);const char *name=cfg_get_str(key,NULL);
    if(p>=USER_MIN && name && *name){snprintf(b,n,"%s",name);return;}
    if(p <= 10) snprintf(b, n, "%s", BUILTIN[p < 0 ? 0 : p]); else snprintf(b, n, "USER%d", p - 10);
}
static int clampi(int v, int lo, int hi){ return v < lo ? lo : v > hi ? hi : v; }
static int band_lo(int i){ return i == 0 ? 20 : (int)(g_f[i - 1] * 1.03f) + 1; }       /* stay between neighbours */
static int band_hi(int i){ return i == NB - 1 ? 20000 : (int)(g_f[i + 1] / 1.03f) - 1; }

/* ------------------------------------------------------------------ player I/O */
static int apply_now(void){                                /* enqueue a frozen USER curve; main timer applies after save */
    if(g_save_busy || !g_editable || !fw_custom_peq_curve_writable(g_f,NB) || !isfinite(g_master) || fabs(g_master)>30) return 0;
    for(int i=0;i<NB;i++) if(g_t[i]<-120 || g_t[i]>120) return 0;
    char json[1200]; int n = snprintf(json, sizeof json, "[");
    for(int i = 0; i < NB; i++){
        int t = g_t[i];
        n += snprintf(json + n, sizeof json - n,
            "%s{\"filterType\":0,\"frequency\":%d,\"position\":%d,\"gain\":\"%s%d.%d\",\"qValue\":\"%.2f\"}",
            i ? "," : "", g_f[i], i, t < 0 ? "-" : "", abs(t) / 10, abs(t) % 10, g_q[i]);
        if(n >= (int)sizeof json - 4) return 0;
    }
    snprintf(json + n, sizeof json - n, "]");
    g_save.preset=g_preset;g_save.master=g_master;
    memcpy(g_save.hz,g_f,sizeof g_f);memcpy(g_save.gains,g_t,sizeof g_t);
    memcpy(g_save.json,json,(size_t)n+2);
    atomic_store(&g_save_done,0);g_save_busy=1;
    pthread_t th;
    if(pthread_create(&th,NULL,save_worker,NULL)){g_save_busy=0;return 0;}
    pthread_detach(th);g_editable=0;g_drag_ok=0;return 1;
}
static void persist_saved(void){                                 /* a mirror in cfg (the player's table is the truth) */
    char k[24];
    for(int i = 0; i < NB; i++){
        snprintf(k, sizeof k, "eq%d_t%d", g_save.preset - 10, i); cfg_set_int_deferred(k, g_save.gains[i]);
        snprintf(k, sizeof k, "eq%d_f%d", g_save.preset - 10, i); cfg_set_int_deferred(k, g_save.hz[i]);
    }
    cfg_flush();
}
static void load(int preset){
    g_preset = clampi(preset, 0, NPRESET - 1);
    g_editable = g_parametric = g_readfail = 0; g_master = 0;
    for(int i = 0; i < NB; i++){ g_t[i] = 0; g_f[i] = STDF[i]; g_q[i]=0.7; }
    if(g_preset == 0) return;                               /* Off: flat, grey */
    int ed = 1, r = mdb_get_peq_ex(g_preset, &g_master, g_t, g_f, &ed, g_q);
    if(r < 0){ g_readfail = 1; return; }                    /* unknown curve: show flat, never write */
    if(g_preset >= USER_MIN){
        if(!ed) g_parametric = 1;                           /* filters we can't represent: show, don't overwrite */
        else g_editable = fw_custom_peq_curve_writable(g_f,NB);
    }
    /* Display constraints must never mutate the saved curve. Unsupported order
     * or spacing is view-only, including on older firmware with custom editing. */
    for(int i=0;i<NB;i++)if(g_f[i]<band_lo(i) || g_f[i]>band_hi(i)){
        g_editable=0;g_parametric=1;
    }
    if(g_save_busy)g_editable=0;
}

/* ------------------------------------------------------------------ drawing */

static float clampf(float v){ return v < 0 ? 0 : v > 1 ? 1 : v; }
/* The dial's geometry never changes, so it's worked out once: every ring pixel gets its band, its distance
 * from the centre (1/16 px), its edge coverage and whether a dB line or the dashed 0 dB ring crosses it -
 * grouped by band. A change then repaints only that band's pixels (integer maths, no trig) and redraws only
 * that band's area of the screen. (Painting every pixel with atan2/sqrt on each change was far too slow on
 * the player's CPU.) */
typedef struct { uint32_t off; uint16_t r16; uint8_t cov; int8_t kind; } px_t;   /* kind: 0 none, 1..12 dB line, 13 dashed 0 dB */
static px_t *g_px; static int g_bstart[NB + 1];
static lv_area_t g_bbox[NB];
static int g_lvl16[NB] = { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };   /* last painted level (1/16 px) per band */
static int g_lastsel = -2, g_lastgrey = -1; static uint32_t g_lastacc;
/* ---- Braun: a fader bank (ten slots cut into the grille, dark caps, a lamp above the selected band) ---- */
#define FX0   58
#define FSTEP 27.1f
#define FTOP  70
#define FBOT  214
#define FMID  ((FTOP + FBOT) / 2)
#define FKPX  ((FBOT - FTOP) / 2.0f / 6.0f)                 /* px per dB */
static lv_obj_t *g_fslot[NB], *g_fcap[NB], *g_fline[NB], *g_flamp;
static int fx(int i){ return (int)lroundf(FX0 + i * FSTEP); }
static void braun_faders_create(lv_obj_t *root){
    lv_obj_t *zero = lv_obj_create(root); lv_obj_remove_style_all(zero);        /* the 0 dB line */
    lv_obj_set_size(zero, 272, 1); lv_obj_set_pos(zero, 44, FMID);
    lv_obj_set_style_bg_color(zero, TC(EQ_ZERO), 0); lv_obj_set_style_bg_opa(zero, LV_OPA_COVER, 0);
    static const char *const SC[3] = { "+6", "0", "\xE2\x88\x92" "6" };
    for(int k = 0; k < 3; k++){ lv_obj_t *l = br_label(root, SC[k], br_font(12, 0), BR_TXT3);
        lv_obj_align(l, LV_ALIGN_TOP_LEFT, 38, (k == 0 ? FTOP : k == 1 ? FMID : FBOT) - 8); }
    for(int i = 0; i < NB; i++){
        g_fslot[i] = lv_obj_create(root); lv_obj_remove_style_all(g_fslot[i]);
        lv_obj_set_size(g_fslot[i], 6, FBOT - FTOP); lv_obj_set_pos(g_fslot[i], fx(i) - 3, FTOP);
        lv_obj_set_style_radius(g_fslot[i], 3, 0); lv_obj_set_style_bg_color(g_fslot[i], TC(EQ_SLOT), 0); lv_obj_set_style_bg_opa(g_fslot[i], LV_OPA_COVER, 0);
        g_fcap[i] = lv_obj_create(root); lv_obj_remove_style_all(g_fcap[i]);
        lv_obj_set_size(g_fcap[i], 18, 12); lv_obj_set_style_radius(g_fcap[i], 2, 0);
        lv_obj_set_style_bg_color(g_fcap[i], TC(CONTROL_KNOB), 0); lv_obj_set_style_bg_opa(g_fcap[i], LV_OPA_COVER, 0);
        lv_obj_set_style_shadow_color(g_fcap[i], TC(ACTION_SHADOW), 0); lv_obj_set_style_shadow_width(g_fcap[i], 4, 0); lv_obj_set_style_shadow_offset_y(g_fcap[i], 1, 0);
        g_fline[i] = lv_obj_create(g_fcap[i]); lv_obj_remove_style_all(g_fline[i]);
        lv_obj_set_size(g_fline[i], 12, 2); lv_obj_center(g_fline[i]);
        lv_obj_set_style_bg_opa(g_fline[i], LV_OPA_COVER, 0);
        lv_obj_clear_flag(g_fslot[i], LV_OBJ_FLAG_CLICKABLE); lv_obj_clear_flag(g_fcap[i], LV_OBJ_FLAG_CLICKABLE); lv_obj_clear_flag(g_fline[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_text_font(g_rim[i], br_font(12, 0), 0);
        lv_obj_align(g_rim[i], LV_ALIGN_TOP_MID, fx(i) - 180, FBOT + 6);
    }
    g_flamp = br_disc(root, 0, 0, 3, BR_ACC);
}
static void braun_faders_paint(void){
    for(int i = 0; i < NB; i++){
        int t = clampi(g_t[i], -GMAX, GMAX);
        int y = (int)lroundf(FMID - t / 10.0f * FKPX);
        lv_obj_set_pos(g_fcap[i], fx(i) - 9, y - 6);
        lv_obj_set_style_bg_color(g_fcap[i], g_editable ? TC(CONTROL_KNOB) : TC(EQ_CAP_DISABLED), 0);
        lv_obj_set_style_bg_color(g_fline[i], i == g_sel && g_editable ? TC(ACCENT_PRIMARY) : g_editable ? TC(EQ_FADER) : TC(EQ_FADER_DISABLED), 0);
    }
    if(g_sel >= 0 && g_editable){ lv_obj_set_pos(g_flamp, fx(g_sel) - 3, FTOP - 12); lv_obj_remove_flag(g_flamp, LV_OBJ_FLAG_HIDDEN); }
    else lv_obj_add_flag(g_flamp, LV_OBJ_FLAG_HIDDEN);
}
/* ---- Disco: rainbow faders - ten rounded glass sliders on a gentle smile, each filled from 0 dB in its own CD colour ---- */
#define DX0   40
#define DSTEP 28.4f
#define DTOP  72
#define DBOT  206
#define DMID  ((DTOP + DBOT) / 2)
#define DKPX  ((DBOT - DTOP) / 2.0f / 6.0f * 0.88f)          /* px per dB (a little headroom at the ends) */
static lv_obj_t *g_dtrk[NB], *g_dfill[NB], *g_dknob[NB];
static int dxp(int i){ return (int)lroundf(DX0 + i * DSTEP); }
static int doff(int i){ float u = (i - (NB - 1) / 2.0f) / ((NB - 1) / 2.0f); return (int)lroundf(16 * u * u); }   /* the smile */
static void disco_faders_create(lv_obj_t *root){
    for(int i = 0; i < NB; i++){
        int x = dxp(i), o = doff(i);
        g_dtrk[i] = lv_obj_create(root); lv_obj_remove_style_all(g_dtrk[i]);
        lv_obj_set_size(g_dtrk[i], 22, DBOT - DTOP); lv_obj_set_pos(g_dtrk[i], x - 11, DTOP + o);
        lv_obj_set_style_radius(g_dtrk[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(g_dtrk[i], TC(SURFACE), 0); lv_obj_set_style_bg_opa(g_dtrk[i], 160, 0);
        lv_obj_set_style_border_color(g_dtrk[i], TC(TEXT_PRIMARY), 0); lv_obj_set_style_border_width(g_dtrk[i], 1, 0); lv_obj_set_style_border_opa(g_dtrk[i], 40, 0);
        lv_obj_t *z = lv_obj_create(g_dtrk[i]); lv_obj_remove_style_all(z);          /* the 0 dB tick */
        lv_obj_set_size(z, 14, 1); lv_obj_align(z, LV_ALIGN_LEFT_MID, 4, 0);
        lv_obj_set_style_bg_color(z, TC(TEXT_PRIMARY), 0); lv_obj_set_style_bg_opa(z, 90, 0);
        g_dfill[i] = lv_obj_create(root); lv_obj_remove_style_all(g_dfill[i]);
        lv_obj_set_style_radius(g_dfill[i], 6, 0); lv_obj_set_style_bg_opa(g_dfill[i], LV_OPA_COVER, 0);   /* solid: no blending on every drag frame */
        g_dknob[i] = lv_obj_create(root); lv_obj_remove_style_all(g_dknob[i]);
        lv_obj_set_style_radius(g_dknob[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(g_dknob[i], TC(FIXED_MEDIA_WHITE), 0); lv_obj_set_style_bg_opa(g_dknob[i], LV_OPA_COVER, 0);
        lv_obj_t *ob[3] = { g_dtrk[i], g_dfill[i], g_dknob[i] };
        for(int k = 0; k < 3; k++) lv_obj_clear_flag(ob[k], LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_text_font(g_rim[i], TF(UI_12), 0);
        lv_obj_align(g_rim[i], LV_ALIGN_TOP_MID, x - 180, DBOT + o + 6);
    }
}
static void disco_faders_paint(void){
    static const uint32_t IRI[NB] = { 0xFF5AA0, 0xFF8A78, 0xFFB45A, 0xFFE070, 0x9AF07A, 0x62F2B0, 0x4AD8FF, 0x5AA8FF, 0x7A8CFF, 0xB36BFF };
    for(int i = 0; i < NB; i++){
        int x = dxp(i), o = doff(i), mid = DMID + o, t = clampi(g_t[i], -GMAX, GMAX), s = (i == g_sel && g_editable);
        int y = (int)lroundf(mid - t / 10.0f * DKPX);
        lv_color_t c = !g_editable ? TC(TEXT_SECONDARY) : cfg_get_int("disco_eq", 0) ? ui_current_accent() : theme_color_from_rgb(IRI[i]);   /* Display > Equalizer: Disco / Accent */
        int a = y < mid ? y : mid, b = y < mid ? mid : y; if(b - a < 2){ a = mid - 1; b = mid + 1; }
        lv_obj_set_pos(g_dfill[i], x - 7, a); lv_obj_set_size(g_dfill[i], 14, b - a);
        lv_obj_set_style_bg_color(g_dfill[i], c, 0);
        int k = s ? 20 : 14;
        lv_obj_set_size(g_dknob[i], k, k); lv_obj_set_pos(g_dknob[i], x - k / 2, y - k / 2);
        lv_obj_set_style_border_color(g_dknob[i], c, 0); lv_obj_set_style_border_width(g_dknob[i], s ? 4 : 3, 0);
        lv_obj_set_style_border_opa(g_dtrk[i], s ? 200 : 40, 0); lv_obj_set_style_border_width(g_dtrk[i], s ? 2 : 1, 0);
        lv_obj_set_style_border_color(g_dtrk[i], s ? ui_current_accent() : TC(TEXT_PRIMARY), 0);
    }
}
static void geometry(void){
    if(g_px) return;
    int cnt[NB] = {0}, total = 0;
    /* two passes: count per band, then fill in band order */
    for(int pass = 0; pass < 2; pass++){
        int pos[NB];
        if(pass == 1){
            g_px = malloc(sizeof(px_t) * (size_t)(total ? total : 1)); if(!g_px) return;
            g_bstart[0] = 0; for(int b = 0; b < NB; b++){ g_bstart[b + 1] = g_bstart[b] + cnt[b]; pos[b] = g_bstart[b];
                g_bbox[b].x1 = g_bbox[b].y1 = 400; g_bbox[b].x2 = g_bbox[b].y2 = -1; }
        }
        for(int y = CY - RMAX - 2; y <= CY + RMAX + 2; y++){
            float dy = y + 0.5f - CY;
            for(int x = CX - RMAX - 2; x <= CX + RMAX + 2; x++){
                float dx = x + 0.5f - CX, d2 = dx * dx + dy * dy;
                if(d2 < (RMIN - 1.5f) * (RMIN - 1.5f) || d2 > (RMAX + 1.5f) * (RMAX + 1.5f)) continue;
                float d = sqrtf(d2), a = atan2f(dx, -dy) * 57.29578f; if(a < 0) a += 360.0f;
                int b = (int)floorf(a / 36.0f + 0.5f) % NB;
                float delta = a - b * 36.0f; if(delta > 180) delta -= 360; if(delta < -180) delta += 360;
                float cov = clampf((HALF - fabsf(delta)) * 0.017453f * d + 0.5f) * clampf(fminf(d - RMIN, RMAX - d) + 0.5f);
                if(cov <= 0) continue;
                if(pass == 0){ cnt[b]++; total++; continue; }
                px_t *p = &g_px[pos[b]++];
                p->off = (uint32_t)(y * 360 + x); p->r16 = (uint16_t)(d * 16.0f + 0.5f); p->cov = (uint8_t)(cov * 255.0f + 0.5f); p->kind = 0;
                for(int k = -6; k <= 6; k++) if(k && fabsf(d - rr(k * 10)) < 0.7f){ p->kind = (int8_t)(k + 7 - (k > 0)); break; }
                if(fabsf(d - (R0 + 1)) < 1.0f && fmodf(a, 5.0f) < 2.5f) p->kind = 13;
                if(x < g_bbox[b].x1) g_bbox[b].x1 = x; if(x > g_bbox[b].x2) g_bbox[b].x2 = x;
                if(y < g_bbox[b].y1) g_bbox[b].y1 = y; if(y > g_bbox[b].y2) g_bbox[b].y2 = y;
            }
        }
    }
}
static inline uint32_t mix32(uint32_t a, uint32_t b, int t256){       /* 0..256 */
    uint32_t ra = (a >> 16) & 255, ga = (a >> 8) & 255, ba = a & 255, rb = (b >> 16) & 255, gb = (b >> 8) & 255, bb = b & 255;
    return ((ra + (((int)rb - (int)ra) * t256 >> 8)) << 16) | ((ga + (((int)gb - (int)ga) * t256 >> 8)) << 8) | (ba + (((int)bb - (int)ba) * t256 >> 8));
}
static uint32_t c32(lv_color_t c){ return ((uint32_t)c.red << 16) | ((uint32_t)c.green << 8) | c.blue; }
static void paint_band(int b, uint32_t *fb){
    int grey = !g_editable, s = (b == g_sel), disco = th_disco();
    lv_color_t accc = grey ? TC(EQ_READONLY) : ui_current_accent();
    uint32_t acc = c32(accc), empty = theme_ring_light()?c32(TC(SURFACE_RAISED)):0x1A1A1C,
             lineE = theme_ring_light()?c32(TC(CONTROL_TRACK_STRONG)):0x2E2E32,
             dash = theme_ring_light()?c32(TC(TEXT_MUTED)):0x78787C;
    uint32_t soft = mix32(empty, acc, s ? 171 : 110), bright = s ? acc : mix32(empty, acc, 220);
    int lvl16 = (int)(rr(clampi(g_t[b], -GMAX, GMAX)) * 16.0f + 0.5f), r0_16 = R0 * 16;
    for(int i = g_bstart[b]; i < g_bstart[b + 1]; i++){
        const px_t *p = &g_px[i];
        int f = lvl16 - p->r16 + 8; f = f < 0 ? 0 : f > 16 ? 16 : f;             /* fill, 0..16 */
        uint32_t base = mix32(empty, p->r16 <= r0_16 ? soft : bright, f * 16);
        if(p->kind == 13) base = dash;
        else if(p->kind) base = f > 8 ? mix32(base, 0, 61) : lineE;
        if(disco){                                                         /* glass: the empty part lets the cover through */
            uint32_t a = (uint32_t)(150 + (105 * f) / 16); if(p->kind == 13 || (p->kind && f <= 8)) a = 200;
            a = (a * (uint32_t)(p->cov + (p->cov >> 7))) >> 8;
            fb[p->off] = (a << 24) | (base & 0xFFFFFFu);
        } else fb[p->off] = 0xFF000000u | mix32(0, base, p->cov + (p->cov >> 7));
    }
    g_lvl16[b] = lvl16;
}
/* repaint only what changed: bands whose level moved, the old/new selection, everything on a colour change */
static void paint(void){
    if(th_braun()){ braun_faders_paint(); return; }
    if(th_disco()){ disco_faders_paint(); return; }
    if(!g_canvas) return;
    geometry(); if(!g_px) return;
    lv_draw_buf_t *db = lv_canvas_get_draw_buf(g_canvas); if(!db) return;
    uint32_t *fb = (uint32_t *)db->data;                                     /* 360 px rows, XRGB8888 */
    uint32_t acc = c32(ui_current_accent()); int grey = !g_editable, all = (grey != g_lastgrey || acc != g_lastacc);
    for(int b = 0; b < NB; b++){
        int lvl16 = (int)(rr(clampi(g_t[b], -GMAX, GMAX)) * 16.0f + 0.5f);
        if(all || lvl16 != g_lvl16[b] || b == g_sel || b == g_lastsel){
            if(!all && lvl16 == g_lvl16[b] && (b == g_sel) == (b == g_lastsel)) continue;   /* selection unchanged for it */
            paint_band(b, fb);
            lv_obj_invalidate_area(g_canvas, &g_bbox[b]);
        }
    }
    g_lastsel = g_sel; g_lastgrey = grey; g_lastacc = acc;
}
static void refresh_labels(void){
    char b[40];
    for(int i = 0; i < NB; i++){                                    /* only labels that changed are touched */
        fmt_freq(b, sizeof b, g_f[i], 0);
        if(strcmp(lv_label_get_text(g_rim[i]), b)) lv_label_set_text(g_rim[i], b);
        lv_color_t want = i == g_sel ? ui_current_accent() : (th_braun() || th_disco()) ? TC(TEXT_SECONDARY) : TC(TEXT_DISABLED);
        if(!lv_color_eq(lv_obj_get_style_text_color(g_rim[i], 0), want)) lv_obj_set_style_text_color(g_rim[i], want, 0);
    }
    preset_name(g_preset, b, sizeof b);
    if(strcmp(lv_label_get_text(g_pill_lbl), b)) lv_label_set_text(g_pill_lbl, b);
    int show_edit = g_editable && g_sel >= 0;
    lv_obj_t *edit_objs[5] = { g_flbl, g_glbl, g_minus, g_plus, g_modecap };
    for(int k = 0; k < 5; k++){ if(show_edit) lv_obj_remove_flag(edit_objs[k], LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(edit_objs[k], LV_OBJ_FLAG_HIDDEN); }
    if(show_edit){
        fmt_freq(b, sizeof b, g_f[g_sel], 1); if(strcmp(lv_label_get_text(g_flbl), b)) lv_label_set_text(g_flbl, b);
        fmt_gain(b, sizeof b, clampi(g_t[g_sel], -GMAX, GMAX)); if(strcmp(lv_label_get_text(g_glbl), b)) lv_label_set_text(g_glbl, b);   /* old +-12 presets show at the edge */
        int br = th_braun();                                        /* Braun: the big value in black, the other in grey */
        lv_obj_set_style_text_font(g_flbl, g_mode_freq ? (br ? br_font(22, 1) : TH_F_TITLE) : (br ? br_font(14, 0) : TF(UI_14)), 0);
        lv_obj_set_style_text_color(g_flbl, g_mode_freq ? (br ? TC(TEXT_PRIMARY) : ui_current_accent()) : br ? TC(TEXT_SECONDARY) : TC(TEXT_SECONDARY), 0);
        lv_obj_set_style_text_font(g_glbl, g_mode_freq ? (br ? br_font(14, 0) : TF(UI_14)) : (br ? br_font(22, 1) : TH_F_TITLE), 0);
        lv_obj_set_style_text_color(g_glbl, g_mode_freq ? br ? TC(TEXT_SECONDARY) : TC(TEXT_SECONDARY) : (br ? TC(TEXT_PRIMARY) : ui_current_accent()), 0);
        if(br){ lv_obj_align(g_flbl, LV_ALIGN_TOP_MID, 0, g_mode_freq ? 272 : 250); lv_obj_align(g_glbl, LV_ALIGN_TOP_MID, 0, g_mode_freq ? 250 : 272); }
        if(th_disco()){                                  /* bigger text, bigger touch areas (the active value on the lower line) */
            lv_obj_set_style_text_font(g_flbl, g_mode_freq ? TF(UI_32) : TF(UI_22), 0);
            lv_obj_set_style_text_font(g_glbl, g_mode_freq ? TF(UI_22) : TF(UI_32), 0);
            lv_obj_align(g_flbl, LV_ALIGN_TOP_MID, -4, g_mode_freq ? 290 : 252); lv_obj_align(g_glbl, LV_ALIGN_TOP_MID, -4, g_mode_freq ? 252 : 290);
            lv_obj_set_ext_click_area(g_flbl, 22); lv_obj_set_ext_click_area(g_glbl, 22);   /* two well-separated lines, each easy to hit */
        }
        lv_label_set_text(g_modecap, g_mode_freq ? "FREQ" : "GAIN");
    }
    const char *note = NULL;
    if(g_save_busy) note="Saving...";
    else if(!g_editable) note = g_preset == 0 ? "EQ off" : g_readfail ? "Couldn't read this preset" : g_parametric ? "Advanced preset" : g_preset >= USER_MIN ? "Custom editing unavailable" : th_braun() ? "Built-in preset" : "built-in preset";
    else if(g_sel < 0) note = "tap a band";
    if(note){ lv_label_set_text(g_note, note); lv_obj_remove_flag(g_note, LV_OBJ_FLAG_HIDDEN); }
    else lv_obj_add_flag(g_note, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(g_lock, LV_OBJ_FLAG_HIDDEN);   /* no lock glyph in the fonts: the grey + caption say it */
}
static void redraw(void){ paint(); refresh_labels(); }
static void draw_tmr_cb(lv_timer_t *t){
    (void)t;
    if(g_save_busy && atomic_load_explicit(&g_save_done,memory_order_acquire)){
        g_save_busy=0;
        if(g_save.result){
            persist_saved();
            /* Navigation may have selected another preset during the save.
             * Do not switch it back or treat an IPC failure as an unsaved edit. */
            if(cfg_get_int("eq_preset",0)!=g_save.preset)ui_toast("EQ saved");
            else if(!fw_custom_peq_curve_writable(g_save.hz,NB) || ui_eq_select(g_save.preset)<0)ui_toast("EQ saved; not applied");
        }else ui_toast("EQ not saved");
        load(g_preset);if(!g_editable)g_sel=-1;g_dirty_draw=1;
    }
    if(screen_current()==SCR_EQ_EDITOR && fw_os_ver()==257){
        eq_rate_request();
        int editable=!g_save_busy && g_preset>=USER_MIN && !g_readfail && !g_parametric && fw_custom_peq_curve_writable(g_f,NB);
        if(editable!=g_editable){g_editable=editable;if(!editable){g_sel=-1;g_drag_ok=0;}g_dirty_draw=1;}
    }
    if(g_dirty_draw){g_dirty_draw=0;redraw();}
}   /* drags repaint at most ~20x/s; verification stays on workers */

/* ------------------------------------------------------------------ edits */
static void commit(void){
    if(!g_changed) return;
    g_changed = 0;
    if(apply_now()) refresh_labels();
    else { load(g_preset); if(!g_editable)g_sel=-1; redraw(); ui_toast("EQ not saved"); }
}
static void step(int dir){
    if(!g_editable || g_sel < 0) return;
    if(g_mode_freq){
        float f = g_f[g_sel] * powf(2.0f, dir / 12.0f);
        int nf = clampi((int)(f + 0.5f), band_lo(g_sel), band_hi(g_sel));
        if(nf == g_f[g_sel]) nf = clampi(g_f[g_sel] + dir, band_lo(g_sel), band_hi(g_sel));
        g_f[g_sel] = nf;
    } else g_t[g_sel] = clampi(clampi(g_t[g_sel], -GMAX, GMAX) + dir, -GMAX, GMAX);   /* from the shown (clamped) value */
    g_changed = 1; redraw();
}
static void pm_cb(lv_event_t *e){
    int dir = (int)(intptr_t)lv_event_get_user_data(e);
    lv_event_code_t c = lv_event_get_code(e);
    if(c == LV_EVENT_SHORT_CLICKED || c == LV_EVENT_LONG_PRESSED || c == LV_EVENT_LONG_PRESSED_REPEAT) step(dir);
    else if(c == LV_EVENT_RELEASED || c == LV_EVENT_PRESS_LOST) commit();
}

/* the dial: select a band; drag out / in for its gain */
static int hit_band(int x, int y, float *dist){
    if(th_disco()){
        if(x < DX0 - 15 || x > dxp(NB - 1) + 15) return -1;
        int b = clampi((int)lroundf((x - DX0) / DSTEP), 0, NB - 1), o = doff(b);
        if(y < DTOP + o - 18 || y > DBOT + o + 18) return -1;
        if(dist) *dist = R0 + (DMID + o - y) / DKPX * ((RMAX - R0) / 6.0f);   /* the dial's distance for this height */
        return b;
    }
    if(th_braun()){
        if(y < FTOP - 16 || y > FBOT + 16 || x < FX0 - 14 || x > fx(NB - 1) + 14) return -1;
        int b = (int)lroundf((x - FX0) / FSTEP); b = clampi(b, 0, NB - 1);
        if(dist) *dist = R0 + (FMID - y) / FKPX * ((RMAX - R0) / 6.0f);        /* the dial's distance for this height */
        return b;
    }
    float dx = x - CX, dy = y - CY, d = sqrtf(dx * dx + dy * dy);
    if(d < RMIN - 6 || d > RMAX + 14) return -1;
    float a = atan2f(dx, -dy) * 57.29578f; if(a < 0) a += 360.0f;
    if(dist) *dist = d;
    return (int)floorf(a / 36.0f + 0.5f) % NB;
}
static void hit_cb(lv_event_t *e){
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *in = lv_indev_active(); if(!in) return;
    lv_point_t p; lv_indev_get_point(in, &p);
    float d = 0; int b = hit_band(p.x, p.y, &d);
    if(code == LV_EVENT_PRESSED){
        /* A touch on another band only SELECTS it - it never changes a gain. Only a touch that starts on the
         * band that was already selected can drag its level, so a swipe across the dial (or a back-swipe from
         * the left edge) can't move anything by accident. */
        g_dragging = 0; g_press_d = (int)d; g_press_pt = p;
        g_drag_ok = (b >= 0 && b == g_sel);
        if(b >= 0 && b != g_sel){ g_sel = b; redraw(); }
    } else if(code == LV_EVENT_PRESSING){
        if(!g_editable || g_sel < 0 || b < 0 || !g_drag_ok) return;
        if(!g_dragging && abs((int)d - g_press_d) < 5) return;         /* a tap is not a drag */
        g_dragging = 1;
        int nt = clampi((int)lroundf((d - R0) / ((RMAX - R0) / 6.0f) * 10.0f), -GMAX, GMAX);
        if(nt != g_t[g_sel]){ g_t[g_sel] = nt; g_changed = 1; g_dirty_draw = 1; }
    } else if(code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST){
        if(g_dirty_draw){ g_dirty_draw = 0; redraw(); }
        commit();
        /* double-tap a band (two quick taps, no drag): its gain back to 0 dB */
        static uint32_t last_tap; static int last_band = -1;
        int moved = abs(p.x - g_press_pt.x) + abs(p.y - g_press_pt.y) > 12;   /* a swipe is not a tap */
        if(code == LV_EVENT_RELEASED && !g_dragging && !moved && b >= 0 && b == g_sel){
            if(last_band == b && lv_tick_elaps(last_tap) < 400 && g_editable && g_t[b] != 0){
                g_t[b] = 0; g_changed = 1; commit(); redraw(); last_band = -1;
            } else { last_band = b; last_tap = lv_tick_get(); }
        } else if(g_dragging || moved) last_band = -1;
    }
}
/* main.c asks whether a press belongs to the dial: only a press on the selected (editable) band does - a drag
 * there adjusts it. Anywhere else the normal gestures work, including the back-swipe from the left edge. */
int eqcustom_owns_point(int x, int y){ return g_editable && g_sel >= 0 && hit_band(x, y, NULL) == g_sel; }

/* the preset button: next preset (only selects - never writes) */
static void pad_close(void);
static void pill_cb(lv_event_t *e){
    (void)e;
    int next = (g_preset + 1) % NPRESET;
    if(ui_eq_select(next) < 0){ ui_toast("Couldn't switch EQ"); return; }
    load(next);
    if(!g_editable) g_sel = -1;
    redraw();
}

/* ------------------------------------------------------------------ the number pad */
static lv_obj_t *g_pad, *g_pad_disp, *g_unit_hz, *g_unit_khz, *g_sign;
static char g_pad_txt[12];
static int g_pad_khz, g_pad_neg;
static void pad_close(void){ if(g_pad){ lv_obj_delete(g_pad); g_pad = NULL; } }
static void pad_show(void){
    if(!g_pad) return;
    lv_label_set_text(g_pad_disp, g_pad_txt[0] ? g_pad_txt : "0");
    if(g_mode_freq){
        lv_obj_set_style_bg_color(g_unit_hz,  g_pad_khz ? TC(EQ_CHIP) : ui_current_accent(), 0);
        lv_obj_set_style_bg_color(g_unit_khz, g_pad_khz ? ui_current_accent() : TC(EQ_CHIP), 0);
    } else lv_label_set_text(lv_obj_get_child(g_sign, 0), g_pad_neg ? "-" : "+");
}
static void pad_key_cb(lv_event_t *e){
    const char *k = lv_event_get_user_data(e); size_t n = strlen(g_pad_txt);
    if(!strcmp(k, "del")){ if(n) g_pad_txt[n - 1] = 0; }
    else if(!strcmp(k, ".")){ if(!strchr(g_pad_txt, '.') && n < sizeof g_pad_txt - 2){ if(!n) strcat(g_pad_txt, "0"); strcat(g_pad_txt, "."); } }
    else if(n < 6){ const char *dot = strchr(g_pad_txt, '.'); if(!dot || strlen(dot) < (size_t)(g_mode_freq ? 3 : 2)) strcat(g_pad_txt, k); }
    pad_show();
}
static void pad_unit_cb(lv_event_t *e){ g_pad_khz = (int)(intptr_t)lv_event_get_user_data(e); pad_show(); }
static void pad_sign_cb(lv_event_t *e){ (void)e; g_pad_neg = !g_pad_neg; pad_show(); }
static void pad_cancel_cb(lv_event_t *e){ (void)e; pad_close(); }
static void pad_set_cb(lv_event_t *e){
    (void)e;
    double v = atof(g_pad_txt[0] ? g_pad_txt : "0");
    if(g_sel >= 0 && g_editable && g_pad_txt[0]){
        if(g_mode_freq){ int hz = (int)lround(v * (g_pad_khz ? 1000.0 : 1.0)); g_f[g_sel] = clampi(hz, band_lo(g_sel), band_hi(g_sel)); }
        else { int t = (int)lround(v * 10.0); if(g_pad_neg) t = -t; g_t[g_sel] = clampi(t, -GMAX, GMAX); }
        g_changed = 1; commit(); redraw();
    }
    pad_close();
}
static lv_obj_t *pad_btn(lv_obj_t *p, const char *txt, int x, int y, int w, int h, lv_color_t bg, lv_event_cb_t cb, void *ud){
    lv_obj_t *b = lv_button_create(p);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, w, h);
    lv_obj_set_pos(b, x - w / 2, y - h / 2);
    lv_obj_set_style_radius(b, 12, 0);
    lv_obj_set_style_bg_color(b, bg, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, TC(SURFACE_RAISED), LV_STATE_PRESSED);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);
    lv_obj_t *l = lv_label_create(b); lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, h >= 38 ? TF(UI_18) : TH_F_CAPTION, 0);
    lv_obj_set_style_text_color(l, TC(TEXT_PRIMARY), 0); lv_obj_center(l);
    return b;
}
static void pad_open(void){
    if(!g_editable || g_sel < 0) return;
    pad_close();
    g_pad = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(g_pad);
    lv_obj_set_size(g_pad, 360, 360);
    lv_obj_set_style_bg_color(g_pad, TC(CANVAS), 0);
    lv_obj_set_style_bg_opa(g_pad, LV_OPA_COVER, 0);
    lv_obj_add_flag(g_pad, LV_OBJ_FLAG_CLICKABLE);
    char t[40]; snprintf(t, sizeof t, "BAND %d %s", g_sel + 1, g_mode_freq ? "FREQUENCY" : "GAIN");
    lv_obj_t *h = lv_label_create(g_pad); lv_label_set_text(h, t);
    lv_obj_set_style_text_font(h, TH_F_CAPTION, 0); lv_obj_set_style_text_color(h, TC(TEXT_DISABLED), 0);
    lv_obj_align(h, LV_ALIGN_TOP_MID, 0, 36);
    lv_obj_t *field = lv_obj_create(g_pad);
    lv_obj_remove_style_all(field); lv_obj_set_size(field, 168, 40); lv_obj_set_pos(field, 96, 56);
    lv_obj_set_style_radius(field, 20, 0); lv_obj_set_style_bg_color(field, TC(SURFACE), 0); lv_obj_set_style_bg_opa(field, LV_OPA_COVER, 0);
    lv_obj_clear_flag(field, LV_OBJ_FLAG_SCROLLABLE);
    g_pad_disp = lv_label_create(field);
    lv_obj_set_style_text_font(g_pad_disp, TH_F_TITLE, 0); lv_obj_set_style_text_color(g_pad_disp, TC(TEXT_PRIMARY), 0);
    lv_obj_align(g_pad_disp, LV_ALIGN_LEFT_MID, 16, 0);
    g_pad_txt[0] = 0;
    lv_color_t chip = TC(EQ_CHIP);
    if(g_mode_freq){
        g_pad_khz = g_f[g_sel] >= 1000;
        g_unit_hz  = pad_btn(field, "Hz",  112, 20, 30, 24, chip, pad_unit_cb, (void *)0);
        g_unit_khz = pad_btn(field, "kHz", 145, 20, 34, 24, chip, pad_unit_cb, (void *)1);
    } else {
        g_pad_neg = g_t[g_sel] < 0;
        g_sign = pad_btn(field, "+", 146, 20, 30, 24, chip, pad_sign_cb, NULL);
    }
    static const char *const K[12] = { "1","2","3","4","5","6","7","8","9",".","0","del" };
    for(int i = 0; i < 12; i++)
        pad_btn(g_pad, !strcmp(K[i], "del") ? LV_SYMBOL_BACKSPACE : K[i], 132 + (i % 3) * 48, 124 + (i / 3) * 42, 42, 38,
                TC(SURFACE), pad_key_cb, (void *)K[i]);
    pad_btn(g_pad, "Cancel", 143, 294, 62, 24, TC(SURFACE), pad_cancel_cb, NULL);
    pad_btn(g_pad, "Set", 217, 294, 62, 24, ui_current_accent(), pad_set_cb, NULL);
    char lo[16], hi[16], r[64];
    if(g_mode_freq){ fmt_freq(lo, sizeof lo, band_lo(g_sel), 0); fmt_freq(hi, sizeof hi, band_hi(g_sel), 0);
                     snprintf(r, sizeof r, "%s - %s (between neighbours)", lo, hi); }
    else snprintf(r, sizeof r, "-6.0 to +6.0 dB, 0.1 steps");
    lv_obj_t *rl = lv_label_create(g_pad); lv_label_set_text(rl, r);
    lv_obj_set_style_text_font(rl, TH_F_CAPTION, 0); lv_obj_set_style_text_color(rl, TC(TEXT_DISABLED), 0);
    lv_obj_align(rl, LV_ALIGN_TOP_MID, 0, 316);
    pad_show();
}
/* tapping a value: the inactive one becomes active; the active one opens the pad */
static void flbl_cb(lv_event_t *e){
    lv_event_code_t c = lv_event_get_code(e);
    if(c == LV_EVENT_LONG_PRESSED){                                   /* reset this band's frequency */
        if(g_editable && g_sel >= 0){ g_f[g_sel] = clampi(STDF[g_sel], band_lo(g_sel), band_hi(g_sel)); g_changed = 1; commit(); redraw(); }
        return;
    }
    if(c != LV_EVENT_SHORT_CLICKED) return;
    if(g_mode_freq) pad_open(); else { g_mode_freq = 1; refresh_labels(); }
}
static void glbl_cb(lv_event_t *e){
    if(lv_event_get_code(e) != LV_EVENT_SHORT_CLICKED) return;
    if(!g_mode_freq) pad_open(); else { g_mode_freq = 0; refresh_labels(); }
}

/* ------------------------------------------------------------------ building the screen */
static lv_obj_t *round_btn(lv_obj_t *p, const char *sym, int dx, int dir){
    lv_obj_t *b = lv_button_create(p);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, 30, 30);
    lv_obj_align(b, LV_ALIGN_CENTER, dx, 39);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(b, TC(SURFACE_RAISED), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, TC(CONTROL_TRACK), LV_STATE_PRESSED);
    lv_obj_set_ext_click_area(b, 4);
    lv_obj_add_event_cb(b, pm_cb, LV_EVENT_ALL, (void *)(intptr_t)dir);
    lv_obj_t *l = lv_label_create(b); lv_label_set_text(l, sym);
    lv_obj_set_style_text_font(l, TF(UI_16), 0); lv_obj_set_style_text_color(l, TC(TEXT_PRIMARY), 0); lv_obj_center(l);
    return b;
}
static lv_obj_t *centre_lbl(lv_obj_t *p, int dy, lv_event_cb_t cb){
    lv_obj_t *l = lv_label_create(p);
    lv_obj_align(l, LV_ALIGN_CENTER, 0, dy);
    lv_obj_add_flag(l, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(l, 8);
    lv_obj_add_event_cb(l, cb, LV_EVENT_ALL, NULL);
    return l;
}
void eqcustom_create(lv_obj_t *root){
    g_root = root;
    lv_obj_set_style_bg_color(root, TC(CANVAS), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    g_buf = th_disco() ? NULL : malloc(LV_CANVAS_BUF_SIZE(360, 360, 32, LV_DRAW_BUF_STRIDE_ALIGN));   /* Disco: faders, no dial canvas */
    if(g_buf){
        g_canvas = lv_canvas_create(root);
        if(th_disco()){                                    /* Disco: see-through dial over the blurred cover */
            lv_canvas_set_buffer(g_canvas, g_buf, 360, 360, LV_COLOR_FORMAT_ARGB8888);
            lv_canvas_fill_bg(g_canvas, TC(CANVAS), LV_OPA_TRANSP);
        } else {
            lv_canvas_set_buffer(g_canvas, g_buf, 360, 360, LV_COLOR_FORMAT_XRGB8888);
            lv_canvas_fill_bg(g_canvas, TC(CANVAS), LV_OPA_COVER);
        }
        lv_obj_clear_flag(g_canvas, LV_OBJ_FLAG_CLICKABLE);
    }
    g_hit = lv_obj_create(root);                          /* the dial's touch layer, under the centre controls */
    lv_obj_remove_style_all(g_hit);
    lv_obj_set_size(g_hit, 360, 360);
    lv_obj_add_flag(g_hit, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(g_hit, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(g_hit, hit_cb, LV_EVENT_ALL, NULL);
    for(int i = 0; i < NB; i++){
        g_rim[i] = lv_label_create(root);
        lv_obj_set_style_text_font(g_rim[i], TH_F_CAPTION, 0);
        float a = (-90.0f + i * 36.0f) * 0.017453f;
        lv_obj_align(g_rim[i], LV_ALIGN_CENTER, (int32_t)lroundf(171 * cosf(a)), (int32_t)lroundf(171 * sinf(a)));
    }
    lv_obj_t *pill = lv_button_create(root);
    lv_obj_remove_style_all(pill);
    lv_obj_set_size(pill, 96, 24);
    lv_obj_align(pill, LV_ALIGN_CENTER, 0, -51);
    lv_obj_set_style_radius(pill, 12, 0);
    lv_obj_set_style_bg_color(pill, TC(EQ_CHIP), 0);
    lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(pill, TC(SURFACE_RAISED), LV_STATE_PRESSED);
    lv_obj_set_ext_click_area(pill, 6);
    lv_obj_add_event_cb(pill, pill_cb, LV_EVENT_CLICKED, NULL);
    g_pill_lbl = lv_label_create(pill);
    lv_obj_set_style_text_font(g_pill_lbl, TH_F_CAPTION, 0); lv_obj_set_style_text_color(g_pill_lbl, TC(TEXT_PRIMARY), 0);
    lv_obj_align(g_pill_lbl, LV_ALIGN_CENTER, -6, 0);
    lv_obj_t *chev = lv_label_create(pill); lv_label_set_text(chev, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_font(chev, TF(UI_10), 0); lv_obj_set_style_text_color(chev, TC(TEXT_SECONDARY), 0);
    lv_obj_align(chev, LV_ALIGN_RIGHT_MID, -9, 0);
    g_flbl = centre_lbl(root, -20, flbl_cb);
    g_glbl = centre_lbl(root, 8, glbl_cb);
    g_minus = round_btn(root, LV_SYMBOL_MINUS, -34, -1);
    g_plus  = round_btn(root, LV_SYMBOL_PLUS, 34, +1);
    g_modecap = lv_label_create(root);
    lv_obj_set_style_text_font(g_modecap, TF(UI_10), 0); lv_obj_set_style_text_color(g_modecap, TC(TEXT_DISABLED), 0);
    lv_obj_align(g_modecap, LV_ALIGN_CENTER, 0, 39);
    g_lock = lv_label_create(root); lv_label_set_text(g_lock, LV_SYMBOL_EYE_CLOSE);
    lv_obj_set_style_text_color(g_lock, TC(TEXT_DISABLED), 0); lv_obj_align(g_lock, LV_ALIGN_CENTER, 0, -10);
    g_note = lv_label_create(root);
    lv_obj_set_style_text_font(g_note, TH_F_CAPTION, 0); lv_obj_set_style_text_color(g_note, TC(TEXT_DISABLED), 0);
    lv_obj_align(g_note, LV_ALIGN_CENTER, 0, 4);
    if(th_braun()){
        br_face(root); lv_obj_move_to_index(br_segment(root, 240), 1);
        if(g_canvas) lv_obj_add_flag(g_canvas, LV_OBJ_FLAG_HIDDEN);
        braun_faders_create(root);
        lv_obj_align(pill, LV_ALIGN_TOP_MID, 0, 28);
        lv_obj_set_style_bg_color(pill, TC(SURFACE), 0); lv_obj_set_style_text_color(g_pill_lbl, TC(TEXT_PRIMARY), 0);
        lv_obj_set_style_text_font(g_pill_lbl, br_font(12, 0), 0); lv_obj_set_style_text_color(chev, TC(TEXT_SECONDARY), 0);
        lv_obj_t *pm[2] = { g_minus, g_plus };
        for(int k = 0; k < 2; k++){                                 /* - / + as dark knobs on the segment */
            lv_obj_set_size(pm[k], 42, 42); lv_obj_align(pm[k], LV_ALIGN_TOP_MID, k ? 84 : -84, 262);
            lv_obj_set_style_bg_color(pm[k], TC(CONTROL_KNOB), 0); lv_obj_set_style_bg_opa(pm[k], LV_OPA_COVER, 0);
            lv_obj_set_style_border_color(pm[k], TC(CONTROL_FILL), 0); lv_obj_set_style_border_width(pm[k], 2, 0);
            lv_obj_t *l = lv_obj_get_child(pm[k], 0); if(l) lv_obj_set_style_text_color(l, TC(TEXT_PRIMARY), 0);
        }
        lv_obj_set_style_text_font(g_modecap, br_font(12, 0), 0); lv_obj_set_style_text_color(g_modecap, TC(TEXT_DISABLED), 0);
        lv_obj_align(g_modecap, LV_ALIGN_TOP_MID, 0, 304);
        lv_obj_set_style_text_font(g_note, br_font(14, 0), 0); lv_obj_set_style_text_color(g_note, TC(TEXT_SECONDARY), 0);
        lv_obj_align(g_note, LV_ALIGN_TOP_MID, 0, 272);
    }
    if(th_disco()){                                    /* Disco: the faders; preset on top, the band's value and - / + below */
        disco_faders_create(root);
        lv_obj_align(pill, LV_ALIGN_TOP_MID, -10, 24); lv_obj_set_size(pill, 150, 32);
        lv_obj_set_style_radius(pill, LV_RADIUS_CIRCLE, 0); lv_obj_set_style_bg_color(pill, TC(SURFACE), 0); lv_obj_set_style_bg_opa(pill, 170, 0);
        lv_obj_set_style_border_width(pill, 1, 0); lv_obj_set_style_border_color(pill, TC(TEXT_PRIMARY), 0); lv_obj_set_style_border_opa(pill, 60, 0);
        lv_obj_set_style_text_font(g_pill_lbl, TF(UI_14), 0);
        lv_obj_t *pm[2] = { g_minus, g_plus };
        for(int k = 0; k < 2; k++){ lv_obj_set_size(pm[k], 52, 52); lv_obj_align(pm[k], LV_ALIGN_TOP_MID, k ? 92 : -100, 268); lv_obj_set_ext_click_area(pm[k], 8);
            lv_obj_set_style_bg_color(pm[k], TC(SURFACE), 0); lv_obj_set_style_bg_opa(pm[k], 190, 0);
            lv_obj_set_style_border_width(pm[k], 1, 0); lv_obj_set_style_border_color(pm[k], ui_current_accent(), 0); lv_obj_set_style_border_opa(pm[k], 150, 0);
            kit_keep(pm[k]); lv_obj_add_flag(pm[k], LV_OBJ_FLAG_USER_2); }
        lv_obj_align(g_modecap, LV_ALIGN_TOP_MID, -4, 232);   /* right under the band labels */ lv_obj_set_style_text_color(g_modecap, TC(TEXT_SECONDARY), 0);
        lv_obj_set_style_text_font(g_modecap, TF(UI_12), 0);
        lv_obj_align(g_note, LV_ALIGN_TOP_MID, -4, 290); lv_obj_set_style_text_color(g_note, TC(TEXT_SECONDARY), 0);
        lv_obj_set_style_text_font(g_note, TF(UI_14), 0);
    }
    if(!g_draw_tmr) g_draw_tmr = lv_timer_create(draw_tmr_cb, 50, NULL);
    /* No profile reads/verification for a hidden editor at startup. */
}
/* shown: follow whatever preset is active now (Settings or the stock player may have changed it) */
void eqcustom_refresh(void){ if(fw_os_ver()==257)eq_rate_request(); pad_close(); load(cfg_get_int("eq_preset", 0)); if(!g_editable) g_sel = -1; g_mode_freq = 0; redraw(); }
void eqcustom_style_apply(void){ if(th_disco() && g_dfill[0]) redraw(); }   /* Settings > Display > Equalizer */
/* host renders only: select a band */
void eqcustom_test_select(int b){ if(b >= 0 && b < NB){ g_sel = b; redraw(); } }
