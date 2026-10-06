/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
/* mqcap [sendframe] [watch_secs] [label]
 * Sole-reader capture: drains /ui and prints each frame (timestamp + ascii),
 * optionally sends one candidate frame to /player first, then watches the
 * player's reaction on /ui. Kill stock mq_ui first so we own /ui.
 */
#include <mqueue.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>

static double now(void){ struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec + t.tv_nsec/1e9; }

int main(int argc, char **argv){
    const char *frame = argc>1 ? argv[1] : NULL;
    double secs = argc>2 ? atof(argv[2]) : 5.0;
    const char *label = argc>3 ? argv[3] : "";

    mqd_t ui = mq_open("/ui", O_RDONLY|O_NONBLOCK);
    if(ui==(mqd_t)-1){ perror("mq_open /ui"); return 2; }
    mqd_t pl = mq_open("/player", O_WRONLY|O_NONBLOCK);
    if(pl==(mqd_t)-1){ perror("mq_open /player"); return 2; }

    /* drain any stale frames first */
    char buf[8300]; int n;
    while((n=mq_receive(ui, buf, sizeof(buf)-1, NULL))>0){}

    if(frame){
        int r = mq_send(pl, frame, strlen(frame), 0);
        fprintf(stdout, "[%s] SENT '%s' rc=%d\n", label, frame, r); fflush(stdout);
    }

    double t0 = now();
    while(now()-t0 < secs){
        n = mq_receive(ui, buf, sizeof(buf)-1, NULL);
        if(n>0){
            buf[n]=0;
            /* print type/code header + a trimmed ascii payload */
            char hdr[9]; int h = n<8?n:8; memcpy(hdr,buf,h); hdr[h]=0;
            fprintf(stdout, "[%s +%.2f] len=%d hdr=%.8s | %.180s\n", label, now()-t0, n, hdr, buf);
            fflush(stdout);
        } else {
            usleep(15000);
        }
    }
    return 0;
}
