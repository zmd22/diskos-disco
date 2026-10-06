/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
/* theme_store.c - the custom theme document: strict parsing (bounded size, no unknown or duplicate keys, every
 * editable role in both variants, six hex digits per colour), serialising, and the atomic save both builders use.
 * No LVGL. A document that fails any check is refused whole; the file on disk is never repaired or half-written. */
#include "theme_model.h"
#include <errno.h>
#include <pthread.h>
#include <sys/file.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#define JSMN_STATIC                 /* a private strict copy; ipc.c keeps its own lenient one */
#define JSMN_STRICT
#include "jsmn.h"

#define E(...) do { snprintf(err, errcap, __VA_ARGS__); return THEME_E_INVALID; } while(0)

static int tok_is(const char *js, const jsmntok_t *t, const char *s){
    int n = t->end - t->start;
    return t->type == JSMN_STRING && (int)strlen(s) == n && !strncmp(js + t->start, s, n);
}
/* index just past the value that starts at token i (skips a whole object/array) */
static int skip(const jsmntok_t *t, int i, int ntok){
    int end = t[i].end;
    i++;
    while(i < ntok && t[i].start < end) i++;
    return i;
}
static int hex6(const char *js, const jsmntok_t *t, uint32_t *out){
    if(t->type != JSMN_STRING || t->end - t->start != 6) return 0;
    uint32_t v = 0;
    for(int i = t->start; i < t->end; i++){
        char c = js[i]; int d;
        if(c >= '0' && c <= '9') d = c - '0';
        else if(c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else if(c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else return 0;
        v = v << 4 | (uint32_t)d;
    }
    *out = v;
    return 1;
}
static int small_int(const char *js, const jsmntok_t *t, int *out){
    if(t->type != JSMN_PRIMITIVE) return 0;
    int n = t->end - t->start;
    if(n < 1 || n > 10) return 0;                      /* up to THEME_REV_MAX (10 digits); range-checked by the caller */
    long long v = 0;
    for(int i = t->start; i < t->end; i++){ if(js[i] < '0' || js[i] > '9') return 0; v = v * 10 + (js[i] - '0'); }
    if(v > THEME_REV_MAX) return 0;
    *out = (int)v;
    return 1;
}

/* one palette object: exactly the editable roles, each once */
static int parse_palette(const char *js, const jsmntok_t *t, int i, int ntok, uint32_t *pal, const char *which,
                         char *err, int errcap){
    if(t[i].type != JSMN_OBJECT) E("%s must be an object", which);
    int seen[THEME_CLR_COUNT] = {0}, n = t[i].size, k = i + 1;
    for(int e = 0; e < n; e++){
        if(k + 1 >= ntok) E("%s is cut short", which);
        int role = -1;
        for(int r = 0; r < THEME_CLR_COUNT; r++) if(theme_role_key(r) && tok_is(js, &t[k], theme_role_key(r))){ role = r; break; }
        if(role < 0) E("%s: unknown colour \"%.*s\"", which, t[k].end - t[k].start > 40 ? 40 : t[k].end - t[k].start, js + t[k].start);
        if(seen[role]++) E("%s: %s is given twice", which, theme_role_key(role));
        if(!hex6(js, &t[k + 1], &pal[role])) E("%s: %s must be six hex digits", which, theme_role_key(role));
        k += 2;
    }
    for(int r=0;r<THEME_CLR_COUNT;r++) if(theme_role_key(r) && !seen[r]){
        int fallback=theme_role_fallback(r);
        if(fallback<0) E("%s: %s is missing",which,theme_role_key(r));
        pal[r]=pal[fallback];
    }
    return THEME_OK;
}

/* jsmn (even in strict mode) accepts some non-JSON (trailing or doubled commas, leading-zero numbers), so the whole
 * text is first checked against the JSON grammar here; jsmn then only tokenises text already known to be valid. */
typedef struct { const char *s; int n, i, depth; } jg_t;
static void jg_ws(jg_t *g){ while(g->i < g->n && (g->s[g->i] == ' ' || g->s[g->i] == '\t' || g->s[g->i] == '\n' || g->s[g->i] == '\r')) g->i++; }
static int jg_value(jg_t *g);
static int jg_string(jg_t *g){
    if(g->i >= g->n || g->s[g->i] != '"') return 0;
    for(g->i++; g->i < g->n; g->i++){
        unsigned char c = (unsigned char)g->s[g->i];
        if(c == '"'){ g->i++; return 1; }
        if(c < 0x20) return 0;
        if(c == '\\'){
            if(++g->i >= g->n) return 0;
            c = (unsigned char)g->s[g->i];
            if(c == 'u'){ for(int k = 0; k < 4; k++){ if(++g->i >= g->n) return 0; c = (unsigned char)g->s[g->i];
                          if(!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return 0; } }
            else if(!strchr("\"\\/bfnrt", c)) return 0;
        }
    }
    return 0;
}
static int jg_number(jg_t *g){
    int st = g->i;
    if(g->i < g->n && g->s[g->i] == '-') g->i++;
    if(g->i >= g->n) return 0;
    if(g->s[g->i] == '0') g->i++;
    else if(g->s[g->i] >= '1' && g->s[g->i] <= '9') while(g->i < g->n && g->s[g->i] >= '0' && g->s[g->i] <= '9') g->i++;
    else return 0;
    if(g->i < g->n && g->s[g->i] == '.'){ g->i++; int d = 0; while(g->i < g->n && g->s[g->i] >= '0' && g->s[g->i] <= '9'){ g->i++; d++; } if(!d) return 0; }
    if(g->i < g->n && (g->s[g->i] == 'e' || g->s[g->i] == 'E')){ g->i++; if(g->i < g->n && (g->s[g->i] == '+' || g->s[g->i] == '-')) g->i++;
        int d = 0; while(g->i < g->n && g->s[g->i] >= '0' && g->s[g->i] <= '9'){ g->i++; d++; } if(!d) return 0; }
    return g->i > st;
}
static int jg_lit(jg_t *g, const char *w){ int l = (int)strlen(w); if(g->n - g->i < l || strncmp(g->s + g->i, w, l)) return 0; g->i += l; return 1; }
static int jg_container(jg_t *g, char open, char close){
    if(++g->depth > 8) return 0;
    g->i++; jg_ws(g);
    if(g->i < g->n && g->s[g->i] == close){ g->i++; g->depth--; return 1; }
    for(;;){
        if(open == '{'){ jg_ws(g); if(!jg_string(g)) return 0; jg_ws(g); if(g->i >= g->n || g->s[g->i] != ':') return 0; g->i++; }
        if(!jg_value(g)) return 0;
        jg_ws(g);
        if(g->i >= g->n) return 0;
        if(g->s[g->i] == ','){ g->i++; continue; }
        if(g->s[g->i] == close){ g->i++; g->depth--; return 1; }
        return 0;
    }
}
static int jg_value(jg_t *g){
    jg_ws(g);
    if(g->i >= g->n) return 0;
    char c = g->s[g->i];
    if(c == '{') return jg_container(g, '{', '}');
    if(c == '[') return jg_container(g, '[', ']');
    if(c == '"') return jg_string(g);
    if(c == 't') return jg_lit(g, "true");
    if(c == 'f') return jg_lit(g, "false");
    if(c == 'n') return jg_lit(g, "null");
    return jg_number(g);
}
static int json_strict_ok(const char *s, int n){
    jg_t g = { s, n, 0, 0 };
    jg_ws(&g);
    if(g.i >= n || s[g.i] != '{' || !jg_value(&g)) return 0;
    jg_ws(&g);
    return g.i == n;
}

int theme_model_parse(const char *js, int len, theme_model_t *m, char *err, int errcap){
    if(!js || len <= 0) E("empty document");
    if(len > THEME_CUSTOM_MAX) E("document is over %d bytes", THEME_CUSTOM_MAX);
    for(int i = 0; i < len; i++) if(!js[i]) E("document contains a NUL byte");
    if(!json_strict_ok(js, len)) E("not valid JSON");
    enum { NT = 768 };
    jsmntok_t t[NT];                 /* per call: parsing is reentrant */
    jsmn_parser p; jsmn_init(&p);
    int ntok = jsmn_parse(&p, js, (size_t)len, t, NT);
    if(ntok < 1 || t[0].type != JSMN_OBJECT) E("not a JSON object");
    memset(m, 0, sizeof *m);
    int have_schema = 0, have_rev = 0, have_id = 0, have_name = 0, have_pol = 0, have_tr = 0, have_d = 0, have_l = 0;
    int k = 1;
    for(int e = 0; e < t[0].size; e++){
        if(k + 1 >= ntok) E("document is cut short");
        const jsmntok_t *key = &t[k], *val = &t[k + 1];
        if(tok_is(js, key, "schema")){
            int v; if(have_schema++ || !small_int(js, val, &v) || v != 1) E("unsupported schema");
        } else if(tok_is(js, key, "revision")){
            if(have_rev++ || !small_int(js, val, &m->revision) || m->revision < 1 || m->revision > THEME_REV_MAX) E("bad revision");
        } else if(tok_is(js, key, "id")){
            if(have_id++ || !tok_is(js, val, "custom")) E("id must be \"custom\"");
        } else if(tok_is(js, key, "name")){
            int n = val->end - val->start;
            if(have_name++ || val->type != JSMN_STRING || n < 1 || n > THEME_NAME_MAX) E("name must be 1-24 characters");
            memcpy(m->name, js + val->start, n); m->name[n] = 0;
        } else if(tok_is(js, key, "accent_policy")){
            if(have_pol++) E("accent_policy is given twice");
            if(tok_is(js, val, "fixed")) m->accent_fixed = 1;
            else if(tok_is(js, val, "allow_album")) m->accent_fixed = 0;
            else E("accent_policy must be fixed or allow_album");
        } else if(tok_is(js, key, "traits")){
            /* only the standard look exists today; any other trait value is refused, never ignored */
            if(have_tr++ || val->type != JSMN_OBJECT || val->size != 3) E("traits must name background, now_playing and quick_settings");
            int seen_bg = 0, seen_np = 0, seen_qs = 0, j = k + 2;
            for(int f = 0; f < val->size; f++){
                if(j + 1 >= ntok) E("traits are cut short");
                if(tok_is(js, &t[j], "background") && !seen_bg++ && tok_is(js, &t[j + 1], "solid")) {}
                else if(tok_is(js, &t[j], "now_playing") && !seen_np++ && tok_is(js, &t[j + 1], "standard")) {}
                else if(tok_is(js, &t[j], "quick_settings") && !seen_qs++ && tok_is(js, &t[j + 1], "fill")) {}
                else E("unsupported trait");
                j += 2;
            }
            if(!(seen_bg && seen_np && seen_qs)) E("traits must name background, now_playing and quick_settings");
        } else if(tok_is(js, key, "dark")){
            if(have_d++) E("dark is given twice");
            int r = parse_palette(js, t, k + 1, ntok, m->pal[0], "dark", err, errcap); if(r) return r;
        } else if(tok_is(js, key, "light")){
            if(have_l++) E("light is given twice");
            int r = parse_palette(js, t, k + 1, ntok, m->pal[1], "light", err, errcap); if(r) return r;
        } else E("unknown key \"%.*s\"", key->end - key->start > 40 ? 40 : key->end - key->start, js + key->start);
        k = skip(t, k + 1, ntok);
    }
    if(k != ntok) E("unexpected content in the document");
    if(!have_schema || !have_rev || !have_id || !have_name || !have_pol || !have_tr || !have_d || !have_l) E("a required field is missing");
    return theme_model_validate(m, err, errcap);
}

int theme_model_serialize(const theme_model_t *m, char *out, int cap){
    int n = snprintf(out, cap, "{\n  \"schema\": 1,\n  \"revision\": %d,\n  \"id\": \"custom\",\n  \"name\": \"%s\",\n"
                     "  \"accent_policy\": \"%s\",\n  \"traits\": { \"background\": \"solid\", \"now_playing\": \"standard\", "
                     "\"quick_settings\": \"fill\" }", m->revision, m->name, m->accent_fixed ? "fixed" : "allow_album");
    for(int v = 0; v < 2 && n >= 0 && n < cap; v++){
        n += snprintf(out + n, cap - n, ",\n  \"%s\": {", v ? "light" : "dark");
        int first = 1;
        for(int r = 0; r < THEME_CLR_COUNT && n < cap; r++){
            if(!theme_role_key(r)) continue;
            n += snprintf(out + n, cap - n, "%s\n    \"%s\": \"%06X\"", first ? "" : ",", theme_role_key(r), m->pal[v][r] & 0xFFFFFFu);
            first = 0;
        }
        if(n < cap) n += snprintf(out + n, cap - n, "\n  }");
    }
    if(n >= 0 && n < cap) n += snprintf(out + n, cap - n, "\n}\n");
    return (n < 0 || n >= cap) ? -1 : n;
}

static int read_file(const char *path, char *buf, int cap, int *len){
    int fd = open(path, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);   /* NONBLOCK: a FIFO can't hang us */
    if(fd < 0) return errno == ENOENT ? 1 : -1;
    struct stat st;
    if(fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size > cap){ close(fd); return -1; }
    int got = 0;
    while(got < st.st_size){ ssize_t r = read(fd, buf + got, (size_t)(st.st_size - got)); if(r <= 0) break; got += (int)r; }
    close(fd);
    if(got != st.st_size) return -1;
    *len = got;
    return 0;
}

int theme_model_load(const char *path, theme_model_t *m, char *err, int errcap){
    char *buf = malloc(THEME_CUSTOM_MAX + 1);
    if(!buf){ snprintf(err, errcap, "out of memory"); return THEME_E_IO; }
    int len = 0, r = read_file(path, buf, THEME_CUSTOM_MAX, &len);
    if(r == 1){ free(buf); snprintf(err, errcap, "no custom theme saved"); return THEME_E_ABSENT; }
    if(r < 0){ free(buf); snprintf(err, errcap, "custom theme file unreadable, not a plain file, or too large"); return THEME_E_IO; }
    r = theme_model_parse(buf, len, m, err, errcap);
    free(buf);
    return r;
}

/* write `data` to `path` atomically: open the directory first (so a missing directory fails BEFORE any change),
 * exclusive new temp file next to it, fsync, rename, fsync the directory. -1 = nothing changed; -4 = renamed into place
 * but the directory sync failed. */
static int atomic_write(const char *path, const char *data, int len){
    char tmp[300], dir[300];
    if(snprintf(tmp, sizeof tmp, "%s.XXXXXX", path) >= (int)sizeof tmp) return -1;
    snprintf(dir, sizeof dir, "%s", path);
    char *slash = strrchr(dir, '/');
    if(!slash) return -1;
    if(slash == dir) slash[1] = 0; else *slash = 0;
    int dfd = open(dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if(dfd < 0) return -1;
    int fd = mkstemp(tmp);                 /* O_CREAT|O_EXCL: never follows or reuses an existing name */
    if(fd < 0){ close(dfd); return -1; }
    int ok = fchmod(fd, 0644) == 0;
    for(int done = 0; ok && done < len; ){ ssize_t w = write(fd, data + done, (size_t)(len - done)); if(w <= 0) ok = 0; else done += (int)w; }
    if(ok && fsync(fd) != 0) ok = 0;
    if(close(fd) != 0) ok = 0;
    if(!ok || rename(tmp, path) != 0){ unlink(tmp); close(dfd); return -1; }
    int dr = fsync(dfd);
    close(dfd);
    return dr == 0 ? 0 : -4;
}

/* one save at a time: a mutex for threads in this process, an flock on a sibling lock file for other processes; the
 * lock spans loading the current file, the revision check and the replace, so two editors can't both pass the check */
static pthread_mutex_t g_save_mu = PTHREAD_MUTEX_INITIALIZER;

int theme_validate_and_save(const char *json, int len, int expect_revision, const char *path, char *err, int errcap){
    theme_model_t *m = malloc(sizeof *m), *cur = malloc(sizeof *cur);
    char *out = malloc(THEME_CUSTOM_MAX + 1);
    int rc = THEME_E_IO, lfd = -1, locked = 0;
    char lockp[320];
    if(!m || !cur || !out){ snprintf(err, errcap, "out of memory"); goto done; }
    rc = theme_model_parse(json, len, m, err, errcap);
    if(rc) goto done;
    int n = theme_model_serialize(m, out, THEME_CUSTOM_MAX + 1);   /* canonical form: exactly what was validated */
    if(n < 0 || n > THEME_CUSTOM_MAX){ snprintf(err, errcap, "theme too large to store"); rc = THEME_E_INVALID; goto done; }
    pthread_mutex_lock(&g_save_mu); locked = 1;
    snprintf(lockp, sizeof lockp, "%s.lock", path);
    lfd = open(lockp, O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600);
    if(lfd < 0 || flock(lfd, LOCK_EX) != 0){ snprintf(err, errcap, "could not lock the theme file"); rc = THEME_E_IO; goto done; }
    char e2[160];
    int st = theme_model_load(path, cur, e2, sizeof e2);
    int current;                                      /* the revision on disk: 0 = nothing saved */
    if(st == THEME_OK) current = cur->revision;
    else if(st == THEME_E_ABSENT) current = 0;
    else if(expect_revision == THEME_REV_REPLACE_DAMAGED) current = 0;   /* explicit reset of an unusable file */
    else { snprintf(err, errcap, "the saved theme can't be read (%s); nothing was changed", e2); rc = THEME_E_CONFLICT; goto done; }
    if(expect_revision == THEME_REV_REPLACE_DAMAGED ? st == THEME_OK || st == THEME_E_ABSENT : expect_revision != current){
        snprintf(err, errcap, "the theme was changed elsewhere (saved revision %d, editor started from %d)", current, expect_revision);
        rc = THEME_E_CONFLICT; goto done;
    }
    if(m->revision != current + 1){ snprintf(err, errcap, "revision must be %d", current + 1); rc = THEME_E_CONFLICT; goto done; }
    int w = atomic_write(path, out, n);
    if(w == -4){ snprintf(err, errcap, "saved, but the save may not survive a power cut"); rc = THEME_E_NOT_DURABLE; goto done; }
    if(w != 0){ snprintf(err, errcap, "could not save the theme; nothing was changed"); rc = THEME_E_IO; goto done; }
    rc = THEME_OK;
done:
    if(lfd >= 0) close(lfd);                          /* closing releases the flock */
    if(locked) pthread_mutex_unlock(&g_save_mu);
    free(m); free(cur); free(out);
    return rc;
}
