/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
/* System rows S21-S27. See system.h for the stock facts. The top half is pure/OS-only (host-tested with
 * SYSTEM_CORE_ONLY); the bottom half is the LVGL flow. */
#include "system.h"
#include "version.h"
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/wait.h>

const sys_paths_t SYS_DEV_PATHS = {
    "/etc/product_version/version.in", "/sys/class/net/wlan0/address", "/sys/class/bluetooth/hci0/address",
    "/sys/class/power_supply/cw221X-bat", "/tmp/sdcard", "/usr/data", "/proc/mounts"
};

/* ---------------------------------------------------------------- small helpers */
static int read_small(const char *path, char *buf, size_t n){   /* whole small text file, NUL-terminated, trimmed; 0 ok */
    if(!buf || n < 2) return -1;
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if(fd < 0) return -1;
    ssize_t r = read(fd, buf, n - 1);
    close(fd);
    if(r < 0) return -1;
    buf[r] = 0;
    while(r > 0 && (buf[r-1] == '\n' || buf[r-1] == '\r' || buf[r-1] == ' ')) buf[--r] = 0;
    return 0;
}
static int ends_with_ci(const char *s, const char *suffix){
    size_t a = strlen(s), b = strlen(suffix);
    if(a < b) return 0;
    for(size_t i = 0; i < b; i++) if(tolower((unsigned char)s[a-b+i]) != tolower((unsigned char)suffix[i])) return 0;
    return 1;
}

int sys_kv(const char *text, const char *key, char *out, size_t n){
    size_t kl = strlen(key);
    for(const char *l = text; l && *l; ){
        const char *nl = strchr(l, '\n');
        size_t len = nl ? (size_t)(nl - l) : strlen(l);
        if(len > kl && !strncmp(l, key, kl) && l[kl] == '='){
            size_t vl = len - kl - 1;
            while(vl && (l[kl+1+vl-1] == '\r' || l[kl+1+vl-1] == ' ')) vl--;
            if(vl >= n) vl = n - 1;
            memcpy(out, l + kl + 1, vl); out[vl] = 0;
            return 0;
        }
        l = nl ? nl + 1 : NULL;
    }
    return -1;
}
void sys_fw_label(int ver, char *buf, size_t n){
    if(ver <= 0) snprintf(buf, n, "--");
    else snprintf(buf, n, "V%d.%02d", ver / 100, ver % 100);
}
void sys_fmt_bytes(unsigned long long b, char *buf, size_t n){
    if(b >= (1ULL << 30)) snprintf(buf, n, "%.1f GB", (double)b / (double)(1ULL << 30));
    else if(b >= (1ULL << 20)) snprintf(buf, n, "%.0f MB", (double)b / (double)(1ULL << 20));
    else snprintf(buf, n, "%llu KB", b >> 10);
}

/* ---------------------------------------------------------------- About */
int sys_read_battery(const sys_paths_t *pp){
    const sys_paths_t *p = pp ? pp : &SYS_DEV_PATHS;
    char path[200], b[16];
    snprintf(path, sizeof path, "%s/capacity", p->batt_dir);
    if(read_small(path, b, sizeof b) != 0 || !b[0]) return -1;
    char *e; long v = strtol(b, &e, 10);
    if(*e || v < 0 || v > 100) return -1;
    return (int)v;
}
unsigned long long sys_sd_free(const sys_paths_t *pp){
    const sys_paths_t *p = pp ? pp : &SYS_DEV_PATHS;
    struct statvfs sv;
    if(statvfs(p->sd_root, &sv) != 0) return 0;
    return (unsigned long long)sv.f_bavail * (unsigned long long)sv.f_frsize;
}
static void add_row(sys_about_row_t *rows, int *n, int max, const char *label, const char *fmt, ...) __attribute__((format(printf, 5, 6)));
#include <stdarg.h>
static void add_row(sys_about_row_t *rows, int *n, int max, const char *label, const char *fmt, ...){
    if(*n >= max) return;
    snprintf(rows[*n].label, sizeof rows[*n].label, "%s", label);
    va_list ap; va_start(ap, fmt);
    vsnprintf(rows[*n].value, sizeof rows[*n].value, fmt, ap);
    va_end(ap);
    (*n)++;
}
int sys_is_mounted(const char *mounts, const char *dir){   /* 1 = dir is a mount point listed in mounts (/proc/mounts) */
    FILE *f = fopen(mounts, "r");
    if(!f) return 0;
    char line[512]; int found = 0;
    while(!found && fgets(line, sizeof line, f)){
        char dev[256], mp[256];
        if(sscanf(line, "%255s %255s", dev, mp) == 2 && !strcmp(mp, dir)) found = 1;
    }
    fclose(f);
    return found;
}
int sys_about_collect(const sys_paths_t *pp, const char *diskos_id, sys_about_row_t *rows, int max){
    const sys_paths_t *p = pp ? pp : &SYS_DEV_PATHS;
    int n = 0;
    char vi[512] = "", prod[48] = "", mainv[16] = "", recv[16] = "", mac[32], path[200], b[64];
    read_small(p->version_in, vi, sizeof vi);
    if(sys_kv(vi, "PRODUCT", prod, sizeof prod) != 0) snprintf(prod, sizeof prod, "--");
    for(char *c = prod; *c; c++) if(*c == '_') *c = ' ';
    add_row(rows, &n, max, "Model", "%s", prod);
    int mv = sys_kv(vi, "MAIN_OS_VER", mainv, sizeof mainv) == 0 ? atoi(mainv) : 0;
    int rv = sys_kv(vi, "RECOVERY_OS_VER", recv, sizeof recv) == 0 ? atoi(recv) : 0;
    char fw[16]; sys_fw_label(mv, fw, sizeof fw);
    if(rv > 0) add_row(rows, &n, max, "Firmware", "%s (recovery %d)", fw, rv);
    else       add_row(rows, &n, max, "Firmware", "%s", fw);
    add_row(rows, &n, max, "diskOS", "%s", diskos_id && *diskos_id ? diskos_id : "--");
    add_row(rows, &n, max, "Wi-Fi MAC", "%s", read_small(p->wlan_mac, mac, sizeof mac) == 0 && mac[0] ? mac : "--");
    add_row(rows, &n, max, "Bluetooth MAC", "%s", read_small(p->bt_mac, mac, sizeof mac) == 0 && mac[0] ? mac : "unavailable");
    struct statvfs sv;
    if(sys_is_mounted(p->mounts, p->sd_root) && statvfs(p->sd_root, &sv) == 0 && sv.f_blocks){
        unsigned long long tot = (unsigned long long)sv.f_blocks * sv.f_frsize, fr = (unsigned long long)sv.f_bavail * sv.f_frsize;
        char a[16], c[16]; sys_fmt_bytes(tot - (unsigned long long)sv.f_bfree * sv.f_frsize, a, sizeof a); sys_fmt_bytes(fr, c, sizeof c);
        char t[16]; sys_fmt_bytes(tot, t, sizeof t);
        add_row(rows, &n, max, "SD card", "%s used, %s free of %s", a, c, t);
    } else add_row(rows, &n, max, "SD card", "unavailable");
    if(statvfs(p->data_root, &sv) == 0){
        char c[16]; sys_fmt_bytes((unsigned long long)sv.f_bavail * sv.f_frsize, c, sizeof c);
        add_row(rows, &n, max, "Internal", "%s free", c);
    }
    int pct = sys_read_battery(p);
    char hl[24] = "", cy[16] = "";
    snprintf(path, sizeof path, "%s/health", p->batt_dir);   if(read_small(path, hl, sizeof hl) != 0) hl[0] = 0;
    snprintf(path, sizeof path, "%s/cycle_count", p->batt_dir); if(read_small(path, cy, sizeof cy) != 0) cy[0] = 0;
    if(pct >= 0) snprintf(b, sizeof b, "%d%%", pct); else snprintf(b, sizeof b, "--");
    if(hl[0]){ size_t l = strlen(b); snprintf(b + l, sizeof b - l, ", %s", hl); }
    if(cy[0]){ size_t l = strlen(b); snprintf(b + l, sizeof b - l, ", %s cycles", cy); }
    add_row(rows, &n, max, "Battery", "%s", b);
    return n;
}

/* ---------------------------------------------------------------- local update: detection */
int sys_update_name_ok(const char *name){
    if(!name || !*name || strlen(name) >= sizeof(((sys_update_t *)0)->name)) return 0;
    if(name[0] == '.' && name[1] == '_') return 0;             /* macOS resource forks (stock skips "._*") */
    if(!strstr(name, SYS_UPDATE_PRODUCT)) return 0;            /* stock: strstr(name, product) */
    return ends_with_ci(name, ".zip");
}
int sys_update_version(const char *name){
    if(!name) return -1;
    for(const char *v = strchr(name, 'v'); v; v = strchr(v + 1, 'v')){
        const char *d = v + 1; int n = 0;
        if(!isdigit((unsigned char)*d)) continue;
        while(isdigit((unsigned char)*d)){ if(n > 100000) return -1; n = n * 10 + (*d - '0'); d++; }
        if(ends_with_ci(d, ".zip") && (d[4] == 0)) return n;   /* stock: sscanf(strchr(name,'v'), "v%d.zip") */
    }
    return -1;
}
int sys_find_update(const char *dir, sys_update_t *out){
    DIR *d = opendir(dir);
    if(!d) return 0;
    int found = 0;
    struct dirent *e;
    while((e = readdir(d))){
        if(!sys_update_name_ok(e->d_name)) continue;
        int ver = sys_update_version(e->d_name);
        if(ver <= 0) continue;
        sys_update_t c; memset(&c, 0, sizeof c);
        if(snprintf(c.path, sizeof c.path, "%s/%s", dir, e->d_name) >= (int)sizeof c.path) continue;
        struct stat st;
        if(stat(c.path, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0) continue;
        snprintf(c.name, sizeof c.name, "%.*s", (int)sizeof c.name - 1, e->d_name);
        c.version = ver; c.size = (long long)st.st_size;
        if(!found || c.version > out->version || (c.version == out->version && strcmp(c.name, out->name) > 0)){ *out = c; found = 1; }
    }
    closedir(d);
    return found;
}
/* Battery and free space are shown as information only; they never gate anything (diskOS starts no update). */

/* ---------------------------------------------------------------- check for diskOS updates */
static int parse_ver(const char *s, int v[3]){
    v[0] = v[1] = v[2] = 0;
    if(*s == 'v' || *s == 'V') s++;
    if(!isdigit((unsigned char)*s)) return -1;
    for(int i = 0; i < 3; i++){
        long x = 0; int digits = 0;
        while(isdigit((unsigned char)*s)){ x = x * 10 + (*s - '0'); if(x > 100000) return -1; s++; digits++; }
        if(!digits) return -1;
        v[i] = (int)x;
        if(*s == '.') { s++; continue; }
        break;
    }
    return 0;
}
int sys_ver_cmp(const char *a, const char *b){
    int x[3], y[3];
    if(!a || !b || parse_ver(a, x) != 0 || parse_ver(b, y) != 0) return -2;
    for(int i = 0; i < 3; i++) if(x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
    return 0;
}
int sys_parse_tag(const char *json, char *out, size_t n){
    if(!json || !out || n < 2) return -1;
    const char *k = strstr(json, "\"tag_name\"");
    if(!k) return -1;
    k += 10;
    while(*k == ' ' || *k == '\t' || *k == '\n') k++;
    if(*k != ':') return -1;
    k++;
    while(*k == ' ' || *k == '\t' || *k == '\n') k++;
    if(*k != '"') return -1;
    k++;
    size_t i = 0;
    while(*k && *k != '"'){
        if(!(isalnum((unsigned char)*k) || *k == '.' || *k == '-' || *k == '_') || i + 1 >= n) return -1;
        out[i++] = *k++;
    }
    if(*k != '"' || i == 0) return -1;
    out[i] = 0;
    return 0;
}
/* ---------------------------------------------------------------- reset diskOS settings */
/* Only the diskOS UI's own look-and-behaviour keys. Deliberately NOT here: keys that mirror the stock player's state
 * (audio_*, eq_*, eqN_*, work_mode, volume, max_vol, balance, replay_gain, gapless, folder_jump, artist_class,
 * charge_protect, auto_time, tz_idx, memory_play, language), radio intents (wifi_on, bt_on), Last.fm accounts
 * (lastfm_*) and stock_power_save (the user's stock idle value that diskOS parked). Resetting those would change the
 * config without changing the player, or drop an account. Wi-Fi/Bluetooth pairings and the SD card are outside
 * diskos.conf entirely. */
static const char *const RESET_KEYS[] = {
    "brightness", "swipe_thresh", "saver_idx", "saver_style", "screenoff_idx",
    "anim", "time_24h", "disc_colour", "accent_mode", "accent_color", "np_style", "theme_preset", "theme_variant",
    "outdoor", "boot_default", "weather_on", "weather_loc", "artcache", "sleep_idx", "sleep_action", "idle_off_idx",
    "qs_transport", "track_numbers", "album_view", "online_art", "online_lyrics",
    "up_next", "ui_theme", "theme_auto", "np_poster_v1", "saver_ring_v1", "accent_red_v1", NULL
};
int sys_reset_match(const char *key){
    if(!key) return 0;
    for(int i = 0; RESET_KEYS[i]; i++) if(!strcmp(key, RESET_KEYS[i])) return 1;
    return 0;
}
int sys_reset_settings(void){ return cfg_remove_if(sys_reset_match); }

#ifndef SYSTEM_CORE_ONLY
/* ================================================================ LVGL flow */
#include "screens.h"
#include "theme.h"
#include "theme_kit.h"
#include "scanner.h"
#include "ota.h"
#include "sdio.h"
#include "fork_build.h"
#include "i18n.h"
#include <pthread.h>
#include <stdatomic.h>

void settings_diskos_id(char *buf, int n);    /* settings.c */
void settings_restart_prompt(const char *note);           /* settings.c */

static lv_obj_t *g_m;                          /* the one open modal (top layer) */
static void modal_close(void){ if(g_m){ lv_obj_delete_async(g_m); g_m = NULL; } }
static void cb_close(lv_event_t *e){ if(lv_event_get_code(e) == LV_EVENT_CLICKED){ modal_close(); } }

static lv_obj_t *modal_card(const char *title, int h){
    modal_close();
    g_m = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(g_m);
    lv_obj_set_size(g_m, 360, 360); lv_obj_center(g_m);
    lv_obj_set_style_bg_color(g_m, TC(SCRIM), 0);
    lv_obj_set_style_bg_opa(g_m, LV_OPA_70, 0);
    lv_obj_clear_flag(g_m, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_m, LV_OBJ_FLAG_CLICKABLE);        /* absorb taps */
    lv_obj_t *card = lv_obj_create(g_m);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, 296, h); lv_obj_center(card);
    lv_obj_set_style_radius(card, 18, 0);
    lv_obj_set_style_bg_color(card, TC(SURFACE), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *t = lv_label_create(card);
    lv_label_set_text(t, title);
    lv_obj_set_style_text_font(t, TF(UI_16), 0);
    lv_obj_set_style_text_color(t, TC(TEXT_PRIMARY), 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 16);
    return card;
}
static lv_obj_t *modal_text(lv_obj_t *card, const char *txt, int y, theme_color_role_t col){
    lv_obj_t *s = lv_label_create(card);
    lv_label_set_text(s, txt);
    lv_label_set_long_mode(s, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s, 256);
    lv_obj_set_style_text_align(s, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(s, TF(UI_14), 0);
    lv_obj_set_style_text_color(s, theme_color(col), 0);
    lv_obj_align(s, LV_ALIGN_TOP_MID, 0, y);
    return s;
}
static void modal_pill(lv_obj_t *card, int x, const char *txt, theme_color_role_t col, lv_event_cb_t cb, const char *action){
    lv_obj_t *b = lv_button_create(card);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, 108, 40); lv_obj_align(b, LV_ALIGN_BOTTOM_MID, x, -14);
    lv_obj_set_ext_click_area(b, 4);
    lv_obj_set_style_radius(b, 12, 0);
    lv_obj_set_style_bg_color(b, TC(SURFACE_RAISED), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    ui_on(b, cb, LV_EVENT_CLICKED, NULL, action, UI_CORE);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, TF(UI_16), 0);
    lv_obj_set_style_text_color(l, theme_color(col), 0);
    lv_obj_center(l);
}
static void modal_simple(const char *title, const char *body){          /* one "OK" */
    lv_obj_t *c = modal_card(title, 190);
    lv_obj_t *s = modal_text(c, body, 46, THEME_CLR_TEXT_MUTED);
    /* grow the card to fit the wrapped text above the pill (46 top + text + 12 gap + 40 pill + 14 margin), so a
     * long or translated message (up to ~10 lines) clears OK; capped at 300 like About so it stays on the round screen */
    lv_obj_update_layout(s);
    int32_t h = 46 + lv_obj_get_height(s) + 12 + 40 + 14;
    if(h > 190) { lv_obj_set_height(c, h > 300 ? 300 : h); lv_obj_center(c); }
    modal_pill(c, 0, "OK", THEME_CLR_ACCENT_EMPHASIS, cb_close, "system.ok");
}

/* ---- About ---- */
void system_about_open(int v){
    (void)v;
    sys_about_row_t rows[12];
    char id[40]; settings_diskos_id(id, sizeof id);
    int n = sys_about_collect(NULL, id, rows, 12);
    int rb = ota_rollback_take(&OTA_DEV);                    /* boot rolled a failed update back: tell the user once */
    lv_obj_t *card = modal_card("About", 300);
    if(rb){
        char note[80]; snprintf(note, sizeof note, "Update failed to start - back on %s", DISKOS_VERSION);
        modal_text(card, note, 40, THEME_CLR_STATUS_WARNING);
    }
    lv_obj_t *list = lv_obj_create(card);
    lv_obj_remove_style_all(list);
    lv_obj_set_size(list, 272, rb ? 150 : 190); lv_obj_align(list, LV_ALIGN_TOP_MID, 0, rb ? 84 : 44);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 6, 0);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);
    for(int i = 0; i < n; i++){
        lv_obj_t *l = lv_label_create(list);
        lv_label_set_text(l, rows[i].label);
        lv_obj_set_style_text_font(l, TF(UI_12), 0);
        lv_obj_set_style_text_color(l, TC(TEXT_MUTED), 0);
        lv_obj_t *v = lv_label_create(list);
        lv_label_set_text(v, rows[i].value);
        lv_label_set_long_mode(v, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(v, 268);
        lv_obj_set_style_text_font(v, TF(UI_14), 0);
        lv_obj_set_style_text_color(v, TC(TEXT_PRIMARY), 0);
    }
    modal_pill(card, 0, "Close", THEME_CLR_ACCENT_EMPHASIS, cb_close, "system.about_close");
}

/* ---- Update from SD card: information only. diskOS never starts a firmware update. ---- */
static void sd_stock_update_info(void){
    sys_update_t u; memset(&u, 0, sizeof u);
    if(!sys_is_mounted(SYS_DEV_PATHS.mounts, SYS_DEV_PATHS.sd_root) || !sys_find_update(SYS_DEV_PATHS.sd_root, &u)){ ui_toast("No update file found on the SD card"); return; }
    char body[300], ver[16]; sys_fw_label(u.version, ver, sizeof ver);
    snprintf(body, sizeof body, "Found a file named like a stock update (%s, unchecked). diskOS does not use it. A stock update replaces diskOS: set Default UI to stock, run stock's updater, then reinstall diskOS with the installer.", ver);
    lv_obj_t *c = modal_card("Update file", 290);
    modal_text(c, body, 46, THEME_CLR_STATUS_WARNING);
    modal_pill(c, 0, "OK", THEME_CLR_ACCENT_EMPHASIS, cb_close, "system.ok");
}

/* ---- Check for diskOS updates, then (when the release carries the signed bundle) download + verify + stage ---- */
static struct { pthread_t th; _Atomic int state; char tag[40]; int bundle; } g_chk;   /* state: 0 idle, 1 running, 2 ok, 3 failed; tag/bundle are written before the release store of 2 */
static lv_timer_t *g_chk_tm;
static void *chk_thread(void *a){ (void)a; int ok = ota_fetch_release(&OTA_DEV, g_chk.tag, sizeof g_chk.tag, &g_chk.bundle) == 0; atomic_store_explicit(&g_chk.state, ok ? 2 : 3, memory_order_release); return NULL; }

/* the download/verify worker: state 0 idle, 1 running, 2 finished (rc is written before the release store of 2) */
static struct { pthread_t th; _Atomic int state; ota_prog_t pg; char tag[40]; int rc; int from_sd; } g_dl;
static lv_timer_t *g_dl_tm;
static lv_obj_t *g_dl_lbl;
static void *dl_thread(void *a){ (void)a; g_dl.rc = ota_stage(&OTA_DEV, DISKOS_VERSION, g_dl.tag, &g_dl.pg); atomic_store_explicit(&g_dl.state, 2, memory_order_release); return NULL; }
#define SD_UPDATE_DIR "diskos-update"            /* <SD>/diskos-update/: the six files made by the owner's signing script */
static void *sd_stage_thread(void *a){
    (void)a;
    char src[200]; snprintf(src, sizeof src, "%s/" SD_UPDATE_DIR, SYS_DEV_PATHS.sd_root);
    if(sd_io_begin()){ g_dl.rc = ota_stage_dir(&OTA_DEV, src, &g_dl.pg); sd_io_end(); }
    else g_dl.rc = OTA_E_IO;
    atomic_store_explicit(&g_dl.state, 2, memory_order_release);
    return NULL;
}
static void dl_status(void){
    if(!g_dl_lbl || !g_m || !lv_obj_is_valid(g_dl_lbl)) return;
    char t[64]; int ph = atomic_load(&g_dl.pg.phase);
    long long tot = atomic_load(&g_dl.pg.total), done = atomic_load(&g_dl.pg.done);   /* long -> long long */
    if(atomic_load(&g_dl.pg.cancel) && ph < 3) snprintf(t, sizeof t, "Cancelling...");
    else if(ph == 2) snprintf(t, sizeof t, "Verifying...");
    else if(ph == 3) snprintf(t, sizeof t, "Saving...");
    else if(tot > 0) snprintf(t, sizeof t, "%s... %d%%", g_dl.from_sd ? "Copying" : "Downloading", (int)(done * 100 / tot > 100 ? 100 : done * 100 / tot));
    else snprintf(t, sizeof t, "%s...", g_dl.from_sd ? "Copying" : "Downloading");
    lv_label_set_text(g_dl_lbl, t);
}
static void dl_tick(lv_timer_t *t){
    if(atomic_load_explicit(&g_dl.state, memory_order_acquire) < 2){ dl_status(); return; }
    pthread_join(g_dl.th, NULL);
    lv_timer_delete(t); g_dl_tm = NULL; g_dl_lbl = NULL;
    int rc = g_dl.rc; char body[120];
    atomic_store(&g_dl.state, 0);
    modal_close();
    if(rc == OTA_OK){
        snprintf(body, sizeof body, "diskOS %s is ready. Restart to finish updating.", g_dl.pg.version);
        settings_restart_prompt(body);
    } else if(rc == OTA_E_CANCEL) ui_toast("Update cancelled");
    else if(rc == OTA_E_SPACE){
        snprintf(body, sizeof body, "%s (need %d MB free).", ota_err_text(rc), (int)((g_dl.pg.need_free + (1 << 20) - 1) >> 20));
        modal_simple("Update failed", body);
    } else modal_simple("Update failed", ota_err_text(rc));
}
static void cb_dl_cancel(lv_event_t *e){ if(lv_event_get_code(e) == LV_EVENT_CLICKED) atomic_store(&g_dl.pg.cancel, 1); }
static void cb_ota_go(lv_event_t *e){
    if(lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    int idle = 0;
    if(!atomic_compare_exchange_strong(&g_dl.state, &idle, 1)){ ui_toast("Already updating"); return; }
    memset(&g_dl.pg, 0, sizeof g_dl.pg); g_dl.from_sd = 0;
    snprintf(g_dl.tag, sizeof g_dl.tag, "%s", g_chk.tag);
    if(pthread_create(&g_dl.th, NULL, dl_thread, NULL) != 0){ atomic_store(&g_dl.state, 0); modal_close(); ui_toast("Couldn't start the update"); return; }
    lv_obj_t *c = modal_card("Updating diskOS", 190);
    g_dl_lbl = modal_text(c, "Downloading...", 60, THEME_CLR_TEXT_MUTED);
    modal_pill(c, 0, "Cancel", THEME_CLR_TEXT_SECONDARY, cb_dl_cancel, "system.ota_cancel");
    g_dl_tm = lv_timer_create(dl_tick, 200, NULL);
}
/* Settings > System > Update from SD Card. Fork: a signed diskOS update in <SD>/diskos-update/ is copied, verified with the
 * root key baked into this install and staged; boot installs it as a trial (rolled back if it never proves healthy).
 * Without such a folder the original stock-update information is shown. */
static void cb_sd_go(lv_event_t *e){
    if(lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    modal_close();
    int idle = 0;
    if(!atomic_compare_exchange_strong(&g_dl.state, &idle, 1)){ ui_toast("Already updating"); return; }
    memset(&g_dl.pg, 0, sizeof g_dl.pg); g_dl.from_sd = 1; g_dl.tag[0] = 0;
    if(pthread_create(&g_dl.th, NULL, sd_stage_thread, NULL) != 0){ atomic_store(&g_dl.state, 0); ui_toast("Couldn't start the update"); return; }
    lv_obj_t *c = modal_card("Updating diskOS", 190);
    g_dl_lbl = modal_text(c, "Copying...", 60, THEME_CLR_TEXT_MUTED);
    modal_pill(c, 0, "Cancel", THEME_CLR_TEXT_SECONDARY, cb_dl_cancel, "system.sd_cancel");
    g_dl_tm = lv_timer_create(dl_tick, 200, NULL);
}
void system_update_sd_open(int v){
    (void)v;
    char m[220]; snprintf(m, sizeof m, "%s/" SD_UPDATE_DIR "/update.manifest", SYS_DEV_PATHS.sd_root);
    if(!sys_is_mounted(SYS_DEV_PATHS.mounts, SYS_DEV_PATHS.sd_root) || access(m, R_OK) != 0){ sd_stock_update_info(); return; }
    if(!ota_supported(&OTA_DEV)){ modal_simple("Update from SD Card", "Found a diskOS update, but this install has no update key. Flash diskOS once with your key (diskos-root.pub.pem in the installer's payload folder)."); return; }
    if(atomic_load(&g_dl.state) != 0){ ui_toast("Already updating"); return; }
    lv_obj_t *c = modal_card("Update from SD Card", 220);
    modal_text(c, "Found a diskOS update on the SD card. Verify and stage it? It is installed at the next restart and rolled back automatically if it doesn't work.", 46, THEME_CLR_TEXT_MUTED);
    modal_pill(c, -58, "Cancel", THEME_CLR_TEXT_SECONDARY, cb_close, "system.sd_later");
    modal_pill(c,  58, "Update", THEME_CLR_ACCENT_EMPHASIS, cb_sd_go, "system.sd_go");
}
static void chk_tick(lv_timer_t *t){
    int st = atomic_load_explicit(&g_chk.state, memory_order_acquire);
    if(st < 2) return;
    pthread_join(g_chk.th, NULL);
    lv_timer_delete(t); g_chk_tm = NULL;
    if(st == 3){ atomic_store(&g_chk.state, 0); modal_simple("Check failed", "Couldn't reach GitHub. Check the Wi-Fi connection and try again."); return; }
    char body[260]; int c = sys_ver_cmp(DISKOS_VERSION, g_chk.tag);
    atomic_store(&g_chk.state, 0);
    if(c == -1 && g_chk.bundle && ota_supported(&OTA_DEV) && !ota_allowed()){    /* reported, never downloaded */
        snprintf(body, sizeof body, tr("diskOS %s is available (you have %s). Updates are turned off in Settings. Turn on Allow diskOS Updates, or install it with the installer from github.com/b0hemia/diskos."), g_chk.tag, DISKOS_VERSION);
        modal_simple(tr("diskOS updates"), body);
        return;
    }
    if(c == -1 && g_chk.bundle && ota_supported(&OTA_DEV)){
        snprintf(body, sizeof body, "diskOS %s is available (you have %s). Download and install it now?", g_chk.tag, DISKOS_VERSION);
        lv_obj_t *card = modal_card("diskOS update", 210);
        modal_text(card, body, 46, THEME_CLR_TEXT_MUTED);
        modal_pill(card, -58, "Later", THEME_CLR_TEXT_SECONDARY, cb_close, "system.ota_later");
        modal_pill(card,  58, "Update", THEME_CLR_ACCENT_EMPHASIS, cb_ota_go, "system.ota_go");
        return;
    }
    if(c == -1) snprintf(body, sizeof body, "diskOS %s is available (you have %s), but it has no in-app update. Download it from github.com/b0hemia/diskos and install it with the installer.", g_chk.tag, DISKOS_VERSION);
    else if(c == 0) snprintf(body, sizeof body, "You have the latest release (%s).", DISKOS_VERSION);
    else if(c == 1) snprintf(body, sizeof body, "You have %s, newer than the latest release (%s).", DISKOS_VERSION, g_chk.tag);
    else snprintf(body, sizeof body, "Latest release: %s. Couldn't compare it with %s.", g_chk.tag, DISKOS_VERSION);
    modal_simple("diskOS updates", body);
}
/* Promotion proof for a TRIAL build (contract 4a). Navigation alone never promotes. After >= 3 minutes up, the trial is
 * kept when (a) a real playback start was confirmed (main.c: g_play_pending cleared by a genuine track change) AND a
 * playback PCM substream is RUNNING, or (b) the user answers the one-time "Keep diskOS <ver>?" prompt with Keep. "Go back"
 * writes the revert request marker (boot rolls the trial back) and offers the restart. No proof = never promoted: the
 * boot's trial counter keeps running and rolls it back. The identity checks (running exe hash, run + launched files) are
 * in ota_health_write / ota_revert_write. */
#define OTA_HEALTH_MIN_MS 180000
static struct { uint32_t t0; _Atomic int play, ok, asked; char ver[24]; } g_hl;
static lv_timer_t *g_hl_tm;
static void *ota_health_thread(void *a){ (void)a; ota_health_write(&OTA_DEV); return NULL; }
static void cb_keep(lv_event_t *e){
    if(lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    modal_close();
    if(!ota_health_write(&OTA_DEV)) ui_toast(tr("Couldn't save your choice. Try again."));
}
static void cb_goback(lv_event_t *e){
    if(lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    modal_close();
    if(ota_revert_write(&OTA_DEV)) settings_restart_prompt(tr("Restart to go back to the previous version."));
    else ui_toast(tr("Couldn't save your choice. Try again."));
}
static void ota_keep_prompt(void){
    char t[64]; snprintf(t, sizeof t, tr("Keep diskOS %s?"), g_hl.ver);
    lv_obj_t *c = modal_card(t, 230);
    modal_text(c, tr("This update has been running for a few minutes. Keep it, or go back to the previous version (the device restarts)."), 50, THEME_CLR_TEXT_MUTED);
    modal_pill(c, -58, tr("Go back"), THEME_CLR_STATUS_DANGER, cb_goback, "system.ota_goback");
    modal_pill(c,  58, tr("Keep"), THEME_CLR_ACCENT_EMPHASIS, cb_keep, "system.ota_keep");
}
static void ota_health_cb(lv_timer_t *t){
    if(lv_tick_elaps(g_hl.t0) < OTA_HEALTH_MIN_MS) return;
    if(!atomic_load(&g_hl.ok) && atomic_load(&g_hl.play) && ota_pcm_running(&OTA_DEV)) atomic_store(&g_hl.ok, 1);
    if(atomic_load(&g_hl.ok)){
        pthread_t th;
        if(pthread_create(&th, NULL, ota_health_thread, NULL) == 0) pthread_detach(th);
        lv_timer_delete(t); g_hl_tm = NULL;
        return;
    }
    if(atomic_load(&g_hl.asked)) return;
    atomic_store(&g_hl.asked, 1);
    ota_keep_prompt();
    lv_timer_delete(t); g_hl_tm = NULL;
}
void system_ota_note_playback(void){ atomic_store(&g_hl.play, 1); }
void system_ota_health_init(void){
    if(!ota_trial_version(&OTA_DEV, g_hl.ver, sizeof g_hl.ver)) return;     /* only a trial build has anything to prove */
    g_hl.t0 = lv_tick_get();
    g_hl_tm = lv_timer_create(ota_health_cb, 2000, NULL);
}
/* Settings > System > Allow diskOS Updates was switched (set_val already saved cfg ota_allow). Turning it OFF while a verified
 * bundle waits would still let boot install it, so OFF then needs the bundle discarded: the switch goes back to ON and the
 * user is asked. Discard removes pending/ and turns the setting OFF; Cancel leaves both as they were. With nothing staged the
 * switch simply stays OFF. The running build is never touched. */
static void cb_discard_go(lv_event_t *e){
    if(lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    modal_close();
    if(ota_discard_pending(&OTA_DEV) && cfg_set_int("ota_allow", 0) == 0) ui_toast(tr("Update discarded. Updates are off."));
    else ui_toast(tr("Couldn't discard the update"));
    settings_row_values_refresh();
}
void system_ota_allow_changed(int v){
    char pv[40];
    if(v || !ota_pending(&OTA_DEV, pv, sizeof pv)) return;
    cfg_set_int("ota_allow", 1);                                  /* stays ON until the user confirms the discard */
    lv_obj_t *c = modal_card(tr("Turn off updates?"), 230);
    modal_text(c, tr("Turning updates off discards the downloaded update."), 46, THEME_CLR_TEXT_MUTED);
    modal_pill(c, -58, tr("Cancel"), THEME_CLR_TEXT_SECONDARY, cb_close, "system.ota_off_cancel");
    modal_pill(c,  58, tr("Discard"), THEME_CLR_STATUS_DANGER, cb_discard_go, "system.ota_discard");
}
void system_check_update_open(int v){
    (void)v;
    if(!DISKOS_FORK_OTA_ENABLED){ modal_simple("Updates", "Automatic updates for this version are not available yet."); return; }
    char pv[40];
    if(atomic_load(&g_dl.state) != 0){ ui_toast("Already updating"); return; }
    if(ota_allowed() && ota_pending(&OTA_DEV, pv, sizeof pv)){   /* a verified bundle is already staged: only a restart is left */
        char n[120]; snprintf(n, sizeof n, "diskOS %s is ready. Restart to finish updating.", pv);
        settings_restart_prompt(n);
        return;
    }
    if(ota_wifi_down(&OTA_DEV)){ modal_simple("Check failed", "Wi-Fi is not connected. Connect to Wi-Fi and try again."); return; }
    int idle = 0;
    if(!atomic_compare_exchange_strong(&g_chk.state, &idle, 1)){ ui_toast("Already checking"); return; }
    if(pthread_create(&g_chk.th, NULL, chk_thread, NULL) != 0){ atomic_store(&g_chk.state, 0); ui_toast("Couldn't start the check"); return; }
    g_chk_tm = lv_timer_create(chk_tick, 300, NULL);
    ui_toast("Checking for updates...");
}

/* ---- Reset diskOS settings ---- */
static void cb_reset_go(lv_event_t *e){
    if(lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    modal_close();
    int rc = sys_reset_settings();
    if(rc == -1){ ui_toast("Couldn't reset - nothing changed"); return; }
    settings_restart_prompt(rc == -2 ? "Settings reset, but may not be saved. Restart to check." : NULL);
}
void system_reset_open(int v){
    (void)v;
    lv_obj_t *c = modal_card("Reset diskOS settings?", 250);
    modal_text(c, "Look and behaviour settings go back to defaults, then diskOS restarts. Music, Wi-Fi, Bluetooth, Last.fm, EQ and audio settings are kept.", 46, THEME_CLR_TEXT_MUTED);
    modal_pill(c, -58, "Cancel", THEME_CLR_TEXT_SECONDARY, cb_close, "system.reset_cancel");
    modal_pill(c,  58, "Reset", THEME_CLR_STATUS_DANGER, cb_reset_go, "system.reset_go");
}
#endif
