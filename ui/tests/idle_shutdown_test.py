#!/usr/bin/env python3
"""Run production idle + shutdown wiring with simulated time, IPC, audio and card IO.
Never invokes a real sync, kill, shutdown or sysconfig write.
"""
from pathlib import Path
import re, subprocess, tempfile
app = Path(__file__).resolve().parents[1]
src = (app/'power.c').read_text()
# Keep production functions and state; replace only hardware/process interfaces.
src = re.sub(r'^#include .*$', '', src, flags=re.M)
src = src.replace('return power_read_sysconfig(POWER_SYSCONFIG, "POWER_SAVE");', 'return stock;')
prefix = r'''
#include "power.h"
#include "sqlite3.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
static unsigned now=100, gen=1;
static int stock, idle_idx=1, audio, spawn_fail, poll_fail, poll_pending, drain=1;
static int shown, reopened, sent, kills, warned, ready, hold, handoff_fail;
static unsigned lv_tick_get(void){return now;}
static uint32_t ui_position_still_ms(void){return audio?0:now;}   /* audio playing: the position moves; otherwise still since start (main.c) */
static int fw_os_ver(void){return 257;}
static unsigned ipc_generation(void){return gen;}
static int cfg_get_int(const char*k,int d){return !strcmp(k,"idle_off_idx")?idle_idx:d;}
static void cfg_set_int(const char*k,int v){(void)k;(void)v;}
static int ipc_send_quiet(const char*f){(void)f;return 0;}
static int ipc_send_cmd(const char*f){if(!strcmp(f,"0806000C0001")){if(handoff_fail)return -1;sent++;}return 0;}
static void ui_shutdown_screen(void){shown++;}
static void ui_shutdown_hide(void){}
static void ui_toast(const char*m){if(strstr(m,"unavailable"))warned++;if(strstr(m,"is ready"))ready++;}
static void ui_sd_quiesce_begin(void){hold=1;}
static int ui_sd_quiesce_drained(void){return drain;}
static void ui_sd_quiesce_end(void){hold=0;reopened++;}
static void scanner_abort(void){}
static void art_kill_all(void){}
static void mdb_history_hold(int h){(void)h;}           /* the play-history writer holds with the SD quiesce */
static int mdb_history_drained(void){return 1;}
static void stats_flush(void){}
static void usage_save(void){}
static int command_spawn(pid_t*p,char *const a[]){(void)a;*p=123;return spawn_fail?-1:0;}
static pid_t fake_waitpid(pid_t p,int*s,int flags){(void)flags;if(s)*s=poll_fail?256:0;return poll_pending?0:p;}
static int fake_kill(pid_t p,int sig){(void)p;(void)sig;kills++;return 0;}
#define waitpid fake_waitpid
#define kill fake_kill
'''
post = r'''
static void tick(int busy){power_tick(0,busy);}
static void start_idle(void){
 tick(0);now+=5000;tick(0);assert(g_take_ok);
 now+=1;tick(0);now+=300000;tick(0);
 assert(power_shutdown_phase()==PW_DRAIN && shown==1 && hold);
}
int main(int argc,char**argv){
 assert(argc==2);const char*c=argv[1];
 if(!strcmp(c,"blocked")){
  stock=-1;tick(0);now+=30000;tick(0);assert(warned==1&&!g_take_ok&&!shown);
  now+=30000;tick(0);assert(warned==1&&!shown);
  stock=0;now+=5000;tick(0);assert(g_take_ok&&ready==1);
 }else if(!strcmp(c,"manual")||!strcmp(c,"sleep")){
  audio=1;g_idle_busy=1;if(!strcmp(c,"sleep"))assert(power_sleep_decide(1000,1000,1,1,POWER_SLEEP_SHUTDOWN)==PW_SLEEP_SHUTDOWN);
  assert(power_shutdown_request()==0);
  tick(1);tick(1);assert(sent==1&&hold&&power_shutdown_phase()==PW_HANDOFF);
 }else if(!strcmp(c,"restart")){
  tick(0);now+=5000;tick(0);assert(g_take_ok);
  now+=1;tick(0);now+=299999;tick(0);gen++;
  now+=2;tick(0);assert(!shown&&!g_take_ok);
 }else if(!strcmp(c,"invalid")){
  idle_idx=-8;tick(0);now+=5000;tick(0);now+=4000000;tick(0);assert(!shown);
 }else{
  start_idle();
  if(!strcmp(c,"drain-input")){power_note_input();tick(0);assert(!hold&&!sent);}
  else if(!strcmp(c,"touch")){now++;power_tick(now,0);assert(!hold&&!sent);}
  else if(!strcmp(c,"drain-busy")){tick(1);assert(!hold&&!sent);}
  else if(!strcmp(c,"disable")){idle_idx=0;tick(0);assert(!hold&&!sent);}
  else if(!strcmp(c,"reconnect")){gen++;tick(0);assert(!hold&&!sent&&!g_take_ok);}
  else if(!strcmp(c,"retry-spawn")||!strcmp(c,"retry-flush")||!strcmp(c,"retry-drain")||!strcmp(c,"retry-timeout")||!strcmp(c,"retry-send")){
   spawn_fail=!strcmp(c,"retry-spawn");poll_fail=!strcmp(c,"retry-flush");drain=strcmp(c,"retry-drain")!=0;poll_pending=!strcmp(c,"retry-timeout");
   handoff_fail=!strcmp(c,"retry-send");
   tick(0);
   if(handoff_fail)tick(0);
   else if(!drain){now+=POWER_DRAIN_MS;tick(0);}
   else if(poll_fail)tick(0);
   else if(poll_pending){now+=POWER_SYNC_MS;tick(0);}
   assert(power_shutdown_phase()==PW_OFF&&!hold&&!sent);
   spawn_fail=poll_fail=poll_pending=handoff_fail=0;drain=1;
   now+=299999;tick(0);assert(shown==1);
   now+=1;tick(0);assert(shown==2&&power_shutdown_phase()==PW_DRAIN);
   tick(0);tick(0);assert(sent==1&&hold);
  }else{
   tick(0);assert(power_shutdown_phase()==PW_SYNC);
   if(!strcmp(c,"sync-busy")){tick(1);assert(!hold&&!sent);}
   else if(!strcmp(c,"sync-input")){now++;power_note_input();tick(0);assert(!hold&&!sent);}
   else if(!strcmp(c,"late-audio")){audio=1;tick(0);assert(!hold&&!sent);}
   else if(!strcmp(c,"protected")){
    tick(0);assert(sent==1&&hold);now+=POWER_HANDOFF_MS;tick(1);
    assert(power_shutdown_phase()==PW_PROTECTED&&hold&&reopened==0);
    gen++;tick(1);assert(power_shutdown_phase()==PW_OFF&&!hold);
   }else assert(0);
  }
 }
 printf("PASS %s\n",c);return 0;
}
'''
cases=['retry-spawn','retry-flush','retry-drain','retry-timeout','retry-send','drain-input','touch','drain-busy','sync-input','sync-busy','late-audio','disable','reconnect','restart','blocked','invalid','manual','sleep','protected']
with tempfile.TemporaryDirectory(prefix='idle-shutdown-') as tmp:
 p=Path(tmp);(p/'test.c').write_text(prefix+src+post)
 subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Wno-unused-parameter','-ffunction-sections','-fdata-sections','-I'+str(app),str(p/'test.c'),'-Wl,--gc-sections','-o',str(p/'test')],check=True)
 for case in cases:subprocess.run([str(p/'test'),case],check=True)
