/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "i18n.h"
#include "config.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static int g_lang = -1;   /* read once: a language change re-launches the UI */

int i18n_lang(void){
    if(g_lang < 0){
        int v = cfg_get_int("language", LANG_EN);
        g_lang = (v >= 0 && v < LANG_COUNT) ? v : LANG_EN;
    }
    return g_lang;
}

static int cmp_entry(const void *k, const void *e){ return strcmp((const char *)k, ((const i18n_entry_t *)e)->en); }

const char *i18n_lookup(int lang, const char *en){
    if(!en || lang < 0 || lang >= LANG_COUNT || !i18n_tables[lang].n) return NULL;
    const i18n_entry_t *r = bsearch(en, i18n_tables[lang].e, (size_t)i18n_tables[lang].n, sizeof(i18n_entry_t), cmp_entry);
    return (r && r->text && r->text[0]) ? r->text : NULL;
}

const char *tr(const char *en){
    if(!en) return en;
    int l = i18n_lang();
    if(l == LANG_EN) return en;
    const char *t = i18n_lookup(l, en);
    return t ? t : en;
}

int i18n_needs_fallback_font(void){ return i18n_lang() != LANG_EN; }

/* a glyph prefix + the translated text ("<symbol>  Play All"); a small ring of buffers, so a few calls per statement
 * are safe. Each result is copied by lv_label_set_text at once. */
const char *tr_sym(const char *sym, const char *en){
    static char *ring[4]; static unsigned at;
    const char *prefix = sym ? sym : "";
    const char *word = tr(en);
    size_t n = strlen(prefix) + 2 + strlen(word) + 1;
    char *b = realloc(ring[at & 3], n);
    if(!b) return word;
    ring[at++ & 3] = b;
    snprintf(b, n, "%s  %s", prefix, word);
    return b;
}
