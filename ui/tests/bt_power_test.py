#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Bluetooth on/off lifecycle (2.5.9 Bluetooth audit, B04).

Compiles the production bt_enabling()/bt_enable()/bt_disable() with system() captured, then runs the generated
shell scripts against stand-in commands (killall, rfkill, hciconfig, patchram, bluetoothd, bluealsa, bluetoothctl
log what they're asked to do). Scenario from the audit: on -> off -> on, with the teardown delayed until after the
second "on". It must end with Bluetooth brought up once, by the newer bring-up; the superseded one stops early.
"""
import os, subprocess, sys, tempfile, time
from pathlib import Path

ui = Path(__file__).resolve().parents[1]
src = (ui / "bt.c").read_text()

def fn(sig):
    start = src.index(sig)
    while src[start:src.index("\n", start)].split("/*")[0].rstrip().endswith(";"):
        start = src.index(sig, start + 1)
    opening = src.index("{", start); depth = 1; end = opening + 1
    while depth:
        depth += (src[end] == "{") - (src[end] == "}"); end += 1
    return src[start:end]

C = r'''
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#define BT_ENABLE_MAX_S 60
static int nscripts; static char scripts[8][4096];
static int fake_system(const char *cmd){
    if(strstr(cmd, "/tmp/bt_enabling") || strstr(cmd, "killall")){ snprintf(scripts[nscripts++], 4096, "%s", cmd); return 0; }
    return system(cmd);
}
#define system fake_system
static void scan_abort(void){}
static void bt_autoroute_stop(void){}
static void bt_announce_reset(void){}
static int g_born;
static const char *cfg_get_str(const char *k, const char *d){ (void)k; return d; }
static int bt_born_for(const char *mac){ (void)mac; return 0; }
static void bt_alsa_args(char *out, int cap, int born){ (void)born; snprintf(out, cap, "-S --device=hci0 --profile=a2dp-source --codec=sbc"); }
static void bt_alsa_cmd(char *out, int cap, int born){ char a[200]; bt_alsa_args(a, sizeof a, born); snprintf(out, cap, "bluealsa %s", a); }
''' + fn("static int bt_enabling(") + "\n" + fn("static void bt_enable(") + "\n" + fn("static void bt_disable(") + r'''
int main(int argc, char **argv){
    unlink("/tmp/bt_enabling"); unlink("/tmp/bt_disabling");
    bt_enable();            /* 1: on */
    bt_disable();           /* 2: off (teardown queued, not run yet) */
    bt_enable();            /* 3: on again: must start a new bring-up even though teardown hasn't run */
    for(int i = 0; i < nscripts; i++){
        char p[256]; snprintf(p, sizeof p, "%s/s%d.sh", argv[1], i);
        FILE *f = fopen(p, "w"); fputs(scripts[i], f); fputs("\nwait\n", f); fclose(f);
    }
    printf("%d\n", nscripts);
    return 0;
}
'''

STUBS = {
    "killall": 'echo "killall $*" >> "$LOG"',
    "rfkill": 'echo "rfkill $*" >> "$LOG"',
    "hciconfig": 'echo "hciconfig $*" >> "$LOG"; [ "$#" -eq 1 ] && echo "UP RUNNING"; exit 0',
    "brcm_patchram_plus": 'echo "patchram" >> "$LOG"',
    "bluealsa": 'echo "bluealsa started" >> "$LOG"',
    "bluetoothctl": 'echo "bluetoothctl $*" >> "$LOG"',
    "sleep": 'exec /bin/sleep 0.05',          # the scripts' 1 s waits, shortened
}

with tempfile.TemporaryDirectory() as d:
    d = Path(d)
    (d / "t.c").write_text(C)
    subprocess.run(["cc", "-std=gnu11", "-o", str(d / "t"), str(d / "t.c")], check=True)
    out = subprocess.run([str(d / "t"), str(d)], capture_output=True, text=True, check=True).stdout.strip()
    assert out == "3", "expected bring-up, teardown, bring-up; got %s scripts" % out
    bin_ = d / "bin"; bin_.mkdir()
    for name, body in STUBS.items():
        (bin_ / name).write_text("#!/bin/sh\n" + body + "\n"); (bin_ / name).chmod(0o755)
    Path("/usr/project").mkdir(parents=True, exist_ok=True)
    btd = Path("/usr/project/bluetoothd"); had = btd.exists()
    if not had:
        btd.write_text('#!/bin/sh\necho "bluetoothd started" >> "$LOG"\n'); btd.chmod(0o755)
    log = d / "log"; log.write_text("")
    env = dict(os.environ, PATH=str(bin_) + ":" + os.environ["PATH"], LOG=str(log))
    try:
        # the delayed teardown runs now, then the superseded first bring-up, then the newer one
        for i in (1, 0, 2):
            subprocess.run(["sh", str(d / ("s%d.sh" % i))], env=env, check=False, timeout=30)
            time.sleep(0.2)
    finally:
        if not had: btd.unlink()
    lines = log.read_text().splitlines()
    starts = [l for l in lines if l == "bluetoothd started"]
    assert len(starts) == 1, "bluetoothd should start exactly once, by the newer bring-up:\n" + "\n".join(lines)
    assert not os.path.exists("/tmp/bt_disabling"), "teardown marker left behind"
    assert not os.path.exists("/tmp/bt_enabling"), "the newer bring-up should finish and drop its marker"
    print("PASS: on -> off -> on with a delayed teardown ends ON; the superseded bring-up stops, the newer one completes")
