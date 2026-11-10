#!/usr/bin/env python3
"""Pixel and bounds checks for cached Immersive lyric movement and word wipes."""
from pathlib import Path
import os
import subprocess
import tempfile

app = Path(__file__).resolve().parents[1]
harness = r'''
#include "imm_record.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static uint32_t cover[364*364], frame[360*360];
static void render(void) { imm_record_render(cover,364,364,frame,0,170); }
static uint32_t pixel(int x,int y) { return frame[y*360+x]&0xFFFFFFu; }
int main(void) {
    imm_record_set_smoothing(0);
    imm_record_set_ring(0,0,0,0);
    uint32_t glyph[4*8];
    for(int i=0;i<4*8;i++)glyph[i]=0xFFFFFFFFu;
    imm_overlay_t line={glyph,140,230,6,4,8,0};
    imm_record_set_overlays(&line,1);
    render();
    assert(pixel(140,230)==0xFFFFFFu && pixel(145,233)==0xFFFFFFu);
    assert(pixel(146,230)==0 && pixel(140,234)==0);

    /* A stationary cached glyph moves continuously without a new snapshot. */
    imm_record_set_overlay_motion(0,-17,255);
    render();
    assert(pixel(140,213)==0xFFFFFFu && pixel(140,230)==0);
    imm_record_set_overlay_motion(0,-9,255);
    render();
    assert(pixel(140,221)==0xFFFFFFu && pixel(140,213)==0);

    /* Word clips use snapshot coordinates even while the row is moving. */
    imm_lyric_clip_t clips[]={{0,0,1,3},{4,2,5,3}};
    imm_record_set_lyric_highlight(0,clips,2,90);
    render();
    assert(pixel(140,221)==0xFFFFFFu && pixel(141,224)==0xFFFFFFu);
    assert(pixel(142,221)>0 && pixel(142,221)<0xFFFFFFu);
    assert(pixel(144,221)==pixel(142,221));
    assert(pixel(144,223)==0xFFFFFFu && pixel(145,224)==0xFFFFFFu);
    uint32_t dim=pixel(142,221);
    imm_record_set_lyric_highlight(0,NULL,0,90);
    render(); assert(pixel(140,221)==dim); /* before the first timestamp */
    imm_lyric_clip_t whole={0,0,5,3};
    imm_record_set_lyric_highlight(0,&whole,1,90);
    render(); assert(pixel(142,221)==0xFFFFFFu); /* completed line */

    /* Fading the outgoing row combines with its existing glyph alpha. */
    imm_record_set_overlay_motion(0,-9,128);
    render(); assert(pixel(142,221)>dim && pixel(142,221)<0xFFFFFFu);
    imm_record_set_overlay_motion(0,-9,0);
    render(); assert(pixel(142,221)==0);
    imm_record_set_overlay_motion(-1,999,255);
    imm_record_set_overlay_motion(9,999,255);
    render(); assert(pixel(142,221)==0);

    /* Replacing snapshots clears old motion, fades and timing clips. */
    imm_record_set_overlays(&line,1);
    render(); assert(pixel(142,230)==0xFFFFFFu && pixel(142,221)==0);
    line.x=-2;line.y=-1;
    imm_record_set_overlays(&line,1);
    render(); assert(pixel(0,0)==0xFFFFFFu && pixel(3,2)==0xFFFFFFu);
    line.x=358;line.y=358;
    imm_record_set_overlays(&line,1);
    render(); assert(pixel(359,359)==0xFFFFFFu);
    imm_record_set_overlay_motion(0,10,255);render(); /* fully outside frame */
    imm_record_set_overlays(NULL,0);render();
    puts("PASS: cached lyric row motion, word clips, alpha fades, reset and edge bounds (ASan/UBSan)");
}
'''
with tempfile.TemporaryDirectory(prefix="disco-imm-lyrics-") as tmp:
    src = Path(tmp) / "test.c"
    exe = Path(tmp) / "test"
    src.write_text(harness)
    subprocess.run([os.environ.get("CC", "cc"), "-std=gnu11", "-O2", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", "-I" + str(app), str(src),
                    str(app / "imm_record.c"), "-lm", "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True, env={**os.environ, "ASAN_OPTIONS": "detect_leaks=0"})
