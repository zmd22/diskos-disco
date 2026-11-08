/* SPDX-License-Identifier: MIT
 * Original native chain-reaction game for diskOS. No Rockbox code is used.
 */
#define _POSIX_C_SOURCE 200809L
#include "game.h"
#include "fonts.h"
#if !BOOMSHINE_FONT_RLE
#error "fonts.h must be run-length encoded (ui/tools/boomshine_font_rle.py)"
#endif
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define W 360
#define H 360
#define BG 0x10151e
#define WHITE 0xf0f3f7
#define MUTED 0xa9b4c3
#define STEP (1.0/60.0)
#define FRAME_TIME (1.0/30.0)
static uint32_t frame[W*H];
static const uint32_t colors[2][6]={
    {0x23ffd7,0xff418c,0x9566ff,0xffe65a,0x45bfff,0xff9046},
    {0xbbe8e0,0xf0b7ce,0xc7bef1,0xf8dba6,0xb6d6f5,0xe5c4ee}
};
static int style, border_color;
#define palette (colors[style])
static double toast_until;
static double seconds(void);
static Game game;
static int mode,score,best,unlocked=1,rotation,dirty=1;
static unsigned screen_epoch;
static int save_error;
static char state_path[512];
static volatile sig_atomic_t quitting;
static const Font *font(int size){
    switch(size){case 14:return &font_14;case 18:return &font_18;case 22:return &font_22;case 30:return &font_30;default:return &font_40;}
}
static void pixel(int x,int y,uint32_t c,int alpha){
    if(x<0||y<0||x>=W||y>=H||alpha<=0)return;
    uint32_t *p=&frame[y*W+x];
    if(alpha>=255){*p=c;return;}
    uint32_t old=*p;
    unsigned r=(((c>>16)&255)*alpha+((old>>16)&255)*(255-alpha))/255;
    unsigned g=(((c>>8)&255)*alpha+((old>>8)&255)*(255-alpha))/255;
    unsigned b=((c&255)*alpha+(old&255)*(255-alpha))/255;
    *p=(r<<16)|(g<<8)|b;
}
static void circle(float cx,float cy,float radius,uint32_t c,int alpha,int arena,int outline){
    if(radius<=0)return;
    int y0=(int)floorf(cy-radius-1),y1=(int)ceilf(cy+radius+1);
    int x0=(int)floorf(cx-radius-1),x1=(int)ceilf(cx+radius+1);
    if(x0<0)x0=0;
    if(y0<0)y0=0;
    if(x1>=W)x1=W-1;
    if(y1>=H)y1=H-1;
    float r2=radius*radius,ir=radius-1.5f,inner=ir*ir;
    float inv=0.5f/radius,invinner=ir>0?0.5f/ir:0;
    for(int y=y0;y<=y1;y++){
        float row=(y-cy)*(y-cy);
        int skip=outline&&inner>row+2*radius?(int)sqrtf(inner-row-2*radius):0;
        for(int x=x0;x<=x1;x++){
            if(skip>1&&x>(int)cx-skip&&x<(int)cx+skip){x=(int)cx+skip-1;continue;}
            float dx=x-cx,d=dx*dx+row;
            float cover=(r2-d)*inv+0.5f;
            if(outline&&ir>0){float hole=(d-inner)*invinner+0.5f;if(hole<cover)cover=hole;}
            if(cover<=0)continue;
            if(cover>1)cover=1;
            if(arena){dx=x-ARENA_X;float dy=y-ARENA_Y;if(dx*dx+dy*dy>ARENA_R*ARENA_R)continue;}
            pixel(x,y,c,(int)(alpha*cover));
        }
    }
}
static int text_width(const char *s,int size){
    const Font *f=font(size);int width=0;
    while(*s){unsigned c=(unsigned char)*s++;if(c>=32&&c<127)width+=f->glyph[c-32].advance;}
    return width;
}
static void text_at(int x,int y,const char *s,int size,uint32_t c){
    const Font *f=font(size);
    while(*s){unsigned ch=(unsigned char)*s++;if(ch<32||ch>=127)continue;
        const Glyph *g=&f->glyph[ch-32];const unsigned char *p=f->pixels+g->off;
        for(int k=0,n=g->w*g->h;k<n;){   /* run-length glyphs (fonts.h): a value draws one pixel, 0 n skips n clear ones */
            if(*p){pixel(x+k%g->w,y+k/g->w,c,*p++);k++;}
            else{k+=p[1];p+=2;}
        }
        x+=g->advance;
    }
}
static void text(int y,const char *s,int size,uint32_t c){text_at((W-text_width(s,size))/2,y,s,size,c);}
static void pill(int cx,int cy,int width,int height,uint32_t c){
    int r=height/2,left=cx-width/2,right=cx+width/2;
    for(int y=cy-r;y<=cy+r;y++)for(int x=left+r;x<=right-r;x++)pixel(x,y,c,255);
    circle(left+r,cy,r,c,255,0,0);circle(right-r,cy,r,c,255,0,0);
}
static void button(int y,const char *label,int primary){
    pill(180,y,208,46,primary?palette[0]:0x27313f);
    text(y-15,label,18,primary?BG:WHITE);
}
static uint32_t now_seed(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint32_t)(t.tv_nsec^t.tv_sec);}
static void start_level(int level){
    uint32_t seed=now_seed();game_init(&game,level,seed);
    border_color=(int)(seed%6);mode=1;dirty=1;screen_epoch++;
}
static void home(void){mode=0;toast_until=0;dirty=1;screen_epoch++;}
static void save_state(void){
    char temp[540];snprintf(temp,sizeof temp,"%s.tmp",state_path);
    FILE *f=fopen(temp,"w");if(!f){save_error=1;return;}
    int ok=fprintf(f,"version=1\nbest=%d\nunlocked=%d\n",best,unlocked)>0;
    if(fflush(f)!=0)ok=0;
    if(ok&&fsync(fileno(f))!=0)ok=0;
    if(fclose(f)!=0)ok=0;
    if(ok&&rename(temp,state_path)==0)save_error=0;
    else {unlink(temp);save_error=1;}
}
static void load_state(const char *argv0){
#ifdef BOOMSHINE_BUILTIN
    /* diskOS Disco build: the game runs from mq_ui itself (mq_ui --boomshine), so scores live in their own folder.
     * A best score from the separate 1.x app is carried over once. */
    (void)argv0;
    mkdir("/usr/data/boomshine",0755);
    snprintf(state_path,sizeof state_path,"/usr/data/boomshine/scores.conf");
    if(access(state_path,F_OK)!=0){
        FILE *in=fopen("/usr/data/apps/boomshine/scores.conf","r"),*out=in?fopen(state_path,"w"):NULL;
        if(in&&out){char line[128];while(fgets(line,sizeof line,in))fputs(line,out);}
        if(out)fclose(out);
        if(in)fclose(in);
    }
#else
    ssize_t n=readlink("/proc/self/exe",state_path,sizeof state_path-1);
    if(n>0)state_path[n]=0;else snprintf(state_path,sizeof state_path,"%s",argv0);
    char *slash=strrchr(state_path,'/');
    if(slash)snprintf(slash+1,(size_t)(state_path+sizeof state_path-slash-1),"scores.conf");
    else snprintf(state_path,sizeof state_path,"scores.conf");
#endif
    FILE *f=fopen(state_path,"r");if(f){char line[128];while(fgets(line,sizeof line,f)){
        int v;if(sscanf(line,"best=%d",&v)==1&&v>=0&&v<=1000000)best=v;
        if(sscanf(line,"unlocked=%d",&v)==1&&v>=1&&v<=LEVELS)unlocked=v;
    }fclose(f);}
    f=fopen("/usr/data/diskos.conf","r");if(f){char line[256];while(fgets(line,sizeof line,f)){
        int v;if(sscanf(line,"screen_rot=%d",&v)==1&&v>=0&&v<4)rotation=v;
    }fclose(f);}
}
static uint32_t background(void){return style?0x15223b:0x000000;}
/* Small alpha-only stamps preserve quarter-pixel motion without recalculating
 * circles for every dot and every steady-state burst. Total cache < 140 KiB. */
#define DOT_SIDE 19
#define BURST_SIDE 89
static unsigned char dot_alpha[16][DOT_SIDE*DOT_SIDE];
static unsigned char dot_light[16][DOT_SIDE*DOT_SIDE];
static unsigned char burst_alpha[16][BURST_SIDE*BURST_SIDE];
static short arena_left[H],arena_right[H];
static int stamp_style=-1,stamp_ready;
static float stamp_radius;
static int coverage(float dx,float dy,float radius,int alpha,int outline){
    float d=dx*dx+dy*dy,v=(radius*radius-d)*(0.5f/radius)+0.5f;
    if(outline){float r=radius-1.5f,h=(d-r*r)*(0.5f/r)+0.5f;if(h<v)v=h;}
    if(v<=0)return 0;
    if(v>1)v=1;
    return (int)(alpha*v);
}
static int over_alpha(int under,int over){return over+(under*(255-over)+127)/255;}
static void init_stamps(void){
    if(!stamp_ready){
        for(int y=0;y<H;y++){
            float span=ARENA_R*ARENA_R-(y-ARENA_Y)*(y-ARENA_Y);
            if(span<0){arena_left[y]=W;arena_right[y]=-1;}
            else {int r=(int)sqrtf(span);arena_left[y]=(int)ARENA_X-r;arena_right[y]=(int)ARENA_X+r;}
        }
        for(int sub=0;sub<16;sub++)for(int y=0;y<DOT_SIDE;y++)for(int x=0;x<DOT_SIDE;x++){
            float dx=x-DOT_SIDE/2-(sub%4)*0.25f,dy=y-DOT_SIDE/2-(sub/4)*0.25f;
            int a=coverage(dx,dy,8,20,0);
            a=over_alpha(a,coverage(dx,dy,6,45,0));
            a=over_alpha(a,coverage(dx,dy,4,255,0));
            dot_alpha[sub][y*DOT_SIDE+x]=(unsigned char)a;
            dot_light[sub][y*DOT_SIDE+x]=(unsigned char)coverage(dx+1,dy+1,1,180,0);
        }
        stamp_ready=1;
    }
    float radius=game_blast_radius(&game,0.8f);
    if(stamp_style==style&&stamp_radius==radius)return;
    for(int sub=0;sub<16;sub++)for(int y=0;y<BURST_SIDE;y++)for(int x=0;x<BURST_SIDE;x++){
        float dx=x-BURST_SIDE/2-(sub%4)*0.25f,dy=y-BURST_SIDE/2-(sub/4)*0.25f;
        int a;
        if(style){float q=1-(dx*dx+dy*dy)/(radius*radius);a=q>0?(int)(155*q*q):0;}
        else {
            a=coverage(dx,dy,radius,24,0);
            a=over_alpha(a,coverage(dx,dy,radius,75,1));
            a=over_alpha(a,coverage(dx,dy,radius-1.5f,95,1));
            a=over_alpha(a,coverage(dx,dy,radius-3,230,1));
        }
        burst_alpha[sub][y*BURST_SIDE+x]=(unsigned char)over_alpha(a,coverage(dx,dy,3,255,0));
    }
    stamp_style=style;stamp_radius=radius;
}
static void stamp(float x,float y,uint32_t color,int is_burst){
    int qx=(int)floorf(x*4+0.5f),qy=(int)floorf(y*4+0.5f);
    int sub=(qy&3)*4+(qx&3),side=is_burst?BURST_SIDE:DOT_SIDE;
    int left=(qx>>2)-side/2,top=(qy>>2)-side/2;
    const unsigned char *alpha=is_burst?burst_alpha[sub]:dot_alpha[sub];
    for(int row=0;row<side;row++){
        int py=top+row;if(py<0||py>=H)continue;
        int lo=left>arena_left[py]?left:arena_left[py];
        int hi=left+side-1<arena_right[py]?left+side-1:arena_right[py];
        for(int px=lo;px<=hi;px++){
            int i=row*side+px-left;
            pixel(px,py,color,alpha[i]);
            if(!is_burst)pixel(px,py,WHITE,dot_light[sub][i]);
        }
    }
}

static void burst(float x,float y,float r,uint32_t c){
    if(r<=0)return;
    if(r==stamp_radius){stamp(x,y,c,1);return;}
    if(style){
        int left=(int)(x-r),right=(int)(x+r),top=(int)(y-r),bottom=(int)(y+r);
        float inv=1/(r*r);
        for(int py=top;py<=bottom;py++)for(int px=left;px<=right;px++){
            float dx=px-x,dy=py-y,q=1-(dx*dx+dy*dy)*inv;
            float ax=px-ARENA_X,ay=py-ARENA_Y;
            if(q>0&&ax*ax+ay*ay<=ARENA_R*ARENA_R)pixel(px,py,c,(int)(155*q*q));
        }
    }else{
        circle(x,y,r,c,24,1,0);
        circle(x,y,r,c,75,1,1);circle(x,y,r-1.5f,c,95,1,1);
        circle(x,y,r-3,c,230,1,1);
    }
    circle(x,y,3,c,255,1,0);
}
static void draw_arena(void){
    init_stamps();
    for(int i=0;i<game.count;i++){
        Dot *d=&game.dots[i];if(d->state==2)continue;
        uint32_t c=palette[d->color];
        if(d->state==1)burst(d->x,d->y,game_blast_radius(&game,d->age),c);
        else stamp(d->x,d->y,c,0);
    }
    if(game.fired)burst(game.seed_x,game.seed_y,game_blast_radius(&game,game.seed_age),WHITE);
    uint32_t c=palette[border_color];
    circle(180,180,179,c,220,0,1);
    circle(180,180,177.5f,c,style?90:120,0,1);
    circle(180,180,175.5f,c,35,0,1);
}
static void render(void){
    for(int i=0;i<W*H;i++)frame[i]=mode?background():0x0b1020;
    char buf[80];
    if(mode==0){
        circle(180,66,28,colors[0][0],30,0,0);
        circle(161,65,24,colors[0][0],180,0,1);
        circle(193,54,19,colors[0][1],210,0,1);
        circle(190,82,15,colors[1][2],100,0,0);
        text(113,"Boomshine",30,WHITE);
        text(151,"One tap. A chain reaction.",18,0xc9d1df);
        pill(180,210,234,54,colors[0][0]);pill(180,210,230,50,0x071f25);
        text(190,"NeoN",30,colors[0][0]);
        pill(180,273,234,54,colors[1][2]);
        text(253,"Pop!",30,0x252743);
        text(311,"Exit",22,WHITE);
    }else if(mode==1){
        draw_arena();
        snprintf(buf,sizeof buf,"%d / %d",game.caught,game.target);
        /* Small backplates keep the two labels legible over every burst. */
        pill(180,31,text_width(buf,22)+22,32,background());
        text(15,buf,22,WHITE);
        snprintf(buf,sizeof buf,"%d",game.level+1);
        pill(180,331,48,30,background());text(315,buf,22,WHITE);
        if(toast_until>seconds()){
            pill(180,262,268,66,style?0x33445f:0x17252c);
            text(237,"Long-press screen to exit",18,WHITE);
            text(264,"Returns to the landing page",14,MUTED);
        }
    }else{
        int complete=game.won&&game.level==LEVELS-1;
        circle(180,180,178,palette[border_color],120,0,1);
        snprintf(buf,sizeof buf,"LEVEL %d",game.level+1);text(54,buf,14,MUTED);
        text(87,complete?"Beautiful!":game.won?"Chain complete":"Almost...",30,WHITE);
        snprintf(buf,sizeof buf,"%d / %d",game.caught,game.target);text(139,buf,40,palette[0]);
        text(192,game.won?"Target reached":"Give it another try",18,MUTED);
        button(251,complete?"Play again":game.won?"Next level":"Retry level",1);
        snprintf(buf,sizeof buf,"SCORE %d  /  BEST %d",score,best);
        text(282,save_error?"Score could not be saved":buf,14,MUTED);
        text(314,"Home",18,WHITE);
    }
}
static int in_button(int x,int y,int cy){return x>=76&&x<=284&&abs(y-cy)<=23;}
static void tap(int x,int y){
    dirty=1;
    if(mode==0){
        if(x>=63&&x<=297&&(abs(y-210)<=27||abs(y-273)<=27)){
            style=y>240;score=0;start_level(0);toast_until=seconds()+3.5;
        }else if(x>=120&&x<=240&&y>=310&&y<=354)quitting=1;
        return;
    }
    if(mode==2){
        if(in_button(x,y,251)){
            if(game.won){if(game.level==LEVELS-1){score=0;start_level(0);}else start_level(game.level+1);}
            else start_level(game.level);
        }else if(x>=120&&x<=240&&y>=307&&y<=354)home();
        return;
    }
    game_fire(&game,(float)x,(float)y);
}
static void finish_round(void){
    mode=2;dirty=1;screen_epoch++;
    if(game.won){score+=game.caught*10+(game.level+1)*25;
        if(score>best)best=score;
        int next=game.level+2;if(next>LEVELS)next=LEVELS;if(next>unlocked)unlocked=next;
        save_state();
    }
}
#ifdef BOOMSHINE_TEST
static void to_raw(int x,int y,int *rx,int *ry){
    switch(rotation){case 1:*rx=y;*ry=W-1-x;break;case 2:*rx=x;*ry=y;break;case 3:*rx=W-1-y;*ry=x;break;default:*rx=W-1-x;*ry=H-1-y;break;}
}
#endif
static void from_raw(int rx,int ry,int *x,int *y){
    switch(rotation){case 1:*x=W-1-ry;*y=rx;break;case 2:*x=rx;*y=ry;break;case 3:*x=ry;*y=W-1-rx;break;default:*x=W-1-rx;*y=H-1-ry;break;}
}
static int write_ppm(const char *path){
    FILE *f=fopen(path,"wb");if(!f)return -1;fprintf(f,"P6\n360 360\n255\n");
    for(int i=0;i<W*H;i++){unsigned char p[3]={frame[i]>>16,frame[i]>>8,frame[i]};if(fwrite(p,1,3,f)!=3){fclose(f);return -1;}}
    return fclose(f);
}
static void signal_exit(int s){(void)s;quitting=1;}
typedef struct {int fd;unsigned char *map;struct fb_var_screeninfo v;struct fb_fix_screeninfo f;size_t offset;} Display;
static int open_display(Display *d){
    memset(d,0,sizeof *d);d->fd=open("/dev/fb0",O_RDWR|O_CLOEXEC);if(d->fd<0)return -1;
    if(ioctl(d->fd,FBIOGET_VSCREENINFO,&d->v)<0||ioctl(d->fd,FBIOGET_FSCREENINFO,&d->f)<0)goto fail;
    if(d->v.xres!=W||d->v.yres!=H||d->v.bits_per_pixel!=32||d->f.line_length<W*4||d->f.line_length%4||
       d->v.red.length!=8||d->v.green.length!=8||d->v.blue.length!=8||
       d->v.red.offset>24||d->v.green.offset>24||d->v.blue.offset>24){errno=ENOTSUP;goto fail;}
    /* Preserve the visible page; the parent resumes rendering into that page. */
    d->offset=(size_t)d->v.yoffset*d->f.line_length+(size_t)d->v.xoffset*4;
    if(d->offset+(H-1)*(size_t)d->f.line_length+W*4>d->f.smem_len){errno=EINVAL;goto fail;}
    d->map=mmap(NULL,d->f.smem_len,PROT_READ|PROT_WRITE,MAP_SHARED,d->fd,0);
    if(d->map==MAP_FAILED)goto fail;
    fprintf(stderr,"display=%ux%u bpp=%u stride=%u offset=%zu\n",d->v.xres,d->v.yres,d->v.bits_per_pixel,d->f.line_length,d->offset);
    return 0;
fail:close(d->fd);d->fd=-1;return -1;
}
static void present(Display *d){
    /* Walk destination rows contiguously. Rotation and format are decided once,
     * rather than branching and repacking all 129,600 pixels every frame. */
    int first,step,rowstep;
    switch(rotation){
        case 1:first=W-1;step=W;rowstep=-1;break;
        case 2:first=0;step=1;rowstep=W;break;
        case 3:first=(H-1)*W;step=-W;rowstep=1;break;
        default:first=W*H-1;step=-1;rowstep=-W;break;
    }
    int native=d->v.red.offset==16&&d->v.green.offset==8&&d->v.blue.offset==0;
    for(int y=0;y<H;y++,first+=rowstep){
        uint32_t *dst=(uint32_t *)(d->map+d->offset+(size_t)y*d->f.line_length);
        if(native&&step==1){memcpy(dst,frame+first,W*4);continue;}
        if(native){for(int x=0,i=first;x<W;x++,i+=step)dst[x]=frame[i];}
        else for(int x=0,i=first;x<W;x++,i+=step){
            uint32_t c=frame[i];
            dst[x]=(((c>>16)&255)<<d->v.red.offset)|(((c>>8)&255)<<d->v.green.offset)|((c&255)<<d->v.blue.offset);
        }
    }
}

typedef struct {
    int rawx,rawy,x0,y0,x,y,down,was_down,validx,validy,moved,dropped,slot,consumed;
    int xmin,xmax,ymin,ymax,mt;
    double pressed;
    unsigned epoch;
} Touch;
static int scale_axis(int value,int lo,int hi){
    if(hi<=lo){lo=0;hi=359;}
    if(value<lo)value=lo;
    if(value>hi)value=hi;
    return (int)(((int64_t)value-lo)*359/((int64_t)hi-lo));
}
static int open_touch(Touch *t){
    for(int i=0;i<32;i++){
        char path[64];snprintf(path,sizeof path,"/dev/input/event%d",i);
        int fd=open(path,O_RDONLY|O_NONBLOCK|O_CLOEXEC);if(fd<0)continue;
        struct input_absinfo ax,ay;int mt=1;
        if(ioctl(fd,EVIOCGABS(ABS_MT_POSITION_X),&ax)<0||ioctl(fd,EVIOCGABS(ABS_MT_POSITION_Y),&ay)<0){
            mt=0;
            if(ioctl(fd,EVIOCGABS(ABS_X),&ax)<0||ioctl(fd,EVIOCGABS(ABS_Y),&ay)<0){close(fd);continue;}
        }
        if(ax.maximum<=ax.minimum||ay.maximum<=ay.minimum){close(fd);continue;}
        t->xmin=ax.minimum;t->xmax=ax.maximum;t->ymin=ay.minimum;t->ymax=ay.maximum;t->mt=mt;
        fprintf(stderr,"touch=%s protocol=%s x=%d..%d y=%d..%d\n",path,mt?"MT":"ABS",t->xmin,t->xmax,t->ymin,t->ymax);
        return fd;
    }
    errno=ENODEV;return -1;
}
static void hold_touch(Touch *t,double now){
    if(t->down&&t->was_down&&!t->moved&&!t->consumed&&t->validx&&t->validy&&now-t->pressed>=0.85){
        t->consumed=1;
        if(mode!=0)home();
    }
}
/* SYN_DROPPED can include the release itself. Query the current kernel state
 * at synchronization so a lost lift cannot leave touch permanently blocked. */
static int resync_touch(int fd,Touch *t){
    int down,rx=t->rawx,ry=t->rawy;
    if(t->mt){
        int tracking[2]={ABS_MT_TRACKING_ID,-1},ax[2]={ABS_MT_POSITION_X,0},ay[2]={ABS_MT_POSITION_Y,0};
        if(ioctl(fd,EVIOCGMTSLOTS(sizeof tracking),tracking)<0||
           ioctl(fd,EVIOCGMTSLOTS(sizeof ax),ax)<0||ioctl(fd,EVIOCGMTSLOTS(sizeof ay),ay)<0)return -1;
        down=tracking[1]>=0;rx=ax[1];ry=ay[1];
    }else{
        unsigned char keys[(KEY_MAX+8)/8]={0};struct input_absinfo ax,ay;
        if(ioctl(fd,EVIOCGKEY(sizeof keys),keys)<0||ioctl(fd,EVIOCGABS(ABS_X),&ax)<0||ioctl(fd,EVIOCGABS(ABS_Y),&ay)<0)return -1;
        down=(keys[BTN_TOUCH/8]>>(BTN_TOUCH%8))&1;rx=ax.value;ry=ay.value;
    }
    t->rawx=rx;t->rawy=ry;t->validx=t->validy=1;t->slot=0;t->dropped=0;
    t->down=t->was_down=down;t->consumed=1;t->moved=0;t->pressed=seconds();t->epoch=screen_epoch;
    from_raw(scale_axis(rx,t->xmin,t->xmax),scale_axis(ry,t->ymin,t->ymax),&t->x,&t->y);
    t->x0=t->x;t->y0=t->y;
    return 0;
}
static void read_touch(int fd,Touch *t){
    struct input_event ev[32];ssize_t bytes;
    for(int batch=0;batch<8&&(bytes=read(fd,ev,sizeof ev))>0;batch++)for(size_t i=0;i<(size_t)bytes/sizeof ev[0];i++){
        struct input_event *e=&ev[i];
        if(e->type==EV_SYN&&e->code==SYN_DROPPED){
            t->dropped=1;t->consumed=1;t->down=t->was_down=0;continue;
        }
        /* A lost packet must never become an accidental shot. */
        if(t->dropped){
            if(e->type==EV_SYN&&e->code==SYN_REPORT){
                if(resync_touch(fd,t)<0)t->dropped=2;
                continue;
            }
            /* The kernel requires discarding the entire lost packet through
             * SYN_REPORT. If state-query ioctls are unavailable, use the next
             * complete release as a conservative fallback. */
            if(t->dropped==1)continue;
            if(e->type==EV_ABS&&e->code==ABS_MT_SLOT){t->slot=e->value;continue;}
            if(e->type==EV_ABS&&t->slot==0){
                if(e->code==(t->mt?ABS_MT_POSITION_X:ABS_X)){t->rawx=e->value;t->validx=1;}
                if(e->code==(t->mt?ABS_MT_POSITION_Y:ABS_Y)){t->rawy=e->value;t->validy=1;}
                if(t->mt&&e->code==ABS_MT_TRACKING_ID&&e->value<0)t->dropped=0;
            }else if(!t->mt&&e->type==EV_KEY&&e->code==BTN_TOUCH&&e->value==0)t->dropped=0;
            continue;
        }
        if(e->type==EV_ABS&&e->code==ABS_MT_SLOT){t->slot=e->value;continue;}
        if(e->type==EV_SYN&&e->code==SYN_REPORT){
            if(t->validx&&t->validy){
                from_raw(scale_axis(t->rawx,t->xmin,t->xmax),scale_axis(t->rawy,t->ymin,t->ymax),&t->x,&t->y);
                if(t->down&&!t->was_down){t->x0=t->x;t->y0=t->y;t->moved=0;t->consumed=0;t->pressed=seconds();t->epoch=screen_epoch;}
                if((t->down||t->was_down)&&(abs(t->x-t->x0)>14||abs(t->y-t->y0)>14))t->moved=1;
                if(!t->down&&t->was_down&&!t->moved&&!t->consumed){
                    if(seconds()-t->pressed>=0.85){if(mode!=0)home();}
                    else if(t->epoch==screen_epoch)tap(t->x0,t->y0);
                }
                t->was_down=t->down;
            }
            continue;
        }
        if(e->type==EV_ABS&&t->slot==0){
            if(e->code==(t->mt?ABS_MT_POSITION_X:ABS_X)){t->rawx=e->value;t->validx=1;}
            if(e->code==(t->mt?ABS_MT_POSITION_Y:ABS_Y)){t->rawy=e->value;t->validy=1;}
            if(e->code==ABS_MT_TRACKING_ID)t->down=e->value>=0;
        }else if(e->type==EV_KEY&&e->code==BTN_TOUCH&&!t->mt)t->down=e->value!=0;
    }
}
static double seconds(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9;}
int main(int argc,char **argv){
    if(argc==4&&!strcmp(argv[1],"--render")){
        if(!strcmp(argv[2],"home")){best=2450;unlocked=8;}
        else if(!strcmp(argv[2],"neon")||!strcmp(argv[2],"pop")||!strcmp(argv[2],"toast")){
            style=!strcmp(argv[2],"pop");start_level(7);game_init(&game,7,8173);border_color=style?2:0;
            game_fire(&game,180,180);
            for(int i=0;i<game.count;i++)if(i%3==0){game.dots[i].state=1;game.dots[i].age=0.25f+(i%5)*0.15f;game.caught++;}
            game.seed_age=0.8f;
            if(!strcmp(argv[2],"toast"))toast_until=seconds()+10;
        }else if(!strcmp(argv[2],"result")){start_level(4);mode=2;game.caught=13;game.won=1;score=810;best=2450;}
        else return 2;
        render();return write_ppm(argv[3])?1:0;
    }
    if(argc!=1){fprintf(stderr,"Usage: %s [--render home|neon|pop|toast|result output.ppm]\n",argv[0]);return 2;}
    load_state(argv[0]);signal(SIGINT,signal_exit);signal(SIGTERM,signal_exit);signal(SIGHUP,signal_exit);
    Display d;if(open_display(&d)<0){perror("Boomshine: display");return 1;}
    Touch touch={0};int touchfd=open_touch(&touch);
    if(touchfd<0){perror("Boomshine: touch");munmap(d.map,d.f.smem_len);close(d.fd);return 1;}
    double last=seconds(),accum=0,nextdraw=last;
    fprintf(stderr,"Boomshine 1.2.0 ready; rotation=%d\n",rotation);
    while(!quitting){
        unsigned epoch=screen_epoch;
        double before=seconds(),deadline=before+0.1;
        if(mode==1){
            deadline=last+STEP-accum;
            if(nextdraw<deadline)deadline=nextdraw;
        }else if(dirty)deadline=before;
        if(touch.down&&!touch.moved&&!touch.consumed&&touch.pressed+0.85<deadline)deadline=touch.pressed+0.85;
        int timeout=(int)ceil((deadline-before)*1000);if(timeout<0)timeout=0;
        struct pollfd p={.fd=touchfd,.events=POLLIN};int r=poll(&p,1,timeout);
        if(r<0&&errno!=EINTR){perror("Boomshine: poll");break;}
        if(r>0&&(p.revents&(POLLERR|POLLHUP|POLLNVAL))){fprintf(stderr,"Boomshine: touch device disconnected\n");break;}
        if(r>0&&(p.revents&POLLIN))read_touch(touchfd,&touch);
        double now=seconds(),dt=now-last;last=now;if(dt>0.1)dt=0.1;
        hold_touch(&touch,now);
        /* No old-screen elapsed time is carried into a newly started level. */
        if(epoch!=screen_epoch){dt=0;accum=0;nextdraw=now;}
        if(mode==1){
            accum+=dt;
            while(accum>=STEP){
                game_step(&game,(float)STEP);accum-=STEP;
                if(game.finished){finish_round();break;}
            }
            dirty=1;
        }else accum=0;
        if(dirty&&(mode!=1||now>=nextdraw)){
            render();present(&d);dirty=0;
            /* Keep the 30 Hz deadline independent of drawing time. Skip missed
             * deadlines after a stall rather than producing catch-up frames. */
            double done=seconds();
            nextdraw+=FRAME_TIME;
            if(nextdraw<=done)nextdraw=done+FRAME_TIME;
        }
    }
    close(touchfd);munmap(d.map,d.f.smem_len);close(d.fd);return 0;
}
