/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
/* theme_validate.c - the ONE check a custom theme must pass before it is saved or used (both builders call it via
 * theme_validate_and_save). No LVGL. The contrast table is the same one the release test and the preset generator
 * use (test_ai compares them). */
#include "theme_model.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

/* JSON key per editable role: the role name in lower case. Fixed identity roles (brand, QR, splash, debug) have no
 * key: a custom theme cannot change them. */
static const char *const KEYS[THEME_CLR_COUNT] = {
    [THEME_CLR_USAGE_CHARGE_BAND]="usage_charge_band",
    [THEME_CLR_USAGE_BAND]="usage_band",

    [THEME_CLR_VOLUME_TRACK] = "volume_track",
    [THEME_CLR_VOLUME_BADGE] = "volume_badge",
    [THEME_CLR_VOLUME_TICK_MAJOR] = "volume_tick_major",
    [THEME_CLR_VOLUME_TICK_MINOR] = "volume_tick_minor",
    [THEME_CLR_VOLUME_DIGITS] = "volume_digits",
    [THEME_CLR_VOLUME_CAPTION] = "volume_caption",
    [THEME_CLR_PAGE_DOT_QUIET] = "page_dot_quiet",
    [THEME_CLR_SAVER_TITLE] = "saver_title",
    [THEME_CLR_SAVER_SUBTITLE] = "saver_subtitle",
    [THEME_CLR_SAVER_ARTIST] = "saver_artist",
    [THEME_CLR_SAVER_RING] = "saver_ring",
    [THEME_CLR_SAVER_DISC] = "saver_disc",
    [THEME_CLR_EQ_ZERO] = "eq_zero",
    [THEME_CLR_EQ_SLOT] = "eq_slot",
    [THEME_CLR_EQ_CAP_DISABLED] = "eq_cap_disabled",
    [THEME_CLR_EQ_FADER] = "eq_fader",
    [THEME_CLR_EQ_FADER_DISABLED] = "eq_fader_disabled",
    [THEME_CLR_EQ_READONLY] = "eq_readonly",
    [THEME_CLR_EQ_CHIP] = "eq_chip",
    [THEME_CLR_BOOK_HEARD] = "book_heard",
    [THEME_CLR_WEATHER_SUN] = "weather_sun",
    [THEME_CLR_WEATHER_MOON] = "weather_moon",
    [THEME_CLR_WEATHER_RAIN] = "weather_rain",
    [THEME_CLR_WEATHER_BORDER] = "weather_border",
    [THEME_CLR_TOAST_SURFACE] = "toast_surface",
    [THEME_CLR_TOAST_BORDER] = "toast_border",
    [THEME_CLR_TOAST_ORB] = "toast_orb",
    [THEME_CLR_ORBIT_PRESSED] = "orbit_pressed",
    [THEME_CLR_ORBIT_SHADOW] = "orbit_shadow",
    [THEME_CLR_ORBIT_POINTER] = "orbit_pointer",
    [THEME_CLR_ORBIT_HUB] = "orbit_hub",
    [THEME_CLR_USAGE_GRID] = "usage_grid",
    [THEME_CLR_USAGE_TEXT] = "usage_text",
    [THEME_CLR_USAGE_AMBER] = "usage_amber",
    [THEME_CLR_USAGE_FAINT] = "usage_faint",
    [THEME_CLR_QUEUE_DRAG] = "queue_drag",
    [THEME_CLR_QUEUE_HANDLE] = "queue_handle",
    [THEME_CLR_PRIMARY_PRESSED] = "primary_pressed",
    [THEME_CLR_SWITCH_SELECTED] = "switch_selected",
    [THEME_CLR_DANGER_DIALOG] = "danger_dialog",
    [THEME_CLR_POSTER_SUBTITLE] = "poster_subtitle",
    [THEME_CLR_POSTER_SIDE] = "poster_side",

    [THEME_CLR_CANVAS]                      = "canvas",
    [THEME_CLR_SCRIM]                       = "scrim",
    [THEME_CLR_IMAGE_TINT]                  = "image_tint",
    [THEME_CLR_SURFACE]                     = "surface",
    [THEME_CLR_SURFACE_RAISED]              = "surface_raised",
    [THEME_CLR_SURFACE_STRONG]              = "surface_strong",
    [THEME_CLR_SURFACE_SELECTED]            = "surface_selected",
    [THEME_CLR_ART_PLACEHOLDER]             = "art_placeholder",
    [THEME_CLR_LIST_PRESSED]                = "list_pressed",
    [THEME_CLR_SURFACE_PRESSED]             = "surface_pressed",
    [THEME_CLR_RAISED_PRESSED]              = "raised_pressed",
    [THEME_CLR_BORDER]                      = "border",
    [THEME_CLR_BORDER_STRONG]               = "border_strong",
    [THEME_CLR_OUTLINE_BRIGHT]              = "outline_bright",
    [THEME_CLR_CONNECTED_SURFACE]           = "connected_surface",
    [THEME_CLR_CONTROL_TRACK]               = "control_track",
    [THEME_CLR_CONTROL_TRACK_STRONG]        = "control_track_strong",
    [THEME_CLR_CONTROL_FILL]                = "control_fill",
    [THEME_CLR_CONTROL_KNOB]                = "control_knob",
    [THEME_CLR_CONTROL_FILL_MUTED]          = "control_fill_muted",
    [THEME_CLR_ON_CONTROL]                  = "on_control",
    [THEME_CLR_CONTROL_OFF]                 = "control_off",
    [THEME_CLR_GRABBER]                     = "grabber",
    [THEME_CLR_PAGE_DOT_ACTIVE]             = "page_dot_active",
    [THEME_CLR_PAGE_DOT_INACTIVE]           = "page_dot_inactive",
    [THEME_CLR_PAGE_DOT_DIM]                = "page_dot_dim",
    [THEME_CLR_KEY_SURFACE]                 = "key_surface",
    [THEME_CLR_KEY_SELECTED]                = "key_selected",
    [THEME_CLR_KEY_TEXT]                    = "key_text",
    [THEME_CLR_KEY_PRESSED]                 = "key_pressed",
    [THEME_CLR_SWITCH_OFF]                  = "switch_off",
    [THEME_CLR_SWITCH_ON]                   = "switch_on",
    [THEME_CLR_SWITCH_KNOB]                 = "switch_knob",
    [THEME_CLR_INPUT_CURSOR]                = "input_cursor",
    [THEME_CLR_INPUT_PLACEHOLDER]           = "input_placeholder",
    [THEME_CLR_ACTION_SURFACE]              = "action_surface",
    [THEME_CLR_ON_ACTION]                   = "on_action",
    [THEME_CLR_ACTION_SHADOW]               = "action_shadow",
    [THEME_CLR_WIDGET_PRIMARY]              = "widget_primary",
    [THEME_CLR_WIDGET_TRACK]                = "widget_track",
    [THEME_CLR_SIGNAL_ACTIVE]               = "signal_active",
    [THEME_CLR_SIGNAL_INACTIVE]             = "signal_inactive",
    [THEME_CLR_ART_WALL_FRONT]              = "art_wall_front",
    [THEME_CLR_DOT_GRID]                   = "dot_grid",
    [THEME_CLR_DECOR_1]                    = "decor_1",
    [THEME_CLR_DECOR_2]                    = "decor_2",
    [THEME_CLR_ART_WALL_SIDE]               = "art_wall_side",
    [THEME_CLR_TEXT_PRIMARY]                = "text_primary",
    [THEME_CLR_TEXT_SECONDARY]              = "text_secondary",
    [THEME_CLR_TEXT_MUTED]                  = "text_muted",
    [THEME_CLR_TEXT_DISABLED]               = "text_disabled",
    [THEME_CLR_TEXT_FOOTNOTE]               = "text_footnote",
    [THEME_CLR_TEXT_LYRICS]                 = "text_lyrics",
    [THEME_CLR_TEXT_SEPARATOR]              = "text_separator",
    [THEME_CLR_TEXT_HINT]                   = "text_hint",
    [THEME_CLR_ON_ACCENT]                   = "on_accent",
    [THEME_CLR_ACCENT_PRIMARY]              = "accent_primary",
    [THEME_CLR_ACCENT_EMPHASIS]             = "accent_emphasis",
    [THEME_CLR_ACCENT_IDLE]                 = "accent_idle",
    [THEME_CLR_STATUS_INFO]                 = "status_info",
    [THEME_CLR_STATUS_SUCCESS]              = "status_success",
    [THEME_CLR_STATUS_DANGER]               = "status_danger",
    [THEME_CLR_STATUS_DANGER_STRONG]        = "status_danger_strong",
    [THEME_CLR_STATUS_WARNING]              = "status_warning",
    [THEME_CLR_FOLDER_ICON]                 = "folder_icon",
    [THEME_CLR_DANGER_SURFACE]              = "danger_surface",
    [THEME_CLR_DANGER_SURFACE_PRESSED]      = "danger_surface_pressed",
    [THEME_CLR_DANGER_BUTTON_SURFACE]       = "danger_button_surface",
    [THEME_CLR_DANGER_PANEL_SURFACE]        = "danger_panel_surface",
};
const char *theme_role_key(int role){ return (role >= 0 && role < THEME_CLR_COUNT) ? KEYS[role] : NULL; }

/* Optional additions use a pre-existing role when reading an upstream schema-1 theme. */
int theme_role_fallback(int role){
    switch(role){
    case THEME_CLR_USAGE_CHARGE_BAND: return THEME_CLR_STATUS_SUCCESS;
    case THEME_CLR_USAGE_BAND: return THEME_CLR_SURFACE_STRONG;

    case THEME_CLR_VOLUME_TRACK: return THEME_CLR_CONTROL_TRACK_STRONG;
    case THEME_CLR_VOLUME_BADGE: return THEME_CLR_SURFACE;
    case THEME_CLR_VOLUME_TICK_MAJOR: return THEME_CLR_TEXT_PRIMARY;
    case THEME_CLR_VOLUME_TICK_MINOR: return THEME_CLR_TEXT_DISABLED;
    case THEME_CLR_VOLUME_DIGITS: return THEME_CLR_TEXT_PRIMARY;
    case THEME_CLR_VOLUME_CAPTION: return THEME_CLR_TEXT_SECONDARY;
    case THEME_CLR_PAGE_DOT_QUIET: return THEME_CLR_PAGE_DOT_INACTIVE;
    case THEME_CLR_SAVER_TITLE: return THEME_CLR_TEXT_PRIMARY;
    case THEME_CLR_SAVER_SUBTITLE: return THEME_CLR_TEXT_SECONDARY;
    case THEME_CLR_SAVER_ARTIST: return THEME_CLR_TEXT_MUTED;
    case THEME_CLR_SAVER_RING: return THEME_CLR_CONTROL_TRACK;
    case THEME_CLR_SAVER_DISC: return THEME_CLR_ART_PLACEHOLDER;
    case THEME_CLR_EQ_ZERO: return THEME_CLR_BORDER;
    case THEME_CLR_EQ_SLOT: return THEME_CLR_CONTROL_TRACK;
    case THEME_CLR_EQ_CAP_DISABLED: return THEME_CLR_TEXT_DISABLED;
    case THEME_CLR_EQ_FADER: return THEME_CLR_CONTROL_FILL;
    case THEME_CLR_EQ_FADER_DISABLED: return THEME_CLR_CONTROL_FILL_MUTED;
    case THEME_CLR_EQ_READONLY: return THEME_CLR_TEXT_DISABLED;
    case THEME_CLR_EQ_CHIP: return THEME_CLR_SURFACE_RAISED;
    case THEME_CLR_BOOK_HEARD: return THEME_CLR_CONTROL_FILL_MUTED;
    case THEME_CLR_WEATHER_SUN: return THEME_CLR_STATUS_WARNING;
    case THEME_CLR_WEATHER_MOON: return THEME_CLR_TEXT_SECONDARY;
    case THEME_CLR_WEATHER_RAIN: return THEME_CLR_STATUS_INFO;
    case THEME_CLR_WEATHER_BORDER: return THEME_CLR_BORDER;
    case THEME_CLR_TOAST_SURFACE: return THEME_CLR_SURFACE_RAISED;
    case THEME_CLR_TOAST_BORDER: return THEME_CLR_BORDER;
    case THEME_CLR_TOAST_ORB: return THEME_CLR_TEXT_DISABLED;
    case THEME_CLR_ORBIT_PRESSED: return THEME_CLR_SURFACE_PRESSED;
    case THEME_CLR_ORBIT_SHADOW: return THEME_CLR_ACTION_SHADOW;
    case THEME_CLR_ORBIT_POINTER: return THEME_CLR_GRABBER;
    case THEME_CLR_ORBIT_HUB: return THEME_CLR_SURFACE_STRONG;
    case THEME_CLR_USAGE_GRID: return THEME_CLR_BORDER;
    case THEME_CLR_USAGE_TEXT: return THEME_CLR_TEXT_PRIMARY;
    case THEME_CLR_USAGE_AMBER: return THEME_CLR_STATUS_WARNING;
    case THEME_CLR_USAGE_FAINT: return THEME_CLR_BORDER;
    case THEME_CLR_QUEUE_DRAG: return THEME_CLR_SURFACE_SELECTED;
    case THEME_CLR_QUEUE_HANDLE: return THEME_CLR_TEXT_DISABLED;
    case THEME_CLR_PRIMARY_PRESSED: return THEME_CLR_ACCENT_EMPHASIS;
    case THEME_CLR_SWITCH_SELECTED: return THEME_CLR_SWITCH_ON;
    case THEME_CLR_DANGER_DIALOG: return THEME_CLR_DANGER_BUTTON_SURFACE;
    case THEME_CLR_POSTER_SUBTITLE: return THEME_CLR_TEXT_SECONDARY;
    case THEME_CLR_POSTER_SIDE: return THEME_CLR_TEXT_PRIMARY;
    default: return -1;
    }
}

/* foreground, background, the colour UNDER the background (-1 = opaque background), background alpha %, minimum
 * contrast x10 */
static const struct { int fg, bg, under, alpha, min10; } RULES[] = {
    { THEME_CLR_TEXT_PRIMARY, THEME_CLR_CANVAS, -1, 100, 45 },
    { THEME_CLR_TEXT_PRIMARY, THEME_CLR_SURFACE, -1, 100, 45 },
    { THEME_CLR_TEXT_PRIMARY, THEME_CLR_SURFACE_RAISED, -1, 100, 45 },
    { THEME_CLR_TEXT_SECONDARY, THEME_CLR_CANVAS, -1, 100, 45 },
    { THEME_CLR_TEXT_SECONDARY, THEME_CLR_SURFACE, -1, 100, 45 },
    { THEME_CLR_TEXT_SECONDARY, THEME_CLR_SURFACE_RAISED, -1, 100, 45 },
    { THEME_CLR_TEXT_MUTED, THEME_CLR_CANVAS, -1, 100, 45 },
    { THEME_CLR_TEXT_MUTED, THEME_CLR_SURFACE, -1, 100, 45 },
    { THEME_CLR_TEXT_MUTED, THEME_CLR_SURFACE_RAISED, -1, 100, 45 },
    { THEME_CLR_TEXT_DISABLED, THEME_CLR_CANVAS, -1, 100, 30 },
    { THEME_CLR_TEXT_DISABLED, THEME_CLR_SURFACE, -1, 100, 30 },
    { THEME_CLR_TEXT_FOOTNOTE, THEME_CLR_CANVAS, -1, 100, 30 },
    { THEME_CLR_TEXT_FOOTNOTE, THEME_CLR_SURFACE, -1, 100, 30 },
    { THEME_CLR_TEXT_HINT, THEME_CLR_CANVAS, -1, 100, 30 },
    { THEME_CLR_TEXT_HINT, THEME_CLR_SURFACE, -1, 100, 30 },
    { THEME_CLR_TEXT_SEPARATOR, THEME_CLR_CANVAS, -1, 100, 30 },
    { THEME_CLR_TEXT_SEPARATOR, THEME_CLR_SURFACE, -1, 100, 30 },
    { THEME_CLR_STATUS_INFO, THEME_CLR_CANVAS, -1, 100, 30 },
    { THEME_CLR_STATUS_INFO, THEME_CLR_SURFACE, -1, 100, 30 },
    { THEME_CLR_STATUS_INFO, THEME_CLR_SURFACE_RAISED, -1, 100, 30 },
    { THEME_CLR_STATUS_SUCCESS, THEME_CLR_CANVAS, -1, 100, 30 },
    { THEME_CLR_STATUS_SUCCESS, THEME_CLR_SURFACE, -1, 100, 30 },
    { THEME_CLR_STATUS_SUCCESS, THEME_CLR_SURFACE_RAISED, -1, 100, 30 },
    { THEME_CLR_STATUS_DANGER, THEME_CLR_CANVAS, -1, 100, 30 },
    { THEME_CLR_STATUS_DANGER, THEME_CLR_SURFACE, -1, 100, 30 },
    { THEME_CLR_STATUS_DANGER, THEME_CLR_SURFACE_RAISED, -1, 100, 30 },
    { THEME_CLR_STATUS_WARNING, THEME_CLR_CANVAS, -1, 100, 30 },
    { THEME_CLR_STATUS_WARNING, THEME_CLR_SURFACE, -1, 100, 30 },
    { THEME_CLR_STATUS_WARNING, THEME_CLR_SURFACE_RAISED, -1, 100, 30 },
    { THEME_CLR_ACCENT_PRIMARY, THEME_CLR_CANVAS, -1, 100, 30 },
    { THEME_CLR_ACCENT_PRIMARY, THEME_CLR_SURFACE, -1, 100, 30 },
    { THEME_CLR_ACCENT_PRIMARY, THEME_CLR_SURFACE_RAISED, -1, 100, 30 },
    { THEME_CLR_TEXT_PRIMARY, THEME_CLR_SURFACE_SELECTED, -1, 100, 45 },
    { THEME_CLR_TEXT_PRIMARY, THEME_CLR_CONNECTED_SURFACE, -1, 100, 45 },
    { THEME_CLR_TEXT_PRIMARY, THEME_CLR_SURFACE_STRONG, -1, 100, 45 },
    { THEME_CLR_KEY_TEXT, THEME_CLR_KEY_SURFACE, -1, 100, 45 },
    { THEME_CLR_KEY_TEXT, THEME_CLR_KEY_SELECTED, -1, 100, 45 },
    { THEME_CLR_KEY_TEXT, THEME_CLR_KEY_PRESSED, -1, 100, 45 },
    { THEME_CLR_STATUS_DANGER, THEME_CLR_DANGER_SURFACE, -1, 100, 45 },
    { THEME_CLR_STATUS_DANGER, THEME_CLR_DANGER_BUTTON_SURFACE, -1, 100, 45 },
    { THEME_CLR_STATUS_DANGER, THEME_CLR_DANGER_SURFACE_PRESSED, -1, 100, 45 },
    { THEME_CLR_ON_ACCENT, THEME_CLR_ACCENT_PRIMARY, -1, 100, 30 },
    { THEME_CLR_ON_ACCENT, THEME_CLR_ACCENT_EMPHASIS, -1, 100, 30 },
    { THEME_CLR_CONTROL_FILL, THEME_CLR_CONTROL_TRACK, -1, 100, 30 },
    { THEME_CLR_CONTROL_KNOB, THEME_CLR_CONTROL_TRACK, -1, 100, 30 },
    { THEME_CLR_ACCENT_PRIMARY, THEME_CLR_CONTROL_TRACK, -1, 100, 30 },
    { THEME_CLR_ON_CONTROL, THEME_CLR_CONTROL_FILL, -1, 100, 30 },
    { THEME_CLR_PAGE_DOT_ACTIVE, THEME_CLR_CANVAS, -1, 100, 30 },
    { THEME_CLR_OUTLINE_BRIGHT, THEME_CLR_CANVAS, -1, 100, 30 },
    { THEME_CLR_TEXT_PRIMARY, THEME_CLR_CONTROL_OFF, -1, 100, 30 },
    { THEME_CLR_FOLDER_ICON, THEME_CLR_CANVAS, -1, 100, 30 },
    { THEME_CLR_GRABBER, THEME_CLR_SURFACE, -1, 100, 30 },
    { THEME_CLR_SWITCH_OFF, THEME_CLR_SURFACE, -1, 100, 30 },
    { THEME_CLR_SWITCH_ON, THEME_CLR_SURFACE, -1, 100, 30 },
    { THEME_CLR_INPUT_PLACEHOLDER, THEME_CLR_SURFACE, -1, 100, 45 },
    { THEME_CLR_INPUT_CURSOR, THEME_CLR_SURFACE, -1, 100, 30 },
    { THEME_CLR_ON_ACTION, THEME_CLR_ACTION_SURFACE, -1, 100, 45 },
    { THEME_CLR_SIGNAL_ACTIVE, THEME_CLR_SURFACE, -1, 100, 30 },
    { THEME_CLR_SWITCH_KNOB, THEME_CLR_SWITCH_OFF, -1, 100, 30 },
    { THEME_CLR_SWITCH_KNOB, THEME_CLR_SWITCH_ON, -1, 100, 30 },
    { THEME_CLR_SIGNAL_ACTIVE, THEME_CLR_SURFACE, THEME_CLR_CANVAS, 50, 30 },
    { THEME_CLR_SIGNAL_INACTIVE, THEME_CLR_SURFACE, THEME_CLR_CANVAS, 50, 30 },
    { THEME_CLR_SIGNAL_ACTIVE, THEME_CLR_CONNECTED_SURFACE, -1, 100, 30 },
    { THEME_CLR_SIGNAL_INACTIVE, THEME_CLR_CONNECTED_SURFACE, -1, 100, 30 },
    { THEME_CLR_TEXT_PRIMARY, THEME_CLR_SURFACE, THEME_CLR_CANVAS, 50, 45 },
};

static double ch(unsigned v){ double c = v / 255.0; return c <= 0.03928 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4); }
static double lum(uint32_t x){ return 0.2126 * ch(x >> 16 & 255) + 0.7152 * ch(x >> 8 & 255) + 0.0722 * ch(x & 255); }
static double ratio(uint32_t a, uint32_t b){ double x = lum(a), y = lum(b); return x > y ? (x + 0.05) / (y + 0.05) : (y + 0.05) / (x + 0.05); }
static uint32_t over(uint32_t top, uint32_t under, int alpha){
    uint32_t out = 0;
    for(int sh = 16; sh >= 0; sh -= 8){
        double c = (top >> sh & 255) * alpha / 100.0 + (under >> sh & 255) * (100 - alpha) / 100.0;
        out |= (uint32_t)lround(c) << sh;
    }
    return out;
}

static int name_ok(const char *n){
    size_t len = strlen(n);
    if(len < 1 || len > THEME_NAME_MAX) return 0;
    for(size_t i = 0; i < len; i++) if(n[i] < 0x20 || n[i] > 0x7e || n[i] == '"' || n[i] == '\\') return 0;
    return 1;
}

int theme_model_validate(const theme_model_t *m, char *err, int errcap){
    if(!m){ snprintf(err, errcap, "no theme"); return THEME_E_INVALID; }
    if(m->revision < 0){ snprintf(err, errcap, "bad revision"); return THEME_E_INVALID; }
    if(!name_ok(m->name)){ snprintf(err, errcap, "name must be 1-24 plain characters"); return THEME_E_INVALID; }
    for(int v = 0; v < 2; v++){
        for(int r = 0; r < THEME_CLR_COUNT; r++)
            if(m->pal[v][r] > 0xFFFFFFu){ snprintf(err, errcap, "%s colour out of range", v ? "light" : "dark"); return THEME_E_INVALID; }
        for(unsigned i = 0; i < sizeof RULES / sizeof RULES[0]; i++){
            uint32_t bg = m->pal[v][RULES[i].bg];
            if(RULES[i].under >= 0) bg = over(bg, m->pal[v][RULES[i].under], RULES[i].alpha);
            double r = ratio(m->pal[v][RULES[i].fg], bg);
            if(r * 10.0 + 1e-6 < RULES[i].min10){
                snprintf(err, errcap, "%s: %s on %s is %.2f:1, needs %.1f:1", v ? "light" : "dark",
                         KEYS[RULES[i].fg], KEYS[RULES[i].bg], r, RULES[i].min10 / 10.0);
                return THEME_E_INVALID;
            }
        }
    }
    return THEME_OK;
}
