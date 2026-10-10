/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
/* Folder browser: long-press a file or folder for a radial menu (the Quick Settings orbit):
 *   Rename  - the full QWERTY keyboard (names need case and symbols)
 *   Copy / Move - pick a destination by browsing folders, then confirm; a name clash asks first
 *   Delete  - always confirmed; for a folder the dialog says how many files go with it
 * Rules: the track that is playing (or a folder containing it) can't be changed; every write goes
 * through the SD write gate (nothing while the card is handed to a PC); copies and deletes run on a
 * worker thread with progress toasts; afterwards the folder refreshes and the library re-scans. */
#include "screens.h"
#include "musicdb.h"
#include "theme.h"
#include "braun.h"
#include "orbit.h"
#include "artcache.h"
#include "ipc.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define FO_PATH 600
#define FO_ROOT "/tmp/sdcard"

static char g_dir[FO_PATH], g_name[256];
static int g_is_dir;
static void (*g_done)(void);
static lv_obj_t *g_ov;                                    /* the current overlay (menu / confirm / picker) */
static orbit_t g_orb;

/* ---------------------------------------------------------------- helpers */
static void close_ov(void){ if(g_ov){ lv_obj_delete(g_ov); g_ov = NULL; } }
static lv_obj_t *overlay(void){
    close_ov();
    g_ov = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(g_ov);
    lv_obj_set_size(g_ov, 360, 360);
    lv_obj_set_style_bg_color(g_ov, TC(CANVAS), 0);
    lv_obj_set_style_bg_opa(g_ov, 235, 0);
    lv_obj_add_flag(g_ov, LV_OBJ_FLAG_CLICKABLE);          /* swallow taps behind it */
    lv_obj_clear_flag(g_ov, LV_OBJ_FLAG_SCROLLABLE);
    return g_ov;
}
static int path_join(char *out, size_t cap, const char *a, const char *b){
    int n = snprintf(out, cap, "%s/%s", a, b); return n > 0 && (size_t)n < cap;
}
static int is_playing_inside(const char *p, int dir){      /* the playing track is p, or inside folder p */
    track_state_t st; ipc_get_state(&st);
    if(!st.have_track || !st.path[0]) return 0;
    size_t n = strlen(p);
    if(!dir) return !strcmp(st.path, p);
    return !strncmp(st.path, p, n) && st.path[n] == '/';
}
static long count_files(const char *p, int depth){
    struct stat st; if(lstat(p, &st) != 0) return 0;
    if(!S_ISDIR(st.st_mode)) return 1;
    if(depth > 24) return 0;
    DIR *d = opendir(p); if(!d) return 0;
    long n = 0; struct dirent *e; char c[FO_PATH];
    while((e = readdir(d))){
        if(!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        if(path_join(c, sizeof c, p, e->d_name)) n += count_files(c, depth + 1);
    }
    closedir(d); return n;
}
static int rm_tree(const char *p, int depth){             /* 0 = removed */
    struct stat st; if(lstat(p, &st) != 0) return -1;
    if(!S_ISDIR(st.st_mode)) return unlink(p);
    if(depth > 24) return -1;
    DIR *d = opendir(p); if(!d) return -1;
    struct dirent *e; char c[FO_PATH]; int r = 0;
    while((e = readdir(d))){
        if(!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        if(!path_join(c, sizeof c, p, e->d_name) || rm_tree(c, depth + 1) != 0) r = -1;
    }
    closedir(d);
    return r == 0 ? rmdir(p) : -1;
}
static int copy_file(const char *s, const char *d){
    int in = open(s, O_RDONLY | O_CLOEXEC); if(in < 0) return -1;
    char tmp[FO_PATH + 16]; snprintf(tmp, sizeof tmp, "%s.part.XXXXXX", d);
    int out = mkstemp(tmp); if(out < 0){ close(in); return -1; }
    if(fcntl(out, F_SETFD, FD_CLOEXEC) != 0 || fchmod(out, 0644) != 0){ close(in); close(out); unlink(tmp); return -1; }
    static char buf[64 * 1024]; ssize_t r; int ok = 1;
    while((r = read(in, buf, sizeof buf)) > 0){
        for(ssize_t o = 0; o < r; ){ ssize_t w = write(out, buf + o, (size_t)(r - o)); if(w <= 0){ ok = 0; break; } o += w; }
        if(!ok) break;
    }
    if(r < 0) ok = 0;
    if(ok && fsync(out) != 0) ok = 0;
    close(in); if(close(out) != 0) ok = 0;
    if(ok) ok = rename(tmp, d) == 0;
    if(!ok) unlink(tmp);
    return ok ? 0 : -1;
}
static int copy_tree(const char *s, const char *d, int depth){
    struct stat st; if(lstat(s, &st) != 0) return -1;
    if(!S_ISDIR(st.st_mode)) return S_ISREG(st.st_mode) ? copy_file(s, d) : 0;
    if(depth > 24) return -1;
    if(mkdir(d, 0755) != 0 && errno != EEXIST) return -1;
    DIR *dd = opendir(s); if(!dd) return -1;
    struct dirent *e; char cs[FO_PATH], cd[FO_PATH]; int r = 0;
    while((e = readdir(dd))){
        if(!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        if(!path_join(cs, sizeof cs, s, e->d_name) || !path_join(cd, sizeof cd, d, e->d_name) || copy_tree(cs, cd, depth + 1) != 0) r = -1;
    }
    closedir(dd); return r;
}

/* Prepare the complete replacement alongside the destination. Only then move
 * the original into the private backup and install the new tree. Failed copies
 * leave the original untouched; failed installs restore it. Never erase a
 * backup if restoring it fails (e.g. the card disappears between renames). */
static int replace_tree(const char *src, const char *dst, int move){
    char stage[FO_PATH], fresh[FO_PATH], backup[FO_PATH]; struct stat st;
    int n = snprintf(stage, sizeof stage, "%s.diskos-replace-XXXXXX", dst);
    if(n < 0 || (size_t)n >= sizeof stage || !mkdtemp(stage)) return -1;
    if(!path_join(fresh, sizeof fresh, stage, "new") || !path_join(backup, sizeof backup, stage, "original")){
        rmdir(stage); return -1;
    }
    if(copy_tree(src, fresh, 0) != 0){ rm_tree(stage, 0); return -1; }
    int have_old = lstat(dst, &st) == 0;
    if((!have_old && errno != ENOENT) ||
       (have_old && is_playing_inside(dst, S_ISDIR(st.st_mode))) ||
       (move && is_playing_inside(src, 1)) || (move && is_playing_inside(src, 0))){
        rm_tree(stage, 0); return -1;
    }
    if(have_old && rename(dst, backup) != 0){ rm_tree(stage, 0); return -1; }
    if(rename(fresh, dst) != 0){
        if(have_old && rename(backup, dst) != 0){
            fprintf(stderr, "fileops: original preserved at %s\n", backup);
            return -1;
        }
        rm_tree(stage, 0); return -1;
    }
    if(rm_tree(stage, 0) != 0) fprintf(stderr, "fileops: replacement backup retained at %s\n", stage);
    return !move || rm_tree(src, 0) == 0 ? 0 : -1;
}

/* ---------------------------------------------------------------- the worker (copy / move / delete) */
enum { OP_DELETE, OP_COPY, OP_MOVE, OP_BOTH };
static struct { int op, replace, busy, finished, ok; char src[FO_PATH], dst[FO_PATH]; } W;
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static lv_timer_t *g_poll;
static void *worker(void *arg){
    (void)arg; int ok = 0;
    if(sd_write_begin()){
        struct stat st;
        int source_playing = W.op != OP_COPY && lstat(W.src, &st) == 0 && is_playing_inside(W.src, S_ISDIR(st.st_mode));
        int dest_playing = W.op != OP_DELETE && lstat(W.dst, &st) == 0 && is_playing_inside(W.dst, S_ISDIR(st.st_mode));
        if(source_playing || dest_playing) ok = 0;
        else if(W.replace) ok = replace_tree(W.src, W.dst, W.op == OP_MOVE) == 0;
        else if(W.op == OP_DELETE) ok = rm_tree(W.src, 0) == 0;
        else if(W.op == OP_MOVE){
            if(rename(W.src, W.dst) == 0) ok = 1;
            else if(errno == EXDEV) ok = copy_tree(W.src, W.dst, 0) == 0 && rm_tree(W.src, 0) == 0;
        } else ok = copy_tree(W.src, W.dst, 0) == 0;
        sync();
        sd_write_end();
    }
    pthread_mutex_lock(&g_mu); W.ok = ok; W.finished = 1; pthread_mutex_unlock(&g_mu);
    return NULL;
}
static void poll_cb(lv_timer_t *t){
    (void)t;
    pthread_mutex_lock(&g_mu); int fin = W.finished, ok = W.ok, op = W.op; pthread_mutex_unlock(&g_mu);
    if(!fin) return;
    lv_timer_delete(g_poll); g_poll = NULL;
    static const char *const done[3] = { "Deleted", "Copied", "Moved" }, *const fail[3] = { "Couldn't delete everything", "Copy failed (card full?)", "Move failed" };
    ui_toast(ok ? done[op] : fail[op]);
    W.busy = 0;
    if(ok && op == OP_DELETE){ queue_path_removed(W.src); mdb_playlist_paths_removed(W.src); }   /* no dead entries left behind */
    if(ok && op == OP_MOVE){ queue_path_moved(W.src, W.dst); mdb_playlist_paths_moved(W.src, W.dst); }
    if(g_done) g_done();                                    /* the folder view refreshes */
    ui_rescan_library();                                    /* and the Library follows the card */
}
static void run(int op, const char *src, const char *dst, int replace){
    if(W.busy){ ui_toast("Still working on the last one"); return; }
    memset(&W, 0, sizeof W); W.op = op; W.replace = replace; W.busy = 1;
    snprintf(W.src, sizeof W.src, "%s", src); if(dst) snprintf(W.dst, sizeof W.dst, "%s", dst);
    pthread_t th; pthread_attr_t at; pthread_attr_init(&at); pthread_attr_setstacksize(&at, 256 * 1024);
    if(pthread_create(&th, &at, worker, NULL) != 0){ W.busy = 0; pthread_attr_destroy(&at); ui_toast("Couldn't start"); return; }
    pthread_detach(th); pthread_attr_destroy(&at);
    g_poll = lv_timer_create(poll_cb, 300, NULL);
    ui_toast(op == OP_DELETE ? "Deleting..." : op == OP_COPY ? "Copying..." : "Moving...");
}

/* ---------------------------------------------------------------- confirm dialog */
static void (*g_yes)(void);
static void yes_cb(lv_event_t *e){ (void)e; void (*f)(void) = g_yes; close_ov(); if(f) f(); }
static void no_cb(lv_event_t *e){ (void)e; close_ov(); }
static lv_obj_t *pill_btn(lv_obj_t *p, const char *txt, uint32_t bg, int x, lv_event_cb_t cb){
    lv_obj_t *b = lv_button_create(p);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, 108, 44);
    lv_obj_align(b, LV_ALIGN_CENTER, x, 64);
    lv_obj_set_style_radius(b, TH_R_PILL, 0);
    lv_obj_set_style_bg_color(b, theme_color_from_rgb(bg), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l = lv_label_create(b); lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, TH_F_DETAIL, 0); lv_obj_set_style_text_color(l, bg == TH_ACCENT ? TC(ON_ACCENT) : TC(TEXT_PRIMARY), 0); lv_obj_center(l);   /* white on the accent in both themes (Braun's TXT1 is dark) */
    return b;
}
static void confirm(const char *title, const char *detail, const char *yes, void (*on_yes)(void)){
    lv_obj_t *o = overlay();
    g_yes = on_yes;
    lv_obj_t *t = lv_label_create(o);
    lv_label_set_text(t, title);
    lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(t, 250);
    lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(t, ui_font_cjk(18), 0);
    lv_obj_set_style_text_color(t, TC(TEXT_PRIMARY), 0);
    lv_obj_align(t, LV_ALIGN_CENTER, 0, -40);
    if(detail && detail[0]){
        lv_obj_t *d = lv_label_create(o);
        lv_label_set_text(d, detail);
        lv_obj_set_style_text_font(d, TH_F_DETAIL, 0);
        lv_obj_set_style_text_color(d, TC(TEXT_SECONDARY), 0);
        lv_obj_align(d, LV_ALIGN_CENTER, 0, 10);
    }
    pill_btn(o, "Cancel", TH_SURF1, -60, no_cb);
    pill_btn(o, yes, TH_ACCENT, 60, yes_cb);
}

void fileops_confirm(const char *title, const char *detail, const char *yes, void (*on_yes)(void)){ confirm(title, detail, yes, on_yes); }   /* generic themed yes/no, used outside file ops */

/* ---------------------------------------------------------------- delete */
static void do_delete(void){ char p[FO_PATH]; if(path_join(p, sizeof p, g_dir, g_name)) run(OP_DELETE, p, NULL, 0); }
static void ask_delete(void){
    char p[FO_PATH], t[320], d[64];
    if(!path_join(p, sizeof p, g_dir, g_name)) return;
    snprintf(t, sizeof t, "Delete \"%s\"?", g_name);
    if(g_is_dir){ long n = count_files(p, 0); snprintf(d, sizeof d, "Folder with %ld file%s", n, n == 1 ? "" : "s"); }
    else d[0] = 0;
    confirm(t, d, "Delete", do_delete);
}

/* ---------------------------------------------------------------- rename */
static void rename_done(const char *txt){
    if(!txt || !txt[0] || !strcmp(txt, g_name)) return;
    if(strchr(txt, '/') || !strcmp(txt, ".") || !strcmp(txt, "..")){ ui_toast("That name isn't allowed"); return; }
    char a[FO_PATH], b[FO_PATH]; struct stat st;
    if(!path_join(a, sizeof a, g_dir, g_name) || !path_join(b, sizeof b, g_dir, txt)){ ui_toast("Name too long"); return; }
    if(lstat(b, &st) == 0){ ui_toast("Something with that name is already here"); return; }
    int ok = 0;
    if(sd_write_begin()){ ok = rename(a, b) == 0; sync(); sd_write_end(); }
    ui_toast(ok ? "Renamed" : "Rename failed");
    if(ok){ queue_path_moved(a, b); mdb_playlist_paths_moved(a, b); if(g_done) g_done(); ui_rescan_library(); }
}

/* ---------------------------------------------------------------- copy / move: pick a destination */
static char g_pick[FO_PATH];
static int g_pick_op;
static lv_obj_t *g_pick_list, *g_pick_title;
static char (*g_sub)[256]; static int g_nsub;
static void pick_fill(void);
static void pick_row_cb(lv_event_t *e){
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if(i < 0 || i >= g_nsub) return;
    char n[FO_PATH]; if(!path_join(n, sizeof n, g_pick, g_sub[i])) return;
    snprintf(g_pick, sizeof g_pick, "%s", n); pick_fill();
}
static void pick_up_cb(lv_event_t *e){
    (void)e; if(!strcmp(g_pick, FO_ROOT)) return;
    char *sl = strrchr(g_pick, '/'); if(sl && sl > g_pick) *sl = 0; pick_fill();
}
static void pick_cancel_cb(lv_event_t *e){ (void)e; free(g_sub); g_sub = NULL; close_ov(); }
static void do_copy_move(void){
    char src[FO_PATH], dst[FO_PATH];
    if(!path_join(src, sizeof src, g_dir, g_name) || !path_join(dst, sizeof dst, g_pick, g_name)){ ui_toast("Path too long"); return; }
    run(g_pick_op, src, dst, 0);
}
static void do_replace_then(void){
    char src[FO_PATH], dst[FO_PATH];
    if(!path_join(src, sizeof src, g_dir, g_name) || !path_join(dst, sizeof dst, g_pick, g_name)) return;
    run(g_pick_op, src, dst, 1);
}
static void pick_here_cb(lv_event_t *e){
    (void)e;
    char src[FO_PATH], dst[FO_PATH]; struct stat st;
    if(!path_join(src, sizeof src, g_dir, g_name) || !path_join(dst, sizeof dst, g_pick, g_name)){ ui_toast("Path too long"); return; }
    free(g_sub); g_sub = NULL;
    if(!strcmp(src, dst)){ close_ov(); ui_toast("It's already here"); return; }
    size_t sl = strlen(src);
    if(g_is_dir && !strncmp(g_pick, src, sl) && (g_pick[sl] == '/' || g_pick[sl] == 0)){ close_ov(); ui_toast("Can't put a folder inside itself"); return; }
    if(lstat(dst, &st) == 0){
        if(is_playing_inside(dst, S_ISDIR(st.st_mode))){ close_ov(); ui_toast("The destination contains the playing track"); return; }
        char t[320]; snprintf(t, sizeof t, "Replace \"%s\"?", g_name);
        confirm(t, "Something with this name is already there", "Replace", do_replace_then);
        return;
    }
    close_ov(); do_copy_move();
}
static void pick_fill(void){
    lv_obj_clean(g_pick_list);
    const char *base = strcmp(g_pick, FO_ROOT) ? strrchr(g_pick, '/') + 1 : "SD card";
    lv_label_set_text(g_pick_title, base);
    free(g_sub); g_sub = NULL; g_nsub = 0;
    DIR *d = opendir(g_pick); int cap = 0;
    if(d){ struct dirent *e; char c[FO_PATH]; struct stat st;
        while((e = readdir(d)) && g_nsub < 500){
            if(e->d_name[0] == '.') continue;
            if(!path_join(c, sizeof c, g_pick, e->d_name) || stat(c, &st) != 0 || !S_ISDIR(st.st_mode)) continue;
            if(g_nsub == cap){ cap = cap ? cap * 2 : 32; void *q = realloc(g_sub, (size_t)cap * sizeof *g_sub); if(!q) break; g_sub = q; }
            snprintf(g_sub[g_nsub++], 256, "%s", e->d_name);
        }
        closedir(d); }
    if(strcmp(g_pick, FO_ROOT)){                                   /* first row: up a level (clear of the rim) */
        lv_obj_t *r = lv_button_create(g_pick_list);
        lv_obj_remove_style_all(r);
        lv_obj_set_size(r, 240, 40);
        lv_obj_set_style_radius(r, TH_R_ROW, 0);
        lv_obj_set_style_bg_color(r, TC(SURFACE_RAISED), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(r, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_add_event_cb(r, pick_up_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_t *l = lv_label_create(r); lv_label_set_text(l, LV_SYMBOL_UP "   Up one level");
        lv_obj_set_style_text_font(l, TH_F_DETAIL, 0); lv_obj_set_style_text_color(l, TC(TEXT_SECONDARY), 0);
        lv_obj_align(l, LV_ALIGN_LEFT_MID, 12, 0);
    }
    for(int i = 0; i < g_nsub; i++){
        lv_obj_t *r = lv_button_create(g_pick_list);
        lv_obj_remove_style_all(r);
        lv_obj_set_size(r, 240, 40);
        lv_obj_set_style_radius(r, TH_R_ROW, 0);
        lv_obj_set_style_bg_color(r, TC(SURFACE), 0);
        lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(r, TC(SURFACE_RAISED), LV_STATE_PRESSED);
        lv_obj_add_event_cb(r, pick_row_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_t *ic = lv_label_create(r); lv_label_set_text(ic, LV_SYMBOL_DIRECTORY);
        lv_obj_set_style_text_color(ic, TC(FOLDER_ICON), 0); lv_obj_align(ic, LV_ALIGN_LEFT_MID, 12, 0);
        lv_obj_t *l = lv_label_create(r); lv_label_set_text(l, g_sub[i]);
        lv_label_set_long_mode(l, LV_LABEL_LONG_DOT); lv_obj_set_width(l, 190);
        lv_obj_set_style_text_font(l, ui_font_cjk(16), 0); lv_obj_set_style_text_color(l, TC(TEXT_PRIMARY), 0);
        lv_obj_align(l, LV_ALIGN_LEFT_MID, 40, 0);
    }
    if(!g_nsub){ lv_obj_t *l = lv_label_create(g_pick_list); lv_label_set_text(l, "No folders here");
        lv_obj_set_style_text_color(l, TC(TEXT_SECONDARY), 0); }
}
static void small_btn(lv_obj_t *p, const char *txt, int x, int y, int w, uint32_t bg, lv_event_cb_t cb){
    lv_obj_t *b = lv_button_create(p);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, w, 38);
    lv_obj_align(b, LV_ALIGN_TOP_MID, x, y);
    lv_obj_set_style_radius(b, TH_R_PILL, 0);
    lv_obj_set_style_bg_color(b, theme_color_from_rgb(bg), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_ext_click_area(b, 4);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l = lv_label_create(b); lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, TH_F_DETAIL, 0); lv_obj_set_style_text_color(l, TC(TEXT_PRIMARY), 0); lv_obj_center(l);
}
static void pick_copy_here_cb(lv_event_t *e){ g_pick_op = OP_COPY; pick_here_cb(e); }
static void pick_move_here_cb(lv_event_t *e){
    char p[FO_PATH];
    if(path_join(p, sizeof p, g_dir, g_name) && is_playing_inside(p, g_is_dir)){
        free(g_sub); g_sub = NULL; close_ov(); ui_toast(g_is_dir ? "The playing track is in this folder" : "This track is playing"); return; }
    g_pick_op = OP_MOVE; pick_here_cb(e);
}
static void open_picker(int op){
    g_pick_op = op;
    snprintf(g_pick, sizeof g_pick, "%s", g_dir);
    lv_obj_t *o = overlay();
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_t *h = lv_label_create(o);
    lv_label_set_text(h, op == OP_COPY ? "Copy to..." : op == OP_MOVE ? "Move to..." : "Copy or move to...");
    lv_obj_set_style_text_font(h, TH_F_CAPTION, 0); lv_obj_set_style_text_color(h, TC(TEXT_SECONDARY), 0);
    lv_obj_align(h, LV_ALIGN_TOP_MID, 0, 22);
    g_pick_title = lv_label_create(o);
    lv_label_set_long_mode(g_pick_title, LV_LABEL_LONG_DOT); lv_obj_set_width(g_pick_title, 200);
    lv_obj_set_style_text_align(g_pick_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(g_pick_title, ui_font_cjk(18), 0); lv_obj_set_style_text_color(g_pick_title, TC(TEXT_PRIMARY), 0);
    lv_obj_align(g_pick_title, LV_ALIGN_TOP_MID, 0, 40);
    g_pick_list = lv_obj_create(o);
    lv_obj_remove_style_all(g_pick_list);
    lv_obj_set_size(g_pick_list, 250, 190);
    lv_obj_align(g_pick_list, LV_ALIGN_TOP_MID, 0, 76);
    lv_obj_set_flex_flow(g_pick_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(g_pick_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(g_pick_list, 4, 0);
    lv_obj_set_scroll_dir(g_pick_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(g_pick_list, LV_SCROLLBAR_MODE_OFF);
    if(op == OP_BOTH){                                  /* one step for either: Copy here / Move here */
        small_btn(o, "Copy here", -62, 276, 108, TH_ACCENT, pick_copy_here_cb);
        small_btn(o, "Move here", 62, 276, 108, TH_ACCENT, pick_move_here_cb);
        small_btn(o, LV_SYMBOL_CLOSE, 0, 318, 44, TH_SURF1, pick_cancel_cb);
    } else {
        small_btn(o, "Cancel", -62, 276, 108, TH_SURF1, pick_cancel_cb);
        small_btn(o, op == OP_COPY ? "Copy here" : "Move here", 62, 276, 108, TH_ACCENT, pick_here_cb);
    }
    pick_fill();
}

/* ---------------------------------------------------------------- the radial menu */
enum { A_RENAME, A_COPY, A_MOVE, A_DELETE, A_N };
static void pick_action(int i){
    char p[FO_PATH];
    if(!path_join(p, sizeof p, g_dir, g_name)){ close_ov(); ui_toast("Path too long"); return; }
    if(is_playing_inside(p, g_is_dir) && i != A_COPY){
        close_ov(); ui_toast(g_is_dir ? "The playing track is in this folder" : "This track is playing"); return; }
    close_ov();
    switch(i){
        case A_RENAME: kbinput_open("New name", g_name, rename_done); break;
        case A_COPY:   open_picker(OP_COPY); break;
        case A_MOVE:   open_picker(OP_MOVE); break;
        case A_DELETE: ask_delete(); break;
    }
}
static void hub_cb(lv_event_t *e){ (void)e; close_ov(); }
void fileops_open(const char *dir, const char *name, int is_dir, void (*done)(void)){
    if(W.busy){ ui_toast("Still working on the last one"); return; }
    snprintf(g_dir, sizeof g_dir, "%s", dir); snprintf(g_name, sizeof g_name, "%s", name);
    g_is_dir = is_dir; g_done = done;
    lv_obj_t *o = overlay();
    static const orbit_item_t it[A_N] = {
        { LV_SYMBOL_EDIT, "Rename" }, { LV_SYMBOL_COPY, "Copy" }, { LV_SYMBOL_UPLOAD, "Move" }, { LV_SYMBOL_TRASH, "Delete" } };
    orbit_create(&g_orb, o, it, A_N, -90 + 45, pick_action);         /* four buttons on the diagonals */
    if(!th_braun()) lv_obj_set_style_text_color(g_orb.icon[A_DELETE], TC(ACCENT_PRIMARY), 0);
    orbit_hub_create(&g_orb, o, hub_cb, is_dir ? LV_SYMBOL_DIRECTORY : LV_SYMBOL_AUDIO, "Cancel");
    disco_menu_glass(&g_orb, o);                                      /* Disco: glass over the cover backdrop */
    lv_obj_t *n = lv_label_create(o);                                 /* what it's about, at the top */
    lv_label_set_text(n, name);
    lv_label_set_long_mode(n, LV_LABEL_LONG_DOT); lv_obj_set_width(n, 220);
    lv_obj_set_style_text_align(n, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(n, ui_font_cjk(16), 0); lv_obj_set_style_text_color(n, TC(TEXT_PRIMARY), 0);
    lv_obj_align(n, LV_ALIGN_TOP_MID, 0, 24);
}

/* the combined song / folder menus (songmenu.c) call straight into these */
void fileops_action(const char *dir, const char *name, int is_dir, void (*done)(void), int action){
    if(W.busy){ ui_toast("Still working on the last one"); return; }
    snprintf(g_dir, sizeof g_dir, "%s", dir); snprintf(g_name, sizeof g_name, "%s", name);
    g_is_dir = is_dir; g_done = done;
    char p[FO_PATH];
    if(!path_join(p, sizeof p, g_dir, g_name)){ ui_toast("Path too long"); return; }
    if(action != 1 && is_playing_inside(p, g_is_dir)){ ui_toast(g_is_dir ? "The playing track is in this folder" : "This track is playing"); return; }
    if(action == 0) kbinput_open("New name", g_name, rename_done);
    else if(action == 1) open_picker(OP_BOTH);
    else ask_delete();
}
