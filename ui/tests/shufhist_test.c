/* SPDX-License-Identifier: GPL-3.0-or-later */
/* host test for shufhist.c: gcc -I.. shufhist_test.c ../shufhist.c && ./a.out */
#include "../shufhist.h"
#include <stdio.h>
#include <string.h>
static int fails;
#define CHECK(c, m) do { if(c) printf("ok   %s\n", m); else { printf("FAIL %s\n", m); fails++; } } while(0)
static void play(int id){ char p[32]; snprintf(p, sizeof p, "/s/%d.flac", id); sh_observe(id, p); sh_observe(id, p); }
int main(void){
    int id; char p[64];
    sh_reset();
    CHECK(!sh_back(&id, p, sizeof p), "nothing to go back to at first");
    play(10); play(20); play(30);                                   /* 10 -> 20 -> 30 (shuffle next) */
    CHECK(sh_back(&id, p, sizeof p) && id == 20, "previous: the song that played before (20)");
    play(30);                                                       /* the old song reports once more: ignored */
    play(20);                                                       /* arrived */
    CHECK(sh_back(&id, p, sizeof p) && id == 10, "previous again: 10");
    CHECK(sh_forward(&id, p, sizeof p) && id == 20, "next while a jump is on its way: 20 (paced skips stack)");
    play(20);
    CHECK(sh_forward(&id, p, sizeof p) && id == 30, "next retraces forward: 30");
    play(30);
    CHECK(!sh_forward(&id, p, sizeof p), "back at the newest: next is the player's own shuffle");
    play(40);                                                       /* the player's next */
    CHECK(sh_back(&id, p, sizeof p) && id == 30, "previous from the new song: 30");
    play(30);
    play(50);                                                       /* another song started (not via history) */
    CHECK(sh_back(&id, p, sizeof p) && id == 30, "a new song after going back drops the forward branch (40)");
    play(30);
    CHECK(sh_forward(&id, p, sizeof p) && id == 50, "forward from 30 is 50, not 40");
    play(50);
    sh_reset(); for(int i = 1; i <= 70; i++) play(i);
    int k = 0; while(sh_back(&id, p, sizeof p)){ play(id); k++; }
    CHECK(k == SH_MAX - 1 && id == 70 - (SH_MAX - 1), "keeps the last 50");
    sh_reset(); play(1); play(2); sh_back(&id, p, sizeof p); sh_cancel(); play(2);
    CHECK(sh_back(&id, p, sizeof p) && id == 1, "a failed jump (cancel) leaves things as they were");
    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails != 0;
}
