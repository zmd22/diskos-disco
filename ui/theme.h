/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
/* theme.h - semantic colours for the whole UI. Every colour on screen is a ROLE; the active theme maps roles to RGB.
 * theme.c is the only file that turns RGB into colours (`make check-theme` fails the build on any other
 * lv_color_hex()). Roles that currently share a value are still separate roles: light and custom themes give them
 * different values. Runtime colours (album-art accent, a user-picked RGB) go through theme_color_from_rgb(). */
#ifndef THEME_H
#define THEME_H
#include <stdint.h>
#include "lvgl/lvgl.h"

#include "theme_roles.h"   /* theme_color_role_t (LVGL-free) */

/* Font roles: what a piece of text IS, so a theme can change typography without touching screens. Stage 1b: every
 * role returns exactly the font the UI used before (UI_n = Montserrat n with LVGL's symbol glyphs; USER_n = the
 * Montserrat -> international -> CJK chain for track/artist/album text). */
typedef enum {
    THEME_FONT_UI_10, THEME_FONT_UI_12, THEME_FONT_UI_14, THEME_FONT_UI_16, THEME_FONT_UI_18, THEME_FONT_UI_20,
    THEME_FONT_UI_22, THEME_FONT_UI_24, THEME_FONT_UI_28, THEME_FONT_UI_32, THEME_FONT_UI_36, THEME_FONT_UI_40,
    THEME_FONT_USER_14, THEME_FONT_USER_16, THEME_FONT_USER_18, THEME_FONT_USER_20,
    THEME_FONT_HEADER,              /* screen header titles (user text: folder/album/playlist names) */
    THEME_FONT_CLOCK,               /* the Home clock */
    THEME_FONT_SAVER_CLOCK,         /* the screensaver clock */
    THEME_FONT_DATE,                /* the Home date under the clock */
    THEME_FONT_ICON_20, THEME_FONT_ICON_28,
    THEME_FONT_HEADER_SM,           /* optional: the header face one size down, a long title's first step to fit */
    THEME_FONT_UI_8, THEME_FONT_UI_26, THEME_FONT_UI_30, THEME_FONT_UI_34, THEME_FONT_UI_38, THEME_FONT_UI_42, THEME_FONT_UI_44, THEME_FONT_UI_46, THEME_FONT_UI_48,
    THEME_FONT_COUNT
} theme_font_role_t;
const lv_font_t *theme_font(theme_font_role_t role);
int theme_font_size(void);   /* 0 Small, 1 Medium (default), 2 Large: cfg "font_size", read once */
#define TF(role) theme_font(THEME_FONT_##role)

void        theme_init(void);
int         theme_variant(void);                    /* 0 dark, 1 light (the active one) */
int         theme_outdoor(void);                    /* 1 = Outdoor mode (light variant + full backlight) this run */
/* Built-in colour presets (cfg "theme_preset"): 0 = Default, then the generated ones (theme_presets.inc). Each has an
 * explicit dark and light palette; Appearance / Outdoor pick which of the two is active. */
#define THEME_PRESET_COUNT 11
#define THEME_PRESET_RING 8
int theme_ring_light(void);
int theme_auto_supported(void);
#define THEME_PRESET_BRAUN 9
#define THEME_PRESET_DISCO 10   /* fork: Ring-family theme on the album cover (disco.c) */
int th_disco(void);        /* the Disco theme is active */
int th_ringlike(void);     /* Ring or Disco: the Ring interface (buttons, Now Playing ring, accent rules) */
extern const char *const theme_preset_names[THEME_PRESET_COUNT];
int         theme_preset(void);                     /* the active preset index (THEME_PRESET_CUSTOM = the saved one) */
#define THEME_PRESET_CUSTOM 100                     /* cfg theme_preset value: use /usr/data/diskos-custom-theme.json */
int         theme_custom_active(void);              /* 1 = this run uses the saved custom theme */
lv_color_t  theme_preset_color(int preset, theme_color_role_t role);   /* preview: a preset's colour, active variant */

/* Component traits: how a theme draws some things, beyond colour (screens ask; the default theme has none). */
typedef enum {
    THEME_TRAIT_DOT_GRID,       /* dot-grid screen backgrounds */
    THEME_TRAIT_DOT_TITLES,     /* screen titles in the dot-matrix face, upper case */
    THEME_TRAIT_MATRIX_CLOCK,   /* the Home clock as a block matrix with an accent colon */
    THEME_TRAIT_QS_RING,        /* Quick Settings: lit tiles are an accent ring + glow, labels upper case */
    THEME_TRAIT_FLAT_LISTS,     /* list rows without cards: text, hairline separators, accent chevrons */
    THEME_TRAIT_NP_BIG_PLAY,    /* Now Playing: dot-matrix title, play/pause as a filled accent circle */
    THEME_TRAIT_COUNT
} theme_trait_t;
int         theme_trait(theme_trait_t t);
/* A theme's own language beyond colour: its fonts, background texture, title case and component kit. Each unique
 * theme has one (theme_defs.c); Default and custom themes have none (Default's fonts and components). */
typedef enum { THEME_TEX_NONE, THEME_TEX_DOTS, THEME_TEX_GRID, THEME_TEX_SCANLINES, THEME_TEX_PAPER, THEME_TEX_BEZEL } theme_texture_t;
typedef enum { THEME_CASE_KEEP, THEME_CASE_UPPER, THEME_CASE_LOWER } theme_case_t;
/* screens a textured theme draws on plain canvas (its design says so); every other screen shows the texture */
enum { THEME_PLAIN_HOME = 1, THEME_PLAIN_LIBRARY = 2, THEME_PLAIN_NOWPLAYING = 4 };
struct theme_kit;
typedef struct theme_def {
    const char *name;                         /* the preset name (theme_presets.inc) */
    const lv_font_t *font[THEME_FONT_COUNT];  /* per font role; NULL = Default's font for that role */
    unsigned traits;                          /* 1 << theme_trait_t */
    theme_texture_t texture;
    theme_case_t title_case;                  /* screen titles */
    unsigned plain;                           /* THEME_PLAIN_*: screens without the texture */
    int fixed_accent;                         /* 1 = the theme's accent always (no album-art / user accent) */
    const struct theme_kit *kit;              /* NULL = Default's components */
} theme_def_t;
const theme_def_t *theme_def(void);
int         theme_screen_plain(unsigned which);   /* 1 = this screen is plain canvas in the active theme */
const lv_font_t *theme_font_substitute(const lv_font_t *f);   /* Default face -> the theme's (NULL = keep) */           /* the active theme's definition, NULL for Default / custom */
const theme_def_t *theme_def_find(const char *name);   /* theme_defs.c */
void        theme_list_row(lv_obj_t *row);         /* call after a list row is filled: flat style + accent chevrons */
char       *theme_lower(char *dst, int cap, const char *src);   /* as theme_upper, A-Z lower-cased */
void        theme_case_text(lv_obj_t *label, const char *text);   /* text in the theme's case (not a title form) */
const lv_font_t *theme_font_smaller(const lv_font_t *f);   /* the theme's next smaller text face, NULL = none */
const lv_font_t *theme_title_smaller(const lv_font_t *f);   /* a title's next smaller face (HEADER_SM first), NULL = none */
extern const char theme_title_tag;   /* user_data of a header title sized by title_fit (ui.c): a title to the kit */
/* set on a screen title that stepped down from the header face to fit: still a title to the theme kit */
#define THEME_TITLE_STEPPED LV_OBJ_FLAG_USER_3
void        theme_title_text(lv_obj_t *label, const char *text);   /* set a screen title (upper case if dot titles) */
/* copy src into dst (cap bytes incl. NUL) with ASCII a-z upper-cased; every other byte (UTF-8 accents, CJK) is kept,
 * and truncation never splits a multi-byte character. Returns dst. */
char       *theme_upper(char *dst, int cap, const char *src);
void        theme_screen_bg(lv_obj_t *root);       /* a screen root's background: canvas, plus the dot grid if any */
void        theme_screen_solid(lv_obj_t *root);    /* this screen is plain canvas even in a dot-grid theme */
const lv_font_t *theme_font_original(int px); /* stable symbol-bearing faces for the existing user-text chains */
const lv_font_t *theme_font_base(int px);          /* Montserrat 14/16/18/20: the symbol-carrying base of the
                                                    * international/CJK fallback chains (never themed) */
lv_color_t  theme_color(theme_color_role_t role);
lv_color_t theme_color_mix(lv_color_t foreground,lv_color_t background,uint8_t opacity);
lv_color_t  theme_color_from_rgb(uint32_t rgb);     /* runtime colours: album accent, user-picked RGB */
lv_color_t  theme_on_color(lv_color_t bg);          /* readable text on a fill of bg (ON_ACCENT, else black/white) */
lv_color_t  theme_accent_from_rgb(uint32_t rgb);    /* an album/user ACCENT made readable on this variant's surfaces */
lv_color_t  theme_accent_from_hsv(uint16_t h, uint8_t s, uint8_t v);   /* a generated accent (no-art fallback), same fit */
uint32_t    theme_rgb(theme_color_role_t role);     /* the role's 0xRRGGBB (for code that stores RGB ints) */
#define TC(role) theme_color(THEME_CLR_##role)

#include "fork_theme.h"
#endif
