#!/usr/bin/env python3
"""Exercise the production history hook without LVGL or a player process."""
from pathlib import Path
import subprocess
import tempfile

ui = Path(__file__).resolve().parents[1]
src = (ui / 'ui.c').read_text()
hook = src[src.index('void ui_history_tick('):src.index('void ui_update(')]
main = (ui / 'main.c').read_text()
assert main.index('ui_history_tick(&st);') < main.index('if(bl_state != 2 && (st.seq')
program = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef struct {int have_track, state; char path[256];} track_state_t;
static char g_stat_path[256];
static int calls;
static uint32_t now = 5000;                                  /* a fake LVGL clock the test moves */
static uint32_t lv_tick_get(void){return now;}
static uint32_t lv_tick_elaps(uint32_t t){return now-t;}
static void copy_cstr(char *d,size_t n,const char *s){snprintf(d,n,"%s",s);}
static int mdb_is_book_path(const char *s){return strstr(s,".m4b")!=NULL;}
static void mdb_record_play(const char *s){assert(s[0]);calls++;}
''' + hook + r'''
int main(void){
 track_state_t s={.have_track=1,.state=1,.path="A.flac"};
 ui_history_tick(&s); assert(calls==0);                                  /* paused: never counted */
 now+=1000; s.state=2;ui_history_tick(&s);ui_history_tick(&s);assert(calls==1);   /* once, not twice */
 s.state=1;strcpy(s.path,"B.flac");ui_history_tick(&s);assert(calls==1);
 s.state=2;ui_history_tick(&s);assert(calls==1);                         /* playing, but not a second yet */
 now+=1000;ui_history_tick(&s);assert(calls==2);
 strcpy(s.path,"C.flac");ui_history_tick(&s);now+=300;                  /* a burst of skips: C and D fly past */
 strcpy(s.path,"D.flac");ui_history_tick(&s);now+=300;
 strcpy(s.path,"E.flac");ui_history_tick(&s);assert(calls==2);
 now+=1000;ui_history_tick(&s);assert(calls==3);                         /* E stayed: counted */
 strcpy(s.path,"book.m4b");ui_history_tick(&s);now+=1000;ui_history_tick(&s);assert(calls==3);   /* books are not history */
 strcpy(s.path,"B.flac");ui_history_tick(&s);now+=1000;ui_history_tick(&s);assert(calls==4);     /* music after a book counts */
 s.have_track=0;ui_history_tick(&s);s.have_track=1;ui_history_tick(&s);assert(calls==5);         /* a reset counts it again */
 ui_history_tick(NULL);ui_history_tick(&s);assert(calls==6);
 s.path[0]=0;ui_history_tick(&s);now+=1000;ui_history_tick(&s);assert(calls==6);                  /* no path: nothing */
 return 0;
}
'''
with tempfile.TemporaryDirectory() as d:
    source=Path(d)/'history.c';binary=Path(d)/'history'
    source.write_text(program)
    subprocess.run(['gcc','-std=c11','-Wall','-Wextra','-Werror',str(source),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
print('PASS: production History hook, paused skips, one-second settle, skip bursts, deduplication, books, reset and screen-off placement')
