/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Exercise the production filesystem helpers, with controlled I/O failures. */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "../ipc.h"
static track_state_t playing;
static int fail_read, fail_install, fail_restore;
static ssize_t test_read(int fd, void *buf, size_t n){
    if(fail_read){ errno=EIO; return -1; }
    return read(fd,buf,n);
}
static int test_rename(const char *src, const char *dst){
    size_t n=strlen(src);
    if((fail_install && n>=4 && !strcmp(src+n-4,"/new")) ||
       (fail_restore && n>=9 && !strcmp(src+n-9,"/original"))){ errno=EIO; return -1; }
    return rename(src,dst);
}
#define read test_read
#define rename test_rename
#include "../fileops.c"
#undef read
#undef rename
void ipc_get_state(track_state_t *out){ *out=playing; }
int sd_write_begin(void){ return 1; }
void sd_write_end(void){}
static void write_text(const char *p,const char *s){
    FILE *f=fopen(p,"w"); assert(f); assert(fputs(s,f)>=0); assert(!fclose(f));
}
static void expect_text(const char *p,const char *s){
    char b[100]={0}; FILE *f=fopen(p,"r"); assert(f); assert(fread(b,1,sizeof b-1,f)==strlen(s)); fclose(f); assert(!strcmp(b,s));
}
int main(void){
    char root[]="/tmp/diskos-fileops-XXXXXX"; assert(mkdtemp(root));
    char src[FO_PATH], dst[FO_PATH], partial[FO_PATH];
    assert(path_join(src,sizeof src,root,"source")); assert(path_join(dst,sizeof dst,root,"target"));
    assert(path_join(partial,sizeof partial,root,"target.part"));
    write_text(src,"new audio"); write_text(partial,"another transfer");
    assert(!copy_file(src,dst)); expect_text(dst,"new audio"); expect_text(partial,"another transfer");
    puts("PASS: copy preserves a pre-existing destination .part file");
    write_text(dst,"original"); fail_read=1;
    assert(replace_tree(src,dst,0)<0); expect_text(dst,"original"); fail_read=0;
    fail_install=1; assert(replace_tree(src,dst,0)<0); expect_text(dst,"original"); fail_install=0;
    assert(replace_tree("/nonexistent/diskos-source",dst,0)<0); expect_text(dst,"original");
    puts("PASS: read failure, missing source and failed install preserve the original");
    playing.have_track=1; snprintf(playing.path,sizeof playing.path,"%s",dst);
    assert(replace_tree(src,dst,0)<0); expect_text(dst,"original");
    memset(&W,0,sizeof W); W.op=OP_COPY; W.replace=1;
    snprintf(W.src,sizeof W.src,"%s",src); snprintf(W.dst,sizeof W.dst,"%s",dst);
    worker(NULL); assert(W.finished && !W.ok); expect_text(dst,"original");
    memset(&playing,0,sizeof playing);
    puts("PASS: replacement and worker reject a currently playing destination");
    assert(!replace_tree(src,dst,0)); expect_text(src,"new audio"); expect_text(dst,"new audio");
    write_text(dst,"original"); assert(!replace_tree(src,dst,1)); assert(access(src,F_OK)<0); expect_text(dst,"new audio");
    puts("PASS: successful replacement copy and move retain the expected files");
    char sd[FO_PATH], dd[FO_PATH], sf[FO_PATH], df[FO_PATH];
    assert(path_join(sd,sizeof sd,root,"source-dir")); assert(path_join(dd,sizeof dd,root,"target-dir"));
    assert(!mkdir(sd,0755)); assert(!mkdir(dd,0755));
    assert(path_join(sf,sizeof sf,sd,"track")); assert(path_join(df,sizeof df,dd,"track"));
    write_text(sf,"new album"); write_text(df,"old album");
    playing.have_track=1; snprintf(playing.path,sizeof playing.path,"%s",df);
    assert(replace_tree(sd,dd,0)<0); expect_text(df,"old album");
    memset(&playing,0,sizeof playing); assert(!replace_tree(sd,dd,0)); expect_text(df,"new album");
    puts("PASS: folder replacement protects a playing descendant and installs a complete new tree");
    write_text(src,"new audio"); write_text(dst,"original"); fail_install=fail_restore=1;
    assert(replace_tree(src,dst,0)<0); fail_install=fail_restore=0;
    DIR *d=opendir(root); assert(d); struct dirent *e; int found=0;
    while((e=readdir(d))) if(strstr(e->d_name,"target.diskos-replace-")){
        char stage[FO_PATH], backup[FO_PATH];
        assert(path_join(stage,sizeof stage,root,e->d_name)); assert(path_join(backup,sizeof backup,stage,"original"));
        expect_text(backup,"original"); found++;
    }
    closedir(d); assert(found==1);
    puts("PASS: failed rollback preserves the original in its backup folder");
    assert(!rm_tree(root,0)); return 0;
}
