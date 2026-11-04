#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Codec choice: Best (LDAC / aptX HD / aptX) -> AAC -> SBC, decided from what the headphones offer (production functions)."""
import subprocess, tempfile
from pathlib import Path
ui = Path(__file__).resolve().parents[1]; src = (ui / "bt.c").read_text()
def fn(sig):
    start = src.index(sig)
    while src[start:src.index("\n", start)].split("/*")[0].rstrip().endswith(";"): start = src.index(sig, start + 1)
    o = src.index("{", start); d = 1; e = o + 1
    while d: d += (src[e] == "{") - (src[e] == "}"); e += 1
    return src[start:e]
C = r'''
#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#define BT_CODEC_N 7
#define BTC_SBC  1u
#define BTC_AAC  2u
#define BTC_LDAC 4u
#define BTC_APTX 8u
#define BTC_APTXHD 16u
''' + fn("static unsigned bt_codec_need(") + "\n" + fn("int bt_codec_pick(") + "\n" + fn("static unsigned bt_codec_below(") + "\n" + fn("unsigned bt_codec_parse_avail(") + r'''
int main(void){
    int k; unsigned all = bt_codec_parse_avail("Available codecs: SBC AAC LDAC\n", &k); assert(k);
    unsigned aac = bt_codec_parse_avail("Available codecs: SBC AAC\n", &k); assert(k);
    unsigned sbc = bt_codec_parse_avail("Available codecs: SBC\n", &k); assert(k);
    for(int q = 2; q <= 4; q++){                      /* Best, LDAC Mobile / Standard / High */
        assert(bt_codec_pick(q, all, 1) == q);        /* LDAC offered: LDAC at that quality */
        assert(bt_codec_pick(q, aac, 1) == 1);        /* no LDAC / aptX, AAC offered: AAC */
        assert(bt_codec_pick(q, sbc, 1) == 0);        /* nothing: SBC */
        assert(bt_codec_pick(q, all, 0) == 0);        /* list unknown: SBC */
    }
    assert(bt_codec_pick(1, aac, 1) == 1 && bt_codec_pick(1, sbc, 1) == 0);   /* AAC chosen: AAC or SBC */
    assert(bt_codec_pick(0, all, 1) == 0);                                    /* SBC chosen: SBC */
    puts("PASS codec choice: Best -> LDAC -> AAC -> SBC by what the headphones offer; AAC -> SBC; unknown -> SBC");
    unsigned ax = bt_codec_parse_avail("Available codecs: SBC AAC aptX aptX-HD LDAC\n", &k); assert(k);
    assert(ax == (BTC_SBC | BTC_AAC | BTC_LDAC | BTC_APTX | BTC_APTXHD));
    unsigned axo = bt_codec_parse_avail("Available codecs: SBC aptX\n", &k); assert(k && axo == (BTC_SBC | BTC_APTX));   /* "aptX" is not "aptX-HD" */
    unsigned hdo = bt_codec_parse_avail("Available codecs: SBC aptX-HD\n", &k); assert(k && hdo == (BTC_SBC | BTC_APTXHD));
    for(int q = 2; q <= 6; q++){                                       /* Best (and the old aptX values 5 / 6) */
        int lq = q <= 4 ? q : 3;
        assert(bt_codec_pick(q, ax, 1) == lq);                        /* LDAC and aptX: LDAC */
        assert(bt_codec_pick(q, all, 1) == lq);                       /* the LDAC pair with aptX chosen: LDAC, not AAC */
        assert(bt_codec_pick(q, hdo | axo | BTC_AAC, 1) == 6);        /* no LDAC: aptX HD */
        assert(bt_codec_pick(q, axo | BTC_AAC, 1) == 5);              /* no aptX HD: aptX */
        assert(bt_codec_pick(q, hdo, 1) == 6 && bt_codec_pick(q, axo, 1) == 5);   /* aptX only: never down to SBC */
        assert(bt_codec_pick(q, aac, 1) == 1 && bt_codec_pick(q, sbc, 1) == 0);
    }
    assert(bt_codec_pick(1, ax, 1) == 1);                             /* AAC chosen: never above AAC */
    /* stepping down after a codec failed: only what is below it */
    assert(bt_codec_pick(4, ax & bt_codec_below(4), 1) == 6);         /* LDAC failed: aptX HD */
    assert(bt_codec_pick(4, ax & bt_codec_below(6), 1) == 5);         /* aptX HD failed: aptX, not back to LDAC */
    assert(bt_codec_pick(4, ax & bt_codec_below(5), 1) == 1);         /* aptX failed: AAC */
    assert(bt_codec_pick(4, ax & bt_codec_below(1), 1) == 0);         /* AAC failed: SBC */
    assert(bt_codec_pick(3, all & bt_codec_below(3), 1) == 1);        /* LDAC failed, no aptX: AAC */
    puts("PASS order: LDAC -> aptX HD -> aptX -> AAC -> SBC for every Best choice; AAC caps at AAC; steps never go back up");
    return 0;
}
'''
with tempfile.TemporaryDirectory() as d:
    p = Path(d) / "t.c"; p.write_text(C)
    subprocess.run(["cc", "-std=gnu11", "-Wall", "-o", str(Path(d) / "t"), str(p)], check=True)
    print(subprocess.run([str(Path(d) / "t")], capture_output=True, text=True, check=True).stdout.strip())
