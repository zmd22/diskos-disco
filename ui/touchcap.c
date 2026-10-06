/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <time.h>
#include <poll.h>
struct iev { long sec,usec; uint16_t type,code; int32_t value; };
int main(int c,char**v){
    const char*dev=c>1?v[1]:"/dev/input/event1";
    int fd=open(dev,O_RDONLY|O_NONBLOCK);
    printf("CAP %s fd=%d (read-only, alongside mq_ui) 20s\n",dev,fd); fflush(stdout);
    struct pollfd p={fd,POLLIN,0}; struct iev e; time_t t0=time(0); int n=0;
    int minx=99999,maxx=-1,miny=99999,maxy=-1;
    while(time(0)-t0<20){
        if(poll(&p,1,300)<=0) continue;
        while(read(fd,&e,sizeof e)==sizeof e){
            if(e.type==3){
                printf("ABS code=0x%02x val=%d\n",e.code,e.value);
                if(e.code==0||e.code==0x35){ if(e.value<minx)minx=e.value; if(e.value>maxx)maxx=e.value; }
                if(e.code==1||e.code==0x36){ if(e.value<miny)miny=e.value; if(e.value>maxy)maxy=e.value; }
                n++;
            } else if(e.type==1) printf("KEY code=0x%03x val=%d\n",e.code,e.value);
            fflush(stdout);
        }
    }
    printf("CAP done n=%d x[%d..%d] y[%d..%d]\n",n,minx,maxx,miny,maxy);
    return 0;
}
