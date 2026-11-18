#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""USB DAC screen shows the host's rate from the gadget capture stream, not the last local song's rate.
Production uac_rate() on a fake /proc/asound tree."""
import subprocess, tempfile
from pathlib import Path
ui = Path(__file__).resolve().parents[1]; src = (ui / "modes.c").read_text()
start = src.index("static int uac_rate(void){"); end = src.index("\n}\n", start) + 3
HWP = "access: MMAP_INTERLEAVED\nformat: S32_LE\nsubformat: STD\nchannels: 2\nrate: %d (%d/1)\nperiod_size: 1024\n"
cases = [
    ("host at 96k",         {"card1/pcm0c/sub0/hw_params": HWP % (96000, 96000)}, 96000),
    ("host at 44.1k",       {"card1/pcm0c/sub0/hw_params": HWP % (44100, 44100)}, 44100),
    ("playback ignored",    {"card0/pcm0p/sub0/hw_params": HWP % (48000, 48000), "card1/pcm0c/sub0/hw_params": HWP % (192000, 192000)}, 192000),
    ("stream closed",       {"card1/pcm0c/sub0/hw_params": "closed\n"}, 0),
    ("no gadget card",      {}, 0),
]
with tempfile.TemporaryDirectory() as d:
    d = Path(d)
    for i, (name, files, want) in enumerate(cases):
        root = d / ("c%d" % i); root.mkdir()
        for rel, txt in files.items():
            f = root / rel; f.parent.mkdir(parents=True, exist_ok=True); f.write_text(txt)
        C = ('#include <glob.h>\n#include <stdio.h>\n#define UAC_HWP_GLOB "%s/card*/pcm*c/sub*/hw_params"\n' % root) + \
            src[start:end] + '\nint main(void){ printf("%d\\n", uac_rate()); return 0; }\n'
        (d / "t.c").write_text(C)
        subprocess.run(["cc", "-std=gnu11", "-o", str(d / "t"), str(d / "t.c")], check=True)
        got = int(subprocess.run([str(d / "t")], capture_output=True, text=True, check=True).stdout)
        assert got == want, (name, got, want); print("ok  %-18s -> %d" % (name, got))
print("PASS USB DAC rate comes from the host's capture stream")
