/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Charge-cycle rules (battcycle.c) on synthetic charges shaped like the Disc's real ones, and optionally a replay of a
 * real /usr/data/usage.bin:  ./battcycle_test [usage.bin]   (the replay prints every session it finds) */
#include "../battcycle.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static bcycle_t c; static uint32_t now; static int events;
static int feed(int pct, int chg, int mv){
    bc_reading_t r = { now, pct, chg, mv, 0 }; int ev = bc_step(&c, &r); events |= ev; now += 60; return ev;
}
/* the measured shape: current reported up to 86 %, then 1 %/min with no current, voltage near the charger's */
static void charge(int from, int to){
    for(int p = from; p <= to; p++) feed(p, p <= 86, p <= 86 ? 4000 + p * 4 : 4340);
}
static void reset(void){ bc_init(&c); now = 1793000000u; events = 0; }

static void synthetic(void){
    /* 1. a normal charge to 100 %, ten minutes on the charger, unplugged, used: one full cycle, dated at the unplug */
    reset(); for(int i = 0; i < 5; i++) feed(60, 0, 3900);
    charge(60, 100);
    for(int i = 0; i < 10; i++) feed(100, 0, 4345);
    uint32_t unplug = now; events = 0;
    for(int i = 0; i < 20; i++) feed(100, 0, 4210);
    assert(!(events & BC_EV_END_FULL));            /* still at 100: nothing to tell yet */
    feed(99, 0, 4190);
    assert(events & BC_EV_END_FULL && !c.pwr);
    assert(c.power_at < unplug && unplug - c.power_at <= 60);   /* the last minute on the charger */
    puts("PASS full charge to 100 %, restart dated at the unplug, not at the first drop");

    /* 2. the charger stops by itself at 92 %: still a full charge */
    reset(); for(int i = 0; i < 5; i++) feed(55, 0, 3880);
    charge(55, 92);
    for(int i = 0; i < 20; i++) feed(92, 0, 4330);
    assert(c.full_seen);
    events = 0; feed(92, 0, 4180); feed(91, 0, 4170);
    assert(events & BC_EV_END_FULL);
    puts("PASS a charge that stops by itself at 92 % counts as full");

    /* 3. unplugged mid-charge at 85 % and left idle at 85 %: not a full charge */
    reset(); for(int i = 0; i < 5; i++) feed(40, 0, 3800);
    charge(40, 85);
    for(int i = 0; i < 40; i++) feed(85, 0, 4120);   /* off the charger: the voltage sags at once */
    feed(84, 0, 4100);
    assert(!c.full_seen && (events & BC_EV_END) && !(events & BC_EV_END_FULL));
    puts("PASS unplugged at 85 % mid-charge: a session, not a full charge");

    /* 4. on the charger at 100 % the gauge dips to 99 and the charger tops it up: still one session */
    reset(); charge(70, 100);
    feed(99, 0, 4335); feed(99, 0, 4338); feed(100, 0, 4345);
    assert(c.pwr && c.full_seen);
    puts("PASS a top-up dip on the charger does not end the session");

    /* 5. a lone +1 % blip on battery is not a charger */
    reset(); feed(91, 0, 4050); feed(92, 0, 4060); feed(92, 0, 4050); feed(91, 0, 4040);
    assert(!c.pwr && !events);
    puts("PASS a +1 % blip on battery starts nothing");

    /* 6. the Disc was off while charging (the level jumped 30 points), then unplugged */
    reset(); bc_reading_t r = { now, 99, 0, 4200, 0 };
    int ev = bc_resume(&c, &r, 69, 0); now += 60;
    assert(ev & BC_EV_START && ev & BC_EV_FULL);
    events = 0; feed(98, 0, 4180); assert(events & BC_EV_END_FULL);
    puts("PASS a charge made while switched off is noticed");

    /* 7. Charging Limit: held at 80 % on the charger, then unplugged */
    reset(); for(int p = 50; p <= 80; p++){ bc_reading_t q = { now, p, 1, 4100, 1 }; events |= bc_step(&c, &q); now += 60; }
    for(int i = 0; i < 30; i++){ bc_reading_t q = { now, 80, 0, 4080, 1 }; events |= bc_step(&c, &q); now += 60; }
    { bc_reading_t q = { now, 79, 0, 4000, 1 }; events |= bc_step(&c, &q); }
    assert(events & BC_EV_END_FULL);
    puts("PASS Charging Limit: 80 % counts as full");

    /* 9. the measured unplug at 99 %: 4.345 V held, then 4.26 V on battery with the level still 99 for a long time.
     * Off power within BC_RECENT_S (idle power-off must work again), though the session only ends when it falls. */
    reset(); charge(70, 99);
    for(int i = 0; i < 6; i++) feed(99, 0, 4345);
    assert(bc_on_power(&c, now - 60, 0) && c.full_seen);
    for(int i = 0; i < 4; i++) feed(99, 0, 4262);
    assert(c.pwr && !bc_on_power(&c, now - 60, 0));
    events = 0; feed(98, 0, 4240); assert(events & BC_EV_END_FULL);
    puts("PASS unplugged at 99 %: off power in minutes, not when the level first falls");

    /* 8. an old USC1 record loads with its totals */
    uint32_t old[5] = { BC_MAGIC1, 1793409044u, 538, 593, 0 };
    bcycle_t l; assert(bc_load(&l, old, sizeof old) && l.full_at == 1793409044u && l.play_min == 538 && !l.pwr && l.low == BC_NONE);
    bcycle_t w; bc_init(&w); w.full_at = 5; w.pwr = 1; w.peak = 93; bcycle_t r2;
    assert(bc_load(&r2, &w, sizeof w) && r2.pwr == 1 && r2.peak == 93 && r2.magic == BC_MAGIC1);
    puts("PASS USC1 totals carry over, and the full record keeps the old start for a downgrade");
}

static void replay(const char *path){
    FILE *f = fopen(path, "rb"); if(!f){ perror(path); return; }
    uint32_t hdr[2]; if(fread(hdr, sizeof hdr, 1, f) != 1){ fclose(f); return; }
    struct { uint32_t t; uint8_t pct, fl; uint16_t mv; } s; uint32_t prev_t = 0; int prev_pct = -1, sessions = 0, fulls = 0;
    bc_init(&c);
    while(fread(&s, sizeof s, 1, f) == 1){
        bc_reading_t r = { s.t, s.pct, s.fl & 1, s.mv, 0 }; int ev;
        if(prev_t && s.t - prev_t > 5 * 60) ev = bc_resume(&c, &r, prev_pct, 0); else ev = bc_step(&c, &r);
        char tb[32]; time_t tt = s.t; strftime(tb, sizeof tb, "%a %d %H:%M", localtime(&tt));
        if(ev & BC_EV_START){ sessions++; printf("  %s %3d%%  session starts\n", tb, s.pct); }
        if(ev & BC_EV_FULL) printf("  %s %3d%%  full\n", tb, s.pct);
        if(ev & (BC_EV_END | BC_EV_END_FULL)){
            time_t u = c.power_at; char ub[32]; strftime(ub, sizeof ub, "%H:%M", localtime(&u));
            printf("  %s %3d%%  ends (%s), unplugged about %s\n", tb, s.pct, ev & BC_EV_END_FULL ? "FULL charge" : "partial", ub);
            if(ev & BC_EV_END_FULL) fulls++;
        }
        prev_t = s.t; prev_pct = s.pct;
    }
    fclose(f);
    printf("replay: %d sessions, %d full charges\n", sessions, fulls);
}

int main(int argc, char **argv){
    synthetic();
    if(argc > 1) replay(argv[1]);
    return 0;
}
