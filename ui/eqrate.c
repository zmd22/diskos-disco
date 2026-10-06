/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "eqrate.h"
#include "md5.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pthread.h>
#include <stdatomic.h>
#include <time.h>
#ifndef EQ_PROC_ROOT
#define EQ_PROC_ROOT "/proc"
#endif
/* Exact non-PIE ELF supplied by the owner. Never use this address for another image. */
#define EQ_RATE_ADDRESS 0x839ae4
#define EQ_PLAYER_BYTES 4601368
static const char fingerprint[] = "ff7beab94287f586d6faa2b51924c901";
static long cached_pid;
static unsigned long long cached_start;
static struct stat cached_image;
static int cached_match;
static int same_image(const struct stat *a, const struct stat *b){
    return a->st_dev==b->st_dev && a->st_ino==b->st_ino && a->st_size==b->st_size &&
        a->st_mtim.tv_sec==b->st_mtim.tv_sec && a->st_mtim.tv_nsec==b->st_mtim.tv_nsec &&
        a->st_ctim.tv_sec==b->st_ctim.tv_sec && a->st_ctim.tv_nsec==b->st_ctim.tv_nsec;
}
static unsigned long long started(long pid){
    char path[128],line[2048]; snprintf(path,sizeof path,EQ_PROC_ROOT "/%ld/stat",pid);
    FILE *f=fopen(path,"r"); if(!f)return 0;
    char *r=fgets(line,sizeof line,f); fclose(f); if(!r)return 0;
    char *p=strrchr(line,')'); if(!p)return 0; p++;
    for(int field=3;field<22;field++){
        while(*p==' ')p++;
        if(!*p)return 0;
        while(*p && *p!=' ')p++;
    }
    char *end; errno=0; unsigned long long v=strtoull(p,&end,10);
    return errno || end==p || (*end!=' ' && *end!='\n' && *end) ? 0 : v;
}
static int read_player_rate(void){
    DIR *d=opendir(EQ_PROC_ROOT); if(!d)return 0;
    long pid=0; struct dirent *e;
    while((e=readdir(d))){
        char *end; long n=strtol(e->d_name,&end,10); if(n<=0 || *end)continue;
        char path[128],comm[32]; snprintf(path,sizeof path,EQ_PROC_ROOT "/%ld/comm",n);
        FILE *f=fopen(path,"r"); if(!f)continue;
        char *r=fgets(comm,sizeof comm,f); fclose(f);
        if(r && !strcmp(comm,"mq_player\n")){ if(pid){closedir(d);return 0;} pid=n; }
    }
    closedir(d); if(!pid)return 0;
    unsigned long long start=started(pid); if(!start)return 0;
    char path[128]; snprintf(path,sizeof path,EQ_PROC_ROOT "/%ld/exe",pid);
    int exe=open(path,O_RDONLY|O_CLOEXEC); if(exe<0)return 0;
    struct stat image; if(fstat(exe,&image) || image.st_size!=EQ_PLAYER_BYTES){close(exe);return 0;}
    if(pid!=cached_pid || start!=cached_start || !same_image(&image,&cached_image)){
        cached_match=0; cached_pid=pid; cached_start=start; cached_image=image;
        void *p=mmap(NULL,(size_t)image.st_size,PROT_READ,MAP_PRIVATE,exe,0);
        if(p!=MAP_FAILED){char hash[33];md5_hex(p,(size_t)image.st_size,hash);
            munmap(p,(size_t)image.st_size);cached_match=!strcmp(hash,fingerprint);}
    }
    close(exe); if(!cached_match)return 0;
    snprintf(path,sizeof path,EQ_PROC_ROOT "/%ld/mem",pid);
    int mem=open(path,O_RDONLY|O_CLOEXEC); if(mem<0)return 0;
    unsigned char raw[4]; ssize_t got;
    do{got=pread(mem,raw,sizeof raw,EQ_RATE_ADDRESS);}while(got<0 && errno==EINTR);
    close(mem); if(got!=4 || started(pid)!=start)return 0;
    /* Reject a concurrent exec/image replacement rather than trust an earlier fingerprint. */
    snprintf(path,sizeof path,EQ_PROC_ROOT "/%ld/exe",pid);
    exe=open(path,O_RDONLY|O_CLOEXEC); if(exe<0)return 0;
    struct stat after; int same=!fstat(exe,&after) && same_image(&image,&after);close(exe);
    if(!same)return 0;
    uint32_t rate=(uint32_t)raw[0]|((uint32_t)raw[1]<<8)|((uint32_t)raw[2]<<16)|((uint32_t)raw[3]<<24);
    return rate>=8000 && rate<=768000 ? (int)rate : 0;
}

/* All /proc scanning and fingerprinting runs on workers. Serialize the identity
 * cache and publish only 32-bit atomics (portable to the target MIPS ABI). */
static pthread_mutex_t probe_lock=PTHREAD_MUTEX_INITIALIZER;
static _Atomic unsigned rate_seq,rate_stamp,rate_value,probe_busy;
static unsigned now_ms(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (unsigned)((unsigned long long)t.tv_sec*1000+t.tv_nsec/1000000);}
int eq_player_rate(void){
    pthread_mutex_lock(&probe_lock);
    int rate=read_player_rate();
    atomic_fetch_add(&rate_seq,1);
    atomic_store(&rate_value,(unsigned)rate);atomic_store(&rate_stamp,now_ms());
    atomic_fetch_add(&rate_seq,1);
    pthread_mutex_unlock(&probe_lock);return rate;
}
int eq_rate_cached(void){
    unsigned a=atomic_load(&rate_seq);if(a&1)return 0;
    unsigned value=atomic_load(&rate_value),stamp=atomic_load(&rate_stamp);
    unsigned b=atomic_load(&rate_seq);
    return a==b && !(b&1) && (unsigned)(now_ms()-stamp)<2500 ? (int)value : 0;
}
static void *probe_worker(void *unused){(void)unused;eq_player_rate();atomic_store(&probe_busy,0);return NULL;}
void eq_rate_request(void){
    unsigned expected=0;
    if((unsigned)(now_ms()-atomic_load(&rate_stamp))<750 && atomic_load(&rate_seq))return;
    if(!atomic_compare_exchange_strong(&probe_busy,&expected,1))return;
    pthread_t th;if(pthread_create(&th,NULL,probe_worker,NULL)){atomic_store(&probe_busy,0);return;}
    pthread_detach(th);
}
