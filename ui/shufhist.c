/* SPDX-License-Identifier: GPL-3.0-or-later */
/* see shufhist.h. A list of what played (oldest first), a cursor on the song playing now, and an "expected"
 * index while a jump into the history is on its way (paced skips can stack several before the player answers). */
#include "shufhist.h"
#include <stdio.h>
#include <string.h>
typedef struct { int pos_id; char path[256]; } sh_t;
static sh_t g_h[SH_MAX];
static int g_n, g_cur = -1, g_exp = -1;
static int same(const sh_t *e, int pos_id, const char *path){
    if(strcmp(e->path, path)) return 0;
    return !(e->pos_id && pos_id && e->pos_id != pos_id);
}
void sh_reset(void){ g_n = 0; g_cur = g_exp = -1; }
void sh_cancel(void){ g_exp = -1; }
void sh_observe(int pos_id, const char *path){
    if(!path || !path[0]) return;
    if(g_exp >= 0 && same(&g_h[g_exp], pos_id, path)){ g_cur = g_exp; g_exp = -1; return; }   /* arrived where we jumped */
    if(g_exp >= 0) return;                                    /* still on the way: the old song may report once more */
    if(g_cur >= 0 && same(&g_h[g_cur], pos_id, path)){ if(!g_h[g_cur].pos_id) g_h[g_cur].pos_id = pos_id; return; }
    g_n = g_cur + 1;                                          /* a new song: anything we had stepped back over goes */
    if(g_n >= SH_MAX){ memmove(g_h, g_h + 1, sizeof g_h[0] * (SH_MAX - 1)); g_n = SH_MAX - 1; }
    g_h[g_n].pos_id = pos_id; snprintf(g_h[g_n].path, sizeof g_h[g_n].path, "%s", path);
    g_cur = g_n++;
}
static int step(int dir, int *pos_id, char *path, size_t n){
    int base = g_exp >= 0 ? g_exp : g_cur, t = base + dir;
    if(base < 0 || t < 0 || t >= g_n) return 0;
    g_exp = t; *pos_id = g_h[t].pos_id; snprintf(path, n, "%s", g_h[t].path);
    return 1;
}
int sh_back(int *pos_id, char *path, size_t n){ return step(-1, pos_id, path, n); }
int sh_forward(int *pos_id, char *path, size_t n){ return step(+1, pos_id, path, n); }
