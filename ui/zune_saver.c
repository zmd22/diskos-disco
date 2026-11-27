/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Zune lock screen / standby (Figma 02), whatever the saver style: with a track the cover bleeds off the left edge
 * (the Now Playing art block) with the time in the title slot, the date, the song and artist; with nothing loaded a
 * type-only clock as big as the glass allows, the weekday and date hugging the curve under it. The battery as "82%"
 * at the top. Redraws once a minute. */
#include "screens.h"
#include "theme.h"
#include "theme_kit.h"
#include "zune_dial.h"
#include "simple_theme.h"
#include "ipc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

LV_FONT_DECLARE(font_zune_light_72) LV_FONT_DECLARE(font_zune_light_150) LV_FONT_DECLARE(font_zune_light_32)
LV_FONT_DECLARE(font_zune_regular_12) LV_FONT_DECLARE(font_zune_regular_13) LV_FONT_DECLARE(font_zune_regular_17)
int zhome_battery_pct(void);
/* light_150 digit box inside the label (measured): top, height, baseline */
#define ZT_TOP 3
#define ZT_H 107
#define ZT_BASE 110
static int zlock_tint(void){ const char *e = getenv("ZLOCK_TINT"); return e ? atoi(e) : simple_cfg_get_int("zlock_tint", 4); }

static struct {
    lv_obj_t *root, *batt, *art, *art_img, *ph;
    lv_obj_t *mt, *mmer, *mdate, *mtitle, *martist, *hint;    /* with music */
    lv_obj_t *ptrack, *pfill, *ptime;                         /* with music: how far into the song */
    lv_obj_t *it, *ic, *im, *imer, *iday, *idate;             /* idle: hours, colon, minutes as three labels (tint) */
    int have, md;
    char t[32];int clock_ready,day_key,tint_key,source_key,prog_ready;uint32_t accent_key,prog_at;unsigned art_revision;

} Z;

static lv_obj_t *lbl(const lv_font_t *f, lv_color_t c){
    lv_obj_t *l=lv_label_create(Z.root);kit_keep(l);lv_label_set_text(l,"");
    lv_obj_set_style_text_font(l, f, 0); lv_obj_set_style_text_color(l, c, 0);
    lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
    return l;
}
static void show(lv_obj_t *o, int on){ if(!o||lv_obj_has_flag(o,LV_OBJ_FLAG_HIDDEN)==!on)return; if(on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN); }

static void fmt_ms(char *b, size_t n, long ms){ long s = ms / 1000; if(s < 0) s = 0;
    if(s >= 3600) snprintf(b, n, "%ld:%02ld:%02ld", s / 3600, s / 60 % 60, s % 60); else snprintf(b, n, "%ld:%02ld", s / 60, s % 60); }
static void zprog_tick(lv_timer_t *t){
    (void)t;
    if(!Z.root||!Z.ptrack||Z.have<=0||screen_current()!=SCR_SAVER||ui_display_is_off())return;
    if(Z.prog_ready&&lv_tick_elaps(Z.prog_at)<500)return;Z.prog_ready=1;Z.prog_at=lv_tick_get();
    long dur = 0, pos = 0; int playing, on = saver_song(&pos, &dur, &playing);
    int vis = on && simple_cfg_get_int("zlock_src", 0) == 0; show(Z.ptrack, vis); show(Z.ptime, vis);
    if(!on) return;
    if(pos > dur && dur > 0) pos = dur;
    char a[16], b[16], l[40]; fmt_ms(a, sizeof a, pos); fmt_ms(b, sizeof b, dur);
    if(dur > 0) snprintf(l, sizeof l, "%s / %s", a, b); else snprintf(l, sizeof l, "%s", a);
    if(strcmp(lv_label_get_text(Z.ptime),l))lv_label_set_text(Z.ptime,l);
    int width=dur>0?(int)(132LL*pos/dur):0;if(width<0)width=0;if(width>132)width=132;
    if(lv_obj_get_style_width(Z.pfill,0)!=width)lv_obj_set_width(Z.pfill,width);
}
void zsaver_create(lv_obj_t *root){
    memset(&Z, 0, sizeof Z);
    Z.root = root;
    lv_obj_set_style_bg_color(root, TC(CANVAS), 0); lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    Z.batt = lbl(zune_font(&font_zune_regular_12), TC(TEXT_PRIMARY));
    lv_obj_align(Z.batt, LV_ALIGN_TOP_MID, 0, 24);
    /* with music */
    Z.art = lv_obj_create(root); lv_obj_remove_style_all(Z.art); kit_keep(Z.art);
    lv_obj_set_size(Z.art, 192, 192); lv_obj_set_pos(Z.art, -46, 48);
    lv_obj_set_style_bg_color(Z.art, TC(SURFACE), 0); lv_obj_set_style_bg_opa(Z.art, LV_OPA_COVER, 0);
    lv_obj_remove_flag(Z.art, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    Z.art_img = lv_image_create(Z.art); lv_obj_set_size(Z.art_img, 192, 192); lv_obj_center(Z.art_img);
    Z.mt = lbl(zune_font(&font_zune_light_72), TC(TEXT_PRIMARY)); lv_obj_set_pos(Z.mt, 162, 46);
    Z.mmer = lbl(zune_font(&font_zune_regular_13), TC(TEXT_MUTED));
    Z.mdate = lbl(zune_font(&font_zune_regular_13), TC(TEXT_MUTED)); lv_obj_set_pos(Z.mdate, 168, 134);
    Z.mtitle = lbl(zune_font(&font_zune_regular_17), TC(TEXT_PRIMARY)); lv_obj_set_pos(Z.mtitle, 168, 174);
    lv_label_set_long_mode(Z.mtitle, LV_LABEL_LONG_SCROLL_CIRCULAR); lv_obj_set_width(Z.mtitle, 140);
    Z.martist = lbl(zune_font(&font_zune_regular_13), TC(TEXT_MUTED)); lv_obj_set_pos(Z.martist, 168, 198);
    lv_label_set_long_mode(Z.martist, LV_LABEL_LONG_SCROLL_CIRCULAR); lv_obj_set_width(Z.martist, 140);
    Z.ptrack = lv_obj_create(root); lv_obj_remove_style_all(Z.ptrack); kit_keep(Z.ptrack);
    lv_obj_set_size(Z.ptrack, 132, 3); lv_obj_set_pos(Z.ptrack, 168, 230);
    lv_obj_set_style_bg_color(Z.ptrack, TC(CONTROL_TRACK), 0); lv_obj_set_style_bg_opa(Z.ptrack, LV_OPA_COVER, 0);
    lv_obj_remove_flag(Z.ptrack, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    Z.pfill = lv_obj_create(Z.ptrack); lv_obj_remove_style_all(Z.pfill); kit_keep(Z.pfill);
    lv_obj_set_size(Z.pfill, 0, 3); lv_obj_set_style_bg_color(Z.pfill, TC(ACCENT_PRIMARY), 0); lv_obj_set_style_bg_opa(Z.pfill, LV_OPA_COVER, 0);
    lv_obj_remove_flag(Z.pfill, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    Z.ptime = lbl(zune_font(&font_zune_regular_12), TC(TEXT_MUTED)); lv_obj_set_pos(Z.ptime, 168, 240);
    Z.hint = lbl(zune_font(&font_zune_regular_12), TC(TEXT_PRIMARY)); lv_label_set_text(Z.hint, "tap to wake");
    lv_obj_align(Z.hint, LV_ALIGN_TOP_MID, 0, 300);
    /* idle */
    Z.it = lbl(&font_zune_light_150, TC(TEXT_PRIMARY)); lv_obj_set_style_text_letter_space(Z.it, -4, 0);
    Z.ic = lbl(&font_zune_light_150, TC(TEXT_PRIMARY)); lv_obj_set_style_text_letter_space(Z.ic, -4, 0);
    Z.im = lbl(&font_zune_light_150, TC(TEXT_PRIMARY)); lv_obj_set_style_text_letter_space(Z.im, -4, 0);
    Z.imer = lbl(zune_font(&font_zune_regular_17), TC(TEXT_MUTED));
    Z.iday = lbl(zune_font(&font_zune_light_32), TC(TEXT_PRIMARY)); lv_obj_align(Z.iday, LV_ALIGN_TOP_MID, 0, 208);
    Z.idate = lbl(zune_font(&font_zune_regular_17), TC(TEXT_MUTED)); lv_obj_align(Z.idate, LV_ALIGN_TOP_MID, 0, 250);
    Z.have = -1;
    zprog_tick(NULL);
}
static void layout(void){
    int m = Z.have > 0 && simple_cfg_get_int("zlock_src", 0) == 0;   /* Clock Only: the big clock even with music */
    int song = m && !Z.md;                                          /* other modes: no cover; zprog_tick shows progress */
    show(Z.art, song); show(Z.mt, m); show(Z.mdate, m); show(Z.mtitle, m); show(Z.martist, m); show(Z.hint, m); show(Z.ptrack, song); show(Z.ptime, song);
    show(Z.it, !m); show(Z.ic, !m); show(Z.im, !m); show(Z.iday, !m); show(Z.idate, !m);
    show(Z.mmer, m && lv_label_get_text(Z.mmer)[0]); show(Z.imer, !m && lv_label_get_text(Z.imer)[0]);
    zsaver_set_visible(screen_current()==SCR_SAVER&&!ui_display_is_off());
}
void zsaver_set_clock(const char *t){
    if(!Z.root) return;
    const char *input=t?t:"--:--";time_t now=time(NULL);struct tm tm;localtime_r(&now,&tm);
    int day=tm.tm_year*366+tm.tm_yday,tint_now=zlock_tint(),source=simple_cfg_get_int("zlock_src",0);
    uint32_t ac_now=lv_color_to_u32(TC(ACCENT_PRIMARY));
    char battery[16]="";int bp=zhome_battery_pct();if(bp>=0)snprintf(battery,sizeof battery,"%d%%",bp);
    if(strcmp(lv_label_get_text(Z.batt),battery))lv_label_set_text(Z.batt,battery);
    if(Z.clock_ready&&!strcmp(Z.t,input)&&Z.day_key==day&&Z.tint_key==tint_now&&Z.source_key==source&&Z.accent_key==ac_now)return;
    snprintf(Z.t,sizeof Z.t,"%s",input);Z.clock_ready=1;Z.day_key=day;Z.tint_key=tint_now;Z.source_key=source;Z.accent_key=ac_now;
    char d[32];snprintf(d,sizeof d,"%s",input);
    char *sp = strchr(d, ' '), mer[8] = "";                        /* "7:29 PM": the digits big, "pm" small beside */
    if(sp){ snprintf(mer, sizeof mer, "%s", sp + 1); *sp = 0; for(char *c = mer; *c; c++) if(*c >= 'A' && *c <= 'Z') *c += 32; }
    lv_label_set_text(Z.mt, d);
    lv_label_set_text(Z.mmer, mer); lv_label_set_text(Z.imer, mer);
    /* idle: the time dead centre of the glass, hours | colon | minutes as three labels so any part can take the
     * accent (cfg "zlock_tint": 0 white, 1 all accent, 2 minutes, 3 colon, 4 hours); "pm" small on its baseline */
    char hh[16] = "", mm[16] = ""; const char *col = strchr(d, ':');
    if(col){ snprintf(hh, sizeof hh, "%.*s", (int)(col - d), d); snprintf(mm, sizeof mm, "%s", col + 1); }
    else snprintf(hh, sizeof hh, "%s", d);
    lv_label_set_text(Z.it, hh); lv_label_set_text(Z.ic, col ? ":" : ""); lv_label_set_text(Z.im, mm);
    lv_point_t a, b2, c;
    lv_text_get_size(&a, hh, &font_zune_light_150, -4, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    lv_text_get_size(&b2, col ? ":" : "", &font_zune_light_150, -4, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    lv_text_get_size(&c, mm, &font_zune_light_150, -4, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    int sx = a.x + b2.x + c.x - 8;
    int merw = 0; if(mer[0]){ lv_point_t ms; lv_text_get_size(&ms, mer, zune_font(&font_zune_regular_17), 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE); merw = ms.x + 6; }
    int room = 286 - merw, sc = sx > room ? 256 * room / sx : 256;
    int cw = sx * sc / 256 + merw, ix = 180 - cw / 2;
    int iy = 180 - (ZT_TOP + ZT_H / 2) * sc / 256;                 /* the digits' middle on the glass's middle */
    lv_obj_t *parts[3] = { Z.it, Z.ic, Z.im }; int w[3] = { a.x - 4, b2.x - 4, c.x };
    int tint = zlock_tint(); lv_color_t ac = TC(ACCENT_PRIMARY), wh = TC(TEXT_PRIMARY);
    int x = ix;
    for(int k = 0; k < 3; k++){
        lv_obj_set_style_transform_pivot_x(parts[k], 0, 0); lv_obj_set_style_transform_pivot_y(parts[k], 0, 0);
        lv_obj_set_style_transform_scale(parts[k], sc, 0);
        lv_obj_set_pos(parts[k], x, iy); x += w[k] * sc / 256;
        int on = tint == 1 || (tint == 2 && k == 2) || (tint == 3 && k == 1) || (tint == 4 && k == 0);
        lv_obj_set_style_text_color(parts[k], on ? ac : wh, 0);
    }
    lv_obj_set_pos(Z.imer, x + 6, iy + ZT_BASE * sc / 256 - 22);
    lv_obj_set_style_text_color(Z.imer, tint == 1 ? ac : TC(TEXT_MUTED), 0);
    int below = iy + (ZT_TOP + ZT_H) * sc / 256;                    /* weekday and date follow the digits */
    lv_obj_align(Z.iday, LV_ALIGN_TOP_MID, 0, below + 24);
    lv_obj_align(Z.idate, LV_ALIGN_TOP_MID, 0, below + 66);
    /* with music: the time shrinks to stay inside the glass right of the cover */
    lv_point_t ts; lv_text_get_size(&ts, d, zune_font(&font_zune_light_72), 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    int troom = 318 - 162 - (mer[0] ? 26 : 0), tsc = ts.x > troom ? 256 * troom / ts.x : 256;
    lv_obj_set_style_transform_pivot_x(Z.mt, 0, 0); lv_obj_set_style_transform_pivot_y(Z.mt, 0, 0);
    lv_obj_set_style_transform_scale(Z.mt, tsc, 0);
    lv_obj_set_pos(Z.mt, 162, 46 + (256 - tsc) * 30 / 256);
    lv_obj_set_pos(Z.mmer, 162 + ts.x * tsc / 256 + 4, 104);
    static const char *WD[] = { "sunday", "monday", "tuesday", "wednesday", "thursday", "friday", "saturday" };
    static const char *MO[] = { "january", "february", "march", "april", "may", "june", "july", "august", "september", "october", "november", "december" };
    char b[48];
    snprintf(b, sizeof b, "%.3s, %s %d", WD[tm.tm_wday], MO[tm.tm_mon], tm.tm_mday); lv_label_set_text(Z.mdate, b);
    lv_label_set_text(Z.iday, WD[tm.tm_wday]);
    snprintf(b, sizeof b, "%s %d", MO[tm.tm_mon], tm.tm_mday); lv_label_set_text(Z.idate, b);
    int p = zhome_battery_pct();
    if(p >= 0){ snprintf(b, sizeof b, "%d%%", p); lv_label_set_text(Z.batt, b); } else lv_label_set_text(Z.batt, "");
    layout();
    zprog_tick(NULL);
}
unsigned ui_art_revision(void);
void zsaver_set_visible(int on){
    lv_label_long_mode_t mode=on&&Z.have>0&&simple_cfg_get_int("zlock_src",0)==0?LV_LABEL_LONG_SCROLL_CIRCULAR:LV_LABEL_LONG_CLIP;
    int resumed=0;lv_obj_t *rows[]={Z.mtitle,Z.martist};
    for(int i=0;i<2;i++)if(rows[i]&&lv_label_get_long_mode(rows[i])!=mode){lv_label_set_long_mode(rows[i],mode);if(mode==LV_LABEL_LONG_SCROLL_CIRCULAR)resumed=1;}
    if(resumed){Z.prog_ready=0;zprog_tick(NULL);}
}
void zsaver_set_track(const char *title,const char *artist){
    if(!Z.root)return;
    int have=title!=NULL,md=saver_mode_active(),changed=Z.have!=have||Z.md!=md;Z.have=have;Z.md=md;
    zsaver_set_visible(screen_current()==SCR_SAVER&&!ui_display_is_off());
    if(strcmp(lv_label_get_text(Z.mtitle),title?title:""))lv_label_set_text(Z.mtitle,title?title:"");
    if(strcmp(lv_label_get_text(Z.martist),artist?artist:""))lv_label_set_text(Z.martist,artist?artist:"");
    unsigned revision=ui_art_revision();
    if(changed||Z.art_revision!=revision){
        Z.art_revision=revision;const lv_image_dsc_t *dsc=ui_current_cover_dsc();
        lv_image_set_src(Z.art_img,NULL);
        if(dsc&&dsc->header.w>0){lv_image_set_src(Z.art_img,dsc);lv_image_set_scale(Z.art_img,(uint32_t)(256*192/dsc->header.w));}
    }
    if(changed)layout();zprog_tick(NULL);
}

void zsaver_progress_tick(void){if(screen_current()==SCR_SAVER&&!ui_display_is_off())zprog_tick(NULL);}
