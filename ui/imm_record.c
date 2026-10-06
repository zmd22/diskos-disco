/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "imm_record.h"
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
static int smoothing=1;
static imm_overlay_t ovl[IMM_MAX_OVERLAYS]; static int novl;
void imm_record_set_overlays(const imm_overlay_t *o,int n){
    if(n<0)n=0;
    if(n>IMM_MAX_OVERLAYS)n=IMM_MAX_OVERLAYS;
    for(int i=0;i<n;i++)ovl[i]=o[i];
    novl=n;
}
/* the progress ring: an arc of the 344 px circle (outer r 172, 3 px wide) from 208 deg clockwise over 124 deg,
 * exactly where the LVGL arc sat. Pixels + their position along the arc + edge coverage, built once. */
#define RING_MAXPX 3000
static uint32_t ring_idx[RING_MAXPX]; static uint16_t ring_pos[RING_MAXPX]; static uint8_t ring_cov[RING_MAXPX];
static int ring_n=-1, ring_on, ring_val; static uint32_t ring_trk, ring_ind;
static void ring_build(void){
    ring_n=0;
    for(int y=0;y<120 && ring_n<RING_MAXPX;y++)for(int x=0;x<360 && ring_n<RING_MAXPX;x++){
        double dx=x+0.5-180,dy=y+0.5-180,d=sqrt(dx*dx+dy*dy);
        double cov=fmin(d-168.5,172.5-d); if(cov<=0)continue; if(cov>1)cov=1;
        double a=atan2(dy,dx)*180.0/3.141592653589793; if(a<0)a+=360;
        double t=a-208; if(t<0)t+=360; if(t>124)continue;
        ring_idx[ring_n]=(uint32_t)(y*360+x); ring_pos[ring_n]=(uint16_t)(t*1000/124); ring_cov[ring_n]=(uint8_t)(cov*255); ring_n++;
    }
}
void imm_record_set_ring(int on,int permille,uint32_t trk,uint32_t ind){
    if(ring_n<0)ring_build();
    ring_on=on; ring_val=permille<0?0:permille>1000?1000:permille; ring_trk=trk; ring_ind=ind;
}
static inline uint32_t mix(uint32_t d,uint32_t c,unsigned a){        /* a 0..255 (scaled to 0..256: a shift, per channel) */
    a+=a>>7;
    uint32_t rb=(((d&0x00FF00FFu)*(256-a)+(c&0x00FF00FFu)*a)>>8)&0x00FF00FFu;
    uint32_t g=(((d&0x0000FF00u)*(256-a)+(c&0x0000FF00u)*a)>>8)&0x0000FF00u;
    return rb|g|0xFF000000u;
}
static void bake_extras(uint32_t *out){
    if(ring_on && ring_n>0)
        for(int i=0;i<ring_n;i++){ uint32_t c=ring_pos[i]<=ring_val?ring_ind:ring_trk; out[ring_idx[i]]=mix(out[ring_idx[i]],c,ring_cov[i]); }
    for(int k=0;k<novl;k++){
        const imm_overlay_t *o=&ovl[k]; if(!o->px)continue;
        for(int y=0;y<o->h;y++){
            int oy=o->y+y; if(oy<0||oy>=360)continue;
            const uint32_t *s=o->px+(size_t)y*o->stride; uint32_t *d=out+oy*360;
            for(int x=0;x<o->w;x++){
                uint32_t p=s[x]; unsigned a=p>>24; if(!a)continue;
                int ox=o->x+x; if(ox<0||ox>=360)continue;
                d[ox]=a==255?(p|0xFF000000u):mix(d[ox],p,a);
            }
        }
    }
}
void imm_record_set_smoothing(int enabled){smoothing=!!enabled;}
/* ---- the CD layer: per pixel, out = cover * K + T (K 0..256 = what is kept of the cover, T = the premultiplied
 * sheen colour), both with the lyric fade already applied. Built once per fade height (~0.7 MB, freed when off). */
static int cd_on, cd_fade=-1;
static uint16_t *cd_k; static uint32_t *cd_t;
void imm_record_set_cd(int on){
    cd_on=!!on; cd_fade=-1;
    if(!cd_on){ free(cd_k); free(cd_t); cd_k=NULL; cd_t=NULL; }
}
static const uint8_t IRI[7][3]={{255,90,160},{255,180,90},{255,240,106},{98,242,140},{74,216,255},{106,140,255},{179,107,255}};
static void iri(double t,double *rgb){
    t-=floor(t); double f=t*7; int i=(int)f; double u=f-i; const uint8_t *a=IRI[i%7],*b=IRI[(i+1)%7];
    for(int k=0;k<3;k++) rgb[k]=a[k]+(b[k]-a[k])*u;
}
#define CD_SPOKES 24
static void cd_build(const int *keep){
    if(!cd_k) cd_k=malloc(360*360*sizeof *cd_k);
    if(!cd_t) cd_t=malloc(360*360*sizeof *cd_t);
    if(!cd_k||!cd_t){ free(cd_k); free(cd_t); cd_k=NULL; cd_t=NULL; cd_on=0; return; }
    const double PI=3.141592653589793;
    for(int y=0;y<360;y++)for(int x=0;x<360;x++){
        double dx=x+0.5-180,dy=y+0.5-180,r=sqrt(dx*dx+dy*dy),ang=atan2(dy,dx);
        double rgb[3]={0,0,0},a=0;
        if(r<180){
            /* the sheen: a faint rainbow wash over the disc, stronger toward the rim */
            double rr=r/180.0, wash=0.10+0.20*rr*rr*rr;
            if(r>36){ iri(ang/(2*PI)*2.0,rgb); a=wash; }
            /* the spokes: thin rainbow lines from the hub to the rim, fading at both ends */
            if(r>38&&r<178){
                double ph=ang/(2*PI)*CD_SPOKES+0.5+0.13*CD_SPOKES/(2*PI); ph-=floor(ph);
                double dist=fabs(ph-0.5)*(2*PI*r/CD_SPOKES);              /* px from the nearest spoke */
                if(dist<1.6){
                    double t=(r-38)/140.0, env=pow(sin(PI*fmin(1,t*1.1)),0.8), sa=0.22*env*(1.6-dist)/1.6;
                    int k=(int)floor(ang/(2*PI)*CD_SPOKES+0.5); double sc[3]; iri((double)k/CD_SPOKES*2.0,sc);
                    double na=sa+a*(1-sa);
                    if(na>0) for(int c=0;c<3;c++) rgb[c]=(sc[c]*sa+rgb[c]*a*(1-sa))/na;
                    a=na;
                }
            }
        }
        int ia=(int)(a*256+0.5); if(ia>256) ia=256;
        int kp=keep[y];                                                  /* the lyric fade of this row (0..256) */
        cd_k[y*360+x]=(uint16_t)((256-ia)*kp>>8);
        uint32_t t=0;
        for(int c=0;c<3;c++){ int v=(int)(rgb[c]*ia/256.0*kp/256.0+0.5); if(v>255)v=255; t|=(uint32_t)v<<(16-8*c); }
        cd_t[y*360+x]=t;
    }
}
static inline uint32_t sample(const uint32_t *s,int w,int h,int x,int y){
    return (unsigned)x<(unsigned)w && (unsigned)y<(unsigned)h ? s[y*w+x] : 0;
}
static inline uint32_t lerp(uint32_t a,uint32_t b,unsigned f){
    uint32_t rb=(((a&0x00FF00FFu)*(256-f)+(b&0x00FF00FFu)*f)>>8)&0x00FF00FFu;
    uint32_t g=(((a&0x0000FF00u)*(256-f)+(b&0x0000FF00u)*f)>>8)&0x0000FF00u;
    return rb|g;
}
void imm_record_render(const uint32_t *src,int w,int h,uint32_t *out,int angle,int fade_height){
    static int first[360],last[360],keep[360],ready,cached_height;
    if(!ready){
        for(int y=0;y<360;y++){
            double dy=y-179.5,span=sqrt(180.0*180.0-dy*dy);
            first[y]=(int)ceil(179.5-span);last[y]=(int)floor(179.5+span);
            if(first[y]<0)first[y]=0;
            if(last[y]>359)last[y]=359;
        }
        ready=1;
    }
    if(fade_height<170)fade_height=170;
    if(cached_height!=fade_height){
        int top=360-fade_height;
        for(int y=0;y<360;y++){
            int alpha=y<=top?0:(y-top)*205*255/(fade_height*140);
            if(alpha>205)alpha=205;
            keep[y]=(255-alpha)*256/255;
        }
        cached_height=fade_height;
    }
    if(cd_on && cd_fade!=fade_height){ cd_build(keep); cd_fade=fade_height; }
    const int cd=cd_on && cd_k && cd_t;
    double a=angle*3.141592653589793/1800.0;
    int32_t c=(int32_t)lround(cos(a)*65536),sn=(int32_t)lround(sin(a)*65536);
    /* fork perf: the output disc (r=180) maps inside the square source, so the per-sample bounds tests of the old
     * loop never fired in practice; one cheap clamp per pixel keeps any source size safe. Output is identical. */
    const int lx=w-1,ly=h-1;
    for(int y=0;y<360;y++){
        int x0=first[y],x1=last[y],dy=y-180,m=keep[y];
        int32_t sx=(x0-180)*c+dy*sn+w*32768;
        int32_t sy=-(x0-180)*sn+dy*c+h*32768;
        uint32_t *o=out+y*360+x0;
        if(!smoothing){
            for(int x=x0;x<=x1;x++,sx+=c,sy-=sn){
                int xi=sx>>16,yi=sy>>16;
                if((unsigned)xi>(unsigned)lx||(unsigned)yi>(unsigned)ly){*o++=0xFF000000u;continue;}
                uint32_t p=src[yi*w+xi];
                if(cd){ int i=y*360+x; unsigned k=cd_k[i]; p=((((p&0x00FF00FFu)*k>>8)&0x00FF00FFu)|(((p&0x0000FF00u)*k>>8)&0x0000FF00u))+cd_t[i]; }
                else if(m!=256)p=(((p&0x00FF00FFu)*m>>8)&0x00FF00FFu)|(((p&0x0000FF00u)*m>>8)&0x0000FF00u);
                *o++=p|0xFF000000u;
            }
        } else {
            for(int x=x0;x<=x1;x++,sx+=c,sy-=sn){
                int xi=sx>>16,yi=sy>>16;unsigned fx=(sx>>8)&255,fy=(sy>>8)&255;
                uint32_t p;
                if((unsigned)xi<(unsigned)lx&&(unsigned)yi<(unsigned)ly){
                    const uint32_t *q=src+yi*w+xi;
                    p=lerp(lerp(q[0],q[1],fx),lerp(q[w],q[w+1],fx),fy);
                } else p=lerp(lerp(sample(src,w,h,xi,yi),sample(src,w,h,xi+1,yi),fx),
                             lerp(sample(src,w,h,xi,yi+1),sample(src,w,h,xi+1,yi+1),fx),fy);   /* the rim: as before */
                if(cd){ int i=y*360+x; unsigned k=cd_k[i]; p=((((p&0x00FF00FFu)*k>>8)&0x00FF00FFu)|(((p&0x0000FF00u)*k>>8)&0x0000FF00u))+cd_t[i]; }
                else if(m!=256)p=(((p&0x00FF00FFu)*m>>8)&0x00FF00FFu)|(((p&0x0000FF00u)*m>>8)&0x0000FF00u);
                *o++=p|0xFF000000u;
            }
        }
    }
    bake_extras(out);
}
