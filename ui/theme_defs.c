/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
/* theme_defs.c - the unique themes' definitions: fonts per role, texture, title case, component kit.
 * Colours are in theme_presets.inc (tools/gen_theme_presets.py); fonts in font_<theme>_*.c
 * (tools/gen_theme_fonts.py). Roles a theme leaves NULL keep Default's font (mostly the icon-bearing sizes 22+). */
#include "theme.h"
#include "theme_kit.h"
#include "theme_fonts.h"
#include <string.h>

LV_FONT_DECLARE(diskos_mono_14)
LV_FONT_DECLARE(diskos_mono_16)
LV_FONT_DECLARE(diskos_mono_18)
LV_FONT_DECLARE(diskos_mono_20)
LV_FONT_DECLARE(diskos_mono_24)
LV_FONT_DECLARE(diskos_mono_bold_12)
LV_FONT_DECLARE(diskos_dot_18)
LV_FONT_DECLARE(diskos_dot_28)

#define TRAIT(t) (1u << THEME_TRAIT_##t)
#define F(role) [THEME_FONT_##role]

extern const theme_kit_t theme_kit_ring, theme_kit_braun;
extern const theme_kit_t theme_kit_something, theme_kit_hifi, theme_kit_term, theme_kit_bauh, theme_kit_blue,
                         theme_kit_stone, theme_kit_zine;

static const theme_def_t DEFS[] = {
    { .name="Ring", .texture=THEME_TEX_NONE, .title_case=THEME_CASE_KEEP, .kit=&theme_kit_ring },
    { .name="Braun", .texture=THEME_TEX_NONE, .title_case=THEME_CASE_KEEP, .fixed_accent=1, .kit=&theme_kit_braun },
    { .name="Disco", .texture=THEME_TEX_NONE, .title_case=THEME_CASE_KEEP, .kit=&theme_kit_ring },   /* Ring's components */
    { .name = "something",
      .font = { F(UI_12) = &diskos_mono_bold_12, F(UI_14) = &diskos_mono_14, F(USER_14) = &diskos_mono_14,
                F(UI_16) = &diskos_mono_16, F(USER_16) = &diskos_mono_16, F(UI_18) = &diskos_mono_18,
                F(USER_18) = &diskos_mono_18, F(UI_20) = &diskos_mono_20, F(USER_20) = &diskos_mono_20,
                F(UI_22) = &diskos_mono_24, F(UI_24) = &diskos_mono_24, F(HEADER) = &diskos_dot_28,
                F(CLOCK) = &diskos_dot_28, F(SAVER_CLOCK) = &diskos_mono_24, F(DATE) = &diskos_dot_18,
                F(HEADER_SM) = &diskos_dot_18 },
      .traits = TRAIT(DOT_TITLES) | TRAIT(MATRIX_CLOCK) | TRAIT(QS_RING) | TRAIT(FLAT_LISTS) | TRAIT(NP_BIG_PLAY),
      .texture = THEME_TEX_DOTS, .title_case = THEME_CASE_UPPER, .fixed_accent = 1, .kit = &theme_kit_something,
      .plain = THEME_PLAIN_HOME | THEME_PLAIN_LIBRARY | THEME_PLAIN_NOWPLAYING },   /* its mockups: plain black */
    { .name = "hi-fi",
      .font = { F(UI_12) = &diskos_hifi_ui_12, F(UI_14) = &diskos_hifi_ui_14, F(USER_14) = &diskos_hifi_ui_14,
                F(UI_16) = &diskos_hifi_ui_16, F(USER_16) = &diskos_hifi_ui_16, F(UI_18) = &diskos_hifi_ui_18,
                F(USER_18) = &diskos_hifi_ui_18, F(UI_20) = &diskos_hifi_ui_20, F(USER_20) = &diskos_hifi_ui_20,
                F(UI_24) = &diskos_hifi_ui_24, F(HEADER) = &diskos_hifi_ui_16, F(CLOCK) = &diskos_hifi_seg_64,
                F(SAVER_CLOCK) = &diskos_hifi_seg_64, F(DATE) = &diskos_hifi_ui_16 },
      .texture = THEME_TEX_BEZEL, .title_case = THEME_CASE_UPPER, .fixed_accent = 1, .kit = &theme_kit_hifi },
    { .name = "terminal",
      .font = { F(UI_12) = &diskos_term_ui_12, F(UI_14) = &diskos_term_ui_14, F(USER_14) = &diskos_term_ui_14,
                F(UI_16) = &diskos_term_ui_16, F(USER_16) = &diskos_term_ui_16, F(UI_18) = &diskos_term_ui_18,
                F(USER_18) = &diskos_term_ui_18, F(UI_20) = &diskos_term_ui_20, F(USER_20) = &diskos_term_ui_20,
                F(UI_24) = &diskos_term_ui_24, F(HEADER) = &diskos_term_ui_16, F(CLOCK) = &diskos_term_clk_72,
                F(SAVER_CLOCK) = &diskos_term_clk_72, F(DATE) = &diskos_term_ui_14 },
      .texture = THEME_TEX_SCANLINES, .title_case = THEME_CASE_LOWER, .fixed_accent = 1, .kit = &theme_kit_term },
    { .name = "bauhaus",
      .font = { F(UI_12) = &diskos_bauh_ui_12, F(UI_14) = &diskos_bauh_ui_14, F(USER_14) = &diskos_bauh_ui_14,
                F(UI_16) = &diskos_bauh_ui_16, F(USER_16) = &diskos_bauh_ui_16, F(UI_18) = &diskos_bauh_ui_18,
                F(USER_18) = &diskos_bauh_ui_18, F(UI_20) = &diskos_bauh_ui_20, F(USER_20) = &diskos_bauh_ui_20,
                F(UI_24) = &diskos_bauh_ui_24, F(HEADER) = &diskos_bauh_bold_32, F(CLOCK) = &diskos_bauh_clk_140,
                F(SAVER_CLOCK) = &diskos_bauh_clk_96, F(DATE) = &diskos_bauh_bold_22,
                F(HEADER_SM) = &diskos_bauh_bold_22 },
      .texture = THEME_TEX_NONE, .title_case = THEME_CASE_UPPER, .fixed_accent = 1, .kit = &theme_kit_bauh },
    { .name = "blueprint",
      .font = { F(UI_12) = &diskos_mono_bold_12, F(UI_14) = &diskos_blue_sans_14, F(USER_14) = &diskos_blue_sans_14,
                F(UI_16) = &diskos_blue_sans_16, F(USER_16) = &diskos_blue_sans_16, F(UI_18) = &diskos_blue_sans_18,
                F(USER_18) = &diskos_blue_sans_18, F(UI_20) = &diskos_blue_sans_20, F(USER_20) = &diskos_blue_sans_20,
                F(UI_24) = &diskos_mono_24, F(HEADER) = &diskos_mono_16, F(CLOCK) = &diskos_blue_clk_56,
                F(SAVER_CLOCK) = &diskos_blue_clk_56, F(DATE) = &diskos_mono_14 },
      .texture = THEME_TEX_GRID, .title_case = THEME_CASE_UPPER, .fixed_accent = 1, .kit = &theme_kit_blue },
    { .name = "stone",
      .font = { F(UI_12) = &diskos_stone_ui_12, F(UI_14) = &diskos_stone_ui_14, F(USER_14) = &diskos_stone_ui_14,
                F(UI_16) = &diskos_stone_ui_16, F(USER_16) = &diskos_stone_ui_16, F(UI_18) = &diskos_stone_ui_18,
                F(USER_18) = &diskos_stone_ui_18, F(UI_20) = &diskos_stone_ui_20, F(USER_20) = &diskos_stone_ui_20,
                F(UI_24) = &diskos_stone_ui_24, F(HEADER) = &diskos_stone_ui_20, F(CLOCK) = &diskos_stone_clk_64,
                F(SAVER_CLOCK) = &diskos_stone_clk_64, F(DATE) = &diskos_stone_ui_16 },
      .texture = THEME_TEX_NONE, .title_case = THEME_CASE_KEEP, .fixed_accent = 1, .kit = &theme_kit_stone },
    { .name = "zine",
      .font = { F(UI_12) = &diskos_zine_ui_12, F(UI_14) = &diskos_zine_ui_14, F(USER_14) = &diskos_zine_ui_14,
                F(UI_16) = &diskos_zine_ui_16, F(USER_16) = &diskos_zine_ui_16, F(UI_18) = &diskos_zine_ui_18,
                F(USER_18) = &diskos_zine_ui_18, F(UI_20) = &diskos_zine_ui_20, F(USER_20) = &diskos_zine_ui_20,
                F(UI_24) = &diskos_zine_ui_24, F(HEADER) = &diskos_zine_glitch_26, F(CLOCK) = &diskos_zine_clk_50,
                F(SAVER_CLOCK) = &diskos_zine_clk_50, F(DATE) = &diskos_zine_bold_16 },
      .texture = THEME_TEX_PAPER, .title_case = THEME_CASE_UPPER, .fixed_accent = 1, .kit = &theme_kit_zine },
};

const theme_def_t *theme_def_find(const char *name){
    for(unsigned i = 0; name && i < sizeof DEFS / sizeof DEFS[0]; i++) if(!strcmp(DEFS[i].name, name)) return &DEFS[i];
    return NULL;
}
