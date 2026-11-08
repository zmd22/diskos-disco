/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Charge cycles: see battcycle.h for what the hardware reports and why the rules are what they are. */
#include "battcycle.h"
#include <string.h>

void bc_init(bcycle_t *c){
    memset(c, 0, sizeof *c);
    c->magic = BC_MAGIC1; c->low = BC_NONE; c->last_pct = BC_NONE; c->rules = BC_RULES;
}
int bc_load(bcycle_t *c, const void *buf, unsigned len){
    uint32_t magic; if(len < 20) return 0;
    memcpy(&magic, buf, 4);
    if(magic != BC_MAGIC1) return 0;
    bc_init(c);
    if(len >= sizeof *c){
        memcpy(c, buf, sizeof *c);
        if(c->rules == BC_RULES) return 1;
        bcycle_t t = *c; bc_init(c);                     /* other rules: keep the totals, find the session again */
        c->full_at = t.full_at; c->play_min = t.play_min; c->scr_min = t.scr_min;
        return 2;
    }
    memcpy(c, buf, 20);                                  /* a short (old) record: the totals; no session known */
    return 2;
}
/* 95, not 90: a quick top-up unplugged at 90 is not a full charge. A charge that stops by itself lower is caught by
 * the charger's held voltage instead (bc_step). */
int bc_full_level(int limit){ return limit ? 79 : 95; }
int bc_on_power(const bcycle_t *c, uint32_t now, int limit){
    if(!c->pwr) return 0;
    if(limit && c->full_seen) return 1;
    return now >= c->power_at && now - c->power_at <= BC_RECENT_S;
}

static int held_by_charger(const bc_reading_t *r){ return r->mv > 0 && r->mv >= BC_HOLD_MV; }
int bc_power_evidence(const bcycle_t *c, const bc_reading_t *r){
    if(r->charging) return 1;
    if(c->last_pct != BC_NONE && r->pct > (int)c->last_pct && c->pwr) return 1;   /* rising inside a session */
    return c->pwr && held_by_charger(r);
}
static void start(bcycle_t *c, const bc_reading_t *r){
    c->pwr = 1; c->peak = (uint32_t)r->pct; c->peak_at = r->now; c->full_seen = 0; c->power_at = r->now;
}
int bc_step(bcycle_t *c, const bc_reading_t *r){
    if(r->pct < 0 || r->pct > 100) return BC_EV_NONE;
    int ev = BC_EV_NONE, pct = r->pct;
    if(!c->pwr){
        /* a lone +1 % happens on battery now and then (the gauge settling), so a rise counts from 2 points */
        if(c->low == BC_NONE || pct < (int)c->low) c->low = (uint32_t)pct;
        if(r->charging || pct >= (int)c->low + 2){ start(c, r); ev |= BC_EV_START; }
    }
    if(c->pwr){
        if(bc_power_evidence(c, r)) c->power_at = r->now;
        if(pct > (int)c->peak){ c->peak = (uint32_t)pct; c->peak_at = r->now; }
        int done = pct >= bc_full_level(r->limit)
                || (!r->charging && held_by_charger(r) && (int)c->peak >= (r->limit ? 75 : 80)
                    && r->now - c->peak_at >= BC_STOP_S);   /* stopped by itself below the top */
        if(done && !c->full_seen){ c->full_seen = 1; ev |= BC_EV_FULL; }
        /* unplugged: the level fell below the peak (only the battery lowers it) and the charger isn't holding it.
         * Three points down ends it whatever the voltage says. */
        if(!r->charging && ((pct < (int)c->peak && !held_by_charger(r)) || pct + 3 <= (int)c->peak)){
            ev |= c->full_seen ? BC_EV_END_FULL : BC_EV_END;
            c->pwr = 0; c->low = (uint32_t)pct;
        }
    }
    c->last_pct = (uint32_t)pct;
    return ev;
}
int bc_feed(bcycle_t *c, const bc_reading_t *r, uint32_t prev_t, int prev_pct){
    if(prev_t && (r->now < prev_t || r->now - prev_t > BC_GAP_S)) return bc_resume(c, r, prev_pct, 0);
    return bc_step(c, r);
}
int bc_resume(bcycle_t *c, const bc_reading_t *r, int last_pct, int last_power){
    if(r->pct < 0 || r->pct > 100) return BC_EV_NONE;
    int ev = BC_EV_NONE;
    if(!c->pwr && last_pct >= 0 && r->pct >= last_pct + 5){   /* it charged while off: an unwatched session */
        start(c, r); ev |= BC_EV_START;
        if(r->pct >= bc_full_level(r->limit)){ c->full_seen = 1; ev |= BC_EV_FULL; }
    }
    (void)last_power;
    /* the gauge re-estimates after a restart and can read 2-3 points higher: across a gap only the 5-point rule
     * above counts, so the battery baseline starts again from this reading */
    if(!c->pwr) c->low = (uint32_t)r->pct;
    c->last_pct = BC_NONE;   /* no rise is measured across the gap */
    return ev | bc_step(c, r);
}
