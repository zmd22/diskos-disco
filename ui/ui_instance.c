/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "ui_instance.h"
#include <sys/file.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#ifndef UI_INSTANCE_LOCK
#define UI_INSTANCE_LOCK "/tmp/diskos-ui.lock"
#endif
int ui_instance_acquire(void){
    static int held=-1;if(held>=0)return 1;
    int fd=open(UI_INSTANCE_LOCK,O_RDWR|O_CREAT|O_CLOEXEC|O_NOFOLLOW,0600);
    if(fd<0)return 0;
    struct stat st;
    if(fstat(fd,&st) || !S_ISREG(st.st_mode) || flock(fd,LOCK_EX|LOCK_NB)){close(fd);return 0;}
    held=fd;return 1; /* never unlink the lock file; exit/exec releases ownership */
}
