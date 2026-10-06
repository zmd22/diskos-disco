/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "screens.h"
#include "theme.h"
#include "theme_kit.h"
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include "theme.h"
#include "braun.h"
#include <string.h>
#include <stdint.h>
#include <pthread.h>
#include <errno.h>

LV_FONT_DECLARE(font_weather16)
static lv_font_t s_appwfont;   /* montserrat_22 + weather-icon fallback */

/* Weather via wttr.in (auto-geolocates by IP). Fetched on a detached thread so
 * the blocking wget never stalls the UI; the result is picked up on the main
 * thread by weather_poll() (LVGL is not thread-safe). */

/* FontAwesome 4 weather glyphs (UTF-8), rendered via font_weather16 fallback. */
#define WI_SUN   "\xEF\x86\x85"   /* f185 sun-o      */
#define WI_MOON  "\xEF\x86\x86"   /* f186 moon-o     */
#define WI_CLOUD "\xEF\x83\x82"   /* f0c2 cloud      */
#define WI_RAIN  "\xEF\x83\xA9"   /* f0e9 umbrella   */
#define WI_BOLT  "\xEF\x83\xA7"   /* f0e7 bolt       */
#define WI_SNOW  "\xEF\x8B\x9C"   /* f2dc snowflake-o*/

static char g_wbuf[160];
static char g_loc[160];               /* percent-encoded location (set on main thread).
                                       * config VLEN caps weather_loc at ~47 bytes raw, so
                                       * worst-case %-encoding (47*3=141) fits with margin. */
/* worker->main handoff is guarded by a mutex (volatile gives no cross-thread memory
 * ordering; on the dual-core X2000 the main thread could otherwise observe g_wready
 * before the g_wbuf writes are visible). */
static pthread_mutex_t g_wx_mu = PTHREAD_MUTEX_INITIALIZER;
static int g_wready = 0;     /* a fresh result is waiting          (guarded) */
static int g_inflight = 0;   /* a fetch thread is running          (guarded) */
static int g_have = 0;       /* got weather at least once          (main thread only) */
static uint32_t g_last = 0;  /* tick of last fetch start           (main thread only) */
static int g_wfail = 0;      /* last fetch returned no usable data (guarded) */
static int g_wgen = 0;       /* bumps when the location changes    (main thread only) */
static int g_wjobgen = 0;    /* g_wgen the in-flight fetch serves  (main thread only) */

/* percent-encode into a URL-safe AND shell-safe form (only [A-Za-z0-9-_.~] + %XX),
 * so g_loc can never break out of the single-quoted wget command. */
static void wx_urlenc(const char *s, char *out, int cap)
{
    static const char *hex = "0123456789ABCDEF";
    int o = 0;
    for(; *s && o < cap - 4; s++){
        unsigned char c = (unsigned char)*s;
        if((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'||c=='~'||c==',')
            out[o++] = (char)c;
        else { out[o++]='%'; out[o++]=hex[c>>4]; out[o++]=hex[c&15]; }
    }
    out[o] = 0;
}

static void *weather_thread(void *arg)
{
    (void)arg;
    int fail = 1;    /* result of this fetch, published under the lock at the end */
    char result[160] = {0};   /* built here; copied into g_wbuf UNDER the lock at publish (no torn reads) */
    char line[256] = {0};
    char cmd[320];   /* fits "wget ... 'http://wttr.in/<g_loc≤141>?format=...' ..." */
    if (g_loc[0])
        snprintf(cmd, sizeof cmd, "wget -qO- -T 8 'http://wttr.in/%s?format=%%t~%%C~%%l' 2>/dev/null", g_loc);
    else
        snprintf(cmd, sizeof cmd, "wget -qO- -T 8 'http://wttr.in/?format=%%t~%%C~%%l' 2>/dev/null");
    FILE *f = popen(cmd, "r");
    if (f) { if (!fgets(line, sizeof line, f)) line[0] = 0; pclose(f); }
    else fprintf(stderr, "weather popen failed: %s\n", strerror(errno));
    if (line[0] && strchr(line, '~')) {
        char temp[32] = {0}, cond[128] = {0};
        char *t1 = strchr(line, '~');
        int tl = t1 - line; if (tl > 31) tl = 31;
        memcpy(temp, line, tl); temp[tl] = 0;
        char *c = t1 + 1;
        char *t2 = strchr(c, '~');
        int full = t2 ? (int)(t2 - c) : (int)strlen(c);
        while (full > 0 && (c[full-1] == '\n' || c[full-1] == '\r')) full--;
        /* lowercase the full condition phrase for icon matching */
        char low[128]; int li = 0;
        for (int i = 0; i < full && li < 127; i++) {
            char ch = c[i]; if (ch >= 'A' && ch <= 'Z') ch += 32; low[li++] = ch;
        }
        low[li] = 0;
        const char *ic = WI_CLOUD;
        if      (strstr(low,"thunder") || strstr(low,"storm"))                       ic = WI_BOLT;
        else if (strstr(low,"snow") || strstr(low,"sleet") || strstr(low,"blizzard") ||
                 strstr(low,"ice"))                                                  ic = WI_SNOW;
        else if (strstr(low,"rain") || strstr(low,"shower") || strstr(low,"drizzle"))ic = WI_RAIN;
        else if (strstr(low,"fog") || strstr(low,"mist") || strstr(low,"haze") ||
                 strstr(low,"cloud") || strstr(low,"overcast"))                      ic = WI_CLOUD;
        else if (strstr(low,"sun") || strstr(low,"clear"))                           ic = WI_SUN;
        /* display condition = first phrase (cut at comma) */
        int cl = full;
        char *comma = memchr(c, ',', cl); if (comma) cl = (int)(comma - c);
        if (cl > 120) cl = 120;
        memcpy(cond, c, cl); cond[cl] = 0;
        char *tp = temp; if (*tp == '+') tp++;   /* drop leading + on positive temps */
        snprintf(result, sizeof result, "%s  %s  %s", ic, tp, cond);   /* into local; g_wbuf write is under the lock */
        { size_t n = strlen(result);                    /* the service can pad the condition: trailing blanks would
                                                         * count in the centred label's width and push it off centre */
          while(n && (result[n - 1] == ' ' || result[n - 1] == '\t' || result[n - 1] == '\r' || result[n - 1] == '\n')) result[--n] = 0; }
        fail = 0;
    }
    if (fail) fprintf(stderr, "weather: no usable data (resp='%.48s')\n", line);
    /* publish atomically: the lock release/acquire pairs with weather_poll so the
     * g_wbuf writes above are guaranteed visible once it observes g_wready. */
    pthread_mutex_lock(&g_wx_mu);
    if (!fail) memcpy(g_wbuf, result, sizeof g_wbuf);   /* g_wbuf written ONLY under the lock */
    g_wfail = fail; g_wready = 1; g_inflight = 0;
    pthread_mutex_unlock(&g_wx_mu);
    return NULL;
}


/* ---- forecast (for the dial) ----------------------------------------------------------------------------
 * wttr.in's JSON (?format=j1): the current condition, today's high/low, and 3-hourly slots for three days.
 * Fetched only when the Weather screen opens (and at most every 10 min), in its own thread, so the passive
 * home/saver weather costs nothing extra. Temperatures follow the units of the classic reading (C or F). */
typedef struct { int day, hour, t, code; } fslot_t;
typedef struct {
    int ok, use_f, cur_t, cur_code, hi, lo, n;
    char loc[48], cond[40];
    fslot_t s[24];
} fc_t;
static pthread_mutex_t g_fc_mu = PTHREAD_MUTEX_INITIALIZER;
static fc_t g_fc, g_fc_pub;
static int g_fc_inflight, g_fc_ready, g_fc_have;
static uint32_t g_fc_last;
static char g_floc[160];

/* the value of "key":"value" (or "key":number) inside obj[0..end) */
static int jval(const char *obj, const char *end, const char *key, char *out, int cap){
    char pat[40]; snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *p = obj;
    size_t pl = strlen(pat);
    for(;;){
        p = memmem(p, (size_t)(end - p), pat, pl);
        if(!p) return 0;
        const char *q = p + pl; while(q < end && (*q == ' ' || *q == ':')) q++;
        if(q >= end) return 0;
        int n = 0;
        if(*q == '"'){ q++; while(q < end && *q != '"' && n < cap - 1){ if(*q == '\\' && q + 1 < end) q++; out[n++] = *q++; } }
        else { while(q < end && *q != ',' && *q != '}' && *q != ']' && n < cap - 1) out[n++] = *q++; }
        out[n] = 0;
        return n > 0;
    }
}
static int jint(const char *obj, const char *end, const char *key, int *out){
    char b[24]; if(!jval(obj, end, key, b, sizeof b)) return 0;
    char *e; long v = strtol(b, &e, 10); if(e == b) return 0;
    *out = (int)v; return 1;
}
/* the next top-level {...} at or after p (strings skipped); returns its start and sets *end past the closing brace */
static const char *jnext_obj(const char *p, const char *lim, const char **end){
    while(p < lim && *p != '{'){ if(*p == ']') return NULL; p++; }
    if(p >= lim) return NULL;
    int depth = 0, instr = 0; const char *q = p;
    for(; q < lim; q++){
        if(instr){ if(*q == '\\') q++; else if(*q == '"') instr = 0; continue; }
        if(*q == '"') instr = 1;
        else if(*q == '{') depth++;
        else if(*q == '}'){ if(--depth == 0){ *end = q + 1; return p; } }
    }
    return NULL;
}
/* "key":[ ... ] : pointer just after the '[' , or NULL */
static const char *jarray(const char *obj, const char *end, const char *key){
    char pat[40]; snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *p = memmem(obj, (size_t)(end - obj), pat, strlen(pat));
    if(!p) return NULL;
    p += strlen(pat); while(p < end && *p != '[') p++;
    return p < end ? p + 1 : NULL;
}
#ifndef WX_TEST
static
#endif
int wx_parse_j1(const char *js, fc_t *o, int use_f){
    memset(o, 0, sizeof *o); o->use_f = use_f;
    const char *end = js + strlen(js);
    const char *tk = use_f ? "temp_F" : "temp_C";
    const char *cur = jarray(js, end, "current_condition"), *ce;
    if(!cur || !(cur = jnext_obj(cur, end, &ce))) return 0;
    if(!jint(cur, ce, tk, &o->cur_t)) return 0;
    jint(cur, ce, "weatherCode", &o->cur_code);
    const char *area = jarray(js, end, "nearest_area"), *ae;
    if(area && (area = jnext_obj(area, end, &ae))){
        const char *an = jarray(area, ae, "areaName"), *ne;
        if(an && (an = jnext_obj(an, ae, &ne))) jval(an, ne, "value", o->loc, sizeof o->loc);
    }
    const char *days = jarray(js, end, "weather"), *de;
    for(int d = 0; days && d < 3 && (days = jnext_obj(days, end, &de)); d++, days = de){
        if(d == 0){ jint(days, de, use_f ? "maxtempF" : "maxtempC", &o->hi); jint(days, de, use_f ? "mintempF" : "mintempC", &o->lo); }
        const char *hr = jarray(days, de, "hourly"), *he;
        for(; hr && o->n < 24 && (hr = jnext_obj(hr, de, &he)); hr = he){
            fslot_t *s = &o->s[o->n]; int tm = 0;
            if(!jint(hr, he, "time", &tm) || !jint(hr, he, use_f ? "tempF" : "tempC", &s->t)) continue;
            jint(hr, he, "weatherCode", &s->code);
            s->day = d; s->hour = tm / 100; o->n++;
        }
    }
    o->ok = o->n > 0;
    return o->ok;
}
static void *forecast_thread(void *arg){
    int use_f = (int)(intptr_t)arg;
    char cmd[320];
    snprintf(cmd, sizeof cmd, "wget -qO- -T 10 'http://wttr.in/%s?format=j1' 2>/dev/null", g_floc);
    static char buf[48 * 1024]; size_t n = 0;                   /* one fetch at a time: g_fc_inflight */
    FILE *f = popen(cmd, "r");
    if(f){ size_t r; while(n < sizeof buf - 1 && (r = fread(buf + n, 1, sizeof buf - 1 - n, f)) > 0) n += r; pclose(f); }
    buf[n] = 0;
    fc_t fc; int ok = n > 0 && wx_parse_j1(buf, &fc, use_f);
    pthread_mutex_lock(&g_fc_mu);
    if(ok) g_fc_pub = fc;
    g_fc_ready = ok ? 1 : 2; g_fc_inflight = 0;                 /* 1 = new data, 2 = failed */
    pthread_mutex_unlock(&g_fc_mu);
    return NULL;
}
static void forecast_fetch_async(void){
    if(g_fc_have && lv_tick_elaps(g_fc_last) < 600000u) return;   /* fresh enough */
    int go = 0;
    pthread_mutex_lock(&g_fc_mu);
    if(!g_fc_inflight){ g_fc_inflight = 1; g_fc_ready = 0; go = 1; }
    pthread_mutex_unlock(&g_fc_mu);
    if(!go) return;
    wx_urlenc(cfg_get_str("weather_loc", ""), g_floc, sizeof g_floc);   /* main thread: cfg isn't thread-safe */
    int use_f = 0;
    pthread_mutex_lock(&g_wx_mu); use_f = strstr(g_wbuf, "\xC2\xB0" "F") != NULL; pthread_mutex_unlock(&g_wx_mu);
    g_fc_last = lv_tick_get();
    pthread_t th;
    if(pthread_create(&th, NULL, forecast_thread, (void *)(intptr_t)use_f) == 0) pthread_detach(th);
    else { pthread_mutex_lock(&g_fc_mu); g_fc_inflight = 0; g_fc_ready = 2; pthread_mutex_unlock(&g_fc_mu); }
}

void weather_fetch_async(void)
{
    int go = 0;
    pthread_mutex_lock(&g_wx_mu);
    if (!g_inflight) { g_inflight = 1; g_wready = 0; go = 1; }   /* claim slot + drop any stale unconsumed result */
    pthread_mutex_unlock(&g_wx_mu);
    if (!go) return;
    g_wjobgen = g_wgen;   /* this fetch serves the current location generation */
    /* read the configured location on the main thread (cfg is not thread-safe);
     * percent-encode it so it's URL-safe AND can't break out of the single-quoted
     * wget command. Empty => wttr.in auto-geolocates by IP. Set before pthread_create,
     * whose barrier publishes it to the worker. */
    wx_urlenc(cfg_get_str("weather_loc", ""), g_loc, sizeof g_loc);
    g_last = lv_tick_get();
    pthread_t th;
    if (pthread_create(&th, NULL, weather_thread, NULL) == 0) pthread_detach(th);
    else {   /* no worker -> publish a failure so the app shows "unavailable", not a stuck "Fetching..." */
        pthread_mutex_lock(&g_wx_mu);
        g_inflight = 0; g_wfail = 1; g_wready = 1;
        pthread_mutex_unlock(&g_wx_mu);
    }
}

/* Main-thread tick: apply a finished fetch, and retry every 20s until the first
 * success (covers WiFi coming up after boot), then refresh every 10 min. */
static void weather_app_refresh(void);   /* fwd */

/* Enable/disable passive weather (home + screensaver display + the background fetch). Called from
 * Settings. Off = no wget, no display (battery); on = force a refetch on the next poll. */
void weather_set_enabled(int on)
{
    if (on) { g_last = 0; }
    else    { home_set_weather(""); saver_set_weather(""); }
}

void weather_poll(lv_timer_t *t)
{
    (void)t;
    {   /* a finished forecast (only ever fetched while the Weather screen is open) */
        int r = 0;
        pthread_mutex_lock(&g_fc_mu);
        if(g_fc_ready){ r = g_fc_ready; g_fc_ready = 0; if(r == 1){ g_fc = g_fc_pub; g_fc_have = 1; } }
        pthread_mutex_unlock(&g_fc_mu);
        if(r) weather_app_refresh();
    }
    if (!cfg_get_int("weather_on", 1)) {
        /* weather off -> no fetch, no display. But DRAIN any in-flight worker result so a fetch that
         * completes while disabled can't flash a stale reading the instant it's re-enabled. */
        pthread_mutex_lock(&g_wx_mu);
        g_wready = 0;
        pthread_mutex_unlock(&g_wx_mu);
        return;
    }
    int ready = 0, fail = 0, inflight;
    pthread_mutex_lock(&g_wx_mu);
    if (g_wready) { g_wready = 0; ready = 1; fail = g_wfail; }
    inflight = g_inflight;
    pthread_mutex_unlock(&g_wx_mu);
    if (ready) {
        if (g_wjobgen != g_wgen) {
            /* result is for a since-changed location -> discard + refetch the current one now */
            g_last = 0;
        } else if (!fail) {        /* keep last-good on failure; only update on success.
                                    * g_wbuf safe here: worker set g_inflight=0 under the same
                                    * lock and the next fetch only starts below, after this use. */
            g_have = 1;
            home_set_weather(g_wbuf);
            saver_set_weather(g_wbuf);
        }
        weather_app_refresh();     /* shows data, or "Weather unavailable" if none yet */
    }
    uint32_t now = lv_tick_get();
    uint32_t interval = g_have ? 600000u : 20000u;
    if (!inflight && (now - g_last) > interval) weather_fetch_async();
}

/* ---- Weather app: set the location used for home + screensaver weather ---- */
/* the dial: 8 forecast slots round the rim, the current weather in the middle */
#define WD_SLOTS 8
static lv_obj_t *g_wd_icon[WD_SLOTS], *g_wd_temp[WD_SLOTS], *g_wd_hour[WD_SLOTS];
static lv_obj_t *g_wd_cicon, *g_wd_ctemp, *g_wd_cond, *g_wd_loc, *g_wd_hl, *g_wd_hint;
void weather_app_open(void) { forecast_fetch_async(); weather_app_refresh(); screen_show(SCR_WEATHER); }

/* wttr.in weather codes -> our icon set (night clear = moon) */
static const char *wd_icon(int code, int night){
    switch(code){
        case 113: return night ? WI_MOON : WI_SUN;
        case 116: return night ? WI_MOON : WI_SUN;
        case 200: case 386: case 389: case 392: case 395: return WI_BOLT;
        case 179: case 182: case 185: case 227: case 230: case 317: case 320: case 323: case 326: case 329: case 332:
        case 335: case 338: case 350: case 362: case 365: case 368: case 371: case 374: case 377: return WI_SNOW;
        case 176: case 263: case 266: case 281: case 284: case 293: case 296: case 299: case 302: case 305: case 308:
        case 311: case 314: case 353: case 356: case 359: return WI_RAIN;
        default: return WI_CLOUD;                                  /* cloud, overcast, fog, mist */
    }
}
static lv_color_t wd_col(const char *ic){
    if(!strcmp(ic, WI_SUN) || !strcmp(ic, WI_BOLT)) return TC(WEATHER_SUN);
    if(!strcmp(ic, WI_MOON)) return TC(WEATHER_MOON);
    if(!strcmp(ic, WI_RAIN)) return TC(WEATHER_RAIN);
    return TC(TEXT_SECONDARY);
}
static void set_if(lv_obj_t *l, const char *t){ if(strcmp(lv_label_get_text(l), t)) lv_label_set_text(l, t); }
static void weather_app_refresh(void)
{
    if(!g_wd_ctemp) return;
    char snap[160]; int wfail;
    pthread_mutex_lock(&g_wx_mu);                     /* the worker can be mid-write: snapshot under the lock */
    snprintf(snap, sizeof snap, "%s", g_wbuf);
    wfail = g_wfail;
    pthread_mutex_unlock(&g_wx_mu);
    fc_t fc; int have_fc, fc_state;
    pthread_mutex_lock(&g_fc_mu); fc = g_fc; have_fc = g_fc_have; fc_state = g_fc_inflight; pthread_mutex_unlock(&g_fc_mu);
    /* current: "<icon>  <temp>  <condition>" from the classic reading (the same one the home glance shows) */
    char temp[32] = "--", cond[64] = "";
    const char *icon = WI_CLOUD;
    if(g_have){
        char *sp = strstr(snap, "  ");
        if(sp){
            static char ic[8]; int il = (int)(sp - snap); if(il > 7) il = 7; memcpy(ic, snap, (size_t)il); ic[il] = 0;
            if(!strncmp(ic, WI_SUN, 3)) icon = WI_SUN; else if(!strncmp(ic, WI_MOON, 3)) icon = WI_MOON; else if(!strncmp(ic, WI_RAIN, 3)) icon = WI_RAIN;
            else if(!strncmp(ic, WI_BOLT, 3)) icon = WI_BOLT; else if(!strncmp(ic, WI_SNOW, 3)) icon = WI_SNOW;
            char *t2 = sp + 2; char *sp2 = strstr(t2, "  ");
            if(sp2){ int tl = (int)(sp2 - t2); if(tl > 31) tl = 31; memcpy(temp, t2, (size_t)tl); temp[tl] = 0; snprintf(cond, sizeof cond, "%.60s", sp2 + 2); }
        }
    }
    lv_label_set_text(g_wd_cicon, g_have ? icon : "");
    lv_obj_set_style_text_color(g_wd_cicon, wd_col(icon), 0);
    set_if(g_wd_ctemp, g_have ? temp : (wfail ? "n/a" : "..."));
    set_if(g_wd_cond, g_have ? cond : (wfail ? "Weather unavailable" : "Fetching..."));
    const char *l = cfg_get_str("weather_loc", "");
    char loc[64]; snprintf(loc, sizeof loc, "%.60s", (l && l[0]) ? l : (have_fc && fc.loc[0] ? fc.loc : "Auto (by IP)"));
    set_if(g_wd_loc, loc);
    char hl[40] = "";
    if(have_fc && fc.ok) snprintf(hl, sizeof hl, "H %d\xC2\xB0  L %d\xC2\xB0", fc.hi, fc.lo);
    set_if(g_wd_hl, hl);
    /* the next eight 3-hourly slots after "now" (the device's clock) */
    time_t now = time(NULL); struct tm lt; localtime_r(&now, &lt);
    int now_abs = lt.tm_hour * 60 + lt.tm_min, first = -1;
    if(have_fc && fc.ok){ for(int i = 0; i < fc.n; i++) if(fc.s[i].day * 1440 + fc.s[i].hour * 60 > now_abs){ first = i; break; } }
    for(int k = 0; k < WD_SLOTS; k++){
        int i = first >= 0 ? first + k : -1;
        if(i < 0 || i >= fc.n){ set_if(g_wd_icon[k], ""); set_if(g_wd_temp[k], ""); set_if(g_wd_hour[k], ""); continue; }
        int night = fc.s[i].hour >= 20 || fc.s[i].hour < 6;
        const char *ic = wd_icon(fc.s[i].code, night);
        set_if(g_wd_icon[k], ic); lv_obj_set_style_text_color(g_wd_icon[k], wd_col(ic), 0);
        char b[16]; snprintf(b, sizeof b, "%d\xC2\xB0", fc.s[i].t); set_if(g_wd_temp[k], b);
        snprintf(b, sizeof b, th_disco() ? "%02d:00" : "%02d", fc.s[i].hour); set_if(g_wd_hour[k], b);
    }
    set_if(g_wd_hint, (have_fc && fc.ok) ? "" : (fc_state ? "Loading forecast..." : "Forecast unavailable"));
}
/* host renders only: a fixed reading + forecast (never called on the Disc) */
void weather_demo(void){
    time_t now = time(NULL); struct tm lt; localtime_r(&now, &lt);
    pthread_mutex_lock(&g_wx_mu); snprintf(g_wbuf, sizeof g_wbuf, "%s  18\xC2\xB0" "C  Partly cloudy", WI_SUN); g_wfail = 0; pthread_mutex_unlock(&g_wx_mu);
    g_have = 1;
    static const int CODE[10] = { 113, 116, 116, 119, 176, 176, 113, 113, 116, 119 }, T[10] = { 18, 19, 17, 15, 14, 13, 12, 12, 14, 16 };
    pthread_mutex_lock(&g_fc_mu);
    memset(&g_fc, 0, sizeof g_fc); g_fc.ok = 1; g_fc.hi = 21; g_fc.lo = 11; g_fc.n = 10;
    for(int i = 0; i < 10; i++){ int h = (lt.tm_hour / 3 + 1 + i) * 3; g_fc.s[i].day = h / 24; g_fc.s[i].hour = h % 24; g_fc.s[i].t = T[i]; g_fc.s[i].code = CODE[i]; }
    g_fc_have = 1; pthread_mutex_unlock(&g_fc_mu);
    cfg_set_str("weather_loc", "Amsterdam");
    weather_app_refresh();
}
#ifdef WX_TEST
void wx_test_refresh(void){ weather_app_refresh(); }
#endif

static void loc_done(const char *text)
{
    cfg_set_str("weather_loc", text ? text : "");
    g_wgen++;                   /* invalidate any in-flight fetch for the old location */
    g_have = 0;                 /* force a fresh fetch + show "Fetching" */
    g_last = 0;
    weather_fetch_async();
    weather_app_refresh();
}
static void set_loc_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    kbinput_open("City / location", cfg_get_str("weather_loc", ""), loc_done);
}
static void auto_loc_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    cfg_set_str("weather_loc", "");
    g_wgen++;                   /* invalidate any in-flight fetch for the old location */
    g_have = 0; g_last = 0;
    weather_fetch_async();
    weather_app_refresh();
}

static void loc_tap_cb(lv_event_t *e){
    lv_event_code_t c = lv_event_get_code(e);
    if(c == LV_EVENT_SHORT_CLICKED) set_loc_cb(e);                  /* tap the place: type a city */
    else if(c == LV_EVENT_LONG_PRESSED){ auto_loc_cb(e); ui_toast("Location: automatic"); }   /* hold: by IP */
}
static lv_obj_t *wlabel(lv_obj_t *root, const lv_font_t *f, lv_color_t col){
    lv_obj_t *l = lv_label_create(root);
    lv_label_set_text(l, "");
    lv_obj_set_style_text_font(l, f, 0); lv_obj_set_style_text_color(l, col, 0);
    return l;
}
void weather_app_create(lv_obj_t *root)
{
    lv_obj_set_style_bg_color(root, TC(CANVAS), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    s_appwfont = *theme_font_original(14);                             /* text + the weather icons as fallback */
    s_appwfont.fallback = &font_weather16;
    static lv_font_t s_bigwf; s_bigwf = *theme_font_original(28); s_bigwf.fallback = &font_weather16;
    lv_obj_t *ring = lv_obj_create(root);                           /* the faint inner circle */
    lv_obj_remove_style_all(ring);
    lv_obj_set_size(ring, 200, 200); lv_obj_center(ring);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(ring, 2, 0); lv_obj_set_style_border_color(ring, TC(WEATHER_BORDER), 0);
    lv_obj_clear_flag(ring, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    for(int k = 0; k < WD_SLOTS; k++){
        float a = (-90.0f + k * 45.0f) * 0.0174533f;
        int cx = (int)lroundf(128 * cosf(a)), cy = (int)lroundf(128 * sinf(a));
        g_wd_icon[k] = wlabel(root, &s_appwfont, TC(TEXT_SECONDARY));
        g_wd_temp[k] = wlabel(root, TF(UI_14), TC(TEXT_PRIMARY));
        g_wd_hour[k] = wlabel(root, TF(UI_10), TC(TEXT_DISABLED));
        lv_obj_align(g_wd_icon[k], LV_ALIGN_CENTER, cx, cy - 10);
        lv_obj_align(g_wd_temp[k], LV_ALIGN_CENTER, cx, cy + 9);
        lv_obj_align(g_wd_hour[k], LV_ALIGN_CENTER, (int32_t)lroundf(160 * cosf(a)), (int32_t)lroundf(160 * sinf(a)));
    }
    g_wd_cicon = wlabel(root, &s_bigwf, TC(TEXT_SECONDARY));
    lv_obj_align(g_wd_cicon, LV_ALIGN_CENTER, 0, -58);
    g_wd_ctemp = wlabel(root, TF(UI_46), TC(TEXT_PRIMARY));
    lv_obj_align(g_wd_ctemp, LV_ALIGN_CENTER, 0, -8);
    g_wd_cond = wlabel(root, ui_font_cjk(14), TC(TEXT_SECONDARY));
    lv_label_set_long_mode(g_wd_cond, LV_LABEL_LONG_DOT); lv_obj_set_width(g_wd_cond, 150);
    lv_obj_set_style_text_align(g_wd_cond, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(g_wd_cond, LV_ALIGN_CENTER, 0, 26);
    g_wd_loc = wlabel(root, ui_font_cjk(16), TC(TEXT_PRIMARY));                 /* tap = change, hold = automatic */
    lv_label_set_long_mode(g_wd_loc, LV_LABEL_LONG_DOT); lv_obj_set_width(g_wd_loc, 150);
    lv_obj_set_style_text_align(g_wd_loc, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(g_wd_loc, LV_ALIGN_CENTER, 0, 46);
    lv_obj_add_flag(g_wd_loc, LV_OBJ_FLAG_CLICKABLE); lv_obj_set_ext_click_area(g_wd_loc, 10);
    lv_obj_add_event_cb(g_wd_loc, loc_tap_cb, LV_EVENT_SHORT_CLICKED, NULL);
    lv_obj_add_event_cb(g_wd_loc, loc_tap_cb, LV_EVENT_LONG_PRESSED, NULL);
    g_wd_hl = wlabel(root, TF(UI_12), TC(TEXT_DISABLED));
    lv_obj_align(g_wd_hl, LV_ALIGN_CENTER, 0, 64);
    g_wd_hint = wlabel(root, TF(UI_10), TC(TEXT_HINT));
    lv_obj_align(g_wd_hint, LV_ALIGN_CENTER, 0, 80);
    weather_app_refresh();
    if(th_disco()){        /* Disco: laid out like Quick Settings: now in the navigation hub's field, the next 12 h as rows */
        lv_obj_add_flag(ring, LV_OBJ_FLAG_HIDDEN);
        lv_obj_t *f = lv_obj_create(root); lv_obj_remove_style_all(f);          /* the hub's shape, its right end cut by the rim */
        lv_obj_set_pos(f, 196, 90); lv_obj_set_size(f, 230, 180);
        lv_obj_set_style_radius(f, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(f, TC(SURFACE), 0); lv_obj_set_style_bg_opa(f, 204, 0);
        lv_obj_clear_flag(f, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_move_to_index(f, 0);
        const int FX = 256 - 180;                                    /* the field's free middle, clear of the closed sliver */
        lv_obj_align(g_wd_cicon, LV_ALIGN_CENTER, FX, -64);
        lv_obj_align(g_wd_ctemp, LV_ALIGN_CENTER, FX, -30);
        lv_obj_align(g_wd_cond, LV_ALIGN_CENTER, FX, 4);   lv_obj_set_width(g_wd_cond, 116);
        lv_obj_align(g_wd_loc, LV_ALIGN_CENTER, FX, 26);   lv_obj_set_width(g_wd_loc, 116);
        lv_obj_align(g_wd_hl, LV_ALIGN_CENTER, FX, 48);    lv_obj_set_style_text_color(g_wd_hl, TC(TEXT_SECONDARY), 0);
        lv_obj_set_style_text_font(g_wd_ctemp, TF(UI_40), 0);
        for(int k = 0; k < WD_SLOTS; k++){
            if(k >= 4){ lv_obj_add_flag(g_wd_icon[k], LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(g_wd_temp[k], LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(g_wd_hour[k], LV_OBJ_FLAG_HIDDEN); continue; }
            int y = 60 + k * 62, mid = y + 27, dy = mid - 180; if(dy < 0) dy = -dy;   /* Settings-size rows */
            int left = 180 - (int)sqrtf((float)(176 * 176 - (dy + 27) * (dy + 27))) + 4; if(left < 14) left = 14;
            lv_obj_t *r = lv_obj_create(root); lv_obj_remove_style_all(r);   /* a pill row: hour, icon, temperature */
            lv_obj_set_pos(r, left, y); lv_obj_set_size(r, 190 - left, TH_ROW_H);
            lv_obj_set_style_radius(r, LV_RADIUS_CIRCLE, 0); lv_obj_set_style_bg_color(r, TC(SURFACE), 0); lv_obj_set_style_bg_opa(r, LV_OPA_70, 0);
            lv_obj_clear_flag(r, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_parent(g_wd_hour[k], r); lv_obj_set_parent(g_wd_icon[k], r); lv_obj_set_parent(g_wd_temp[k], r);
            lv_obj_set_style_text_font(g_wd_hour[k], TH_F_LIST, 0); lv_obj_set_style_text_color(g_wd_hour[k], TC(TEXT_PRIMARY), 0);
            lv_obj_align(g_wd_hour[k], LV_ALIGN_LEFT_MID, 12, 0);
            lv_obj_align(g_wd_icon[k], LV_ALIGN_LEFT_MID, 70, 0);   /* right after the hour, even in the narrow rows */
            lv_obj_set_style_text_font(g_wd_temp[k], TH_F_LIST, 0); lv_obj_align(g_wd_temp[k], LV_ALIGN_RIGHT_MID, -12, 0);
        }
        lv_obj_align(g_wd_hint, LV_ALIGN_TOP_LEFT, 70, 306);
    }
    if(th_braun()){                                               /* Braun: the grille, a panel under the dial, dark type */
        br_face(root); lv_obj_t *pd = br_disc(root, 180, 180, 104, BR_PANEL); lv_obj_move_to_index(pd, 1);
        lv_obj_set_style_text_color(g_wd_ctemp, TC(TEXT_PRIMARY), 0);
        lv_obj_set_style_text_color(g_wd_cond, TC(TEXT_SECONDARY), 0);
        lv_obj_set_style_text_color(g_wd_loc, TC(TEXT_PRIMARY), 0);
        lv_obj_set_style_text_color(g_wd_hl, TC(TEXT_DISABLED), 0);
        for(int k = 0; k < WD_SLOTS; k++){ lv_obj_set_style_text_color(g_wd_temp[k], TC(TEXT_PRIMARY), 0); lv_obj_set_style_text_color(g_wd_hour[k], TC(TEXT_DISABLED), 0); }
    }
}
