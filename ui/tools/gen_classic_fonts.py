#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Classic theme fonts: ChicagoFLF (public domain) converted with lv_font_conv 1.5.3.
Output: ui/font_classic_<px>.c, LVGL name font_classic_<px>.
Usage: python3 ui/tools/gen_classic_fonts.py --converter /path/to/lv_font_conv.js"""
import argparse, subprocess
from pathlib import Path
UI = Path(__file__).resolve().parent.parent
TEXT = '0x20-0x7E,0xA0-0xAC,0xAE-0xFF,0x2018-0x201D,0x2026,0x2022,0x203A'
DIGITS = '0x20,0x25,0x2D,0x2E,0x30-0x3A,0xB0'
CUTS = [(12, TEXT), (14, TEXT), (16, TEXT), (18, TEXT), (20, TEXT), (22, TEXT), (24, TEXT), (28, TEXT), (32, TEXT), (64, DIGITS)]
def main():
    p = argparse.ArgumentParser(); p.add_argument('--src', type=Path, default=UI / 'fonts/classic/ChicagoFLF.ttf')
    p.add_argument('--converter', type=Path, default=UI.parent / 'node_modules/lv_font_conv/lv_font_conv.js'); a = p.parse_args()
    for px, rng in CUTS:
        fn = f'font_classic_{px}'; dest = UI / f'{fn}.c'
        subprocess.run(['node', str(a.converter), '--font', str(a.src), '--size', str(px), '--bpp', '4', '--format', 'lvgl',
                        '--range', rng, '--no-compress', '--force-fast-kern-format', '--lv-font-name', fn, '-o', str(dest)], check=True)
        dest.write_text(dest.read_text().replace(str(a.src), 'fonts/classic/ChicagoFLF.ttf').replace(str(dest), dest.name).rstrip() + '\n')
        print(f'made {fn}', flush=True)
if __name__ == '__main__': main()
