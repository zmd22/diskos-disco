#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Quiet Wi-Fi while listening over Bluetooth (production functions from bt.c). The shell steps run for real against
stand-in `wl` and `wpa_cli` that log what they're asked: power-save 2 while routed and back to the previous mode after;
searching paused only when not connected and not on the Wi-Fi screen, and resumed after; re-applied every minute;
off with the setting, with Bluetooth off, and leftovers undone at start-up."""
import os, subprocess, tempfile, time
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
    d = Path(d); b = d / "bin"; b.mkdir(); log = d / "log"; st = d / "state"
    (b / "wl").write_text('#!/bin/sh\necho "wl $*" >> %s\n[ "$#" -eq 1 ] && echo 0\nexit 0\n' % log)
    (b / "wpa_cli").write_text('#!/bin/sh\necho "wpa_cli $*" >> %s\ncase "$*" in *status*) echo "wpa_state=$(cat %s)";; esac\nexit 0\n' % (log, st))
    for f in b.iterdir(): f.chmod(0o755)
    C = r'''
#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define BT_WQ_DIR "%s"
enum { SCR_HOME, SCR_WIFI = 16, SCR_WIFI_INFO = 17 };
static int scr, quiet = 1; static char route[18]; static uint32_t now = 1000;
static int screen_current(void){ return scr; }
static const char *ui_route_mac(void){ return route; }
static uint32_t lv_tick_get(void){ return now; }
static uint32_t lv_tick_elaps(uint32_t t){ return now - t; }
static int cfg_get_int(const char *k, int d){ return !strcmp(k, "bt_wifi_quiet") ? quiet : d; }
static int sys_(const char *c){ char x[800]; snprintf(x, sizeof x, "%%s", c); char *a = strrchr(x, '&'); if(a) *a = 0; return system(x); }
#define system sys_
''' % d + block("#define BT_WQ_PM ", "static void bt_autoroute_poll_cb") + r'''
static void state(const char *s){ FILE *f = fopen("%s", "w"); fputs(s, f); fclose(f); }
static int logged(const char *s){ FILE *f = fopen("%s", "r"); if(!f) return 0; char b[4096]; size_t n = fread(b, 1, sizeof b - 1, f); b[n] = 0; fclose(f); return strstr(b, s) != NULL; }
static void clear(void){ unlink("%s"); }
int main(void){
    state("COMPLETED");
    bt_wifi_quiet_tick(0); assert(!logged("wl"));                              /* not on Bluetooth: nothing */
    snprintf(route, sizeof route, "AA:BB:CC:DD:EE:FF");
    bt_wifi_quiet_tick(0); assert(logged("wl PM 2") && !logged("disconnect"));  /* connected Wi-Fi: power-save only */
    assert(access(BT_WQ_PM, F_OK) == 0 && access(BT_WQ_PAUSED, F_OK) != 0);
    clear(); now += 3000; bt_wifi_quiet_tick(0); assert(!logged("wl"));         /* not again every poll */
    state("SCANNING"); now += 60000; bt_wifi_quiet_tick(0);                     /* a minute later, Wi-Fi searching */
    assert(logged("wl PM 2") && logged("wpa_cli -i wlan0 disconnect") && access(BT_WQ_PAUSED, F_OK) == 0);
    route[0] = 0; clear(); bt_wifi_quiet_tick(0);                              /* off Bluetooth: all undone */
    assert(logged("wl PM 0") && logged("reconnect") && access(BT_WQ_PM, F_OK) != 0 && access(BT_WQ_PAUSED, F_OK) != 0);
    puts("PASS power-save while on Bluetooth and back after; searching paused only when not connected, then resumed");
    clear(); scr = SCR_WIFI; snprintf(route, sizeof route, "AA:BB:CC:DD:EE:FF"); bt_wifi_quiet_tick(0);
    assert(logged("wl PM 2") && !logged("disconnect"));                        /* Wi-Fi screen open: never paused */
    scr = 0; clear(); bt_wifi_quiet_tick(1); assert(logged("wl PM 0") && !logged("reconnect"));   /* Bluetooth off */
    state("INACTIVE"); clear(); now += 1; bt_wifi_quiet_tick(0); assert(!logged("disconnect"));   /* nothing to search */
    quiet = 0; clear(); bt_wifi_quiet_tick(0); assert(logged("wl PM 0"));       /* setting off: undone */
    clear(); bt_wifi_quiet_tick(0); assert(!logged("wl"));
    puts("PASS never on the Wi-Fi screen; undone with Bluetooth off and with the setting off");
    quiet = 1; state("SCANNING"); bt_wifi_quiet_tick(0); assert(logged("disconnect"));
    clear(); bt_wifi_quiet_boot(); assert(logged("wl PM 0") && logged("reconnect"));   /* a restarted mq_ui undoes it */
    clear(); bt_wifi_quiet_boot(); assert(!logged("wl") && !logged("wpa_cli"));
    puts("PASS a leftover quiet Wi-Fi is undone at start-up");
    return 0;
}
''' % (st, log, log)
    (d / "t.c").write_text(C)
    subprocess.run(["cc", "-std=gnu11", "-Wall", "-Wno-unused-function", "-o", str(d / "t"), str(d / "t.c")], check=True)
    env = dict(os.environ, PATH=f"{b}:{os.environ['PATH']}")
    r = subprocess.run([str(d / "t")], capture_output=True, text=True, env=env)
    print(r.stdout.strip(), r.stderr.strip()); r.check_returncode()
