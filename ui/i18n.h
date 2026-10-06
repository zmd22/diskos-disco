/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
/* i18n.h - the interface language: tr("English text") returns the active language's text, or the English text
 * itself when the language is English or has no entry (so a missing translation never shows a blank or a key).
 * The languages follow the stock V2.57 resource variants (mq_ui 'Language/' resource). diskOS-only: the
 * choice is cfg "language" and is NOT sent to the player. Tables are generated (tools/generate_i18n.py from
 * the data file i18n/strings.tsv) into i18n_tables.c. */
#ifndef I18N_H
#define I18N_H

typedef enum { LANG_ZH, LANG_TW, LANG_EN, LANG_JA, LANG_KO, LANG_ES, LANG_PT, LANG_IT, LANG_DE, LANG_FR, LANG_RU,
               LANG_COUNT } i18n_lang_t;

typedef struct { const char *en; const char *text; } i18n_entry_t;
typedef struct { const i18n_entry_t *e; int n; } i18n_table_t;

extern const char *const i18n_lang_names[LANG_COUNT];   /* each language's name in its own script */
extern const i18n_table_t i18n_tables[LANG_COUNT];      /* sorted by .en; LANG_EN is empty */

int         i18n_lang(void);                            /* the active language (cfg "language", default LANG_EN) */
const char *i18n_lookup(int lang, const char *en);      /* that language's text, or NULL if it has none */
const char *tr(const char *en);                         /* the active language's text for `en`, else `en` */
const char *tr_sym(const char *sym, const char *en);   /* "<sym>  <tr(en)>", for a glyph-prefixed label */
int         i18n_needs_fallback_font(void);             /* 1 = not English: text needs the Latin-ext/Cyrillic/CJK chain */

#endif
