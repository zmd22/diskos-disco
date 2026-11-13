/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "shelf.h"
#include "smb_publish.h"
#include <time.h>
#include <stddef.h>
#include "sqlite3.h"
#include "md5.h"
#include "vendor/libsmb2/include/smb2/smb2.h"
#include "vendor/libsmb2/include/smb2/libsmb2.h"
#include "vendor/libsmb2/include/smb2/libsmb2-raw.h"
#include "vendor/libsmb2/include/smb2/smb2-errors.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
extern char ds_server[1024],ds_username[256],ds_password[256],ds_download_root[1024],ds_state_dir[1100];
#define PATHCAP 1024
#define MAX_ENTRIES 100000
#define CHUNK 32768
/* Only the worker touches this state. Responses and downloads have fixed bounds. */
typedef struct {
 struct smb2_context *client;sqlite3 *db;char source[33];
 int done,status;void *result;uint8_t dir_id[SMB2_FD_SIZE];int query_end,entries;
 const char *listing;int queue_children,epoch;
 uint8_t *buffer;
 const char *operation;
 char failure[256];
} smb_state_t;
static int cancelled(void){return atomic_load(&ds_stopping)||atomic_load(&ds_abort_job)||!shelf_sd_ready();}
static int sql_stop(void*p){(void)p;return cancelled();}
static long long now_ms(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1000LL+t.tv_nsec/1000000;}
static void rpc_cb(struct smb2_context*c,int status,void*data,void*arg){(void)c;smb_state_t*s=arg;s->status=status;s->result=data;s->done=1;}
static void drop(smb_state_t*s){if(s->client){struct smb2_context*c=s->client;s->client=NULL;smb2_destroy_context(c);}}
static void begin(smb_state_t*s){s->done=0;s->status=0;s->result=NULL;}
/* Keep the first failure: closing a handle must not hide the failed read/save. */
static int fail_text(smb_state_t*s,const char*message){
 if(!s->failure[0])snprintf(s->failure,sizeof s->failure,"%s",message);
 return 0;
}
static int fail_errno(smb_state_t*s,int error){
 if(error<=0||error>4095)error=EIO;
 if(!s->failure[0])snprintf(s->failure,sizeof s->failure,"%s failed: %s.",s->operation?s->operation:"SMB request",strerror(error));
 return 0;
}
static int wait_rpc(smb_state_t*s){
 long long end=now_ms()+30000;
 while(!s->done){
  if(cancelled()){fail_errno(s,ECANCELED);drop(s);return 0;}
  if(now_ms()>end){fail_errno(s,ETIMEDOUT);drop(s);return 0;}
  size_t n=0;int timeout=100;const t_socket*fds=smb2_get_fds(s->client,&n,&timeout);
  if(n>8){fail_text(s,"SMB connection exceeded its socket limit.");drop(s);return 0;}struct pollfd p[8];
  for(size_t i=0;i<n;i++)p[i]=(struct pollfd){.fd=fds[i],.events=smb2_which_events(s->client)};
  if(timeout<0||timeout>100)timeout=100;
  int r=poll(p,n,timeout);if(r<0&&errno==EINTR)continue;if(r<0){fail_errno(s,errno);drop(s);return 0;}
  if(!r){if(smb2_service_fd(s->client,-1,0)<0){fail_errno(s,EIO);drop(s);return 0;}}
  for(size_t i=0;i<n&&!s->done;i++)if(p[i].revents&&smb2_service_fd(s->client,p[i].fd,p[i].revents)<0){fail_errno(s,EIO);drop(s);return 0;}
 }
 return s->status>=0?1:fail_errno(s,s->status>=-4095?-s->status:EIO);
}
static int queued(smb_state_t*s,int rc){return rc>=0?wait_rpc(s):fail_text(s,"Could not start the SMB request. Retry the connection.");}
static int connect_share(smb_state_t*s){
 if(s->client)return 1;if(cancelled())return 0;
 s->operation="Connect to SMB";
 if(!ds_available_memory())return fail_text(s,"Not enough free memory to connect to SMB.");
 s->client=smb2_init_context();if(!s->client)return fail_errno(s,ENOMEM);
 smb2_set_version(s->client,SMB2_VERSION_ANY3);smb2_set_seal(s->client,1);smb2_set_sign(s->client,1);
 smb2_set_security_mode(s->client,SMB2_NEGOTIATE_SIGNING_ENABLED|SMB2_NEGOTIATE_SIGNING_REQUIRED);
 smb2_set_timeout(s->client,15);smb2_set_user(s->client,ds_username);smb2_set_password(s->client,ds_password);
 smb2_set_domain(s->client,ds_smb_domain);smb2_set_workstation(s->client,"Disco");
 begin(s);if(!queued(s,smb2_connect_share_async(s->client,ds_server,ds_smb_share,ds_username,rpc_cb,s))||smb2_get_dialect(s->client)<SMB2_VERSION_0300){fail_text(s,"Cannot connect. Check SMB3 encryption, share and login.");drop(s);ds_status_text("%s",s->failure);return 0;}
 return 1;
}
static int sql(smb_state_t*s,const char*q){return sqlite3_exec(s->db,q,NULL,NULL,NULL)==SQLITE_OK?1:fail_text(s,"Cannot update the SMB catalogue on SD. Check free space.");}
static int component(const char*n){if(!*n||!strcmp(n,".")||!strcmp(n,"..")||strchr(n,'/')||strchr(n,'\\'))return 0;for(const unsigned char*p=(const void*)n;*p;p++)if(*p<32||*p==127)return 0;return 1;}
static int safe_path(const char*p){
 if(!p||strlen(p)>=PATHCAP||*p=='/'||strchr(p,'\\')||!strcmp(p,".shelf")||!strncmp(p,".shelf/",7))return 0;if(!*p)return 1;
 const char*a=p;while(*a){const char*b=strchr(a,'/');size_t n=b?(size_t)(b-a):strlen(a);if(!n||(n==1&&a[0]=='.')||(n==2&&a[0]=='.'&&a[1]=='.'))return 0;for(size_t i=0;i<n;i++)if((unsigned char)a[i]<32||(unsigned char)a[i]==127)return 0;if(!b)return 1;a=b+1;if(!*a)return 0;}return 1;
}
static int resolve(smb_state_t*s,const char*id,char*path){
 if(!id[0]){path[0]=0;return 1;}char*end;long long n=strtoll(id,&end,10);if(*end||n<=0)return 0;
 sqlite3_stmt*q=NULL;int ok=sqlite3_prepare_v2(s->db,"SELECT path FROM entries WHERE id=?",-1,&q,NULL)==SQLITE_OK;
 if(ok){sqlite3_bind_int64(q,1,n);ok=sqlite3_step(q)==SQLITE_ROW;if(ok){const char*p=(const char*)sqlite3_column_text(q,0);ok=p&&strlen(p)<PATHCAP&&safe_path(p);if(ok)strcpy(path,p);}}sqlite3_finalize(q);return ok;
}
static int job_add(smb_state_t*s,const char*path,int dir){sqlite3_stmt*q=NULL;int ok=sqlite3_prepare_v2(s->db,"INSERT OR IGNORE INTO jobs(path,dir,done) VALUES(?,?,0)",-1,&q,NULL)==SQLITE_OK;if(ok){sqlite3_bind_text(q,1,path,-1,SQLITE_TRANSIENT);sqlite3_bind_int(q,2,dir);ok=sqlite3_step(q)==SQLITE_DONE;}sqlite3_finalize(q);return ok;}
static int save_entry(smb_state_t*s,const char*name,const struct smb2_fileidfulldirectoryinformation*f){
 if(!strcmp(name,".")||!strcmp(name,".."))return 1;
 if(!component(name))return fail_text(s,"Folder contains an unsupported file name.");
 if(f->file_attributes&SMB2_FILE_ATTRIBUTE_REPARSE_POINT)return 1;
 if(!s->listing[0]&&!strcmp(name,".shelf"))return 1;
 int dir=!!(f->file_attributes&SMB2_FILE_ATTRIBUTE_DIRECTORY);char path[PATHCAP];
 if(strlen(name)>=STR||snprintf(path,sizeof path,"%s%s%s",s->listing,s->listing[0]?"/":"",name)>=(int)sizeof path||!safe_path(path))return fail_text(s,"A name or path is too long. Folder was not loaded.");
 if(f->end_of_file>INT64_MAX)return 0;
 if(++s->entries>MAX_ENTRIES)return fail_text(s,"Folder exceeds 100,000 entries.");
 sqlite3_stmt*q=NULL;int ok=sqlite3_prepare_v2(s->db,"INSERT INTO entries(parent,path,name,dir,size,mtime,nsec,seen) VALUES(?,?,?,?,?,?,?,?) ON CONFLICT(path) DO UPDATE SET name=excluded.name,dir=excluded.dir,size=excluded.size,mtime=excluded.mtime,nsec=excluded.nsec,seen=excluded.seen",-1,&q,NULL)==SQLITE_OK;
 if(ok){sqlite3_bind_text(q,1,s->listing,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,2,path,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,3,name,-1,SQLITE_TRANSIENT);sqlite3_bind_int(q,4,dir);sqlite3_bind_int64(q,5,f->end_of_file);sqlite3_bind_int64(q,6,f->last_write_time.tv_sec);sqlite3_bind_int(q,7,f->last_write_time.tv_usec*1000);sqlite3_bind_int(q,8,s->epoch);ok=sqlite3_step(q)==SQLITE_DONE;}
 sqlite3_finalize(q);if(ok&&s->queue_children)ok=job_add(s,path,dir);return ok;
}
static void dir_open_cb(struct smb2_context*c,int status,void*data,void*arg){smb_state_t*s=arg;if(!status)memcpy(s->dir_id,((struct smb2_create_reply*)data)->file_id,SMB2_FD_SIZE);rpc_cb(c,status? -EIO:0,NULL,arg);}
static void query_cb(struct smb2_context*c,int status,void*data,void*arg){
 smb_state_t*s=arg;if((uint32_t)status==SMB2_STATUS_NO_MORE_FILES){s->query_end=1;rpc_cb(c,0,NULL,arg);return;}
 if(status){rpc_cb(c,-EIO,NULL,arg);return;}
 struct smb2_query_directory_reply*r=data;size_t off=0;int ok=r&&r->output_buffer_length>0;
 while(ok&&off<r->output_buffer_length){
  struct smb2_iovec v={.buf=r->output_buffer+off,.len=r->output_buffer_length-off};struct smb2_fileidfulldirectoryinformation f={0};
  if(v.len<SMB2_FILEID_FULL_DIRECTORY_INFORMATION_SIZE||smb2_decode_fileidfulldirectoryinformation(c,&f,&v)<0){ok=0;break;}
  ok=f.name&&save_entry(s,f.name,&f);free((void*)f.name);
  if(!f.next_entry_offset)break;if(f.next_entry_offset<80||f.next_entry_offset>v.len-80){ok=0;break;}off+=f.next_entry_offset;
  if(cancelled())ok=0;
 }
 rpc_cb(c,ok?0:-EIO,NULL,arg);
}
static int list_directory(smb_state_t*s,const char*path,int queue){
 if(!safe_path(path)||!connect_share(s)||!sql(s,"BEGIN"))return 0;
 s->operation="Read SMB folder";
 s->listing=path;s->queue_children=queue;s->entries=0;s->epoch++;s->query_end=0;
 struct smb2_create_request o={0};o.requested_oplock_level=SMB2_OPLOCK_LEVEL_NONE;o.impersonation_level=SMB2_IMPERSONATION_IMPERSONATION;o.desired_access=SMB2_FILE_LIST_DIRECTORY|SMB2_FILE_READ_ATTRIBUTES;o.file_attributes=SMB2_FILE_ATTRIBUTE_DIRECTORY;o.share_access=SMB2_FILE_SHARE_READ|SMB2_FILE_SHARE_WRITE;o.create_disposition=SMB2_FILE_OPEN;o.create_options=SMB2_FILE_DIRECTORY_FILE;o.name=path;
 begin(s);struct smb2_pdu*p=smb2_cmd_create_async(s->client,&o,dir_open_cb,s);int opened=0,ok=p!=NULL;
 if(ok){smb2_queue_pdu(s->client,p);ok=wait_rpc(s);opened=ok;}
 while(ok&&!s->query_end){
  struct smb2_query_directory_request q={0};q.file_information_class=SMB2_FILE_ID_FULL_DIRECTORY_INFORMATION;memcpy(q.file_id,s->dir_id,SMB2_FD_SIZE);q.output_buffer_length=65536;q.name="*";
  begin(s);p=smb2_cmd_query_directory_async(s->client,&q,query_cb,s);if(!p){ok=0;break;}smb2_queue_pdu(s->client,p);ok=wait_rpc(s);
  if(ok&&(!ds_available_memory()||!ds_storage_headroom(65536)))ok=fail_text(s,"Not enough memory or SD space.");
 }
 if(opened&&s->client){struct smb2_close_request q={0};memcpy(q.file_id,s->dir_id,SMB2_FD_SIZE);begin(s);p=smb2_cmd_close_async(s->client,&q,rpc_cb,s);if(p){smb2_queue_pdu(s->client,p);if(!wait_rpc(s))ok=0;}else ok=0;}
 if(ok){sqlite3_stmt*q=NULL;ok=sqlite3_prepare_v2(s->db,"DELETE FROM entries WHERE parent=? AND seen!=?",-1,&q,NULL)==SQLITE_OK;if(ok){sqlite3_bind_text(q,1,path,-1,SQLITE_TRANSIENT);sqlite3_bind_int(q,2,s->epoch);ok=sqlite3_step(q)==SQLITE_DONE;}sqlite3_finalize(q);}
 if(ok)ok=sql(s,"COMMIT");if(!ok){sql(s,"ROLLBACK");drop(s);}return ok;
}
/* Open destination parents relative to the SD root, refusing links at each step. */
static int parent_fd(const char*remote,int create,char name[STR]){
 if(!safe_path(remote)||!*remote)return -1;char p[PATHCAP];strcpy(p,remote);char*slash=strrchr(p,'/');const char*leaf=slash?slash+1:p;if(strlen(leaf)>=STR)return -1;strcpy(name,leaf);if(slash)*slash=0;else p[0]=0;
 int fd=open(ds_download_root,O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);if(fd<0)return -1;
 char*save=NULL;for(char*c=strtok_r(p,"/",&save);c;c=strtok_r(NULL,"/",&save)){
  if(create&&mkdirat(fd,c,0755)&&errno!=EEXIST){close(fd);return -1;}
  int next=openat(fd,c,O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);close(fd);if(next<0)return -1;fd=next;
 }return fd;
}
static int owned(smb_state_t*s,const char*remote,const struct stat*st,int64_t size,int64_t mtime,int nsec){
 sqlite3_stmt*q=NULL;int ok=sqlite3_prepare_v2(s->db,"SELECT size,mtime,nsec FROM downloads WHERE source=? AND path=?",-1,&q,NULL)==SQLITE_OK;
 if(ok){sqlite3_bind_text(q,1,s->source,-1,SQLITE_STATIC);sqlite3_bind_text(q,2,remote,-1,SQLITE_TRANSIENT);ok=sqlite3_step(q)==SQLITE_ROW&&S_ISREG(st->st_mode)&&st->st_size==sqlite3_column_int64(q,0)&&(size<0||(sqlite3_column_int64(q,0)==size&&sqlite3_column_int64(q,1)==mtime&&sqlite3_column_int(q,2)==nsec));}sqlite3_finalize(q);return ok;
}
static int presence(smb_state_t*s,const char*remote,int dir,int64_t size,int64_t mt,int ns){
 char name[STR];int fd=parent_fd(remote,0,name);if(fd<0)return 0;struct stat st;int exists=!fstatat(fd,name,&st,AT_SYMLINK_NOFOLLOW);close(fd);if(!exists)return 0;
 if(dir)return S_ISDIR(st.st_mode)?2:0;if(!S_ISREG(st.st_mode))return 0;return owned(s,remote,&st,size,mt,ns)?1:2;
}
static int publish(smb_state_t*s,request_t r){
 char parent[PATHCAP];if(!resolve(s,r.id,parent))return 0;
 sqlite3_stmt*q=NULL;int total=0;const char*order="dir DESC,name COLLATE NOCASE,path";
 if(sqlite3_prepare_v2(s->db,"SELECT count(*) FROM entries WHERE parent=?",-1,&q,NULL)!=SQLITE_OK)return 0;sqlite3_bind_text(q,1,parent,-1,SQLITE_STATIC);if(sqlite3_step(q)==SQLITE_ROW)total=sqlite3_column_int(q,0);sqlite3_finalize(q);
 if(r.jump){const char*cond=r.letter=='#'?"upper(substr(name,1,1)) NOT BETWEEN 'A' AND 'Z'":"upper(substr(name,1,1))=?";
  char text[600];snprintf(text,sizeof text,"SELECT id FROM entries WHERE parent=? AND %s ORDER BY %s LIMIT 1",cond,order);
  if(sqlite3_prepare_v2(s->db,text,-1,&q,NULL)!=SQLITE_OK)return 0;sqlite3_bind_text(q,1,parent,-1,SQLITE_STATIC);char letter[2]={r.letter,0};if(r.letter!='#')sqlite3_bind_text(q,2,letter,-1,SQLITE_TRANSIENT);int64_t id=sqlite3_step(q)==SQLITE_ROW?sqlite3_column_int64(q,0):0;sqlite3_finalize(q);q=NULL;
  /* Two bounded scans on the disk index; never allocate the directory in RAM. */
  if(id){if(sqlite3_prepare_v2(s->db,"SELECT id FROM entries WHERE parent=? ORDER BY dir DESC,name COLLATE NOCASE,path",-1,&q,NULL)!=SQLITE_OK)return 0;sqlite3_bind_text(q,1,parent,-1,SQLITE_STATIC);r.offset=0;while(sqlite3_step(q)==SQLITE_ROW&&sqlite3_column_int64(q,0)!=id)r.offset++;sqlite3_finalize(q);}
 }
 if(r.offset<0)r.offset=0;if(r.offset>=total)r.offset=total>PAGE?total-PAGE:0;
 if(sqlite3_prepare_v2(s->db,"SELECT id,name,path,dir,size,mtime,nsec FROM entries WHERE parent=? ORDER BY dir DESC,name COLLATE NOCASE,path LIMIT 32 OFFSET ?",-1,&q,NULL)!=SQLITE_OK)return 0;sqlite3_bind_text(q,1,parent,-1,SQLITE_STATIC);sqlite3_bind_int(q,2,r.offset);
 int n=0,rc;while((rc=sqlite3_step(q))==SQLITE_ROW){item_t*x=&ds_shared.scratch[n++];memset(x,0,sizeof *x);snprintf(x->id,IDLEN,"%lld",(long long)sqlite3_column_int64(q,0));snprintf(x->name,STR,"%s",sqlite3_column_text(q,1));x->kind=sqlite3_column_int(q,3)?SMB_DIR:SMB_FILE;x->size=sqlite3_column_int64(q,4);x->local=presence(s,(const char*)sqlite3_column_text(q,2),x->kind==SMB_DIR,x->size,sqlite3_column_int64(q,5),sqlite3_column_int(q,6));}
 sqlite3_finalize(q);if(rc!=SQLITE_DONE)return 0;
 pthread_mutex_lock(&ds_shared.mu);memcpy(ds_shared.rows,ds_shared.scratch,n*sizeof(item_t));ds_shared.count=n;ds_shared.total_count=total;ds_shared.more=r.offset+n<total;ds_shared.offset=r.offset;ds_shared.view=SMB_BROWSER;ds_shared.letter=r.letter;pthread_mutex_unlock(&ds_shared.mu);return 1;
}
static int stat_remote(smb_state_t*s,struct smb2fh*fh,struct smb2_stat_64*st){begin(s);return queued(s,smb2_fstat_async(s->client,fh,st,rpc_cb,s));}
static int stable(const struct smb2_stat_64*a,const struct smb2_stat_64*b){return a->smb2_type==SMB2_TYPE_FILE&&b->smb2_type==SMB2_TYPE_FILE&&a->smb2_ino==b->smb2_ino&&a->smb2_size==b->smb2_size&&a->smb2_mtime==b->smb2_mtime&&a->smb2_mtime_nsec==b->smb2_mtime_nsec;}
static int download_file(smb_state_t*s,const char*path,int replace,int*saved,int*kept){
 if(!connect_share(s)||!safe_path(path))return 0;
 s->operation="Open SMB file";
 begin(s);if(!queued(s,smb2_open_async(s->client,path,O_RDONLY,rpc_cb,s)))return 0;
 struct smb2fh*fh=s->result;struct smb2_stat_64 before={0},after={0};
 s->operation="Read SMB file information";
 int ok=fh&&stat_remote(s,fh,&before);
 if(ok&&(before.smb2_type!=SMB2_TYPE_FILE||before.smb2_size>INT64_MAX))ok=fail_text(s,"SMB file type or size is unsupported.");
 char name[STR],tmp[80]={0};int dir=-1,fd=-1,temp_owned=0;struct stat old={0};int exists=0,skip=0,was_owned=0;
 if(ok){s->operation="Open SD folder";dir=parent_fd(path,1,name);if(dir<0)ok=fail_errno(s,errno);}
 if(ok){
  s->operation="Check existing SD file";
  if(fstatat(dir,name,&old,AT_SYMLINK_NOFOLLOW)==0){
   exists=1;ok=S_ISREG(old.st_mode);
   if(!ok)fail_text(s,"Destination is not a regular file. Kept existing data.");
   was_owned=ok&&owned(s,path,&old,-1,0,0);skip=ok&&(!replace||!was_owned);
  }else if(errno!=ENOENT)ok=fail_errno(s,errno);
 }
 if(ok&&!skip){
  if(!ds_available_memory())ok=fail_text(s,"Not enough free memory to download.");
  else if(!ds_storage_headroom(before.smb2_size))ok=fail_text(s,"Not enough free SD space for this download and its reserve.");
  if(ok){
   s->operation="Create temporary SD file";
   for(int n=0;n<20;n++){
    snprintf(tmp,sizeof tmp,".shelf-%ld-%d.part",(long)getpid(),n);if(!strcmp(tmp,name))continue;
    fd=openat(dir,tmp,O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);
    if(fd>=0||errno!=EEXIST)break;
   }
   ok=fd>=0;temp_owned=ok;if(!ok)fail_errno(s,errno);
  }
 }
 if(ok&&!skip){
  ds_status_text("Downloading %s",name);uint64_t pos=0;
  pthread_mutex_lock(&ds_shared.mu);ds_shared.transfer_serial++;ds_shared.byte_done=0;ds_shared.byte_total=before.smb2_size;pthread_mutex_unlock(&ds_shared.mu);
  uint32_t max=smb2_get_max_read_size(s->client);if(max>CHUNK)max=CHUNK;
  if(!max)ok=fail_text(s,"SMB server returned an invalid read limit.");
  while(ok&&pos<before.smb2_size){
   uint32_t count=before.smb2_size-pos<max?(uint32_t)(before.smb2_size-pos):max;
   s->operation="Read SMB file";begin(s);
   ok=!cancelled()&&queued(s,smb2_pread_async(s->client,fh,s->buffer,count,pos,rpc_cb,s));
   if(!ok)break;
   int got=s->status;
   if(got<=0||(uint32_t)got>count){ok=fail_text(s,"SMB file ended before the expected size. Retry download.");break;}
   s->operation="Write to SD";
   int at=0;while(at<got){
    ssize_t n=write(fd,s->buffer+at,got-at);
    if(n<0&&errno==EINTR)continue;
    if(n<=0){ok=fail_errno(s,n<0?errno:EIO);break;}at+=n;
   }
   pos+=at;
   pthread_mutex_lock(&ds_shared.mu);ds_shared.byte_done=pos;pthread_mutex_unlock(&ds_shared.mu);
  }
  if(ok){s->operation="Verify SMB file";ok=stat_remote(s,fh,&after);}
  if(ok&&!stable(&before,&after))ok=fail_text(s,"File changed on the SMB server. Retry download.");
  if(ok&&cancelled())ok=0;
  if(ok){s->operation="Flush download to SD";if(fsync(fd))ok=fail_errno(s,errno);}
 }
 if(!ok&&!s->failure[0]&&!cancelled())fail_errno(s,EIO);
 if(fh&&s->client){s->operation="Close SMB file";begin(s);if(!queued(s,smb2_close_async(s->client,fh,rpc_cb,s)))ok=0;}
 if(fd>=0){s->operation="Close SD file";if(close(fd))ok=fail_errno(s,errno);fd=-1;}
 if(ok&&!skip){
  ds_status_text("Saving %s to SD...",name);
  s->operation="Save download to SD";
  struct stat check;
  if(exists){
   if(fstatat(dir,name,&check,AT_SYMLINK_NOFOLLOW))ok=fail_errno(s,errno);
   else if(!S_ISREG(check.st_mode)||check.st_dev!=old.st_dev||check.st_ino!=old.st_ino||check.st_size!=old.st_size||check.st_mtim.tv_sec!=old.st_mtim.tv_sec||check.st_mtim.tv_nsec!=old.st_mtim.tv_nsec)
    ok=fail_text(s,"Destination changed during download. Kept existing file.");
   else if(renameat(dir,tmp,dir,name))ok=fail_errno(s,errno);
  }else if(!ds_smb_publish_new(dir,tmp,name,s->buffer,CHUNK,cancelled,ds_storage_headroom))ok=fail_errno(s,errno);
  if(ok){s->operation="Flush SD folder";if(fsync(dir))ok=fail_errno(s,errno);}
  if(ok){
   sqlite3_stmt*q=NULL;ok=sqlite3_prepare_v2(s->db,"INSERT OR REPLACE INTO downloads VALUES(?,?,?,?,?)",-1,&q,NULL)==SQLITE_OK;
   if(ok){sqlite3_bind_text(q,1,s->source,-1,SQLITE_STATIC);sqlite3_bind_text(q,2,path,-1,SQLITE_STATIC);sqlite3_bind_int64(q,3,before.smb2_size);sqlite3_bind_int64(q,4,before.smb2_mtime);sqlite3_bind_int64(q,5,before.smb2_mtime_nsec);ok=sqlite3_step(q)==SQLITE_DONE;}
   sqlite3_finalize(q);if(ok)(*saved)++;else fail_text(s,"File saved, but download history could not be updated on SD.");
  }
 }
 if(dir>=0){if(temp_owned)unlinkat(dir,tmp,0);close(dir);}if(skip)(*kept)++;if(!ok)drop(s);return ok;
}
static int download_selected(smb_state_t*s,int n,int replace){
 if(!sql(s,"DELETE FROM jobs"))return 0;
 for(int i=0;i<n;i++){char path[PATHCAP];if(!resolve(s,ds_shared.picks[i].id,path)||!job_add(s,path,ds_shared.picks[i].kind==SMB_DIR))return 0;}
 int ok=1,saved=0,kept=0,count=0;
 while(ok&&!cancelled()){
  sqlite3_stmt*q=NULL;if(sqlite3_prepare_v2(s->db,"SELECT path,dir FROM jobs WHERE done=0 ORDER BY path LIMIT 1",-1,&q,NULL)!=SQLITE_OK)return 0;int rc=sqlite3_step(q);if(rc==SQLITE_DONE){sqlite3_finalize(q);break;}if(rc!=SQLITE_ROW){sqlite3_finalize(q);return 0;}
  char path[PATHCAP];snprintf(path,sizeof path,"%s",sqlite3_column_text(q,0));int dir=sqlite3_column_int(q,1);sqlite3_finalize(q);
  int depth=0;for(char*p=path;*p;p++)depth+=*p=='/';if(depth>=32||++count>MAX_ENTRIES)return fail_text(s,"Folder tree exceeds the depth or file limit.");
  if(dir){char leaf[STR];int parent=parent_fd(path,1,leaf);ok=parent>=0;
   if(ok){ok=mkdirat(parent,leaf,0755)==0||errno==EEXIST;if(ok){int child=openat(parent,leaf,O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);ok=child>=0;if(child>=0)close(child);}close(parent);}
   if(ok)ok=list_directory(s,path,1);
  }else ok=download_file(s,path,replace,&saved,&kept);
  if(ok){if(sqlite3_prepare_v2(s->db,"UPDATE jobs SET done=1 WHERE path=?",-1,&q,NULL)!=SQLITE_OK)return 0;sqlite3_bind_text(q,1,path,-1,SQLITE_STATIC);ok=sqlite3_step(q)==SQLITE_DONE;sqlite3_finalize(q);}
 }
 if(cancelled())ok=0;if(ok)ds_status_text("Done: %d downloaded, %d kept.\nUse Disco indexing for new music.",saved,kept);return ok;
}
void *ds_smb_worker(void*unused){
 (void)unused;smb_state_t*s=calloc(1,sizeof *s);if(!s)goto fail_no_state;s->buffer=malloc(CHUNK);if(!s->buffer)goto fail;
 char name[1200],identity[2800];snprintf(identity,sizeof identity,"%s\n%s\n%s\n%s\n%s",ds_server,ds_smb_share,ds_username,ds_smb_domain,ds_download_root);md5_hex(identity,strlen(identity),s->source);snprintf(name,sizeof name,"%s/smb-catalog.sqlite",ds_state_dir);
 if(sqlite3_open(name,&s->db)!=SQLITE_OK)goto fail;sqlite3_busy_timeout(s->db,1000);sqlite3_progress_handler(s->db,1000,sql_stop,NULL);
 if(!sql(s,"PRAGMA cache_size=-128;PRAGMA mmap_size=0;PRAGMA temp_store=FILE;PRAGMA journal_mode=DELETE;PRAGMA max_page_count=32768;CREATE TABLE IF NOT EXISTS entries(id INTEGER PRIMARY KEY AUTOINCREMENT,parent TEXT,path TEXT UNIQUE,name TEXT,dir INTEGER,size INTEGER,mtime INTEGER,nsec INTEGER,seen INTEGER);CREATE INDEX IF NOT EXISTS by_parent ON entries(parent,dir DESC,name COLLATE NOCASE,path);CREATE TABLE IF NOT EXISTS downloads(source TEXT,path TEXT,size INTEGER,mtime INTEGER,nsec INTEGER,PRIMARY KEY(source,path));DROP TABLE IF EXISTS jobs;CREATE TABLE jobs(path TEXT PRIMARY KEY,dir INTEGER,done INTEGER);DELETE FROM entries;"))goto fail;
 ds_status_text("Connecting to SMB3 share...");int connected=connect_share(s);
 pthread_mutex_lock(&ds_shared.mu);ds_shared.busy=0;ds_shared.revision++;pthread_mutex_unlock(&ds_shared.mu);
 if(connected)ds_status_text("Ready to browse folders");
 while(!atomic_load(&ds_stopping)){
  request_t req;int command,n,replace;pthread_mutex_lock(&ds_shared.mu);while(!ds_shared.command&&!atomic_load(&ds_stopping))pthread_cond_wait(&ds_shared.wake,&ds_shared.mu);if(atomic_load(&ds_stopping)){pthread_mutex_unlock(&ds_shared.mu);break;}command=ds_shared.command;ds_shared.command=0;req=ds_shared.req;n=ds_shared.npicks;replace=ds_shared.replace;pthread_mutex_unlock(&ds_shared.mu);
  int ok;s->failure[0]=0;s->operation=command==2?"Download selection":"Load SMB folder";
  if(command==2){int previous_pm=ds_wifi_copy_begin();ok=download_selected(s,n,replace);ds_wifi_copy_end(previous_pm);}else{char path[PATHCAP];ok=resolve(s,req.id,path);if(ok&&command!=3)ok=list_directory(s,path,0);if(ok)ok=publish(s,req);}
  if(!ok){
   if(cancelled())ds_status_text("Cancelled. Completed files kept.");
   else{if(!s->failure[0])snprintf(s->failure,sizeof s->failure,"%s failed. Check connection or SD card, then retry.",s->operation);ds_status_text("%s",s->failure);fprintf(stderr,"Disc Shelf: %s\n",s->failure);}
  }
  pthread_mutex_lock(&ds_shared.mu);if(!ok&&command!=2){ds_shared.count=ds_shared.total_count=0;ds_shared.view=req.view;}ds_shared.busy=0;ds_shared.result_generation=req.generation;ds_shared.done=command==2?(ok?1:2):0;ds_shared.revision++;pthread_mutex_unlock(&ds_shared.mu);
 }
 goto cleanup;
fail:ds_status_text("Cannot open SMB catalogue. Check memory and SD card.");
fail_no_state:pthread_mutex_lock(&ds_shared.mu);ds_shared.busy=0;ds_shared.fatal=1;ds_shared.revision++;pthread_mutex_unlock(&ds_shared.mu);
cleanup:if(s){drop(s);if(s->db){sqlite3_progress_handler(s->db,0,NULL,NULL);sqlite3_exec(s->db,"ROLLBACK",NULL,NULL,NULL);sqlite3_close(s->db);}free(s->buffer);free(s);}shelf_close();return NULL;
}
