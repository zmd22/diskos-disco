/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "connection.h"
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
atomic_int ds_source;
char ds_smb_share[256],ds_smb_domain[128];
static pthread_mutex_t secrets_mu=PTHREAD_MUTEX_INITIALIZER;
static ds_profile_t session[2];
void ds_secret_clear(void*p,size_t n){volatile unsigned char*b=p;while(n--)*b++=0;}
void ds_session_clear(void){pthread_mutex_lock(&secrets_mu);ds_secret_clear(session,sizeof session);pthread_mutex_unlock(&secrets_mu);}
static int same(const ds_profile_t*a,const ds_profile_t*b){return !strcmp(a->server,b->server)&&!strcmp(a->user,b->user)&&!strcmp(a->share,b->share)&&!strcmp(a->domain,b->domain);}
static const char*names[]={"server","username","password","download_dir","share","domain"};
static char*field(ds_profile_t*p,int i,size_t*n){switch(i){case 0:*n=sizeof p->server;return p->server;case 1:*n=sizeof p->user;return p->user;case 2:*n=sizeof p->password;return p->password;case 3:*n=sizeof p->dest;return p->dest;case 4:*n=sizeof p->share;return p->share;default:*n=sizeof p->domain;return p->domain;}}
int ds_settings_load(const char*path,ds_settings_t*c){
 memset(c,0,sizeof *c);c->profile[0].remember=c->profile[1].remember=0;
 strcpy(c->profile[0].dest,"/tmp/sdcard/Navidrome");strcpy(c->profile[1].dest,"/tmp/sdcard/SMB");
 int fd=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);if(fd<0)return 0;
 struct stat st;if(fstat(fd,&st)||!S_ISREG(st.st_mode)){close(fd);return 0;}
 FILE*f=fdopen(fd,"r");if(!f){close(fd);return 0;}char line[2048];int ok=1,remember_seen[2]={0};
 while(fgets(line,sizeof line,f)){
  if(!strchr(line,'\n')&&!feof(f)){ok=0;break;}line[strcspn(line,"\r\n")]=0;
  if(!line[0]||line[0]=='#')continue;char*value=strchr(line,'=');if(!value)continue;*value++=0;
  if(!strcmp(line,"source")){if(!strcmp(value,"smb"))c->source=DS_SMB;else if(strcmp(value,"navidrome")){ok=0;break;}continue;}
  int s=!strncmp(line,"smb_",4);const char*k=line+(s?4:0);ds_profile_t*p=&c->profile[s];
  if(!strcmp(k,"remember_password")){if(strcmp(value,"0")&&strcmp(value,"1")){ok=0;break;}p->remember=atoi(value);remember_seen[s]=1;continue;}
  for(int i=0;i<6;i++)if(!strcmp(k,names[i])){size_t n;char*dst=field(p,i,&n);if(strlen(value)>=n){ok=0;break;}strcpy(dst,value);}
  if(!ok)break;
 }
 if(ferror(f))ok=0;fclose(f);ds_secret_clear(line,sizeof line);
 for(int i=0;i<2;i++)if(!remember_seen[i])c->profile[i].remember=c->profile[i].password[0]!=0;
 pthread_mutex_lock(&secrets_mu);
 for(int i=0;i<2;i++)if(!c->profile[i].remember){c->profile[i].password[0]=0;if(same(&c->profile[i],&session[i]))strcpy(c->profile[i].password,session[i].password);}
 pthread_mutex_unlock(&secrets_mu);return ok;
}
int ds_settings_validate(const ds_settings_t*c,int test,char*err,size_t cap){
 if(c->source<0||c->source>1){snprintf(err,cap,"Choose Navidrome or SMB");return 0;}
 const ds_profile_t*p=&c->profile[c->source];
 for(int s=0;s<2;s++)for(int i=0;i<6;i++){size_t n;char*v=field((ds_profile_t*)&c->profile[s],i,&n);if(strpbrk(v,"\r\n")){snprintf(err,cap,"Settings cannot contain new lines");return 0;}}
 int server_ok=c->source==DS_SMB?p->server[0]&&!strpbrk(p->server," /\\\t@?#"):
 (!strncmp(p->server,"http://",7)||!strncmp(p->server,"https://",8))&&!strpbrk(p->server," \t?#@")&&p->server[!strncmp(p->server,"https://",8)?8:7]!=0;
 if(!p->server[0]){snprintf(err,cap,"Set the server");return 0;}
 if(!server_ok){snprintf(err,cap,c->source==DS_SMB?"Server: host or IP only, no smb:// or path":"Server must start with http:// or https://");return 0;}
 if(!p->user[0]){snprintf(err,cap,"Set the username");return 0;}
 if(!p->password[0]){snprintf(err,cap,"Set the password");return 0;}
 if(c->source==DS_SMB&&(!p->share[0]||strpbrk(p->share,"/\\")||!strcmp(p->share,".")||!strcmp(p->share,".."))){snprintf(err,cap,"Set a share name, without a folder path");return 0;}
 if((!test&&strncmp(p->dest,"/tmp/sdcard/",12))||strstr(p->dest,"..")||!p->dest[0]||strlen(p->dest)>900){snprintf(err,cap,"Download folder must be on the SD card");return 0;}
 return 1;
}
int ds_settings_save(const char*path,const ds_settings_t*c,char*err,size_t cap){
 if(!ds_settings_validate(c,0,err,cap))return 0;
 char parent[1200],tmp[1280];if(strlen(path)>=sizeof parent)goto fail;strcpy(parent,path);char*slash=strrchr(parent,'/');if(!slash)goto fail;*slash=0;
 struct stat st;if(lstat(parent,&st)||!S_ISDIR(st.st_mode)||chmod(parent,0700))goto fail;
 if(snprintf(tmp,sizeof tmp,"%s/.shelf-XXXXXX",parent)>=(int)sizeof tmp)goto fail;
 int fd=mkstemp(tmp);if(fd<0)goto fail;FILE*f=fdopen(fd,"w");int ok=f!=NULL;
 if(f){if(fprintf(f,"source=%s\n",c->source==DS_SMB?"smb":"navidrome")<0)ok=0;
  for(int s=0;s<2;s++){const ds_profile_t*p=&c->profile[s];const char*prefix=s?"smb_":"";
   if(fprintf(f,"%sremember_password=%d\n",prefix,!!p->remember)<0)ok=0;
   for(int i=0;i<6;i++){size_t n;char*v=field((ds_profile_t*)p,i,&n);if(i==2&&!p->remember)continue;if(fprintf(f,"%s%s=%s\n",prefix,names[i],v)<0)ok=0;}
  }
  if(fflush(f)||fsync(fd))ok=0;if(fclose(f))ok=0;
 }else close(fd);
 if(!ok||rename(tmp,path)){unlink(tmp);goto fail;}
 int d=open(parent,O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);int durable=d>=0&&fsync(d)==0;if(d>=0)close(d);
 pthread_mutex_lock(&secrets_mu);for(int s=0;s<2;s++){ds_secret_clear(&session[s],sizeof session[s]);if(!c->profile[s].remember)session[s]=c->profile[s];}pthread_mutex_unlock(&secrets_mu);
 snprintf(err,cap,"%s",durable?"Server settings saved":"Saved; folder sync failed");return 1;
fail:snprintf(err,cap,"Cannot save; previous settings kept");return 0;
}
