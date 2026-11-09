#!/usr/bin/env python3
"""Nunito Bold extension for Simple; existing faces and themes are unchanged."""
from pathlib import Path
import argparse,subprocess,tempfile
from fontTools.ttLib import TTFont
from fontTools.varLib.instancer import instantiateVariableFont
root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--converter',required=True);args=p.parse_args()
with tempfile.TemporaryDirectory() as t:
 f=instantiateVariableFont(TTFont(root/'fonts/nowplaying/Nunito-VF.ttf'),{'wght':700});src=Path(t)/'Nunito-Bold.ttf';f.save(src)
 for px in (26,28,32,36,38):
  name=f'font_simple_nunito_{px}';out=root/(name+'.c')
  subprocess.run(['node',args.converter,'--font',str(src),'--size',str(px),'--bpp','4','--format','lvgl','--range','0x20-0x7E,0xA0-0xFF,0x2018-0x201D,0x2026,0x2022','--no-compress','--force-fast-kern-format','--lv-font-name',name,'-o',str(out)],check=True)
  out.write_text(out.read_text().replace(str(src),'fonts/nowplaying/Nunito-VF.ttf (wght=700)').replace(str(out),out.name))

# Solid Font Awesome symbols at their native drawing sizes.
symbols='0xF0A0,0xF028,0xF00B,0xF287,0xF0EA,0xF0EC,0xF001,0xF015,0xF07B,0xF013,0xF009,0xF0C9,0xF03A,0xF002,0xF03E,0xF051,0xF0C5,0xF15B,0xF130,0xF192,0xF074,0xF04B,0xF04C,0xF185,0xF1EB,0xF293,0xF0E7,0xF021,0xF048,0xF050,0xF077,0xF054,0xF053,0xF240,0xF244,0xF004,0xF1C7'
for px in (36,80):
 name=f'font_simple_icons_{px}';out=root/(name+'.c')
 subprocess.run(['node',args.converter,'--font',str(root/'fonts/simple/fontawesome-webfont.ttf'),'--size',str(px),'--bpp','4','--format','lvgl','--range',symbols,'--no-compress','--force-fast-kern-format','--lv-font-name',name,'-o',str(out)],check=True)
 out.write_text(out.read_text().replace(str(root)+'/',''))

for name in ("font_simple_nunito_26.c","font_simple_nunito_36.c","font_simple_nunito_28.c","font_simple_nunito_32.c","font_simple_nunito_38.c","font_simple_icons_36.c","font_simple_icons_80.c"):
 out=root/name;out.write_text(out.read_text().rstrip()+"\n")
