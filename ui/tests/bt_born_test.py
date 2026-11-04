#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Born on the right codec (2.6.2), including the 2.6.2 b2 review's scenarios: the running BlueALSA's real arguments
are read from /proc (not a remembered intention); restarts happen only when the kernel shows no connection; the LDAC
quality counts; other headphones get the normal order before connecting. Production functions from bt.c on fake
/proc and /sys trees."""
import subprocess, tempfile
from pathlib import Path
ui = Path(__file__).resolve().parents[1]; src = (ui / "bt.c").read_text()
def fn(sig):
    start = src.index(sig)
    while src[start:src.index("\n", start)].split("/*")[0].rstrip().endswith(";"): start = src.index(sig, start + 1)
    o = src.index("{", start); d = 1; e = o + 1
    while d: d += (src[e] == "{") - (src[e] == "}"); e += 1
    return src[start:e]
def block(a, b): return src[src.index(a):src.index(b)]
with tempfile.TemporaryDirectory() as d:
    d = Path(d); proc = d / "proc"; sysbt = d / "sysbt"; proc.mkdir(); sysbt.mkdir()
    C = r'''
#include <assert.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#define BT_PROC_DIR "%s"
#define BT_SYSBT_DIR "%s"
#define TD "%s"
#define BT_CODEC_N 7
#define BTC_SBC  1u
#define BTC_AAC  2u
#define BTC_LDAC 4u
#define BTC_APTX 8u
#define BTC_APTXHD 16u
#include <unistd.h>
#define BT_APTX_BINS "%s/aptx/bluealsa"
static const char *bt_alsa_aptx_bin(void);
static int fw = 257, codec = 0, restarts; static char last[20], keys[16][32], last_cmd[700]; static int vals[16], nk;
static int fw_os_ver(void){ return fw; }
static const char *cfg_get_str(const char *k, const char *d){ return !strcmp(k, "bt_last") ? last : d; }
static int cfg_get_int(const char *k, int d){ if(!strcmp(k, "bt_codec")) return codec;
    for(int i = 0; i < nk; i++) if(!strcmp(keys[i], k)) return vals[i]; return d; }
static void cfg_set_int(const char *k, int v){ for(int i = 0; i < nk; i++) if(!strcmp(keys[i], k)){ vals[i] = v; return; }
    snprintf(keys[nk], 32, "%%s", k); vals[nk++] = v; }
static int real_system(const char *c){ return system(c); }
static int system_(const char *c){ snprintf(last_cmd, sizeof last_cmd, "%%s", c); if(strstr(c, "killall bluealsa")) restarts++; return 0; }
#define system system_
static void daemon_(const char *args){   /* fake a running bluealsa with these arguments (or none) */
    char p[512]; snprintf(p, sizeof p, "rm -rf %%s/4242; mkdir -p %%s/4242", BT_PROC_DIR, BT_PROC_DIR); if(real_system(p)){}
    restarts = 0; if(!args) { snprintf(p, sizeof p, "rm -rf %%s/4242", BT_PROC_DIR); if(real_system(p)){} restarts = 0; return; }
    snprintf(p, sizeof p, "%%s/4242/comm", BT_PROC_DIR); FILE *f = fopen(p, "w"); fputs("bluealsa\n", f); fclose(f);
    snprintf(p, sizeof p, "%%s/4242/cmdline", BT_PROC_DIR); f = fopen(p, "w");
    char a[512]; snprintf(a, sizeof a, "bluealsa %%s", args); for(char *x = a; *x; x++) fputc(*x == ' ' ? 0 : *x, f); fclose(f);
}
static void links(int n){ char p[256]; snprintf(p, sizeof p, "rm -rf %%s/*; mkdir -p %%s/hci0", BT_SYSBT_DIR, BT_SYSBT_DIR); if(real_system(p)){}
    for(int i = 0; i < n; i++){ snprintf(p, sizeof p, "mkdir -p %%s/hci0:%%d", BT_SYSBT_DIR, 11 + i); if(real_system(p)){} } restarts = 0; }
''' % (proc, sysbt, d, d) + fn("static unsigned bt_codec_need(") + "\n" + fn("int bt_codec_pick(") + "\n" + \
        fn("static void bt_mac_cfg_key(") + "\n" + fn("static void bt_caps_key(") + "\n" + fn("static int bt_caps_get(") + "\n" + \
        fn("static int bt_caps_full(") + "\n" + fn("static void bt_caps_write(") + "\n" + fn("static void bt_caps_set(") + "\n" + fn("static void bt_caps_add(") + "\n" + fn("static int bt_bad_get(") + "\n" + fn("static void bt_mark_codec(") + "\n" + \
        fn("static int bt_born_for(") + "\n" + block("static const char *bt_ldac_q(void){", "static void bt_ensure_services(void){") + \
        fn("static void bt_alsa_rearm_born(") + "\n" + fn("static int bt_codec_prepare_steps(") + r'''
static int has(const char *a, const char *s){ return strstr(a, s) != NULL; }
int main(void){
    char a[512]; const char *hp = "AA:BB:CC:DD:EE:FF", *other = "11:22:33:44:55:66";
    snprintf(last, sizeof last, "%s", hp);
    codec = 1; assert(bt_born_for(hp) == 0);                                 /* unknown headphones: normal */
    bt_caps_set(hp, BTC_SBC | BTC_AAC | BTC_LDAC);
    codec = 1; assert(bt_born_for(hp) == 1); bt_alsa_args(a, sizeof a, 1); assert(has(a, "--codec=-sbc --codec=-ldac"));
    codec = 4; assert(bt_born_for(hp) == 2); bt_alsa_args(a, sizeof a, 2); assert(has(a, "--codec=-sbc") && !has(a, "--codec=-ldac") && has(a, "--ldac-quality=high"));
    bt_mark_codec(hp, BTC_LDAC, 0); assert(bt_born_for(hp) == 1); bt_mark_codec(hp, BTC_LDAC, 1); assert(bt_born_for(hp) == 2);
    puts("PASS arguments and known-good marks");
    /* finding 2: the real daemon decides, not a remembered intention */
    bt_alsa_args(a, sizeof a, 0); daemon_(a); links(0);
    assert(bt_alsa_actual(NULL, 0) == 0); bt_alsa_rearm_born(); assert(restarts == 1 && has(last_cmd, "--codec=-sbc"));
    bt_alsa_args(a, sizeof a, 1); daemon_(a); codec = 0;                     /* surviving AAC-only daemon, SBC chosen */
    char steps[512]; assert(bt_codec_prepare_steps(other, steps, sizeof steps) == 1 && has(steps, "--codec=sbc"));
    puts("PASS the running BlueALSA's real arguments decide (normal daemon re-armed; restricted one reset for others)");
    /* finding 4: LDAC quality counts */
    codec = 2; bt_alsa_args(a, sizeof a, 2); daemon_(a); links(0); bt_alsa_rearm_born(); assert(restarts == 0);   /* Mobile running, Mobile chosen */
    codec = 4; bt_alsa_rearm_born(); assert(restarts == 1 && has(last_cmd, "--ldac-quality=high"));             /* High chosen: restart */
    puts("PASS a changed LDAC quality is applied at the next restart");
    /* finding 3: only on evidence that nothing is connected; never on "can't tell" */
    codec = 2; bt_alsa_args(a, sizeof a, 0); daemon_(a); links(1); bt_alsa_rearm_born(); assert(restarts == 0);
    { char p[256]; snprintf(p, sizeof p, "rm -rf %s", BT_SYSBT_DIR); if(real_system(p)){} } restarts = 0;
    bt_alsa_rearm_born(); assert(restarts == 0);
    { char p[256]; snprintf(p, sizeof p, "mkdir -p %s", BT_SYSBT_DIR); if(real_system(p)){} } links(0);
    bt_alsa_rearm_born(); assert(restarts == 1);
    puts("PASS BlueALSA is restarted only when the kernel shows no connection (a link or 'can't tell': left alone)");
    daemon_(NULL); restarts = 0; bt_alsa_rearm_born(); assert(restarts == 0);       /* none running: nothing to restart */
    assert(!has(last_cmd, "sleep 0.3; bluealsa") || has(last_cmd, "( killall"));    /* restarts run in the background */
    puts("PASS restarts run in the background with a readiness wait; nothing is restarted when no daemon runs");
    /* aptX: only with the aptX BlueALSA installed; stock arguments unchanged without it */
    bt_caps_set(hp, BTC_SBC | BTC_AAC | BTC_LDAC | BTC_APTX | BTC_APTXHD);
    codec = 6; assert(bt_born_for(hp) == 1);                                      /* not installed: aptX ignored, best left is AAC */
    cfg_set_int("bt_aptx", 1);                                                    /* Settings > aptX (experimental): on */
    char c[800]; bt_alsa_cmd(c, sizeof c, 4); assert(!strncmp(c, "bluealsa -S", 11) && !has(c, "aptX"));
    { char p[512]; snprintf(p, sizeof p, "mkdir -p %s/aptx && printf '#!/bin/sh\\n' > %s/aptx/bluealsa && chmod +x %s/aptx/bluealsa", TD, TD, TD); if(real_system(p)){} }
    assert(bt_born_for(hp) == 4); codec = 5; assert(bt_born_for(hp) == 3);
    codec = 4; assert(bt_born_for(hp) == 2);
    bt_mark_codec(hp, BTC_LDAC, 0); assert(bt_born_for(hp) == 4); bt_mark_codec(hp, BTC_LDAC, 1);   /* LDAC failed: aptX HD next */                                      /* LDAC offered: LDAC */
    bt_mark_codec(hp, BTC_LDAC, 0); assert(bt_born_for(hp) == 4); bt_mark_codec(hp, BTC_LDAC, 1);   /* LDAC failed: aptX HD */
    bt_alsa_cmd(c, sizeof c, 4); assert(has(c, "/aptx/bluealsa -S") && has(c, "--codec=-aptX --codec=aptX-HD") && !has(c, "&&") && !has(c, "{"));
    bt_alsa_cmd(c, sizeof c, 2); assert(has(c, "--codec=-sbc --codec=ldac --codec=-aptX --codec=-aptX-HD --initial"));   /* LDAC on explicitly */
    bt_alsa_cmd(c, sizeof c, 0); assert(has(c, "--codec=sbc --codec=ldac --codec=aptX --codec=aptX-HD"));                /* normal: LDAC too */
    cfg_set_int("bt_aptx", 0); assert(bt_born_for(hp) == 2 && !bt_alsa_aptx_bin());   /* switch off: stock; LDAC chosen -> LDAC */
    bt_alsa_cmd(c, sizeof c, 4); assert(!strncmp(c, "bluealsa -S", 11) && !has(c, "aptX"));
    cfg_set_int("bt_aptx", 1);
    bt_alsa_args_for(a, sizeof a, 3, 1); daemon_(a); assert(bt_alsa_actual(NULL, 0) == 3);
    bt_alsa_args_for(a, sizeof a, 4, 1); daemon_(a); assert(bt_alsa_actual(NULL, 0) == 4);
    bt_alsa_args_for(a, sizeof a, 0, 1); daemon_(a); assert(bt_alsa_actual(NULL, 0) == 0);
    bt_alsa_args_for(a, sizeof a, 2, 1); daemon_(a); assert(bt_alsa_actual(NULL, 0) == 2);
    bt_alsa_args_for(a, sizeof a, 1, 1); daemon_(a); assert(bt_alsa_actual(NULL, 0) == 1);
    bt_mark_codec(hp, BTC_APTXHD, 0); codec = 6; assert(bt_born_for(hp) == 3);       /* aptX HD failed: born on aptX */
    /* the Nov 2026 loop: an AAC-only start saw "AAC" and stuck every later start on AAC */
    { const char *tw = "80:C3:BA:72:32:A9"; codec = 6;
      bt_caps_add(tw, BTC_AAC); assert(bt_born_for(tw) == 0);          /* partial list only: normal start, learn it */
      bt_caps_set(tw, BTC_SBC | BTC_AAC | BTC_APTX); assert(bt_born_for(tw) == 3);   /* full list: aptX HD chosen -> aptX */
      bt_caps_add(tw, BTC_AAC); assert(bt_born_for(tw) == 3);          /* a later AAC-only list adds, never shrinks */
      bt_mark_codec(tw, BTC_AAC, 1); assert(bt_caps_get(tw) == (int)(BTC_SBC | BTC_AAC | BTC_APTX));
      bt_caps_set(tw, BTC_AAC); codec = 1; assert(bt_born_for(tw) == 0); }   /* "full" without SBC (the MTW4 memory): relearn */
    puts("PASS codec memory: only a full list replaces it; partial lists add; an unproven memory gets a normal start");
    puts("PASS aptX BlueALSA: born on aptX / aptX HD only when installed; /proc reads every birth back; LDAC enabled explicitly; the switch off means stock");
    return 0;
}
'''
    (d / "t.c").write_text(C)
    subprocess.run(["cc", "-std=gnu11", "-o", str(d / "t"), str(d / "t.c")], check=True)
    r = subprocess.run([str(d / "t")], capture_output=True, text=True); print(r.stdout.strip(), r.stderr.strip()); r.check_returncode()
