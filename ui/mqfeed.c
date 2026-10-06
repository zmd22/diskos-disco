/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include <mqueue.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
/* Inject synthetic a2 metadata + a1 position into /ui for UI testing without the real player.
   usage: mqfeed <title> <artist> <album> <path> <dur_ms> <pos_ms> [state] */
int main(int c,char**v){
    if(c<7){ printf("usage: mqfeed title artist album path dur_ms pos_ms [state]\n"); return 1; }
    mqd_t q=mq_open("/ui",O_WRONLY|O_NONBLOCK);
    if(q==(mqd_t)-1){ perror("mq_open /ui"); return 2; }
    int state = c>7 ? atoi(v[7]) : 1;
    int srate = c>8 ? atoi(v[8]) : 44100;   /* optional sample rate */
    char song[2048];
    snprintf(song,sizeof song,
      "{\"song_name\":\"%s\",\"song_artist_name\":\"%s\",\"song_album_name\":\"%s\",\"song_file_path\":\"%s\",\"song_duration_time\":%s,\"song_sample_rate\":%d,\"is_dsd\":false}",
      v[1],v[2],v[3],v[4],v[5],srate);
    char esc[4096]; int o=0;
    for(char*p=song;*p&&o<4090;p++){ if(*p=='"'||*p=='\\') esc[o++]='\\'; esc[o++]=*p; }
    esc[o]=0;
    char payload[5000];
    snprintf(payload,sizeof payload,
      "{\"song\":\"%s\",\"state\":%d,\"playing_num\":\"3/19\",\"work_mode\":\"LOCAL\"}",esc,state);
    char frame[5100];
    int total=8+(int)strlen(payload);
    snprintf(frame,sizeof frame,"a202%04X%s",total&0xFFFF,payload);
    int r1=mq_send(q,frame,strlen(frame),0);
    char a1[24]; snprintf(a1,sizeof a1,"a1030010%08lX",strtol(v[6],0,10));
    int r2=mq_send(q,a1,strlen(a1),0);
    printf("fed a2(len=%d rc=%d) a1(rc=%d)\n",total,r1,r2);
    return 0;
}
