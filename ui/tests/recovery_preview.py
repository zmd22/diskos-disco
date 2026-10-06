#!/usr/bin/env python3
"""Render production widgets; only music/firmware/IPC fixture data is substituted."""
from pathlib import Path
import subprocess
from PIL import Image, ImageDraw, ImageFont
app=Path(__file__).resolve().parents[1];out=app.parent/'docs/fork/renders/recovery-20261003';out.mkdir(parents=True,exist_ok=True)
cover=Image.new('RGB',(480,480));p=cover.load()
for y in range(480):
 for x in range(480):
  t=y/479;p[x,y]=(int(34+112*t),int(37+23*t),int(64-28*t))
d=ImageDraw.Draw(cover);d.ellipse((95,75,385,365),fill='#ce713e');d.polygon([(0,340),(95,270),(195,360),(280,240),(480,370),(480,480),(0,480)],fill='#25282c');d.text((110,405),'AFTERGLOW',fill='#f5e7d1',font=ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',30))
exe=app/'tests/host_build/host-render'
for preset,variant,name in [(8,1,'ring-light'),(8,0,'ring-dark'),(9,1,'braun')]:
 for page in ('home','quick','np','eq','bands','display','low','charging'):
  stem=out/(name+'-'+page);cover.save(stem.with_suffix('.png'))
  r=subprocess.run([str(exe),str(preset),str(variant),'preview-'+page,'1',str(stem.with_suffix('.ppm'))],capture_output=True,text=True)
  if r.returncode:raise SystemExit(r.stdout+r.stderr)
  im=Image.open(stem.with_suffix('.ppm')).convert('RGB');mask=Image.new('L',(360,360));ImageDraw.Draw(mask).ellipse((0,0,359,359),fill=255);framed=Image.new('RGB',(360,360),'#191c20');framed.paste(im,(0,0),mask);framed.save(stem.with_suffix('.png'))
font=ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',18)
for group,pages in [('ring-backdrops',('home','quick')),('eq-and-colours',('eq','bands','np')),('battery-and-settings',('low','charging','display'))]:
 names=['ring-light','ring-dark'] if group=='ring-backdrops' else ['ring-light','braun']
 sheet=Image.new('RGB',(len(pages)*384,len(names)*404),'#191c20');draw=ImageDraw.Draw(sheet)
 for row,name in enumerate(names):
  for col,page in enumerate(pages):
   im=Image.open(out/(name+'-'+page+'.png'));sheet.paste(im,(col*384+12,row*404+38));draw.text((col*384+12,row*404+10),name+' / '+page,fill='white',font=font)
 sheet.save(out/(group+'.png'))
print(out)
