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
 */

#define I18N_MAXFORMS 6      /* msgstr[0] .. msgstr[5] */
#define I18N_CODELEN 9       /* up to 8 letters and a NUL */
#define I18N_LINELEN 2560    /* longest PO line read; longer ones are bad */
#define I18N_CTXSEP '\004'   /* separates context from msgid in a key */

struct i18n_entry {
    struct i18n_entry *next;
    unsigned long hash;
    char *key;                  /* msgid, or context + I18N_CTXSEP + msgid */
    char *str[I18N_MAXFORMS];   /* msgstr forms; NULL if absent or empty */
    int nforms;                 /* number of msgstr forms in the file */
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
