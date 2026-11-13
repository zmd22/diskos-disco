#include "simple_theme.h"
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Listening Stats (built in, opened from its landing page like Boomshine).
 *
 * Recording: every song that really plays leaves one row in the diskOS-owned PLAY_LOG table (musicdb.c writes it on
 * its history worker): when it started, how long it actually played, and its title / artist / album / album artist
 * as the library has them. A song counts once it has played for 30 seconds, or almost all of a shorter song. Time is
 * measured from the song's own position while it plays (pause and seeking don't add any). Audiobooks are left out.
 *
 * The screen: one list that scrolls like the Library (rows curve with the round screen, position dots on the left
 * rim): the period at a glance, top artists, top albums, the song on repeat and the top songs, and a 24-hour
 * listening clock round the rim. Week / Month / Year at the top, arrows step back through earlier periods. Data is
 * read only when the screen opens (Show me), never while the landing page is merely passed in the menu. */
#include "stats.h"
#include "screens.h"
#include "theme.h"
#include "config.h"
#include "musicdb.h"
#include "artcache.h"
#include "curvelist.h"
#include "art.h"
#include "sqlite3.h"
#include <ctype.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#ifndef STATS_DB_PATH
#define STATS_DB_PATH "/usr/data/fiio/db/song.db"
#endif
#define COUNT_MS 30000                     /* a play counts after 30 s ... */

/* ================================ recording ================================ */
static char r_path[256], r_title[160], r_artist[160], r_album[160];
static long r_dur, r_last_pos;
static uint32_t r_ms;
static long long r_last_ms;                          /* monotonic ms at the last position seen (0: none yet) */
static long long r_at;
static int r_active;

static void rec_finish(void){
    if(!r_active) return;
    r_active = 0;
    int counts = r_ms >= COUNT_MS || (r_dur > 0 && r_ms >= 5000 && (long)r_ms >= r_dur * 9 / 10);   /* ... or most of a short one */
    long long now = (long long)time(NULL);
    if(!counts || now < 1600000000LL) return;                                    /* the clock isn't set: nothing to date it by */
    long long at = r_at >= 1600000000LL ? r_at : now - r_ms / 1000;
    mdb_log_play(r_path, r_title, r_artist, r_album, at, (int)r_ms);
}
static void rec_start(const track_state_t *st){
    snprintf(r_path, sizeof r_path, "%s", st->path);
    snprintf(r_title, sizeof r_title, "%s", st->title);
    snprintf(r_artist, sizeof r_artist, "%s", st->artist);
    snprintf(r_album, sizeof r_album, "%s", st->album);
    r_dur = st->duration_ms; r_ms = 0; r_last_ms = 0; r_last_pos = st->position_ms;
    r_at = (long long)time(NULL);
    r_active = !mdb_is_book_path(st->path);                                      /* audiobooks have their own progress */
}
static long long mono_ms(void){ struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (long long)t.tv_sec * 1000 + t.tv_nsec / 1000000; }
/* Listening time is how far the song's position moved while it played, never more than the time that really
 * passed: a seek forward doesn't count, and music that played on while an app had the screen (this loop waits
 * then) still does. */
void stats_tick(const track_state_t *st, int playing){
    int have = st && st->have_track && st->path[0];
    if(!have){ rec_finish(); r_path[0] = 0; return; }
    if(strcmp(st->path, r_path)) { rec_finish(); rec_start(st); }
    else if(st->position_ms < 5000 && st->position_ms + 3000 < r_last_pos){
        rec_finish(); rec_start(st);   /* back to its start (Repeat One, a restart, a seek): a new attempt; rec_finish decides
                                        * whether the one before counts, so short tries never add up into one play */
    }
    long long now = mono_ms();
    if(!playing){ r_last_pos = st->position_ms; r_last_ms = now; }              /* paused: a seek meanwhile never counts */
    else if(st->position_ms != r_last_pos || !r_last_ms){
        long d = st->position_ms - r_last_pos;
        if(playing && r_last_ms && d > 0 && d <= now - r_last_ms + 1500) r_ms += (uint32_t)d;
        r_last_pos = st->position_ms; r_last_ms = now;
    }
    if(st->duration_ms > 0) r_dur = st->duration_ms;
}
void stats_flush(void){ rec_finish(); r_path[0] = 0; }

#ifndef STATS_RECORDER_ONLY                         /* (the recorder alone: its host test) */
/* Stats keeps its Disco typography and component geometry under Null; only
 * semantic colours follow the selected Null palette. */
static const lv_font_t *stats_font(theme_font_role_t role){
    return th_null()?theme_font_default(role):theme_font(role);
}
#undef TF
#define TF(role) stats_font(THEME_FONT_##role)
/* ================================ periods ================================ */
enum { P_WEEK, P_MONTH, P_YEAR };
static int g_ptype = P_MONTH, g_poff;                /* the period shown: its kind, and how many back from now */
static const char *const MON[12] = { "January", "February", "March", "April", "May", "June", "July", "August",
                                     "September", "October", "November", "December" };
static long long mk(int y, int m, int d){
    struct tm t; memset(&t, 0, sizeof t);
    t.tm_year = y - 1900; t.tm_mon = m; t.tm_mday = d; t.tm_isdst = -1;
    return (long long)mktime(&t);
}
/* [from, to) of the period `off` steps back (0 = the current one), as local calendar weeks (Monday first), months, years */
static void period_range(int type, int off, long long *from, long long *to, struct tm *first){
    time_t now = time(NULL); struct tm n; localtime_r(&now, &n);
    if(type == P_WEEK){
        int back = (n.tm_wday + 6) % 7;                                         /* days since Monday */
        *from = mk(n.tm_year + 1900, n.tm_mon, n.tm_mday - back + 7 * off);
        *to = mk(n.tm_year + 1900, n.tm_mon, n.tm_mday - back + 7 * (off + 1));
    } else if(type == P_MONTH){
        *from = mk(n.tm_year + 1900, n.tm_mon + off, 1);
        *to = mk(n.tm_year + 1900, n.tm_mon + off + 1, 1);
    } else {
        *from = mk(n.tm_year + 1900 + off, 0, 1);
        *to = mk(n.tm_year + 1900 + off + 1, 0, 1);
    }
    time_t f = (time_t)*from; localtime_r(&f, first);
}
static void period_name(int type, int off, char *out, size_t n, int short_form){
    long long a, b; struct tm f; period_range(type, off, &a, &b, &f);
    time_t now = time(NULL); struct tm t; localtime_r(&now, &t);
    if(type == P_WEEK){
        if(off == 0) snprintf(out, n, short_form ? "this week" : "This week");
        else if(off == -1) snprintf(out, n, short_form ? "last week" : "Last week");
        else snprintf(out, n, "Week of %d %.3s", f.tm_mday, MON[f.tm_mon]);
    } else if(type == P_MONTH){
        if(f.tm_year == t.tm_year) snprintf(out, n, "%s", MON[f.tm_mon]);
        else snprintf(out, n, "%s %d", MON[f.tm_mon], f.tm_year + 1900);
    } else snprintf(out, n, "%d", f.tm_year + 1900);
}

/* ================================ the numbers ================================ */
#define TOPN 15                                       /* the sub-views list 15; the main screen shows 5 */
typedef struct { char name[128], sub[128], path[256]; int plays; long long ms; } item_t;
typedef struct {
    int plays, songs, albums, artists, streak, empty, days; long long ms, prev_ms; int prev_known;
    long long best_day_ms; int best_day;                 /* the busiest day (julian day number) and its time */
    long long hour_ms[24];
    item_t art[TOPN], alb[TOPN], song[TOPN]; int nart, nalb, nsong;
} stats_t;
static stats_t g_s;
static long long g_first_at;                         /* the oldest row in the log (0: none) - how far back the arrows go */

static const char *col(sqlite3_stmt *st, int i){ const char *t = (const char *)sqlite3_column_text(st, i); return t ? t : ""; }
static int bind_range(sqlite3 *d, const char *sql, long long a, long long b, sqlite3_stmt **st){
    if(sqlite3_prepare_v2(d, sql, -1, st, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_int64(*st, 1, a); sqlite3_bind_int64(*st, 2, b);
    return 1;
}
static void trim(char *s){
    char *p = s; while(*p == ' ' || *p == '\t') p++;
    if(p != s) memmove(s, p, strlen(p) + 1);
    size_t n = strlen(s); while(n && (s[n - 1] == ' ' || s[n - 1] == '\t')) s[--n] = 0;
}
static int item_cmp(const void *x, const void *y){
    const item_t *a = x, *b = y;
    if(a->plays != b->plays) return b->plays - a->plays;
    return (a->ms < b->ms) - (a->ms > b->ms);
}
/* Top artists (rows per artist and song, most played first). By Artist (the Library's setting): each credit of "A, B" counts for both, as the Artists list shows
 * them; by Album Artist: the album artist (else the artist), unsplit. The cover is from the artist's busiest group. */
typedef struct { char name[128], path[256]; int plays, best; long long ms; } acc_t;
static void load_artists(sqlite3 *d, long long a, long long b){
    int by_album = mdb_artist_mode();
    const char *sql = by_album
        ? "SELECT COALESCE(NULLIF(AARTIST,''),ARTIST),COUNT(*),SUM(MS),PATH FROM PLAY_LOG WHERE AT>=?1 AND AT<?2 "
          "AND IFNULL(COALESCE(NULLIF(AARTIST,''),ARTIST),'')<>'' GROUP BY 1,PATH ORDER BY 2 DESC LIMIT 3000;"
        : "SELECT ARTIST,COUNT(*),SUM(MS),PATH FROM PLAY_LOG WHERE AT>=?1 AND AT<?2 AND IFNULL(ARTIST,'')<>'' "
          "GROUP BY 1,PATH ORDER BY 2 DESC LIMIT 3000;";   /* per song: the artist's picture is their most played song's cover */
    sqlite3_stmt *st; if(!bind_range(d, sql, a, b, &st)) return;
    enum { ASLOTS = 1024 };                                                      /* > 2 x cap: the table never fills */
    int cap = 400, n = 0; acc_t *acc = calloc((size_t)cap, sizeof *acc); int *slots = malloc(ASLOTS * sizeof *slots);
    if(!acc || !slots){ free(acc); free(slots); sqlite3_finalize(st); return; }
    for(int i = 0; i < ASLOTS; i++) slots[i] = -1;
    while(sqlite3_step(st) == SQLITE_ROW){
        char raw[256]; snprintf(raw, sizeof raw, "%s", col(st, 0));
        int plays = sqlite3_column_int(st, 1); long long ms = sqlite3_column_int64(st, 2);
        const char *path = col(st, 3);
        for(char *tok = raw, *next; tok; tok = next){
            next = by_album ? NULL : strpbrk(tok, ",;");
            if(next) *next++ = 0;
            char nm[128]; snprintf(nm, sizeof nm, "%.127s", tok); trim(nm);
            if(!nm[0]) continue;
            unsigned h = 2166136261u; for(const char *q = nm; *q; q++){ h ^= (unsigned char)tolower((unsigned char)*q); h *= 16777619u; }
            int slot = (int)(h & (ASLOTS - 1)), k = -1;                          /* case-blind lookup, no list scan */
            while(slots[slot] >= 0){ if(!strcasecmp(acc[slots[slot]].name, nm)){ k = slots[slot]; break; } slot = (slot + 1) & (ASLOTS - 1); }
            if(k < 0){
                if(n == cap) continue;                                           /* very long tails: the top is what shows */
                snprintf(acc[n].name, sizeof acc[n].name, "%s", nm); slots[slot] = n; k = n++;
            }
            acc[k].plays += plays; acc[k].ms += ms;
            if(plays > acc[k].best){ acc[k].best = plays; snprintf(acc[k].path, sizeof acc[k].path, "%s", path); }
        }
    }
    sqlite3_finalize(st);
    item_t *it = calloc((size_t)(n ? n : 1), sizeof *it);
    if(it){
        for(int i = 0; i < n; i++){ snprintf(it[i].name, sizeof it[i].name, "%s", acc[i].name);
            snprintf(it[i].path, sizeof it[i].path, "%s", acc[i].path); it[i].plays = acc[i].plays; it[i].ms = acc[i].ms; }
        qsort(it, (size_t)n, sizeof *it, item_cmp);
        g_s.nart = n < TOPN ? n : TOPN; g_s.artists = n;
        memcpy(g_s.art, it, (size_t)g_s.nart * sizeof *it);
        free(it);
    }
    free(acc); free(slots);
}
static int load_items(sqlite3 *d, const char *sql, long long a, long long b, item_t *out){
    sqlite3_stmt *st; if(!bind_range(d, sql, a, b, &st)) return 0;
    int n = 0;
    while(n < TOPN && sqlite3_step(st) == SQLITE_ROW){
        item_t *it = &out[n++];
        snprintf(it->name, sizeof it->name, "%s", col(st, 0));
        snprintf(it->sub, sizeof it->sub, "%s", col(st, 1));
        it->plays = sqlite3_column_int(st, 2); it->ms = sqlite3_column_int64(st, 3);
        snprintf(it->path, sizeof it->path, "%s", col(st, 4));
        if(!it->name[0]) snprintf(it->name, sizeof it->name, "Untitled");
    }
    sqlite3_finalize(st);
    return n;
}
static long long sum_ms(sqlite3 *d, long long a, long long b, int *plays, int *songs){
    sqlite3_stmt *st; long long ms = 0;
    if(!bind_range(d, "SELECT COUNT(*),IFNULL(SUM(MS),0),COUNT(DISTINCT PATH) FROM PLAY_LOG WHERE AT>=?1 AND AT<?2;", a, b, &st)) return -1;
    if(sqlite3_step(st) == SQLITE_ROW){
        if(plays) *plays = sqlite3_column_int(st, 0);
        ms = sqlite3_column_int64(st, 1);
        if(songs) *songs = sqlite3_column_int(st, 2);
    }
    sqlite3_finalize(st);
    return ms;
}
static void load_days_hours(sqlite3 *d, long long a, long long b){
    sqlite3_stmt *st;
    if(bind_range(d, "SELECT CAST(strftime('%H',AT,'unixepoch','localtime') AS INTEGER),SUM(MS) FROM PLAY_LOG "
                     "WHERE AT>=?1 AND AT<?2 GROUP BY 1;", a, b, &st)){
        while(sqlite3_step(st) == SQLITE_ROW){ int h = sqlite3_column_int(st, 0); if(h >= 0 && h < 24) g_s.hour_ms[h] += sqlite3_column_int64(st, 1); }
        sqlite3_finalize(st);
    }
    if(bind_range(d, "SELECT CAST(julianday(AT,'unixepoch','localtime','start of day') AS INTEGER),SUM(MS) FROM PLAY_LOG "
                     "WHERE AT>=?1 AND AT<?2 GROUP BY 1 ORDER BY 1;", a, b, &st)){
        int prev = -100, run = 0;                                               /* the longest run of days in a row */
        while(sqlite3_step(st) == SQLITE_ROW){
            int day = sqlite3_column_int(st, 0); long long ms = sqlite3_column_int64(st, 1);
            run = day == prev + 1 ? run + 1 : 1; prev = day; g_s.days++;
            if(run > g_s.streak) g_s.streak = run;
            if(ms > g_s.best_day_ms){ g_s.best_day_ms = ms; g_s.best_day = day; }   /* the busiest day */
        }
        sqlite3_finalize(st);
    }
}
static int count_albums(sqlite3 *d, long long a, long long b){
    sqlite3_stmt *st; int n = 0;
    if(!bind_range(d, "SELECT COUNT(*) FROM (SELECT 1 FROM PLAY_LOG WHERE AT>=?1 AND AT<?2 AND IFNULL(ALBUM,'')<>'' "
                      "GROUP BY ALBUM,COALESCE(NULLIF(AARTIST,''),ARTIST));", a, b, &st)) return 0;
    if(sqlite3_step(st) == SQLITE_ROW) n = sqlite3_column_int(st, 0);
    sqlite3_finalize(st);
    return n;
}
static long long g_ra, g_rb;                         /* the loaded period's [from, to) - the Wrapped covers query it again */
static void stats_load(void){
    memset(&g_s, 0, sizeof g_s); g_s.empty = 1; g_first_at = 0;
    sqlite3 *d = NULL;
    if(sqlite3_open_v2(STATS_DB_PATH, &d, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK){ if(d) sqlite3_close(d); return; }
    sqlite3_busy_timeout(d, 400);
    long long a, b, pa, pb; struct tm f;
    period_range(g_ptype, g_poff, &a, &b, &f); g_ra = a; g_rb = b;
    period_range(g_ptype, g_poff - 1, &pa, &pb, &f);
    sqlite3_stmt *st;
    if(sqlite3_prepare_v2(d, "SELECT IFNULL(MIN(AT),0) FROM PLAY_LOG;", -1, &st, NULL) == SQLITE_OK){
        if(sqlite3_step(st) == SQLITE_ROW) g_first_at = sqlite3_column_int64(st, 0);
        sqlite3_finalize(st);
    }
    g_s.ms = sum_ms(d, a, b, &g_s.plays, &g_s.songs);
    if(g_s.ms < 0){ g_s.ms = 0; sqlite3_close(d); return; }                    /* no log yet (nothing recorded so far) */
    g_s.empty = g_s.plays == 0;
    if(g_poff == 0){ long long in = (long long)time(NULL) - a; if(in > 0 && pa + in < pb) pb = pa + in; }   /* now: the same stretch of the last one */
    g_s.prev_ms = sum_ms(d, pa, pb, NULL, NULL);
    g_s.prev_known = g_first_at && g_first_at < pb && g_s.prev_ms > 0;          /* only compare with a period that was recorded */
    if(!g_s.empty){
        load_artists(d, a, b);
        g_s.nalb = load_items(d, "SELECT ALBUM,COALESCE(NULLIF(AARTIST,''),ARTIST),COUNT(*),SUM(MS),MAX(PATH) FROM PLAY_LOG "
                                 "WHERE AT>=?1 AND AT<?2 AND IFNULL(ALBUM,'')<>'' GROUP BY ALBUM,2 ORDER BY 3 DESC,4 DESC LIMIT 15;",
                              a, b, g_s.alb);
        g_s.albums = count_albums(d, a, b);
        g_s.nsong = load_items(d, "SELECT MAX(TITLE),MAX(ARTIST),COUNT(*),SUM(MS),PATH FROM PLAY_LOG "
                                  "WHERE AT>=?1 AND AT<?2 GROUP BY PATH ORDER BY 3 DESC,4 DESC LIMIT 15;", a, b, g_s.song);
        load_days_hours(d, a, b);
    }
    sqlite3_close(d);
}

/* ================================ formatting ================================ */
static void fmt_dur(long long ms, char *out, size_t n){
    long long m = ms / 60000;
    if(m >= 60) snprintf(out, n, "%lldh %02lldm", m / 60, m % 60);
    else snprintf(out, n, "%lldm", m);
}
static void fmt_dur_short(long long ms, char *out, size_t n){       /* "4h" / "35m": for the comparison line */
    long long m = (ms + 30000) / 60000;
    if(m >= 90) snprintf(out, n, "%lldh", (m + 30) / 60);
    else snprintf(out, n, "%lldm", m);
}
static void fmt_hour(int h, char *out, size_t n){
    if(simple_cfg_get_int("time_24h", 1)) snprintf(out, n, "%02d:00", h);
    else snprintf(out, n, "%d %s", h % 12 ? h % 12 : 12, h < 12 ? "am" : "pm");
}
static const char *persona(int h){
    if(h >= 5 && h < 9) return "Early bird";
    if(h >= 9 && h < 12) return "Morning listener";
    if(h >= 12 && h < 17) return "Daytime listener";
    if(h >= 17 && h < 21) return "Evening listener";
    return "Night owl";
}

/* ================================ the screen ================================
 * Main list: the overview (Week / Month / Year on an arc at the top, the period strip, the time, three pills), then
 * Top Artists, Top Albums and On repeat (top 5 each: the #1 as a hero, then rows), then the listening clock.
 * A tap on a section's title opens its top 15 (a sub-view); a tap on its #1 artwork opens its Wrapped (two pages:
 * the covers as bubbles, then the top 5 with their colour and time). Back closes the top-most of the two. */
LV_FONT_DECLARE(font_inter_bold_44)
LV_FONT_DECLARE(font_inter_bold_22)
LV_FONT_DECLARE(font_inter_medium_14)
LV_FONT_DECLARE(font_inter_medium_20)

static lv_obj_t *g_root, *g_list, *g_sub, *g_subl, *g_wrap;
static curvelist_t g_cl, g_cl2;
enum { K_ART, K_ALB, K_SONG };
/* covers, decoded into RAM once per build (no LVGL file decoding, no per-frame scaling): thumbs at their own 42 px,
 * the big ones scaled once here to their size. The sub-view and Wrapped take theirs after g_pic_keep and give them
 * back when they close. */
#define NPIC 64
static struct { uint8_t *px; lv_image_dsc_t dsc; } g_pic[NPIC];
static int g_npic, g_pic_keep;
static lv_obj_t *box(lv_obj_t *p, int x, int y, int w, int h, int r, lv_color_t c, lv_opa_t o){
    lv_obj_t *b = lv_obj_create(p); lv_obj_remove_style_all(b);
    lv_obj_set_pos(b, x, y); lv_obj_set_size(b, w, h);
    lv_obj_set_style_radius(b, r, 0); lv_obj_set_style_bg_color(b, c, 0); lv_obj_set_style_bg_opa(b, o, 0);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return b;
}
static lv_obj_t *text(lv_obj_t *p, const char *s, int x, int y, int w, const lv_font_t *f, lv_color_t c, lv_text_align_t al){
    lv_obj_t *l = lv_label_create(p); lv_label_set_text(l, s);
    lv_obj_set_style_text_font(l, f, 0); lv_obj_set_style_text_color(l, c, 0);
    lv_obj_set_style_text_align(l, al, 0); lv_obj_set_width(l, w);
    /* The theme can replace the font later; let its final line height size the marquee. */
    if(!strchr(s, '\n')){ lv_label_set_long_mode(l, LV_LABEL_LONG_SCROLL_CIRCULAR); lv_obj_set_height(l, LV_SIZE_CONTENT); }
    lv_obj_set_pos(l, x, y);
    lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
    return l;
}
static lv_obj_t *centre(lv_obj_t *p, const char *s, int y, int w, const lv_font_t *f, lv_color_t c){
    return text(p, s, (lv_obj_get_style_width(p, LV_PART_MAIN) - w) / 2, y, w, f, c, LV_TEXT_ALIGN_CENTER);   /* before layout: the set width */
}
static int text_w(const char *s, const lv_font_t *f){
    lv_point_t sz; lv_text_get_size(&sz, s, f, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return sz.x;
}
static void arc(lv_obj_t *p, int cx, int cy, int r, int w, int a0, int a1, lv_color_t c, lv_opa_t o, int round){
    lv_obj_t *a = lv_arc_create(p); lv_obj_remove_style_all(a);
    lv_obj_set_size(a, 2 * r, 2 * r); lv_obj_set_pos(a, cx - r, cy - r); lv_arc_set_bg_angles(a, a0, a1);
    lv_obj_set_style_arc_width(a, w, LV_PART_MAIN); lv_obj_set_style_arc_color(a, c, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(a, o, LV_PART_MAIN); lv_obj_set_style_arc_rounded(a, round, LV_PART_MAIN);
    lv_obj_remove_flag(a, LV_OBJ_FLAG_CLICKABLE);
}
/* a full-width block in a list that doesn't curve (the overview, the heroes, the clock) */
static lv_obj_t *block(lv_obj_t *list, int h){
    lv_obj_t *b = lv_obj_create(list); lv_obj_remove_style_all(b);
    lv_obj_set_size(b, 360, h);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return b;
}
/* a cache BMP (24-bit, bottom-up, rows padded to 4 bytes: the 42 px thumb's are) into XRGB8888 */
static int read_bmp(const char *path, int n, uint8_t *dst){
    if(!art_bmp_valid(path, n)) return -1;
    FILE *f = fopen(path, "rb"); if(!f) return -1;
    size_t stride = ((size_t)n * 3 + 3) & ~(size_t)3; uint8_t row[148 * 3 + 4]; int ok = n <= 148 && !fseek(f, 54, SEEK_SET);
    for(int y = 0; y < n && ok; y++){
        if(fread(row, 1, stride, f) != stride){ ok = 0; break; }
        uint8_t *d = dst + (size_t)(n - 1 - y) * n * 4;
        for(int x = 0; x < n; x++){ d[x * 4] = row[x * 3]; d[x * 4 + 1] = row[x * 3 + 1]; d[x * 4 + 2] = row[x * 3 + 2]; d[x * 4 + 3] = 0xFF; }
    }
    fclose(f);
    return ok ? 0 : -1;
}
static void pics_forget_from(int keep){
    for(int i = keep; i < g_npic; i++){ free(g_pic[i].px); g_pic[i].px = NULL; }
    if(g_npic > keep) g_npic = keep;
}
/* the track's cached cover as an image of size x size (42: the thumb as is; else the 148 px cover, box-filtered down);
 * NULL when the cache doesn't have it yet */
static const lv_image_dsc_t *cover_image(const char *path, int size){
    if(!path || !path[0] || g_npic >= NPIC) return NULL;
    int thumb = size == 42, n = thumb ? 42 : 148;
    char tmp[48]; snprintf(tmp, sizeof tmp, "/tmp/stats_cover_%d.bmp", g_npic);
    uint8_t *src = malloc((size_t)n * n * 4), *dst = NULL;
    if(!src) return NULL;
    int ok = (thumb ? artcache_get_thumb(path, tmp) : artcache_get_cover(path, tmp)) == 0 && read_bmp(tmp, n, src) == 0;
    unlink(tmp);
    if(!ok){ free(src); return NULL; }
    if(thumb) dst = src;
    else {
        if(size > n) size = n;
        dst = malloc((size_t)size * size * 4);
        if(!dst){ free(src); return NULL; }
        for(int y = 0; y < size; y++) for(int x = 0; x < size; x++){       /* average the source pixels under each one */
            int x0 = x * n / size, x1 = (x + 1) * n / size, y0 = y * n / size, y1 = (y + 1) * n / size, acc[3] = { 0, 0, 0 }, k = 0;
            for(int yy = y0; yy < y1; yy++) for(int xx = x0; xx < x1; xx++){ const uint8_t *p = src + ((size_t)yy * n + xx) * 4;
                acc[0] += p[0]; acc[1] += p[1]; acc[2] += p[2]; k++; }
            uint8_t *o = dst + ((size_t)y * size + x) * 4;
            o[0] = (uint8_t)(acc[0] / k); o[1] = (uint8_t)(acc[1] / k); o[2] = (uint8_t)(acc[2] / k); o[3] = 0xFF;
        }
        free(src);
    }
    lv_image_dsc_t *d = &g_pic[g_npic].dsc; g_pic[g_npic].px = dst; g_npic++;
    memset(d, 0, sizeof *d);
    d->header.magic = LV_IMAGE_HEADER_MAGIC; d->header.cf = LV_COLOR_FORMAT_XRGB8888;
    d->header.w = d->header.h = (uint32_t)size; d->header.stride = (uint32_t)size * 4;
    d->data = dst; d->data_size = (uint32_t)size * size * 4;
    return d;
}
/* the cover's colour, for the Wrapped dots: the thumb's most colourful pixels, lifted so a dot reads on the canvas */
static lv_color_t cover_colour(const char *path){
    if(th_classic()) return TC(TEXT_PRIMARY);   /* Graphite: one ink, never the cover's colour */
    const lv_image_dsc_t *img = cover_image(path, 42);
    if(!img) return TC(ACCENT_PRIMARY);
    long acc[3] = { 0, 0, 0 }, wsum = 0;
    for(int i = 0; i < 42 * 42; i++){
        const uint8_t *p = img->data + i * 4;
        int mx = p[0], mn = p[0];
        for(int k = 1; k < 3; k++){ if(p[k] > mx) mx = p[k]; if(p[k] < mn) mn = p[k]; }
        long w = 1 + (mx - mn) * (mx - mn) / 64;                                 /* saturated pixels weigh more */
        acc[0] += p[0] * w; acc[1] += p[1] * w; acc[2] += p[2] * w; wsum += w;
    }
    int b = (int)(acc[0] / wsum), g = (int)(acc[1] / wsum), r = (int)(acc[2] / wsum);
    int mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
    if(mx < 200 && mx > 0){ r = r * 200 / mx; g = g * 200 / mx; b = b * 200 / mx; }   /* bright enough on dark */
    pics_forget_from(g_npic - 1);                                                /* only the colour is kept */
    return theme_color_from_rgb(((uint32_t)(r > 255 ? 255 : r) << 16) | ((uint32_t)(g > 255 ? 255 : g) << 8) | (uint32_t)(b > 255 ? 255 : b));
}
/* a round or rounded picture: the cover scaled to `size`, else the theme surface with a glyph or a letter */
/* the cover for a played track: its own, else (the art cache holds one cover per album for sure, not per track) the
 * cover of another track of the same album, else its small thumb blown up. *scale_from: the source size to scale from */
static const lv_image_dsc_t *cover_any(const char *path, const char *album, int size, int *scale_from){
    *scale_from = 0;
    const lv_image_dsc_t *img = cover_image(path, size);
    if(!img && album && album[0]){
        int ids[6], n = mdb_album_track_ids(album, ids, 6);
        for(int i = 0; i < n && !img; i++){
            char t[512]; t[0] = 0;
            if(mdb_song_path(ids[i], t, sizeof t) && t[0] && strcmp(t, path ? path : "")) img = cover_image(t, size);
        }
    }
    if(!img && size != 42 && (img = cover_image(path, 42))) *scale_from = 42;
    else if(img && size > 148) *scale_from = 148;
    return img;
}
static lv_obj_t *picture(lv_obj_t *p, int x, int y, int size, int round, const char *path, const char *album, const char *fallback){
    lv_obj_t *w = box(p, x, y, size, size, round ? LV_RADIUS_CIRCLE : (size > 60 ? 12 : 6), TC(SURFACE_RAISED), LV_OPA_COVER);
    lv_obj_set_style_clip_corner(w, true, 0);
    int from; const lv_image_dsc_t *img = cover_any(path, album, size, &from);
    if(img){
        lv_obj_t *im = lv_image_create(w);
        lv_image_set_src(im, img);
        if(from){ lv_image_set_scale(im, (uint32_t)(256 * size / from)); }
        lv_obj_center(im);
    } else {
        lv_obj_t *l = lv_label_create(w); lv_label_set_text(l, fallback);
        lv_obj_set_style_text_font(l, size >= 40 ? ui_font_cjk(28) : ui_font_cjk(16), 0);   /* any script's first letter */
        lv_obj_set_style_text_color(l, TC(ACCENT_PRIMARY), 0); lv_obj_center(l);
    }
    return w;
}
static void initial(const char *name, char *out, size_t n){       /* the first letter (a whole UTF-8 character) */
    unsigned char c = (unsigned char)name[0]; int k = 1;
    if(c >= 0xF0) k = 4; else if(c >= 0xE0) k = 3; else if(c >= 0xC0) k = 2;
    snprintf(out, n, "%.*s", k, name);
    if(k == 1 && out[0] >= 'a' && out[0] <= 'z') out[0] = (char)(out[0] - 32);
}

/* ---- rows: wide enough that the play counts sit near the rim; the curve narrows the first and last in view,
 * which may cut their counts (only the three in the middle need to show them whole) ---- */
#define ROW_W 304
#define ROW_H 66
static lv_obj_t *row(lv_obj_t *list){
    lv_obj_t *r = lv_obj_create(list); lv_obj_remove_style_all(r);
    lv_obj_set_size(r, ROW_W, ROW_H);
    lv_obj_add_flag(r, LV_OBJ_FLAG_USER_1);                                     /* curves like a Library row */
    lv_obj_set_style_radius(r, 14, 0);
    lv_obj_set_style_bg_color(r, TC(SURFACE), LV_STATE_PRESSED); lv_obj_set_style_bg_opa(r, LV_OPA_70, LV_STATE_PRESSED);
    lv_obj_remove_flag(r, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return r;
}
static void plays_right(lv_obj_t *r, int plays, int accent){
    char t[16]; snprintf(t, sizeof t, "%d", plays);
    const lv_font_t *fn = TF(UI_24), *fc = TF(UI_14);
    int lh = lv_font_get_line_height(fn), sh = lv_font_get_line_height(fc), y = (ROW_H - lh - sh) / 2;
    text(r, t, ROW_W - 74, y, 60, fn, accent ? TC(ACCENT_PRIMARY) : TC(TEXT_PRIMARY), LV_TEXT_ALIGN_RIGHT);    /* the count: right-aligned */
    text(r, plays == 1 ? "play" : "plays", ROW_W - 74, y + lh, 60, fc, TC(TEXT_MUTED), LV_TEXT_ALIGN_RIGHT);
}
static void two_lines(lv_obj_t *r, int x, int w, const char *top, const char *sub){
    const lv_font_t *ft = ui_font_cjk(22), *fs = ui_font_cjk(18);
    int h1 = lv_font_get_line_height(ft), h2 = lv_font_get_line_height(fs);
    int two = sub && sub[0], y = (ROW_H - (two ? h1 + h2 : h1)) / 2;
    lv_obj_add_flag(text(r, top, x, y, w, ft, TC(TEXT_PRIMARY), LV_TEXT_ALIGN_LEFT), LV_OBJ_FLAG_USER_2);
    if(two) lv_obj_add_flag(text(r, sub, x, y + h1, w, fs, TC(TEXT_LYRICS), LV_TEXT_ALIGN_LEFT), LV_OBJ_FLAG_USER_2);
}
static void rank(lv_obj_t *r, int n){
    char t[8]; snprintf(t, sizeof t, "%d", n);
    const lv_font_t *f = TF(UI_24); int lh = lv_font_get_line_height(f);
    text(r, t, 4, (ROW_H - lh) / 2, 36, f, TC(ACCENT_PRIMARY), LV_TEXT_ALIGN_CENTER);
}
/* after the curve: the names give way, so they never run under a count. Names carry LV_OBJ_FLAG_USER_2. */
static void names_fit(lv_obj_t *list){
    if(!list) return;
    uint32_t n = lv_obj_get_child_count(list);
    for(uint32_t i = 0; i < n; i++){
        lv_obj_t *r = lv_obj_get_child(list, i);
        if(!lv_obj_has_flag(r, LV_OBJ_FLAG_USER_1)) continue;
        lv_area_t a; lv_obj_get_coords(r, &a);
        if(a.y2 < -40 || a.y1 > 400) continue;
        int rw = lv_obj_get_style_width(r, LV_PART_MAIN), narrow = rw < ROW_W - 6;   /* the curve took the count's room */
        for(uint32_t k = 0; k < lv_obj_get_child_count(r); k++){
            lv_obj_t *l = lv_obj_get_child(r, k);
            int count = lv_obj_check_type(l, &lv_label_class) && lv_obj_get_style_text_align(l, 0) == LV_TEXT_ALIGN_RIGHT;   /* plays_right's */
            if(!lv_obj_has_flag(l, LV_OBJ_FLAG_USER_2) && !count) continue;
            int x = lv_obj_get_style_x(l, LV_PART_MAIN), w = ROW_W - 80 - x;    /* the count: right-aligned at the row's end */
            if(count){                                                 /* a count: only where the row shows it whole */
                lv_obj_set_style_opa(l, narrow ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
                continue;
            }
            if(narrow) w = rw - x - 14;
            if(lv_obj_get_style_width(l, LV_PART_MAIN) != w) lv_obj_set_width(l, w);
        }
    }
}
static void fit_cb(lv_event_t *e){ names_fit(lv_event_get_target(e)); }
static lv_obj_t *tap(lv_obj_t *o, lv_event_cb_t cb, intptr_t ud){
    lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE); lv_obj_add_event_cb(o, cb, LV_EVENT_CLICKED, (void *)ud);
    if(!lv_obj_has_flag(o, LV_OBJ_FLAG_USER_1)) lv_obj_add_flag(o, LV_OBJ_FLAG_USER_4);   /* our own look in every theme (the kit leaves it) */
    return o;
}
static void rebuild(void);
static void song_cb(lv_event_t *e){
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if(i < 0 || i >= g_s.nsong) return;
    if(ui_play_song_by_path(g_s.song[i].path)) screen_show(SCR_NOWPLAYING);
}
static int kind_n(int k){ return k == K_ART ? g_s.nart : k == K_ALB ? g_s.nalb : g_s.nsong; }
static const item_t *kind_items(int k){ return k == K_ART ? g_s.art : k == K_ALB ? g_s.alb : g_s.song; }
static const char *const KIND_TITLE[3] = { "Top Artists", "Top Albums", "On Repeat" };

/* one row of a top list: rank, (cover), name (+ artist), plays */
static void item_row(lv_obj_t *list, int k, int i){
    const item_t *it = &kind_items(k)[i];
    lv_obj_t *r = row(list);
    if(k == K_SONG) tap(r, song_cb, i);
    rank(r, i + 1);
    if(k == K_ART){
        int lh = lv_font_get_line_height(ui_font_cjk(22));
        lv_obj_add_flag(text(r, it->name, 44, (ROW_H - lh) / 2, 150, ui_font_cjk(22), TC(TEXT_PRIMARY), LV_TEXT_ALIGN_LEFT), LV_OBJ_FLAG_USER_2);
    } else if(k == K_ALB){
        char n[12]; snprintf(n, sizeof n, "%d", i + 1);
        picture(r, 42, (ROW_H - 46) / 2, 46, 0, it->path, k == K_ALB ? it->name : NULL, n);
        two_lines(r, 98, 120, it->name, it->sub);
    } else two_lines(r, 44, 150, it->name, it->sub);
    plays_right(r, it->plays, 0);
}

/* ================================ swipe strips (period kind, period) ================================ */
static lv_point_t g_press;
static void period_step(int which, int step){                                   /* 0: Week/Month/Year, 1: the period */
    if(!step) return;
    if(which == 0){
        int t = g_ptype + step; if(t < P_WEEK || t > P_YEAR) return;
        g_ptype = t; g_poff = 0;
    } else {
        long long a, z; struct tm f; period_range(g_ptype, g_poff, &a, &z, &f);
        if(step < 0 && !(g_first_at && g_first_at < a)) return;                  /* nothing recorded before */
        if(step > 0 && g_poff >= 0) return;
        g_poff += step;
    }
    stats_load(); rebuild();
}
static void strip_cb(lv_event_t *e){
    lv_event_code_t code = lv_event_get_code(e);
    int which = (int)(intptr_t)lv_event_get_user_data(e);                       /* 0: Week/Month/Year, 1: the period */
    lv_indev_t *id = lv_indev_active(); if(!id) return;
    lv_point_t p; lv_indev_get_point(id, &p);
    if(code == LV_EVENT_PRESSED){ g_press = p; return; }
    if(code != LV_EVENT_RELEASED) return;
    int dx = p.x - g_press.x, dy = p.y - g_press.y, step = 0;
    if(abs(dy) > 24 && abs(dy) > abs(dx)) return;                                /* the list scrolled */
    if(abs(dx) >= 24) step = dx < 0 ? 1 : -1;                                    /* drag the strip: the next one comes in */
    else if(which == 0){ step = (p.x - 180 + (p.x < 180 ? -36 : 36)) / 72 - (g_ptype - P_MONTH); }   /* tap a segment */
    else if(p.x < 110) step = -1; else if(p.x > 250) step = 1;                   /* or tap an arrow */
    if(which == 0){ int t = g_ptype + step; if(t < P_WEEK) step = P_WEEK - g_ptype; if(t > P_YEAR) step = P_YEAR - g_ptype; }
    period_step(which, step);
}
static lv_obj_t *strip(lv_obj_t *p, int y, int h, int which){
    lv_obj_t *s = box(p, 0, y, 360, h, 0, TC(CANVAS), LV_OPA_TRANSP);
    lv_obj_add_flag(s, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s, strip_cb, LV_EVENT_PRESSED, (void *)(intptr_t)which);
    lv_obj_add_event_cb(s, strip_cb, LV_EVENT_RELEASED, (void *)(intptr_t)which);
    return s;
}
/* a label on the top arc: centred at `deg` from 12 o'clock (clockwise +), turned along the rim */
static void arc_label(lv_obj_t *p, const char *s, int deg, int rad, const lv_font_t *f, lv_color_t c){
    int w = 96, h = lv_font_get_line_height(f);
    float a = (float)deg * 3.14159265f / 180.0f;
    int cx = 180 + (int)lroundf(sinf(a) * rad), cy = 180 - (int)lroundf(cosf(a) * rad);
    lv_obj_t *l = text(p, s, cx - w / 2, cy - h / 2, w, f, c, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_style_transform_pivot_x(l, w / 2, 0); lv_obj_set_style_transform_pivot_y(l, h / 2, 0);
    if(deg) lv_obj_set_style_transform_rotation(l, deg * 10, 0);
}

/* ================================ the overview ================================ */
static void build_overview(void){
    lv_obj_t *b = block(g_list, 360);
    /* the rim: this period against the one before (now: against the same stretch of it; full when ahead) */
    arc(b, 180, 180, 179, 3, 0, 360, TC(SURFACE), LV_OPA_COVER, 0);
    if(!g_s.empty){
        int deg = 360;
        if(g_s.prev_known && g_s.prev_ms > 0 && g_s.ms < g_s.prev_ms) deg = (int)(360 * g_s.ms / g_s.prev_ms);
        if(deg >= 4) arc(b, 180, 180, 179, 3, 270, 270 + deg, TC(ACCENT_PRIMARY), LV_OPA_COVER, 1);
    }
    /* Week / Month / Year: a straight segmented pill, the chosen one lit (tap a segment, or swipe it) */
    static const char *const P[3] = { "Week", "Month", "Year" };
    { lv_obj_t *pill = box(b, 72, 44, 216, 34, 17, TC(SURFACE), LV_OPA_COVER);
      box(pill, (g_ptype - P_WEEK) * 72 + 2, 2, 68, 30, 15, TC(ACCENT_PRIMARY), LV_OPA_COVER);
      for(int i = 0; i < 3; i++)
          text(pill, P[i], i * 72, (34 - lv_font_get_line_height(th_simple()?TF(UI_18):TF(UI_16))) / 2, 72, th_simple()?TF(UI_18):TF(UI_16), i == g_ptype - P_WEEK ? TC(ON_ACCENT) : TC(TEXT_SECONDARY), LV_TEXT_ALIGN_CENTER); }
    strip(b, 40, 42, 0);                                                         /* on top: tap or drag it */
    /* the period, bigger, with < > to step back and forward */
    lv_obj_t *s1 = strip(b, 90, 44, 1);
    char name[48]; period_name(g_ptype, g_poff, name, sizeof name, 0);
    centre(s1, name, (44 - lv_font_get_line_height(TF(UI_28))) / 2, 220, TF(UI_28), TC(TEXT_PRIMARY));
    { long long a0, z0; struct tm f0; period_range(g_ptype, g_poff, &a0, &z0, &f0);
      int can[2] = { g_first_at && g_first_at < a0, g_poff < 0 }, ah = lv_font_get_line_height(TF(UI_22));
      text(s1, LV_SYMBOL_LEFT, 44, (44 - ah) / 2, 36, TF(UI_22), can[0] ? TC(ACCENT_PRIMARY) : TC(TEXT_MUTED), LV_TEXT_ALIGN_CENTER);
      text(s1, LV_SYMBOL_RIGHT, 280, (44 - ah) / 2, 36, TF(UI_22), can[1] ? TC(ACCENT_PRIMARY) : TC(TEXT_MUTED), LV_TEXT_ALIGN_CENTER); }
    /* the time, on one line across the widest part: big figures, bigger units, then "of music" ("2h 40m of music") */
    long long m = g_s.ms / 60000;
    char n1[16], n2[16]; const char *u1, *u2 = NULL;
    if(m >= 60){ snprintf(n1, sizeof n1, "%lld", m / 60); u1 = "h"; snprintf(n2, sizeof n2, "%02lld", m % 60); u2 = "m"; }
    else { snprintf(n1, sizeof n1, "%lld", m); u1 = "m"; }
    const lv_font_t *fb = &font_inter_bold_44, *fu = TF(UI_28), *fo = TF(UI_22);
    int ty = 148, gap = 2, sp = 10;
    const char *om = g_s.empty ? NULL : "of music";
    int w = text_w(n1, fb) + gap + text_w(u1, fu) + (u2 ? sp + text_w(n2, fb) + gap + text_w(u2, fu) : 0) + (om ? sp + 4 + text_w(om, fo) : 0);
    int x = 180 - w / 2, base = ty + fb->line_height - fb->base_line;
    const char *part[5] = { n1, u1, u2 ? n2 : NULL, u2, NULL };
    for(int i = 0; i < 4 && part[i]; i++){
        int big = !(i & 1); const lv_font_t *ff = big ? fb : fu; int pw = text_w(part[i], ff);
        int py = big ? ty : base - (ff->line_height - ff->base_line);
        text(b, part[i], x, py, pw + 2, ff, big ? TC(TEXT_PRIMARY) : TC(TEXT_SECONDARY), LV_TEXT_ALIGN_LEFT);
        x += pw + (big ? gap : sp);
    }
    if(om){ int pw = text_w(om, fo); text(b, om, x + 4, base - (fo->line_height - fo->base_line), pw + 2, fo, TC(TEXT_SECONDARY), LV_TEXT_ALIGN_LEFT); }
    int y = ty + fb->line_height + 8;
    if(g_s.empty){
        centre(b, g_poff == 0 ? "Nothing played yet" : "Nothing played then", y + 4, 260, TF(UI_20), th_simple()?TC(TEXT_PRIMARY):TC(TEXT_SECONDARY));
        centre(b, "A song counts once it has played\nfor 30 seconds. Your stats\nfill in as you listen.", y + 40, 290, TF(UI_16), th_simple()?TC(TEXT_SECONDARY):TC(TEXT_MUTED));
        return;
    }
    if(g_s.prev_known){                                                          /* the comparison, in a soft accent chip */
        char d[24], prev[48], line[96]; long long diff = g_s.ms - g_s.prev_ms;
        fmt_dur_short(diff < 0 ? -diff : diff, d, sizeof d);
        period_name(g_ptype, g_poff - 1, prev, sizeof prev, 1);
        if(llabs(diff) < 60000) snprintf(line, sizeof line, g_poff ? "About the same as %s" : "On a par with %s", prev);
        else if(g_poff) snprintf(line, sizeof line, "%s  %s %s than %s", diff > 0 ? LV_SYMBOL_UP : LV_SYMBOL_DOWN, d, diff > 0 ? "more" : "less", prev);
        else snprintf(line, sizeof line, "%s  %s %s %s", diff > 0 ? LV_SYMBOL_UP : LV_SYMBOL_DOWN, d, diff > 0 ? "ahead of" : "behind", prev);
        int cw = text_w(line, TF(UI_16)) + 28; if(cw > 300) cw = 300;
        lv_obj_t *chip = box(b, 180 - cw / 2, y, cw, 30, 15, TC(ACCENT_PRIMARY), LV_OPA_20);
        text(chip, line, 0, (30 - lv_font_get_line_height(TF(UI_16))) / 2, cw, TF(UI_16), TC(ACCENT_PRIMARY), LV_TEXT_ALIGN_CENTER);
    }
    y += 30 + 14;
    /* songs / albums / artists: no boxes, a soft vertical line fading out at both ends between them */
    char v[3][16]; const char *cap[3] = { g_s.songs == 1 ? "song" : "songs", g_s.albums == 1 ? "album" : "albums", g_s.artists == 1 ? "artist" : "artists" };
    snprintf(v[0], 16, "%d", g_s.songs); snprintf(v[1], 16, "%d", g_s.albums); snprintf(v[2], 16, "%d", g_s.artists);
    for(int i = 0; i < 3; i++){
        text(b, v[i], 42 + i * 92, y, 92, TF(UI_32), TC(TEXT_PRIMARY), LV_TEXT_ALIGN_CENTER);
        text(b, cap[i], 42 + i * 92, y + lv_font_get_line_height(TF(UI_32)), 92, TF(UI_16), TC(TEXT_SECONDARY), LV_TEXT_ALIGN_CENTER);
        if(i){ for(int hlf = 0; hlf < 2; hlf++){                                 /* two halves: clear -> line -> clear */
            lv_obj_t *ln = box(b, 42 + i * 92 - 1, y + 2 + hlf * 28, 2, 28, 0, TC(TEXT_SECONDARY), LV_OPA_COVER);
            lv_obj_set_style_bg_grad_color(ln, TC(TEXT_SECONDARY), 0); lv_obj_set_style_bg_grad_dir(ln, LV_GRAD_DIR_VER, 0);
            lv_obj_set_style_bg_main_opa(ln, hlf ? 150 : 0, 0); lv_obj_set_style_bg_grad_opa(ln, hlf ? 0 : 150, 0); } }
    }
    y += lv_font_get_line_height(TF(UI_32)) + lv_font_get_line_height(TF(UI_16)) + 8;
    const char *fav = "Your favourites";
    int fw = text_w(fav, TF(UI_22)), aw = text_w(LV_SYMBOL_DOWN, TF(UI_18)) + 10;
    text(b, LV_SYMBOL_DOWN, 180 - (fw + aw) / 2, y + 3, aw, TF(UI_18), TC(ACCENT_PRIMARY), LV_TEXT_ALIGN_LEFT);
    text(b, fav, 180 - (fw + aw) / 2 + aw, y, fw + 2, TF(UI_22), TC(TEXT_SECONDARY), LV_TEXT_ALIGN_LEFT);
}

/* ================================ the three sections ================================ */
static void sub_open(int k);
static void wrap_open(int k);
static void sub_cb(lv_event_t *e){ sub_open((int)(intptr_t)lv_event_get_user_data(e)); }
static void wrap_cb(lv_event_t *e){ wrap_open((int)(intptr_t)lv_event_get_user_data(e)); }
/* the section title: bigger, in a soft pill with a chevron - it opens the top 15 */
static void heading(int k){
    lv_obj_t *b = block(g_list, 58);
    char t[48]; snprintf(t, sizeof t, "%s", KIND_TITLE[k]);
    for(char *q = t; *q; q++) if(*q >= 'a' && *q <= 'z') *q = (char)(*q - 32);
    const lv_font_t *f = TF(UI_20);
    int tw = text_w(t, f), cw = text_w(LV_SYMBOL_RIGHT, TF(UI_16)), pw = tw + cw + 40;
    lv_obj_t *p = box(b, 180 - pw / 2, 12, pw, 38, 19, TC(SURFACE), LV_OPA_COVER);
    lv_obj_set_style_border_width(p, 1, 0); lv_obj_set_style_border_color(p, TC(ACCENT_PRIMARY), 0); lv_obj_set_style_border_opa(p, LV_OPA_40, 0);
    lv_obj_set_style_bg_color(p, TC(SURFACE_PRESSED), LV_STATE_PRESSED);
    lv_obj_set_ext_click_area(p, 8);
    text(p, t, 16, (38 - lv_font_get_line_height(f)) / 2, tw + 2, f, TC(ACCENT_PRIMARY), LV_TEXT_ALIGN_LEFT);
    text(p, LV_SYMBOL_RIGHT, pw - cw - 16, (38 - lv_font_get_line_height(TF(UI_16))) / 2, cw + 2, TF(UI_16), TC(ACCENT_PRIMARY), LV_TEXT_ALIGN_LEFT);
    tap(p, sub_cb, k);
}
/* the #1: its artwork (tap: Wrapped), name, and how much */
static void hero(int k){
    const item_t *top = &kind_items(k)[0];
    lv_obj_t *h = block(g_list, 186);
    char ini[8]; initial(top->name, ini, sizeof ini);
    lv_obj_t *pic = picture(h, 124, 2, 112, k == K_ART, top->path, k == K_ALB ? top->name : NULL, k == K_SONG ? LV_SYMBOL_AUDIO : ini);
    lv_obj_set_style_border_width(pic, 3, 0); lv_obj_set_style_border_color(pic, TC(ACCENT_PRIMARY), 0);
    lv_obj_set_ext_click_area(pic, 6);
    tap(pic, wrap_cb, k);
    lv_obj_t *badge = box(h, 214, 84, 32, 32, LV_RADIUS_CIRCLE, TC(ACCENT_PRIMARY), LV_OPA_COVER);
    text(badge, "1", 0, (32 - lv_font_get_line_height(TF(UI_16))) / 2, 32, TF(UI_16), TC(ON_ACCENT), LV_TEXT_ALIGN_CENTER);
    centre(h, top->name, 120, 290, ui_font_cjk(28), TC(TEXT_PRIMARY));
    char d[24], line[96]; fmt_dur(top->ms, d, sizeof d);
    if(k == K_ART) snprintf(line, sizeof line, "%d %s in %s", top->plays, top->plays == 1 ? "play" : "plays", d);
    else snprintf(line, sizeof line, "%s  \xC2\xB7  %d %s", top->sub[0] ? top->sub : d, top->plays, top->plays == 1 ? "play" : "plays");
    centre(h, line, 156, 280, ui_font_cjk(18), TC(TEXT_SECONDARY));
}
static void build_section(int k){
    int n = kind_n(k); if(!n) return;
    heading(k);
    hero(k);
    for(int i = 1; i < n && i < 5; i++) item_row(g_list, k, i);
}
/* when you listen: 24 bars round the rim (midnight at the top), the busiest hour in the middle */
static void build_clock(void){
    long long mx = 0; int peak = 0;
    for(int h = 0; h < 24; h++) if(g_s.hour_ms[h] > mx){ mx = g_s.hour_ms[h]; peak = h; }
    if(mx <= 0) return;
    lv_obj_t *b = block(g_list, 360);
    for(int h = 0; h < 24; h++){
        int a0 = 270 + h * 15 + 2, a1 = a0 + 11;
        arc(b, 180, 180, 160, 36, a0, a1, TC(SURFACE), LV_OPA_COVER, 0);
        int w = (int)(36 * g_s.hour_ms[h] / mx);
        if(g_s.hour_ms[h] > 0 && w < 4) w = 4;
        if(w) arc(b, 180, 180, 160, w, a0, a1, TC(ACCENT_PRIMARY), h == peak ? LV_OPA_COVER : LV_OPA_60, 0);
    }
    const lv_font_t *fs = TF(UI_14); int lh = lv_font_get_line_height(fs);
    text(b, "0", 168, 64, 24, fs, TC(TEXT_MUTED), LV_TEXT_ALIGN_CENTER);
    text(b, "6", 272, 180 - lh / 2, 24, fs, TC(TEXT_MUTED), LV_TEXT_ALIGN_CENTER);
    text(b, "12", 168, 296 - lh, 24, fs, TC(TEXT_MUTED), LV_TEXT_ALIGN_CENTER);
    text(b, "18", 64, 180 - lh / 2, 24, fs, TC(TEXT_MUTED), LV_TEXT_ALIGN_CENTER);
    char hr[16]; fmt_hour(peak, hr, sizeof hr);
    centre(b, "WHEN YOU LISTEN", 106, 180, TF(UI_14), TC(ACCENT_PRIMARY));
    centre(b, "Most of all at", 134, 180, TF(UI_16), TC(TEXT_SECONDARY));
    centre(b, hr, 156, 180, TF(UI_36), TC(TEXT_PRIMARY));
    centre(b, persona(peak), 160 + lv_font_get_line_height(TF(UI_36)), 190, TF(UI_18), TC(ACCENT_PRIMARY));
}

/* ================================ overlays: the top 15, Wrapped ================================ */
static lv_obj_t *panel(void){
    lv_obj_t *p = lv_obj_create(g_root); lv_obj_remove_style_all(p);
    lv_obj_set_size(p, 360, 360); lv_obj_set_pos(p, 0, 0);
    lv_obj_set_style_bg_color(p, TC(CANVAS), 0); lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    lv_obj_remove_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(p, LV_OBJ_FLAG_CLICKABLE);                                   /* nothing underneath takes a tap */
    return p;
}
static void overlays_close(void);
static void sub_back_cb(lv_event_t *e){ (void)e; screen_back(); }
static void sub_open(int k){
    overlays_close();
    g_pic_keep = g_npic;
    g_sub = panel();
    char t[48]; snprintf(t, sizeof t, "%s", KIND_TITLE[k]);
    if(th_null()){
        extern lv_obj_t *kit_default_header(lv_obj_t *,const char *,lv_event_cb_t);
        kit_default_header(g_sub,t,sub_back_cb);
    }else ui_header_cb(g_sub, t, sub_back_cb);
    g_subl = lv_obj_create(g_sub); lv_obj_remove_style_all(g_subl);
    lv_obj_set_size(g_subl, 360, 360);
    lv_obj_set_flex_flow(g_subl, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(g_subl, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_top(g_subl, 64, 0); lv_obj_set_style_pad_bottom(g_subl, 120, 0);
    lv_obj_set_scroll_dir(g_subl, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(g_subl, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(g_subl, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_move_background(g_subl);                                              /* under the header */
    curvelist_attach(&g_cl2, g_subl, g_sub, ROW_W);
    g_cl2.disco_style=th_null();
    lv_obj_add_event_cb(g_subl, fit_cb, LV_EVENT_SCROLL, NULL);
    for(int i = 0; i < kind_n(k); i++) item_row(g_subl, k, i);
    curvelist_update(&g_cl2); names_fit(g_subl);
}
/* Wrapped: two pages that snap - the covers as overlapping bubbles with the #1, then the top 5 with each one's colour */
static void wrap_back_cb(lv_event_t *e){ (void)e; screen_back(); }
static int artist_covers(const char *artist, char out[3][256], char alb[3][128]){
    sqlite3 *d = NULL; int n = 0;
    if(sqlite3_open_v2(STATS_DB_PATH, &d, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK){ if(d) sqlite3_close(d); return 0; }
    sqlite3_stmt *st;
    if(sqlite3_prepare_v2(d, "SELECT MAX(PATH),ALBUM FROM PLAY_LOG WHERE AT>=?1 AND AT<?2 AND IFNULL(ALBUM,'')<>'' AND "
                             "(ARTIST=?3 OR AARTIST=?3 OR (','||REPLACE(REPLACE(ARTIST,', ',','),';',',')||',') LIKE '%,'||?3||',%') "
                             "GROUP BY ALBUM ORDER BY COUNT(*) DESC LIMIT 3;", -1, &st, NULL) == SQLITE_OK){
        sqlite3_bind_int64(st, 1, g_ra); sqlite3_bind_int64(st, 2, g_rb); sqlite3_bind_text(st, 3, artist, -1, SQLITE_STATIC);
        while(n < 3 && sqlite3_step(st) == SQLITE_ROW){ snprintf(out[n], 256, "%s", col(st, 0)); snprintf(alb[n], 128, "%s", col(st, 1)); n++; }
        sqlite3_finalize(st);
    }
    sqlite3_close(d);
    return n;
}
static void wrap_open(int k){
    int n = kind_n(k); if(!n) return;
    const item_t *it = kind_items(k);
    if(g_wrap){ lv_obj_delete(g_wrap); g_wrap = NULL; }
    if(!g_sub) g_pic_keep = g_npic;
    g_wrap = panel();
    lv_obj_t *sc = lv_obj_create(g_wrap); lv_obj_remove_style_all(sc);
    lv_obj_set_size(sc, 360, 360);
    lv_obj_set_flex_flow(sc, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(sc, LV_DIR_VER); lv_obj_set_scroll_snap_y(sc, LV_SCROLL_SNAP_START);
    lv_obj_set_scrollbar_mode(sc, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(sc, LV_OBJ_FLAG_SCROLL_ONE);
    char per[48]; period_name(g_ptype, g_poff, per, sizeof per, 0);
    for(char *q = per; *q; q++) if(*q >= 'a' && *q <= 'z') *q = (char)(*q - 32);

    /* page 1: the bubbles */
    lv_obj_t *p1 = block(sc, 360);
    char paths[3][256], albs[3][128]; int np = 0;
    if(k == K_ART) np = artist_covers(it[0].name, paths, albs);
    else for(; np < 3 && np < n; np++){ snprintf(paths[np], 256, "%s", it[np].path); snprintf(albs[np], 128, "%s", k == K_ALB ? it[np].name : ""); }
    if(!np){ snprintf(paths[0], 256, "%s", it[0].path); albs[0][0] = 0; np = 1; }
    lv_color_t glow = cover_colour(paths[0]);
    lv_obj_t *g = box(p1, 30, 20, 300, 300, LV_RADIUS_CIRCLE, glow, LV_OPA_20);   /* the #1's colour, softly behind */
    lv_obj_set_style_bg_grad_color(g, TC(CANVAS), 0); lv_obj_set_style_bg_grad_dir(g, LV_GRAD_DIR_VER, 0);
    static const struct { int x, y, s; } B[3] = { { 82, 64, 136 }, { 202, 92, 100 }, { 150, 168, 78 } };   /* big, right, below */
    for(int i = np - 1; i >= 0; i--){                                            /* the biggest on top */
        int s = np == 1 ? 150 : B[i].s, x = np == 1 ? 105 : B[i].x, y = np == 1 ? 58 : B[i].y;
        if(np == 2 && i == 0) x = 96;
        char ini[8]; initial(it[k == K_ART ? 0 : i].name, ini, sizeof ini);              /* each bubble its own letter */
        lv_obj_t *pc = picture(p1, x, y, s, 1, paths[i], albs[i], k == K_SONG ? LV_SYMBOL_AUDIO : ini);
        lv_obj_set_style_border_width(pc, 4, 0); lv_obj_set_style_border_color(pc, TC(CANVAS), 0);
        lv_obj_set_style_shadow_width(pc, 24, 0); lv_obj_set_style_shadow_color(pc, TC(FIXED_MEDIA_BLACK), 0); lv_obj_set_style_shadow_opa(pc, LV_OPA_60, 0);
    }
    char cap[64]; snprintf(cap, sizeof cap, "%s, %s", k == K_ART ? "YOUR TOP ARTIST" : k == K_ALB ? "YOUR TOP ALBUM" : "YOUR SONG", per);
    centre(p1, cap, 30, 230, TF(UI_12), TC(ACCENT_PRIMARY));
    centre(p1, it[0].name, 254, 280, ui_font_cjk(28), TC(TEXT_PRIMARY));
    char d[24], line[96]; fmt_dur(it[0].ms, d, sizeof d);
    if(k == K_ART) snprintf(line, sizeof line, "%s  \xC2\xB7  %d %s", d, it[0].plays, it[0].plays == 1 ? "play" : "plays");
    else snprintf(line, sizeof line, "%s  \xC2\xB7  %d %s", it[0].sub, it[0].plays, it[0].plays == 1 ? "play" : "plays");
    centre(p1, line, 292, 250, ui_font_cjk(16), TC(TEXT_SECONDARY));
    char more[48]; snprintf(more, sizeof more, LV_SYMBOL_DOWN "  Your top %d", n < 5 ? n : 5);
    centre(p1, more, 322, 200, TF(UI_14), TC(TEXT_MUTED));

    /* page 2: the top 5, each its cover's colour, then how long */
    lv_obj_t *p2 = block(sc, 360);
    char t2[48]; snprintf(t2, sizeof t2, "TOP %d %s", n < 5 ? n : 5, k == K_ART ? "ARTISTS" : k == K_ALB ? "ALBUMS" : "SONGS");
    centre(p2, t2, 30, 200, TF(UI_12), TC(ACCENT_PRIMARY));
    int m5 = n < 5 ? n : 5, y0 = 180 - m5 * 56 / 2 + 6;
    for(int i = 0; i < m5; i++){
        int y = y0 + i * 56;
        const lv_font_t *fn = ui_font_cjk(i ? 20 : 24);
        char nm[160]; snprintf(nm, sizeof nm, "%s", it[i].name);
        int nw = text_w(nm, fn); if(nw > 220) nw = 220;
        int dx = 180 - (nw + 22) / 2;
        lv_color_t c = cover_colour(it[i].path);
        lv_obj_t *dot = box(p2, dx, y + (lv_font_get_line_height(fn) - 14) / 2, 14, 14, LV_RADIUS_CIRCLE, c, LV_OPA_COVER);
        lv_obj_set_style_shadow_width(dot, 10, 0); lv_obj_set_style_shadow_color(dot, c, 0); lv_obj_set_style_shadow_opa(dot, LV_OPA_50, 0);
        text(p2, nm, dx + 22, y, nw + 2, fn, i ? TC(TEXT_PRIMARY) : c, LV_TEXT_ALIGN_LEFT);
        char tm[24], l2[64]; fmt_dur(it[i].ms, tm, sizeof tm);
        snprintf(l2, sizeof l2, "listened for %s", tm);
        centre(p2, l2, y + lv_font_get_line_height(fn) + 1, 240, TF(UI_14), TC(TEXT_SECONDARY));
    }
    /* the back hint: a small tab on the left edge */
    lv_obj_t *tab = box(g_wrap, -18, 150, 40, 60, 30, TC(SURFACE), LV_OPA_80);
    lv_obj_set_ext_click_area(tab, 10);
    text(tab, LV_SYMBOL_LEFT, 18, (60 - lv_font_get_line_height(TF(UI_14))) / 2, 18, TF(UI_14), TC(TEXT_SECONDARY), LV_TEXT_ALIGN_CENTER);
    tap(tab, wrap_back_cb, 0);
}
static void overlays_close(void){
    int had = g_wrap || g_sub;
    if(g_wrap){ lv_obj_delete(g_wrap); g_wrap = NULL; }
    if(g_sub){ lv_obj_delete(g_sub); g_sub = NULL; g_subl = NULL; memset(&g_cl2, 0, sizeof g_cl2); }
    if(had) pics_forget_from(g_pic_keep);
}
/* screen_back: the top-most overlay closes first (Wrapped, then the top 15); 0 = nothing open, leave the screen */
int stats_back_consumed(void){
    if(g_wrap){ lv_obj_delete(g_wrap); g_wrap = NULL; if(!g_sub) pics_forget_from(g_pic_keep); return 1; }
    if(g_sub){ overlays_close(); return 1; }
    return 0;
}

/* highlights: the longest run of days, the busiest day, and how long a listening day lasts on average */
static void build_highlights(void){
    if(g_s.empty || g_s.days <= 0) return;
    lv_obj_t *b = block(g_list, 290);
    centre(b, "HIGHLIGHTS", 24, 200, TF(UI_14), TC(ACCENT_PRIMARY));
    char v[3][32], d[24]; const char *cap[3];
    snprintf(v[0], 32, "%d", g_s.streak); cap[0] = g_s.streak == 1 ? "day in a row" : "days in a row";
    { time_t t = (time_t)((g_s.best_day - 2440587.5) * 86400.0) + 43200; struct tm tm; localtime_r(&t, &tm);
      static const char *const WD[7] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };
      static const char *const MO[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
      fmt_dur_short(g_s.best_day_ms, d, sizeof d); snprintf(v[1], 32, "%s", d);
      static char cb[48]; snprintf(cb, sizeof cb, "on %s, %d %s", WD[tm.tm_wday], tm.tm_mday, MO[tm.tm_mon]); cap[1] = cb; }
    fmt_dur_short(g_s.ms / g_s.days, d, sizeof d); snprintf(v[2], 32, "%s", d); cap[2] = "a day you listen";
    static const char *const T[3] = { "Longest streak", "Busiest day", "On average" };
    for(int i = 0; i < 3; i++){
        int y = 54 + i * 76;
        centre(b, T[i], y, 260, TF(UI_16), TC(TEXT_SECONDARY));
        centre(b, v[i], y + 20, 260, TF(UI_28), TC(TEXT_PRIMARY));
        centre(b, cap[i], y + 20 + lv_font_get_line_height(TF(UI_28)) - 2, 260, TF(UI_14), TC(TEXT_MUTED));
    }
}
static void rebuild(void){
    if(!g_list) return;
    overlays_close();
    lv_obj_clean(g_list);
    pics_forget_from(0); g_pic_keep = 0;                    /* after the clean: no image points at them any more */
    build_overview();
    build_section(K_ART);
    build_section(K_ALB);
    build_section(K_SONG);
    build_clock();
    build_highlights();
    lv_obj_scroll_to_y(g_list, 0, LV_ANIM_OFF);
    curvelist_update(&g_cl);
    names_fit(g_list);
}
void stats_create(lv_obj_t *root){
    g_root = root;
    lv_obj_set_style_bg_color(root, TC(CANVAS), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    g_list = lv_obj_create(root);
    lv_obj_remove_style_all(g_list);
    lv_obj_set_size(g_list, 360, 360);
    lv_obj_set_flex_flow(g_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(g_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(g_list, 0, 0);
    lv_obj_set_scroll_dir(g_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(g_list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(g_list, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    curvelist_attach(&g_cl, g_list, root, ROW_W);
    g_cl.disco_style=th_null();
    lv_obj_add_event_cb(g_list, fit_cb, LV_EVENT_SCROLL, NULL);   /* after the curve's own handler */
}
lv_obj_t *stats_scroller(void){ return g_sub ? g_subl : g_list; }
/* host renders: open a section's top 15 / Wrapped (k: 0 artists, 1 albums, 2 songs) and its wrapped page */
void stats_test_sub(int k){ sub_open(k); }
void stats_test_wrap(int k, int page){
    wrap_open(k);
    if(page && g_wrap){ lv_obj_t *sc = lv_obj_get_child(g_wrap, 0); lv_obj_update_layout(sc); lv_obj_scroll_to_y(sc, 360, LV_ANIM_OFF); }
}
/* the landing page's button: read the numbers now, then show them */
void stats_open(void){
    g_ptype = P_MONTH; g_poff = 0;
    stats_load();
    rebuild();
    screen_show(SCR_STATS);
}
void stats_landing(void){
    applanding_show("Listening Stats", "Your music, in numbers.",
                    "See what moves you.", "Show me", APP_BUILTIN_STATS);
}
#endif /* STATS_RECORDER_ONLY */
