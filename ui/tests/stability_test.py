#!/usr/bin/env python3
"""Focused host regression checks for the recovery candidate; no device access."""
from pathlib import Path
import subprocess,tempfile
app=Path(__file__).resolve().parents[1]
def section(s,a,b):return s[s.index(a):s.index(b,s.index(a))]
db=(app/'musicdb.c').read_text();eq=(app/'eqcustom.c').read_text();fw=(app/'fwcaps.c').read_text()
gate=section(fw,'int fw_custom_peq_writable(',"/* V2.57's player arbitrates")
utils=section(db,'static int peq_tok_int(','/* Is a PEQ slot representable')
reader=section(db,'static int peq_tok_tenths(','int mdb_get_peq_ex(')
writer=section(db,'static int mdb_set_peq_transaction(','/* Read a PEQ slot')
state=section(eq,'static int g_preset,','static int g_sel')
helpers=section(eq,'static int clampi(','/* ------------------------------------------------------------------ player I/O */')
io=section(eq,'static int apply_now(','/* ------------------------------------------------------------------ drawing */')
poll=section(eq,'static void draw_tmr_cb(','/* ------------------------------------------------------------------ edits */')
prefix=r'''
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <unistd.h>
#include <time.h>
#include "jsmn.h"
#define NB 10
#define NPRESET 21
#define USER_MIN 11
static const int STDF[10]={32,64,125,250,500,1000,2000,4000,8000,16000};
static char dbpath[1024];
#define DB_PATH dbpath
static sqlite3 *database;
static sqlite3 *db(void){return database;}
static int cached_rate=44100,live_rate=44100;
int eq_rate_cached(void){return cached_rate;}
int eq_player_rate(void){return live_rate;}
void eq_rate_request(void){}
#define SCR_EQ_EDITOR 99
static int screen_current(void){return 0;}
static int version=240,selected=11,send_ok=1,send_count,mirror_count;
int fw_os_ver(void){return version;}
static int sd_io_begin(void){return 1;}
static void sd_io_end(void){}
static int ui_eq_select(int p){assert(p==11);send_count++;return send_ok?0:-1;}
static int cfg_get_int(const char *k,int def){(void)k;(void)def;return selected;}
static void cfg_set_int_deferred(const char *k,int v){(void)k;(void)v;mirror_count++;}
static void cfg_flush(void){}
static int g_sel=-1,g_dirty_draw,g_drag_ok;
static char toast[100];
static void ui_toast(const char *s){snprintf(toast,sizeof toast,"%s",s);}
static void redraw(void){}
typedef int lv_timer_t;
'''
wrapper='static int mdb_get_peq_ex(int p,double *m,int *t,int *f,int *e,double *q){return mdb_get_peq_ex_leased(p,m,t,f,e,q);}\n'
tail=r'''
static void profile(int unordered){
 char js[2000];int n=snprintf(js,sizeof js,"[");
 for(int i=0;i<10;i++)n+=snprintf(js+n,sizeof js-n,"%s{\"filterType\":0,\"frequency\":%d,\"position\":%d,\"gain\":\"0.0\",\"qValue\":\"0.71\"}",i?",":"",unordered&&i==0?1000:STDF[i],i);
 snprintf(js+n,sizeof js-n,"]");sqlite3_stmt *s;assert(sqlite3_prepare_v2(database,"UPDATE PEQ SET PARAMS_JSON=? WHERE STYLE_PRESET=11",-1,&s,0)==0);sqlite3_bind_text(s,1,js,-1,SQLITE_TRANSIENT);assert(sqlite3_step(s)==SQLITE_DONE);sqlite3_finalize(s);
}
static int scalar(const char *sql){sqlite3_stmt *s;assert(sqlite3_prepare_v2(database,sql,-1,&s,0)==0);assert(sqlite3_step(s)==SQLITE_ROW);int v=sqlite3_column_int(s,0);sqlite3_finalize(s);return v;}
static void finish(void){for(int i=0;i<300 && !atomic_load(&g_save_done);i++)usleep(10000);assert(atomic_load(&g_save_done));draw_tmr_cb(NULL);assert(!g_save_busy);}
int main(int argc,char **argv){
 assert(argc==2);snprintf(dbpath,sizeof dbpath,"%s",argv[1]);assert(sqlite3_open(dbpath,&database)==0);
 assert(sqlite3_exec(database,"CREATE TABLE PEQ(STYLE_NAME TEXT,MASTER_GAIN REAL,PARAMS_JSON TEXT,STYLE_PRESET INTEGER PRIMARY KEY);INSERT INTO PEQ VALUES('Original',0,'[]',11)",0,0,0)==0);
 profile(0);version=257;load(11);assert(g_editable);assert(fw_custom_peq_curve_writable(STDF,10));
 cached_rate=0;load(11);assert(!g_editable);assert(!apply_now());cached_rate=44100;
 live_rate=8000;assert(!fw_custom_peq_curve_writecheck(STDF,10));
 assert(!mdb_set_peq(11,0,"[]",STDF));assert(scalar("SELECT PARAMS_JSON!='[]' FROM PEQ"));
 live_rate=44100;load(11);g_t[0]=10;assert(apply_now());finish();assert(send_count==1);send_count=0;
 puts("PASS: V2.57 editing and saving enabled; unverified/low-rate writes rejected by worker");
 version=999;assert(!fw_custom_peq_writable());version=240;
 profile(1);load(11);assert(!g_editable && g_parametric && g_f[1]==64);assert(!apply_now());
 puts("PASS: unordered curve retains original frequencies and cannot be rewritten");
 profile(0);load(11);assert(g_editable);g_t[0]=10;send_ok=0;
 assert(apply_now());assert(g_save_busy && !g_editable);assert(!apply_now());finish();
 assert(!strcmp(toast,"EQ saved; not applied"));assert(send_count==1);
 assert(scalar("SELECT instr(PARAMS_JSON,'\"gain\":\"1.0\"')>0 AND instr(PARAMS_JSON,'\"qValue\":\"0.71\"')>0 FROM PEQ"));
 assert(scalar("SELECT instr(PARAMS_JSON,'\"gain\":\"1.0\"')=0 FROM DISKOS_PEQ_BACKUP"));
 puts("PASS: asynchronous save preserves Q/backup and reports saved-but-not-applied accurately");
 send_count=0;send_ok=1;g_t[0]=20;assert(apply_now());selected=12;load(12);finish();assert(send_count==0);assert(!strcmp(toast,"EQ saved"));
 puts("PASS: completing a save never reselects a preset changed during the operation");
 selected=11;load(11);sqlite3 *lock;assert(sqlite3_open(dbpath,&lock)==0);assert(sqlite3_exec(lock,"BEGIN IMMEDIATE",0,0,0)==0);
 struct timespec a,b;clock_gettime(CLOCK_MONOTONIC,&a);assert(apply_now());clock_gettime(CLOCK_MONOTONIC,&b);
 double dt=b.tv_sec-a.tv_sec+(b.tv_nsec-a.tv_nsec)/1e9;assert(dt<0.25);finish();assert(!strcmp(toast,"EQ not saved"));assert(send_count==0);
 sqlite3_exec(lock,"ROLLBACK",0,0,0);sqlite3_close(lock);
 printf("PASS: contended database save returns to UI in %.4f s; worker reports failure\n",dt);
 sqlite3_close(database);
}
'''
command_test=r'''
#include "command_spawn.h"
#include <assert.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/file.h>
#include <fcntl.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include "ui_instance.h"
int main(int argc,char **argv){
 if(argc>1 && argv[1][0]=='c')return ui_instance_acquire()?1:0;
 assert(ui_instance_acquire());
 pid_t p=fork();assert(p>=0);if(!p){execl(argv[0],argv[0],"child",NULL);_exit(99);}int status;assert(waitpid(p,&status,0)==p);assert(WIFEXITED(status)&&WEXITSTATUS(status)==0);
 puts("PASS: independent second UI cannot acquire lifetime lock");
 char *ok[]={"/bin/sh","-c","exit 0",NULL};assert(command_wait(ok,1000)==0);
 char *bad[]={"/bin/sh","-c","exit 7",NULL};assert(command_wait(bad,1000)==-1);
 char *missing[]={"/no/such/command",NULL};assert(command_wait(missing,1000)==-1);
 char *longrun[]={"/bin/sh","-c","sleep 10",NULL};struct timespec a,b;clock_gettime(CLOCK_MONOTONIC,&a);assert(command_wait(longrun,100)==-1);clock_gettime(CLOCK_MONOTONIC,&b);assert(b.tv_sec-a.tv_sec<3);
 int fd=open("/dev/null",O_RDONLY);assert(fd>=3);int high=fcntl(fd,F_DUPFD,300);assert(high>=300);
 char text[160];snprintf(text,sizeof text,"test ! -e /proc/self/fd/%d && test ! -e /proc/self/fd/%d",fd,high);
 char *closed[]={"/bin/sh","-c",text,NULL};assert(command_wait(closed,1000)==0);
 close(fd);close(high);puts("PASS: spawn success/failure/timeout and low/high descriptor cleanup");
}
'''
with tempfile.TemporaryDirectory(prefix='diskos-stability-') as td:
 t=Path(td);(t/'eq.c').write_text(prefix+gate+utils+reader+wrapper+writer+state+helpers+io+poll+tail)
 subprocess.run(['gcc','-std=gnu11','-Wall','-Wextra','-Werror','-I'+str(app),str(t/'eq.c'),'-l:libsqlite3.so.0','-lm','-pthread','-o',str(t/'eq')],check=True)
 subprocess.run([str(t/'eq'),str(t/'db')],check=True)
 (t/'commands.c').write_text(command_test)
 subprocess.run(['gcc','-D_GNU_SOURCE','-std=gnu11','-Wall','-Wextra','-Werror','-I'+str(app),'-DUI_INSTANCE_LOCK="'+str(t/'instance.lock')+'"',str(t/'commands.c'),str(app/'command_spawn.c'),str(app/'ui_instance.c'),'-o',str(t/'commands')],check=True)
 subprocess.run([str(t/'commands')],check=True)
