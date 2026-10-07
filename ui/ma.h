/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MA_H
#define MA_H
#include "lvgl/lvgl.h"
void ma_create(lv_obj_t *root);      /* SCR_MA */
void ma_refresh(void);
int  ma_is_on(void);
int  ma_playing(void);           /* Music Assistant is playing (keeps the Disc awake) */
void ma_set_on(int on);
void ma_edit_server(void);
void ma_edit_name(void);
void ma_settings_load(void);
void ma_restart(void);               /* new settings: restart the helper if it runs */
void ma_delay_changed(void);         /* Sync Delay moved: restart once it settles */
void ma_mode_leaving(void);          /* another working mode was picked */
int  ma_controls(void);              /* Music's buttons go to Music Assistant */
void ma_send(const char *cmd);       /* toggle | play | pause | next | previous | seek <ms> */
int  ma_seek(long ms);
void ma_boot(void);                  /* start it again after boot if it was on */
void ma_test_state(int s);           /* host renders: 0 off, 1 connecting, 2 ready, 3 playing, 4 error */
extern char g_ma_server_lbl[48], g_ma_name_lbl[48];
#endif
