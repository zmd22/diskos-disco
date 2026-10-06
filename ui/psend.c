/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include <mqueue.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
int main(int argc, char**argv){
    if(argc<2){ fprintf(stderr,"usage: psend FRAME\n"); return 2; }
    mqd_t q = mq_open("/player", O_WRONLY);
    if(q==(mqd_t)-1){ perror("mq_open /player"); return 1; }
    if(mq_send(q, argv[1], strlen(argv[1]), 0)!=0){ perror("mq_send"); return 1; }
    printf("sent: %s\n", argv[1]);
    return 0;
}
