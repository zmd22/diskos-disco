/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The Braun theme (Settings > Display > Theme): 70s Braun / Dieter Rams - warm off-white, a speaker-grille
 * dot texture, dark-grey knobs, a solid lower "segment" panel following the round edge, Inter-like type,
 * and one orange accent meaning only "on / primary / focused". Everything here is used only when the
 * Braun theme is active (th_braun()); the Ring theme is untouched. */
#pragma once
#include "lvgl/lvgl.h"
#include "theme.h"
/* Braun colours come from upstream semantic palette roles, including its component refinements. */
int  br_dark(void);                                          /* 1 when Braun runs in its dark variant */
#define BR_BG theme_rgb(THEME_CLR_CANVAS)
#define BR_PANEL theme_rgb(THEME_CLR_SURFACE_STRONG)
#define BR_SURF theme_rgb(THEME_CLR_SURFACE)
#define BR_DOT theme_rgb(THEME_CLR_DOT_GRID)
#define BR_RULE theme_rgb(THEME_CLR_BORDER)
#define BR_TXT theme_rgb(THEME_CLR_TEXT_PRIMARY)
#define BR_TXT2 theme_rgb(THEME_CLR_TEXT_SECONDARY)
#define BR_TXT3 theme_rgb(THEME_CLR_TEXT_DISABLED)
#define BR_ACC theme_rgb(THEME_CLR_ACCENT_PRIMARY)
#define BR_KNOB theme_rgb(THEME_CLR_CONTROL_KNOB)
#define BR_KNOB_L theme_rgb(THEME_CLR_CONTROL_FILL)
#define BR_KNOB_IC theme_rgb(THEME_CLR_ON_CONTROL)
#define BR_PTR theme_rgb(THEME_CLR_GRABBER)
#define BR_PRESS theme_rgb(THEME_CLR_SURFACE_PRESSED)
#define BR_SHADOW theme_rgb(THEME_CLR_ACTION_SHADOW)
int  th_braun(void);
const lv_font_t *br_font(int size, int bold);                /* Inter: Medium 12/14/16, Bold 16/18/22, 44 (digits) */                                        /* 1 when the Braun theme is active (read once at boot) */
void br_face(lv_obj_t *root);                               /* off-white face + the grille (shared image) */
lv_obj_t *br_segment(lv_obj_t *root, int y);                /* the lower panel: straight top at y, down to the rim */
lv_obj_t *br_disc(lv_obj_t *parent, int cx, int cy, int r, uint32_t col);   /* a solid disc (panels behind content) */
lv_obj_t *br_knob(lv_obj_t *parent, int cx, int cy, int r, const char *icon, const lv_font_t *font);  /* a dark knob */
void br_pointer_set(lv_obj_t *p, int d, int on);   /* fork: ON/OFF knob pointer (0 deg icon colour / 45 deg orange) */
void br_knob_set_on(lv_obj_t *knob, int on);                /* pointer orange + lamp lit */
lv_obj_t *br_button(lv_obj_t *parent, int cx, int cy, int r, const char *icon, const lv_font_t *font, int primary);  /* flat disc button */
void br_style_switch(lv_obj_t *sw);                         /* an lv_switch as a Braun sliding switch */
lv_obj_t *br_clock_face(lv_obj_t *parent, int cx, int cy, int rd, int numerals);   /* the Braun clock face, drawn once */
lv_obj_t *br_weather_window(lv_obj_t *parent, int x, int y, lv_obj_t **label_out);   /* the window at 3 o'clock */
void br_weather_text(lv_obj_t *label, const char *s);   /* "<icon>  14°C  Sunny" -> "<icon> 14°" */
lv_obj_t *br_label(lv_obj_t *parent, const char *txt, const lv_font_t *font, uint32_t col);
