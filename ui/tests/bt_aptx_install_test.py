#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""aptX files from mq_ui: the real embedded data (aptx_blob.c) and the production installer (bt.c) into a temporary
folder - written once, identical, right modes; nothing rewritten when in place; a damaged file repaired; no .new left."""
import os, stat, subprocess, tempfile
from pathlib import Path
ui = Path(__file__).resolve().parents[1]; src = (ui / "bt.c").read_text()
def fn(sig):
    start = src.index(sig)
    o = src.index("{", start); d = 1; e = o + 1
    while d: d += (src[e] == "{") - (src[e] == "}"); e += 1
    return src[start:e]
with tempfile.TemporaryDirectory() as d:
    d = Path(d); target = d / "diskos-aptx"
    C = r'''
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include "aptx_blob.h"
#define BT_APTX_DIR "%s"
static int fw_os_ver(void){ return 257; }
''' % target + fn("static int aptx_same(") + "\n" + fn("static int bt_aptx_install(") + r'''
int main(void){
    assert(bt_aptx_install() == 1);                                   /* first time: written */
    for(int i = 0; i < APTX_NFILES; i++){
        char p[512]; snprintf(p, sizeof p, BT_APTX_DIR "/%s", APTX_FILES[i].name);
        assert(aptx_same(p, APTX_FILES[i].data, aptx_file_size(i)));
        struct stat st; assert(stat(p, &st) == 0 && (st.st_mode & 0777) == (unsigned)APTX_FILES[i].mode);
    }
    assert(bt_aptx_install() == 0);                                   /* in place: nothing written */
    FILE *f = fopen(BT_APTX_DIR "/bluealsa", "r+b"); fputc('X', f); fclose(f);   /* damaged */
    assert(bt_aptx_install() == 1 && bt_aptx_install() == 0);         /* repaired, then in place */
    puts("PASS aptX files: written once, identical, right modes; left alone when in place; a damaged file repaired");
    return 0;
}
'''
    (d / "t.c").write_text(C)
    subprocess.run(["cc", "-std=gnu11", "-Wall", "-Werror", "-I" + str(ui), f"-DAPTX_DIR=\"{ui}/vendor/aptx\"",
                    str(d / "t.c"), str(ui / "aptx_blob.c"), "-o", str(d / "t")], check=True)
    print(subprocess.run([str(d / "t")], capture_output=True, text=True, check=True).stdout.strip())
    left = [p.name for p in target.iterdir() if p.name.endswith(".new")]
    assert not left, left
    assert (target / "bluealsa").read_bytes() == (ui / "vendor/aptx/bluealsa").read_bytes()
    print("PASS no .new files left; the written bluealsa is byte-identical to vendor/aptx")
