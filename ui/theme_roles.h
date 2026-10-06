/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
/* theme_roles.h - the colour roles (no LVGL, so the custom-theme model and validator can use them anywhere). */
#ifndef THEME_ROLES_H
#define THEME_ROLES_H

typedef enum {
    /* backgrounds + surfaces */
    THEME_CLR_CANVAS,                 /* a screen's own background */
    THEME_CLR_SCRIM,                  /* darkening overlay (drawn with opacity) */
    THEME_CLR_IMAGE_TINT,             /* recolour that darkens a background image */
    THEME_CLR_SURFACE,                /* cards, list rows, panels */
    THEME_CLR_SURFACE_RAISED,         /* buttons, toasts, dialogs, the highlighted row */
    THEME_CLR_SURFACE_STRONG,         /* stronger panel */
    THEME_CLR_SURFACE_SELECTED,       /* the selected row of a choice list */
    THEME_CLR_ART_PLACEHOLDER,        /* cover-art square before the art arrives */
    THEME_CLR_LIST_PRESSED,           /* pressed state of a row drawn on the canvas */
    THEME_CLR_SURFACE_PRESSED,        /* pressed state of a SURFACE item */
    THEME_CLR_RAISED_PRESSED,         /* pressed state of a SURFACE_RAISED item */
    THEME_CLR_BORDER,                 /* outline of a card/control */
    THEME_CLR_BORDER_STRONG,          /* stronger outline */
    THEME_CLR_OUTLINE_BRIGHT,         /* bright outline (focus, selection, debug) */
    THEME_CLR_CONNECTED_SURFACE,      /* a connected Wi-Fi/Bluetooth row */
    /* controls */
    THEME_CLR_CONTROL_TRACK,          /* slider / arc / progress background */
    THEME_CLR_CONTROL_TRACK_STRONG,   /* a stronger track (volume arc) */
    THEME_CLR_CONTROL_FILL,           /* slider / arc indicator (the neutral one) */
    THEME_CLR_CONTROL_KNOB,           /* slider / ring knob */
    THEME_CLR_CONTROL_FILL_MUTED,     /* a grey slider fill (colour picker) */
    THEME_CLR_ON_CONTROL,             /* a glyph drawn on a control track/fill (the brightness sun) */
    THEME_CLR_CONTROL_OFF,            /* an OFF toggle tile */
    THEME_CLR_GRABBER,                /* the Quick Settings grab handle */
    THEME_CLR_PAGE_DOT_ACTIVE,
    THEME_CLR_PAGE_DOT_INACTIVE,
    THEME_CLR_PAGE_DOT_DIM,           /* Now Playing's (dimmer) inactive page dot */
    THEME_CLR_KEY_SURFACE,            /* on-screen keyboard key */
    THEME_CLR_KEY_SELECTED,
    THEME_CLR_KEY_TEXT,
    THEME_CLR_KEY_PRESSED,            /* a key while pressed */
    THEME_CLR_SWITCH_OFF,             /* switch track when off (LVGL switch widget) */
    THEME_CLR_SWITCH_ON,              /* switch track when on, where the screen does not set its own accent */
    THEME_CLR_SWITCH_KNOB,
    THEME_CLR_INPUT_CURSOR,           /* text-entry cursor */
    THEME_CLR_INPUT_PLACEHOLDER,      /* text-entry placeholder text */
    THEME_CLR_ACTION_SURFACE,         /* a plain (un-restyled) button: its fill */
    THEME_CLR_ON_ACTION,              /* ...and the text on it */
    THEME_CLR_ACTION_SHADOW,          /* ...and its drop shadow */
    THEME_CLR_WIDGET_PRIMARY,         /* LVGL widget parts a screen leaves unstyled: slider/arc knob + fill, a checked
                                       * plain button */
    THEME_CLR_WIDGET_TRACK,           /* ...and an unstyled arc track */
    THEME_CLR_SIGNAL_ACTIVE,          /* lit signal-strength bar */
    THEME_CLR_SIGNAL_INACTIVE,        /* unlit signal-strength bar */
    THEME_CLR_ART_WALL_FRONT,         /* album wall: cover sprite before its art loads */
    THEME_CLR_ART_WALL_SIDE,          /* album wall: side (spine) sprite before its art loads */
    THEME_CLR_DOT_GRID,               /* the background texture: dots, grid lines, scanlines, paper grain */
    THEME_CLR_DECOR_1,                /* ornament colour 1 (e.g. a second accent block, highlighter); no meaning */
    THEME_CLR_DECOR_2,                /* ornament colour 2 */
    /* text + icons */
    THEME_CLR_TEXT_PRIMARY,
    THEME_CLR_TEXT_SECONDARY,
    THEME_CLR_TEXT_MUTED,             /* metadata, hints, inactive icons */
    THEME_CLR_TEXT_DISABLED,
    THEME_CLR_TEXT_FOOTNOTE,
    THEME_CLR_TEXT_LYRICS,
    THEME_CLR_TEXT_SEPARATOR,         /* the faint "/" between elapsed and remaining time */
    THEME_CLR_TEXT_HINT,              /* faint hint glyph (Home swipe chevron) */
    THEME_CLR_ON_ACCENT,              /* glyph/text drawn on an accent fill */
    /* accents + statuses */
    THEME_CLR_ACCENT_PRIMARY,         /* the theme's fixed accent */
    THEME_CLR_ACCENT_EMPHASIS,        /* the initial static accent + emphasised actions */
    THEME_CLR_ACCENT_IDLE,            /* dynamic accent when nothing is playing */
    THEME_CLR_STATUS_INFO,            /* connected / current / export */
    THEME_CLR_STATUS_SUCCESS,
    THEME_CLR_STATUS_DANGER,
    THEME_CLR_STATUS_DANGER_STRONG,
    THEME_CLR_STATUS_WARNING,
    THEME_CLR_FOLDER_ICON,
    THEME_CLR_DANGER_SURFACE,
    THEME_CLR_DANGER_SURFACE_PRESSED,
    THEME_CLR_DANGER_BUTTON_SURFACE,
    THEME_CLR_DANGER_PANEL_SURFACE,
    /* fixed identities: never themed, identical in every palette (theme.c refuses a palette that changes them);
     * here so they are not raw literals in screen code */
    THEME_CLR_FIXED_LASTFM,           /* Last.fm brand red */
    THEME_CLR_FIXED_ON_LASTFM,        /* text on the Last.fm red */
    THEME_CLR_FIXED_QR_DARK,          /* a QR code must stay black on white to scan */
    THEME_CLR_FIXED_QR_LIGHT,
    THEME_CLR_FIXED_DEBUG_TOUCH,      /* developer touch-debug dot */
    THEME_CLR_FIXED_SPLASH_BG,        /* boot splash identity (SPLASH_3, last: the wordmark, edge light, the click) */
    THEME_CLR_FIXED_SPLASH_RING,      /* the Disc window's bezel */
    THEME_CLR_FIXED_SPLASH_1,         /* the CD face: top / bottom */
    THEME_CLR_FIXED_SPLASH_2,
    THEME_CLR_FIXED_SPLASH_WELL,      /* the Disc window, empty */
    THEME_CLR_FIXED_SPLASH_SPINDLE,   /* its spindle */
    THEME_CLR_FIXED_SPLASH_HUB_LINE,  /* the CD hub rings */
    THEME_CLR_FIXED_SPLASH_HOLE,      /* the CD centre hole */
    THEME_CLR_FIXED_SPLASH_MARK,      /* the marks that show the CD turning */
    THEME_CLR_FIXED_SPLASH_BAND_1,    /* the light a CD throws, hub to rim */
    THEME_CLR_FIXED_SPLASH_BAND_2,
    THEME_CLR_FIXED_SPLASH_BAND_3,
    THEME_CLR_FIXED_SPLASH_BAND_4,
    THEME_CLR_FIXED_SPLASH_BAND_5,
    THEME_CLR_FIXED_SPLASH_BLACK_HI,  /* the drawn Disc body, per Disc Colour: top / bottom of its sheen */
    THEME_CLR_FIXED_SPLASH_BLACK_LO,
    THEME_CLR_FIXED_SPLASH_TURQ_HI,
    THEME_CLR_FIXED_SPLASH_TURQ_LO,
    THEME_CLR_FIXED_SPLASH_PINK_HI,
    THEME_CLR_FIXED_SPLASH_PINK_LO,
    THEME_CLR_FIXED_SPLASH_3,
    /* A’s immersive record and lyrics keep their original appearance in every theme. */
    THEME_CLR_FIXED_MEDIA_BLACK,
    THEME_CLR_FIXED_MEDIA_WHITE,
    THEME_CLR_FIXED_MEDIA_TRACK,
    THEME_CLR_FIXED_MEDIA_LABEL,
    THEME_CLR_FIXED_MEDIA_HUB,
    THEME_CLR_FIXED_MEDIA_SPINDLE,
    THEME_CLR_FIXED_MEDIA_LYRIC_CURRENT,
    THEME_CLR_FIXED_MEDIA_LYRIC_ADJACENT,
    THEME_CLR_FIXED_MEDIA_HINT,
    THEME_CLR_FIXED_MEDIA_TITLE,
    THEME_CLR_FIXED_MEDIA_ARTIST,
    /* Optional component refinements; older custom themes inherit their standard role. */
    THEME_CLR_VOLUME_TRACK,
    THEME_CLR_VOLUME_BADGE,
    THEME_CLR_VOLUME_TICK_MAJOR,
    THEME_CLR_VOLUME_TICK_MINOR,
    THEME_CLR_VOLUME_DIGITS,
    THEME_CLR_VOLUME_CAPTION,
    THEME_CLR_PAGE_DOT_QUIET,
    THEME_CLR_SAVER_TITLE,
    THEME_CLR_SAVER_SUBTITLE,
    THEME_CLR_SAVER_ARTIST,
    THEME_CLR_SAVER_RING,
    THEME_CLR_SAVER_DISC,
    THEME_CLR_EQ_ZERO,
    THEME_CLR_EQ_SLOT,
    THEME_CLR_EQ_CAP_DISABLED,
    THEME_CLR_EQ_FADER,
    THEME_CLR_EQ_FADER_DISABLED,
    THEME_CLR_EQ_READONLY,
    THEME_CLR_EQ_CHIP,
    THEME_CLR_BOOK_HEARD,
    THEME_CLR_WEATHER_SUN,
    THEME_CLR_WEATHER_MOON,
    THEME_CLR_WEATHER_RAIN,
    THEME_CLR_WEATHER_BORDER,
    THEME_CLR_TOAST_SURFACE,
    THEME_CLR_TOAST_BORDER,
    THEME_CLR_TOAST_ORB,
    THEME_CLR_ORBIT_PRESSED,
    THEME_CLR_ORBIT_SHADOW,
    THEME_CLR_ORBIT_POINTER,
    THEME_CLR_ORBIT_HUB,
    THEME_CLR_USAGE_GRID,
    THEME_CLR_USAGE_TEXT,
    THEME_CLR_USAGE_AMBER,
    THEME_CLR_USAGE_FAINT,
    THEME_CLR_QUEUE_DRAG,
    THEME_CLR_QUEUE_HANDLE,
    THEME_CLR_PRIMARY_PRESSED,
    THEME_CLR_SWITCH_SELECTED,
    THEME_CLR_DANGER_DIALOG,
    THEME_CLR_POSTER_SUBTITLE,
    THEME_CLR_POSTER_SIDE,
    THEME_CLR_USAGE_CHARGE_BAND,
    THEME_CLR_USAGE_BAND,
    THEME_CLR_COUNT
} theme_color_role_t;

#endif
