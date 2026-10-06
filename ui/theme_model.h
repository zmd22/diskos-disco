/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
/* theme_model.h - a custom colour theme as data (no LVGL): the model both theme builders (on the Disc and, later, in
 * the browser) edit, the one validator that decides whether it may be used, and its storage in
 * /usr/data/diskos-custom-theme.json. Every original non-fixed colour role must be present in BOTH variants; optional component roles inherit a standard role and every pair in
 * the contrast table must pass; the fixed identity roles always come from the built-in default. */
#ifndef THEME_MODEL_H
#define THEME_MODEL_H
#include <stdint.h>
#include "theme_roles.h"

#ifndef THEME_CUSTOM_PATH   /* overridable only so host tests can point it at a temp file */
#define THEME_CUSTOM_PATH     "/usr/data/diskos-custom-theme.json"
#endif
#define THEME_CUSTOM_MAX      16384   /* bytes: a full canonical document (two palettes of 66 roles) is ~4.3 KiB */
#define THEME_NAME_MAX        24

typedef struct {
    int      revision;                   /* 1 for the first save, then exactly +1 per save */
    char     name[THEME_NAME_MAX + 1];   /* 1-24 printable ASCII, no quotes or backslashes */
    int      accent_fixed;               /* 1 = always the theme's accent; 0 = album/user accents allowed */
    uint32_t pal[2][THEME_CLR_COUNT];    /* [0] dark, [1] light: 0xRRGGBB */
} theme_model_t;

enum { THEME_OK = 0, THEME_E_INVALID = -1, THEME_E_CONFLICT = -2, THEME_E_IO = -3,
       THEME_E_NOT_DURABLE = -4,   /* the new file IS in place, but the directory sync failed (may not survive power loss) */
       THEME_E_ABSENT = -5 };      /* load: nothing saved */
#define THEME_REV_REPLACE_DAMAGED (-2)   /* expect_revision: overwrite a present-but-unusable file (explicit user reset) */
#define THEME_REV_MAX 1000000000

int theme_role_fallback(int role); /* -1 for required/fixed roles */
const char *theme_role_key(int role);    /* "text_primary" etc.; NULL for a fixed (non-editable) role */
int  theme_model_validate(const theme_model_t *m, char *err, int errcap);            /* THEME_OK / THEME_E_INVALID */
int  theme_model_parse(const char *json, int len, theme_model_t *m, char *err, int errcap);
int  theme_model_serialize(const theme_model_t *m, char *out, int cap);              /* bytes, or -1 */
/* THE entry point for both builders. expect_revision = the revision the editor started from (0 = nothing was
 * saved), or THEME_REV_REPLACE_DAMAGED. The document's revision must be exactly current + 1 (1 for a first save).
 * Refused (file untouched): invalid document, stale/unexpected revision, a current file that is present but unusable
 * (damaged, unreadable, a symlink or not a regular file) unless THEME_REV_REPLACE_DAMAGED, or any I/O failure before
 * the rename. Thread- and process-safe: load, revision check and replace run under one lock. */
int  theme_validate_and_save(const char *json, int len, int expect_revision, const char *path, char *err, int errcap);
int  theme_model_load(const char *path, theme_model_t *m, char *err, int errcap);   /* OK / ABSENT / IO / INVALID */

#endif
