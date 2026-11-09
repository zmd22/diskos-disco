#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Plugged-in detection for idle power-off (#17): any non-battery power supply with online=1 counts, whatever its
name; the battery doesn't; no such source -> unknown (-1). Production plugged_listed() (the sysfs half of
usage_plugged_in(), which adds the charge-cycle and USB-host checks on top) on a fake sysfs tree."""
import subprocess, tempfile
from pathlib import Path
ui = Path(__file__).resolve().parents[1]; src = (ui / "usage.c").read_text()
start = src.index("static int plugged_listed(void){"); end = src.index("\n}\n", start) + 3
cases = [
    ("unplugged",                {"cw221X-bat": ("Battery", "1"), "usb": ("USB", "0")}, 0),
    ("plugged into USB",         {"cw221X-bat": ("Battery", "1"), "usb": ("USB", "1")}, 1),
    ("full, still on a charger", {"cw221X-bat": ("Battery", "1"), "ac": ("Mains", "1"), "usb": ("USB", "0")}, 1),
    ("other name (FiiO's own)",  {"cw221X-bat": ("Battery", "1"), "fiio-charger": ("USB", "1")}, 1),
    ("no charger listed",        {"cw221X-bat": ("Battery", "1")}, -1),
]
with tempfile.TemporaryDirectory() as d:
    d = Path(d)
    for name, tree, want in cases:
        root = d / name.replace(" ", "_").replace(",", "").replace("(", "").replace(")", "").replace("'", "")
        for sup, (typ, online) in tree.items():
            (root / sup).mkdir(parents=True); (root / sup / "type").write_text(typ + "\n"); (root / sup / "online").write_text(online + "\n")
        C = ('#include <dirent.h>\n#include <stdint.h>\n#include <stdio.h>\n#include <string.h>\n'
             'static uint32_t lv_tick_get(void){ return 1000; }\n#define POWER_SUPPLY_DIR "%s"\n' % root) + src[start:end] + \
            '\nint main(void){ printf("%d\\n", plugged_listed()); return 0; }\n'
        (d / "t.c").write_text(C)
        subprocess.run(["cc", "-std=gnu11", "-o", str(d / "t"), str(d / "t.c")], check=True)
        got = int(subprocess.run([str(d / "t")], capture_output=True, text=True, check=True).stdout)
        assert got == want, (name, got, want)
        print("ok  %-26s -> %d" % (name, got))
print("PASS plugged-in detection: any non-battery source online counts, whatever its name; unknown when none listed")
