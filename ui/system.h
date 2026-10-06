/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
/* System rows S21-S27: About device, "Update from SD Card" (information only), "check for diskOS updates", diskOS
 * settings reset. diskOS never starts a firmware update: stock's local update (mq_player udisk_upgrade 0x42b6a4:
 * unzip <mount>/SNOWSKY_DISC_update_images, then /etc/ota_bin/network_main_os_update_recovery.sh) replaces the whole
 * rootfs including diskOS, so the row only finds the zip, shows its version and explains the way to install it
 * (Default UI = stock, stock's updater, then the diskOS installer). */
#ifndef DISKOS_SYSTEM_H
#define DISKOS_SYSTEM_H
#include <stddef.h>
#include <sys/types.h>

#define SYS_UPDATE_PRODUCT   "SNOWSKY_DISC"
/* Where diskOS looks for its own updates. Release builds use the public repo. A device-qualification build (never
 * shipped) may point at a test repo with EXTRA_CFLAGS -DDISKOS_OTA_REPO=\"owner/repo\", or at a GitHub-compatible
 * server (e.g. Forgejo) by overriding both SYS_GITHUB_LATEST and SYS_GITHUB_DOWNLOAD. */
#ifndef DISKOS_OTA_REPO
#define DISKOS_OTA_REPO      "b0hemia/diskos"
#endif
#ifndef SYS_GITHUB_LATEST
#define SYS_GITHUB_LATEST    "https://api.github.com/repos/" DISKOS_OTA_REPO "/releases/latest"
#endif
#ifndef SYS_GITHUB_DOWNLOAD
#define SYS_GITHUB_DOWNLOAD  "https://github.com/" DISKOS_OTA_REPO "/releases/download"
#endif

/* ---- device info (About): every source is read-only ---- */
typedef struct {
    const char *version_in;   /* /etc/product_version/version.in */
    const char *wlan_mac;     /* /sys/class/net/wlan0/address */
    const char *bt_mac;       /* /sys/class/bluetooth/hci0/address (absent while Bluetooth is off) */
    const char *batt_dir;     /* /sys/class/power_supply/cw221X-bat */
    const char *sd_root;      /* /tmp/sdcard */
    const char *data_root;    /* /usr/data */
    const char *mounts;       /* /proc/mounts */
} sys_paths_t;
extern const sys_paths_t SYS_DEV_PATHS;

typedef struct { char label[20]; char value[64]; } sys_about_row_t;
/* Fills up to `max` rows; returns the count. `diskos_id` is the diskOS version+build text (settings.c read_about). */
int  sys_about_collect(const sys_paths_t *p, const char *diskos_id, sys_about_row_t *rows, int max);
int  sys_is_mounted(const char *mounts, const char *dir);   /* 1 = dir is a mount point in mounts */
int  sys_kv(const char *text, const char *key, char *out, size_t n);   /* KEY=value line lookup; 0 found */
void sys_fw_label(int ver, char *buf, size_t n);                        /* 257 -> "V2.57", <= 0 -> "--" */
void sys_fmt_bytes(unsigned long long b, char *buf, size_t n);          /* "1.5 GB" */

/* ---- local update from the SD card ---- */
typedef struct { char path[320]; char name[128]; int version; long long size; } sys_update_t;
int  sys_update_name_ok(const char *name);        /* stock's match: contains SNOWSKY_DISC, not "._*"; and ends in .zip */
int  sys_update_version(const char *name);        /* "v257.zip" -> 257; -1 if the name carries no version */
int  sys_find_update(const char *dir, sys_update_t *out);   /* 1 = found (highest version), 0 = none */
/* 0 = ok to start; else a code and a one-line reason. batt < 0 = unreadable (refused). */
int  sys_read_battery(const sys_paths_t *p);      /* percent or -1 */
unsigned long long sys_sd_free(const sys_paths_t *p);

/* ---- check for diskOS updates (read-only) ---- */
int  sys_parse_tag(const char *json, char *out, size_t n);   /* the release "tag_name"; 0 ok */
int  sys_ver_cmp(const char *a, const char *b);              /* -1 a<b, 0 equal, 1 a>b, -2 unparsable */

/* ---- reset diskOS settings ---- */
int  sys_reset_match(const char *key);   /* 1 = a diskOS UI setting that "Reset diskOS settings" clears */
int  sys_reset_settings(void);           /* 0 ok, -1 nothing changed, -2 file changed but durability unconfirmed (memory reloaded from disk) */

#ifndef SYSTEM_CORE_ONLY
/* Settings rows (settings.c). Each takes the row's (unused) value argument. */
void system_about_open(int v);
void system_update_sd_open(int v);
void system_check_update_open(int v);
void system_ota_allow_changed(int v);  /* row hook: OFF with a staged update asks to discard it (switch stays ON until Discard) */
void settings_row_values_refresh(void);  /* settings.c: re-read every visible row value */
void system_reset_open(int v);
void system_ota_note_playback(void); /* main.c: a real playback start was confirmed */
void system_ota_health_init(void);   /* called once from main.c when the UI is up (OTA health marker) */
#endif
#endif
