#!/usr/bin/env python3
"""Capture the real LVGL UI for the website, with illustrated sample sleeves/history.
Run in a disposable Linux container only: the host harness replaces /usr/data/fiio/db/song.db.
Build first: make -C ui CROSS= host-render
"""
import argparse, json, math, os, re, shutil, sqlite3, subprocess, tempfile
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
ROOT = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--output', type=Path, default=ROOT/'docs/site')
p.add_argument('--allow-host-fixtures', action='store_true')
a = p.parse_args()
if not a.allow_host_fixtures: p.error('--allow-host-fixtures is required; never run on a player')
colors = {'magenta':'ec008c', 'cyan':'00b7d8', 'orange':'ff751f', 'lime':'9bd238'}
albums = ['Endless Summer','OutRun','Night Drive','Atlas','Monsters','Turbo Kid','Dreamwave','Kids']
artists = ['The Midnight','Kavinsky','Timecop1983','FM-84','The Midnight','Le Matos','Gunship','The Midnight']
titles = ['Sunset','Vampires','Jason','Gloria','Endless Summer','Synthetic','The Comeback','Memories','Night Skies','Before the Dawn']
# Original geometric demo sleeves, extending the website's existing illustrated fixtures.
# These are not the artists' official album covers.
def sleeve(k):
    palettes=[('#19173b','#ea7580','#ffca85'),('#252135','#f14263','#ffd987'),('#132635','#57b6c7','#f6c998'),('#f5e6c7','#186c85','#e76947'),('#153a39','#99ca98','#ffbe94'),('#1b1950','#846cde','#f294bd'),('#e4a77f','#5c3f75','#f5d890'),('#19465d','#4eb7c2','#ffba74')]
    bg,mid,fg=palettes[k]; im=Image.new('RGB',(600,600),bg);d=ImageDraw.Draw(im)
    if k in (0,2,7):
        d.ellipse((160,120,460,420),fill=fg)
        for j in range(7):
            y=310+j*31;d.rectangle((0,y,600,y+13+j*2),fill=bg)
        d.polygon([(0,475),(165,340),(300,465),(440,355),(600,485),(600,600),(0,600)],fill=mid)
        d.polygon([(0,545),(260,415),(420,525),(600,455),(600,600),(0,600)],fill=bg)
    elif k in (1,5):
        for j in range(9):
            pad=60+j*22;d.rounded_rectangle((pad,80+pad//3,600-pad,530-pad//4),radius=75,outline=mid if j%2 else fg,width=7)
        d.polygon([(220,600),(300,320),(380,600)],fill=bg)
        d.line((300,340,300,600),fill=fg,width=3)
    elif k==3:
        for j in range(5):
            x=70+j*78;d.rectangle((x,140,x+45,450-j*35),fill=mid if j%2 else fg)
        d.ellipse((350,310,515,475),fill=mid)
    elif k==4:
        for j in range(12):
            x=40+j*45;d.arc((x-190,110,x+190,510),260,90,fill=mid if j%2 else fg,width=11)
    else:
        for j in range(7):
            y=140+j*50;d.polygon([(0,y),(300,y-90),(600,y),(600,y+25),(300,y-65),(0,y+25)],fill=mid if j%2 else fg)
    font=ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf',30)
    small=ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',16)
    d.text((36,30),albums[k].upper(),font=font,fill=fg)
    d.text((38,75),'ILLUSTRATED DEMO SLEEVE / %02d'%(k+1),font=small,fill=fg)
    return im

with tempfile.TemporaryDirectory(prefix='disco-site-') as temp:
    tmp=Path(temp);art=tmp/'art';art.mkdir()
    for k in range(8):
        folder=art/str(k);folder.mkdir();im=sleeve(k)
        im.save(folder/'cover.png')
        for t in range(10): im.save(folder/f'{t+1:02d}.png')
        for kind,size in [('c',148),('t',42)]:im.resize((size,size),Image.Resampling.LANCZOS).save(art/f'a{k}_{kind}.bmp')
    db=tmp/'catalog.db';con=sqlite3.connect(db)
    cols='ID INTEGER PRIMARY KEY,PATH,NAME,TITLE,ALBUM,ARTIST,GENRE,DISC,TRACK,IS_CUE,IS_ISO,IS_DSD,OFFSET,DURATION,ALBUM_ARTIST,IS_M3U,M3U_PATH'
    for table in ['SONG','MY_LOVE']:con.execute(f'CREATE TABLE {table}({cols})')
    con.execute('CREATE TABLE CUSTOM_PLAYLIST_INDEX(LIST_ID INTEGER PRIMARY KEY,LIST_NAME,M3U_PATH)')
    con.execute(f'CREATE TABLE CUSTOM_PLAYLIST({cols},PLAYLIST_ID)')
    for k,(album,artist) in enumerate(zip(albums,artists)):
        for t,title in enumerate(titles):
            name=f'{t+1:02d}.png'
            con.execute('INSERT INTO SONG VALUES('+','.join('?'*17)+')',(k*10+t+1,str(art/str(k)/name),name,title,album,artist,'Electronic',1,t+1,0,0,0,0,240000,artist,0,''))
    con.commit();con.close()
    dest=Path('/usr/data/fiio/db/song.db');dest.parent.mkdir(parents=True,exist_ok=True)
    manifest=[]
    def capture(name,theme,mode,extra=None,light=0):
        extra=extra or {};shutil.copy2(db,dest)
        # Each render starts clean; settings from a preceding scene must not leak.
        (ROOT/'ui/tests/host_build/diskos.conf').unlink(missing_ok=True)
        env={**os.environ,'ZNP_PATH':str(art/'0/01.png'),'SITE_CAPTURE':'1','PREVIEW_TECH':'1','NULL_DEMO_FLAC':'1','CFG_INT':'disco_hints=2','NP_TITLE':'Sunset','NP_ALBUM':'Endless Summer',**extra}
        if mode.startswith('statsapp'):env['STATS_ART_DIR']=str(art)
        ppm=tmp/'capture.ppm';output=a.output/f'{name}.webp';output.parent.mkdir(parents=True,exist_ok=True)
        result=subprocess.run([str(ROOT/'ui/tests/host_build/host-render'),str(theme),str(light),mode,'0',str(ppm)],env=env,capture_output=True,text=True,timeout=40)
        if result.returncode:raise RuntimeError(f'{name}: {result.stderr[-2500:]}')
        Image.open(ppm).save(output,quality=94,method=6)
        manifest.append({'file':str(output.relative_to(a.output)),'theme':theme,'variant':light,'scene':mode,'options':extra})
        print(name,flush=True)
    for color,rgb in colors.items():
        for page,mode,extra in [('menu','preview-home',{'ZHFOCUS':'0'}),('music','preview-np',{}),('coverflow','preview-albums',{'ALBUM_VIEW':'2','ZUNE_LIBRARY_FIXTURE':'1','PREVIEW_SETTLE':'3200'}),('wall','preview-albums',{'ALBUM_VIEW':'1','ZUNE_LIBRARY_FIXTURE':'1','PREVIEW_SETTLE':'3200'}),('quick','preview-quick',{}),('stats','statsapp',{'STATS_WRAP':'0'}),('immersive','preview-immersive',{'SITE_IMM_CONTROLS':'1'})]:
            capture(f'zune/{color}-{page}',13,mode,{'SITE_ACCENT':rgb,**extra})
    for page,mode,extra in [('music','preview-np',{}),('menu','preview-home',{'ZHFOCUS':'0'}),('wall','preview-albums',{'ALBUM_VIEW':'1','ZUNE_LIBRARY_FIXTURE':'1','PREVIEW_SETTLE':'3200'})]:
        capture(f'zune/light-{page}',13,mode,{'SITE_ACCENT':'ec008c',**extra},light=1)
    for name,theme,mode,extra,light in [
        ('disco-music',10,'preview-home',{},0),('disco-immersive',10,'preview-immersive',{},0),('disco-stats',10,'statsapp',{'STATS_WRAP':'0'},0),
        ('null-music',11,'preview-np',{},0),('null-quick',11,'preview-quick',{},0),('null-menu',11,'preview-home',{},0),
        ('squash-music',12,'preview-np',{},0),('squash-light',12,'preview-np',{},1),('squash-quick',12,'preview-quick',{},0),('squash-menu',12,'preview-home',{},0),
        ('wheel-album',10,'preview-library',{'ZUNE_LIBRARY_FIXTURE':'1','DISCO_SCROLL':'3','SITE_ALBUM':'Endless Summer','PREVIEW_SETTLE':'3200'},0),
        ('wheel-tracks',10,'preview-library',{'ZUNE_LIBRARY_FIXTURE':'1','DISCO_SCROLL':'3','SITE_ALBUM':'Endless Summer','SITE_SCROLL':'252','PREVIEW_SETTLE':'3200'},0),
        ('wheel-artists',10,'preview-library',{'ZUNE_LIBRARY_FIXTURE':'1','DISCO_SCROLL':'3','SITE_ARTIST':'The Midnight','PREVIEW_SETTLE':'3200'},0),
        ('library-list',13,'preview-albums',{'ZUNE_LIBRARY_FIXTURE':'1','ALBUM_VIEW':'0','PREVIEW_SETTLE':'3200'},0),
        ('stats-overview',13,'statsapp',{},0),('stats-albums',13,'statsapp',{'STATS_WRAP':'1'},0),('stats-artists',13,'statsapp',{'STATS_WRAP':'0','STATS_PAGE':'1'},0),
        ('braun2-home',14,'preview-home',{},0),('braun2-music',14,'preview-np',{},1),('braun2-quick',14,'preview-quick',{},1),
        ('help-howto',10,'preview-howto',{},0)]:
        capture(f'screens/{name}',theme,mode,extra,light)
    revision=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip()
    (a.output/'captures.json').write_text(json.dumps({'release':'v'+re.search(r'#define DISCO_VERSION\s+"([^"]+)"',(ROOT/'ui/version.h').read_text())[1],'source_commit':revision,'renderer':'LVGL host-render; production screen code with sample catalog and listening history','artwork':'Original geometric demo sleeves, not official album artwork','captures':manifest},indent=2)+'\n')
