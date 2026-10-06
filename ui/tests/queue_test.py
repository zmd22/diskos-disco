#!/usr/bin/env python3
"""Real queue backend and SQLite plans; UI, IPC and SD admission are boundaries."""
import os
from pathlib import Path
import subprocess
import tempfile
app=Path(__file__).resolve().parents[1]
queue=(app/'queue.c').read_text().split('/* ================================================================ the Queue screen */')[0]
stubs=r'''
#include <assert.h>
#include "modes.h"
#include "sdio.h"
static int local=1,slot_fail,rest_fail,sends,restores,last_pos,leases;
static track_state_t state;
int sd_io_begin(void){ if(!local) return 0; leases++;return 1; }
void sd_io_end(void){ assert(leases>0); leases--; }
int modes_output_busy(void){return 0;}
int ui_local_playback_allowed(void){return local;}
void ipc_get_state(track_state_t *out){*out=state;}
int cfg_get_int(const char *key,int fallback){(void)key;return fallback;}
uint32_t lv_tick_get(void){return 10000;}
uint32_t lv_tick_elaps(uint32_t t){return 10000-t;}
void ui_queue_changed(void){}
void ui_toast(const char *s){(void)s;}
int screen_current(void){return SCR_NOWPLAYING;}
int ui_play_context(int *type,char *name,int cap,long *pid){ *type=3;snprintf(name,(size_t)cap,"Album");*pid=0;return 3;}
int ui_play_slot(int pos){sends++;last_pos=pos;return !slot_fail;}
int ui_play_restore(int type,const char *name,long pid,int pos){(void)name;(void)pid;assert(type==3);restores++;last_pos=pos;return !rest_fail;}
static void view_reload(void){}
'''
test=r'''
static void sql(sqlite3 *db,const char *query){char *err=NULL;int rc=sqlite3_exec(db,query,0,0,&err);if(rc) fprintf(stderr,"%s: %s\n",query,err);sqlite3_free(err);assert(rc==SQLITE_OK);}
int main(int argc,char **argv){
    assert(argc==2);sqlite3 *db=NULL;assert(sqlite3_open(argv[1],&db)==SQLITE_OK);
    sql(db,"CREATE TABLE SONG(ID INTEGER PRIMARY KEY,PATH,NAME,TITLE,ALBUM,ARTIST,GENRE,DISC,TRACK,IS_CUE,IS_ISO,IS_DSD,OFFSET,DURATION,ALBUM_ARTIST,IS_M3U,M3U_PATH);");
    sql(db,"CREATE TABLE CUSTOM_PLAYLIST_INDEX(LIST_ID INTEGER PRIMARY KEY,LIST_NAME,M3U_PATH);");
    sql(db,"CREATE TABLE CUSTOM_PLAYLIST(ID INTEGER PRIMARY KEY,PLAYLIST_ID,PATH,NAME,TITLE,ALBUM,ARTIST,GENRE,DISC,TRACK,IS_CUE,IS_ISO,IS_DSD,OFFSET,DURATION,ALBUM_ARTIST,IS_M3U,M3U_PATH,UNIQUE(PLAYLIST_ID,PATH,TRACK,IS_CUE,IS_ISO));");
    sql(db,"CREATE TABLE LIST_SONG_0(ID INTEGER PRIMARY KEY,PATH,TRACK,IS_CUE,IS_ISO,TITLE);");
    sql(db,"INSERT INTO SONG VALUES(10,'/album.flac','album.flac','First','Album','Artist','',2,1,1,0,0,0,10000,'Artist',1,'/list.m3u'),(11,'/album.flac','album.flac','Second','Album','Artist','',2,2,1,0,0,10000,12000,'Artist',1,'/list.m3u'),(20,'/next.flac','next.flac','Next','Album','Artist','',2,3,0,0,0,0,14000,'Artist',0,'');");
    sql(db,"INSERT INTO LIST_SONG_0 VALUES(1,'/album.flac',1,1,0,'First'),(2,'/album.flac',2,1,0,'Second'),(3,'/next.flac',3,0,0,'Next');");
    assert(mdb_queue_current_id("/album.flac",0,"")==0);
    assert(mdb_queue_current_id("/album.flac",2,"Second")==11);
    assert(mdb_queue_current_id("/album.flac",2,"First")==0);
    assert(mdb_queue_current_id("/next.flac",0,"")==20);
    int ids[8];assert(mdb_queue_live_ids(ids,8)==3 && ids[0]==10 && ids[1]==11 && ids[2]==20);
    assert(mdb_queue_live_ids(ids,2)==-1);
    puts("PASS ambiguous subtracks require verified row/title; live queues retain exact track identity");
    state.have_track=1;state.pos_id=1;strcpy(state.path,"/album.flac");strcpy(state.title,"First");
    assert(queue_add_path("/album.flac")==2 && g_q[0].id==10 && g_q[1].id==11);
    local=0;assert(queue_next()==-1 && sends==0 && queue_count()==2);local=1;
    slot_fail=1;assert(queue_next()==-1 && queue_count()==2 && !g_active);slot_fail=0;
    assert(queue_next()==1 && queue_count()==1 && g_q[0].id==11 && g_active && last_pos==1);
    sqlite3_stmt *st;assert(sqlite3_prepare_v2(db,"SELECT COUNT(*) FROM CUSTOM_PLAYLIST WHERE IS_M3U=1 AND M3U_PATH='/list.m3u';",-1,&st,0)==SQLITE_OK);assert(sqlite3_step(st)==SQLITE_ROW && sqlite3_column_int(st,0)==2);sqlite3_finalize(st);
    state.pos_id=2;strcpy(state.title,"Second");queue_tick(&state,1);assert(queue_count()==0);
    state.pos_id=3;strcpy(state.path,"/next.flac");strcpy(state.title,"Next");queue_tick(&state,1);
    assert(!g_active && restores==1 && last_pos==3 && !leases);
    puts("PASS same-file CUE transitions, V2.57 M3U fields, IPC failure retention, SD hold and exact context restoration");
    assert(queue_add_path("/next.flac")==1);g_last_id=0;g_prev2=0;
    assert(queue_next()==1);rest_fail=1;hand_back(20);assert(g_active);rest_fail=0;hand_back(20);assert(!g_active);
    assert(queue_add_path("/next.flac")==1);int old=sends;sql(db,"UPDATE SONG SET PATH='/replacement.flac' WHERE ID=20;");
    assert(!play_copy(0) && sends==old && queue_count()==1);
    assert(!leases);sqlite3_close(db);
    puts("PASS failed restoration and stale IDs do not consume queue state");
    puts("HARNESS COMPLETE");
}
'''
with tempfile.TemporaryDirectory(prefix='diskos-queue-test-') as tmp:
    root=Path(tmp);db=root/'song.db';qfile=root/'queue.tsv'
    (root/'db.c').write_text((app/'musicdb.c').read_text().replace('"/usr/data/fiio/db/song.db"','"'+str(db)+'"'))
    (root/'test.c').write_text(queue+stubs+'\n#include "sqlite3.h"\n'+test)
    exe=root/'test'
    subprocess.run([os.environ.get('CC','cc'),'-O1','-g','-std=gnu11','-D_GNU_SOURCE','-DLV_CONF_INCLUDE_SIMPLE','-DQUEUE_FILE="'+str(qfile)+'"','-ffunction-sections','-fdata-sections','-fsanitize=address,undefined','-I'+str(app),str(root/'test.c'),str(root/'db.c'),str(app/'sqlite3.c'),'-pthread','-lm','-ldl','-Wl,--gc-sections','-o',str(exe)],check=True)
    r=subprocess.run([str(exe),str(db)],text=True,capture_output=True,timeout=20)
    print(r.stdout,end='');print(r.stderr,end='')
    assert r.returncode==0 and r.stdout.splitlines()[-1]=='HARNESS COMPLETE'
print('COMPLETE queue_test.py')
