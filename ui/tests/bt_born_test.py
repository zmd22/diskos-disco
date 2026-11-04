#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Born on the right codec, in the right BlueALSA. The 2.6.2 b2 review's scenarios (the running BlueALSA's real
arguments are read from /proc; restarts only when the kernel shows no connection; the LDAC quality counts; other
headphones get their own start before connecting), plus 3.4.0: FiiO's BlueALSA for SBC/AAC/LDAC, the aptX build
(never LDAC) for aptX; born only on a codec that has played; nothing remembered as bad except a crash-guard skip,
which choosing the codec again lifts. Production functions from bt.c on fake /proc and /sys trees."""
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
#define BT_FULL_APTX 2
#include <unistd.h>
#define BT_APTX_BINS "%s/aptx/bluealsa"
static const char *bt_alsa_aptx_bin(void);
static int fw = 257, codec = 0, restarts; static char last[20], keys[64][32], last_cmd[700]; static int vals[64], nk;
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
static void exe_(const char *target){   /* the fake daemon's /proc exe link (absent = can't tell) */
    char p[512]; snprintf(p, sizeof p, "%%s/4242/exe", BT_PROC_DIR); unlink(p); if(target && symlink(target, p)){} }
static void links(int n){ char p[256]; snprintf(p, sizeof p, "rm -rf %%s/*; mkdir -p %%s/hci0", BT_SYSBT_DIR, BT_SYSBT_DIR); if(real_system(p)){}
    for(int i = 0; i < n; i++){ snprintf(p, sizeof p, "mkdir -p %%s/hci0:%%d", BT_SYSBT_DIR, 11 + i); if(real_system(p)){} } restarts = 0; }
''' % (proc, sysbt, d, d) + fn("static unsigned bt_codec_need(") + "\n" + fn("int bt_codec_pick(") + "\n" + \
        fn("static void bt_mac_cfg_key(") + "\n" + fn("static void bt_caps_key(") + "\n" + fn("static int bt_caps_get(") + "\n" + \
        fn("static int bt_caps_full(") + "\n" + fn("static void bt_caps_write(") + "\n" + fn("static void bt_caps_set(") + "\n" + fn("static void bt_caps_add(") + "\n" + fn("static int bt_good_get(") + "\n" + fn("static void bt_good_add(") + "\n" + \
        fn("static unsigned bt_skip_get(") + "\n" + fn("static void bt_skip_add(") + "\n" + fn("void bt_codec_setting_changed(") + "\n" + \
        fn("static int bt_codec_choice(") + "\n" + fn("static unsigned bt_usable(") + "\n" + fn("static unsigned bt_kind_can(") + "\n" + \
        fn("static int bt_kind_for_v(") + "\n" + fn("static int bt_kind_for(") + "\n" + fn("static int bt_born_for(") + "\n" + block("static const char *bt_ldac_q(void){", "static void bt_ensure_services(void){") + \
        fn("static void bt_alsa_rearm_born(") + "\n" + fn("static int bt_codec_prepare_steps(") + r'''
static int has(const char *a, const char *s){ return strstr(a, s) != NULL; }
int main(void){
    char a[512], c[800], steps[1024]; const char *hp = "AA:BB:CC:DD:EE:FF", *other = "11:22:33:44:55:66";
    snprintf(last, sizeof last, "%s", hp);
    /* aptX off: FiiO's BlueALSA only, born only on a codec that has played on these headphones */
    codec = 1; assert(bt_born_for(hp) == 0 && bt_kind_for(hp) == 0);          /* unknown headphones: normal */
    bt_caps_set(hp, BTC_SBC | BTC_AAC | BTC_LDAC);
    codec = 4; assert(bt_born_for(hp) == 0);                                  /* LDAC listed but never played: normal */
    bt_good_add(hp, BTC_LDAC); assert(bt_born_for(hp) == 2);
    bt_alsa_args(a, sizeof a, 2); assert(has(a, "--codec=-sbc") && !has(a, "--codec=-ldac") && has(a, "--ldac-quality=high"));
    codec = 1; assert(bt_born_for(hp) == 0); bt_good_add(hp, BTC_AAC); assert(bt_born_for(hp) == 1);
    bt_alsa_args(a, sizeof a, 1); assert(has(a, "--codec=-sbc --codec=-ldac"));
    puts("PASS born only on a codec that has played on these headphones; arguments as 3.3.1");
    /* nothing is remembered as bad; only a crash-guard skip, until the codec is chosen again */
    codec = 3; bt_skip_add(hp, BTC_LDAC); assert(bt_born_for(hp) == 1);       /* LDAC skipped: AAC */
    bt_codec_setting_changed(); assert(bt_skip_get(hp) == 0 && bt_born_for(hp) == 2);   /* chosen again: LDAC back */
    bt_caps_add(hp, BTC_AAC); assert(bt_caps_get(hp) == (int)(BTC_SBC | BTC_AAC | BTC_LDAC));   /* a short list never shrinks it */
    puts("PASS a skip lasts until Bluetooth Codec is chosen again; the codec memory only grows");
    /* finding 2: the real daemon decides, not a remembered intention */
    codec = 1; bt_alsa_args(a, sizeof a, 0); daemon_(a); links(0);
    assert(bt_alsa_actual(NULL, 0) == 0); bt_alsa_rearm_born(); assert(restarts == 1 && has(last_cmd, "--codec=-sbc"));
    bt_alsa_args(a, sizeof a, 1); daemon_(a); codec = 0;                     /* surviving AAC-only daemon, SBC chosen */
    assert(bt_codec_prepare_steps(other, steps, sizeof steps) == 1 && has(steps, "--codec=sbc"));
    puts("PASS the running BlueALSA's real arguments decide (normal daemon re-armed; restricted one reset for others)");
    /* finding 4: LDAC quality counts */
    codec = 2; bt_alsa_args(a, sizeof a, 2); daemon_(a); links(0); bt_alsa_rearm_born(); assert(restarts == 0);
    codec = 4; bt_alsa_rearm_born(); assert(restarts == 1 && has(last_cmd, "--ldac-quality=high"));
    puts("PASS a changed LDAC quality is applied at the next restart");
    /* finding 3: only on evidence that nothing is connected; never on "can't tell" */
    codec = 2; bt_alsa_args(a, sizeof a, 0); daemon_(a); links(1); bt_alsa_rearm_born(); assert(restarts == 0);
    { char p[256]; snprintf(p, sizeof p, "rm -rf %s", BT_SYSBT_DIR); if(real_system(p)){} } restarts = 0;
    bt_alsa_rearm_born(); assert(restarts == 0);
    { char p[256]; snprintf(p, sizeof p, "mkdir -p %s", BT_SYSBT_DIR); if(real_system(p)){} } links(0);
    bt_alsa_rearm_born(); assert(restarts == 1);
    puts("PASS BlueALSA is restarted only when the kernel shows no connection (a link or 'can't tell': left alone)");
    daemon_(NULL); restarts = 0; bt_alsa_rearm_born(); assert(restarts == 0);
    assert(!has(last_cmd, "sleep 0.3; bluealsa") || has(last_cmd, "( killall"));
    puts("PASS restarts run in the background with a readiness wait; nothing is restarted when no daemon runs");
    /* aptX on: which BlueALSA for which headphones */
    const char *ld = "22:33:44:55:66:77", *ax = "33:44:55:66:77:88", *both = "44:55:66:77:88:99", *nw = "55:66:77:88:99:AA";
    bt_caps_set(ld, BTC_SBC | BTC_AAC | BTC_LDAC); bt_caps_set(ax, BTC_SBC | BTC_AAC | BTC_APTX | BTC_APTXHD);
    bt_caps_set(both, BTC_SBC | BTC_AAC | BTC_LDAC | BTC_APTX | BTC_APTXHD);
    cfg_set_int("bt_aptx", 1); codec = 3;
    assert(bt_kind_for(ax) == 0);                                            /* switched on, files not there yet: FiiO's */
    { char p[512]; snprintf(p, sizeof p, "mkdir -p %s/aptx && printf '#!/bin/sh\\n' > %s/aptx/bluealsa && chmod +x %s/aptx/bluealsa", TD, TD, TD); if(real_system(p)){} }
    assert(bt_kind_for(ld) == 0 && bt_kind_for(both) == 0);                   /* LDAC: always FiiO's */
    assert(bt_kind_for(ax) == 1);                                            /* aptX headphones: ours */
    assert(bt_kind_for(nw) == 1);                                            /* unknown: ours, to learn the whole list */
    bt_caps_add(nw, BTC_AAC); assert(bt_kind_for(nw) == 1);                   /* a partial list without LDAC: learn it */
    bt_caps_add(nw, BTC_LDAC); assert(bt_kind_for(nw) == 0);                  /* LDAC seen anywhere: FiiO's, no need to learn */
    codec = 1; assert(bt_kind_for(ax) == 0); codec = 0; assert(bt_kind_for(ax) == 0); codec = 3;   /* AAC / SBC chosen: FiiO's */
    bt_skip_add(both, BTC_LDAC); assert(bt_kind_for(both) == 1); bt_codec_setting_changed(); assert(bt_kind_for(both) == 0);
    puts("PASS FiiO's BlueALSA for LDAC (and SBC/AAC choices), ours for aptX headphones and to learn unknown ones");
    /* ours never runs LDAC */
    for(int b = 0; b <= 4; b++){ bt_alsa_args_for(a, sizeof a, b, 1); assert(has(a, "--codec=-ldac ") && !has(a, "--codec=ldac ")); }
    bt_alsa_cmd(c, sizeof c, 4, 1); assert(has(c, "/aptx/bluealsa -S") && has(c, "--codec=-aptX --codec=aptX-HD") && !has(c, "&&"));
    bt_alsa_cmd(c, sizeof c, 2, 0); assert(!strncmp(c, "bluealsa -S", 11) && !has(c, "aptX") && !has(c, "--codec=-ldac"));   /* LDAC: FiiO's */
    bt_alsa_cmd(c, sizeof c, 2, 1); assert(has(c, "/aptx/bluealsa") && has(c, "--codec=-ldac") && has(c, "--codec=sbc "));   /* asked LDAC on ours: normal */
    puts("PASS the aptX build is always started with LDAC off; LDAC starts go to FiiO's");
    /* born: only a whole list and a played codec */
    assert(bt_born_for(ax) == 0); bt_good_add(ax, BTC_APTXHD); assert(bt_born_for(ax) == 4);
    bt_good_add(ld, BTC_LDAC); assert(bt_born_for(ld) == 2);
    bt_good_add(both, BTC_LDAC); bt_good_add(both, BTC_APTXHD); assert(bt_born_for(both) == 2);   /* both: LDAC on FiiO's */
    bt_skip_add(ax, BTC_APTXHD); assert(bt_born_for(ax) == 0); bt_codec_setting_changed();      /* skipped, aptX never played: normal */
    { const char *tw = "80:C3:BA:72:32:A9";                                   /* the MTW4 memory (AAC, no SBC): relearn */
      bt_caps_set(tw, BTC_AAC); cfg_set_int("btgood_80C3BA7232A9", BTC_AAC); assert(bt_kind_for(tw) == 1 && bt_born_for(tw) == 0); }
    puts("PASS born on aptX / aptX HD / LDAC only from a whole list and a codec that has played");
    /* /proc reads every birth back, and which binary runs */
    bt_alsa_args_for(a, sizeof a, 3, 1); daemon_(a); assert(bt_alsa_actual(NULL, 0) == 3);
    bt_alsa_args_for(a, sizeof a, 4, 1); daemon_(a); assert(bt_alsa_actual(NULL, 0) == 4);
    bt_alsa_args_for(a, sizeof a, 0, 1); daemon_(a); assert(bt_alsa_actual(NULL, 0) == 0);
    bt_alsa_args_for(a, sizeof a, 1, 1); daemon_(a); assert(bt_alsa_actual(NULL, 0) == 1);
    bt_alsa_args_for(a, sizeof a, 2, 0); daemon_(a); assert(bt_alsa_actual(NULL, 0) == 2);
    { char axp[600]; snprintf(axp, sizeof axp, "%s/aptx/bluealsa", TD);
      exe_(axp); assert(bt_alsa_kind_running() == 1); exe_("/usr/bin/bluealsa"); assert(bt_alsa_kind_running() == 0);
      exe_(NULL); assert(bt_alsa_kind_running() == -1); }
    puts("PASS /proc tells the start arguments and which BlueALSA runs ('can't tell' = -1)");
    /* the right binary for the usual headphones, only while nothing is connected */
    snprintf(last, sizeof last, "%s", ax);
    bt_alsa_args(a, sizeof a, 0); daemon_(a); exe_("/usr/bin/bluealsa"); links(0);
    bt_alsa_rearm_born(); assert(restarts == 1 && has(last_cmd, "/aptx/bluealsa") && has(last_cmd, "--codec=-ldac"));
    links(1); bt_alsa_rearm_born(); assert(restarts == 0);                   /* connected: left alone */
    snprintf(last, sizeof last, "%s", ld);
    { char axp[600]; snprintf(axp, sizeof axp, "%s/aptx/bluealsa", TD);
      bt_alsa_args_for(a, sizeof a, 0, 1); daemon_(a); exe_(axp); links(0);
      bt_alsa_rearm_born(); assert(restarts == 1 && has(last_cmd, "bluealsa -S") && !has(last_cmd, "/aptx/"));   /* LDAC pair: FiiO's */
      exe_(NULL); restarts = 0; bt_alsa_args(a, sizeof a, 2); daemon_(a); bt_alsa_rearm_born(); assert(restarts == 0);   /* right args, can't tell: leave */
    /* before connecting: the right binary for those headphones */
      bt_alsa_args_for(a, sizeof a, 0, 1); daemon_(a); exe_(axp);
      assert(bt_codec_prepare_steps(ld, steps, sizeof steps) == 1 && has(steps, "bluealsa -S") && !has(steps, "/aptx/"));
      assert(bt_codec_prepare_steps(ax, steps, sizeof steps) == 0);           /* ours, normal order: fine for aptX */
      bt_alsa_args(a, sizeof a, 0); daemon_(a); exe_("/usr/bin/bluealsa");
      assert(bt_codec_prepare_steps(ax, steps, sizeof steps) == 1 && has(steps, "/aptx/bluealsa"));
      assert(bt_codec_prepare_steps(ld, steps, sizeof steps) == 0); }
    cfg_set_int("bt_aptx", 0); assert(bt_kind_for(ax) == 0 && !bt_alsa_aptx_bin());    /* switched off: FiiO's for everyone */
    bt_alsa_cmd(c, sizeof c, 4, 1); assert(!strncmp(c, "bluealsa -S", 11) && !has(c, "aptX"));
    puts("PASS the usual headphones' BlueALSA is restarted only with nothing connected; before connecting, the right one");
    return 0;
}
'''
    (d / "t.c").write_text(C)
    subprocess.run(["cc", "-std=gnu11", "-o", str(d / "t"), str(d / "t.c")], check=True)
    r = subprocess.run([str(d / "t")], capture_output=True, text=True); print(r.stdout.strip(), r.stderr.strip()); r.check_returncode()
