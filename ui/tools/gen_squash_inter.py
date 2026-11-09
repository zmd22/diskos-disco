#!/usr/bin/env python3
"""Squash metadata: Inter Semibold, fixed 36/32/28 px; leaves older faces unchanged."""
from pathlib import Path
import argparse,subprocess,tempfile
from fontTools.ttLib import TTFont
from fontTools.varLib.instancer import instantiateVariableFont
root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--converter',required=True);a=p.parse_args()
with tempfile.TemporaryDirectory() as tmp:
 font=instantiateVariableFont(TTFont(root/'fonts/nowplaying/Inter-VF.ttf'),{'wght':600,'opsz':14});ttf=Path(tmp)/'Inter.ttf';font.save(ttf)
 for size in (28,32,36):
  name=f'font_squash_inter_{size}';out=root/(name+'.c')
  subprocess.run(['node',a.converter,'--font',str(ttf),'--size',str(size),'--bpp','4','--format','lvgl','--range','0x20-0x7E,0xA0-0xFF,0x2018-0x201D,0x2026,0x2022','--no-compress','--force-fast-kern-format','--lv-font-name',name,'-o',str(out)],check=True)
  out.write_text(out.read_text().replace(str(ttf),'fonts/nowplaying/Inter-VF.ttf').replace(str(out),out.name).rstrip()+'\n')
