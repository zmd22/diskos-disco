/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
/* theme_kit.h - how a theme BUILDS things, beyond colours and fonts (theme.h).
 *
 * Every user action is wired with ui_on(), which names it (e.g. "np.fav") and says how essential it is. Themes may
 * rearrange a screen, but every CORE action must stay on its screen and every REACHABLE action must stay somewhere;
 * DECORATIVE things may be dropped. The host tests (tests/host) compare the named actions of every theme against
 * Default's, so a theme cannot lose a control without failing the gate.
 *
 * Components are built through the active theme's kit (theme_kit()). A theme overrides the builders it changes; any
 * builder it leaves NULL falls back to Default's, which is the UI's original code. */
#ifndef THEME_KIT_H
#define THEME_KIT_H
#include "lvgl/lvgl.h"

typedef enum {
    UI_CORE,        /* must be on the same screen in every theme (play/pause, seek, back, list rows, settings controls) */
    UI_REACHABLE,   /* must exist somewhere in every theme (favourite, sleep timer, lyrics, queue, song info) */
    UI_DECORATIVE,  /* a theme may drop it (tapping the art, the weather line when weather is reachable elsewhere) */
} ui_tier_t;

/* lv_obj_add_event_cb for a user action, with its stable name ("screen.thing[.gesture]") and tier */
void ui_on(lv_obj_t *obj, lv_event_cb_t cb, lv_event_code_t code, void *user_data, const char *action, ui_tier_t tier);
/* the action name of a player transport command ("np.prev", "np.pp", "np.next", else "np.cmd") */
const char *ui_transport_action(const char *cmd, const char *prefix, char *buf, int cap);

#ifdef DISKOS_HOST
const char *ui_action_of(const lv_obj_t *obj);   /* host tests: the action name(s) on obj, "a|b" (NULL = none) */
lv_obj_t   *ui_action_find(const char *name);    /* host tests: the newest object carrying that action (NULL = none) */
int         ui_action_tier(const lv_obj_t *obj); /* host tests: the strongest tier on obj (-1 = none) */
#endif

/* ---- the styling pass ----
 * When a screen is shown (and when a popup appears on the top layer) the active theme's styler visits every widget
 * once, with the widget's role worked out from what it is. Default has no styler: its widgets are never touched. */
typedef enum {
    KIT_SCREEN,          /* a screen root */
    KIT_TITLE,           /* a screen title (header font) */
    KIT_BACK,            /* the header back button */
    KIT_ROW,             /* a full-width tappable list row */
    KIT_ROW_TEXT,        /* the first label in a row (its title) */
    KIT_ROW_SUB,         /* further labels in a row (subtitle / value) */
    KIT_ROW_CHEVRON,     /* a row's drill-down chevron */
    KIT_BUTTON,          /* a tappable control with a text label */
    KIT_ICON_BUTTON,     /* a tappable control showing one glyph */
    KIT_CARD,            /* a non-tappable filled container with content */
    KIT_TEXT,            /* any other label */
    KIT_TOGGLE, KIT_SLIDER, KIT_ROLLER, KIT_ARC, KIT_BAR, KIT_TEXTAREA, KIT_KEYBOARD, KIT_IMAGE,
    KIT_SCRIM,           /* a full-screen dimmer on the top layer */
    KIT_POPUP,           /* a popup panel on the top layer (dialog, menu, toast) */
    KIT_OTHER,
} kit_role_t;
void kit_pass(lv_obj_t *root);          /* style root and everything under it not yet styled (no-op for Default) */
void kit_accent_changed(void); /* defer Ring recolouring until a screen becomes visible */
void kit_media_scope(lv_obj_t *root); /* keep the immersive subtree independent of appearance */
void kit_restyle(lv_obj_t *root);       /* style root's tree again (content rebuilt in place) */
void kit_install(void);                 /* screens_init: start the styling pass (no-op for Default) */
void kit_keep(lv_obj_t *obj);           /* a layout hook styled obj itself: the pass leaves it (not its children) */

/* Home's parts, as built by home.c, for a theme that lays Home out its own way. It may move, resize and restyle any
 * of them and hide only DECORATIVE ones (hint, thumb art/glyph, date); every control must stay reachable. */
typedef struct {
    lv_obj_t *root, *status, *clock, *date, *weather, *search, *library, *hint, *np, *np_thumb, *np_art, *np_glyph,
             *np_title, *np_artist, *pp, *pp_label;
} home_parts_t;

/* Now Playing's parts (ui.c). CORE: prev, pp, next, mode, the seek (ring, or a bar the theme passes to
 * ui_np_seek_line), times. REACHABLE: fav, sleep (they may move, e.g. into the NP menu, only with a replacement path).
 * DECORATIVE: backdrop, cover art, dots, the separator. The cover's tap box follows wherever the cover is. */
typedef struct {
    lv_obj_t *root, *backdrop, *ring, *cover, *cover_img, *cover_note, *fav, *fav_icon, *sleep, *sleep_icon, *mode,
             *mode_icon, *mode_one, *title, *artist, *album, *prev, *pp, *next, *elapsed, *sep, *remain, *dots[2];
} np_parts_t;

/* The Quick Settings drawer (quicksettings.c), rebuilt on every open. CORE: every tile (short + long press), the
 * brightness slider, the transport buttons when the user turned them on. tile[i]'s first child is its glyph. */
#define QS_TILES_MAX 6
typedef struct {
    lv_obj_t *root, *grab, *bright, *prev, *pp, *next;
    int n; lv_obj_t *tile[QS_TILES_MAX], *cap[QS_TILES_MAX];
} qs_parts_t;

/* The screensaver (saver.c) after it laid out the user's saver style (0 Cover, 1 Analog, 2 Minimal, 3 Digital,
 * 4 Vinyl). Display only: any touch wakes it. */
typedef struct { lv_obj_t *root, *clock, *date, *weather, *track, *artist, *face; int style; } saver_parts_t;

/* ---- the kit: component builders. NULL entries fall back to Default's. ---- */
typedef struct theme_kit {
    const char *id;
    /* screen header: title + back. Returns the title label. back_cb NULL = screen_back(). */
    lv_obj_t *(*header)(lv_obj_t *root, const char *title, lv_event_cb_t back_cb);
    /* the styler: called once per widget by the styling pass (NULL = leave Default's look) */
    void (*style)(lv_obj_t *obj, kit_role_t role);
    void (*home_build)(lv_obj_t *root); /* complete fork Home builder, before shared shortcuts */
    void (*fork_np_layout)(void); /* A's retained Now Playing layout over its renderer */
    void (*home)(const home_parts_t *p);      /* Home layout (after home.c built the default one) */
    void (*home_clock)(const char *time);     /* each Home clock update (after the clock label is set) */
    void (*nowplaying)(const np_parts_t *p);  /* Now Playing layout (after ui.c built the default one) */
    void (*quicksettings)(const qs_parts_t *p);   /* drawer layout (after each build) */
    void (*qs_tile)(lv_obj_t *tile, lv_obj_t *glyph, lv_obj_t *cap, int on, int toggle);   /* paint a tile lit / unlit (toggle: 0 = a one-shot action) */
    void (*saver)(const saver_parts_t *p);    /* screensaver touches (after each relayout) */
    void (*saver_clock)(const char *t);       /* the screensaver's time changed (a theme drawing its own clock) */
    void (*pass_end)(void);                   /* after a styling pass: work that needs the whole of a list at once */
    void (*np_state)(int playing, int have_track);   /* Now Playing's play state changed (a theme spelling it out) */
    void (*title)(lv_obj_t *label, const char *text);   /* set a screen title, already in the theme's case */
    void (*title_fit)(lv_obj_t *label, const char *text);   /* size a content-sized title for this text, BEFORE it is set
                                                            * (face, tracking, the box's width); the caller then sets it */
    const char *back_text;                    /* Back as text (e.g. "<"), NULL = the arrow icon */
    int saver_mer_apart;                      /* the saver's AM/PM beside the digits instead of in the clock face: 1 = in a small
                                               * text face (hi-fi: its segment face has no letters), 2 = in the theme's date
                                               * face (bauhaus: as on its Home, and "5:53 PM" was too wide for the saver) */
} theme_kit_t;

const theme_kit_t *theme_kit(void);          /* the active theme's kit, entries already resolved (never NULL) */
extern const theme_kit_t theme_kit_default;  /* the original diskOS components */
const theme_kit_t *theme_kit_for_active(void); /* theme.c: the active preset's kit (entries may be NULL) */
#endif
