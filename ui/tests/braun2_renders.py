#!/usr/bin/env python3
"""Capture production screens with host fixtures. Build host-render first."""
from pathlib import Path
import subprocess,os,json,re,tempfile,shutil
from PIL import Image,ImageDraw,ImageFont
root=Path(__file__).resolve().parents[2];out=root/'docs/themes/braun2-renders';out.mkdir(exist_ok=True)
tmp=Path(tempfile.mkdtemp(prefix='braun2-renders-'));Image.new('RGB',(360,360),'#815c47').save(tmp/'art.png')
s=(root/'ui/screens.h').read_text();names=re.search(r'enum \{ (SCR_HOME.*?)SCR_COUNT \}',s).group(1).replace('\n','').replace(' ','').strip(',').split(',')
pages={'HOME':'home','NOWPLAYING':'np','QUICK':'quick','LIBRARY':'library','SETTINGS':'settings','WEATHER':'weather','BT':'bt','WIFI':'wifi','SEARCH':'search','SONGINFO':'info','WORKMODE':'modes','EQ':'eq','EQ_EDITOR':'bands','USAGE':'battery','QUEUE':'queue','UPNEXT':'upnext','LASTFM':'lastfm','SAVER':'saver','WIFI_INFO':'wifiinfo','SETTING_DETAIL':'setkey'}
manifest=[]
for v in (0,1):
 for n,name in enumerate(names):
  key=name[4:];page=pages.get(key,'scr'+str(n));fn=out/f'{n:02d}-{key.lower()}-{v}.png';ppm=tmp/'output.ppm'
  (root/'ui/tests/host_build/diskos.conf').unlink(missing_ok=True)
  (root/'ui/tests/host_build/queue.tsv').unlink(missing_ok=True)
  env={**os.environ,'ZNP_PATH':str(tmp/'art.png'),'CFG_INT':'disco_hints=2','PREVIEW_SETTLE':'300','SITE_CAPTURE':'1','ASSERT_NO_VERTICAL':'1','NP_TITLE':'Hey Jude','NP_ARTIST':'The Beatles','NP_ALBUM':'Past Masters','FONTSZ':'1'}
  if key=='EQ_EDITOR':env['EQ_PRESET']='11'
  if key=='SETTING_DETAIL':env['SETKEY']='brightness'
  if key=='SETLIST':page='display'
  special={'STATS':'statsapp','APPSHELF':'appshelf','APPPICK':'appshelf-pick','APPLAND':'statsapp-land','ROULETTE_SPIN':'roulette-1200','SPHERE':'ssphere-0','SHELF':'shelf-artists'}
  capture=special.get(key,'preview-'+page)
  r=subprocess.run([str(root/'ui/tests/host_build/host-render'),'14',str(v),capture,'0',str(ppm)],env=env,capture_output=True,text=True,timeout=35)
  if r.returncode:print('FAIL',key,v,r.stderr[-200:],flush=True);manifest.append({'screen':name,'variant':v,'error':r.stderr[-400:]});continue
  im=Image.open(ppm);im.save(fn);manifest.append({'screen':name,'variant':v,'file':fn.name});print(key,v,flush=True)
 # A readable overview; individual images remain native 360x360.
 ims=[x for x in manifest if x['variant']==v and 'file'in x]
 sheet=Image.new('RGB',(6*250,((len(ims)+5)//6)*275),'#d8d3ca' if v else '#131311');d=ImageDraw.Draw(sheet)
 for i,x in enumerate(ims):
  im=Image.open(out/x['file']).resize((240,240));mask=Image.new('L',(240,240));ImageDraw.Draw(mask).ellipse((0,0,239,239),fill=255);xx=(i%6)*250+5;yy=(i//6)*275;sheet.paste(im,(xx,yy),mask);d.text((xx+5,yy+244),x['screen'][4:],fill='#1c1b19' if v else '#eeeae2')
 sheet.quantize(colors=96).save(out/f'overview-{v}.png',optimize=True)
# Stateful subscreens use their real public routes, not bare screen roots.
extras=[('settings-'+g.lower().replace(' ','_').replace('&','and'),'preview-group',{'SETGROUP':g}) for g in
        ('Playback','Audio','Theme settings','Appearance','Music screen','Theme navigation','Text & layout','Library appearance','Battery & power','Help','Maintenance','Date & Time','Bluetooth','Sleep timer')]
extras += [('standby-'+str(i),'preview-saver',{'SAVER_STYLE':str(i)}) for i in range(6)]
extras += [('volume','preview-home',{'VOLSHOW':'62'}),('confirm','preview-home',{'POPUP':'confirm'}),
           ('song-actions','preview-home',{'POPUP':'track'}),('eq-off','preview-bands',{'CFG_INT':'disco_hints=2,eq_preset=0'}),
           ('eq-builtin','preview-bands',{'CFG_INT':'disco_hints=2,eq_preset=1'})]
for v in (0,1):
 for name,page,extra in extras:
  (root/'ui/tests/host_build/diskos.conf').unlink(missing_ok=True)
  env={**os.environ,'ZNP_PATH':str(tmp/'art.png'),'CFG_INT':'disco_hints=2','PREVIEW_SETTLE':'100','NP_TITLE':'Hey Jude','NP_ARTIST':'The Beatles','NP_ALBUM':'Past Masters',**extra}
  r=subprocess.run([str(root/'ui/tests/host_build/host-render'),'14',str(v),page,'0',str(tmp/'output.ppm')],env=env,capture_output=True,text=True,timeout=35)
  fn=f'{name}-{v}.png'
  if r.returncode:manifest.append({'screen':name,'variant':v,'error':r.stderr[-400:]});print('FAIL',name,v,r.stderr[-200:],flush=True)
  else:Image.open(tmp/'output.ppm').save(out/fn);manifest.append({'screen':name,'variant':v,'file':fn});print(name,v,flush=True)
(out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')

# The three approved layouts at native resolution in both appearances.
sheet=Image.new('RGB',(1160,850),'#f5f3ed');d=ImageDraw.Draw(sheet)
for v in (1,0):
 for i,(n,label) in enumerate(((0,'Clock Home'),(2,'Music C'),(7,'Quick Settings'))):
  x=20+i*380;y=40 if v else 460
  f=next(z for z in manifest if z['variant']==v and z['screen']==names[n])
  im=Image.open(out/f['file']);mask=Image.new('L',(360,360));ImageDraw.Draw(mask).ellipse((0,0,359,359),fill=255);sheet.paste(im,(x,y),mask)
  d.text((x+120,y-24),label,fill='#1c1b19')
sheet.save(out/'core-comparison.png')
shutil.rmtree(tmp)
assert not any('error' in x for x in manifest), 'See manifest.json for failed captures'
