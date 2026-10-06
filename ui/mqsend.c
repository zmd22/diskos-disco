/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include <mqueue.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
int main(int c,char**v){
    if(c<2){ printf("usage: mqsend <frame>\n"); return 1; }
    mqd_t q=mq_open("/player",O_WRONLY|O_NONBLOCK);
    if(q==(mqd_t)-1){ perror("mq_open /player"); return 2; }
    int r=mq_send(q,v[1],strlen(v[1]),0);
    printf("sent %s rc=%d\n",v[1],r);
    return 0;
}
