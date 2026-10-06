#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise the real merged lyrics worker and immersive parser without network or device I/O."""
from pathlib import Path
import os
import subprocess
import tempfile

app=Path(__file__).resolve().parents[1]
source=r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static int admitted=1,leases,net_calls;
static const char *reply="[{\"syncedLyrics\":\"[00:01]online one\\n[00:02]online two\"}]";
static FILE *test_popen(const char *cmd,const char *mode){
    (void)mode; assert(strstr(cmd,"https://lrclib.net/api/search?")); net_calls++;
    return fmemopen((void *)reply,strlen(reply),"r");
}
static int test_pclose(FILE *f){ return fclose(f); }
#define popen test_popen
#define pclose test_pclose
#include "lyrics.c"
int sd_io_begin(void){ if(!admitted) return 0; leases++; return 1; }
void sd_io_end(void){ assert(leases>0); leases--; }
static void fetch(const char *path,int online,unsigned request){
    ly_job_t *j=calloc(1,sizeof *j); assert(j);
    snprintf(j->path,sizeof j->path,"%s",path);
    snprintf(j->title,sizeof j->title,"title");
    snprintf(j->artist,sizeof j->artist,"artist"); j->online=online; j->req=request;
    g_linflight=1; g_lready=0;
    lyrics_thread(j);
    assert(!leases && !g_linflight && g_lready && g_done_req==request);
}
static void write_file(const char *path,const char *text){
    FILE *f=fopen(path,"wb"); assert(f); assert(fwrite(text,1,strlen(text),f)==strlen(text)); assert(!fclose(f));
}
int main(int argc,char **argv){
    assert(argc==2); char song[256],lrc[256];
    snprintf(song,sizeof song,"%s/song.mp3",argv[1]);
    snprintf(lrc,sizeof lrc,"%s/song.lrc",argv[1]);
    write_file(song,"unsupported audio fixture");
    write_file(lrc,"[offset:+250]\n[00:02.00][00:01.00]<00:00.50>héllo\n[00:03]third\n");
    fetch(song,1,7); assert(g_lkind==2 && !net_calls);
    lyr_line_t lines[8]; int n=timed_parse(g_lbuf,lines,8);
    assert(n==3 && lines[0].ms==750 && lines[1].ms==1750 && lines[2].ms==2750);
    assert(!strcmp(lines[0].text,"héllo") && !strcmp(lines[1].text,"héllo"));
    assert(lyrics_timed_load(song,lines,8)==3 && !leases);
    admitted=0; assert(!lyrics_timed_load(song,lines,8)); admitted=1;
    puts("PASS: sidecar precedence, SD admission, enhanced/repeated timestamps and offset");
    unlink(lrc); fetch(song,0,8); assert(g_lkind==0 && !net_calls);
    fetch(song,1,9); assert(g_lkind==2 && net_calls==1);
    assert(timed_parse(g_lbuf,lines,8)==2 && !strcmp(lines[0].text,"online one"));
    /* The worker publishes its captured request; main can reject it after a skip. */
    g_req=10; assert(g_done_req!=g_req);
    puts("PASS: online toggle/fallback and superseded-request identity");
    reply="[{\"plainLyrics\":\"plain words\"}]";
    fetch(song,1,10); assert(g_lkind==1 && !strcmp(g_lbuf,"plain words"));
    assert(!timed_parse(g_lbuf,lines,8));
    char raw[512]="[00:01]"; size_t k=strlen(raw);
    for(int i=0;i<90;i++){ memcpy(raw+k,"界",3); k+=3; }
    strcpy(raw+k,"\n[00:02]end");
    assert(timed_parse(raw,lines,8)==2);
    assert(strlen(lines[0].text)==117 && (unsigned char)lines[0].text[116]==0x8C);
    puts("PASS: plain fallback and whole-codepoint immersive truncation");
    return 0;
}
'''
# Include the complete production translation unit; unused UI sections are removed by the linker.
with tempfile.TemporaryDirectory(prefix='diskos-lyrics-test-') as tmp:
    path=Path(tmp); (path/'test.c').write_text(source)
    subprocess.run(['cc','-D_GNU_SOURCE','-std=gnu11','-O1','-g','-ffunction-sections','-fdata-sections',
                    '-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(app),
                    str(path/'test.c'),'-Wl,--gc-sections','-pthread','-o',str(path/'test')],check=True)
    env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0')
    subprocess.run([str(path/'test'),tmp],env=env,check=True)
print('COMPLETE lyrics_test.py')
