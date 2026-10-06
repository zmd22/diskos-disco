/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
/* theme.c - the active theme's palette. Stage 1: only the default dark theme, whose values are exactly the colours
 * the UI used before roles existed (so it renders pixel-identically). The only lv_color_hex() calls in the app. */
#include "theme.h"
#include "theme_kit.h"
#include "screens.h"   /* ui_font_cjk: the CJK-capable user-text chains (built in ui.c) */
#include "config.h"
#include "i18n.h"
#include "theme_model.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include "lvgl/src/themes/lv_theme_private.h"   /* lv_theme_t layout: our overlay copies the base theme */

LV_FONT_DECLARE(font_icons_20)
LV_FONT_DECLARE(font_icons_28)
/* "something" faces (SIL OFL 1.1, texts in licenses/): IBM Plex Mono SemiBold/Bold converted as "diskos_mono"
 * (the licence reserves the name "Plex"), and Doto (weight 700, square dots) as "diskos_dot" */
LV_FONT_DECLARE(diskos_mono_14)
LV_FONT_DECLARE(diskos_mono_16)
LV_FONT_DECLARE(diskos_mono_18)
LV_FONT_DECLARE(diskos_mono_20)
LV_FONT_DECLARE(diskos_mono_24)
LV_FONT_DECLARE(diskos_mono_bold_12)
LV_FONT_DECLARE(diskos_dot_18)
LV_FONT_DECLARE(diskos_dot_28)

static const uint32_t DEFAULT_DARK[THEME_CLR_COUNT] = {
    [THEME_CLR_CANVAS]                 = 0x000000,
    [THEME_CLR_SCRIM]                  = 0x000000,
    [THEME_CLR_IMAGE_TINT]             = 0x000000,
    [THEME_CLR_SURFACE]                = 0x1C1C1E,
    [THEME_CLR_SURFACE_RAISED]         = 0x2C2C2E,
    [THEME_CLR_SURFACE_STRONG]         = 0x3A3A3C,
    [THEME_CLR_SURFACE_SELECTED]       = 0x242426,
    [THEME_CLR_ART_PLACEHOLDER]        = 0x1C1C1E,
    [THEME_CLR_LIST_PRESSED]           = 0x1C1C1E,
    [THEME_CLR_SURFACE_PRESSED]        = 0x2C2C2E,
    [THEME_CLR_RAISED_PRESSED]         = 0x3A3A3C,
    [THEME_CLR_BORDER]                 = 0x2C2C2E,
    [THEME_CLR_BORDER_STRONG]          = 0x3A3A3C,
    [THEME_CLR_OUTLINE_BRIGHT]         = 0xFFFFFF,
    [THEME_CLR_CONNECTED_SURFACE]      = 0x0A2A4A,
    [THEME_CLR_CONTROL_TRACK]          = 0x2C2C2E,
    [THEME_CLR_CONTROL_TRACK_STRONG]   = 0x3A3A3C,
    [THEME_CLR_CONTROL_FILL]           = 0xFFFFFF,
    [THEME_CLR_CONTROL_KNOB]           = 0xFFFFFF,
    [THEME_CLR_CONTROL_FILL_MUTED]     = 0x8E8E93,
    [THEME_CLR_ON_CONTROL]             = 0x3A3A3C,
    [THEME_CLR_CONTROL_OFF]            = 0x3A3A3C,
    [THEME_CLR_GRABBER]                = 0x5A5A5E,
    [THEME_CLR_PAGE_DOT_ACTIVE]        = 0xFFFFFF,
    [THEME_CLR_PAGE_DOT_INACTIVE]      = 0x48484A,
    [THEME_CLR_PAGE_DOT_DIM]           = 0x2C2C2E,
    [THEME_CLR_KEY_SURFACE]            = 0x202022,
    [THEME_CLR_KEY_SELECTED]           = 0x425A78,
    [THEME_CLR_KEY_TEXT]               = 0xF0F0F0,
    [THEME_CLR_KEY_PRESSED]            = 0x3A3A3C,
    [THEME_CLR_SWITCH_OFF]             = 0xE0E0E0,
    [THEME_CLR_SWITCH_ON]              = 0x2196F3,
    [THEME_CLR_SWITCH_KNOB]            = 0xFFFFFF,
    [THEME_CLR_INPUT_CURSOR]           = 0x212121,
    [THEME_CLR_INPUT_PLACEHOLDER]      = 0xBDBDBD,
    [THEME_CLR_ACTION_SURFACE]         = 0x2196F3,
    [THEME_CLR_ON_ACTION]              = 0xFFFFFF,
    [THEME_CLR_ACTION_SHADOW]          = 0x9E9E9E,
    [THEME_CLR_WIDGET_PRIMARY]         = 0x2196F3,
    [THEME_CLR_WIDGET_TRACK]           = 0xE0E0E0,
    [THEME_CLR_SIGNAL_ACTIVE]          = 0xFFFFFF,
    [THEME_CLR_SIGNAL_INACTIVE]        = 0x3A3A3C,
    [THEME_CLR_ART_WALL_FRONT]         = 0x2C2C2E,
    [THEME_CLR_ART_WALL_SIDE]          = 0x1E1E20,
    [THEME_CLR_DOT_GRID]               = 0x000000,
    [THEME_CLR_DECOR_1]               = 0x000000,
    [THEME_CLR_DECOR_2]               = 0x000000,
    [THEME_CLR_TEXT_PRIMARY]           = 0xFFFFFF,
    [THEME_CLR_TEXT_SECONDARY]         = 0xC7C7CC,
    [THEME_CLR_TEXT_MUTED]             = 0x8E8E93,
    [THEME_CLR_TEXT_DISABLED]          = 0x636366,
    [THEME_CLR_TEXT_FOOTNOTE]          = 0x6E6E73,
    [THEME_CLR_TEXT_LYRICS]            = 0xE5E5EA,
    [THEME_CLR_TEXT_SEPARATOR]         = 0x2C2C2E,
    [THEME_CLR_TEXT_HINT]              = 0x48484A,
    [THEME_CLR_ON_ACCENT]              = 0xFFFFFF,
    [THEME_CLR_ACCENT_PRIMARY]         = 0xFF375F,
    [THEME_CLR_ACCENT_EMPHASIS]        = 0xF23260,
    [THEME_CLR_ACCENT_IDLE]            = 0xC4C6CB,
    [THEME_CLR_STATUS_INFO]            = 0x0A84FF,
    [THEME_CLR_STATUS_SUCCESS]         = 0x34C759,
    [THEME_CLR_STATUS_DANGER]          = 0xFF453A,
    [THEME_CLR_STATUS_DANGER_STRONG]   = 0xFF6961,
    [THEME_CLR_STATUS_WARNING]         = 0xFF5E5B,
    [THEME_CLR_FOLDER_ICON]            = 0xE5C158,
    [THEME_CLR_DANGER_SURFACE]         = 0x2A1416,
    [THEME_CLR_DANGER_SURFACE_PRESSED] = 0x3A1C1E,
    [THEME_CLR_DANGER_BUTTON_SURFACE]  = 0x3A1416,
    [THEME_CLR_DANGER_PANEL_SURFACE]   = 0x3A1417,
    [THEME_CLR_FIXED_LASTFM]           = 0xD51007,
    [THEME_CLR_FIXED_ON_LASTFM]        = 0xFFFFFF,
    [THEME_CLR_FIXED_QR_DARK]          = 0x000000,
    [THEME_CLR_FIXED_QR_LIGHT]         = 0xFFFFFF,
    [THEME_CLR_FIXED_DEBUG_TOUCH]      = 0x00FF66,
    [THEME_CLR_FIXED_SPLASH_BG]        = 0x07080A,
    [THEME_CLR_FIXED_SPLASH_RING]      = 0x0B0C0F,
    [THEME_CLR_FIXED_SPLASH_1]         = 0xD9DCE2,
    [THEME_CLR_FIXED_SPLASH_2]         = 0x9EA3AD,
    [THEME_CLR_FIXED_SPLASH_WELL]      = 0x0C0D10,
    [THEME_CLR_FIXED_SPLASH_SPINDLE]   = 0x1D1F24,
    [THEME_CLR_FIXED_SPLASH_HUB_LINE]  = 0x787D87,
    [THEME_CLR_FIXED_SPLASH_HOLE]      = 0x050506,
    [THEME_CLR_FIXED_SPLASH_MARK]      = 0x2C3038,
    [THEME_CLR_FIXED_SPLASH_BAND_1]    = 0xFF5D7E,
    [THEME_CLR_FIXED_SPLASH_BAND_2]    = 0xFFC15D,
    [THEME_CLR_FIXED_SPLASH_BAND_3]    = 0x9CF07A,
    [THEME_CLR_FIXED_SPLASH_BAND_4]    = 0x5DC8FF,
    [THEME_CLR_FIXED_SPLASH_BAND_5]    = 0x9A73FF,
    [THEME_CLR_FIXED_SPLASH_BLACK_HI]  = 0x4A4C51,
    [THEME_CLR_FIXED_SPLASH_BLACK_LO]  = 0x232428,
    [THEME_CLR_FIXED_SPLASH_TURQ_HI]   = 0x9FE0E8,
    [THEME_CLR_FIXED_SPLASH_TURQ_LO]   = 0x5AB3BF,
    [THEME_CLR_FIXED_SPLASH_PINK_HI]   = 0xF4C8C8,
    [THEME_CLR_FIXED_SPLASH_PINK_LO]   = 0xDA9CA2,
    [THEME_CLR_FIXED_SPLASH_3]         = 0xF6F2F8,
    [THEME_CLR_FIXED_MEDIA_BLACK] = 0x000000,
    [THEME_CLR_FIXED_MEDIA_WHITE] = 0xFFFFFF,
    [THEME_CLR_FIXED_MEDIA_TRACK] = 0x2C2C2E,
    [THEME_CLR_FIXED_MEDIA_LABEL] = 0x0C0C0E,
    [THEME_CLR_FIXED_MEDIA_HUB] = 0x2A2A2E,
    [THEME_CLR_FIXED_MEDIA_SPINDLE] = 0x2E2A2A,
    [THEME_CLR_FIXED_MEDIA_LYRIC_CURRENT] = 0xF6F6F8,
    [THEME_CLR_FIXED_MEDIA_LYRIC_ADJACENT] = 0xA4A4AA,
    [THEME_CLR_FIXED_MEDIA_HINT] = 0x8E8E93,
    [THEME_CLR_FIXED_MEDIA_TITLE] = 0xE6E6EA,
    [THEME_CLR_FIXED_MEDIA_ARTIST] = 0xC7C7CC,
    [THEME_CLR_VOLUME_TRACK] = 0x46464A,
    [THEME_CLR_VOLUME_BADGE] = 0x141416,
    [THEME_CLR_VOLUME_TICK_MAJOR] = 0xECE8E0,
    [THEME_CLR_VOLUME_TICK_MINOR] = 0x96928B,
    [THEME_CLR_VOLUME_DIGITS] = 0xF8F5EE,
    [THEME_CLR_VOLUME_CAPTION] = 0xB0ACA4,
    [THEME_CLR_PAGE_DOT_QUIET] = 0x5A5A60,
    [THEME_CLR_SAVER_TITLE] = 0xC8C8CD,
    [THEME_CLR_SAVER_SUBTITLE] = 0x78787E,
    [THEME_CLR_SAVER_ARTIST] = 0x68686E,
    [THEME_CLR_SAVER_RING] = 0x222224,
    [THEME_CLR_SAVER_DISC] = 0x28282C,
    [THEME_CLR_EQ_ZERO] = 0xBAB4AA,
    [THEME_CLR_EQ_SLOT] = 0x282624,
    [THEME_CLR_EQ_CAP_DISABLED] = 0x96928B,
    [THEME_CLR_EQ_FADER] = 0xC8C4BC,
    [THEME_CLR_EQ_FADER_DISABLED] = 0xDCD8D0,
    [THEME_CLR_EQ_READONLY] = 0x808084,
    [THEME_CLR_EQ_CHIP] = 0x28282C,
    [THEME_CLR_BOOK_HEARD] = 0x6E6E74,
    [THEME_CLR_WEATHER_SUN] = 0xFFD60A,
    [THEME_CLR_WEATHER_MOON] = 0xA8B4E0,
    [THEME_CLR_WEATHER_RAIN] = 0x64B5F6,
    [THEME_CLR_WEATHER_BORDER] = 0x1E1E20,
    [THEME_CLR_TOAST_SURFACE] = 0x18181A,
    [THEME_CLR_TOAST_BORDER] = 0x38383C,
    [THEME_CLR_TOAST_ORB] = 0x8E897F,
    [THEME_CLR_ORBIT_PRESSED] = 0x2A2927,
    [THEME_CLR_ORBIT_SHADOW] = 0xA8A296,
    [THEME_CLR_ORBIT_POINTER] = 0xC8C4BC,
    [THEME_CLR_ORBIT_HUB] = 0x121214,
    [THEME_CLR_USAGE_GRID] = 0x28282A,
    [THEME_CLR_USAGE_TEXT] = 0xF0F0F5,
    [THEME_CLR_USAGE_AMBER] = 0xFFD60A,
    [THEME_CLR_USAGE_FAINT] = 0x1E1E20,
    [THEME_CLR_QUEUE_DRAG] = 0x3A3A3E,
    [THEME_CLR_QUEUE_HANDLE] = 0x78787E,
    [THEME_CLR_PRIMARY_PRESSED] = 0xC84C12,
    [THEME_CLR_SWITCH_SELECTED] = 0xF3C7AE,
    [THEME_CLR_DANGER_DIALOG] = 0x3A1416,
    [THEME_CLR_POSTER_SUBTITLE] = 0xE6E6EA,
    [THEME_CLR_POSTER_SIDE] = 0xF6F6F8,
    [THEME_CLR_USAGE_CHARGE_BAND] = 0x0F3A1B,
    [THEME_CLR_USAGE_BAND] = 0x2E2E32,
};

/* The default theme's LIGHT variant (Outdoor mode forces it). Every text/control pair passes the contrast rules in
 * tests/release (text >= 4.5:1 on the surfaces it sits on, quiet text/controls >= 3:1). */
static const uint32_t DEFAULT_LIGHT[THEME_CLR_COUNT] = {
    [THEME_CLR_CANVAS]                 = 0xFFFFFF,
    [THEME_CLR_SCRIM]                  = 0x000000,
    [THEME_CLR_IMAGE_TINT]             = 0xFFFFFF,   /* lightens a background image so dark text stays readable */
    [THEME_CLR_SURFACE]                = 0xF2F2F7,
    [THEME_CLR_SURFACE_RAISED]         = 0xE5E5EA,
    [THEME_CLR_SURFACE_STRONG]         = 0xD1D1D6,
    [THEME_CLR_SURFACE_SELECTED]       = 0xE3E9F4,
    [THEME_CLR_ART_PLACEHOLDER]        = 0xE5E5EA,
    [THEME_CLR_LIST_PRESSED]           = 0xE5E5EA,
    [THEME_CLR_SURFACE_PRESSED]        = 0xD1D1D6,
    [THEME_CLR_RAISED_PRESSED]         = 0xC7C7CC,
    [THEME_CLR_BORDER]                 = 0xC7C7CC,
    [THEME_CLR_BORDER_STRONG]          = 0xAEAEB2,
    [THEME_CLR_OUTLINE_BRIGHT]         = 0x000000,
    [THEME_CLR_CONNECTED_SURFACE]      = 0xDCEBFF,
    [THEME_CLR_CONTROL_TRACK]          = 0xD1D1D6,
    [THEME_CLR_CONTROL_TRACK_STRONG]   = 0xC7C7CC,
    [THEME_CLR_CONTROL_FILL]           = 0x3A3A3C,
    [THEME_CLR_CONTROL_KNOB]           = 0x1C1C1E,
    [THEME_CLR_CONTROL_FILL_MUTED]     = 0x6C6C70,
    [THEME_CLR_ON_CONTROL]             = 0xF2F2F7,
    [THEME_CLR_CONTROL_OFF]            = 0xD1D1D6,
    [THEME_CLR_GRABBER]                = 0x7A7A7F,
    [THEME_CLR_PAGE_DOT_ACTIVE]        = 0x1C1C1E,
    [THEME_CLR_PAGE_DOT_INACTIVE]      = 0xAEAEB2,
    [THEME_CLR_PAGE_DOT_DIM]           = 0xC7C7CC,
    [THEME_CLR_KEY_SURFACE]            = 0xF2F2F7,
    [THEME_CLR_KEY_SELECTED]           = 0xB9D2F0,
    [THEME_CLR_KEY_TEXT]               = 0x1C1C1E,
    [THEME_CLR_KEY_PRESSED]            = 0xC7C7CC,
    [THEME_CLR_SWITCH_OFF]             = 0x87878C,
    [THEME_CLR_SWITCH_ON]              = 0xC8184A,
    [THEME_CLR_SWITCH_KNOB]            = 0xFFFFFF,
    [THEME_CLR_INPUT_CURSOR]           = 0x000000,
    [THEME_CLR_INPUT_PLACEHOLDER]      = 0x6C6C70,
    [THEME_CLR_ACTION_SURFACE]         = 0xC8184A,
    [THEME_CLR_ON_ACTION]              = 0xFFFFFF,
    [THEME_CLR_ACTION_SHADOW]          = 0xAEAEB2,
    [THEME_CLR_WIDGET_PRIMARY]         = 0xC8184A,
    [THEME_CLR_WIDGET_TRACK]           = 0xD1D1D6,
    [THEME_CLR_SIGNAL_ACTIVE]          = 0x1C1C1E,
    [THEME_CLR_SIGNAL_INACTIVE]        = 0x7C7C80,
    [THEME_CLR_ART_WALL_FRONT]         = 0xE5E5EA,
    [THEME_CLR_ART_WALL_SIDE]          = 0xD1D1D6,
    [THEME_CLR_DOT_GRID]               = 0xFFFFFF,
    [THEME_CLR_DECOR_1]               = 0xFFFFFF,
    [THEME_CLR_DECOR_2]               = 0xFFFFFF,
    [THEME_CLR_TEXT_PRIMARY]           = 0x000000,
    [THEME_CLR_TEXT_SECONDARY]         = 0x3A3A3C,
    [THEME_CLR_TEXT_MUTED]             = 0x5E5E63,
    [THEME_CLR_TEXT_DISABLED]          = 0x8A8A8F,
    [THEME_CLR_TEXT_FOOTNOTE]          = 0x6C6C70,
    [THEME_CLR_TEXT_LYRICS]            = 0x1C1C1E,
    [THEME_CLR_TEXT_SEPARATOR]         = 0x8A8A8F,
    [THEME_CLR_TEXT_HINT]              = 0x8A8A8F,
    [THEME_CLR_ON_ACCENT]              = 0xFFFFFF,
    [THEME_CLR_ACCENT_PRIMARY]         = 0xC8184A,
    [THEME_CLR_ACCENT_EMPHASIS]        = 0xC0144A,
    [THEME_CLR_ACCENT_IDLE]            = 0x6C6C70,
    [THEME_CLR_STATUS_INFO]            = 0x0059B8,
    [THEME_CLR_STATUS_SUCCESS]         = 0x1B7A34,
    [THEME_CLR_STATUS_DANGER]          = 0xC4121E,
    [THEME_CLR_STATUS_DANGER_STRONG]   = 0xA50E18,
    [THEME_CLR_STATUS_WARNING]         = 0xA93C00,
    [THEME_CLR_FOLDER_ICON]            = 0x8A6400,
    [THEME_CLR_DANGER_SURFACE]         = 0xFDE8EA,
    [THEME_CLR_DANGER_SURFACE_PRESSED] = 0xFADCDF,
    [THEME_CLR_DANGER_BUTTON_SURFACE]  = 0xFAD9DC,
    [THEME_CLR_DANGER_PANEL_SURFACE]   = 0xFDE8EA,
    [THEME_CLR_FIXED_LASTFM]           = 0xD51007,
    [THEME_CLR_FIXED_ON_LASTFM]        = 0xFFFFFF,
    [THEME_CLR_FIXED_QR_DARK]          = 0x000000,
    [THEME_CLR_FIXED_QR_LIGHT]         = 0xFFFFFF,
    [THEME_CLR_FIXED_DEBUG_TOUCH]      = 0x00FF66,
    [THEME_CLR_FIXED_SPLASH_BG]        = 0x07080A,
    [THEME_CLR_FIXED_SPLASH_RING]      = 0x0B0C0F,
    [THEME_CLR_FIXED_SPLASH_1]         = 0xD9DCE2,
    [THEME_CLR_FIXED_SPLASH_2]         = 0x9EA3AD,
    [THEME_CLR_FIXED_SPLASH_WELL]      = 0x0C0D10,
    [THEME_CLR_FIXED_SPLASH_SPINDLE]   = 0x1D1F24,
    [THEME_CLR_FIXED_SPLASH_HUB_LINE]  = 0x787D87,
    [THEME_CLR_FIXED_SPLASH_HOLE]      = 0x050506,
    [THEME_CLR_FIXED_SPLASH_MARK]      = 0x2C3038,
    [THEME_CLR_FIXED_SPLASH_BAND_1]    = 0xFF5D7E,
    [THEME_CLR_FIXED_SPLASH_BAND_2]    = 0xFFC15D,
    [THEME_CLR_FIXED_SPLASH_BAND_3]    = 0x9CF07A,
    [THEME_CLR_FIXED_SPLASH_BAND_4]    = 0x5DC8FF,
    [THEME_CLR_FIXED_SPLASH_BAND_5]    = 0x9A73FF,
    [THEME_CLR_FIXED_SPLASH_BLACK_HI]  = 0x4A4C51,
    [THEME_CLR_FIXED_SPLASH_BLACK_LO]  = 0x232428,
    [THEME_CLR_FIXED_SPLASH_TURQ_HI]   = 0x9FE0E8,
    [THEME_CLR_FIXED_SPLASH_TURQ_LO]   = 0x5AB3BF,
    [THEME_CLR_FIXED_SPLASH_PINK_HI]   = 0xF4C8C8,
    [THEME_CLR_FIXED_SPLASH_PINK_LO]   = 0xDA9CA2,
    [THEME_CLR_FIXED_SPLASH_3]         = 0xF6F2F8,
    [THEME_CLR_FIXED_MEDIA_BLACK] = 0x000000,
    [THEME_CLR_FIXED_MEDIA_WHITE] = 0xFFFFFF,
    [THEME_CLR_FIXED_MEDIA_TRACK] = 0x2C2C2E,
    [THEME_CLR_FIXED_MEDIA_LABEL] = 0x0C0C0E,
    [THEME_CLR_FIXED_MEDIA_HUB] = 0x2A2A2E,
    [THEME_CLR_FIXED_MEDIA_SPINDLE] = 0x2E2A2A,
    [THEME_CLR_FIXED_MEDIA_LYRIC_CURRENT] = 0xF6F6F8,
    [THEME_CLR_FIXED_MEDIA_LYRIC_ADJACENT] = 0xA4A4AA,
    [THEME_CLR_FIXED_MEDIA_HINT] = 0x8E8E93,
    [THEME_CLR_FIXED_MEDIA_TITLE] = 0xE6E6EA,
    [THEME_CLR_FIXED_MEDIA_ARTIST] = 0xC7C7CC,
    [THEME_CLR_VOLUME_TRACK] = 0xC7C7CC,
    [THEME_CLR_VOLUME_BADGE] = 0xF2F2F7,
    [THEME_CLR_VOLUME_TICK_MAJOR] = 0x000000,
    [THEME_CLR_VOLUME_TICK_MINOR] = 0x8A8A8F,
    [THEME_CLR_VOLUME_DIGITS] = 0x000000,
    [THEME_CLR_VOLUME_CAPTION] = 0x3A3A3C,
    [THEME_CLR_PAGE_DOT_QUIET] = 0xAEAEB2,
    [THEME_CLR_SAVER_TITLE] = 0x000000,
    [THEME_CLR_SAVER_SUBTITLE] = 0x3A3A3C,
    [THEME_CLR_SAVER_ARTIST] = 0x5E5E63,
    [THEME_CLR_SAVER_RING] = 0xD1D1D6,
    [THEME_CLR_SAVER_DISC] = 0xE5E5EA,
    [THEME_CLR_EQ_ZERO] = 0xC7C7CC,
    [THEME_CLR_EQ_SLOT] = 0xD1D1D6,
    [THEME_CLR_EQ_CAP_DISABLED] = 0x8A8A8F,
    [THEME_CLR_EQ_FADER] = 0x3A3A3C,
    [THEME_CLR_EQ_FADER_DISABLED] = 0x6C6C70,
    [THEME_CLR_EQ_READONLY] = 0x8A8A8F,
    [THEME_CLR_EQ_CHIP] = 0xE5E5EA,
    [THEME_CLR_BOOK_HEARD] = 0x6C6C70,
    [THEME_CLR_WEATHER_SUN] = 0xA93C00,
    [THEME_CLR_WEATHER_MOON] = 0x3A3A3C,
    [THEME_CLR_WEATHER_RAIN] = 0x0059B8,
    [THEME_CLR_WEATHER_BORDER] = 0xC7C7CC,
    [THEME_CLR_TOAST_SURFACE] = 0xE5E5EA,
    [THEME_CLR_TOAST_BORDER] = 0xC7C7CC,
    [THEME_CLR_TOAST_ORB] = 0x8A8A8F,
    [THEME_CLR_ORBIT_PRESSED] = 0xD1D1D6,
    [THEME_CLR_ORBIT_SHADOW] = 0xAEAEB2,
    [THEME_CLR_ORBIT_POINTER] = 0x7A7A7F,
    [THEME_CLR_ORBIT_HUB] = 0xD1D1D6,
    [THEME_CLR_USAGE_GRID] = 0xC7C7CC,
    [THEME_CLR_USAGE_TEXT] = 0x000000,
    [THEME_CLR_USAGE_AMBER] = 0xA93C00,
    [THEME_CLR_USAGE_FAINT] = 0xC7C7CC,
    [THEME_CLR_QUEUE_DRAG] = 0xE3E9F4,
    [THEME_CLR_QUEUE_HANDLE] = 0x8A8A8F,
    [THEME_CLR_PRIMARY_PRESSED] = 0xC0144A,
    [THEME_CLR_SWITCH_SELECTED] = 0xC8184A,
    [THEME_CLR_DANGER_DIALOG] = 0xFAD9DC,
    [THEME_CLR_POSTER_SUBTITLE] = 0x3A3A3C,
    [THEME_CLR_POSTER_SIDE] = 0x000000,
    [THEME_CLR_USAGE_CHARGE_BAND] = 0x1B7A34,
    [THEME_CLR_USAGE_BAND] = 0xD1D1D6,
};

#include "theme_presets.inc"   /* generated: tools/gen_theme_presets.py */

typedef struct { const char *name; const uint32_t *dark, *light; } theme_preset_t;
static uint32_t FORK_RING[2][THEME_CLR_COUNT], FORK_BRAUN[2][THEME_CLR_COUNT], FORK_DISCO[2][THEME_CLR_COUNT];
static const theme_preset_t PRESETS[] = {
    { "Default", DEFAULT_DARK, DEFAULT_LIGHT },
    THEME_PRESETS_GENERATED
    { "Ring", FORK_RING[0], FORK_RING[1] },
    { "Braun", FORK_BRAUN[0], FORK_BRAUN[1] },
    { "Disco", FORK_DISCO[0], FORK_DISCO[1] },
};
_Static_assert(sizeof PRESETS / sizeof PRESETS[0] == THEME_PRESET_COUNT, "THEME_PRESET_COUNT (theme.h) is stale");
const char *const theme_preset_names[THEME_PRESET_COUNT] = { "Default", THEME_PRESET_NAMES_GENERATED, "Ring", "Braun", "Disco" };
static int g_preset;
static uint32_t g_custom[2][THEME_CLR_COUNT];      /* the saved custom theme, fixed roles filled from Default */
static int g_custom_on, g_custom_fixed_accent;
static const theme_def_t *g_def;                     /* the active unique theme, NULL = Default / custom */
static unsigned g_traits;
/* the active theme's fonts, as copies whose fallback is the matching international/CJK chain (symbols, accents, CJK):
 * text a face lacks renders in the chain instead of disappearing */
static lv_font_t g_tf[THEME_FONT_COUNT];
static const lv_font_t *g_tfp[THEME_FONT_COUNT];
static int role_px(int role){
    static const unsigned char PX[THEME_FONT_COUNT] = {
        [THEME_FONT_UI_10] = 10, [THEME_FONT_UI_12] = 12, [THEME_FONT_UI_14] = 14, [THEME_FONT_UI_16] = 16,
        [THEME_FONT_UI_18] = 18, [THEME_FONT_UI_20] = 20, [THEME_FONT_UI_22] = 22, [THEME_FONT_UI_24] = 24,
        [THEME_FONT_UI_28] = 28, [THEME_FONT_UI_32] = 32, [THEME_FONT_UI_36] = 36, [THEME_FONT_UI_40] = 40,
        [THEME_FONT_USER_14] = 14, [THEME_FONT_USER_16] = 16, [THEME_FONT_USER_18] = 18, [THEME_FONT_USER_20] = 20,
        [THEME_FONT_HEADER] = 28, [THEME_FONT_CLOCK] = 28, [THEME_FONT_SAVER_CLOCK] = 40, [THEME_FONT_DATE] = 18,
        [THEME_FONT_ICON_20] = 20, [THEME_FONT_ICON_28] = 28, [THEME_FONT_HEADER_SM] = 22,
        [THEME_FONT_UI_8] = 8, [THEME_FONT_UI_26] = 26, [THEME_FONT_UI_30] = 30, [THEME_FONT_UI_34] = 34, [THEME_FONT_UI_38] = 38, [THEME_FONT_UI_42] = 42, [THEME_FONT_UI_44] = 44, [THEME_FONT_UI_46] = 46, [THEME_FONT_UI_48] = 48 };
    return PX[role];
}
static void theme_fonts_install(void){
    for(int r = 0; r < THEME_FONT_COUNT; r++){
        g_tfp[r] = NULL;
        if(!g_def || !g_def->font[r]) continue;
        int px = role_px(r);
        g_tf[r] = *g_def->font[r];
        /* the chain of the role's size (the largest chain is 20 px); 12 px and under: Montserrat 12 (symbols) */
        g_tf[r].fallback = px <= 12 ? &lv_font_montserrat_12 : ui_font_cjk(px > 20 ? 20 : px);
        g_tfp[r] = &g_tf[r];
    }
    /* a text size the theme's set lacks takes the theme's nearest own face: otherwise Default's face shows through
     * (the weather's "Fetching..." in Montserrat on a monospace theme) */
    static const theme_font_role_t GAP[][3] = { { THEME_FONT_UI_22, THEME_FONT_UI_24, THEME_FONT_UI_20 },
                                                { THEME_FONT_UI_10, THEME_FONT_UI_12, THEME_FONT_UI_14 } };
    if(g_def) for(unsigned i = 0; i < sizeof GAP / sizeof GAP[0]; i++)
        if(!g_tfp[GAP[i][0]]) g_tfp[GAP[i][0]] = g_tfp[GAP[i][1]] ? g_tfp[GAP[i][1]] : g_tfp[GAP[i][2]];
}
const theme_def_t *theme_def(void){ return g_def; }
int theme_screen_plain(unsigned which){ return g_def && (g_def->plain & which) != 0; }
const lv_font_t *theme_font_smaller(const lv_font_t *f){   /* the theme's next smaller text face (NULL = none) */
    static const theme_font_role_t R[] = { THEME_FONT_UI_24, THEME_FONT_UI_20, THEME_FONT_UI_18, THEME_FONT_UI_16,
                                           THEME_FONT_UI_14, THEME_FONT_UI_12 };
    if(!g_def || !f) return NULL;
    int h = lv_font_get_line_height(f);
    for(unsigned i = 0; i < sizeof R / sizeof R[0]; i++)
        if(g_tfp[R[i]] && lv_font_get_line_height(g_tfp[R[i]]) < h) return g_tfp[R[i]];
    return NULL;
}
/* A label still in one of Default's Montserrat text sizes, under a theme with its own faces: the theme's face at the
 * nearest role size (NULL = keep). Glyph-only labels are the caller's to skip (the icons live in Montserrat). */
const lv_font_t *theme_font_substitute(const lv_font_t *f){
    static const struct { const lv_font_t *m; theme_font_role_t r; } T[] = {
        { &lv_font_montserrat_12, THEME_FONT_UI_12 }, { &lv_font_montserrat_14, THEME_FONT_UI_14 },
        { &lv_font_montserrat_16, THEME_FONT_UI_16 }, { &lv_font_montserrat_18, THEME_FONT_UI_18 },
        { &lv_font_montserrat_20, THEME_FONT_UI_20 }, { &lv_font_montserrat_22, THEME_FONT_UI_24 },
        { &lv_font_montserrat_24, THEME_FONT_UI_24 }, { &lv_font_montserrat_28, THEME_FONT_UI_24 },
        { &lv_font_montserrat_32, THEME_FONT_UI_24 }, { &lv_font_montserrat_36, THEME_FONT_UI_24 },
        { &lv_font_montserrat_40, THEME_FONT_UI_24 } };
    if(!g_def || !f) return NULL;
    for(unsigned i = 0; i < sizeof T / sizeof T[0]; i++)
        if(T[i].m == f && g_tfp[T[i].r]) return g_tfp[T[i].r];
    return NULL;
}
/* Load the saved custom theme into g_custom. Any problem (none saved, unreadable, invalid, future schema) -> 0 and
 * the caller uses Default; the file itself is never changed here. */
static int load_custom(void){
    static theme_model_t m;
    char err[128];
    if(theme_model_load(THEME_CUSTOM_PATH, &m, err, sizeof err) != THEME_OK){
        fprintf(stderr, "theme: custom theme not used: %s\n", err);
        return 0;
    }
    for(int v = 0; v < 2; v++)
        for(int r = 0; r < THEME_CLR_COUNT; r++)
            g_custom[v][r] = theme_role_key(r) ? m.pal[v][r] : DEFAULT_DARK[r];   /* keyless = fixed identity */
    g_custom_fixed_accent = m.accent_fixed;
    return 1;
}

static const uint32_t *g_pal = DEFAULT_DARK;

static int g_variant;   /* 0 dark, 1 light */
static int g_outdoor;   /* Outdoor mode was on at startup */

/* Called once at startup, after the config is loaded and before any screen is built (a theme change re-launches
 * the UI, so the palette never changes under live objects). Outdoor mode forces the light variant. */
static void theme_install_lvgl(void);
/* Preserve upstream numeric IDs; fork presets are appended. */
static void fork_palettes_init(void){
    static const uint32_t base[3][9] = {
        {0x000000,0x1C1C1E,0x2C2C2E,0x3A3A3C,0xFFFFFF,0xAEAEB2,0x636366,0xE4122C,0x8E8E93},
        {0xECE8E0,0xDED8CD,0xCFC8BC,0xDED8CD,0x1C1B19,0x68645E,0x96928B,0xE85A16,0x7A766F},
        {0x1F1E1C,0x34332F,0x3E3D39,0x34332F,0xECE8E0,0xA8A39A,0x77736C,0xE85A16,0x8A867E}
    };
    for(int v=0;v<2;v++){
        memcpy(FORK_RING[v], v ? DEFAULT_LIGHT : DEFAULT_DARK, sizeof DEFAULT_DARK);
        memcpy(FORK_BRAUN[v], v ? DEFAULT_LIGHT : DEFAULT_DARK, sizeof DEFAULT_DARK);
        for(int family=0;family<2;family++){
            uint32_t *pal = family ? FORK_BRAUN[v] : FORK_RING[v];
            const uint32_t *b = base[family ? (v ? 1 : 2) : 0];
            pal[THEME_CLR_CANVAS]=b[0]; pal[THEME_CLR_SURFACE]=b[1];
            pal[THEME_CLR_SURFACE_RAISED]=b[2]; pal[THEME_CLR_CONTROL_TRACK]=b[3];
            pal[THEME_CLR_TEXT_PRIMARY]=b[4]; pal[THEME_CLR_TEXT_SECONDARY]=b[5];
            pal[THEME_CLR_TEXT_DISABLED]=b[6]; pal[THEME_CLR_TEXT_MUTED]=b[8];
            pal[THEME_CLR_ACCENT_PRIMARY]=pal[THEME_CLR_ACCENT_EMPHASIS]=pal[THEME_CLR_ACCENT_IDLE]=b[7];
            pal[THEME_CLR_ON_ACCENT]=0xFFFFFF;
            pal[THEME_CLR_TEXT_LYRICS] = family ? (v ? 0x55524C : 0xC8C4BB) : 0xC7C7CC;
        }
        if(v){
            uint32_t *p=FORK_RING[v];
            p[THEME_CLR_CANVAS]=0xF5F4EE;p[THEME_CLR_SURFACE]=0xE8E8E0;p[THEME_CLR_SURFACE_RAISED]=0xDDDED5;
            p[THEME_CLR_TEXT_PRIMARY]=0x242622;p[THEME_CLR_TEXT_SECONDARY]=0x393D35;p[THEME_CLR_TEXT_MUTED]=0x53584E;
            p[THEME_CLR_TEXT_LYRICS]=0x393D35;p[THEME_CLR_TEXT_DISABLED]=0x999E93;
            p[THEME_CLR_SURFACE_SELECTED]=p[THEME_CLR_LIST_PRESSED]=0xDADDD1;
            p[THEME_CLR_CONTROL_TRACK]=0xDDDED5;
            p[THEME_CLR_ACCENT_PRIMARY]=p[THEME_CLR_ACCENT_EMPHASIS]=p[THEME_CLR_ACCENT_IDLE]=0xA6CF22;
            p[THEME_CLR_ON_ACCENT]=0x242622;
        }
        FORK_BRAUN[v][THEME_CLR_VOLUME_TICK_MAJOR] = v ? 0xECE8E0 : 0x2A2926;
        FORK_BRAUN[v][THEME_CLR_VOLUME_TICK_MINOR] = v ? 0x96928B : 0x6A6862;
        FORK_BRAUN[v][THEME_CLR_VOLUME_DIGITS] = v ? 0xF8F5EE : 0x1C1B19;
        FORK_BRAUN[v][THEME_CLR_VOLUME_CAPTION] = v ? 0xB0ACA4 : 0x55534E;
        FORK_BRAUN[v][THEME_CLR_EQ_ZERO] = v ? 0xBAB4AA : 0x55534E;
        FORK_BRAUN[v][THEME_CLR_EQ_SLOT] = v ? 0x282624 : 0x0E0E0D;
        FORK_BRAUN[v][THEME_CLR_EQ_CAP_DISABLED] = v ? 0x96928B : 0x6A6862;
        FORK_BRAUN[v][THEME_CLR_EQ_FADER] = v ? 0xC8C4BC : 0x4A4945;
        FORK_BRAUN[v][THEME_CLR_EQ_FADER_DISABLED] = v ? 0xDCD8D0 : 0x8A867E;
        FORK_BRAUN[v][THEME_CLR_TOAST_SURFACE] = v ? 0xF5F2EC : 0x2A2926;
        FORK_BRAUN[v][THEME_CLR_TOAST_BORDER] = v ? 0xC4BEB4 : 0x3A3935;
        FORK_BRAUN[v][THEME_CLR_TOAST_ORB] = v ? 0x8E897F : 0x77736C;
        FORK_BRAUN[v][THEME_CLR_ORBIT_PRESSED] = v ? 0x2A2927 : 0x2A2927;
        FORK_BRAUN[v][THEME_CLR_ORBIT_SHADOW] = v ? 0xA8A296 : 0xA8A296;
        FORK_BRAUN[v][THEME_CLR_ORBIT_POINTER] = v ? 0xC8C4BC : 0xC8C4BC;
        FORK_BRAUN[v][THEME_CLR_ORBIT_HUB] = v ? 0xF5F2EC : 0x2A2926;
        FORK_BRAUN[v][THEME_CLR_USAGE_GRID] = v ? 0xD6D0C6 : 0x3A3935;
        FORK_BRAUN[v][THEME_CLR_USAGE_TEXT] = v ? 0x1C1B19 : 0xECE8E0;
        FORK_BRAUN[v][THEME_CLR_USAGE_AMBER] = v ? 0x68645E : 0xA8A39A;
        FORK_BRAUN[v][THEME_CLR_USAGE_FAINT] = v ? 0xE4DED4 : 0x2A2926;
        FORK_BRAUN[v][THEME_CLR_PRIMARY_PRESSED] = v ? 0xC84C12 : 0xC84C12;
        FORK_BRAUN[v][THEME_CLR_SWITCH_SELECTED] = v ? 0xF3C7AE : 0x6A3518;
        FORK_BRAUN[v][THEME_CLR_ART_PLACEHOLDER] = 0x1C1C1E;
        FORK_BRAUN[v][THEME_CLR_USAGE_CHARGE_BAND]=v ? 0xC6DEC9 : 0x22352A;
        FORK_BRAUN[v][THEME_CLR_USAGE_BAND]=v ? 0xD2CCC2 : 0x3E3D39;
        /* Braun component colours are palette roles too: the renderer never owns a second palette. */
        static const struct { theme_color_role_t role; uint32_t light, dark; } parts[] = {
            {THEME_CLR_SURFACE_STRONG,0xF5F2EC,0x2A2926},
            {THEME_CLR_DOT_GRID,0xC4BEB4,0x3A3935},
            {THEME_CLR_BORDER,0xD6D0C6,0x3A3935},
            {THEME_CLR_CONTROL_KNOB,0x3A3936,0xC9C5BD},
            {THEME_CLR_CONTROL_FILL,0x54524E,0xE2DED6},
            {THEME_CLR_ON_CONTROL,0xFFFFFF,0x1C1B19},
            {THEME_CLR_GRABBER,0xC8C4BC,0x5E5C57},
            {THEME_CLR_SURFACE_PRESSED,0xCFC8BC,0x46453F},
            {THEME_CLR_ACTION_SHADOW,0xA8A296,0x000000}
        };
        for(unsigned i=0;i<sizeof parts/sizeof *parts;i++)
            FORK_BRAUN[v][parts[i].role]=v ? parts[i].light : parts[i].dark;
    }
}
/* Disco: the Ring palette re-tuned for text over the album cover (dark: white on a darkened cover; light: dark grey
 * on a lightened one). Surfaces are drawn translucent over the cover by disco.c; the accent defaults to cyan. */
static void disco_palettes_init(void){
    for(int v=0;v<2;v++){
        memcpy(FORK_DISCO[v], FORK_RING[v], sizeof FORK_DISCO[v]);
        uint32_t *p=FORK_DISCO[v];
        if(!v){
            p[THEME_CLR_CANVAS]=0x05070A;p[THEME_CLR_SURFACE]=0x10151B;p[THEME_CLR_SURFACE_RAISED]=0x1E252E;
            p[THEME_CLR_CONTROL_TRACK]=0x3A424C;p[THEME_CLR_TEXT_PRIMARY]=0xFFFFFF;p[THEME_CLR_TEXT_SECONDARY]=0xB8C2CC;
            p[THEME_CLR_TEXT_MUTED]=0x9AA3AD;p[THEME_CLR_TEXT_LYRICS]=0xC8D0D8;p[THEME_CLR_TEXT_DISABLED]=0x6B737D;
            p[THEME_CLR_ACCENT_PRIMARY]=p[THEME_CLR_ACCENT_EMPHASIS]=p[THEME_CLR_ACCENT_IDLE]=0x22D3EE;
            p[THEME_CLR_ON_ACCENT]=0x05070A;
        } else {
            p[THEME_CLR_CANVAS]=0xF4F2EE;p[THEME_CLR_SURFACE]=0xFFFFFF;p[THEME_CLR_SURFACE_RAISED]=0xE6E8EB;
            p[THEME_CLR_CONTROL_TRACK]=0xC9CDD2;p[THEME_CLR_TEXT_PRIMARY]=0x1E2228;p[THEME_CLR_TEXT_SECONDARY]=0x4A515A;
            p[THEME_CLR_TEXT_MUTED]=0x5A616B;p[THEME_CLR_TEXT_LYRICS]=0x3A4048;p[THEME_CLR_TEXT_DISABLED]=0x9AA0A8;
            p[THEME_CLR_SURFACE_SELECTED]=p[THEME_CLR_LIST_PRESSED]=0xDDE1E6;
            p[THEME_CLR_ACCENT_PRIMARY]=p[THEME_CLR_ACCENT_EMPHASIS]=p[THEME_CLR_ACCENT_IDLE]=0x0B8FAA;   /* cyan, darkened to read on light */
            p[THEME_CLR_ON_ACCENT]=0xFFFFFF;
        }
    }
}
static void fork_config_migrate(void){
    if(cfg_get_int("fork_theme_migrated",0)) return;
    cfg_begin();
    if(cfg_get_int("theme_preset",-1)<0){
        int old=cfg_get_int("ui_theme",-1);
        cfg_set_int("theme_preset",old==0 ? THEME_PRESET_RING : THEME_PRESET_BRAUN);
        cfg_set_int("theme_variant",(old==0 || old==2) ? 0 : 1);
    } else if(cfg_get_int("theme_variant",-1)<0){
        /* Upstream configurations without this key used the dark variant. */
        cfg_set_int("theme_variant",0);
    }
    cfg_set_int("fork_theme_migrated",1);
    if(cfg_commit()) fprintf(stderr,"theme: migration save failed\n");
}

void theme_init(void){
    fork_palettes_init();
    disco_palettes_init();
    fork_config_migrate();
    int outdoor = cfg_get_int("outdoor", 0) == 1;
    g_outdoor = outdoor;
    g_variant = (outdoor || cfg_get_int("theme_variant", 1) == 1) ? 1 : 0;
    g_preset = cfg_get_int("theme_preset", THEME_PRESET_BRAUN);
    if(theme_auto_supported() && cfg_get_int("theme_auto",0) && !outdoor){
        time_t now=time(NULL); struct tm tm; localtime_r(&now,&tm);
        g_variant = !(tm.tm_hour >= 20 || tm.tm_hour < 7);
    }
    g_custom_on = 0;
    g_def = NULL;
    if(g_preset == THEME_PRESET_CUSTOM && load_custom()){
        g_custom_on = 1;
        g_pal = g_custom[g_variant];
    } else if(g_preset == THEME_PRESET_CUSTOM){
        g_preset = 0;                                   /* use Default this run; KEEP the choice (the file may be back) */
        g_pal = g_variant ? DEFAULT_LIGHT : DEFAULT_DARK;
    } else {
        if(g_preset < 0 || g_preset >= THEME_PRESET_COUNT){ g_preset = 0; cfg_set_int("theme_preset", 0); }   /* Settings shows it */
        g_pal = g_variant ? PRESETS[g_preset].light : PRESETS[g_preset].dark;
        g_def = theme_def_find(PRESETS[g_preset].name);   /* NULL for Default */
    }
    /* fixed identities (brand, QR, splash, debug) are the same in every palette; a palette that changes one is
     * refused as a whole rather than half-applied */
    for(int r = THEME_CLR_FIXED_LASTFM; r <= THEME_CLR_FIXED_MEDIA_ARTIST; r++)
        if(g_pal[r] != DEFAULT_DARK[r]){ fprintf(stderr, "theme: palette changes fixed role %d, using dark\n", r);
                                         g_pal = DEFAULT_DARK; g_variant = 0; g_preset = 0; g_custom_on = 0; g_def = NULL; break; }
    g_traits = g_def ? g_def->traits : 0;
    if(g_def && g_def->texture != THEME_TEX_NONE) g_traits |= 1u << THEME_TRAIT_DOT_GRID;   /* textured screens */
    theme_fonts_install();
    theme_install_lvgl();
}
int theme_variant(void){ return g_variant; }
int theme_outdoor(void){ return g_outdoor; }
int theme_preset(void){ return g_preset; }
int theme_custom_active(void){ return g_custom_on; }
lv_color_t theme_preset_color(int preset, theme_color_role_t role){
    if(preset < 0 || preset >= THEME_PRESET_COUNT || (unsigned)role >= THEME_CLR_COUNT) return lv_color_hex(0xFF00FF);
    return lv_color_hex((g_variant ? PRESETS[preset].light : PRESETS[preset].dark)[role]);
}

uint32_t theme_rgb(theme_color_role_t role){
    return ((unsigned)role < THEME_CLR_COUNT) ? g_pal[role] : 0xFF00FFu;   /* magenta = a bug, never a silent black */
}
lv_color_t theme_color(theme_color_role_t role){ return lv_color_hex(theme_rgb(role)); }
lv_color_t theme_color_mix(lv_color_t foreground,lv_color_t background,uint8_t opacity){ return lv_color_mix(foreground,background,opacity); }
lv_color_t theme_color_from_rgb(uint32_t rgb){ return lv_color_hex(rgb & 0xFFFFFFu); }

const lv_font_t *theme_font_original(int px){
    switch(px){
    case 8: return &lv_font_montserrat_8;
    case 10: return &lv_font_montserrat_10;
    case 12: return &lv_font_montserrat_12;
    case 14: return &lv_font_montserrat_14;
    case 16: return &lv_font_montserrat_16;
    case 18: return &lv_font_montserrat_18;
    case 20: return &lv_font_montserrat_20;
    case 22: return &lv_font_montserrat_22;
    case 24: return &lv_font_montserrat_24;
    case 26: return &lv_font_montserrat_26;
    case 28: return &lv_font_montserrat_28;
    case 30: return &lv_font_montserrat_30;
    case 32: return &lv_font_montserrat_32;
    case 34: return &lv_font_montserrat_34;
    case 36: return &lv_font_montserrat_36;
    case 38: return &lv_font_montserrat_38;
    case 40: return &lv_font_montserrat_40;
    case 42: return &lv_font_montserrat_42;
    case 44: return &lv_font_montserrat_44;
    case 46: return &lv_font_montserrat_46;
    case 48: return &lv_font_montserrat_48;
    default: return &lv_font_montserrat_16;
    }
}

const lv_font_t *theme_font_base(int px){
    return px >= 20 ? &lv_font_montserrat_20 : px >= 18 ? &lv_font_montserrat_18 : px >= 16 ? &lv_font_montserrat_16
                    : &lv_font_montserrat_14;
}
int theme_trait(theme_trait_t t){ return (unsigned)t < THEME_TRAIT_COUNT && (g_traits >> t & 1u); }

/* flat list rows: no card, a hairline underneath, accent drill-down chevrons, and the pressed row as an accent-outlined
 * pill (assets/reference/something/library_dark.png). Default theme: nothing changes. */
void theme_list_row(lv_obj_t *row){
    if(!row || !theme_trait(THEME_TRAIT_FLAT_LISTS)) return;
    int h = lv_obj_get_style_height(row, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_STATE_PRESSED);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_color(row, TC(BORDER), 0);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_FULL, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(row, 2, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(row, TC(ACCENT_PRIMARY), LV_STATE_PRESSED);
    lv_obj_set_style_radius(row, h > 0 ? h / 2 : 24, 0);
    uint32_t n = lv_obj_get_child_count(row);
    for(uint32_t i = 0; i < n; i++){
        lv_obj_t *c = lv_obj_get_child(row, (int32_t)i);
        if(lv_obj_check_type(c, &lv_label_class) && !strcmp(lv_label_get_text(c), LV_SYMBOL_RIGHT))
            lv_obj_set_style_text_color(c, TC(ACCENT_PRIMARY), 0);
    }
}
char *theme_upper(char *dst, int cap, const char *src){
    if(!dst || cap <= 0) return dst;
    int n = 0;
    if(src) while(src[n]){
        unsigned char c = (unsigned char)src[n];
        int len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1;
        for(int k = 1; k < len; k++)                   /* malformed (a missing or wrong continuation byte, or the */
            if(((unsigned char)src[n + k] & 0xC0) != 0x80){ len = 1; break; }   /* string ends early): one byte */
        if(n + len > cap - 1) break;                   /* would not fit whole: stop at the character boundary */
        for(int k = 0; k < len && src[n + k]; k++) dst[n + k] = src[n + k];
        if(len == 1 && c >= 'a' && c <= 'z') dst[n] = (char)(c - 32);
        n += len;
    }
    dst[n] = 0;
    return dst;
}
char *theme_lower(char *dst, int cap, const char *src){
    theme_upper(dst, cap, src);                       /* same UTF-8-safe copy and truncation */
    for(char *p = dst; p && *p; p++) if(*p >= 'A' && *p <= 'Z' && (unsigned char)*p < 0x80) *p = (char)(*p + 32);
    return dst;
}
void theme_case_text(lv_obj_t *label, const char *text){   /* the theme's case only (dates, captions) */
    if(!label) return;
    theme_case_t c = g_def ? g_def->title_case : THEME_CASE_KEEP;
    char buf[160];
    if(c == THEME_CASE_UPPER) theme_upper(buf, sizeof buf, text);
    else if(c == THEME_CASE_LOWER) theme_lower(buf, sizeof buf, text);
    else lv_snprintf(buf, sizeof buf, "%s", text ? text : "");
    lv_label_set_text(label, buf);
}
/* A header title set after its header was laid out (a playlist's name) that is too wide for its box steps down the
 * theme's smaller faces rather than ending in "...", its last line staying where it was. Chosen before the text is
 * set: a dotted label edits its own copy. A title form may add a little (the terminal's "~/"): measured with it. */
#define TITLE_STEPPED THEME_TITLE_STEPPED
const char theme_title_tag = 0;
/* a title's next smaller face: a display-face theme steps inside its own face first (HEADER_SM), so titles don't
 * mix two families, then down the theme's text faces */
const lv_font_t *theme_title_smaller(const lv_font_t *f){
    const lv_font_t *hf = theme_font(THEME_FONT_HEADER), *hsm = g_def ? g_tfp[THEME_FONT_HEADER_SM] : NULL;
    if(f == hf && hsm && lv_font_get_line_height(hsm) < lv_font_get_line_height(hf)) return hsm;
    return theme_font_smaller(f);
}
static void title_face(lv_obj_t *l, const char *text){
    if(!g_def) return;
    const lv_font_t *hf = theme_font(THEME_FONT_HEADER), *cur = lv_obj_get_style_text_font(l, 0);
    if((cur != hf && !lv_obj_has_flag(l, TITLE_STEPPED)) || lv_obj_get_style_width(l, 0) == LV_SIZE_CONTENT) return;
    lv_obj_update_layout(l);
    int room = lv_obj_get_content_width(l);
    if(room <= 0) return;
    char m[168]; lv_snprintf(m, sizeof m, "%s%s", theme_kit()->title ? "~/" : "", text);
    const lv_font_t *f = hf;
    for(int k = 0; k < 4; k++){
        lv_point_t sz; lv_text_get_size(&sz, m, f, lv_obj_get_style_text_letter_space(l, 0), 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        const lv_font_t *sm = theme_title_smaller(f);
        if(sz.x <= room || !sm) break;
        f = sm;
    }
    if(f == cur) return;
    lv_obj_set_style_pad_top(l, lv_obj_get_style_pad_top(l, 0) + lv_font_get_line_height(cur) - lv_font_get_line_height(f), 0);
    lv_obj_set_style_text_font(l, f, 0);
    if(f == hf) lv_obj_remove_flag(l, TITLE_STEPPED); else lv_obj_add_flag(l, TITLE_STEPPED);
}
void theme_title_text(lv_obj_t *label, const char *text){
    if(!label) return;
    theme_case_t c = g_def ? g_def->title_case : THEME_CASE_KEEP;
    char buf[160];
    if(c == THEME_CASE_UPPER) theme_upper(buf, sizeof buf, text);
    else if(c == THEME_CASE_LOWER) theme_lower(buf, sizeof buf, text);
    else lv_snprintf(buf, sizeof buf, "%s", text ? text : "");
    title_face(label, buf);
    /* A content-sized dotted title (blueprint) still has the OLD text's box when the new text is set, and the label
     * picks its dots against that box: a longer title wrapped, became "..." and then shrank to fit the dots. Set the
     * text at the title's widest instead, so it dots only past that, then let it take its content size again. */
    int fit = lv_obj_get_style_width(label, 0) == LV_SIZE_CONTENT && lv_label_get_long_mode(label) == LV_LABEL_LONG_DOT;
    if(fit && theme_kit()->title_fit) theme_kit()->title_fit(label, buf);   /* the theme sizes it for THIS text */
    else if(fit){
        int32_t mw = lv_obj_get_style_max_width(label, 0);
        if(mw <= 0 || mw >= LV_COORD_MAX || LV_COORD_IS_PCT(mw)) mw = lv_obj_get_content_width(lv_obj_get_parent(label));
        lv_obj_set_width(label, mw);
        lv_obj_update_layout(label);
    }
    if(theme_kit()->title) theme_kit()->title(label, buf);   /* the theme's title form (e.g. "~/library") */
    else lv_label_set_text(label, buf);
    if(fit) lv_obj_set_width(label, LV_SIZE_CONTENT);
}

/* dot grid: one 2x2 dot every 12 px, DOT_GRID on CANVAS, pre-rendered ONCE as a full 360x360 image (a single blit
 * per redraw; tiling a small image would cost ~900 blits on this GPU-less CPU). Built on first use, never freed. */
#define GRID_W 360
#define GRID_H 360
static uint8_t *g_grid_px;
static lv_image_dsc_t g_grid_img;
static uint32_t mix(uint32_t a, uint32_t b, int k){   /* a -> b by k/256 */
    uint32_t o = 0;
    for(int sh = 0; sh <= 16; sh += 8){ int ca = a >> sh & 255, cb = b >> sh & 255; o |= (uint32_t)(ca + (cb - ca) * k / 256) << sh; }
    return o;
}
/* one texel of the active theme's background texture (the texture colour is DOT_GRID) */
static uint32_t texel(int x, int y, uint32_t bg, uint32_t tx, uint32_t ink){
    switch(g_def ? g_def->texture : THEME_TEX_NONE){
    case THEME_TEX_DOTS:      return (x % 12 >= 5 && x % 12 < 7 && y % 12 >= 5 && y % 12 < 7) ? tx : bg;
    case THEME_TEX_GRID:      /* drafting paper: a fine line every 12 px, a rule in the drawing ink every 60 */
        if(x % 60 == 0 || y % 60 == 0) return mix(bg, ink, 72);
        return (x % 12 == 0 || y % 12 == 0) ? tx : bg;
    case THEME_TEX_SCANLINES: return (y % 3 == 0) ? tx : bg;
    case THEME_TEX_PAPER: {   /* photocopy grain: sparse deterministic speckle */
        uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u; h = (h ^ (h >> 13)) * 1274126177u; h ^= h >> 16;
        return (h & 1023) < 22 ? tx : bg; }
    case THEME_TEX_BEZEL: {   /* hi-fi: minute ticks round the WHOLE rim (no gap: a gap read as a flaw), a longer one every 5 */
        int dx = x - 180, dy = y - 180, r2 = dx * dx + dy * dy;
        if(r2 < 166 * 166 || r2 > 176 * 176) return bg;
        double a = __builtin_atan2(dy, dx) * 60.0 / 6.283185307179586;   /* in minute steps */
        double f = a - __builtin_floor(a + 0.5);
        int m = (int)__builtin_floor(a + 0.5);
        int major = (m % 5) == 0;
        if(!major && r2 < 170 * 170) return bg;
        double half = (major ? 0.9 : 0.5) / 2.0 * 60.0 / (6.283185307179586 * 171);   /* ~1.8 / 1 px wide */
        return (f > -half && f < half) ? tx : bg; }
    default: return bg;
    }
}
void theme_screen_bg(lv_obj_t *root){
    lv_obj_set_style_bg_color(root, TC(CANVAS), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    if(!theme_trait(THEME_TRAIT_DOT_GRID)) return;
    if(!g_grid_px){
        g_grid_px = malloc(GRID_W * GRID_H * 4);
        if(!g_grid_px) return;                       /* out of memory: plain canvas */
        uint32_t bg = theme_rgb(THEME_CLR_CANVAS), dot = theme_rgb(THEME_CLR_DOT_GRID), ink = theme_rgb(THEME_CLR_DECOR_2);
        for(int y = 0; y < GRID_H; y++) for(int x = 0; x < GRID_W; x++){
            uint32_t c = texel(x, y, bg, dot, ink);
            uint8_t *p = g_grid_px + (y * GRID_W + x) * 4;
            p[0] = c & 255; p[1] = c >> 8 & 255; p[2] = c >> 16 & 255; p[3] = 255;   /* BGRA */
        }
        g_grid_img.header.magic = LV_IMAGE_HEADER_MAGIC;
        g_grid_img.header.cf = LV_COLOR_FORMAT_XRGB8888;
        g_grid_img.header.w = GRID_W; g_grid_img.header.h = GRID_H; g_grid_img.header.stride = GRID_W * 4;
        g_grid_img.data_size = GRID_W * GRID_H * 4; g_grid_img.data = g_grid_px;
    }
    lv_obj_set_style_bg_image_src(root, &g_grid_img, 0);
}

void theme_screen_solid(lv_obj_t *root){ if(root) lv_obj_set_style_bg_image_src(root, NULL, 0); }

/* Font size (Settings > Display, stock's Small / Medium / Large): body text roles move one rung where the
 * fixed-height layouts fit. Large keeps UI_12/UI_14 and USER_14/USER_16 at Medium: enlarging those clips Home
 * controls or overlaps Now Playing metadata. Titles, clocks and display sizes stay fixed. Read once at startup. */
int theme_font_size(void){
    static int v = -1;
    if(v < 0){ v = cfg_get_int("font_size", 1); if(v < 0 || v > 2) v = 1; }
    return v;
}
static theme_font_role_t sized_role(theme_font_role_t r){
    static const signed char STEP[THEME_FONT_COUNT][2] = {   /* {Small, Large} replacement, 0 = unchanged */
        [THEME_FONT_UI_14] = { THEME_FONT_UI_12, 0 },
        [THEME_FONT_UI_16] = { THEME_FONT_UI_14, THEME_FONT_UI_18 },
        [THEME_FONT_UI_18] = { THEME_FONT_UI_16, THEME_FONT_UI_20 },
        [THEME_FONT_UI_20] = { THEME_FONT_UI_18, 0 },
        [THEME_FONT_USER_16] = { THEME_FONT_USER_14, 0 },
        [THEME_FONT_USER_18] = { THEME_FONT_USER_16, THEME_FONT_USER_20 },
        [THEME_FONT_USER_20] = { THEME_FONT_USER_18, 0 },
        [THEME_FONT_USER_14] = { 0, 0 } };
    int sz = theme_font_size();
    if(sz == 1 || (unsigned)r >= THEME_FONT_COUNT) return r;
    int n = STEP[r][sz == 0 ? 0 : 1];
    return n ? (theme_font_role_t)n : r;
}
/* Default theme, a language that is not English: Montserrat has neither accents nor Cyrillic nor CJK, so its text
 * sizes get the same fallback chain the user-text fonts use (mutable copies; .fallback cannot be set on the const ones) */
static const lv_font_t *lang_font(int px, const lv_font_t *base){
    static lv_font_t c[8]; static const lv_font_t *b[8];
    for(int i = 0; i < 8; i++){
        if(b[i] == base) return &c[i];
        if(!b[i]){ b[i] = base; c[i] = *base; c[i].fallback = ui_font_cjk(px < 14 ? 14 : px > 20 ? 20 : px); return &c[i]; }
    }
    return base;
}
const lv_font_t *theme_font(theme_font_role_t role){
    role = sized_role(role);
    if((unsigned)role < THEME_FONT_COUNT && g_tfp[role]) return g_tfp[role];
    if(i18n_needs_fallback_font()){
        switch(role){
        case THEME_FONT_UI_12: return lang_font(12, &lv_font_montserrat_12);
        case THEME_FONT_UI_14: return lang_font(14, &lv_font_montserrat_14);
        case THEME_FONT_UI_16: return lang_font(16, &lv_font_montserrat_16);
        case THEME_FONT_UI_18: return lang_font(18, &lv_font_montserrat_18);
        case THEME_FONT_UI_20: return lang_font(20, &lv_font_montserrat_20);
        case THEME_FONT_UI_24: return lang_font(24, &lv_font_montserrat_24);
        default: break;
        }
    }
    switch(role){
    case THEME_FONT_UI_8: return theme_font_original(8);
    case THEME_FONT_UI_26: return theme_font_original(26);
    case THEME_FONT_UI_30: return theme_font_original(30);
    case THEME_FONT_UI_34: return theme_font_original(34);
    case THEME_FONT_UI_38: return theme_font_original(38);
    case THEME_FONT_UI_42: return theme_font_original(42);
    case THEME_FONT_UI_44: return theme_font_original(44);
    case THEME_FONT_UI_46: return theme_font_original(46);
    case THEME_FONT_UI_48: return theme_font_original(48);
    case THEME_FONT_UI_10: return &lv_font_montserrat_10;
    case THEME_FONT_UI_12: return &lv_font_montserrat_12;
    case THEME_FONT_UI_14: return &lv_font_montserrat_14;
    case THEME_FONT_UI_16: return &lv_font_montserrat_16;
    case THEME_FONT_UI_18: return &lv_font_montserrat_18;
    case THEME_FONT_UI_20: return &lv_font_montserrat_20;
    case THEME_FONT_UI_22: return &lv_font_montserrat_22;
    case THEME_FONT_UI_24: return &lv_font_montserrat_24;
    case THEME_FONT_UI_28: return &lv_font_montserrat_28;
    case THEME_FONT_UI_32: return &lv_font_montserrat_32;
    case THEME_FONT_UI_36: return &lv_font_montserrat_36;
    case THEME_FONT_UI_40: return &lv_font_montserrat_40;
    case THEME_FONT_USER_14: return ui_font_cjk(14);
    case THEME_FONT_USER_16: return ui_font_cjk(16);
    case THEME_FONT_USER_18: return ui_font_cjk(18);
    case THEME_FONT_USER_20: return ui_font_cjk(20);
    case THEME_FONT_HEADER: return ui_font_cjk(18);
    case THEME_FONT_CLOCK: return &lv_font_montserrat_28;
    case THEME_FONT_SAVER_CLOCK: return &lv_font_montserrat_40;
    case THEME_FONT_DATE: return &lv_font_montserrat_14;
    case THEME_FONT_ICON_20: return &font_icons_20;
    case THEME_FONT_ICON_28: return &font_icons_28;
    default: return &lv_font_montserrat_16;
    }
}

/* WCAG 2 relative luminance / contrast */
static float lin(unsigned c){ float v = c / 255.0f; return v <= 0.03928f ? v / 12.92f : __builtin_powf((v + 0.055f) / 1.055f, 2.4f); }
static float lum(uint32_t rgb){ return 0.2126f * lin(rgb >> 16 & 255) + 0.7152f * lin(rgb >> 8 & 255) + 0.0722f * lin(rgb & 255); }
static float contrast(uint32_t a, uint32_t b){ float x = lum(a), y = lum(b); return x > y ? (x + 0.05f) / (y + 0.05f) : (y + 0.05f) / (x + 0.05f); }

/* text on a fill of `bg`: the palette's ON_ACCENT while it reads at 3:1 (the rule for glyphs on the accent, below), else
 * black or white, whichever reads better
 * (Default dark passes album/user accents through unchecked, and a light one under white text is unreadable) */
lv_color_t theme_on_color(lv_color_t bg){
    uint32_t b = lv_color_to_u32(bg) & 0xFFFFFFu, on = g_pal[THEME_CLR_ON_ACCENT] & 0xFFFFFFu;
    if(contrast(on, b) >= 3.0f) return lv_color_hex(on);
    return lv_color_hex(contrast(0x000000u, b) >= contrast(0xFFFFFFu, b) ? 0x000000u : 0xFFFFFFu);
}
/* The album-art accent is fitted (and cached per song) for the default DARK surfaces. Default dark returns it
 * unchanged (the shipped look). Every other palette moves it in small steps, keeping its hue - toward white on a dark
 * palette, toward black on a light one - until it reads at 4.5:1 on the canvas and on surfaces AND glyphs drawn on it
 * (ON_ACCENT) read at 3:1. Both ends always pass (white on dark surfaces with black ON_ACCENT, black on light ones
 * with white ON_ACCENT), so the loop ends. */
lv_color_t theme_accent_from_rgb(uint32_t rgb){
    rgb &= 0xFFFFFFu;
    if((g_custom_on && g_custom_fixed_accent) || (g_def && g_def->fixed_accent))
        return lv_color_hex(g_pal[THEME_CLR_ACCENT_PRIMARY]);                  /* the theme's one accent */
    if(!g_variant && g_preset == 0) return lv_color_hex(rgb);
    uint32_t bg1 = g_pal[THEME_CLR_CANVAS], bg2 = g_pal[THEME_CLR_SURFACE], on = g_pal[THEME_CLR_ON_ACCENT];
    int lighten = !g_variant;
    float r = (float)(rgb >> 16 & 255), g = (float)(rgb >> 8 & 255), b = (float)(rgb & 255);
    uint32_t c = rgb;
    for(int i = 0; i < 200; i++){
        c = ((uint32_t)(r + 0.5f) << 16) | ((uint32_t)(g + 0.5f) << 8) | (uint32_t)(b + 0.5f);
        if(contrast(c, bg1) >= 4.5f && contrast(c, bg2) >= 4.5f && contrast(c, on) >= 3.0f) break;
        if(lighten){ r += (255.0f - r) * 0.05f; g += (255.0f - g) * 0.05f; b += (255.0f - b) * 0.05f; }
        else       { r *= 0.95f; g *= 0.95f; b *= 0.95f; }
    }
    return lv_color_hex(c);
}

/* a generated accent (the no-art fallback hue), fitted like an album accent */
lv_color_t theme_accent_from_hsv(uint16_t h, uint8_t s, uint8_t v){
    lv_color_t c = lv_color_hsv_to_rgb(h, s, v);
    return theme_accent_from_rgb(((uint32_t)c.red << 16) | ((uint32_t)c.green << 8) | c.blue);
}

/* ---- LVGL's built-in theme ---------------------------------------------------------------------------------------
 * LVGL styles every widget with its default theme before the app touches it. Screens that do not restyle a widget
 * (switches, the text-entry cursor/placeholder, plain buttons) would keep LVGL's own colours in every palette, so an
 * overlay theme on top of it re-colours exactly those parts from roles. Only colours: sizes, radii, animations stay
 * LVGL's. Screen-local styles still win over these (they are theme styles). */
static lv_theme_t g_lv_overlay;
static lv_style_t st_sw_main, st_sw_on, st_sw_knob, st_ta_cursor, st_ta_placeholder, st_btn, st_btn_checked,
                  st_primary_bg, st_arc_track, st_arc_indic;
static void overlay_apply(lv_theme_t *th, lv_obj_t *obj){
    (void)th;
#if LV_USE_SWITCH
    if(lv_obj_check_type(obj, &lv_switch_class)){
        lv_obj_add_style(obj, &st_sw_main, 0);
        lv_obj_add_style(obj, &st_sw_on, LV_PART_INDICATOR | LV_STATE_CHECKED);
        lv_obj_add_style(obj, &st_sw_knob, LV_PART_KNOB);
        return;
    }
#endif
#if LV_USE_TEXTAREA
    if(lv_obj_check_type(obj, &lv_textarea_class)){
        lv_obj_add_style(obj, &st_ta_cursor, LV_PART_CURSOR | LV_STATE_FOCUSED);
        lv_obj_add_style(obj, &st_ta_placeholder, LV_PART_TEXTAREA_PLACEHOLDER);
        return;
    }
#endif
    if(lv_obj_check_type(obj, &lv_button_class)){
        lv_obj_add_style(obj, &st_btn, 0);
        lv_obj_add_style(obj, &st_btn_checked, LV_STATE_CHECKED);
        return;
    }
#if LV_USE_SLIDER
    if(lv_obj_check_type(obj, &lv_slider_class) || lv_obj_check_type(obj, &lv_bar_class)){
        lv_obj_add_style(obj, &st_primary_bg, 0);                   /* LVGL draws the track at 20% of this */
        lv_obj_add_style(obj, &st_primary_bg, LV_PART_INDICATOR);
        lv_obj_add_style(obj, &st_primary_bg, LV_PART_KNOB);
        return;
    }
#endif
#if LV_USE_ARC
    if(lv_obj_check_type(obj, &lv_arc_class)){
        lv_obj_add_style(obj, &st_arc_track, 0);
        lv_obj_add_style(obj, &st_arc_indic, LV_PART_INDICATOR);
        lv_obj_add_style(obj, &st_primary_bg, LV_PART_KNOB);
    }
#endif
    /* Not colours, so not roles: LVGL's pressed (darken 35/255) and disabled (50% toward grey) filters shade
     * whatever role colour is underneath, in every palette. Focus-key/edited outlines need a keypad or encoder,
     * which this UI never attaches. */
}
static void theme_install_lvgl(void){
    lv_display_t *d = lv_display_get_default();
    lv_theme_t *base = d ? lv_display_get_theme(d) : NULL;
    static int installed;
    if(!base || installed) return;     /* one-shot: a theme change re-launches the UI, so styles never go stale */
    installed = 1;
    lv_style_init(&st_sw_main);        lv_style_set_bg_color(&st_sw_main, TC(SWITCH_OFF));
    lv_style_init(&st_sw_on);          lv_style_set_bg_color(&st_sw_on, TC(SWITCH_ON));
    lv_style_init(&st_sw_knob);        lv_style_set_bg_color(&st_sw_knob, TC(SWITCH_KNOB));
    lv_style_init(&st_ta_cursor);      lv_style_set_border_color(&st_ta_cursor, TC(INPUT_CURSOR));
    lv_style_init(&st_ta_placeholder); lv_style_set_text_color(&st_ta_placeholder, TC(INPUT_PLACEHOLDER));
    lv_style_init(&st_btn);
    lv_style_set_bg_color(&st_btn, TC(ACTION_SURFACE));
    lv_style_set_text_color(&st_btn, TC(ON_ACTION));
    lv_style_set_shadow_color(&st_btn, TC(ACTION_SHADOW));
    lv_style_init(&st_btn_checked);
    lv_style_set_bg_color(&st_btn_checked, TC(WIDGET_PRIMARY));
    lv_style_set_text_color(&st_btn_checked, TC(WIDGET_PRIMARY));
    lv_style_init(&st_primary_bg);     lv_style_set_bg_color(&st_primary_bg, TC(WIDGET_PRIMARY));
    lv_style_init(&st_arc_track);      lv_style_set_arc_color(&st_arc_track, TC(WIDGET_TRACK));
    lv_style_init(&st_arc_indic);      lv_style_set_arc_color(&st_arc_indic, TC(WIDGET_PRIMARY));
    g_lv_overlay = *base;                        /* same fonts + primary/secondary as the base */
    lv_theme_set_parent(&g_lv_overlay, base);
    lv_theme_set_apply_cb(&g_lv_overlay, overlay_apply);
    lv_display_set_theme(d, &g_lv_overlay);
}

/* the active preset's component kit (theme_kit.h); presets without their own kit use Default's builders */
const theme_kit_t *theme_kit_for_active(void){ return (g_def && g_def->kit) ? g_def->kit : &theme_kit_default; }

int theme_ring_light(void){return (g_preset==THEME_PRESET_RING || g_preset==THEME_PRESET_DISCO) && g_variant==1;}
int theme_auto_supported(void){return g_preset==THEME_PRESET_RING || g_preset==THEME_PRESET_BRAUN || g_preset==THEME_PRESET_DISCO;}
int th_disco(void){return g_preset==THEME_PRESET_DISCO;}
int th_ringlike(void){return g_preset==THEME_PRESET_RING || g_preset==THEME_PRESET_DISCO;}
