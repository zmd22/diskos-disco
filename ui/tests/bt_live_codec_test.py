#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Song Info's Output row: BlueALSA's codec word -> the name people know (production function)."""
import subprocess, tempfile
from pathlib import Path
ui = Path(__file__).resolve().parents[1]; src = (ui / "bt.c").read_text()
def fn(sig):
    start = src.index(sig)
    o = src.index("{", start); d = 1; e = o + 1
    while d: d += (src[e] == "{") - (src[e] == "}"); e += 1
    return src[start:e]
C = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
''' + fn("const char *bt_codec_display(") + r'''
int main(void){
    assert(!strcmp(bt_codec_display("SBC"), "SBC"));
    assert(!strcmp(bt_codec_display("AAC"), "AAC"));
    assert(!strcmp(bt_codec_display("LDAC"), "LDAC"));
    assert(!strcmp(bt_codec_display("APTX"), "aptX"));
    assert(!strcmp(bt_codec_display("aptX"), "aptX"));
    assert(!strcmp(bt_codec_display("APTX-HD"), "aptX HD"));
    assert(!strcmp(bt_codec_display("aptX-HD"), "aptX HD"));
    assert(!strcmp(bt_codec_display("LC3PLUS"), "LC3PLUS"));   /* unknown: passed through */
    assert(!strcmp(bt_codec_display(""), "") && !strcmp(bt_codec_display(NULL), ""));
    puts("PASS live codec names: SBC/AAC/LDAC as is, aptX / aptX HD spelled out, unknown passed through");
    return 0;
}
'''
with tempfile.TemporaryDirectory() as d:
    p = Path(d) / "t.c"; p.write_text(C)
    subprocess.run(["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror", str(p), "-o", str(Path(d) / "t")], check=True)
    subprocess.run([str(Path(d) / "t")], check=True)
