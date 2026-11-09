#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Zune theme fonts: Open Sans (SIL OFL 1.1) cut to fixed weights, converted with lv_font_conv 1.5.3.
Output: ui/font_zune_<weight>_<px>.c, LVGL name font_zune_<weight>_<px>.
Usage: python3 ui/tools/gen_zune_fonts.py --src ../fonts/OpenSans-VF.ttf"""
import argparse, subprocess
from pathlib import Path
UI = Path(__file__).resolve().parent.parent
TEXT = '0x20-0x7E,0xA0-0xFF,0x400-0x45F,0x2018-0x201D,0x2026,0x2022,0x203A'   # Latin-1, Cyrillic (RU/UA/BE/SR/BG), punctuation
DIGITS = '0x20,0x25,0x2D,0x2E,0x30-0x3A,0xB0'          # space % - . 0-9 : °
WORDMARK = '0x63,0x64,0x69,0x73'                       # "disc" (the boot splash)
CUTS = {  # weight name -> (wght, [(px, range)])
    'light':    (300, [(18, TEXT), (21, TEXT), (24, TEXT), (32, TEXT), (46, TEXT), (50, TEXT), (28, TEXT), (40, TEXT), (72, DIGITS), (88, DIGITS), (96, DIGITS), (150, DIGITS), (64, WORDMARK)]),
    'regular':  (400, [(12, TEXT), (13, TEXT), (14, TEXT), (15, TEXT), (16, TEXT), (17, TEXT), (19, TEXT)]),
    'bold': (700, [(14, TEXT)]),
    'semibold': (600, [(10, TEXT), (11, TEXT), (12, TEXT), (22, TEXT), (24, TEXT)]),
}
def main():
    p = argparse.ArgumentParser(); p.add_argument('--src', type=Path, default=UI / 'fonts/zune/OpenSans-VF.ttf')
    p.add_argument('--only',help='Generate only one LVGL font symbol');p.add_argument('--converter', type=Path, default=UI.parent / 'node_modules/lv_font_conv/lv_font_conv.js'); a = p.parse_args()
    from fontTools.ttLib import TTFont; from fontTools.varLib import instancer
    tmp = UI.parent / '.fontcache'; tmp.mkdir(exist_ok=True)
    # Existing symbol names are nominal slots. Enlarge their raster cuts, keeping all
    # callers and fallback chains intact. No bitmap scaling or extra runtime font cache.
    for name, (w, sizes) in CUTS.items():
        for px, rng in sizes:
            if a.only and a.only != f'font_zune_{name}_{px}': continue
            if name == 'light' and px == 64: continue  # drawn logo, not interface text
            actual = px + (1 if px <= 17 else 2 if px <= 50 else 3)
            weight = (500 if px in (32, 46) else 350 if px <= 28 else 325) if name == 'light' else 425 if name == 'regular' else w
            ttf = tmp / f'OpenSans-{name}-{weight}.ttf'
            instancer.instantiateVariableFont(TTFont(a.src), {'wght': weight, 'wdth': 100}).save(ttf)
            fn = f'font_zune_{name}_{px}'
            dest = UI / f'{fn}.c'
            subprocess.run(['node', str(a.converter), '--font', str(ttf), '--size', str(actual), '--bpp', '4', '--format', 'lvgl',
                            '--range', rng, '--no-compress', '--force-fast-kern-format', '--lv-font-name', fn,
                            '-o', str(dest)], check=True)
            dest.write_text(dest.read_text().replace(str(ttf), f'fonts/zune/OpenSans-VF.ttf@wght={weight}').replace(str(dest), dest.name).rstrip()+'\n')
            print(f'made {fn}: {actual}px / weight {weight}', flush=True)
if __name__ == '__main__': main()
