/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "screens.h"
#include "braun.h"
void dsaver_create(lv_obj_t *root); void dsaver_set_clock(const char *t); void dsaver_set_weather(const char *s); void dsaver_set_track(const char *title, const char *artist);
void bsaver_create(lv_obj_t *root); void bsaver_set_clock(const char *t); void bsaver_set_weather(const char *s); void bsaver_set_track(const char *title, const char *artist);
#include "theme.h"
#include "ipc.h"
#include "theme_kit.h"
#include "config.h"
#include <time.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Idle screensaver with selectable STYLES (cfg "saver_style"):
 *   0 Cover   - dim blurred album-art backdrop + digital clock + track (classic)
 *   1 Analog  - round analog clock face (hour/minute hands) + date
 *   2 Minimal - just the big time, centered, black
 *   3 Digital - big clock + date + weather, no art, no track (clean info)
 * Any touch wakes (handled in main.c). Kept cheap: static blit + labels;
 * analog hands recompute only on the ~10s clock tick. */

LV_FONT_DECLARE(font_weather16)
static lv_font_t s_swfont;   /* montserrat_16 + weather-icon fallback */
static lv_font_t s_swfont_big;   /* montserrat_22 + icons: the Ring style's weather line */
static char g_wx_full[160];     /* the full reading; the Ring style shows only icon + temperature */
static void weather_apply(void);

static lv_obj_t *g_bg;
static lv_obj_t *g_clock;
static void saver_clock_style_cb(lv_event_t *e);   /* refits the time when the theme changes its face */
static void saver_clock_fit(void);                  /* lays out the time (+ AM/PM beside it, hidden with the clock) */
static lv_obj_t *g_date;
static lv_obj_t *g_weather;
static lv_obj_t *g_track;
static lv_obj_t *g_artist;

/* analog clock */
static lv_obj_t *g_face;
static lv_obj_t *g_tick[12];
static lv_obj_t *g_hour;
static lv_obj_t *g_min;
static lv_obj_t *g_sec;
static lv_obj_t *g_hub;
static lv_point_precise_t g_hpts[2], g_mpts[2], g_spts[2];

/* vinyl (stock-style: just the spinning square cover - no disc/label/spindle) */
static lv_obj_t *g_vinyl;    /* the spinning square cover image */
/* Ring (style 5): the Home Ring clock, dimmed - clock, date, weather and the playing track inside a faint
 * progress ring with the cover riding it. Everything is grey or darkened; it redraws once a minute. */
static lv_obj_t *g_rring, *g_rdisc, *g_rdisc_img;
static char g_rlast[16];
#define SR_R 148                  /* the bigger ring: close to the rim */
#define SR_DISC 34
static void ring_place(int frac){
    if(!g_rdisc) return;
    float a = (-90.0f + frac * 0.36f) * 0.0174533f;
    lv_obj_set_pos(g_rdisc, 180 + (int)lroundf(SR_R * cosf(a)) - SR_DISC / 2, 180 + (int)lroundf(SR_R * sinf(a)) - SR_DISC / 2);
}
static void ring_progress(void){                /* called when the minute changes */
    if(!g_rring) return;
    track_state_t st; ipc_get_state(&st);
    int f = (st.have_track && st.duration_ms > 0) ? (int)((long long)st.position_ms * 1000 / st.duration_ms) : 0;
    if(f > 1000) f = 1000;
    lv_arc_set_value(g_rring, f); ring_place(f);
}
static int g_vspin = 0;      /* spin state (idempotent) */
static int g_have_track = 0; /* is a track currently loaded? The art savers (Cover backdrop + Vinyl
                                cover) show ONLY when a track is loaded, so an idle player shows
                                nothing (blank/clock) instead of a stale cover. Set by
                                saver_set_track. The spin is separately gated on actually-playing
                                (main.c), so a paused-but-loaded track shows the cover, frozen. */

static int g_style = -1;   /* currently-applied style */

/* Sharp full-bleed vinyl cover: the art worker's cover is only 148px (blurry when
 * upscaled to 360 for a full-bleed spin), so decode the stock player's 364px
 * /usr/data/fiio/cover.jpg into a NATIVE 360px ARGB RAM image ONCE per track. That's
 * both crisp (no upscale) and cheap to rotate (native format, no per-frame scale). */
#define VIN_W 360
static uint8_t *g_vbuf = NULL;       /* 360x360 BGRA (== LVGL ARGB8888 byte order) */
static int g_vbuf_valid = 0;         /* does g_vbuf hold a good decode of the CURRENT track's cover?
                                        A failed/absent decode must NOT display the previous track's
                                        buffer, so visibility + brightness gate on this, not on g_vbuf. */
static lv_image_dsc_t g_vdsc;

/* The spin: diskOS rotates the cover itself into g_vout (a second 360x360 ARGB image the widget shows as-is) instead of
 * asking LVGL to transform the image every frame. LVGL's generic transform cost ~19 ms/frame with anti-aliasing OFF
 * (jagged edges) and ~52 ms with it on; this fixed-point bilinear loop over the round screen only measured ~6 ms/frame
 * on the Disc (scratch/rotbench, 2026-09-27) with smooth edges, so the spin costs a fraction of the CPU (heat/battery)
 * and looks better. Outside the rotated square stays transparent, as before. */
static uint8_t *g_vout = NULL;       /* 360x360 BGRA: the rotated cover the widget shows */
static lv_image_dsc_t g_vodsc;
static int32_t g_vangle = 0;         /* current angle, 0.1 degree units */
static inline uint32_t vin_px(const uint32_t *src, int x, int y){
    return ((unsigned)x < VIN_W && (unsigned)y < VIN_W) ? src[y * VIN_W + x] : 0;   /* outside: transparent */
}
static inline uint32_t vin_lerp(uint32_t a, uint32_t b, uint32_t w){   /* per-channel a..b, w in 0..256 */
    uint32_t rb = (((a & 0x00FF00FFu) * (256 - w) + (b & 0x00FF00FFu) * w) >> 8) & 0x00FF00FFu;
    uint32_t ag = ((((a >> 8) & 0x00FF00FFu) * (256 - w) + ((b >> 8) & 0x00FF00FFu) * w)) & 0xFF00FF00u;
    return rb | ag;
}
__attribute__((optimize("O2")))
static void vinyl_rotate(int32_t tenths)
{
    if (!g_vout || !g_vbuf) return;
    const uint32_t *src = (const uint32_t *)g_vbuf;
    uint32_t *out = (uint32_t *)g_vout;
    double a = (double)tenths * 3.14159265358979 / 1800.0;
    int32_t c = (int32_t)lround(cos(a) * 65536.0), sn = (int32_t)lround(sin(a) * 65536.0);
    const int32_t h = VIN_W / 2;
    for (int y = 0; y < VIN_W; y++) {
        int32_t dy = y - h;
        int32_t hw = (int32_t)sqrt((double)(h * h - dy * dy));   /* only the round screen's span of this row */
        int x0 = h - hw, x1 = h + hw;
        if (x0 < 0) x0 = 0;
        if (x1 > VIN_W) x1 = VIN_W;
        /* inverse map (destination -> source), stepped along the row; +0.5 px so pixel centres line up */
        int32_t u = (x0 - h) * c + dy * sn + (h << 16), v = -(x0 - h) * sn + dy * c + (h << 16);
        uint32_t *o = out + y * VIN_W;
        for (int x = x0; x < x1; x++, u += c, v -= sn) {
            int xi = u >> 16, yi = v >> 16;
            uint32_t fx = (u >> 8) & 0xFF, fy = (v >> 8) & 0xFF;
            uint32_t p00, p10, p01, p11;
            if ((unsigned)xi < VIN_W - 1 && (unsigned)yi < VIN_W - 1) {   /* interior: no bounds checks */
                const uint32_t *q = src + yi * VIN_W + xi;
                p00 = q[0]; p10 = q[1]; p01 = q[VIN_W]; p11 = q[VIN_W + 1];
            } else {                                                      /* the square's edge: soft, transparent out */
                p00 = vin_px(src, xi, yi); p10 = vin_px(src, xi + 1, yi);
                p01 = vin_px(src, xi, yi + 1); p11 = vin_px(src, xi + 1, yi + 1);
            }
            o[x] = vin_lerp(vin_lerp(p00, p10, fx), vin_lerp(p01, p11, fx), fy) | 0xFF000000u;   /* opaque: see below */
        }
    }
    lv_image_cache_drop(&g_vodsc);
    if (g_vinyl) lv_obj_invalidate(g_vinyl);
}

static void vinyl_update_vis(void)
{
    if (!g_vinyl) return;
    if (g_style == 4 && g_vbuf && g_vbuf_valid && g_have_track) lv_obj_remove_flag(g_vinyl, LV_OBJ_FLAG_HIDDEN);
    else                                                        lv_obj_add_flag(g_vinyl, LV_OBJ_FLAG_HIDDEN);
}

/* The vinyl saver is an album-art SHOWCASE, so it should stay at the user's set
 * brightness rather than crushing to the clock-saver dim level (which reads as a
 * washed-out/blurry cover on the panel). It still powers fully off after the
 * screen-off delay, so there's no power regression. */
int saver_wants_bright(void) { if(th_braun() || th_disco()) return 0; return g_style == 4 && g_have_track && g_vbuf_valid; }

/* Sharp vinyl cover, decoded OFF the UI thread.
 *
 * The UI thread only posts a request (the track's path + a generation number) and returns. A worker thread takes
 * the shared decode lock and runs diskos-artdec in "saver" mode on the TRACK ITSELF (embedded art, else its sidecar
 * cover), through art_make_saver: the same SD lease, deadline, revocation and staged-publish rules as all artwork.
 * It reads the result into a NEW buffer and hands it back. A poll timer on the UI thread installs it only if its
 * generation is still the current one, so a track change during the decode can never show the previous cover.
 * A track change hides the old cover at once. If the worker cannot start, the cover simply stays hidden. */
#include <pthread.h>
#include <fcntl.h>
#include "art.h"
void ui_decode_lock(void); void ui_decode_unlock(void);
static char g_vpath[1024];               /* UI thread: the track the vinyl cover is for ("" = none) */
static unsigned g_vgen;                  /* UI thread: bumped on every track change */
static pthread_mutex_t g_vmu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_vcv = PTHREAD_COND_INITIALIZER;
static int g_vwork_started, g_vwant;     /* under g_vmu: worker running; a request is pending */
static char g_vwant_path[1024];          /* under g_vmu */
static unsigned g_vwant_gen;             /* under g_vmu */
static int64_t g_vwant_deadline;         /* under g_vmu: CLOCK_MONOTONIC ms, fixed when the request was posted */
static int g_vfails;                     /* UI thread: failed attempts for the current track (retry limit) */
#define VIN_RETRIES 3
static int64_t vin_now_ms(void){ struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000; }
static uint8_t *g_vres; static unsigned g_vres_gen; static int g_vres_ready;   /* under g_vmu: finished decode */
static unsigned g_vasked_gen = ~0u;      /* UI thread: generation last requested (no duplicate requests) */
static lv_timer_t *g_vpoll;

static void *vinyl_worker(void *arg)
{
    (void)arg;
    const size_t need = (size_t)VIN_W * VIN_W * 4;
    for (;;) {
        pthread_mutex_lock(&g_vmu);
        while (!g_vwant) pthread_cond_wait(&g_vcv, &g_vmu);
        char path[1024]; snprintf(path, sizeof path, "%s", g_vwant_path);
        unsigned gen = g_vwant_gen; int64_t deadline = g_vwant_deadline; g_vwant = 0;
        pthread_mutex_unlock(&g_vmu);

        char out[64]; snprintf(out, sizeof out, "/tmp/.diskos-vinyl-%u.bgra", gen);
        uint8_t *buf = NULL;
        ui_decode_lock();                               /* one decoder at a time, shared with NP + prewarm */
        /* after waiting for the lock: a newer request, or a spent deadline, means this one is not worth decoding */
        pthread_mutex_lock(&g_vmu);
        int superseded = g_vwant && g_vwant_gen != gen;
        pthread_mutex_unlock(&g_vmu);
        int rc = (superseded || vin_now_ms() >= deadline) ? -1 : art_make_saver(path, out, deadline);
        ui_decode_unlock();
        if (rc == 0) {
            int fd = open(out, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
            buf = malloc(need);
            size_t got = 0;
            if (fd >= 0 && buf) {
                ssize_t n;
                while (got < need && (n = read(fd, buf + got, need - got)) > 0) got += (size_t)n;
                char extra; if (got == need && read(fd, &extra, 1) != 0) got = 0;   /* too long is as bad as too short */
            }
            if (fd >= 0) close(fd);
            if (got != need) { free(buf); buf = NULL; }
        }
        unlink(out);
        pthread_mutex_lock(&g_vmu);
        free(g_vres);                                   /* an older, never-collected result */
        g_vres = buf; g_vres_gen = gen; g_vres_ready = 1;
        pthread_mutex_unlock(&g_vmu);
    }
    return NULL;
}

/* UI thread: install a finished decode if it belongs to the current track */
static void vinyl_poll(lv_timer_t *t)
{
    (void)t;
    pthread_mutex_lock(&g_vmu);
    if (!g_vres_ready) { pthread_mutex_unlock(&g_vmu); return; }
    uint8_t *buf = g_vres; unsigned gen = g_vres_gen;
    g_vres = NULL; g_vres_ready = 0;
    pthread_mutex_unlock(&g_vmu);
    if (gen != g_vgen || !buf || !g_vinyl) {            /* stale (track changed) or failed: show nothing */
        free(buf);
        if (gen == g_vgen) {
            g_vbuf_valid = 0; vinyl_update_vis();
            if (++g_vfails < VIN_RETRIES) g_vasked_gen = ~0u;   /* allow a retry the next time the saver shows */
        }
        return;
    }
    uint8_t *old = g_vbuf;
    if (old) lv_image_cache_drop(&g_vdsc);             /* LVGL must not keep the old pixels cached */
    g_vbuf = buf; g_vbuf_valid = 1;
    memset(&g_vdsc, 0, sizeof g_vdsc);
    g_vdsc.header.magic  = LV_IMAGE_HEADER_MAGIC;
    g_vdsc.header.cf     = LV_COLOR_FORMAT_ARGB8888;
    g_vdsc.header.w      = VIN_W;
    g_vdsc.header.h      = VIN_W;
    g_vdsc.header.stride = VIN_W * 4;
    g_vdsc.data          = g_vbuf;
    g_vdsc.data_size     = (uint32_t)VIN_W * VIN_W * 4;
    if (!g_vout) g_vout = calloc((size_t)VIN_W * VIN_W, 4);   /* zeroed: outside the round area stays transparent */
    if (g_vout) {                                            /* show the diskOS-rotated copy (see vinyl_rotate) */
        g_vodsc = g_vdsc;
        g_vodsc.data = g_vout;
        /* OPAQUE: a square cover turned about its centre always covers the round screen, so no visible pixel is ever
         * see-through - and an opaque image is a plain copy for LVGL instead of a per-pixel alpha blend (~4-5 ms of
         * the frame). Outside the circle (invisible on the round panel) stays black. */
        g_vodsc.header.cf = LV_COLOR_FORMAT_XRGB8888;
        vinyl_rotate(g_vangle);
        lv_image_set_src(g_vinyl, &g_vodsc);
        lv_image_set_rotation(g_vinyl, 0);
    } else {                                                 /* no memory for the copy: LVGL rotates, as before */
        lv_image_set_src(g_vinyl, &g_vdsc);
        lv_image_set_pivot(g_vinyl, VIN_W / 2, VIN_W / 2);
    }
    lv_image_set_scale(g_vinyl, 256);                    /* 1x - already 360px, no upscale */
    lv_obj_center(g_vinyl);
    free(old);
    vinyl_update_vis();
}

/* UI thread: ask for the current track's sharp cover (returns at once) */
static void vinyl_load_sharp_cover(void)
{
    if (!g_vinyl || !g_have_track || !g_vpath[0]) { g_vbuf_valid = 0; vinyl_update_vis(); return; }
    if (g_vbuf_valid || g_vasked_gen == g_vgen) return;  /* already showing it, or already asked */
    if (!g_vpoll) g_vpoll = lv_timer_create(vinyl_poll, 150, NULL);
    pthread_mutex_lock(&g_vmu);
    if (!g_vwork_started) {
        pthread_t th;
        if (pthread_create(&th, NULL, vinyl_worker, NULL) == 0) { pthread_detach(th); g_vwork_started = 1; }
    }
    if (g_vwork_started) {
        snprintf(g_vwant_path, sizeof g_vwant_path, "%s", g_vpath);
        g_vwant_gen = g_vgen; g_vwant = 1;
        g_vwant_deadline = vin_now_ms() + 15000;       /* time spent queued behind other decodes counts */
        pthread_cond_signal(&g_vcv);
        g_vasked_gen = g_vgen;
    }
    pthread_mutex_unlock(&g_vmu);
    vinyl_update_vis();                                  /* stays hidden until the new cover arrives */
}

#define CX 180
#define CY 180

static lv_obj_t *mk(lv_obj_t *p, const lv_font_t *font, lv_color_t c, int y)
{
    lv_obj_t *l = lv_label_create(p);
    lv_obj_set_width(l, 320);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, c, 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, y);
    lv_label_set_text(l, "");
    return l;
}

static lv_obj_t *mk_hand(lv_obj_t *p, int w, lv_color_t c, lv_point_precise_t *pts)
{
    lv_obj_t *ln = lv_line_create(p);
    lv_obj_set_style_line_width(ln, w, 0);
    lv_obj_set_style_line_color(ln, c, 0);
    lv_obj_set_style_line_rounded(ln, true, 0);
    pts[0].x = CX; pts[0].y = CY; pts[1].x = CX; pts[1].y = CY - 1;
    lv_line_set_points(ln, pts, 2);
    return ln;
}

static void set_hand(lv_obj_t *ln, lv_point_precise_t *pts, double deg, int len)
{
    double r = deg * 3.14159265 / 180.0;
    pts[0].x = CX; pts[0].y = CY;
    pts[1].x = (lv_value_precise_t)(CX + len * sin(r));
    pts[1].y = (lv_value_precise_t)(CY - len * cos(r));
    lv_line_set_points(ln, pts, 2);
    lv_obj_invalidate(ln);
}

/* 1s tick: animate the analog hands (incl. seconds) only while the analog saver
 * is actually on screen - cheap no-op otherwise (battery). */
static void saver_anim_cb(lv_timer_t *t)
{
    (void)t;
    if (g_style != 1 || screen_current() != SCR_SAVER) return;
    time_t now = time(NULL); struct tm lt; localtime_r(&now, &lt);
    set_hand(g_hour, g_hpts, (lt.tm_hour % 12) * 30.0 + lt.tm_min * 0.5, 70);
    set_hand(g_min,  g_mpts, lt.tm_min * 6.0, 104);
    set_hand(g_sec,  g_spts, lt.tm_sec * 6.0, 118);
}

/* vinyl spin - idempotent; main.c calls it each loop with the live condition
 * (vinyl style && saver visible && backlight on). Freezes on stop. */
/* Throttle the actual rotation to ~20 Hz: on this GPU-less SoC a full-bleed rotate
 * is close to a full-screen software transform, so applying it every LVGL frame
 * (~30 Hz) is wasteful. Skipping a callback causes NO invalidation, so capping to
 * 50 ms keeps the redraw cost down while ~2°/step stays smooth for a slow spin. */
static void vspin_cb(void *var, int32_t v){
    static uint32_t last = 0;
    uint32_t now = lv_tick_get();
    /* 30 fps. With diskOS's own rotation (vinyl_rotate) a frame costs ~7 ms, so the loop would otherwise run ~60 fps
     * and spend the saving on frames nobody needs: at 27 s/rev, 30 fps is under half a degree per frame (smooth), and
     * the cap sits far above the render time, so the cadence stays even. Device: ~58% CPU (old LVGL transform) ->
     * ~44% uncapped -> see the cap's figure in plans (measured with /proc stat ticks over 10 s). */
    if (now - last < 33) return;
    last = now;
    g_vangle = v % 3600;
    if (g_vout && lv_image_get_src((lv_obj_t *)var) == (const void *)&g_vodsc) vinyl_rotate(g_vangle);
    else lv_image_set_rotation((lv_obj_t *)var, g_vangle);
}
void saver_vinyl_spin(int want)
{
    if(th_braun() || th_disco()) return;
    want = want && g_style == 4 && g_vinyl &&
           !lv_obj_has_flag(g_vinyl, LV_OBJ_FLAG_HIDDEN);   /* never spin a hidden/no-cover image */
    if (want == g_vspin) return;
    g_vspin = want;
    if (want) {
        int32_t cur = g_vout ? g_vangle : lv_image_get_rotation(g_vinyl);
        lv_anim_t a; lv_anim_init(&a);
        lv_anim_set_var(&a, g_vinyl);
        lv_anim_set_exec_cb(&a, vspin_cb);
        lv_anim_set_values(&a, cur, cur + 3600);
        lv_anim_set_time(&a, 27000);                /* ~27s/rev - slow, calm screensaver spin */
        lv_anim_set_path_cb(&a, lv_anim_path_linear);   /* constant angular speed */
        lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
        lv_anim_start(&a);
    } else {
        lv_anim_delete(g_vinyl, vspin_cb);
    }
}

/* show/hide + position elements for a style */
static void relayout(int style)
{
    int analog = (style == 1);
    int cover  = (style == 0);
    int minim  = (style == 2);
    int vinyl  = (style == 4);
    int ring   = (style == 5);
    /* Ring Light standby uses the light surface; artwork styles keep their scrim. */
    if (g_clock) lv_obj_set_style_bg_color(lv_obj_get_parent(g_clock),
                                          ring && theme_ring_light() ? TC(SURFACE) : TC(SCRIM), 0);
    lv_obj_t *rparts[] = { g_rring, g_rdisc };
    for (unsigned i = 0; i < 2; i++)
        if (rparts[i]) { if (ring && g_have_track) lv_obj_remove_flag(rparts[i], LV_OBJ_FLAG_HIDDEN);
                         else lv_obj_add_flag(rparts[i], LV_OBJ_FLAG_HIDDEN); }
    lv_color_t dimw = ring ? TC(SAVER_TITLE) : TC(FIXED_MEDIA_WHITE);
    if (g_clock) lv_obj_set_style_text_color(g_clock, dimw, 0);
    { lv_color_t dimg = ring ? TC(SAVER_SUBTITLE) : TC(TEXT_SECONDARY);   /* the Ring saver: greys only */
      if (g_date) lv_obj_set_style_text_color(g_date, dimg, 0);
      if (g_weather) lv_obj_set_style_text_color(g_weather, dimg, 0); }

    /* backdrop only in Cover AND only when a track is loaded (never reveal a stale cover on an
     * idle player when the style changes or the saver is entered manually) */
    if (g_bg) { if (cover && g_have_track) lv_obj_remove_flag(g_bg, LV_OBJ_FLAG_HIDDEN);
                else                        lv_obj_add_flag(g_bg, LV_OBJ_FLAG_HIDDEN); }

    /* vinyl: just the spinning square cover (shown only in vinyl style + when a cover
     * has been decoded; saver_show_sync/saver_set_track drive the decode + visibility) */
    (void)vinyl;
    vinyl_update_vis();

    /* analog parts */
    lv_obj_t *aparts[] = { g_face, g_hour, g_min, g_sec, g_hub };
    for (unsigned i = 0; i < sizeof(aparts)/sizeof(aparts[0]); i++)
        if (aparts[i]) { if (analog) lv_obj_remove_flag(aparts[i], LV_OBJ_FLAG_HIDDEN);
                         else        lv_obj_add_flag(aparts[i], LV_OBJ_FLAG_HIDDEN); }
    for (int i = 0; i < 12; i++)
        if (g_tick[i]) { if (analog) lv_obj_remove_flag(g_tick[i], LV_OBJ_FLAG_HIDDEN);
                         else         lv_obj_add_flag(g_tick[i], LV_OBJ_FLAG_HIDDEN); }

    /* digital clock: shown in every style except analog + vinyl */
    if (g_clock) {
        if (analog || vinyl) lv_obj_add_flag(g_clock, LV_OBJ_FLAG_HIDDEN);
        else { lv_obj_remove_flag(g_clock, LV_OBJ_FLAG_HIDDEN);
               lv_obj_set_style_text_font(g_clock, ring ? TF(UI_48) : TF(UI_40), 0);
               /* Ring: 48 px is the largest built-in size, so scale it up ~1.3x (redrawn once a minute: cheap) */
               lv_obj_set_style_transform_pivot_x(g_clock, lv_pct(50), 0); lv_obj_set_style_transform_pivot_y(g_clock, lv_pct(50), 0);
               lv_obj_set_style_transform_scale(g_clock, ring ? 333 : 256, 0);
               lv_obj_align(g_clock, LV_ALIGN_TOP_MID, 0, ring ? 134 : (minim ? 150 : 110)); }
    }
    /* date: every style except vinyl, position varies */
    if (g_date) {
        if (vinyl || ring) lv_obj_add_flag(g_date, LV_OBJ_FLAG_HIDDEN);   /* Ring: no date */
        else { lv_obj_remove_flag(g_date, LV_OBJ_FLAG_HIDDEN);
               lv_obj_align(g_date, LV_ALIGN_TOP_MID, 0, ring ? 184 : (analog ? 250 : (minim ? 206 : 170))); }
    }
    /* weather: Cover + Digital */
    if (g_weather) { if (cover || style == 3 || ring) lv_obj_remove_flag(g_weather, LV_OBJ_FLAG_HIDDEN);
                     else lv_obj_add_flag(g_weather, LV_OBJ_FLAG_HIDDEN);
                     lv_obj_set_style_text_font(g_weather, ring ? &s_swfont_big : &s_swfont, 0);
                     if (ring) lv_obj_align(g_weather, LV_ALIGN_TOP_MID, 0, 80);    /* Ring: weather on top */
                     else lv_obj_align(g_weather, LV_ALIGN_TOP_MID, 0, 200);
                     weather_apply(); }
    /* track/artist: Cover only */
    if (g_track)  { if (cover || ring) lv_obj_remove_flag(g_track, LV_OBJ_FLAG_HIDDEN);  else lv_obj_add_flag(g_track, LV_OBJ_FLAG_HIDDEN);
                    lv_obj_set_style_text_font(g_track, ring ? ui_font_cjk(28) : TF(UI_16), 0);
                    lv_obj_set_width(g_track, ring ? 240 : 320);   /* inside the ring */
                    lv_label_set_long_mode(g_track, ring ? LV_LABEL_LONG_SCROLL_CIRCULAR : LV_LABEL_LONG_DOT);
                    if (ring) lv_obj_align(g_track, LV_ALIGN_TOP_MID, 0, 208); else lv_obj_align(g_track, LV_ALIGN_TOP_MID, 0, 254); }
    if (ring) { g_rlast[0] = 0; ring_progress(); }
    if (g_artist) { if (cover || ring) lv_obj_remove_flag(g_artist, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(g_artist, LV_OBJ_FLAG_HIDDEN);
                    lv_obj_set_style_text_font(g_artist, ring ? ui_font_cjk(24) : TF(UI_14), 0);
                    lv_obj_set_width(g_artist, ring ? 230 : 320);
                    lv_label_set_long_mode(g_artist, ring ? LV_LABEL_LONG_SCROLL_CIRCULAR : LV_LABEL_LONG_DOT);
                    lv_obj_set_style_text_color(g_artist, ring ? TC(SAVER_ARTIST) : TC(TEXT_MUTED), 0);
                    if (ring) lv_obj_align(g_artist, LV_ALIGN_TOP_MID, 0, 244); else lv_obj_align(g_artist, LV_ALIGN_TOP_MID, 0, 278); }
}

void saver_create(lv_obj_t *root)
{
    if(th_disco()){ dsaver_create(root); return; }            /* Disco: the Music screen at rest */
    if(th_braun()){ bsaver_create(root); return; }            /* Braun: its own standby, whatever the saver style */
    lv_obj_set_style_bg_color(root, TC(SCRIM), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);

    g_bg = lv_image_create(root);
    lv_obj_set_size(g_bg, 360, 360);
    lv_obj_align(g_bg, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_image_recolor(g_bg, TC(IMAGE_TINT), 0);
    lv_obj_set_style_image_recolor_opa(g_bg, 185, 0);
    lv_obj_add_flag(g_bg, LV_OBJ_FLAG_HIDDEN);

    /* analog face: 12 hour ticks around a circle + hands + hub */
    for (int i = 0; i < 12; i++) {
        lv_obj_t *d = lv_obj_create(root);
        lv_obj_remove_style_all(d);
        int big = (i % 3 == 0);
        lv_obj_set_size(d, big ? 8 : 5, big ? 8 : 5);
        lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(d, (big ? TC(TEXT_PRIMARY) : TC(TEXT_MUTED)), 0);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
        double r = i * 30 * 3.14159265 / 180.0;
        int x = (int)(CX + 150 * sin(r)), y = (int)(CY - 150 * cos(r));
        lv_obj_set_pos(d, x - (big?4:2), y - (big?4:2));
        g_tick[i] = d;
    }
    g_hour = mk_hand(root, 6, TC(TEXT_PRIMARY), g_hpts);
    g_min  = mk_hand(root, 4, TC(TEXT_SECONDARY), g_mpts);
    g_sec  = mk_hand(root, 2, ui_current_accent(), g_spts);
    g_hub  = lv_obj_create(root);
    lv_obj_remove_style_all(g_hub);
    lv_obj_set_size(g_hub, 14, 14);
    lv_obj_set_style_radius(g_hub, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(g_hub, ui_current_accent(), 0);
    lv_obj_set_style_bg_opa(g_hub, LV_OPA_COVER, 0);
    lv_obj_set_pos(g_hub, CX - 7, CY - 7);

    /* vinyl: STOCK-STYLE - the plain SQUARE album cover free-rotates; the round
     * screen cuts its corners (black shows at the corners mid-tilt). No record
     * graphic (no disc/label/spindle) - copies stock, not our earlier invention.
     * g_vdisc/g_vlabel/g_vhole are intentionally left unused (NULL). */
    g_vinyl = lv_image_create(root);
    lv_obj_center(g_vinyl);
    /* AA OFF for FASTER frames: measured render is ~19ms with AA off (~50fps) vs ~52ms
     * with AA on (~19fps). On the native-res + bright cover the nearest-neighbour edges
     * are acceptable, and the ~50fps makes the spin much smoother. */
    lv_image_set_antialias(g_vinyl, false);   /* pivot + scale set per-cover in saver_set_track */
    lv_obj_add_flag(g_vinyl, LV_OBJ_FLAG_HIDDEN);

    s_swfont = *TF(UI_16);
    s_swfont.fallback = &font_weather16;
    s_swfont_big = *theme_font_original(28);
    s_swfont_big.fallback = &font_weather16;

    g_clock   = mk(root, TF(SAVER_CLOCK), TC(TEXT_PRIMARY), 110);
    g_date    = mk(root, TF(UI_16), TC(TEXT_SECONDARY), 170);
    g_weather = mk(root, &s_swfont,              TC(TEXT_SECONDARY), 200);
    g_track   = mk(root, TF(UI_16), TC(TEXT_PRIMARY), 254);
    g_artist  = mk(root, TF(UI_14), TC(TEXT_MUTED), 278);
    lv_label_set_text(g_clock, "--:--");
    lv_obj_add_event_cb(g_clock, saver_clock_style_cb, LV_EVENT_STYLE_CHANGED, NULL);   /* refit after the theme's face lands */

    relayout(cfg_get_int("saver_style", 0));
    g_style = cfg_get_int("saver_style", 0);
    lv_timer_create(saver_anim_cb, 1000, NULL);   /* analog seconds hand */
    g_rring = lv_arc_create(root);
    lv_obj_remove_style(g_rring, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(g_rring, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(g_rring, 2 * SR_R, 2 * SR_R);
    lv_obj_center(g_rring);
    lv_arc_set_rotation(g_rring, 270);
    lv_arc_set_bg_angles(g_rring, 0, 360);
    lv_arc_set_range(g_rring, 0, 1000);
    lv_obj_set_style_arc_width(g_rring, 3, LV_PART_MAIN);
    lv_obj_set_style_arc_color(g_rring, TC(SAVER_RING), LV_PART_MAIN);
    lv_obj_set_style_arc_width(g_rring, 3, LV_PART_INDICATOR);
    lv_obj_add_flag(g_rring, LV_OBJ_FLAG_HIDDEN);
    g_rdisc = lv_obj_create(root);
    lv_obj_remove_style_all(g_rdisc);
    lv_obj_set_size(g_rdisc, SR_DISC, SR_DISC);
    lv_obj_set_style_radius(g_rdisc, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_clip_corner(g_rdisc, true, 0);
    lv_obj_set_style_bg_color(g_rdisc, TC(SAVER_DISC), 0);
    lv_obj_set_style_bg_opa(g_rdisc, LV_OPA_COVER, 0);
    lv_obj_set_style_opa(g_rdisc, 170, 0);                 /* dimmed like the rest */
    lv_obj_clear_flag(g_rdisc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(g_rdisc, LV_OBJ_FLAG_HIDDEN);
    g_rdisc_img = lv_image_create(g_rdisc);
    lv_obj_clear_flag(g_rdisc_img, LV_OBJ_FLAG_CLICKABLE);

}

/* repaint the saver's accent-bearing decorations (analog second hand + hub, vinyl
 * record label) with the live accent - called from ui.c apply_accent so they match the
 * rest of the UI instead of staying hardcoded pink. Elements are created once, so the
 * pointers are always valid. */
void saver_set_accent(lv_color_t c)
{
    if(th_braun() || th_disco()) return;
    if (g_rring) lv_obj_set_style_arc_color(g_rring, theme_color_mix(ui_media_accent(), TC(SCRIM), 120), LV_PART_INDICATOR);
    if (g_track && g_style == 5) lv_obj_set_style_text_color(g_track, theme_color_mix(ui_media_accent(), TC(SCRIM), 150), 0);
    if (g_sec)    lv_obj_set_style_line_color(g_sec, c, 0);
    if (g_hub)    lv_obj_set_style_bg_color(g_hub, c, 0);
}

/* apply the saved style + current accent immediately when the saver is shown, rather
 * than waiting for the next clock tick (which is where style changes were applied). */
void saver_show_sync(void)
{
    if(th_braun() || th_disco()) return;
    int s = cfg_get_int("saver_style", 0);
    if (s != g_style) { relayout(s); g_style = s; }
    saver_set_accent(ui_current_accent());
    /* decode the sharp cover as the vinyl saver appears - but only if a track is loaded;
     * otherwise make sure the vinyl stays hidden (no stale cover on an idle player). */
    if (g_style == 4) { if (g_have_track) vinyl_load_sharp_cover(); else vinyl_update_vis(); }
}

/* The time as set (a dotted label edits its own copy, so the fit always starts from this). */
static char g_clock_text[32] = "--:--";
/* Fit the whole time in the saver: a big theme face can make "12:59 PM" wider than the 320 px box, and the label would
 * end in "..." (bauhaus showed "5:53..."). Then the label takes the text's natural width (one line; its alignment keeps
 * it centred) and is drawn scaled down about its centre. Runs on every time change AND whenever the label's style
 * changes - the theme's styling pass sets the final (bigger) face after the first time is set. */
static lv_obj_t *g_mer;   /* AM/PM in a text face, for a theme whose clock face has no letters (saver_mer_apart) */
static void saver_clock_fit(void)
{
    if (!g_clock) return;
    char digits[32]; snprintf(digits, sizeof digits, "%s", g_clock_text);
    const char *mer = "";
    char *sp = strrchr(digits, ' ');
    if (theme_kit()->saver_mer_apart && sp && (!strcmp(sp + 1, "AM") || !strcmp(sp + 1, "PM"))) { *sp = 0; mer = g_clock_text + (sp - digits) + 1; }
    lv_label_set_text(g_clock, digits);
    lv_point_t sz;
    lv_text_get_size(&sz, digits, lv_obj_get_style_text_font(g_clock, 0),
                     lv_obj_get_style_text_letter_space(g_clock, 0), 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    int32_t room = 320 - 4;
    int32_t sc = (sz.x > room && sz.x > 0) ? (int32_t)(256L * room / sz.x) : 256;
    lv_obj_set_width(g_clock, sc < 256 ? sz.x + 4 : 320);
    lv_obj_update_layout(g_clock);
    lv_obj_set_style_transform_pivot_x(g_clock, lv_obj_get_width(g_clock) / 2, 0);
    lv_obj_set_style_transform_pivot_y(g_clock, lv_obj_get_height(g_clock) / 2, 0);
    lv_obj_set_style_transform_scale(g_clock, sc, 0);
    /* AM/PM beside the digits' foot, the pair centred as one group */
    if (mer[0] && !g_mer) {
        g_mer = lv_label_create(lv_obj_get_parent(g_clock));
        kit_keep(g_mer);
        lv_obj_set_style_text_font(g_mer, theme_kit()->saver_mer_apart == 2 ? TF(DATE) : TF(UI_14), 0);
    }
    if (g_mer) {
        int show = mer[0] && !lv_obj_has_flag(g_clock, LV_OBJ_FLAG_HIDDEN);
        if (!show) { lv_obj_add_flag(g_mer, LV_OBJ_FLAG_HIDDEN); lv_obj_set_style_translate_x(g_clock, 0, 0); }
        else {
            lv_label_set_text(g_mer, mer);
            lv_obj_set_style_text_color(g_mer, lv_obj_get_style_text_color(g_clock, 0), 0);
            lv_obj_remove_flag(g_mer, LV_OBJ_FLAG_HIDDEN);
            lv_obj_update_layout(g_mer);
            int32_t mw = lv_obj_get_width(g_mer) + 6;
            lv_obj_set_width(g_clock, sz.x + 4);                  /* the digits' own width, so AM/PM sits right beside them */
            lv_obj_set_style_translate_x(g_clock, -mw / 2, 0);   /* centre digits + AM/PM together */
            lv_obj_update_layout(g_clock);
            lv_obj_align_to(g_mer, g_clock, LV_ALIGN_OUT_RIGHT_BOTTOM, 6, -lv_obj_get_height(g_clock) / 5);   /* coords include the translate */
        }
    }
}
static void saver_clock_style_cb(lv_event_t *e)
{
    (void)e;
    static int busy;                     /* the fit sets styles itself: don't recurse */
    if (busy) return;
    busy = 1; saver_clock_fit(); busy = 0;
}

void saver_set_clock(const char *t, const char *date)
{
    if(th_disco()){ dsaver_set_clock(t); return; }
    if(th_braun()){ bsaver_set_clock(t); return; }
    /* re-apply layout if the style setting changed */
    int s = cfg_get_int("saver_style", 0);
    if (s != g_style) { relayout(s); g_style = s; }

    if (g_clock) lv_label_set_text(g_clock, t ? t : "--:--");
    if (g_style == 5 && t && strcmp(t, g_rlast)){ snprintf(g_rlast, sizeof g_rlast, "%s", t); ring_progress(); }   /* once a minute */
    if (g_date)  lv_label_set_text(g_date, date ? date : "");

    /* analog hands from the live time */
    if (g_style == 1) {
        time_t now = time(NULL); struct tm lt; localtime_r(&now, &lt);
        set_hand(g_hour, g_hpts, (lt.tm_hour % 12) * 30.0 + lt.tm_min * 0.5, 70);
        set_hand(g_min,  g_mpts, lt.tm_min * 6.0, 104);
    }
}

/* "<icon>  14°C  Partly cloudy" -> "<icon>  14°C": the icon and temperature only (the condition dropped) */
static void wx_short(const char *in, char *out, size_t cap){
    if(!in){ out[0] = 0; return; }
    const char *a = strstr(in, "  "); const char *b = a ? strstr(a + 2, "  ") : NULL;
    size_t n = b ? (size_t)(b - in) : strlen(in); if(n >= cap) n = cap - 1;
    memcpy(out, in, n); out[n] = 0;
}
static void weather_apply(void){
    if(!g_weather) return;
    if(g_style == 5){ char s[96]; wx_short(g_wx_full, s, sizeof s); lv_label_set_text(g_weather, s); }
    else lv_label_set_text(g_weather, g_wx_full);
}
void saver_set_weather(const char *text)
{
    if(th_disco()){ dsaver_set_weather(text); return; }
    if(th_braun()){ bsaver_set_weather(text); return; }
    snprintf(g_wx_full, sizeof g_wx_full, "%s", text ? text : "");
    weather_apply();
}

void saver_set_track(const char *title, const char *artist, const void *backdrop_src, const char *path)
{
    if(th_disco()){ dsaver_set_track(title, artist); return; }
    if(th_braun()){ bsaver_set_track(title, artist); return; }
    /* The caller passes a non-NULL title iff st.have_track (NULL when no track), so key off
     * NULL-ness, not emptiness - a valid but untitled file still counts as a loaded track. */
    g_have_track = (title != NULL);
    if (g_rdisc_img){ const void *dsc = ui_current_cover_dsc(); lv_image_set_src(g_rdisc_img, NULL);
        if (dsc){ const lv_image_dsc_t *d = dsc; lv_image_set_src(g_rdisc_img, dsc);
                  if (d->header.w > 0) lv_image_set_scale(g_rdisc_img, (uint32_t)(SR_DISC * 256 / d->header.w) + 2);
                  lv_obj_center(g_rdisc_img); } }
    if (g_style == 5) relayout(5);
    if (g_track)  lv_label_set_text(g_track, title ? title : "");
    if (g_artist) lv_label_set_text(g_artist, artist ? artist : "");

    /* vinyl cover: re-decode the sharp 360px cover only when the vinyl saver is
     * actually on-screen (track auto-advanced while idle); otherwise just refresh
     * visibility - the decode runs on the next saver show. Never run the decoder for a
     * track change while NOT showing the vinyl saver (would hitch the live UI). */
    if (g_vinyl) {
        if (g_style == 4 && screen_current() == SCR_SAVER) vinyl_load_sharp_cover();
        else                                               vinyl_update_vis();
    }

    if (g_bg) {
        if (g_have_track && backdrop_src) {
            lv_image_set_src(g_bg, backdrop_src);
            if (g_style == 0) lv_obj_remove_flag(g_bg, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_background(g_bg);
        } else {
            lv_obj_add_flag(g_bg, LV_OBJ_FLAG_HIDDEN);   /* no track -> no backdrop art */
        }
    }
}
