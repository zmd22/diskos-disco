#!/usr/bin/env python3
"""Exercise production async rate cache with a controlled /proc fixture.
The test hash stub models a verified image; it does not validate its fingerprint.
"""
from pathlib import Path
import subprocess,tempfile
app=Path(__file__).resolve().parents[1]
h=r'''
#include "eqrate.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
static int hashes;
void md5_hex(const void *p,size_t n,char out[33]){(void)p;assert(n==4601368);hashes++;strcpy(out,"ff7beab94287f586d6faa2b51924c901");}
static void setrate(int rate){int fd=open(EQ_PROC_ROOT "/123/mem",O_WRONLY);assert(fd>=0);unsigned char raw[4]={rate,rate>>8,rate>>16,rate>>24};assert(pwrite(fd,raw,4,0x839ae4)==4);close(fd);}
static void *probe(void *p){(void)p;assert(eq_player_rate()==44100);return NULL;}
int main(void){
 assert(eq_rate_cached()==0);eq_rate_request();
 for(int i=0;i<200 && !eq_rate_cached();i++)usleep(10000);
 assert(eq_rate_cached()==44100);assert(hashes==1);
 pthread_t a,b;assert(!pthread_create(&a,0,probe,0));assert(!pthread_create(&b,0,probe,0));pthread_join(a,0);pthread_join(b,0);assert(hashes==1);
 setrate(8000);assert(eq_player_rate()==8000);assert(eq_rate_cached()==8000);
 setrate(0);assert(eq_player_rate()==0);assert(eq_rate_cached()==0);
 setrate(44100);assert(eq_player_rate()==44100);usleep(2600000);assert(eq_rate_cached()==0);
 puts("PASS: async rate verification, serialized identity cache, changed/invalid rate and stale result expiry");
}
'''
with tempfile.TemporaryDirectory(prefix='diskos-eqrate-') as td:
 t=Path(td);proc=t/'proc';p=proc/'123';p.mkdir(parents=True)
 (p/'comm').write_text('mq_player\n');(p/'stat').write_text('123 (mq_player) S '+'0 '*18+'100 0\n')
 with (p/'exe').open('wb') as f:f.truncate(4601368)
 with (p/'mem').open('wb') as f:f.seek(0x839ae4);f.write((44100).to_bytes(4,'little'))
 (t/'test.c').write_text(h)
 subprocess.run(['gcc','-std=gnu11','-Wall','-Wextra','-Werror','-I'+str(app),'-DEQ_PROC_ROOT="'+str(proc)+'"',str(t/'test.c'),str(app/'eqrate.c'),'-pthread','-o',str(t/'test')],check=True)
 subprocess.run([str(t/'test')],check=True)
