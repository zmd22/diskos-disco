/* SPDX-License-Identifier: GPL-3.0-or-later */
/* diskOS fork design tokens: every shared colour, size, radius, arc width, duration and icon lives here.
 * Screens use these names instead of raw values, so the interface stays one piece as it grows. */
#ifndef FORK_THEME_H
#define FORK_THEME_H
#include "lvgl/lvgl.h"

/* ---- colour -------------------------------------------------------------------------------------------
 * Rule: the accent (red) means "on" / selected in the UI. Media surfaces - anything showing the playing
 * track - use the album's own colour instead (ui_media_accent()). Levels (brightness, volume) are text-1. */
/* The colours are looked up in the active theme's palette (Settings > Display > Theme): the Ring values are
 * the originals, the Braun values its warm-white equivalents. Screens use these names - never raw hex - so a
 * theme applies everywhere by construction. (Media surfaces, such as text over album art, keep explicit white.) */
enum { TH_C_BG, TH_C_SURF1, TH_C_SURF2, TH_C_TRACK, TH_C_TXT1, TH_C_TXT2, TH_C_TXT3, TH_C_ACCENT,
       TH_C_MUTED, TH_C_SOFT, TH_C_ONACC, TH_C_COUNT };
uint32_t th_hex(int token);
#define TH_BG        th_hex(TH_C_BG)       /* Ring 000000 */
#define TH_SURF1     th_hex(TH_C_SURF1)    /* 1C1C1E  cards, rows, tiles, pills */
#define TH_SURF2     th_hex(TH_C_SURF2)    /* 2C2C2E  pressed / lifted */
#define TH_TRACK     th_hex(TH_C_TRACK)    /* 3A3A3C  the unfilled part of an arc */
#define TH_TXT1      th_hex(TH_C_TXT1)     /* FFFFFF  primary text and glyphs */
#define TH_TXT2      th_hex(TH_C_TXT2)     /* AEAEB2  secondary: details, captions */
#define TH_TXT3      th_hex(TH_C_TXT3)     /* 636366  tertiary: least important */
#define TH_ACCENT    th_hex(TH_C_ACCENT)   /* E4122C  = UI_RED (Braun: orange) */
#define TH_MUTED     th_hex(TH_C_MUTED)    /* 8E8E93  muted labels, keys */
#define TH_SOFT      th_hex(TH_C_SOFT)     /* C7C7CC  soft secondary text, glyphs */
#define TH_ONACC     th_hex(TH_C_ONACC)    /* FFFFFF  text / glyphs on the accent (white in both themes) */

/* ---- type scale (Montserrat; lists that show user text use ui_font_cjk() at the same sizes) ---------- */
#define TH_F_CLOCK   (TF(UI_46))
#define TH_F_TITLE   (TF(UI_20))   /* screen headers */
#define TH_F_LIST    (TF(UI_18))   /* list and row titles */
#define TH_F_DETAIL  (TF(UI_16))   /* values, subtitles, date */
#define TH_F_CAPTION (TF(UI_12))   /* tile captions, times, hub captions */
/* poster 28 is s_font28 in ui.c (needs the CJK fallback chain); the Now Playing play/pause stays oversized */

/* ---- shape --------------------------------------------------------------------------------------------- */
#define TH_R_ROW     14
#define TH_R_CARD    18
#define TH_R_PILL    LV_RADIUS_CIRCLE
#define TH_ROW_H     54            /* list rows at the list/detail sizes */

/* ---- rims: what each edge of the round panel is for ----------------------------------------------------
 * top: status & level   bottom: time & amount   left: position in a list   right: jump (A-Z) */
#define TH_ARC_IND   3             /* indicators: battery, progress, immersive progress */
#define TH_ARC_CTL   10            /* controls you drag: brightness, volume (rounded ends) */

/* ---- motion -------------------------------------------------------------------------------------------- */
#define TH_T_PRESS   120
#define TH_T_SCREEN  200

/* ---- icons: Font Awesome 4.7 (font_theme_20 carries the glyphs LVGL's symbol set lacks) ------------ */
LV_FONT_DECLARE(font_theme_20);
LV_FONT_DECLARE(font_theme_24);   /* the search glyph at 24 px (Ring Home's larger buttons) */
#define TH_IC_SEARCH "\xEF\x80\x82"   /* f002 */
#define TH_IC_HEART  "\xEF\x80\x84"   /* f004 solid: state is carried by colour, not by outline */
#define TH_IC_RECORD "\xEF\x86\x92"   /* f192 dot-circle: the immersive (record) button */

/* the playing track's colour, or the accent when the cover's colour is too dull (ui.c) */
lv_color_t ui_media_accent(void);
void ring_button_style(lv_obj_t *b);   /* fork: Ring buttons - accent border, album-colour glyph (no-op in other themes) */
#endif
