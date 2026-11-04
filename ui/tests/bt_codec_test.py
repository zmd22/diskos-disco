#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Codec choice: LDAC (any quality) -> AAC -> SBC, decided from what the headphones offer (production functions)."""
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
''' + fn("static unsigned bt_codec_need(") + "\n" + fn("int bt_codec_pick(") + "\n" + fn("unsigned bt_codec_parse_avail(") + r'''
int main(void){
    int k; unsigned all = bt_codec_parse_avail("Available codecs: SBC AAC LDAC\n", &k); assert(k);
    unsigned aac = bt_codec_parse_avail("Available codecs: SBC AAC\n", &k); assert(k);
    unsigned sbc = bt_codec_parse_avail("Available codecs: SBC\n", &k); assert(k);
    for(int q = 2; q <= 4; q++){                      /* LDAC Mobile / Standard / High */
        assert(bt_codec_pick(q, all, 1) == q);        /* offered: as chosen */
        assert(bt_codec_pick(q, aac, 1) == 1);        /* no LDAC, AAC offered: AAC */
        assert(bt_codec_pick(q, sbc, 1) == 0);        /* neither: SBC */
        assert(bt_codec_pick(q, all, 0) == 0);        /* list unknown: SBC */
    }
    assert(bt_codec_pick(1, aac, 1) == 1 && bt_codec_pick(1, sbc, 1) == 0);   /* AAC chosen: AAC or SBC */
    assert(bt_codec_pick(0, all, 1) == 0);                                    /* SBC chosen: SBC */
    puts("PASS codec choice: LDAC (any quality) -> AAC -> SBC by what the headphones offer; AAC -> SBC; unknown -> SBC");
    unsigned ax = bt_codec_parse_avail("Available codecs: SBC AAC aptX aptX-HD LDAC\n", &k); assert(k);
    assert(ax == (BTC_SBC | BTC_AAC | BTC_LDAC | BTC_APTX | BTC_APTXHD));
    unsigned axo = bt_codec_parse_avail("Available codecs: SBC aptX\n", &k); assert(k && axo == (BTC_SBC | BTC_APTX));   /* "aptX" is not "aptX-HD" */
    unsigned hdo = bt_codec_parse_avail("Available codecs: SBC aptX-HD\n", &k); assert(k && hdo == (BTC_SBC | BTC_APTXHD));
    assert(bt_codec_pick(6, ax, 1) == 6 && bt_codec_pick(5, ax, 1) == 5);
    assert(bt_codec_pick(6, axo | BTC_AAC, 1) == 5);   /* no aptX HD: aptX */
    assert(bt_codec_pick(6, aac, 1) == 1 && bt_codec_pick(5, aac, 1) == 1);   /* no aptX: AAC */
    assert(bt_codec_pick(6, sbc, 1) == 0 && bt_codec_pick(5, all, 0) == 0);   /* nothing / unknown: SBC */
    for(int q = 2; q <= 4; q++){                                       /* LDAC: the best the headphones offer */
        assert(bt_codec_pick(q, hdo | axo | BTC_AAC, 1) == 6);        /* no LDAC: aptX HD */
        assert(bt_codec_pick(q, axo | BTC_AAC, 1) == 5);              /* no aptX HD: aptX */
        assert(bt_codec_pick(q, hdo, 1) == 6 && bt_codec_pick(q, axo, 1) == 5);   /* aptX only: never down to SBC */
        assert(bt_codec_pick(q, aac, 1) == 1 && bt_codec_pick(q, sbc, 1) == 0);
    }
    assert(bt_codec_pick(6, hdo | axo | BTC_LDAC, 1) == 6 && bt_codec_pick(5, ax, 1) == 5);   /* never steps UP to LDAC */
    assert(bt_codec_pick(5, hdo | BTC_AAC, 1) == 1);                  /* aptX chosen, only aptX HD offered: AAC (no step up) */
    puts("PASS order: LDAC -> aptX HD -> aptX -> AAC -> SBC; aptX HD -> aptX -> AAC -> SBC; aptX -> AAC -> SBC; never upward");
    return 0;
}
'''
with tempfile.TemporaryDirectory() as d:
    p = Path(d) / "t.c"; p.write_text(C)
    subprocess.run(["cc", "-std=gnu11", "-Wall", "-o", str(Path(d) / "t"), str(p)], check=True)
    print(subprocess.run([str(Path(d) / "t")], capture_output=True, text=True, check=True).stdout.strip())
