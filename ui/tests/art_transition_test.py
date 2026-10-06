#!/usr/bin/env python3
"""Reproduce a superseded album-load failure through production UI and real decoder."""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
import subprocess, os, argparse
p=argparse.ArgumentParser();p.add_argument('--baseline',action='store_true');p.add_argument('--out',type=Path);a=p.parse_args()
app=Path(__file__).resolve().parents[1];out=a.out or app/'tests/host_build/art-transition';out.mkdir(parents=True,exist_ok=True)
phase='before' if a.baseline else 'after';env=os.environ.copy()
if a.baseline:env['DISKOS_ART_RACE_EXPECT_MISSING']='1'
else:env.pop('DISKOS_ART_RACE_EXPECT_MISSING',None)
for preset,variant,name,page in [(8,1,'ring-light','home'),(8,1,'ring-light','np'),(8,0,'ring-dark','home'),(8,0,'ring-dark','np'),(9,1,'braun-light','np'),(9,0,'braun-dark','np')]:
 stem=out/f'{phase}-{name}-{page}'
 cover=Image.new('RGB',(480,480),'#283442');d=ImageDraw.Draw(cover);d.ellipse((90,60,400,370),fill='#ce713e');d.polygon([(0,350),(180,250),(300,360),(390,260),(480,390),(480,480),(0,480)],fill='#25282c');d.text((95,410),'AFTERGLOW',fill='white',font=ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',30));cover.save(stem.with_suffix('.png'))
 (app/'tests/host_build/diskos.conf').unlink(missing_ok=True)
 subprocess.run([str(app/'tests/host_build/host-render'),str(preset),str(variant),'preview-art-race-'+page,'0',str(stem.with_suffix('.ppm'))],env=env,check=True,timeout=15)
 Image.open(stem.with_suffix('.ppm')).save(stem.with_suffix('.png'))
print('PASS: all 6 controlled transition scenarios matched',phase)
