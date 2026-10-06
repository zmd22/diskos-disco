/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <time.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
struct iev { long sec,usec; uint16_t type,code; int32_t value; };
int main(void){
    int fb=open("/dev/fb0",O_RDWR);
    struct fb_var_screeninfo v; struct fb_fix_screeninfo f;
    ioctl(fb,FBIOGET_VSCREENINFO,&v); ioctl(fb,FBIOGET_FSCREENINFO,&f);
    uint8_t*m=mmap(0,f.smem_len,PROT_READ|PROT_WRITE,MAP_SHARED,fb,0);
    /* draw on the visible page */
    uint8_t*base=m + (size_t)v.yoffset*f.line_length;
    for(unsigned y=0;y<v.yres;y++){ uint32_t*r=(uint32_t*)(base+(size_t)y*f.line_length); for(unsigned x=0;x<v.xres;x++) r[x]=0x00000000; }
    /* white filled circle at center (180,180) r=28 */
    for(int dy=-28;dy<=28;dy++) for(int dx=-28;dx<=28;dx++){ if(dx*dx+dy*dy<=28*28){ int x=180+dx,y=180+dy; if(x>=0&&x<360&&y>=0&&y<360) ((uint32_t*)(base+(size_t)y*f.line_length))[x]=0x00FFFFFF; } }
    int ev=open("/dev/input/event1",O_RDONLY|O_NONBLOCK);
    printf("DISCOVER fb=%d ev=%d : tap the WHITE CENTER DOT several times (12s)\n",fb,ev); fflush(stdout);
    struct pollfd p={ev,POLLIN,0}; struct iev e; time_t t0=time(0); int n=0;
    while(time(0)-t0<12){
        if(poll(&p,1,200)<=0) continue;
        while(read(ev,&e,sizeof e)==sizeof e){
            if(e.type==0) continue;
            printf("type=%d code=0x%03x val=%d\n",e.type,e.code,e.value);
            if(++n>200){ printf("(cap)\n"); goto done; } fflush(stdout);
        }
    }
done:
    printf("DISCOVER done (%d events)\n",n);
    return 0;
}
