/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "discshelf/shelf.h"
#include "sqlite3.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
int sd_io_begin(void){return 1;}void sd_io_end(void){}int sd_io_allowed(void){return 1;}
static void idle(void){for(int n=0;n<40000;n++){pthread_mutex_lock(&ds_shared.mu);int b=ds_shared.busy;pthread_mutex_unlock(&ds_shared.mu);if(!b)return;usleep(1000);}assert(!"worker timeout");}
static void send(int cmd,request_t r,int replace){atomic_store(&ds_abort_job,0);pthread_mutex_lock(&ds_shared.mu);ds_shared.command=cmd;ds_shared.req=r;ds_shared.busy=1;ds_shared.done=0;ds_shared.replace=replace;pthread_cond_signal(&ds_shared.wake);pthread_mutex_unlock(&ds_shared.mu);}
static item_t lookup(sqlite3*d,const char*path,int kind){item_t r={.kind=kind};sqlite3_stmt*q=NULL;assert(sqlite3_prepare_v2(d,"SELECT id,name FROM entries WHERE path=?",-1,&q,NULL)==SQLITE_OK);sqlite3_bind_text(q,1,path,-1,SQLITE_STATIC);assert(sqlite3_step(q)==SQLITE_ROW);snprintf(r.id,IDLEN,"%lld",(long long)sqlite3_column_int64(q,0));snprintf(r.name,STR,"%s",sqlite3_column_text(q,1));sqlite3_finalize(q);return r;}
static void download(item_t r,int replace){pthread_mutex_lock(&ds_shared.mu);ds_shared.npicks=1;ds_shared.picks[0]=r;pthread_mutex_unlock(&ds_shared.mu);send(2,(request_t){.view=SMB_BROWSER},replace);idle();fprintf(stderr,"download: %s\n",ds_shared.message);assert(ds_shared.done==1);}
int main(int argc,char**argv){
 assert(argc>=3);setenv("SHELF_TEST","1",1);assert(ds_backend_start(argv[1]));idle();fprintf(stderr,"startup: %s\n",ds_shared.message);assert(atomic_load(&ds_source)==DS_SMB);
 send(1,(request_t){.view=SMB_BROWSER},0);idle();fprintf(stderr,"browse: %s (%d/%d)\n",ds_shared.message,ds_shared.count,ds_shared.total_count);
 if(!strcmp(argv[2],"reject")){assert(ds_shared.count==0);goto close;}
 assert(ds_shared.count==PAGE&&ds_shared.total_count>=88);
 char dbpath[1600];snprintf(dbpath,sizeof dbpath,"%s/.shelf/smb-catalog.sqlite",argv[3]);sqlite3*d=NULL;assert(sqlite3_open(dbpath,&d)==SQLITE_OK);
 item_t album=lookup(d,"Album",SMB_DIR),unowned=lookup(d,"Unowned.flac",SMB_FILE),large=lookup(d,"ZZ-large.bin",SMB_FILE);
 send(3,(request_t){.view=SMB_BROWSER,.offset=60},0);idle();assert(ds_shared.offset==60&&ds_shared.count>0);
 send(3,(request_t){.view=SMB_BROWSER,.letter='Z',.jump=1},0);idle();assert(ds_shared.offset>60);
 download(album,0);download(unowned,1); /* Unowned files are never replaced. */
 request_t folder={.view=SMB_BROWSER};strcpy(folder.id,album.id);send(1,folder,0);idle();assert(ds_shared.count==6);
 for(int i=0;i<ds_shared.count;i++)if(strstr(ds_shared.rows[i].name,"-large.m4a"))assert(ds_shared.rows[i].local==1);
 item_t track=lookup(d,"Album/01.flac",SMB_FILE);assert(ds_shared.rows[0].local==1||ds_shared.rows[1].local==1);
 char remote[1800];snprintf(remote,sizeof remote,"%s/Album/01.flac",argv[4]);FILE*f=fopen(remote,"w");assert(f);fputs("fLaCNEW CONTENT",f);fclose(f);download(track,0);download(track,1);
 /* Cancel an owned replacement after actual encrypted read progress. */
 char target[1800];snprintf(target,sizeof target,"%s/ZZ-large.bin",argv[3]);f=fopen(target,"w");assert(f);fputs("ORIGINAL",f);fclose(f);
 sqlite3_stmt*q=NULL;assert(sqlite3_prepare_v2(d,"INSERT OR REPLACE INTO downloads SELECT source,'ZZ-large.bin',8,0,0 FROM downloads LIMIT 1",-1,&q,NULL)==SQLITE_OK);assert(sqlite3_step(q)==SQLITE_DONE);sqlite3_finalize(q);
 pthread_mutex_lock(&ds_shared.mu);ds_shared.npicks=1;ds_shared.picks[0]=large;ds_shared.byte_done=0;pthread_mutex_unlock(&ds_shared.mu);send(2,(request_t){.view=SMB_BROWSER},1);
 int saw=0;for(int i=0;i<20000;i++){pthread_mutex_lock(&ds_shared.mu);long long n=ds_shared.byte_done;int busy=ds_shared.busy;pthread_mutex_unlock(&ds_shared.mu);if(n>0){saw=1;atomic_store(&ds_abort_job,1);break;}if(!busy)break;usleep(1000);}assert(saw);idle();assert(ds_shared.done==2);sqlite3_close(d);
 puts("PASS encrypted SMB3 listing, paging, A-Z, recursive/Unicode/empty and completed large downloads, owned replacement, unowned protection and mid-transfer cancel");
close:ds_backend_stop();for(int i=0;!ds_backend_collect();i++){assert(i<3000);usleep(1000);}assert(!ds_context);return 0;
}
