/* $NHDT-Header: NetHack 5.0 hacklib.c 2026/11/02_21:05:17 NetHack-5.0:1.137 $ */
/* Copyright (c) Stichting Mathematisch Centrum, Amsterdam, 1985. */
/*-Copyright (c) Michael Allison, 2007. */
/* Copyright (c) Robert Patrick Rankin, 1991                      */
/* NetHack may be freely redistributed.  See license for details. */

#include "hack.h" /* for config.h+extern.h */

staticfn int utf8_decode_lim(const char *, size_t, int *);
staticfn boolean utf8_is_mark(int);
staticfn size_t utf8_cut(const char *, int, size_t);
staticfn const char *utf8_prev_cp(const char *, const char *);

/*=
    Assorted 'small' utility routines.  They're virtually independent of
    NetHack.

      return type     routine name    argument type(s)
        boolean         digit           (char)
        boolean         letter          (char)
        char            highc           (char)
        char            lowc            (char)
        char *          lcase           (char *)
        char *          ucase           (char *)
        char *          upstart         (char *)
        char *          upwords         (char *)
        char *          mungspaces      (char *)
        char *          trimspaces      (char *)
        char *          strip_newline   (char *)
        char *          stripchars      (char *, const char *, const char *)
        char *          stripdigits     (char *)
        char *          eos             (char *)
        const char *    c_eos           (const char *)
        boolean         str_start_is    (const char *, const char *, boolean)
        boolean         str_end_is      (const char *, const char *)
        int             str_lines_maxlen (const char *)
        char *          strkitten       (char *,char)
        void            copynchars      (char *,const char *,int)
        char            chrcasecpy      (int,int)
        char *          strcasecpy      (char *,const char *)
        char *          s_suffix        (const char *)
        char *          ing_suffix      (const char *)
        char *          xcrypt          (const char *, char *)
        boolean         onlyspace       (const char *)
        char *          tabexpand       (char *)
        char *          visctrl         (char)
        char *          strsubst        (char *, const char *, const char *)
        int             strNsubst       (char *,const char *,const char *,int)
        const char *    findword        (const char *,const char *,int,boolean)
        const char *    ordin           (int)
        char *          sitoa           (int)
        int             sgn             (int)
        int             distmin         (coordxy, coordxy, coordxy, coordxy)
        int             dist2           (coordxy, coordxy, coordxy, coordxy)
        boolean         online2         (coordxy, coordxy)
        boolean         is_in_usignarr  (unsigned, unsigned *, int)
        int             strncmpi        (const char *, const char *, int)
        char *          strstri         (const char *, const char *)
        boolean         fuzzymatch      (const char *, const char *,
                                         const char *, boolean)
        int             swapbits        (int, int, int)
        void            nh_snprintf     (const char *, int, char *, size_t,
                                         const char *, ...)
        boolean         utf8_is_cont    (int)
        int             utf8_decode     (const char *, int *)
        int             utf8_cpwidth    (int)
        int             utf8_nwidth     (const char *, size_t)
        int             utf8_width      (const char *)
        int             utf8_len        (const char *)
        size_t          utf8_cut_cols   (const char *, int)
        size_t          utf8_cut_bytes  (const char *, size_t)
        const char *    utf8_prev       (const char *, const char *)
=*/

/* is 'c' a digit? */
boolean
digit(char c)
{
    return (boolean) ('0' <= c && c <= '9');
}

/* is 'c' a letter?  note: '@' classed as letter */
boolean
letter(char c)
{
    return (boolean) ('@' <= c && c <= 'Z') || ('a' <= c && c <= 'z');
}

/* force 'c' into uppercase */
char
highc(char c)
{
    return (char) (('a' <= c && c <= 'z') ? (c & ~040) : c);
}

/* force 'c' into lowercase */
char
lowc(char c)
{
    return (char) (('A' <= c && c <= 'Z') ? (c | 040) : c);
}

/* convert a string into all lowercase */
char *
lcase(char *s)
{
    char *p;

    for (p = s; *p; p++)
        if ('A' <= *p && *p <= 'Z')
            *p |= 040;
    return s;
}

/* convert a string into all uppercase */
char *
ucase(char *s)
{
    char *p;

    for (p = s; *p; p++)
        if ('a' <= *p && *p <= 'z')
            *p &= ~040;
    return s;
}

/* convert first character of a string to uppercase */
char *
upstart(char *s)
{
    if (s)
        *s = highc(*s);
    return s;
}

/* capitalize first letter of every word in a string (in place) */
char *
upwords(char *s)
{
    char *p;
    boolean space = TRUE;

    for (p = s; *p; p++)
        if (*p == ' ') {
            space = TRUE;
        } else if (space && letter(*p)) {
            *p = highc(*p);
            space = FALSE;
        } else {
            space = FALSE;
        }
    return s;
}

/* remove excess whitespace from a string buffer (in place) */
char *
mungspaces(char *bp)
{
    char c, *p, *p2;
    boolean was_space = TRUE;

    for (p = p2 = bp; (c = *p) != '\0'; p++) {
        if (c == '\n')
            break; /* treat newline the same as end-of-string */
        if (c == '\t')
            c = ' ';
        if (c != ' ' || !was_space)
            *p2++ = c;
        was_space = (c == ' ');
    }
    if (was_space && p2 > bp)
        p2--;
    *p2 = '\0';
    return bp;
}

/* skip leading whitespace; remove trailing whitespace, in place */
char *
trimspaces(char *txt)
{
    char *end;

    /* leading whitespace will remain in the buffer */
    while (*txt == ' ' || *txt == '\t')
        txt++;
    end = eos(txt);
    while (--end >= txt && (*end == ' ' || *end == '\t'))
        *end = '\0';

    return txt;
}

/* remove \n from end of line; remove \r too if one is there */
char *
strip_newline(char *str)
{
    char *p = strrchr(str, '\n');

    if (p) {
        if (p > str && *(p - 1) == '\r')
            --p;
        *p = '\0';
    }
    return str;
}

/* return the end of a string (pointing at '\0') */
char *
eos(char *s)
{
    while (*s)
        s++; /* s += strlen(s); */
    return s;
}

/* version of eos() which takes a const* arg and returns that result */
const char *
c_eos(const char *s)
{
    while (*s)
        s++; /* s += strlen(s); */
    return s;
}

/* determine whether 'str' starts with 'chkstr', possibly ignoring case;
 * panics on huge strings */
boolean
str_start_is(
    const char *str,
    const char *chkstr,
    boolean caseblind)
{
    char t1, t2;
    int n = LARGEST_INT;

    while (--n) {
        if (!*str)
            return (*chkstr == 0); /* chkstr >= str */
        else if (!*chkstr)
            return TRUE; /* chkstr < str */
        t1 = caseblind ? lowc(*str) : *str;
        t2 = caseblind ? lowc(*chkstr) : *chkstr;
        str++, chkstr++;
        if (t1 != t2)
            return FALSE;
    }
#if 0
    if (n == 0)
        panic("string too long");
#endif
    return TRUE;
}

/* determine whether 'str' ends in 'chkstr' */
boolean
str_end_is(const char *str, const char *chkstr)
{
    int clen = (int) strlen(chkstr);

    if ((int) strlen(str) >= clen)
        return (boolean) (!strncmp(eos((char *) str) - clen, chkstr, clen));
    return FALSE;
}

/* return max line length from buffer comprising newline-separated strings */
int
str_lines_maxlen(const char *str)
{
    const char *s1, *s2;
    int len, max_len = 0;

    s1 = str;
    while (s1 && *s1) {
        s2 = strchr(s1, '\n');
        if (s2) {
            len = (int) (s2 - s1);
            s1 = s2 + 1;
        } else {
            len = (int) strlen(s1);
            s1 = (char *) 0;
        }
        if (len > max_len)
            max_len = len;
    }

    return max_len;
}

/* append a character to a string (in place): strcat(s, {c,'\0'}); */
char *
strkitten(char *s, char c)
{
    char *p = eos(s);

    *p++ = c;
    *p = '\0';
    return s;
}

/* truncating string copy */
void
copynchars(char *dst, const char *src, int n)
{
    /* copies at most n characters, stopping sooner if terminator reached;
       treats newline as input terminator; unlike strncpy, always supplies
       '\0' terminator so dst must be able to hold at least n+1 characters */
    while (n > 0 && *src != '\0' && *src != '\n') {
        *dst++ = *src++;
        --n;
    }
    *dst = '\0';
}

/* convert char nc into oc's case; mostly used by strcasecpy */
char
chrcasecpy(int oc, int nc)
{
#if 0 /* this will be necessary if we switch to <ctype.h> */
    oc = (int) (unsigned char) oc;
    nc = (int) (unsigned char) nc;
#endif
    if ('a' <= oc && oc <= 'z') {
        /* old char is lower case; if new char is upper case, downcase it */
        if ('A' <= nc && nc <= 'Z')
            nc += 'a' - 'A'; /* lowc(nc) */
    } else if ('A' <= oc && oc <= 'Z') {
        /* old char is upper case; if new char is lower case, upcase it */
        if ('a' <= nc && nc <= 'z')
            nc += 'A' - 'a'; /* highc(nc) */
    }
    return (char) nc;
}

/* overwrite string, preserving old chars' case;
   for case-insensitive editions of makeplural() and makesingular();
   src might be shorter, same length, or longer than dst */
char *
strcasecpy(char *dst, const char *src)
{
    char *result = dst;
    int ic, oc, dst_exhausted = 0;

    /* while dst has characters, replace each one with corresponding
       character from src, converting case in the process if they differ;
       once dst runs out, propagate the case of its last character to any
       remaining src; if dst starts empty, it must be a pointer to the
       tail of some other string because we examine the char at dst[-1] */
    while ((ic = (int) *src++) != '\0') {
        if (!dst_exhausted && !*dst)
            dst_exhausted = 1;
        oc = (int) *(dst - dst_exhausted);
        *dst++ = chrcasecpy(oc, ic);
    }
    *dst = '\0';
    return result;
}

/* return a name converted to possessive */
char *
s_suffix(const char *s)
{
    static char buf[BUFSZ];

    Strcpy(buf, s);
    if (!strcmpi(buf, "it")) /* it -> its */
        Strcat(buf, "s");
    else if (!strcmpi(buf, "you")) /* you -> your */
        Strcat(buf, "r");
    else if (*(eos(buf) - 1) == 's') /* Xs -> Xs' */
        Strcat(buf, "'");
    else /* X -> X's */
        Strcat(buf, "'s");
    return buf;
}

/* construct a gerund (a verb formed by appending "ing" to a noun) */
char *
ing_suffix(const char *s)
{
    static const char vowel[] = "aeiouwy";
    static char buf[BUFSZ];
    char onoff[10];
    char *p;

    Strcpy(buf, s);
    p = eos(buf);
    onoff[0] = *p = *(p + 1) = '\0';
    if ((p >= &buf[3] && !strcmpi(p - 3, " on"))
        || (p >= &buf[4] && !strcmpi(p - 4, " off"))
        || (p >= &buf[5] && !strcmpi(p - 5, " with"))) {
        p = strrchr(buf, ' ');
        Strcpy(onoff, p);
        *p = '\0';
    }
    if (p >= &buf[2] && !strcmpi(p - 2, "er")) { /* slither + ing */
        /* nothing here */
    } else if (p >= &buf[3] && !strchr(vowel, *(p - 1))
        && strchr(vowel, *(p - 2)) && !strchr(vowel, *(p - 3))) {
        /* tip -> tipp + ing */
        *p = *(p - 1);
        *(p + 1) = '\0';
    } else if (p >= &buf[2] && !strcmpi(p - 2, "ie")) { /* vie -> vy + ing */
        *(p - 2) = 'y';
        *(p - 1) = '\0';
    } else if (p >= &buf[1] && *(p - 1) == 'e') /* grease -> greas + ing */
        *(p - 1) = '\0';
    Strcat(buf, "ing");
    if (onoff[0])
        Strcat(buf, onoff);
    return buf;
}

/* trivial text encryption routine (see makedefs) */
char *
xcrypt(const char *str, char *buf)
{
    const char *p;
    char *q;
    int bitmask;

    for (bitmask = 1, p = str, q = buf; *p; q++) {
        *q = *p++;
        if (*q & (32 | 64))
            *q ^= bitmask;
        if ((bitmask <<= 1) >= 32)
            bitmask = 1;
    }
    *q = '\0';
    return buf;
}

/* is a string entirely whitespace? */
boolean
onlyspace(const char *s)
{
    for (; *s; s++)
        if (*s != ' ' && *s != '\t')
            return FALSE;
    return TRUE;
}

/* expand tabs into proper number of spaces (in place) */
char *
tabexpand(
    char *sbuf) /* assumed to be [BUFSZ] but can be smaller provided that
                 * expanded string fits; expansion bigger than BUFSZ-1
                 * will be truncated */
{
    char buf[BUFSZ + 10];
    char *bp, *s = sbuf;
    int idx, n;

    if (!*s)
        return sbuf;
    for (bp = buf, idx = 0; *s; s++) {
        if (*s == '\t') {
            /*
             * clang-8's optimizer at -Os has been observed to mis-compile
             * this code.  Symptom is nethack getting stuck in an apparent
             * infinite loop (or perhaps just an extremely long one) when
             * examining data.base entries.
             * clang-9 doesn't exhibit this problem.  [Was the incorrect
             * optimization fixed or just disabled?]
             */
            do
                *bp++ = ' ';
            while (++idx % 8);
        } else if ((uchar) *s < 0x80) {
            *bp++ = *s;
            ++idx;
        } else {
            /* a UTF-8 character is several bytes but one tab stop column
               (or two or none: wide, combining) */
            int cp;

            n = utf8_decode(s, &cp);
            idx += (n > 1) ? utf8_cpwidth(cp) : 1;
            while (--n > 0)
                *bp++ = *s++;
            *bp++ = *s;
        }
        if (idx >= BUFSZ || bp - buf >= BUFSZ) {
            bp = &buf[BUFSZ - 1];
            break;
        }
    }
    *bp = 0;
    return strcpy(sbuf, buf);
}

#define VISCTRL_NBUF 5
/* make a displayable string from a character */
char *
visctrl(char c)
{
    static char visctrl_bufs[VISCTRL_NBUF][5];
    static int nbuf = 0;
    int i = 0;
    char *ccc = visctrl_bufs[nbuf];
    nbuf = (nbuf + 1) % VISCTRL_NBUF;

    if ((uchar) c & 0200) {
        ccc[i++] = 'M';
        ccc[i++] = '-';
    }
    c &= 0177;
    if (c < 040) {
        ccc[i++] = '^';
        ccc[i++] = c | 0100; /* letter */
    } else if (c == 0177) {
        ccc[i++] = '^';
        ccc[i++] = c & ~0100; /* '?' */
    } else {
        ccc[i++] = c; /* printable character */
    }
    ccc[i] = '\0';
    return ccc;
}

/* strip all the chars in stuff_to_strip from orig */
/* caller is responsible for ensuring that bp is a
   valid pointer to a BUFSZ buffer */
char *
stripchars(
    char *bp,
    const char *stuff_to_strip,
    const char *orig)
{
    int i = 0;
    char *s = bp;

    while (*orig && i < (BUFSZ - 1)) {
        if (!strchr(stuff_to_strip, *orig)) {
            *s++ = *orig;
            i++;
        }
        orig++;
    }
    *s = '\0';

    return bp;
}

/* remove digits from string */
char *
stripdigits(char *s)
{
    char *s1, *s2;

    for (s1 = s2 = s; *s1; s1++)
        if (*s1 < '0' || *s1 > '9')
            *s2++ = *s1;
    *s2 = '\0';

    return s;
}

/* substitute a word or phrase in a string (in place);
   caller is responsible for ensuring that bp points to big enough buffer */
char *
strsubst(
    char *bp,
    const char *orig,
    const char *replacement)
{
    char *found, buf[BUFSZ];
    /* [this could be replaced by strNsubst(bp, orig, replacement, 1)] */

    found = strstr(bp, orig);
    if (found) {
        Strcpy(buf, found + strlen(orig));
        Strcpy(found, replacement);
        Strcat(bp, buf);
    }
    return bp;
}

/* substitute the Nth occurrence of a substring within a string (in place);
   if N is 0, substitute all occurrences; returns the number of substitutions;
   maximum output length is BUFSZ (BUFSZ-1 chars + terminating '\0') */
int
strNsubst(
    char *inoutbuf,   /* current string, and result buffer */
    const char *orig, /* old substring; if "", insert in front of Nth char */
    const char *replacement, /* new substring; if "", delete old substring */
    int n) /* which occurrence to replace; 0 => all */
{
    char *bp, *op, workbuf[BUFSZ];
    const char *rp;
    unsigned len = (unsigned) strlen(orig);
    int ocount = 0, /* number of times 'orig' has been matched */
        rcount = 0; /* number of substitutions made */

    for (bp = inoutbuf, op = workbuf; *bp && op < &workbuf[BUFSZ - 1]; ) {
        if ((!len || !strncmp(bp, orig, len)) && (++ocount == n || n == 0)) {
            /* Nth match found */
            for (rp = replacement; *rp && op < &workbuf[BUFSZ - 1]; )
                *op++ = *rp++;
            ++rcount;
            if (len) {
                bp += len; /* skip 'orig' */
                continue;
            }
        }
        /* no match (or len==0) so retain current character */
        *op++ = *bp++;
    }
    if (!len && n == ocount + 1) {
        /* special case: orig=="" (!len) and n==strlen(inoutbuf)+1,
           insert in front of terminator (in other words, append);
           [when orig=="", ocount will have been incremented once for
           each input char] */
        for (rp = replacement; *rp && op < &workbuf[BUFSZ - 1]; )
            *op++ = *rp++;
        ++rcount;
    }
    if (rcount) {
        *op = '\0';
        Strcpy(inoutbuf, workbuf);
    }
    return rcount;
}

/* search for a word in a space-separated list; returns non-Null if found */
const char *
findword(
    const char *list,   /* string of space-separated words */
    const char *word,   /* word to try to find */
    int wordlen,        /* so that it isn't required to be \0 terminated */
    boolean ignorecase) /* T: case-blind, F: case-sensitive */
{
    const char *p = list;

    while (p) {
        while (*p == ' ')
            ++p;
        if (!*p)
            break;
        if ((ignorecase ? !strncmpi(p, word, wordlen)
                        : !strncmp(p, word, wordlen))
            && (p[wordlen] == '\0' || p[wordlen] == ' '))
            return p;
        p = strchr(p + 1, ' ');
    }
    return (const char *) 0;
}

/* return the ordinal suffix of a number */
const char *
ordin(int n)               /* note: should be non-negative */
{
    int dd = n % 10;

    return (dd == 0 || dd > 3 || (n % 100) / 10 == 1) ? "th"
               : (dd == 1) ? "st" : (dd == 2) ? "nd" : "rd";
}

DISABLE_WARNING_FORMAT_NONLITERAL  /* one compiler complains about
                                      result of ?: for format string */

/* make a signed digit string from a number */
char *
sitoa(int n)
{
    static char buf[13];

    Sprintf(buf, (n < 0) ? "%d" : "+%d", n);
    return buf;
}

RESTORE_WARNING_FORMAT_NONLITERAL

/* return the sign of a number: -1, 0, or 1 */
int
sgn(int n)
{
    return (n < 0) ? -1 : (n != 0);
}

/* distance between two points, in moves */
int
distmin(coordxy x0, coordxy y0, coordxy x1, coordxy y1)
{
    coordxy dx = x0 - x1, dy = y0 - y1;

    if (dx < 0)
        dx = -dx;
    if (dy < 0)
        dy = -dy;
    /*  The minimum number of moves to get from (x0,y0) to (x1,y1) is the
     *  larger of the [absolute value of the] two deltas.
     */
    return (dx < dy) ? dy : dx;
}

/* square of Euclidean distance between pair of pts */
int
dist2(coordxy x0, coordxy y0, coordxy x1, coordxy y1)
{
    coordxy dx = x0 - x1, dy = y0 - y1;

    return dx * dx + dy * dy;
}

/* integer square root function without using floating point */
int
isqrt(int val)
{
    int rt = 0;
    int odd = 1;
    /*
     * This could be replaced by a faster algorithm, but has not been because:
     * + the simple algorithm is easy to read;
     * + this algorithm does not require 64-bit support;
     * + in current usage, the values passed to isqrt() are not really that
     *   large, so the performance difference is negligible;
     * + isqrt() is used in only few places, which are not bottle-necks.
     */
    while (val >= odd) {
        val = val - odd;
        odd = odd + 2;
        rt = rt + 1;
    }
    return rt;
}

/* are two points lined up (on a straight line)? */
boolean
online2(coordxy x0, coordxy y0, coordxy x1, coordxy y1)
{
    int dx = x0 - x1, dy = y0 - y1;
    /*  If either delta is zero then they're on an orthogonal line,
     *  else if the deltas are equal (signs ignored) they're on a diagonal.
     */
    return (boolean) (!dy || !dx || dy == dx || dy == -dx);
}

/* return TRUE if value is in array of unsigned values arrlen length */
boolean
is_in_usignarr(unsigned value, unsigned *arr, int arrlen)
{
    int i;

    for (i = 0; i < arrlen; ++i)
        if (arr[i] == value)
            return TRUE;
    return FALSE;
}

#ifndef STRNCMPI
/* case-insensitive counted string comparison */
/*{ aka strncasecmp }*/
int
strncmpi(
    const char *s1, const char *s2,
    int n) /*(should probably be size_t, which is unsigned)*/
{
    char t1, t2;

    while (n--) {
        if (!*s2)
            return (*s1 != 0); /* s1 >= s2 */
        else if (!*s1)
            return -1; /* s1  < s2 */
        t1 = lowc(*s1++);
        t2 = lowc(*s2++);
        if (t1 != t2)
            return (t1 > t2) ? 1 : -1;
    }
    return 0; /* s1 == s2 */
}
#endif /* STRNCMPI */

#ifndef STRSTRI
/* case-insensitive substring search */
char *
strstri(const char *str, const char *sub)
{
    const char *s1, *s2;
    int i, k;
#define TABSIZ 0x20                  /* 0x40 would be case-sensitive */
    char tstr[TABSIZ], tsub[TABSIZ]; /* nibble count tables */
#if 0
    assert( (TABSIZ & ~(TABSIZ-1)) == TABSIZ ); /* must be exact power of 2 */
    assert( &lowc != 0 );                       /* can't be unsafe macro */
#endif

    /* special case: empty substring */
    if (!*sub)
        return (char *) str;

    /* do some useful work while determining relative lengths */
    for (i = 0; i < TABSIZ; i++)
        tstr[i] = tsub[i] = 0; /* init */
    for (k = 0, s1 = str; *s1; k++)
        tstr[*s1++ & (TABSIZ - 1)]++;
    for (s2 = sub; *s2; --k)
        tsub[*s2++ & (TABSIZ - 1)]++;

    /* evaluate the info we've collected */
    if (k < 0)
        return (char *) 0;       /* sub longer than str, so can't match */
    for (i = 0; i < TABSIZ; i++) /* does sub have more 'x's than str? */
        if (tsub[i] > tstr[i])
            return (char *) 0; /* match not possible */

    /* now actually compare the substring repeatedly to parts of the string */
    for (i = 0; i <= k; i++) {
        s1 = &str[i];
        s2 = sub;
        while (lowc(*s1++) == lowc(*s2++))
            if (!*s2)
                return (char *) &str[i]; /* full match */
    }
    return (char *) 0; /* not found */
}
#endif /* STRSTRI */

/* compare two strings for equality, ignoring the presence of specified
   characters (typically whitespace) and possibly ignoring case */
boolean
fuzzymatch(
    const char *s1, const char *s2,
    const char *ignore_chars,
    boolean caseblind)
{
    char c1, c2;

    do {
        while ((c1 = *s1++) != '\0' && strchr(ignore_chars, c1) != 0)
            continue;
        while ((c2 = *s2++) != '\0' && strchr(ignore_chars, c2) != 0)
            continue;
        if (!c1 || !c2)
            break; /* stop when end of either string is reached */

        if (caseblind) {
            c1 = lowc(c1);
            c2 = lowc(c2);
        }
    } while (c1 == c2);

    /* match occurs only when the end of both strings has been reached */
    return (boolean) (!c1 && !c2);
}

/*
 * Time routines
 *
 * The time is used for:
 *  - seed for rand()
 *  - year on tombstone and yyyymmdd in record file
 *  - phase of the moon (various monsters react to NEW_MOON or FULL_MOON)
 *  - night and midnight (the undead are dangerous at midnight)
 *  - determination of what files are "very old"
 */

/* TIME_type: type of the argument to time(); we actually use &(time_t);
   you might need to define either or both of these to 'long *' in *conf.h */
#ifndef TIME_type
#define TIME_type time_t *
#endif
#ifndef LOCALTIME_type
#define LOCALTIME_type time_t *
#endif

/* swapbits(val, bita, bitb) swaps bit a with bit b in val */
int
swapbits(int val, int bita, int bitb)
{
    int tmp = ((val >> bita) & 1) ^ ((val >> bitb) & 1);

    return (val ^ ((tmp << bita) | (tmp << bitb)));
}

DISABLE_WARNING_FORMAT_NONLITERAL

/*
 * Wrap snprintf for use in the main code.
 *
 * Wrap reasons:
 *   1. If there are any platform issues, we have one spot to fix them -
 *      snprintf is a routine with a troubling history of bad implementations.
 *   2. Add cumbersome error checking in one spot.  Problems with text
 *      wrangling do not have to be fatal.
 *   3. Gcc 9+ will issue a warning unless the return value is used.
 *      Annoyingly, explicitly casting to void does not remove the error.
 *      So, use the result - see reason #2.
 */
void
nh_snprintf(
    const char *func UNUSED, int line UNUSED,
    char *str, size_t size,
    const char *fmt, ...)
{
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(str, size, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t) n >= size) { /* is there a problem? */
#if 0
TODO: add set_impossible(), impossible -> func pointer,
 test funcpointer before call
        impossible("snprintf %s: func %s, file line %d",
                   (n < 0) ? "format error" : "overflow",
                   func, line);
#endif
        str[size - 1] = '\0'; /* make sure it is nul terminated */
    }
}

RESTORE_WARNING_FORMAT_NONLITERAL

/* Unicode routines */

int
unicodeval_to_utf8str(int uval, uint8 *buffer, size_t bufsz)
{
    //    static uint8 buffer[7];
    uint8 *b = buffer;

    if (bufsz < 5)
        return 0;
    /*
     *   Binary   Hex        Comments
     *   0xxxxxxx 0x00..0x7F Only byte of a 1-byte character encoding
     *   10xxxxxx 0x80..0xBF Continuation byte : one of 1-3 bytes following
     * first 110xxxxx 0xC0..0xDF First byte of a 2-byte character encoding
     *   1110xxxx 0xE0..0xEF First byte of a 3-byte character encoding
     *   11110xxx 0xF0..0xF7 First byte of a 4-byte character encoding
     */
    *b = '\0';
    if (uval < 0x80) {
        *b++ = uval;
    } else if (uval < 0x800) {
        *b++ = 192 + uval / 64;
        *b++ = 128 + uval % 64;
    } else if (uval - 0xd800u < 0x800) {
        return 0;
    } else if (uval < 0x10000) {
        *b++ = 224 + uval / 4096;
        *b++ = 128 + uval / 64 % 64;
        *b++ = 128 + uval % 64;
    } else if (uval < 0x110000) {
        *b++ = 240 + uval / 262144;
        *b++ = 128 + uval / 4096 % 64;
        *b++ = 128 + uval / 64 % 64;
        *b++ = 128 + uval % 64;
    } else {
        return 0;
    }
    *b = '\0'; /* NUL terminate */
    return 1;
}

/*
 * UTF-8 decoding and display width.  These are deliberately independent of
 * wchar.h, wcwidth() and the locale; the tty port never calls setlocale().
 * All of them accept arbitrary bytes: anything that is not well-formed UTF-8
 * is treated as single bytes of width 1, so every loop over a string makes
 * progress and nothing reads past the terminating NUL.
 */

/* is 'c' a UTF-8 continuation byte (10xxxxxx)? */
boolean
utf8_is_cont(int c)
{
    return (boolean) (((uchar) c & 0xC0) == 0x80);
}

/* decode one code point from at most 'lim' bytes of 's'; return the number
   of bytes used (1..4).  Anything invalid, overlong, a surrogate, above
   U+10FFFF or cut short by 'lim' or by a NUL, uses exactly one byte and
   yields U+FFFD.  (A genuine U+FFFD uses three bytes, so the length tells
   the two apart.)  A NUL yields 0 with a length of 1. */
staticfn int
utf8_decode_lim(const char *s, size_t lim, int *cpp)
{
    const uchar *p = (const uchar *) s;
    int cp, i, need, minval;

    if (p[0] < 0x80) {
        *cpp = p[0];
        return 1;
    }
    if (p[0] < 0xC2) { /* continuation byte, or overlong 2-byte lead */
        need = -1;
        minval = 0;
        cp = 0;
    } else if (p[0] < 0xE0) {
        need = 1;
        minval = 0x80;
        cp = p[0] & 0x1F;
    } else if (p[0] < 0xF0) {
        need = 2;
        minval = 0x800;
        cp = p[0] & 0x0F;
    } else if (p[0] < 0xF5) {
        need = 3;
        minval = 0x10000;
        cp = p[0] & 0x07;
    } else { /* F5..FF: would be above U+10FFFF, or a 5- or 6-byte form */
        need = -1;
        minval = 0;
        cp = 0;
    }
    for (i = 1; i <= need; ++i) {
        if ((size_t) i >= lim || !utf8_is_cont(p[i])) {
            need = -1;
            break;
        }
        cp = (cp << 6) | (p[i] & 0x3F);
    }
    if (need < 0 || cp < minval || cp > 0x10FFFF
        || (cp >= 0xD800 && cp <= 0xDFFF)) {
        *cpp = 0xFFFD;
        return 1;
    }
    *cpp = cp;
    return need + 1;
}

/* decode the code point at 's' into *cpp (which may be Null); return the
   number of bytes it uses, 1 to 4 */
int
utf8_decode(const char *s, int *cpp)
{
    int cp, n;

    n = utf8_decode_lim(s, 4, &cp);
    if (cpp)
        *cpp = cp;
    return n;
}

/* number of terminal columns used by code point 'cp': 0 for control
   characters and combining or other zero-width characters, 2 for East Asian
   wide and fullwidth characters, otherwise 1 */
int
utf8_cpwidth(int cp)
{
    /* sorted, non-overlapping */
    static const struct {
        int lo, hi;
    } zerowidth[] = {
        { 0x0300, 0x036F }, /* combining diacritical marks */
        { 0x1AB0, 0x1AFF }, /* combining diacritical marks extended */
        { 0x1DC0, 0x1DFF }, /* combining diacritical marks supplement */
        { 0x200B, 0x200F }, /* zero width space, joiners, direction marks */
        { 0x20D0, 0x20FF }, /* combining marks for symbols */
        { 0xFE00, 0xFE0F }  /* variation selectors */
    }, wide[] = {
        { 0x1100, 0x115F },   /* Hangul Jamo */
        { 0x2E80, 0x303E },   /* CJK radicals .. CJK symbols (not 303F) */
        { 0x3040, 0xA4CF },   /* kana .. Yi */
        { 0xAC00, 0xD7A3 },   /* Hangul syllables */
        { 0xF900, 0xFAFF },   /* CJK compatibility ideographs */
        { 0xFE30, 0xFE4F },   /* CJK compatibility forms */
        { 0xFF00, 0xFF60 },   /* fullwidth forms */
        { 0xFFE0, 0xFFE6 },   /* fullwidth signs */
        { 0x1F300, 0x1F64F }, /* pictographs and emoticons */
        { 0x1F900, 0x1F9FF }, /* supplemental symbols and pictographs */
        { 0x20000, 0x3FFFD }  /* CJK extensions B and up */
    };
    int i;

    if (cp < 0x20 || (cp >= 0x7F && cp < 0xA0)) /* C0, DEL, C1 */
        return 0;
    if (cp < 0x300) /* the common case */
        return 1;
    for (i = 0; i < SIZE(zerowidth); ++i)
        if (cp >= zerowidth[i].lo && cp <= zerowidth[i].hi)
            return 0;
    for (i = 0; i < SIZE(wide); ++i)
        if (cp >= wide[i].lo && cp <= wide[i].hi)
            return 2;
    return 1;
}

/* is 'cp' a mark which belongs to the character before it? */
staticfn boolean
utf8_is_mark(int cp)
{
    return (boolean) (cp >= 0x300 && utf8_cpwidth(cp) == 0);
}

/* display width of the first 'n' bytes of 's' (stopping early at a NUL);
   a character cut short by 'n' counts as invalid bytes, one column each */
int
utf8_nwidth(const char *s, size_t n)
{
    size_t used = 0;
    int cp, width = 0;

    while (used < n && s[used]) {
        used += utf8_decode_lim(s + used, n - used, &cp);
        width += utf8_cpwidth(cp);
    }
    return width;
}

/* display width of a NUL-terminated string */
int
utf8_width(const char *s)
{
    return utf8_nwidth(s, (size_t) -1);
}

/* number of code points in a string; each invalid byte counts as one */
int
utf8_len(const char *s)
{
    int count = 0;

    while (*s) {
        s += utf8_decode(s, (int *) 0);
        ++count;
    }
    return count;
}

/* return the largest byte offset into 's' such that the text before it is
   at most 'maxcols' columns (-1: no limit) and 'maxbytes' bytes, and the
   cut is between characters, never between a character and the combining
   marks that follow it; if the first character of a cluster does not fit,
   none of it is taken */
staticfn size_t
utf8_cut(const char *s, int maxcols, size_t maxbytes)
{
    size_t pos = 0, best = 0;
    int cp, cols = 0, cw;

    while (s[pos]) {
        pos += utf8_decode(s + pos, &cp);
        cw = utf8_cpwidth(cp);
        while (s[pos]) {
            int n = utf8_decode(s + pos, &cp);

            if (!utf8_is_mark(cp))
                break;
            pos += n;
        }
        if ((maxcols >= 0 && cw > maxcols - cols) || pos > maxbytes)
            break;
        cols += cw;
        best = pos;
    }
    return best;
}

/* byte offset at which to cut 's' to fit in 'maxcols' columns */
size_t
utf8_cut_cols(const char *s, int maxcols)
{
    return utf8_cut(s, maxcols < 0 ? 0 : maxcols, (size_t) -1);
}

/* byte offset at which to cut 's' to fit in 'maxbytes' bytes */
size_t
utf8_cut_bytes(const char *s, size_t maxbytes)
{
    return utf8_cut(s, -1, maxbytes);
}

/* start of the character which ends just before 'p', for p > start, where
   'start' is the beginning of the buffer; moves back at least one byte */
staticfn const char *
utf8_prev_cp(const char *start, const char *p)
{
    const char *q = p - 1;
    int cp, back = 0;

    /* at most three continuation bytes can belong to a lead byte */
    while (q > start && back < 3 && utf8_is_cont(*q)) {
        --q;
        ++back;
    }
    if (utf8_decode_lim(q, (size_t) (p - q), &cp) == (int) (p - q))
        return q;
    return p - 1; /* not a well-formed character: just the one byte */
}

/* given the start of a buffer and a pointer 'p' into it (at a character
   boundary, or at the terminating NUL), return the start of the previous
   character, including any combining marks that follow its base; returns
   'start' if there is nothing before 'p'.  Never reads at or beyond 'p'. */
const char *
utf8_prev(const char *start, const char *p)
{
    const char *q;
    int cp;

    while (p > start) {
        q = utf8_prev_cp(start, p);
        (void) utf8_decode_lim(q, (size_t) (p - q), &cp);
        p = q;
        if (!utf8_is_mark(cp))
            break;
    }
    return p;
}

int
case_insensitive_comp(const char *s1, const char *s2)
{
    uchar u1, u2;

    for (;; s1++, s2++) {
        u1 = (uchar) *s1;
        if (isupper(u1))
            u1 = (uchar) tolower(u1);
        u2 = (uchar) *s2;
        if (isupper(u2))
            u2 = (uchar) tolower(u2);
        if (u1 == '\0' || u1 != u2)
            break;
    }
    return u1 - u2;
}

#if defined(MACOS)
#define RETTYPE ssize_t
#else
#define RETTYPE int
#endif

boolean
copy_bytes(int ifd, int ofd)
{
    char buf[BUFSIZ];
    RETTYPE nfrom, nto;

    do {
        nto = 0;
        nfrom = read(ifd, buf, BUFSIZ);
        /* read can return -1 */
        if (nfrom >= 0 && nfrom <= BUFSIZ)
            nto = write(ofd, buf, nfrom);
        if (nto != nfrom || nfrom < 0)
            return FALSE;
    } while (nfrom == (RETTYPE) BUFSIZ);
    return TRUE;
}
#undef RETTYPE

#define MAX_D 5
struct datamodel_information {
    int sz[MAX_D];
    const char *datamodel;
    const char *dmplatform;
};

static struct datamodel_information dm[] = {
    { { (int) sizeof(short), (int) sizeof(int), (int) sizeof(long),
        (int) sizeof(long long), (int) sizeof(genericptr_t) },
      "", "" },
    { { 2, 4, 4, 8, 4 }, "ILP32LL64", "x86 32-bit" }, /* Windows or Unix */
    { { 2, 4, 4, 8, 8 }, "IL32LLP64", "Windows x64 64-bit" },
    { { 2, 4, 8, 8, 8 }, "I32LP64", "Unix 64-bit"},
    { { 2, 8, 8, 8, 8 }, "ILP64", "Unix ILP64"},      /* HAL, SPARC64 */
};

const char *
datamodel(int retidx)
{
    int i, j, matchcount;
    static const char *unknown = "Unknown";

    for (i = 1; i < SIZE(dm); ++i) {
        matchcount = 0;
        for (j = 0; j < MAX_D; ++j) {
            if (dm[0].sz[j] == dm[i].sz[j])
                ++matchcount;
        }
        if (matchcount == MAX_D)
            return (retidx == 0) ? dm[i].datamodel : dm[i].dmplatform;
    }
    return unknown;
}

const char *
what_datamodel_is_this(int retidx, int szshort, int szint, int szlong, int szll,
                       int szptr)
{
    int i;
    static const char *unknown = "Unknown";

    for (i = 1; i < SIZE(dm); ++i) {
        if (szshort == dm[i].sz[0] && szint == dm[i].sz[1]
            && szlong == dm[i].sz[2] && szll == dm[i].sz[3]
            && szptr == dm[i].sz[4])
            return (retidx == 0) ? dm[i].datamodel : dm[i].dmplatform;
    }
    return unknown;
}
#undef MAX_D
/*hacklib.c*/
