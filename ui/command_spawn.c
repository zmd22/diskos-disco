/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "command_spawn.h"
#include <spawn.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
#include <time.h>
#include <dirent.h>
#include <stdlib.h>
extern char **environ;
int command_spawn(pid_t *pid, char *const argv[]){ return command_spawn_io(pid, argv, -1, -1); }
int command_spawn_io(pid_t *pid, char *const argv[], int out_fd, int pass_fd){
    posix_spawn_file_actions_t fa;
    posix_spawnattr_t at;
    int rc=posix_spawn_file_actions_init(&fa);
    if(rc)return rc;
    rc=posix_spawnattr_init(&at);
    if(rc){posix_spawn_file_actions_destroy(&fa);return rc;}
    rc=posix_spawn_file_actions_addopen(&fa,0,"/dev/null",O_RDONLY,0);
    if(!rc && out_fd>=0)rc=posix_spawn_file_actions_adddup2(&fa,out_fd,1);        /* stdout into the caller's pipe */
    if(!rc && pass_fd>=0){                                                         /* handed over as fd 3 */
        if(pass_fd==3){ rc=posix_spawn_file_actions_adddup2(&fa,3,255);            /* dup2(3,3) would keep close-on-exec */
                        if(!rc)rc=posix_spawn_file_actions_adddup2(&fa,255,3); }
        else rc=posix_spawn_file_actions_adddup2(&fa,pass_fd,3);
    }
    /* The legacy runner closed 3..255. Also close observed high descriptors.
     * Prepared in the parent: no application code runs after a threaded fork. */
    for(int fd=pass_fd>=0?4:3;fd<256 && !rc;fd++)rc=posix_spawn_file_actions_addclose(&fa,fd);
    DIR *d=opendir("/proc/self/fd");
    if(d){struct dirent *e;while(!rc && (e=readdir(d))){
        char *end;long fd=strtol(e->d_name,&end,10);
        if(!*end && fd>=256 && fd!=dirfd(d))rc=posix_spawn_file_actions_addclose(&fa,(int)fd);
    }closedir(d);}
    sigset_t mask;sigemptyset(&mask);
    if(!rc)rc=posix_spawnattr_setsigmask(&at,&mask);
    if(!rc)rc=posix_spawnattr_setpgroup(&at,0);
    if(!rc)rc=posix_spawnattr_setflags(&at,POSIX_SPAWN_SETPGROUP|POSIX_SPAWN_SETSIGMASK);
    if(!rc)rc=posix_spawnp(pid,argv[0],&fa,&at,argv,environ);
    posix_spawnattr_destroy(&at);posix_spawn_file_actions_destroy(&fa);
    return rc;
}
static long long now_ms(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (long long)t.tv_sec*1000+t.tv_nsec/1000000;}
int command_wait(char *const argv[],int timeout_ms){
    pid_t pid;int rc=command_spawn(&pid,argv);if(rc)return -1;
    long long deadline=now_ms()+timeout_ms;
    for(;;){
        int status;pid_t w=waitpid(pid,&status,WNOHANG);
        if(w==pid)return WIFEXITED(status)&&WEXITSTATUS(status)==0?0:-1;
        if(w<0 && errno!=EINTR)return -1;
        if(now_ms()>=deadline){
            kill(-pid,SIGKILL);kill(pid,SIGKILL);
            while(waitpid(pid,&status,0)<0 && errno==EINTR){}
            return -1;
        }
        usleep(10000);
    }
}
