#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compress large LVGL fonts in place: plain lv_font_conv output -> LVGL's own RLE + line prefilter
(bitmap_format 1, decoded by lv_font_fmt_txt.c; needs LV_USE_FONT_COMPRESSED 1 in lv_conf.h).

Only large faces are compressed (32 px and up: clocks, headers, big numerals, icons). Their glyphs are
decoded each time they are drawn, which is cheap at these sizes; the small list/body faces stay plain so
scrolling lists never pay for it. Every glyph is decoded again here and must match the original pixels
exactly, so the rendered result is unchanged.

Usage: python3 ui/tools/compress_lv_font.py            compress every listed font (already compressed: skipped)
       python3 ui/tools/compress_lv_font.py FILE...    compress these files if they are listed (generators call this)
       python3 ui/tools/compress_lv_font.py --check    fail if a listed font is plain (run by the mq_ui build)"""
import re, sys
from pathlib import Path

UI = Path(__file__).resolve().parent.parent
LARGE = [  # paths relative to ui/; every face of 32 px and up that mq_ui links
    *[f'lvgl/src/font/lv_font_montserrat_{px}.c' for px in (32, 34, 36, 38, 40, 42, 44, 46, 48)],
    'font_simple_nunito_32.c', 'font_simple_nunito_36.c', 'font_simple_nunito_38.c',
    'font_squash_inter_32.c', 'font_squash_inter_36.c', 'font_simple_icons_36.c', 'font_simple_icons_80.c',
    'font_zune_light_32.c', 'font_zune_light_40.c', 'font_zune_light_46.c', 'font_zune_light_50.c',
    'font_zune_light_64.c', 'font_zune_light_72.c', 'font_zune_light_88.c', 'font_zune_light_96.c',
    'font_zune_light_150.c', 'font_inter_bold_44.c', 'font_stone_clk_64.c', 'font_splash_disco_72.c',
]
FMT = re.compile(r'(\.bitmap_format\s*=\s*)(\d+)')


def encode(vals, bpp):
    """Mirror of rle_next() in lv_font_fmt_txt.c: returns the bit list for one glyph."""
    out = []
    put = lambda v, n: out.extend((v >> (n - 1 - k)) & 1 for k in range(n))
    n, i, prev, state, count = len(vals), 0, 0, 'single', 0
    while i < n:
        if state == 'single':
            first = not out                     # rdp == 0: the first literal never starts a run
            v = vals[i]; put(v, bpp)
            if not first and v == prev:
                count, state = 0, 'repeated'
            prev = v; i += 1
        elif state == 'repeated':
            count += 1
            if vals[i] == prev:
                put(1, 1); i += 1
                if count == 11:
                    run = 0
                    while i + run < n and vals[i + run] == prev and run < 62: run += 1
                    c = run + 1                     # c-1 more repeats, then one literal
                    put(c, 6); i += run
                    if i < n:
                        prev = vals[i]; put(prev, bpp); i += 1
                    state = 'single'
            else:
                put(0, 1); prev = vals[i]; put(prev, bpp); i += 1; state = 'single'
    return out


def decode(data, n, bpp):
    """Python port of rle_init()/rle_next() for verification."""
    def bits(pos, ln):
        v = 0
        for k in range(ln): v = (v << 1) | ((data[(pos + k) >> 3] >> (7 - ((pos + k) & 7))) & 1)
        return v
    out, rdp, prev, state, count = [], 0, 0, 'single', 0
    for _ in range(n):
        if state == 'single':
            ret = bits(rdp, bpp)
            if rdp != 0 and prev == ret: count, state = 0, 'repeated'
            prev = ret; rdp += bpp
        elif state == 'repeated':
            v = bits(rdp, 1); count += 1; rdp += 1
            if v == 1:
                ret = prev
                if count == 11:
                    count = bits(rdp, 6); rdp += 6
                    if count != 0: state = 'counter'
                    else: ret = prev = bits(rdp, bpp); rdp += bpp; state = 'single'
            else:
                ret = prev = bits(rdp, bpp); rdp += bpp; state = 'single'
        else:
            ret = prev; count -= 1
            if count == 0: ret = prev = bits(rdp, bpp); rdp += bpp; state = 'single'
        out.append(ret)
    return out


def unpack(data, start, n, bpp):
    return [(data[(start * 8 + k * bpp) >> 3] >> (8 - bpp - ((k * bpp) & 7))) & ((1 << bpp) - 1) for k in range(n)]


def compress(path):
    src = path.read_text()
    fmt = FMT.search(src)
    if not fmt: sys.exit(f'{path}: no bitmap_format')
    if fmt.group(2) == '1': return None
    if fmt.group(2) != '0': sys.exit(f'{path}: unexpected bitmap_format {fmt.group(2)}')
    bpp = int(re.search(r'\.bpp\s*=\s*(\d+)', src).group(1))
    if bpp not in (2, 4): sys.exit(f'{path}: {bpp} bpp is not supported')
    arr = re.search(r'(glyph_bitmap\[\]\s*=\s*\{)(.*?)(\n\};)', src, re.S)
    data = bytes(int(x, 16) for x in re.findall(r'0x[0-9a-fA-F]+', re.sub(r'/\*.*?\*/', '', arr.group(2), flags=re.S)))
    names = re.findall(r'/\*\s*(U\+[0-9A-F]+ .*?)\s*\*/', arr.group(2))
    dsc = re.search(r'glyph_dsc\[\]\s*=\s*\{(.*?)\n\};', src, re.S)
    entry = re.compile(r'\.bitmap_index\s*=\s*(\d+)(,\s*\.adv_w\s*=\s*\d+,\s*\.box_w\s*=\s*(\d+),\s*\.box_h\s*=\s*(\d+))')
    glyphs = [(int(m.group(1)), int(m.group(3)), int(m.group(4))) for m in entry.finditer(dsc.group(1))]
    real = glyphs[1:]                                   # id 0 is reserved
    if len(names) != len(real): names = [''] * len(real)
    body, index, offset = [], [0], 0
    for (start, w, h), name in zip(real, names):
        vals = unpack(data, start, w * h, bpp)
        rows = [vals[r * w:(r + 1) * w] for r in range(h)]
        filt = rows[0][:] if h else []
        for r in range(1, h): filt += [a ^ b for a, b in zip(rows[r], rows[r - 1])]
        bl = encode(filt, bpp)
        bl += [0] * (-len(bl) % 8)
        enc = bytes(int(''.join(map(str, bl[k:k + 8])), 2) for k in range(0, len(bl), 8))
        # verify exactly as LVGL will decode it (prefilter undone line by line)
        got = decode(enc + b'\0', w * h, bpp)
        for r in range(1, h):
            for x in range(w): got[r * w + x] ^= got[(r - 1) * w + x]
        if got != vals: sys.exit(f'{path}: glyph {name or start} does not round-trip')
        index.append(offset)
        lines = [f'\n    /* {name} */'] if name else ['\n']
        for k in range(0, len(enc), 8): lines.append('    ' + ', '.join(f'0x{b:x}' for b in enc[k:k + 8]) + ',')
        body.append('\n'.join(lines) + '\n')
        offset += len(enc)
    if offset >= 1 << 20: sys.exit(f'{path}: bitmap larger than the 20-bit index')
    body.append('\n    /* read-ahead pad: LVGL reads one byte past a bit field that ends a glyph */\n    0x0\n')
    it = iter(index)
    new_dsc = entry.sub(lambda m: f'.bitmap_index = {next(it)}{m.group(2)}', dsc.group(1))
    out = src[:arr.start(2)] + ''.join(body).rstrip(',\n') + src[arr.end(2):dsc.start(1)] + new_dsc + src[dsc.end(1):]
    out = FMT.sub(r'\g<1>1', out, count=1)
    out = out.replace(' * Bpp:', ' * Compressed: ui/tools/compress_lv_font.py (LVGL RLE + line prefilter)\n * Bpp:', 1)
    path.write_text(out)
    return len(data), offset + 1


def main(argv):
    if argv == ['--check']:
        plain = [p for p in LARGE if FMT.search((UI / p).read_text()).group(2) != '1']
        if plain: sys.exit('compress_lv_font: plain large fonts (run ui/tools/compress_lv_font.py): ' + ' '.join(plain))
        return
    files = [Path(a).resolve() for a in argv] if argv else [UI / p for p in LARGE]
    listed = {(UI / p).resolve() for p in LARGE}
    for f in files:
        if f not in listed: continue
        r = compress(f)
        print(f'{f.relative_to(UI)}: ' + ('already compressed' if r is None else f'{r[0]} -> {r[1]} bytes'))


if __name__ == '__main__': main(sys.argv[1:])
