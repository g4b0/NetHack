/* NetHack 5.0 i18n.c */
/* Copyright (c) NetHack i18n contributors, 2026. */
/* NetHack may be freely redistributed.  See license for details. */

#include "hack.h"
#include "dlb.h"

/*
 * Translation catalogs.
 *
 * A catalog is a file lang_<code>.po in the game's data directory (or inside
 * the dlb archive): a subset of GNU gettext's PO format.  Entries are
 * separated by blank lines and made of
 *      msgctxt "context"               (optional)
 *      msgid "English text"
 *      msgid_plural "English plural"   (optional)
 *      msgstr "translation"            or msgstr[0] "..." msgstr[1] "..."
 * Lines starting with '#' are comments, adjacent "..." strings are
 * concatenated, and the escapes \n \t \\ \" are understood.  The entry with
 * an empty msgid is the file's header and is skipped.  Anything malformed is
 * dropped without disturbing its neighbours and counted; i18n_skipped()
 * reports the count so the caller can say so once.
 *
 * Lookup is by the English string itself, so with no language set (the
 * default) each lookup function is a single test and returns its argument.
 * The table is a chained hash keyed like gettext's: msgid alone, or
 * context + '\004' + msgid.
 *
 * i18n_vsnprintf() is vsnprintf() for formats that have a translation.  A
 * translated format is never handed to the C library: the English format
 * says what type each argument is, the arguments are read from the va_list
 * in that order, and the translation is rendered one conversion at a time.
 * That lets a translation drop or reorder arguments (%2$s) on every port,
 * and a translation whose conversions do not fit the English ones is
 * refused when the catalog is read, so a catalog cannot crash the game.
 * Supported conversions: d i u x X o c s p, with the length modifiers l and
 * ll on the integer ones, flags - + space # 0, width and precision as
 * numbers or '*', and %%.  Nothing else.
 */

#define I18N_MAXFORMS 6      /* msgstr[0] .. msgstr[5] */
#define I18N_CODELEN 9       /* up to 8 letters and a NUL */
#define I18N_LINELEN 2560    /* longest PO line read; longer ones are bad */
#define I18N_CTXSEP '\004'   /* separates context from msgid in a key */
#define I18N_MAXARGS 16      /* arguments (including '*') in one format */

/* flags of a conversion */
#define CF_MINUS 1
#define CF_PLUS 2
#define CF_SPACE 4
#define CF_ALT 8
#define CF_ZERO 16

/* what va_arg() must fetch for an argument */
enum i18n_atype {
    AT_INT, AT_UINT, AT_LONG, AT_ULONG, AT_LLONG, AT_ULLONG, AT_STR, AT_PTR
};

/* one conversion specification, as parsed */
struct i18n_conv {
    int pos;                    /* 1-based argument number, 0 if sequential */
    int flags;                  /* CF_* */
    int width, prec;            /* -1 if absent */
    boolean wstar, pstar;       /* width or precision comes from an int arg */
    char conv;                  /* d i u x X o c s p, or % for "%%" */
    int len;                    /* 0, 1 for l, 2 for ll */
    enum i18n_atype type;
};

/* an argument fetched from the va_list */
struct i18n_arg {
    enum i18n_atype type;
    union {
        int i;
        unsigned u;
        long l;
        unsigned long ul;
        long long ll;
        unsigned long long ull;
        const char *s;
        void *p;
    } v;
};

struct i18n_entry {
    struct i18n_entry *next;
    unsigned long hash;
    char *key;                  /* msgid, or context + I18N_CTXSEP + msgid */
    char *str[I18N_MAXFORMS];   /* msgstr forms; NULL if absent or empty */
    int nforms;                 /* number of msgstr forms in the file */
    boolean plural;             /* entry had msgid_plural */
};

/* growable string, used while reading */
struct i18n_sbuf {
    char *p;
    size_t len, cap;
};

/* where the reader is within one PO entry */
enum i18n_pestate { PE_NONE, PE_CTX, PE_ID, PE_PLURAL, PE_STR };

/* a PO entry being assembled */
struct i18n_pe {
    enum i18n_pestate state;
    struct i18n_sbuf ctx, id, plural, str[I18N_MAXFORMS];
    struct i18n_sbuf *cur;      /* where "..." continuation lines go */
    int nforms, seen;           /* msgstr forms read; bitmask of indexes */
    boolean has_ctx, has_plural, bad;
};

staticfn unsigned long i18n_hash(const char *);
staticfn struct i18n_entry *i18n_find(const char *);
staticfn void i18n_grow(void);
staticfn void i18n_store(const char *, struct i18n_pe *);
staticfn void i18n_unload(void);
staticfn void i18n_sb_add(struct i18n_sbuf *, char);
staticfn void i18n_sb_reset(struct i18n_sbuf *);
staticfn boolean i18n_unquote(struct i18n_sbuf *, const char *);
staticfn const char *i18n_kw(const char *, const char *);
staticfn void i18n_pe_reset(struct i18n_pe *);
staticfn void i18n_pe_bad(struct i18n_pe *);
staticfn void i18n_pe_text(struct i18n_pe *, const char *);
staticfn void i18n_pe_flush(struct i18n_pe *);
staticfn void i18n_parse_line(struct i18n_pe *, char *);
staticfn boolean i18n_load(const char *);
staticfn boolean i18n_valid_code(const char *);
staticfn boolean i18n_conv(const char **, struct i18n_conv *);
staticfn boolean i18n_scan(const char *, struct i18n_conv *, int *,
                           boolean *);
staticfn int i18n_slots(const struct i18n_conv *, int, enum i18n_atype *);
staticfn boolean i18n_fmt_ok(const char *, const char *);
staticfn void i18n_fetch(struct i18n_arg *, const enum i18n_atype *, int,
                         va_list);
staticfn void i18n_lit(char *, size_t, size_t *, const char *, size_t);
staticfn void i18n_emit(char *, size_t, size_t *, const struct i18n_conv *,
                        int, int, const struct i18n_arg *);
staticfn int i18n_render(char *, size_t, const char *,
                         const struct i18n_arg *, int);

static char i18n_lang[I18N_CODELEN];    /* "" when no language is set */
static struct i18n_entry **i18n_tab;    /* hash buckets, a power of two */
static unsigned i18n_ntab, i18n_nent;
static int i18n_nskipped;               /* entries dropped by last load */

void
i18n_init(void)
{
    i18n_unload();
}

void
i18n_free(void)
{
    i18n_unload();
}

/* set the language, or with ""/NULL/"en" none; if the catalog cannot be
   opened, return FALSE and leave no language set */
boolean
i18n_set_language(const char *code)
{
    i18n_unload();
    if (!code || !*code || !strcmp(code, "en"))
        return TRUE;
    if (!i18n_valid_code(code) || !i18n_load(code))
        return FALSE;
    Strcpy(i18n_lang, code);
    return TRUE;
}

const char *
i18n_language(void)
{
    return i18n_lang;
}

int
i18n_skipped(void)
{
    return i18n_nskipped;
}

const char *
i18n_gettext(const char *msgid)
{
    struct i18n_entry *e;

    if (!i18n_lang[0])
        return msgid;
    e = i18n_find(msgid);
    return (e && e->str[0]) ? e->str[0] : msgid;
}

const char *
i18n_pgettext(const char *ctx, const char *msgid)
{
    struct i18n_entry *e;
    char key[BUFSZ];

    if (!i18n_lang[0])
        return msgid;
    if (!ctx)
        return i18n_gettext(msgid);
    if (strlen(ctx) + 1 + strlen(msgid) >= sizeof key)
        return msgid;
    Sprintf(key, "%s%c%s", ctx, I18N_CTXSEP, msgid);
    e = i18n_find(key);
    return (e && e->str[0]) ? e->str[0] : msgid;
}

const char *
i18n_ngettext(const char *sing, const char *plur, long n)
{
    struct i18n_entry *e;
    int idx;

    if (i18n_lang[0] && (e = i18n_find(sing)) != 0) {
        /* Germanic rule for every language for now: form 0 for n == 1 and
           form 1 for the rest, or the last form the catalog has;
           per-language plural rules come later */
        idx = (n != 1L) ? 1 : 0;
        if (idx >= e->nforms)
            idx = e->nforms - 1;
        if (e->str[idx])
            return e->str[idx];
    }
    return (n == 1L) ? sing : plur;
}

/* [a-z][a-z_A-Z]{1,7}: a code goes into a file name, so be strict */
staticfn boolean
i18n_valid_code(const char *code)
{
    const char *p;

    if (!(*code >= 'a' && *code <= 'z'))
        return FALSE;
    for (p = code + 1; *p; ++p)
        if (!(*p == '_' || (*p >= 'a' && *p <= 'z')
              || (*p >= 'A' && *p <= 'Z')))
            return FALSE;
    return (p - code >= 2 && p - code < I18N_CODELEN);
}

/* FNV-1a, kept to 32 bits */
staticfn unsigned long
i18n_hash(const char *s)
{
    unsigned long h = 2166136261UL;

    for (; *s; ++s)
        h = ((h ^ (unsigned long) (uchar) *s) * 16777619UL) & 0xffffffffUL;
    return h;
}

staticfn struct i18n_entry *
i18n_find(const char *key)
{
    unsigned long h;
    struct i18n_entry *e;

    if (!i18n_tab)
        return (struct i18n_entry *) 0;
    h = i18n_hash(key);
    for (e = i18n_tab[h & (i18n_ntab - 1)]; e; e = e->next)
        if (e->hash == h && !strcmp(e->key, key))
            return e;
    return (struct i18n_entry *) 0;
}

/* double the number of buckets (or create the table) */
staticfn void
i18n_grow(void)
{
    unsigned i, nn = i18n_ntab ? i18n_ntab * 2U : 256U;
    struct i18n_entry **nt, *e, *nxt;

    nt = (struct i18n_entry **) alloc(nn * (unsigned) sizeof *nt);
    (void) memset((genericptr_t) nt, 0, nn * sizeof *nt);
    for (i = 0; i < i18n_ntab; ++i)
        for (e = i18n_tab[i]; e; e = nxt) {
            nxt = e->next;
            e->next = nt[e->hash & (nn - 1)];
            nt[e->hash & (nn - 1)] = e;
        }
    if (i18n_tab)
        free((genericptr_t) i18n_tab);
    i18n_tab = nt;
    i18n_ntab = nn;
}

/* add the finished entry in 'pe' under 'key'; a repeated key replaces the
   earlier one; an entry with nothing translated is not worth keeping */
staticfn void
i18n_store(const char *key, struct i18n_pe *pe)
{
    struct i18n_entry *e;
    int i;
    boolean any = FALSE;

    for (i = 0; i < pe->nforms; ++i)
        if (pe->str[i].len)
            any = TRUE;
    if (!any)
        return;

    e = i18n_find(key);
    if (e) {
        for (i = 0; i < I18N_MAXFORMS; ++i)
            if (e->str[i])
                free((genericptr_t) e->str[i]), e->str[i] = (char *) 0;
    } else {
        if (!i18n_tab || i18n_nent >= i18n_ntab * 2U)
            i18n_grow();
        e = (struct i18n_entry *) alloc(sizeof *e);
        (void) memset((genericptr_t) e, 0, sizeof *e);
        e->key = dupstr(key);
        e->hash = i18n_hash(key);
        e->next = i18n_tab[e->hash & (i18n_ntab - 1)];
        i18n_tab[e->hash & (i18n_ntab - 1)] = e;
        ++i18n_nent;
    }
    e->nforms = pe->nforms;
    e->plural = pe->has_plural;
    for (i = 0; i < pe->nforms; ++i)
        if (pe->str[i].len)
            e->str[i] = dupstr(pe->str[i].p);
}

/* drop the catalog and the language */
staticfn void
i18n_unload(void)
{
    unsigned i;
    int k;
    struct i18n_entry *e, *nxt;

    for (i = 0; i < i18n_ntab; ++i)
        for (e = i18n_tab[i]; e; e = nxt) {
            nxt = e->next;
            for (k = 0; k < I18N_MAXFORMS; ++k)
                if (e->str[k])
                    free((genericptr_t) e->str[k]);
            free((genericptr_t) e->key);
            free((genericptr_t) e);
        }
    if (i18n_tab)
        free((genericptr_t) i18n_tab);
    i18n_tab = (struct i18n_entry **) 0;
    i18n_ntab = i18n_nent = 0;
    i18n_nskipped = 0;
    i18n_lang[0] = '\0';
}

/* ---- the safe formatter ---- */

/* parse the conversion that follows a '%' at *pp; on success advance *pp
   past it; FALSE if it is not one we support */
staticfn boolean
i18n_conv(const char **pp, struct i18n_conv *cv)
{
    const char *p = *pp, *q;
    int n, nd;

    (void) memset((genericptr_t) cv, 0, sizeof *cv);
    cv->width = cv->prec = -1;
    if (*p == '%') {
        cv->conv = '%';
        *pp = p + 1;
        return TRUE;
    }
    /* "N$" selects an argument; a leading 0 is a flag, not a number */
    for (q = p, n = 0, nd = 0; *q >= '0' && *q <= '9' && nd < 4; ++q, ++nd)
        n = n * 10 + (*q - '0');
    if (q > p && *q == '$' && n > 0) {
        if (n > I18N_MAXARGS)
            return FALSE;
        cv->pos = n;
        p = q + 1;
    }
    for (;; ++p) {
        if (*p == '-')
            cv->flags |= CF_MINUS;
        else if (*p == '+')
            cv->flags |= CF_PLUS;
        else if (*p == ' ')
            cv->flags |= CF_SPACE;
        else if (*p == '#')
            cv->flags |= CF_ALT;
        else if (*p == '0')
            cv->flags |= CF_ZERO;
        else
            break;
    }
    if (*p == '*') {
        if (cv->pos)
            return FALSE;
        cv->wstar = TRUE;
        ++p;
    } else if (*p >= '0' && *p <= '9') {
        for (n = 0, nd = 0; *p >= '0' && *p <= '9'; ++p, ++nd) {
            if (nd >= 4)
                return FALSE;
            n = n * 10 + (*p - '0');
        }
        cv->width = n;
    }
    if (*p == '.') {
        ++p;
        if (*p == '*') {
            if (cv->pos)
                return FALSE;
            cv->pstar = TRUE;
            ++p;
        } else {
            for (n = 0, nd = 0; *p >= '0' && *p <= '9'; ++p, ++nd) {
                if (nd >= 4)
                    return FALSE;
                n = n * 10 + (*p - '0');
            }
            cv->prec = n; /* "%.s" means precision 0 */
        }
    }
    if (*p == 'l') {
        cv->len = 1;
        if (*++p == 'l') {
            cv->len = 2;
            ++p;
        }
    }
    cv->conv = *p;
    switch (*p) {
    case 'd':
    case 'i':
        cv->type = (cv->len == 0) ? AT_INT
                   : (cv->len == 1) ? AT_LONG : AT_LLONG;
        break;
    case 'u':
    case 'x':
    case 'X':
    case 'o':
        cv->type = (cv->len == 0) ? AT_UINT
                   : (cv->len == 1) ? AT_ULONG : AT_ULLONG;
        break;
    case 'c':
    case 's':
    case 'p':
        if (cv->len)
            return FALSE;
        cv->type = (*p == 'c') ? AT_INT : (*p == 's') ? AT_STR : AT_PTR;
        break;
    default:
        return FALSE; /* includes %n, %f and a '%' at the end */
    }
    /* only flags that mean something for this conversion */
    if (((cv->flags & (CF_PLUS | CF_SPACE)) && *p != 'd' && *p != 'i')
        || ((cv->flags & CF_ALT) && *p != 'x' && *p != 'X' && *p != 'o')
        || ((cv->flags & CF_ZERO) && (*p == 'c' || *p == 's' || *p == 'p')))
        return FALSE;
    *pp = p + 1;
    return TRUE;
}

/* parse every conversion in 'fmt' (not counting "%%"); FALSE if any is
   unsupported, there are too many, or sequential and positional ones are
   mixed; *positional says which kind they are */
staticfn boolean
i18n_scan(
    const char *fmt,
    struct i18n_conv *cvs,
    int *ncv,
    boolean *positional)
{
    const char *p = fmt;
    struct i18n_conv cv;

    *ncv = 0;
    *positional = FALSE;
    while ((p = strchr(p, '%')) != 0) {
        ++p;
        if (!i18n_conv(&p, &cv))
            return FALSE;
        if (cv.conv == '%')
            continue;
        if (*ncv >= I18N_MAXARGS)
            return FALSE;
        if (*ncv == 0)
            *positional = (cv.pos != 0);
        else if ((cv.pos != 0) != *positional)
            return FALSE;
        cvs[(*ncv)++] = cv;
    }
    return TRUE;
}

/* the argument types a sequential format consumes, in order, '*' included;
   returns how many, or -1 if there would be too many */
staticfn int
i18n_slots(const struct i18n_conv *cvs, int ncv, enum i18n_atype *slots)
{
    int i, n = 0;

    for (i = 0; i < ncv; ++i) {
        if (n + 1 + (cvs[i].wstar ? 1 : 0) + (cvs[i].pstar ? 1 : 0)
            > I18N_MAXARGS)
            return -1;
        if (cvs[i].wstar)
            slots[n++] = AT_INT;
        if (cvs[i].pstar)
            slots[n++] = AT_INT;
        slots[n++] = cvs[i].type;
    }
    return n;
}

/* can the translation 'tr' be rendered from the arguments of English
   format 'eng'? */
staticfn boolean
i18n_fmt_ok(const char *eng, const char *tr)
{
    struct i18n_conv ec[I18N_MAXARGS], tc[I18N_MAXARGS];
    enum i18n_atype slots[I18N_MAXARGS];
    int ne, nt, ns, i, idx, next;
    boolean epos, tpos, star = FALSE;

    if (!strchr(eng, '%'))
        return (strchr(tr, '%') == 0);
    if (!i18n_scan(eng, ec, &ne, &epos) || epos
        || (ns = i18n_slots(ec, ne, slots)) < 0)
        return FALSE;
    if (!i18n_scan(tr, tc, &nt, &tpos))
        return FALSE;
    for (i = 0; i < nt; ++i)
        if (tc[i].wstar || tc[i].pstar)
            star = TRUE;
    if (star) {
        /* '*' is only accepted where it is plainly the English one */
        if (tpos || nt != ne)
            return FALSE;
        for (i = 0; i < nt; ++i)
            if (tc[i].type != ec[i].type || tc[i].wstar != ec[i].wstar
                || tc[i].pstar != ec[i].pstar)
                return FALSE;
        return TRUE;
    }
    for (i = 0, next = 0; i < nt; ++i) {
        idx = tc[i].pos ? tc[i].pos - 1 : next++;
        if (idx >= ns || slots[idx] != tc[i].type)
            return FALSE;
    }
    return TRUE;
}

/* read the arguments of an English format from the va_list; the types
   come from i18n_slots() */
staticfn void
i18n_fetch(
    struct i18n_arg *args,
    const enum i18n_atype *slots,
    int n,
    va_list ap)
{
    int i;

    for (i = 0; i < n; ++i) {
        args[i].type = slots[i];
        switch (slots[i]) {
        case AT_INT:
            args[i].v.i = va_arg(ap, int);
            break;
        case AT_UINT:
            args[i].v.u = va_arg(ap, unsigned);
            break;
        case AT_LONG:
            args[i].v.l = va_arg(ap, long);
            break;
        case AT_ULONG:
            args[i].v.ul = va_arg(ap, unsigned long);
            break;
        case AT_LLONG:
            args[i].v.ll = va_arg(ap, long long);
            break;
        case AT_ULLONG:
            args[i].v.ull = va_arg(ap, unsigned long long);
            break;
        case AT_STR:
            args[i].v.s = va_arg(ap, const char *);
            break;
        case AT_PTR:
            args[i].v.p = va_arg(ap, void *);
            break;
        }
    }
}

/* append literal text; 'total' counts what the result would be, the buffer
   keeps room for the terminating NUL */
staticfn void
i18n_lit(char *buf, size_t siz, size_t *total, const char *s, size_t len)
{
    size_t k;

    for (k = 0; k < len; ++k)
        if (*total + k + 1 < siz)
            buf[*total + k] = s[k];
    *total += len;
}

DISABLE_WARNING_FORMAT_NONLITERAL

/* append one conversion, rendered by snprintf from a rebuilt single
   conversion spec; width and prec are the values to use (-1 for none) */
staticfn void
i18n_emit(
    char *buf,
    size_t siz,
    size_t *total,
    const struct i18n_conv *cv,
    int width,
    int prec,
    const struct i18n_arg *a)
{
    char spec[40], *sp = spec;
    char *dst = (*total < siz) ? buf + *total : (char *) 0;
    size_t room = dst ? siz - *total : 0;
    int n = 0;

    *sp++ = '%';
    if (cv->flags & CF_MINUS)
        *sp++ = '-';
    if (cv->flags & CF_PLUS)
        *sp++ = '+';
    if (cv->flags & CF_SPACE)
        *sp++ = ' ';
    if (cv->flags & CF_ALT)
        *sp++ = '#';
    if (cv->flags & CF_ZERO)
        *sp++ = '0';
    if (width >= 0)
        sp += snprintf(sp, sizeof spec - (size_t) (sp - spec), "%d", width);
    if (prec >= 0)
        sp += snprintf(sp, sizeof spec - (size_t) (sp - spec), ".%d", prec);
    if (cv->len >= 1)
        *sp++ = 'l';
    if (cv->len >= 2)
        *sp++ = 'l';
    *sp++ = cv->conv;
    *sp = '\0';

    switch (a->type) {
    case AT_INT:
        n = snprintf(dst, room, spec, a->v.i);
        break;
    case AT_UINT:
        n = snprintf(dst, room, spec, a->v.u);
        break;
    case AT_LONG:
        n = snprintf(dst, room, spec, a->v.l);
        break;
    case AT_ULONG:
        n = snprintf(dst, room, spec, a->v.ul);
        break;
    case AT_LLONG:
        n = snprintf(dst, room, spec, a->v.ll);
        break;
    case AT_ULLONG:
        n = snprintf(dst, room, spec, a->v.ull);
        break;
    case AT_STR:
        n = snprintf(dst, room, spec, a->v.s);
        break;
    case AT_PTR:
        n = snprintf(dst, room, spec, a->v.p);
        break;
    }
    if (n > 0)
        *total += (size_t) n;
}

/* render translation 'tr', which i18n_fmt_ok() accepted, from the English
   arguments; returns the length the result would have, like vsnprintf */
staticfn int
i18n_render(
    char *buf,
    size_t siz,
    const char *tr,
    const struct i18n_arg *args,
    int nargs)
{
    const char *p = tr;
    struct i18n_conv cv;
    size_t total = 0, run;
    int next = 0, idx, width, prec;

    while (*p) {
        if (*p != '%') {
            run = strcspn(p, "%");
            i18n_lit(buf, siz, &total, p, run);
            p += run;
            continue;
        }
        ++p;
        if (!i18n_conv(&p, &cv))
            break; /* cannot happen: the catalog was checked at load */
        if (cv.conv == '%') {
            i18n_lit(buf, siz, &total, "%", 1);
            continue;
        }
        width = cv.width;
        prec = cv.prec;
        if (cv.wstar && next < nargs) {
            width = args[next++].v.i;
            if (width < 0) { /* negative width means left justified */
                cv.flags |= CF_MINUS;
                width = (width < -99999) ? 99999 : -width;
            } else if (width > 99999) {
                width = 99999;
            }
        }
        if (cv.pstar && next < nargs) {
            prec = args[next++].v.i;
            if (prec > 99999)
                prec = 99999;
            /* negative precision is as if there were none; prec < 0 */
        }
        idx = cv.pos ? cv.pos - 1 : next++;
        if (idx >= nargs || args[idx].type != cv.type)
            continue; /* cannot happen either */
        i18n_emit(buf, siz, &total, &cv, width, prec, &args[idx]);
    }
    if (siz)
        buf[(total < siz) ? total : siz - 1] = '\0';
    return (int) total;
}

/* vsnprintf() that uses the translation of 'fmt' if there is one that fits */
int
i18n_vsnprintf(char *buf, size_t siz, const char *fmt, va_list ap)
{
    struct i18n_entry *e;
    struct i18n_conv cvs[I18N_MAXARGS];
    enum i18n_atype slots[I18N_MAXARGS];
    struct i18n_arg args[I18N_MAXARGS];
    int ncv, nslots;
    boolean positional;

    if (!i18n_lang[0] || (e = i18n_find(fmt)) == 0 || e->plural
        || !e->str[0])
        return vsnprintf(buf, siz, fmt, ap);
    /* the catalog was checked when it was read, so these cannot fail */
    if (!i18n_scan(fmt, cvs, &ncv, &positional) || positional
        || (nslots = i18n_slots(cvs, ncv, slots)) < 0)
        return vsnprintf(buf, siz, fmt, ap);
    i18n_fetch(args, slots, nslots, ap);
    return i18n_render(buf, siz, e->str[0], args, nslots);
}

RESTORE_WARNING_FORMAT_NONLITERAL

/* snprintf() through i18n_vsnprintf(); the tests use this one */
int
i18n_snprintf(char *buf, size_t siz, const char *fmt, ...)
{
    va_list ap;
    int ret;

    va_start(ap, fmt);
    ret = i18n_vsnprintf(buf, siz, fmt, ap);
    va_end(ap);
    return ret;
}

/* ---- reading a catalog ---- */

staticfn void
i18n_sb_add(struct i18n_sbuf *sb, char c)
{
    if (sb->len + 2 > sb->cap) {
        size_t ncap = sb->cap ? sb->cap * 2 : 64;
        char *np = (char *) alloc((unsigned) ncap);

        if (sb->p) {
            (void) memcpy((genericptr_t) np, (genericptr_t) sb->p, sb->len);
            free((genericptr_t) sb->p);
        }
        sb->p = np;
        sb->cap = ncap;
    }
    sb->p[sb->len++] = c;
    sb->p[sb->len] = '\0';
}

staticfn void
i18n_sb_reset(struct i18n_sbuf *sb)
{
    sb->len = 0;
    if (sb->p)
        sb->p[0] = '\0';
}

/* append the quoted string at 'p' to 'sb'; FALSE if it is malformed */
staticfn boolean
i18n_unquote(struct i18n_sbuf *sb, const char *p)
{
    char c;

    while (*p == ' ' || *p == '\t')
        ++p;
    if (*p++ != '"')
        return FALSE;
    for (; *p != '"'; ++p) {
        if (!*p)
            return FALSE; /* no closing quote */
        c = *p;
        if (c == '\\') {
            switch (*++p) {
            case 'n':
                c = '\n';
                break;
            case 't':
                c = '\t';
                break;
            case '\\':
            case '"':
                c = *p;
                break;
            default:
                return FALSE;
            }
        }
        i18n_sb_add(sb, c);
    }
    for (++p; *p == ' ' || *p == '\t'; ++p)
        continue;
    return (*p == '\0');
}

/* if 'p' starts with keyword 'kw' as a whole word, return what follows it */
staticfn const char *
i18n_kw(const char *p, const char *kw)
{
    size_t len = strlen(kw);

    if (strncmp(p, kw, len))
        return (const char *) 0;
    p += len;
    return (*p == ' ' || *p == '\t' || *p == '"') ? p : (const char *) 0;
}

staticfn void
i18n_pe_reset(struct i18n_pe *pe)
{
    int i;

    pe->state = PE_NONE;
    i18n_sb_reset(&pe->ctx);
    i18n_sb_reset(&pe->id);
    i18n_sb_reset(&pe->plural);
    for (i = 0; i < I18N_MAXFORMS; ++i)
        i18n_sb_reset(&pe->str[i]);
    pe->cur = (struct i18n_sbuf *) 0;
    pe->nforms = pe->seen = 0;
    pe->has_ctx = pe->has_plural = pe->bad = FALSE;
}

/* the current line, or entry, is malformed */
staticfn void
i18n_pe_bad(struct i18n_pe *pe)
{
    if (pe->state == PE_NONE)
        ++i18n_nskipped; /* stray line outside any entry: count it now */
    else
        pe->bad = TRUE;  /* counted once, when the entry ends */
}

/* add the quoted text at 'q' to the field being read */
staticfn void
i18n_pe_text(struct i18n_pe *pe, const char *q)
{
    if (!pe->cur || !i18n_unquote(pe->cur, q))
        i18n_pe_bad(pe);
}

/* the entry is complete: store it, or count it if it is unusable */
staticfn void
i18n_pe_flush(struct i18n_pe *pe)
{
    char *key;

    if (pe->state == PE_NONE)
        return;
    if (pe->state != PE_STR || pe->nforms == 0)
        pe->bad = TRUE; /* msgid without msgstr, or msgctxt alone */
    if (!pe->bad && !pe->has_plural && pe->str[0].len
        && (pe->id.len || pe->has_ctx)
        && !i18n_fmt_ok(pe->id.p ? pe->id.p : "", pe->str[0].p))
        pe->bad = TRUE; /* translation does not fit the English format */
    if (pe->bad) {
        ++i18n_nskipped;
    } else if (!pe->id.len && !pe->has_ctx) {
        ; /* the header */
    } else {
        key = (char *) alloc((unsigned) (pe->ctx.len + pe->id.len + 2));
        if (pe->has_ctx)
            Sprintf(key, "%s%c%s", pe->ctx.p ? pe->ctx.p : "", I18N_CTXSEP,
                    pe->id.p ? pe->id.p : "");
        else
            Strcpy(key, pe->id.p ? pe->id.p : "");
        i18n_store(key, pe);
        free((genericptr_t) key);
    }
    i18n_pe_reset(pe);
}

/* feed one line, newline already removed, to the entry reader */
staticfn void
i18n_parse_line(struct i18n_pe *pe, char *line)
{
    const char *p = line, *q;
    int idx;

    while (*p == ' ' || *p == '\t')
        ++p;
    if (!*p) {                  /* blank line ends an entry */
        i18n_pe_flush(pe);
    } else if (*p == '#') {     /* comment; after a msgstr it starts the
                                 * next entry's preamble */
        if (pe->state == PE_STR)
            i18n_pe_flush(pe);
    } else if ((q = i18n_kw(p, "msgctxt")) != 0) {
        i18n_pe_flush(pe);
        pe->state = PE_CTX;
        pe->has_ctx = TRUE;
        pe->cur = &pe->ctx;
        i18n_pe_text(pe, q);
    } else if ((q = i18n_kw(p, "msgid_plural")) != 0) {
        if (pe->state != PE_ID)
            i18n_pe_bad(pe);
        pe->state = PE_PLURAL;
        pe->has_plural = TRUE;
        pe->cur = &pe->plural;
        i18n_pe_text(pe, q);
    } else if ((q = i18n_kw(p, "msgid")) != 0) {
        if (pe->state > PE_CTX)
            i18n_pe_flush(pe);
        pe->state = PE_ID;
        pe->cur = &pe->id;
        i18n_pe_text(pe, q);
    } else if ((q = i18n_kw(p, "msgstr")) != 0) {
        if (pe->state != PE_ID)
            i18n_pe_bad(pe);
        pe->state = PE_STR;
        pe->cur = &pe->str[0];
        pe->nforms = 1;
        i18n_pe_text(pe, q);
    } else if (!strncmp(p, "msgstr[", 7)) {
        q = p + 7;
        idx = (*q >= '0' && *q <= '9') ? *q - '0' : -1;
        if (idx < 0 || idx >= I18N_MAXFORMS || q[1] != ']'
            || (q[2] != ' ' && q[2] != '\t' && q[2] != '"')
            || !pe->has_plural || (pe->state != PE_PLURAL
                                   && pe->state != PE_STR)
            || (pe->seen & (1 << idx)) != 0) {
            i18n_pe_bad(pe);
            if (pe->state != PE_NONE)
                pe->state = PE_STR;
            pe->cur = (struct i18n_sbuf *) 0;
        } else {
            pe->state = PE_STR;
            pe->seen |= 1 << idx;
            if (idx >= pe->nforms)
                pe->nforms = idx + 1;
            pe->cur = &pe->str[idx];
            i18n_pe_text(pe, q + 2);
        }
    } else if (*p == '"') {     /* continuation of the current field */
        i18n_pe_text(pe, p);
    } else {
        i18n_pe_bad(pe);
    }
}

/* read lang_<code>.po into the (empty) table; FALSE if it can't be opened */
staticfn boolean
i18n_load(const char *code)
{
    char fname[I18N_CODELEN + 12], line[I18N_LINELEN], *cp;
    boolean first = TRUE, toolong;
    struct i18n_pe pe;
    dlb *fp;
    int c;

    Sprintf(fname, "lang_%s.po", code);
    /* startup reads options before the data library is opened */
    dlb_init();
    fp = dlb_fopen(fname, RDTMODE);
    if (!fp)
        return FALSE;

    (void) memset((genericptr_t) &pe, 0, sizeof pe);
    while (dlb_fgets(line, (int) sizeof line, fp)) {
        cp = line;
        if (first && !strncmp(cp, "\xEF\xBB\xBF", 3))
            cp += 3; /* UTF-8 byte order mark */
        first = FALSE;
        toolong = FALSE;
        if (!strchr(cp, '\n')) {
            /* either the last line of the file, or too long for 'line' */
            c = dlb_fgetc(fp);
            if (c != EOF && c != '\n') {
                toolong = TRUE;
                while (c != EOF && c != '\n')
                    c = dlb_fgetc(fp);
            }
        }
        cp[strcspn(cp, "\r\n")] = '\0';
        i18n_parse_line(&pe, cp);
        if (toolong)
            i18n_pe_bad(&pe);
    }
    i18n_pe_flush(&pe);
    (void) dlb_fclose(fp);

    free((genericptr_t) pe.ctx.p);
    free((genericptr_t) pe.id.p);
    free((genericptr_t) pe.plural.p);
    for (c = 0; c < I18N_MAXFORMS; ++c)
        free((genericptr_t) pe.str[c].p);
    return TRUE;
}

/*i18n.c*/
