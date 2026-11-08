#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run-length encode Boomshine's embedded glyphs (ui/boomshine/fonts.h) in place.

Each glyph is a w*h stream of 8-bit coverage values, row by row. A non-zero byte is one pixel; a zero byte is
followed by a count (1-255) of transparent pixels. Runs never cross glyphs, Glyph.off is the stream start, and
text_at() in main.c draws straight from the stream, so the pixels drawn are exactly those of the plain table.
Usage: python3 ui/tools/boomshine_font_rle.py   (an already encoded fonts.h is left alone)"""
import re, sys
from pathlib import Path

FONTS = Path(__file__).resolve().parent.parent / 'boomshine/fonts.h'
MARK = '#define BOOMSHINE_FONT_RLE 1'


def encode(px):
    out, i = bytearray(), 0
    while i < len(px):
        if px[i]:
            out.append(px[i]); i += 1
        else:
            j = i
            while j < len(px) and not px[j] and j - i < 255: j += 1
            out += bytes((0, j - i)); i = j
    return bytes(out)


def decode(rle, n):
    out, k = [], 0
    while len(out) < n:
        if rle[k]: out.append(rle[k]); k += 1
        else: out += [0] * rle[k + 1]; k += 2
    return out


def main():
    src = FONTS.read_text()
    if MARK in src: print('fonts.h: already encoded'); return
    before = after = 0
    for size in re.findall(r'pixels_(\d+)\[\]', src):
        pm = re.search(r'(static const unsigned char pixels_%s\[\] = \{)([^}]*)(\};)' % size, src)
        gm = re.search(r'(static const Glyph glyphs_%s\[\] = \{)(.*?)(\};)' % size, src)
        px = bytes(int(v) for v in pm.group(2).split(','))
        glyphs = [tuple(map(int, g)) for g in re.findall(r'\{(\d+),(\d+),(\d+),(\d+)\}', gm.group(2))]
        stream, table = bytearray(), []
        for off, w, h, adv in glyphs:
            plain = px[off:off + w * h]
            enc = encode(plain)
            if decode(enc, w * h) != list(plain): sys.exit(f'pixels_{size}: glyph at {off} does not round-trip')
            table.append(f'{{{len(stream)},{w},{h},{adv}}}'); stream += enc
        before += len(px); after += len(stream)
        src = src[:gm.start(2)] + ','.join(table) + src[gm.end(2):]
        pm = re.search(r'(static const unsigned char pixels_%s\[\] = \{)([^}]*)(\};)' % size, src)
        src = src[:pm.start(2)] + ','.join(map(str, stream)) + src[pm.end(2):]
    src = src.replace('\ntypedef struct { unsigned off;',
                      f'\n/* Run-length encoded by ui/tools/boomshine_font_rle.py: a non-zero byte is one pixel, 0 n is n clear pixels. */\n{MARK}\ntypedef struct {{ unsigned off;', 1)
    FONTS.write_text(src)
    print(f'fonts.h: {before} -> {after} bytes of glyph data')


if __name__ == '__main__': main()
