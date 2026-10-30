/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "theme_kit.h"
void br2_style(lv_obj_t *,kit_role_t);
lv_obj_t *br2_header(lv_obj_t *,const char *,lv_event_cb_t);
void br2_keep_tree(lv_obj_t *);
void br2_emboss(lv_obj_t *o,int raised);
void br2_depth(lv_obj_t *o,int orange_icons);   /* shaded key with depth; orange icon glyphs */   /* subtle top highlight; raised adds a soft drop */
void kit_fork_np_braun2(void);
