/* NetHack 5.0 i18n.h */
/* Copyright (c) NetHack i18n contributors, 2026. */
/* NetHack may be freely redistributed.  See license for details. */

#ifndef I18N_H
#define I18N_H

/* Translation catalogs: GNU PO subset, one file per language.  See
   src/i18n.c.  Lookup keys are the English strings themselves, so with no
   language set every function here returns its argument unchanged. */

/* init and free */
extern void i18n_init(void);
extern void i18n_free(void);

/* the "language" option; code is "" or "en" for none, else letters only */
extern boolean i18n_set_language(const char *);
extern const char *i18n_language(void);    /* "" when none */
extern int i18n_skipped(void);    /* entries dropped by the last load */

/* lookup; each returns its (first) msgid argument on a miss */
extern const char *i18n_gettext(const char *);
extern const char *i18n_pgettext(const char *, const char *);
extern const char *i18n_ngettext(const char *, const char *, long);

#endif /* I18N_H */
