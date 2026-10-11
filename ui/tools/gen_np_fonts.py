#!/usr/bin/env python3
"""Generate the two OFL Now Playing faces; sources/licenses live in ui/fonts/nowplaying."""
from pathlib import Path
import argparse, subprocess, tempfile
from fontTools.ttLib import TTFont
from fontTools.varLib.instancer import instantiateVariableFont
root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--converter', required=True);a=p.parse_args()
with tempfile.TemporaryDirectory() as tmp:
    for face, source, weight in [('inter','Inter',600),('nunito','Nunito',700)]:
        font=TTFont(root/'fonts/nowplaying'/f'{source}-VF.ttf')
        axes={'wght':weight}
        if face=='inter': axes['opsz']=14
        font=instantiateVariableFont(font,axes);ttf=Path(tmp)/f'{source}.ttf';font.save(ttf)
        for size in (22,30):
            out=root/f'font_np_{face}_{size}.c'
            subprocess.run(['node',a.converter,'--font',str(ttf),'--size',str(size),'--bpp','4','--format','lvgl','--range','0x20-0x7E,0xA0-0xFF,0x2018-0x201D,0x2026,0x2022','--no-compress','--force-fast-kern-format','--lv-font-name',f'font_np_{face}_{size}','-o',str(out)],check=True)
            out.write_text(out.read_text().replace(str(ttf),f'fonts/nowplaying/{source}-VF.ttf').replace(str(out),out.name).rstrip()+'\n')
