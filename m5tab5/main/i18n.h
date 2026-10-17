/*
 * i18n.h -- the screen's text in English, Simplified Chinese or
 * Japanese, chosen at run time. lv_i18n's shape without LVGL, as in
 * defeatist-music-player-for-m5tab5 (whose i18n.c, i18n.h and
 * tools/i18n.py these are, cut to this game's three languages and its
 * screen; the browser-remote half is gone):
 *
 *     gfx_draw_text(..., _("Hint"), ...);
 *     static const char *const k[] = { N_("Light"), N_("Dark") };
 *     ... _(k[i]) ...
 *     snprintf(buf, n, _p("%d tiles", n), n);
 *
 * The English is the key. ./tools/i18n.py extract finds every marked
 * literal in main/ and lists it in i18n/<locale>.yml; compile turns those
 * into i18n_tab.c. The tool's docstring has the YAML format and rules.
 *
 * _() of anything the table does not hold returns its argument
 * unchanged, so _() on a pointer is always safe and never NULL for a
 * non-NULL argument.
 *
 * Only for text a person reads on the screen -- not log lines or NVS
 * keys.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

typedef enum {
    I18N_EN = 0,        /* also the fallback: the keys are English */
    I18N_ZH_CN,
    I18N_JA,
    I18N_LANG_COUNT,
} i18n_lang_t;

/* Takes effect on the next draw; nothing caches a translated pointer
 * across frames. Out-of-range values select English. */
void i18n_set_lang(i18n_lang_t lang);
i18n_lang_t i18n_lang(void);

/* Endonym for the language picker: "English", "简体中文", "日本語".
 * Deliberately not translated -- a reader has to find their own. */
const char *i18n_lang_name(i18n_lang_t lang);

const char *i18n_get(const char *msgid);
const char *i18n_get_plural(const char *msgid, long n);

#define _(s)      i18n_get(s)
#define N_(s)     (s)
/* Deliberately not translated: expands to its argument; what it adds is
 * the decision, written where the string is. */
#define same(s)   (s)
#define _p(s, n)  i18n_get_plural((s), (long)(n))

/* The generated tables, for i18n.c. */
extern const unsigned i18n_count;
extern const char *const i18n_keys[];
extern const unsigned i18n_pcount;
extern const char *const i18n_pkeys[];
