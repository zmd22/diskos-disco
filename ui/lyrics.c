/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "screens.h"
#include "theme.h"
#include "sdio.h"
#include "ipc.h"
#include "musicdb.h"
#include "config.h"
#include "lrc.h"
#include "lyrics_fx.h"
#include "braun.h"
#include "zune_dial.h"
#include "theme_kit.h"
LV_FONT_DECLARE(font_zune_light_40)
LV_FONT_DECLARE(font_zune_light_32)
LV_FONT_DECLARE(font_zune_light_18)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>

/* Lyrics view: a sibling .lrc on the SD card first, then lyrics stored inside the audio file (lrc.h), then - unless
 * Settings > Display > Online Lyrics is off - lrclib.net by track + artist (no API key). The blocking
 * work runs on a detached thread; the result is applied on the main thread by lyrics_poll(). Timed lyrics (a .lrc,
 * or lrclib's syncedLyrics) show one row per line: the line at the playback position is highlighted and kept in
 * the middle, unless the user has just scrolled. Untimed lyrics show as one block of text. */

static lv_obj_t *g_scroll;
static lv_obj_t *g_text;           /* plain lyrics / status messages */
static lv_obj_t *g_lines_box;      /* timed lyrics: one label per line (hidden otherwise) */
static lv_obj_t *g_zcur, *g_znext, *g_zbig, *g_zhint, *g_ztop;

#define LY_MAXL 400
static lrc_line_t g_ll[LY_MAXL];   /* the timed lines on screen (main thread only) */
#define LY_MAX_WORDS 2048
static lrc_word_t g_words[LY_MAX_WORDS];
static lrc_word_t g_estimated_words[LYR_WORDS_MAX]; /* current line only; parsed timings stay unchanged */
static int g_word_count;
static lyrics_fx_t g_word_fx;
static lv_timer_t *g_motion_timer;
static int g_auto_scroll;
static int g_reading_touch;
static int g_pressed_line = -1;
static unsigned g_pressed_request;
static unsigned g_pressed_path_seq;
static char g_pressed_path[256], g_pressed_title[200];
static lv_point_t g_pressed_point;
static int g_press_moved;
static uint32_t g_seek_until;
static long g_seek_target;
static unsigned g_seek_sequence;
static long g_follow_position = -1;
static char g_ltext[16384];        /* their text */
static char g_lplain[16384];       /* untimed rendering of a .lrc */
static int g_nll = 0;              /* timed lines shown; 0 = block-text mode */
static int g_lcur = -2;            /* highlighted line (-1 = before the first, -2 = not drawn yet) */
static int g_holding = 0;          /* the user scrolled: no auto-scroll until g_hold_until */
static uint32_t g_hold_until = 0;
static int32_t g_scroll_target = -1; /* the scroll position last asked for (-1 = none) */
static long g_pos_base = 0;        /* displayed anchor; repeated quantized a1 frames do not reset it */
static uint32_t g_pos_tick = 0;
static long g_reported_position;
static int g_clock_valid, g_clock_playing;
static int g_progressive = -1;
static int g_estimate_words = -1;
static long g_estimate_duration;

/* worker->main handoff guarded by a mutex (volatile gives no cross-thread memory
 * ordering - on the dual-core X2000 the main thread could see g_lready set before the
 * g_lbuf writes are visible). The lock release/acquire pairs publish with consume. */
static pthread_mutex_t g_ly_mu = PTHREAD_MUTEX_INITIALIZER;
static char g_lbuf[16384];         /* result text (lyrics or message)  (see handoff note) */
static int g_lkind = 0;            /* what g_lbuf holds: 0 = a message, 1 = plain lyrics, 2 = LRC text (guarded) */
static int g_lready = 0;           /* a fetch finished                 (guarded) */
static int g_linflight = 0;        /* a fetch thread is running        (guarded) */
static int g_lphase = 0;           /* 0=checking local .lrc, 1=searching online (guarded) */
static unsigned g_req = 0;         /* bumps on each lyrics_open         (main thread only) */
static unsigned g_done_req = 0;    /* request id the finished fetch was for (guarded) */
static char g_q_title[512], g_q_artist[512];  /* url-encoded query (main thread only); 3x MDB_STR(160)
                                               * expansion for %-encoded non-ASCII/CJK titles + margin */
static char g_identity_title[200], g_identity_artist[200];
static int g_identity_online=-1;
static unsigned g_identity_path_seq;
static unsigned g_irev, g_cached_req;
static int g_cached_n;
static lyr_line_t g_cached_lines[240];
static char g_path[256];                       /* current track path (main thread only) */

/* Per-fetch inputs snapshotted on the MAIN thread before pthread_create and handed to
 * the worker as its arg, so the worker never reads the g_req / g_q_title / g_q_artist /
 * g_path globals directly (they are main-thread-only; a re-open could otherwise race
 * the worker's reads of them). */
typedef struct { unsigned req; int online; char title[512], artist[512], path[256]; } ly_job_t;  /* enc query, see g_q_* */


static int timed_parse(const char *raw,lyr_line_t *out,int cap);
static void lyrics_follow(void);
static void lyric_tap(lv_event_t *e);

static void lyric_scroll_exec(void *object, int32_t y)
{
    g_auto_scroll = 1;
    lv_obj_scroll_to_y(object, y, LV_ANIM_OFF);
    g_auto_scroll = 0;
}

static void lyric_fade_exec(void *object, int32_t opacity)
{
    lv_obj_set_style_text_opa(object, (lv_opa_t)opacity, 0);
}

static void lyric_emphasis(lv_obj_t *label, int current, int animate)
{
    lv_anim_delete(label, lyric_fade_exec);
    lv_obj_set_style_text_color(label, current ? TC(TEXT_LYRICS) : TC(TEXT_MUTED), 0);
    int target = current ? LV_OPA_COVER : 170;
    if(!animate || !screen_get_anim()) { lyric_fade_exec(label, target); return; }
    lv_anim_t a; lv_anim_init(&a);
    lv_anim_set_var(&a, label); lv_anim_set_exec_cb(&a, lyric_fade_exec);
    lv_anim_set_values(&a, lv_obj_get_style_text_opa(label, 0), target);
    lv_anim_set_duration(&a, 360); lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

static void lyric_motion_tick(lv_timer_t *timer)
{
    int ui_main_is_idle(void);
    int on = screen_current() == SCR_LYRICS && g_nll && g_scroll && !ui_main_is_idle();
    if(on){ lv_timer_set_period(timer, 40); lyrics_follow(); }
    else {
        lv_timer_pause(timer);
        if(g_scroll) lv_anim_delete(g_scroll, lyric_scroll_exec);
        g_scroll_target = -1;
        if(g_lines_box) for(uint32_t i = 0; i < lv_obj_get_child_count(g_lines_box); i++){
            lv_obj_t *label = lv_obj_get_child(g_lines_box, i);
            lv_anim_delete(label, lyric_fade_exec);
            lyric_fade_exec(label, (int)i == g_lcur ? LV_OPA_COVER : 170);
        }
    }
}

static void lyric_root_deleted(lv_event_t *e)
{
    (void)e;
    lyrics_fx_unbind(&g_word_fx);
    if(g_motion_timer) lv_timer_pause(g_motion_timer);
    if(g_scroll) lv_anim_delete(g_scroll, lyric_scroll_exec);
    g_scroll = g_text = g_lines_box = NULL;
    g_zcur = g_znext = g_zbig = g_zhint = g_ztop = NULL;
    g_nll = 0;
}

static void urlenc(const char *s, char *out, int cap)
{
    static const char *hex = "0123456789ABCDEF";
    int o = 0;
    for(; *s && o < cap - 4; s++){
        unsigned char c = (unsigned char)*s;
        if((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.')
            out[o++] = (char)c;
        else { out[o++]='%'; out[o++]=hex[c>>4]; out[o++]=hex[c&15]; }
    }
    out[o] = 0;
}

/* audio path -> sibling .lrc path */
static void derive_lrc(const char *audio, char *out, int cap)
{
    snprintf(out, cap, "%s", audio);
    char *slash = strrchr(out, '/');
    char *dot   = strrchr(out, '.');
    if(dot && (!slash || dot > slash)) snprintf(dot, cap - (int)(dot - out), ".lrc");
    else { int n = (int)strlen(out); snprintf(out + n, cap - n, ".lrc"); }
}

/* read a whole .lrc (up to cap-1 bytes; a longer file is cut at its last complete line) */
static int read_lrc(FILE *f, char *out, int cap)
{
    size_t n = fread(out, 1, (size_t)cap - 1, f);
    out[n] = 0;
    if(n == (size_t)cap - 1){ char *nl = strrchr(out, '\n'); if(nl) nl[1] = 0; }
    return (int)strlen(out);
}

/* parse 4 hex digits -> value, or -1 if any is not hex */
static int hex4(const char *s){
    int v = 0;
    for(int i=0;i<4;i++){
        char c = s[i]; int d;
        if(c>='0'&&c<='9') d = c-'0';
        else if(c>='a'&&c<='f') d = c-'a'+10;
        else if(c>='A'&&c<='F') d = c-'A'+10;
        else return -1;
        v = (v<<4) | d;
    }
    return v;
}
/* encode a Unicode codepoint as UTF-8 into out (up to 4 bytes); returns bytes written (0 if no room) */
static int utf8_enc(unsigned cp, char *out, int cap){
    if(cp < 0x80)   { if(cap<1) return 0; out[0]=(char)cp; return 1; }
    if(cp < 0x800)  { if(cap<2) return 0; out[0]=(char)(0xC0|(cp>>6));  out[1]=(char)(0x80|(cp&0x3F)); return 2; }
    if(cp < 0x10000){ if(cap<3) return 0; out[0]=(char)(0xE0|(cp>>12)); out[1]=(char)(0x80|((cp>>6)&0x3F)); out[2]=(char)(0x80|(cp&0x3F)); return 3; }
    if(cap<4) return 0;
    out[0]=(char)(0xF0|(cp>>18)); out[1]=(char)(0x80|((cp>>12)&0x3F)); out[2]=(char)(0x80|((cp>>6)&0x3F)); out[3]=(char)(0x80|(cp&0x3F)); return 4;
}

/* the first top-level result object of an lrclib search reply ("[{...},{...}]"): [*b, *e) - quotes and escapes are
 * followed so a brace or quote inside a value can't end it early. 0 if there is none (or it is cut off). */
static int json_first_object(const char *resp, const char **b, const char **e)
{
    const char *p = strchr(resp, '{');
    if(!p) return 0;
    int depth = 0, instr = 0;
    for(const char *q = p; *q; q++){
        if(instr){
            if(*q == '\\'){ if(!q[1]) return 0; q++; }
            else if(*q == '"') instr = 0;
        } else if(*q == '"') instr = 1;
        else if(*q == '{') depth++;
        else if(*q == '}' && --depth == 0){ *b = p; *e = q + 1; return 1; }
    }
    return 0;
}

/* pull the `key` JSON string ("key":"...") of the FIRST search result out of resp into out (unescaped); only a key
 * of that object counts, whatever order its fields come in */
static int json_lyrics(const char *resp, const char *key, char *out, int cap)
{
    char pat[40]; snprintf(pat, sizeof pat, "\"%s\":\"", key);
    out[0] = 0;
    const char *ob, *oe;
    if(!json_first_object(resp, &ob, &oe)) return 0;
    const char *p = strstr(ob, pat);
    if(!p || p + strlen(pat) > oe) return 0;
    p += strlen(pat);
    int len = 0;
    while(*p && len < cap - 2){
        if(*p == '\\'){
            p++; char e = *p;
            if(e=='n') out[len++]='\n';
            else if(e=='t') out[len++]=' ';
            else if(e=='"') out[len++]='"';
            else if(e=='\\') out[len++]='\\';
            else if(e=='/') out[len++]='/';
            else if(e=='r') { /* drop */ }
            else if(e=='u'){   /* \uXXXX -> UTF-8 (CJK etc.); combine surrogate pairs */
                int u = (p[1]&&p[2]&&p[3]&&p[4]) ? hex4(p+1) : -1;
                if(u >= 0){
                    unsigned cp = (unsigned)u;
                    p += 4;        /* consume the 4 hex digits (trailing p++ steps off the last one) */
                    if(cp>=0xD800 && cp<=0xDBFF && p[1]=='\\' && p[2]=='u' &&
                       p[3]&&p[4]&&p[5]&&p[6]){
                        int lo = hex4(p+3);
                        if(lo>=0xDC00 && lo<=0xDFFF){
                            cp = 0x10000u + ((cp-0xD800u)<<10) + (unsigned)(lo-0xDC00);
                            p += 6;   /* consume the "\uXXXX" low surrogate too */
                        }
                    }
                    /* drop any leftover lone/unpaired surrogate (0xD800..0xDFFF) - it
                     * has no valid UTF-8 encoding; only emit real scalar values. */
                    if(!(cp>=0xD800 && cp<=0xDFFF)) len += utf8_enc(cp, out+len, cap-1-len);
                } else {
                    out[len++]='u';   /* malformed \u -> keep the literal */
                }
            }
            else if(e) out[len++]=e;
            if(*p) p++;
        } else if(*p == '"'){
            break;            /* end of the JSON string */
        } else {
            out[len++] = *p++;
        }
    }
    out[len] = 0;
    return len;
}

static void *lyrics_thread(void *arg)
{
    ly_job_t *job = (ly_job_t *)arg;   /* our private snapshot; no shared-global reads */
    char cmd[1200];   /* holds two %-encoded query fields (2x512) + the wget URL template */
    static char resp[32768];           /* single worker at a time (inflight guard) -> static ok */
    resp[0] = 0;
    g_lbuf[0] = 0;                      /* sole writer until we publish; main reads after the lock */
    g_lkind = 0;

    /* 1) local sibling .lrc FIRST - instant for offline users who sideloaded lyrics (no 12s wait) */
    if(job->path[0] && sd_io_begin()){
        char lrc[320]; derive_lrc(job->path, lrc, sizeof lrc);
        FILE *f = fopen(lrc, "r");
        if(f){ read_lrc(f, g_lbuf, sizeof g_lbuf); fclose(f); }
        sd_io_end();
        g_lkind = 2;
    }

    /* 2) lyrics inside the audio file (ID3 USLT/SYLT, Vorbis comments, MP4 (c)lyr): still on the card, no network */
    if(!g_lbuf[0] && job->path[0] && sd_io_begin()){
        FILE *f = fopen(job->path, "rb");
        if(f){
            int synced = 0;
            if(lyr_embedded_read(f, g_lbuf, sizeof g_lbuf, &synced) > 0) g_lkind = synced ? 2 : 1;
            fclose(f);
        }
        sd_io_end();
    }

    /* 3) fall back to online (lrclib) by track + artist, unless the user turned it off */
    if(!g_lbuf[0] && job->title[0] && job->online){
        pthread_mutex_lock(&g_ly_mu); g_lphase = 1; pthread_mutex_unlock(&g_ly_mu);   /* -> poll shows "Searching online..." */
        if(job->artist[0])
            snprintf(cmd, sizeof cmd,
                "wget -qO- -T 12 'https://lrclib.net/api/search?track_name=%s&artist_name=%s' 2>/dev/null",
                job->title, job->artist);
        else
            snprintf(cmd, sizeof cmd,
                "wget -qO- -T 12 'https://lrclib.net/api/search?track_name=%s' 2>/dev/null", job->title);
        FILE *f = popen(cmd, "r");
        if(f){ int n = fread(resp, 1, sizeof(resp)-1, f); resp[n>0?n:0] = 0; pclose(f); }
        else fprintf(stderr, "lyrics popen failed: %s\n", strerror(errno));
        g_lkind = 2;
        if(!json_lyrics(resp, "syncedLyrics", g_lbuf, sizeof g_lbuf)){
            g_lkind = 1;
            json_lyrics(resp, "plainLyrics", g_lbuf, sizeof g_lbuf);
        }
        if(f && !g_lbuf[0]) fprintf(stderr, "lyrics: lrclib returned no lyrics for '%s'\n", job->title);
    }

    if(!g_lbuf[0]){
        snprintf(g_lbuf, sizeof g_lbuf, "No lyrics found.");
        g_lkind = 0;
    }

    /* publish atomically (pairs with lyrics_poll's lock so g_lbuf is visible) */
    pthread_mutex_lock(&g_ly_mu);
    g_done_req = job->req;
    g_linflight = 0;
    g_lready = 1;
    pthread_mutex_unlock(&g_ly_mu);
    free(job);
    return NULL;
}

/* Claim the single fetch slot and launch a worker with a snapshot of the current
 * request (req + query + path), all read on the main thread before pthread_create. */
static void start_fetch(void)
{
    int start = 0;
    pthread_mutex_lock(&g_ly_mu);
    if(!g_linflight){ g_linflight = 1; g_lready = 0; g_lphase = 0; start = 1; }
    pthread_mutex_unlock(&g_ly_mu);
    if(!start) return;
    ly_job_t *job = malloc(sizeof *job);
    if(!job){
        pthread_mutex_lock(&g_ly_mu); g_linflight = 0; pthread_mutex_unlock(&g_ly_mu);
        if(g_text) lv_label_set_text(g_text, "Lyrics unavailable.");   /* don't leave "Fetching..." stuck */
        return;
    }
    job->req = g_req;
    job->online = cfg_get_int("online_lyrics", 1);   /* Settings > Display > Online Lyrics (on unless turned off) */
    snprintf(job->title,  sizeof job->title,  "%s", g_q_title);
    snprintf(job->artist, sizeof job->artist, "%s", g_q_artist);
    snprintf(job->path,   sizeof job->path,   "%s", g_path);
    pthread_t th;
    if(pthread_create(&th, NULL, lyrics_thread, job) == 0) pthread_detach(th);
    else {
        free(job);
        pthread_mutex_lock(&g_ly_mu); g_linflight = 0; pthread_mutex_unlock(&g_ly_mu);
        if(g_text) lv_label_set_text(g_text, "Lyrics unavailable.");
    }
}

/* ---- Zune (Figma 12 + 67): the line being sung, huge and centred, the next one small under it; untimed lyrics as an
 * even list under "LYRICS · NOT SYNCED"; no lyrics as one quiet line and a hint. The rim keeps the track progress. ---- */
static int g_zcur_i = -3;
static void z_show(lv_obj_t *o, int on){ if(o){ if(on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN); } }
static void z_top(const char *t, int caps){
    if(!g_ztop) return;
    lv_label_set_text(g_ztop, t);
    lv_obj_set_style_text_font(g_ztop, caps ? TF(UI_10) : TF(UI_12), 0);
    lv_obj_set_style_text_letter_space(g_ztop, caps ? 2 : 0, 0);
}
static void z_message(const char *big, const char *hint){        /* no lyrics, nothing playing, still looking */
    z_show(g_scroll, 0); z_show(g_zcur, 0); z_show(g_znext, 0);
    z_show(g_zbig, 1); z_show(g_zhint, hint && hint[0]);
    lv_label_set_text(g_zbig, big); if(hint) lv_label_set_text(g_zhint, hint);
    z_top(g_identity_title[0] ? g_identity_title : "", 0);
}
static void z_message_for(const char *txt){                       /* the stock status strings, in Zune's words */
    if(!txt) txt = "";
    if(strstr(txt, "No lyrics"))          z_message("no lyrics", "put a .lrc file with the same name next to the song");
    else if(strstr(txt, "unavailable"))   z_message("no lyrics", "couldn't load lyrics for this song");
    else if(strstr(txt, "No track"))      z_message("nothing playing", "");
    else if(strstr(txt, "online"))        z_message("lyrics", "searching online\xE2\x80\xA6");
    else                                  z_message("lyrics", "checking the sd card\xE2\x80\xA6");
}
static void z_plain(const char *txt){                             /* untimed: the whole text as one even list */
    z_show(g_zbig, 0); z_show(g_zhint, 0); z_show(g_zcur, 0); z_show(g_znext, 0); z_show(g_scroll, 1);
    if(g_text){ lv_obj_remove_flag(g_text, LV_OBJ_FLAG_HIDDEN); lv_label_set_text(g_text, txt); }
    if(g_scroll) lv_obj_scroll_to_y(g_scroll, 0, LV_ANIM_OFF);
    z_top("LYRICS \xC2\xB7 NOT SYNCED", 1);
}
/* block-text mode: one label (status messages, untimed lyrics) */
static int g_zplain_next;                                          /* Zune: the next show_text is lyrics, not a message */
static void show_text(const char *txt)
{
    lyrics_fx_unbind(&g_word_fx);
    if(g_scroll) lv_anim_delete(g_scroll, lyric_scroll_exec);
    g_word_count = 0; g_seek_until = 0; g_pressed_line = -1; g_follow_position = -1;
    g_reading_touch = 0; g_clock_valid = 0;
    if(!g_scroll){ g_nll = 0; g_lcur = -2; return; }
    if(th_zune()){ int plain = g_zplain_next; g_zplain_next = 0; g_nll = 0; g_lcur = -2; g_zcur_i = -3;
                   if(plain) z_plain(txt); else z_message_for(txt); return; }
    g_nll = 0; g_lcur = -2;
    if(g_lines_box){ lv_obj_clean(g_lines_box); lv_obj_add_flag(g_lines_box, LV_OBJ_FLAG_HIDDEN); }
    if(g_text){ lv_obj_remove_flag(g_text, LV_OBJ_FLAG_HIDDEN); lv_label_set_text(g_text, txt); }
    if(g_scroll) lv_obj_scroll_to_y(g_scroll, 0, LV_ANIM_OFF);
}

/* timed mode: one label per line in g_ll; returns 0 (and leaves the view alone) if the text isn't timed */
static int show_timed(const char *lrc)
{
    lyrics_fx_unbind(&g_word_fx);
    if(g_scroll) lv_anim_delete(g_scroll, lyric_scroll_exec);
    int synced = 0;
    int n = lrc_parse_words(lrc, g_ll, LY_MAXL, g_ltext, sizeof g_ltext,
                           g_lplain, sizeof g_lplain, &synced, g_words, LY_MAX_WORDS, &g_word_count);
    if(!synced || !g_lines_box){
        g_zplain_next = g_lplain[0] != 0;
        show_text(g_lplain[0] ? g_lplain : "No lyrics found.");
        return 0;
    }
    if(th_zune()){
        /* The same continuous lyric list, in Zune's large typography. Keeping
         * rows alive makes both forward and backward line taps predictable. */
        z_show(g_scroll, 1); z_show(g_zbig, 0); z_show(g_zhint, 0);
        z_show(g_zcur, 0); z_show(g_znext, 0); z_top("", 0);
    }
    lv_obj_clean(g_lines_box);
    for(int i = 0; i < n; i++){
        lv_obj_t *l = lv_label_create(g_lines_box);
        lv_obj_set_width(l, 280);
        lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(l, th_zune() ? zune_font(&font_zune_light_32) :
                                  th_braun_family() ? br_font(20,0) : th_classic() ? TF(UI_24) : ui_font_cjk(20), 0);
        lv_obj_set_style_text_line_space(l, 6, 0);
        lv_obj_set_style_text_color(l, TC(TEXT_MUTED), 0);
        lv_obj_set_style_text_opa(l, 170, 0);
        lv_label_set_text(l, g_ltext[g_ll[i].text] ? g_ltext + g_ll[i].text : " ");   /* an empty timed line is a pause */
        if(g_ltext[g_ll[i].text]){
            lv_obj_add_flag(l, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(l, lyric_tap, LV_EVENT_ALL, (void *)(intptr_t)i);
            lv_obj_set_ext_click_area(l, 4);
        }
    }
    g_nll = n; g_lcur = -2; g_holding = 0; g_scroll_target = -1; g_follow_position = -1;
    g_pressed_line = -1;
    if(g_text) lv_obj_add_flag(g_text, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(g_lines_box, LV_OBJ_FLAG_HIDDEN);
    lv_obj_update_layout(g_scroll);
    lyric_scroll_exec(g_scroll, 0);
    if(g_motion_timer){ lv_timer_resume(g_motion_timer); lv_timer_set_period(g_motion_timer, 40); }
    return 1;
}

/* Interpolate changed position reports, preserving forward motion across
 * quantized repeats. Explicit seeks and decreasing reports still rebase. */
static long lyrics_position(void)
{
    track_state_t st; ipc_get_state(&st);
    uint32_t now = lv_tick_get();
    int playing = ui_is_playing();
    int ignore_report = 0;
    if(g_seek_until){
        if((int32_t)(now - g_seek_until) >= 0 ||
           (st.pos_seq != g_seek_sequence && labs(st.position_ms - g_seek_target) < 750)) {
            g_seek_until = 0;
            g_clock_valid = 0;
        } else ignore_report = 1;
    }
    /* A track-change message can precede its first position report. */
    if(st.path_seq && st.pos_seq <= st.path_seq) st.position_ms = 0;
    long est = g_pos_base;
    if(g_clock_valid && g_clock_playing){
        uint32_t d = now - g_pos_tick; est += d > 1500 ? 1500 : (long)d;
    }
    if(g_clock_valid && !ignore_report && est > g_reported_position && est - g_reported_position > 1500)
        est = g_reported_position + 1500;
    if(!g_clock_valid || (!ignore_report && st.position_ms != g_reported_position)){
        long reported = st.position_ms;
        /* A forward quantized report can trail our interpolation. Do not
         * revisit the preceding lyric for that ordinary clock correction. */
        if(g_clock_valid && playing && g_clock_playing && reported >= g_reported_position &&
           reported < est && est - reported <= 1600) reported = est;
        g_reported_position = st.position_ms;
        g_pos_base = est = reported; g_pos_tick = now; g_clock_valid = 1;
    } else if(playing != g_clock_playing){
        /* Freeze the displayed position on pause, and resume from it. */
        g_pos_base = est; g_pos_tick = now;
    }
    g_clock_playing = playing;
    if(st.duration_ms > 0 && est > st.duration_ms) est = st.duration_ms;
    return est;
}

/* highlight the line at the playback position and keep it in the middle */
static void lyrics_follow(void)
{
    if(!g_nll || !g_lines_box) return;
    int progressive = cfg_get_int("lyrics_progressive", 1) != 0;
    int estimate = cfg_get_int("lyrics_estimate_words", 1) != 0;
    track_state_t st; ipc_get_state(&st);
    long duration = st.have_track && st.path_seq == g_identity_path_seq &&
        !strcmp(st.path, g_path) && !strcmp(st.title, g_identity_title) ? st.duration_ms : 0;
    if(duration < 0) duration = 0;
    int end_changed = progressive && estimate && g_lcur >= 0 && g_lcur == g_nll - 1 &&
        !g_ll[g_lcur].word_count && !g_ll[g_lcur].has_word_timing && duration != g_estimate_duration;
    if(progressive != g_progressive || estimate != g_estimate_words || end_changed){
        lyrics_fx_unbind(&g_word_fx);
        if(g_lcur >= 0 && g_lcur < g_nll)
            lyric_emphasis(lv_obj_get_child(g_lines_box, g_lcur), 0, 0);
        lv_anim_delete(g_scroll, lyric_scroll_exec);
        g_lcur = -2; g_scroll_target = -1; g_follow_position = -1;
        g_progressive = progressive;
        g_estimate_words = estimate;
    }
    g_estimate_duration = duration;
    long position = lyrics_position();
    int idx = lrc_index_at(g_ll, g_nll, position);
    int discontinuity = g_follow_position >= 0 && (position < g_follow_position - 200 || position - g_follow_position > 1800);
    int changed = idx != g_lcur;
    int sequential = progressive && changed && g_lcur >= 0 && idx == g_lcur + 1 && !discontinuity;
    g_follow_position = position;
    if(changed){
        lyrics_fx_unbind(&g_word_fx);
        if(g_lcur >= 0 && g_lcur < g_nll) lyric_emphasis(lv_obj_get_child(g_lines_box, g_lcur), 0, sequential);
        g_lcur = idx;
        if(idx >= 0){
            lv_obj_t *label = lv_obj_get_child(g_lines_box, idx);
            lyric_emphasis(label, 1, sequential);
            lrc_line_t *line = &g_ll[idx];
            const lrc_word_t *words = NULL;
            int count = 0;
            if(line->word_count > 0 && line->word_first >= 0 && line->word_first + line->word_count <= g_word_count){
                words = g_words + line->word_first; count = line->word_count;
            } else if(progressive && estimate && !line->word_count && !line->has_word_timing){
                long end = duration;
                for(int i = idx + 1; i < g_nll; i++) if(g_ll[i].ms > line->ms){ end = g_ll[i].ms; break; }
                if(duration > 0 && end > duration) end = duration;
                count = lrc_estimate_line_words(g_ltext + line->text, (int)sizeof g_ltext - line->text,
                    line->ms, end, g_estimated_words, LYR_WORDS_MAX);
                words = g_estimated_words;
            }
            if(progressive && count > 0 &&
               lyrics_fx_bind(&g_word_fx, label, words, count, TC(TEXT_LYRICS))){
                lv_anim_delete(label, lyric_fade_exec);
                lv_obj_set_style_text_color(label, TC(TEXT_MUTED), 0);
                lv_obj_set_style_text_opa(label, LV_OPA_COVER, 0);
            }
        }
    }
    lyrics_fx_update(&g_word_fx, position);
    if(g_lcur < 0) return;
    if(g_holding && (g_reading_touch || (int32_t)(lv_tick_get() - g_hold_until) < 0)) return;
    if(g_holding){ g_holding = 0; g_scroll_target = -1; }                   /* hold over: come back to the line */
    if(!changed && g_scroll_target >= 0 && !discontinuity) return;
    lv_obj_update_layout(g_scroll);
    lv_obj_t *l = lv_obj_get_child(g_lines_box, g_lcur);
    lv_area_t la, sa;                               /* on-screen: how far the line's middle is from the view's middle */
    lv_obj_get_coords(l, &la); lv_obj_get_coords(g_scroll, &sa);
    int32_t y = lv_obj_get_scroll_y(g_scroll) + (la.y1 + la.y2) / 2 - (sa.y1 + sa.y2) / 2;
    if(y < 0) y = 0;
    int32_t max_y = lv_obj_get_scroll_y(g_scroll) + lv_obj_get_scroll_bottom(g_scroll);
    if(y > max_y) y = max_y > 0 ? max_y : 0;
    if(y != g_scroll_target || discontinuity){
        g_scroll_target = y;
        lv_anim_delete(g_scroll, lyric_scroll_exec);
        if(!screen_get_anim() || discontinuity || !sequential){ lyric_scroll_exec(g_scroll, y); return; }
        lv_anim_t a; lv_anim_init(&a);
        lv_anim_set_var(&a, g_scroll); lv_anim_set_exec_cb(&a, lyric_scroll_exec);
        lv_anim_set_values(&a, lv_obj_get_scroll_y(g_scroll), y);
        lv_anim_set_duration(&a, 480); lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
        lv_anim_start(&a);
    }
}

static void scroll_cb(lv_event_t *e)
{
    lv_event_code_t c = lv_event_get_code(e);
    if(!g_auto_scroll && (c == LV_EVENT_PRESSED || (c == LV_EVENT_SCROLL_BEGIN && lv_indev_active()))) {
        g_reading_touch = 1;
        g_holding = 1; g_hold_until = lv_tick_get() + 4000;
        lv_anim_delete(g_scroll, lyric_scroll_exec);
    } else if(c == LV_EVENT_RELEASED || c == LV_EVENT_PRESS_LOST || c == LV_EVENT_SCROLL_END){
        g_reading_touch = 0; g_hold_until = lv_tick_get() + 4000;
    }
}

static void lyric_tap(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    int index = (int)(intptr_t)lv_event_get_user_data(e);
    lv_indev_t *input = lv_indev_active();
    if(code == LV_EVENT_PRESSED){
        track_state_t st; ipc_get_state(&st);
        g_pressed_line = index; g_pressed_request = g_req; g_press_moved = 0;
        g_pressed_path_seq = st.path_seq;
        g_reading_touch = 1;
        snprintf(g_pressed_path, sizeof g_pressed_path, "%s", st.path);
        snprintf(g_pressed_title, sizeof g_pressed_title, "%s", st.title);
        if(input) lv_indev_get_point(input, &g_pressed_point);
        g_holding = 1; g_hold_until = lv_tick_get() + 4000;
        lv_anim_delete(g_scroll, lyric_scroll_exec);
    } else if(code == LV_EVENT_PRESSING && input){
        lv_point_t p; lv_indev_get_point(input, &p);
        if(abs(p.x - g_pressed_point.x) > 8 || abs(p.y - g_pressed_point.y) > 8) g_press_moved = 1;
    } else if(code == LV_EVENT_PRESS_LOST || code == LV_EVENT_LONG_PRESSED){
        g_press_moved = 1;
        if(code == LV_EVENT_PRESS_LOST){ g_reading_touch = 0; g_hold_until = lv_tick_get() + 4000; }
    } else if(code == LV_EVENT_RELEASED){
        g_reading_touch = 0; g_hold_until = lv_tick_get() + 4000;
    } else if(code == LV_EVENT_SHORT_CLICKED){
        if(screen_current() != SCR_LYRICS || g_press_moved || screen_press_was_long() ||
           index != g_pressed_line || g_pressed_request != g_req || index < 0 || index >= g_nll) return;
        g_pressed_line = -1;
        track_state_t st; ipc_get_state(&st);
        char path[sizeof g_pressed_path]; snprintf(path, sizeof path, "%s", st.path);
        if(st.path_seq != g_pressed_path_seq || strcmp(path, g_pressed_path) || strcmp(st.title, g_pressed_title) ||
           strcmp(path, g_path) || st.duration_ms <= 0) return;
        long target = g_ll[index].ms;
        if(target >= st.duration_ms) target = st.duration_ms - 1;
        if(target < 0) target = 0;
        if(ui_seek_to(target) < 0){ ui_toast("Couldn't seek - try again"); return; }
        ui_book_user_seeked(target);
        g_seek_target = target; g_seek_sequence = st.pos_seq;
        g_pos_tick = lv_tick_get(); g_seek_until = g_pos_tick + 2500;
        g_pos_base = target; g_reported_position = st.position_ms;
        g_clock_valid = 1; g_clock_playing = ui_is_playing();
        g_holding = 0; g_reading_touch = 0; g_scroll_target = -1;
        lyrics_follow();
    }
}

static void request_current(int force)
{
    /* resolve title / artist / path: live state, else resume track from DB */
    track_state_t st; ipc_get_state(&st);
    char title[200] = "", artist[200] = "";
    char path[256]="";
    if(st.path[0]) snprintf(path, sizeof path, "%s", st.path);
    if(st.title[0])  snprintf(title,  sizeof title,  "%s", st.title);
    if(st.artist[0]) snprintf(artist, sizeof artist, "%s", st.artist);
    if(!title[0]){
        mdb_song_t cur; int pm, pl;
        if(mdb_current_play(&cur, &pm, &pl)){
            snprintf(title,  sizeof title,  "%s", cur.title);
            snprintf(artist, sizeof artist, "%s", cur.artist);
            if(!path[0]) mdb_song_path(cur.id, path, sizeof path);
        }
    }
    int online=cfg_get_int("online_lyrics",1);
    if(!force && !strcmp(path,g_path) && !strcmp(title,g_identity_title) &&
       !strcmp(artist,g_identity_artist) && online==g_identity_online && st.path_seq==g_identity_path_seq) return;
    g_req++; g_irev++; g_cached_n=0; g_cached_req=0;
    snprintf(g_path,sizeof g_path,"%s",path);
    snprintf(g_identity_title,sizeof g_identity_title,"%s",title);
    snprintf(g_identity_artist,sizeof g_identity_artist,"%s",artist);
    g_identity_online=online;
    g_identity_path_seq=st.path_seq;
    if(screen_current()==SCR_LYRICS) show_text("Checking SD card...");
    urlenc(title,  g_q_title,  sizeof g_q_title);
    urlenc(artist, g_q_artist, sizeof g_q_artist);

    if(g_path[0] || g_q_title[0]) start_fetch();
    else { g_cached_req=g_req; g_irev++; }
}
void lyrics_open(void){
    request_current(1);
    show_text(g_path[0] || g_q_title[0] ? "Checking SD card..." : "No track playing.");
    screen_show(SCR_LYRICS);
}
void lyrics_invalidate_current(void){ g_identity_online=-1; }
int lyrics_immersive_load(lyr_line_t *out,int cap,unsigned *revision){
    request_current(0);
    if(revision) *revision=g_irev;
    if(g_cached_req!=g_req) return -1;
    int n=g_cached_n < cap ? g_cached_n : cap;
    if(n>0 && out) memcpy(out,g_cached_lines,(size_t)n*sizeof *out);
    return n;
}


void lyrics_poll(lv_timer_t *t)
{
    (void)t;
    /* If the track changed while the lyrics screen is open, reload for the new track
     * (g_path is the track we last fetched for). lyrics_open() re-resolves the live
     * track, bumps g_req to supersede any in-flight fetch, and updates g_path so this
     * fires once per change. Only while SCR_LYRICS is visible - no wasted fetches. */
    if(screen_current() == SCR_LYRICS){
        track_state_t st; ipc_get_state(&st);
        request_current(0);
    }
    int ui_main_is_idle(void);
    int on = screen_current() == SCR_LYRICS && !ui_main_is_idle();
    if(t) lv_timer_set_period(t, (on && g_nll) ? 200 : 500);   /* follow timed lines closely only while they show */
    if(on && g_nll && g_motion_timer){ lv_timer_resume(g_motion_timer); lv_timer_set_period(g_motion_timer, 40); }
    if(on) lyrics_follow();
    int ready = 0, inflight, phase; unsigned done = 0;
    pthread_mutex_lock(&g_ly_mu);
    if(g_lready){ g_lready = 0; ready = 1; done = g_done_req; }
    inflight = g_linflight; phase = g_lphase;
    pthread_mutex_unlock(&g_ly_mu);
    if(!ready){
        /* still fetching: reflect the phase so the wait isn't a silent "Checking SD card..." */
        if(inflight && th_zune() && g_scroll) z_message_for(phase ? "Searching online..." : "Checking SD card...");
        else if(inflight && g_text) lv_label_set_text(g_text, phase ? "Searching online..." : "Checking SD card...");
        return;
    }
    if(done == g_req){
        g_cached_n=g_lkind==2 ? timed_parse(g_lbuf,g_cached_lines,240) : 0;
        g_cached_req=g_req; g_irev++;
        /* result is for the current track; g_lbuf is visible (published under the lock)
         * and no worker is running, so reading it here is race-free. */
        if(g_lkind == 2){ if(show_timed(g_lbuf) && on) lyrics_follow(); }
        else { g_zplain_next = g_lkind == 1; show_text(g_lbuf); }
    } else if(g_path[0] || g_q_title[0]){
        /* finished fetch was for a superseded track - fetch the current one
         * (start_fetch no-ops if a newer fetch is already running). */
        (void)inflight;
        start_fetch();
    }
}

void lyrics_create(lv_obj_t *root)
{
    lv_obj_add_event_cb(root, lyric_root_deleted, LV_EVENT_DELETE, NULL);
    if(!g_motion_timer) g_motion_timer = lv_timer_create(lyric_motion_tick, 500, NULL);
    else lv_timer_resume(g_motion_timer);
    lv_obj_set_style_bg_color(root, TC(CANVAS), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);

    if(!th_zune()) ui_header(root, "Lyrics");   /* shared standard header (Zune: none - the lyrics own the screen) */

    g_scroll = lv_obj_create(root);
    lv_obj_remove_style_all(g_scroll);
    lv_obj_set_pos(g_scroll, 40, 76); lv_obj_set_size(g_scroll, 280, 250);
    lv_obj_set_style_pad_bottom(g_scroll, 44, 0);   /* last lyric lines scroll clear of the round bezel */
    lv_obj_set_style_bg_opa(g_scroll, LV_OPA_TRANSP, 0);
    lv_obj_set_scroll_dir(g_scroll, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(g_scroll, LV_SCROLLBAR_MODE_OFF);

    g_text = lv_label_create(g_scroll);
    lv_obj_set_width(g_text, 280);
    lv_label_set_long_mode(g_text, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(g_text, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(g_text, th_classic() ? TF(UI_24) : ui_font_cjk(20), 0);   /* larger; CJK lyrics render via Source Han Sans fallback */
    lv_obj_set_style_text_color(g_text, TC(TEXT_LYRICS), 0);
    lv_obj_set_style_text_line_space(g_text, 6, 0);
    lv_label_set_text(g_text, "");
    if(th_braun_family()){                                              /* Braun: the lyrics on the off-white segment, dark Inter */
        br_face(root);
        lv_obj_move_to_index(br_segment(root, 68), 1);
        lv_obj_set_style_text_font(g_text, br_font(20, 0), 0);
        lv_obj_set_style_text_color(g_text, TC(TEXT_PRIMARY), 0);
        lv_obj_set_style_text_line_space(g_text, 8, 0);
    }
    g_lines_box = lv_obj_create(g_scroll);
    lv_obj_remove_style_all(g_lines_box);
    lv_obj_set_width(g_lines_box,280); lv_obj_set_height(g_lines_box,LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(g_lines_box,LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(g_lines_box,th_zune() ? 20 : 16,0);
    lv_obj_set_style_pad_top(g_lines_box,100,0);
    lv_obj_set_style_pad_bottom(g_lines_box,110,0);
    lv_obj_remove_flag(g_lines_box,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(g_lines_box,LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(g_scroll,scroll_cb,LV_EVENT_PRESSED,NULL);
    lv_obj_add_event_cb(g_scroll,scroll_cb,LV_EVENT_SCROLL_BEGIN,NULL);
    lv_obj_add_event_cb(g_scroll,scroll_cb,LV_EVENT_RELEASED,NULL);
    lv_obj_add_event_cb(g_scroll,scroll_cb,LV_EVENT_PRESS_LOST,NULL);
    lv_obj_add_event_cb(g_scroll,scroll_cb,LV_EVENT_SCROLL_END,NULL);
    if(th_zune()){
        lv_obj_set_pos(g_scroll, 30, 56); lv_obj_set_size(g_scroll, 300, 270);       /* the untimed list */
        lv_obj_set_x(g_lines_box, 10);
        lv_obj_set_style_pad_top(g_scroll, 22, 0); lv_obj_set_style_pad_bottom(g_scroll, 80, 0);
        lv_obj_set_width(g_text, 300);
        lv_obj_set_style_text_font(g_text, zune_font(&font_zune_light_18), 0);
        lv_obj_set_style_text_color(g_text, TC(TEXT_PRIMARY), 0);
        lv_obj_set_style_text_line_space(g_text, 12, 0);
        for(int k = 0; k < 2; k++){                                                  /* fades over the list's ends */
            lv_obj_t *f = lv_obj_create(root); lv_obj_remove_style_all(f); kit_keep(f);
            lv_obj_set_size(f, 360, k ? 70 : 90); lv_obj_align(f, k ? LV_ALIGN_BOTTOM_MID : LV_ALIGN_TOP_MID, 0, 0);
            lv_obj_set_style_bg_color(f, TC(CANVAS), 0); lv_obj_set_style_bg_grad_color(f, TC(CANVAS), 0);
            lv_obj_set_style_bg_grad_dir(f, LV_GRAD_DIR_VER, 0); lv_obj_set_style_bg_opa(f, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_main_opa(f, k ? LV_OPA_TRANSP : LV_OPA_COVER, 0); lv_obj_set_style_bg_grad_opa(f, k ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
            lv_obj_remove_flag(f, LV_OBJ_FLAG_CLICKABLE);
        }
        zune_rim(root, SCR_LYRICS, 1);                                               /* above the fades */
        g_ztop = lv_label_create(root); lv_label_set_text(g_ztop, "");
        lv_obj_set_width(g_ztop, 220); lv_label_set_long_mode(g_ztop, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(g_ztop, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(g_ztop, TC(TEXT_MUTED), 0); lv_obj_align(g_ztop, LV_ALIGN_TOP_MID, 0, 38);
        g_zcur = lv_label_create(root); lv_label_set_text(g_zcur, "");
        lv_obj_set_width(g_zcur, 270); lv_label_set_long_mode(g_zcur, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(g_zcur, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_letter_space(g_zcur, -1, 0);
        lv_obj_set_style_text_color(g_zcur, TC(TEXT_PRIMARY), 0);
        g_znext = lv_label_create(root); lv_label_set_text(g_znext, "");
        lv_obj_set_width(g_znext, 280); lv_label_set_long_mode(g_znext, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(g_znext, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(g_znext, zune_font(TF(UI_14)), 0); lv_obj_set_style_text_color(g_znext, TC(TEXT_MUTED), 0);
        g_zbig = lv_label_create(root); lv_label_set_text(g_zbig, "");
        lv_obj_set_style_text_font(g_zbig, zune_font(&font_zune_light_32), 0); lv_obj_set_style_text_color(g_zbig, TC(TEXT_PRIMARY), 0);
        lv_obj_align(g_zbig, LV_ALIGN_TOP_MID, 0, 138);
        g_zhint = lv_label_create(root); lv_label_set_text(g_zhint, "");
        lv_obj_set_width(g_zhint, 220); lv_label_set_long_mode(g_zhint, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(g_zhint, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(g_zhint, TF(UI_12), 0); lv_obj_set_style_text_color(g_zhint, TC(TEXT_MUTED), 0);
        lv_obj_align(g_zhint, LV_ALIGN_TOP_MID, 0, 188);
        z_show(g_zcur, 0); z_show(g_znext, 0); z_show(g_scroll, 0);
        lv_obj_t *own[] = { g_scroll, g_text, g_ztop, g_zcur, g_znext, g_zbig, g_zhint };
        for(unsigned i = 0; i < sizeof own / sizeof own[0]; i++) kit_keep(own[i]);
    }
}

/* B's parser handles offsets, enhanced tags and stable repeated timestamps for A's three-line renderer. */
static int timed_parse(const char *raw,lyr_line_t *out,int cap){
    lrc_line_t lines[400]; char text[16384]; int synced=0;
    lrc_word_t words[LY_MAX_WORDS]; int word_count = 0;
    if(!raw || !out || cap<=0) return 0;
    int n=lrc_parse_words(raw,lines,400,text,sizeof text,NULL,0,&synced,
                          words,LY_MAX_WORDS,&word_count);
    if(!synced) return 0;
    if(n>cap) n=cap;
    for(int i=0;i<n;i++){
        memset(&out[i], 0, sizeof out[i]);
        out[i].ms=lines[i].ms;
        const char *s=text+lines[i].text;
        size_t full_len=strlen(s),k=full_len; if(k>=sizeof out[i].text) k=sizeof out[i].text-1;
        /* A truncated prefix ends at a complete UTF-8 codepoint. */
        while(k>0 && ((unsigned char)s[k]&0xC0)==0x80) k--;
        memcpy(out[i].text,s,k); out[i].text[k]=0;
        out[i].estimate_blocked=lines[i].has_word_timing || full_len>k;
        int first = lines[i].word_first, count = lines[i].word_count;
        if(count > 0 && count <= LYR_WORDS_MAX && first >= 0 && first + count <= word_count){
            int valid = 1;
            for(int w = 0; w < count; w++)
                if(words[first+w].end_byte > (int)k) valid = 0;
            if(valid){
                out[i].word_count = count;
                memcpy(out[i].words, words + first, (size_t)count * sizeof words[0]);
            }
        }
    }
    return n;
}
int lyrics_timed_load(const char *path,lyr_line_t *out,int cap){
    if(!path || !path[0] || !out || cap<=0 || !sd_io_begin()) return 0;
    char raw[16384]="", sidecar[600]; derive_lrc(path,sidecar,sizeof sidecar);
    FILE *f=fopen(sidecar,"rb");
    if(f){ read_lrc(f,raw,sizeof raw); fclose(f); }
    int n=raw[0]?timed_parse(raw,out,cap):0;
    if(!n){                                       /* no sidecar, or an unsynced one: the song's own synced lyrics */
        raw[0]=0; f=fopen(path,"rb");
        if(f){ int synced=0; lyr_embedded_read(f,raw,sizeof raw,&synced); fclose(f); }
        n=timed_parse(raw,out,cap);
    }
    sd_io_end();
    return n;
}
