/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
int main(int c,char**v){
    const char*out = c>1?v[1]:"/usr/data/fb.raw";
    int fd=open("/dev/fb0",O_RDONLY); if(fd<0){perror("fb");return 1;}
    struct fb_var_screeninfo vi; struct fb_fix_screeninfo fi;
    ioctl(fd,FBIOGET_VSCREENINFO,&vi); ioctl(fd,FBIOGET_FSCREENINFO,&fi);
    long pagesz=(long)vi.xres*vi.yres*(vi.bits_per_pixel/8);
    long off=(long)vi.yoffset*fi.line_length;
    unsigned char*m=mmap(0,fi.smem_len,PROT_READ,MAP_SHARED,fd,0);
    if(m==MAP_FAILED){perror("mmap");return 2;}
    FILE*f=fopen(out,"wb"); if(!f){perror("open out");return 3;} fwrite(m+off,1,pagesz,f); fclose(f);
    printf("shot %ldB %ux%u yoff=%u\n",pagesz,vi.xres,vi.yres,vi.yoffset);
    return 0;
}
