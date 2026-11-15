/* $NHDT-Header: NetHack 5.0 getline.c 2026/06/20_22:26:43 NetHack-5.0:1.74 $ */
/* Copyright (c) Stichting Mathematisch Centrum, Amsterdam, 1985. */
/*-Copyright (c) Michael Allison, 2006. */
/* NetHack may be freely redistributed.  See license for details. */

#include "hack.h"

#ifdef TTY_GRAPHICS

#if !defined(MAC68K)
#define NEWAUTOCOMP
#endif

#include "wintty.h"
#include "func_tab.h"

char morc = 0; /* tell the outside world what char you chose */
static boolean suppress_history;
static boolean ext_cmd_getlin_hook(char *);

typedef boolean (*getlin_hook_proc)(char *);

static void hooked_tty_getlin(const char *, char *, getlin_hook_proc);
static void getlin_redraw(const char *, const char *);
extern int extcmd_via_menu(void); /* cmd.c */

extern char erase_char, kill_char; /* from appropriate tty.c file */

/* bytes read ahead and not used yet (a LIFO, at most 4 long) */
static int ungot[4], nungot;

static int
getlin_getc(void)
{
    return nungot ? ungot[--nungot] : pgetchar();
}

static void
getlin_ungetc(int c)
{
    if (nungot < SIZE(ungot))
        ungot[nungot++] = c;
}

/* redraw the prompt and the text typed so far; used instead of erasing on
   screen when that would have to back up over a line wrap, where the
   position of the end of the previous row depends on the characters */
static void
getlin_redraw(const char *query, const char *text)
{
    struct WinDesc *cw = wins[WIN_MESSAGE];

    tty_clear_nhwindow(WIN_MESSAGE);
    cw->maxcol = cw->maxrow;
    addtopl(query);
    addtopl(" ");
    addtopl(text);
}

/*
 * Read a line closed with '\n' into the array char bufp[BUFSZ].
 * (The '\n' is not stored. The string is closed with a '\0'.)
 * Reading can be interrupted by an escape ('\033').  If there is already
 * some text, it is removed and prompting continues as if from the start.
 * However, if there is no text yet (or anymore) then "\033" is returned.
 */
void
tty_getlin(const char *query, char *bufp)
{
    suppress_history = FALSE;
    hooked_tty_getlin(query, bufp, (getlin_hook_proc) 0);
}

static void
hooked_tty_getlin(
    const char *query,
    char *bufp,
    getlin_hook_proc hook)
{
    char *obufp = bufp;
    int c, k, w, mblen;
    boolean wrapped;
    char mb[5];
    struct WinDesc *cw = wins[WIN_MESSAGE];
    boolean doprev = FALSE;

    if (ttyDisplay->toplin == TOPLINE_NEED_MORE && !(cw->flags & WIN_STOP))
        more();
    cw->flags &= ~WIN_STOP;
    ttyDisplay->toplin = TOPLINE_SPECIAL_PROMPT;
    ttyDisplay->inread++;
    nungot = 0;

    /*
     * Issue the prompt.
     *
     * custompline() will call vpline() which calls flush_screen() which
     * calls bot(). The core now disables bot() processing while inside
     * getlin, so the screen won't be modified during whatever this prompt
     * is for.
     */
    custompline(OVERRIDE_MSGTYPE | SUPPRESS_HISTORY, "%s ", query);

#ifdef EDIT_GETLIN
    /* bufp is input/output; treat current contents (presumed to be from
       previous getlin()) as default input */
    addtopl(obufp);
    bufp = eos(obufp);
#else
    /* !EDIT_GETLIN: bufp is output only; init it to empty */
    *bufp = '\0';
#endif

    for (;;) {
        (void) fflush(stdout);
        Strcat(strcat(strcpy(gt.toplines, query), " "), obufp);
        term_curs_set(1);
        c = getlin_getc();
        /* a multibyte character is collected here, before anything is
           echoed, so that its bytes reach the terminal together */
        mb[0] = (char) c;
        mblen = 1;
        if ((uchar) c >= 0xC2 && (uchar) c <= 0xF4) {
            int need = ((uchar) c < 0xE0) ? 2 : ((uchar) c < 0xF0) ? 3 : 4;
            int extra = -2; /* not a byte: nothing was read ahead */

            while (mblen < need) {
                k = getlin_getc();
                if (!utf8_is_cont(k)) {
                    extra = k; /* not part of it; use it next time */
                    break;
                }
                mb[mblen++] = (char) k;
            }
            mb[mblen] = '\0';
            if (extra != -2)
                getlin_ungetc(extra);
            if (mblen < need || utf8_decode(mb, (int *) 0) != mblen) {
                /* not UTF-8 after all: one byte at a time, as ever */
                while (mblen > 1)
                    getlin_ungetc(mb[--mblen]);
            }
        }
        term_curs_set(0);
        if (c == '\033' || c == EOF) {
            if (c == EOF)
                iflags.term_gone = 1;
            if (c == '\033' && obufp[0] != '\0') {
                obufp[0] = '\0';
                bufp = obufp;
                tty_clear_nhwindow(WIN_MESSAGE);
                cw->maxcol = cw->maxrow;
                addtopl(query);
                addtopl(" ");
                addtopl(obufp);
            } else {
                obufp[0] = '\033';
                obufp[1] = '\0';
                break;
            }
        }
        if (ttyDisplay->intr) {
            ttyDisplay->intr--;
            *bufp = 0;
        }
        if (c == C('p')) { /* ctrl-P, doesn't honor rebinding #prevmsg cmd */
            int sav = ttyDisplay->inread;

            ttyDisplay->inread = 0;
            if (iflags.prevmsg_window == 's'
                || (iflags.prevmsg_window == 'c' && !doprev)) {
                /* msg_window:single, or msg_window:combination while it's
                   behaving like msg_window:single */
                if (!doprev)
                    (void) tty_doprev_message(); /* need two initially */
                (void) tty_doprev_message();
                ttyDisplay->inread = sav;
                doprev = TRUE;
                continue;
            } else {
                /* msg_window:full or reverse, or msg_window:combination while
                   it's behaving like msg_window:full */
                (void) tty_doprev_message();
                ttyDisplay->inread = sav;
                doprev = FALSE;
                tty_clear_nhwindow(WIN_MESSAGE);
                cw->maxcol = cw->maxrow;
                addtopl(query);
                addtopl(" ");
                *bufp = 0;
                addtopl(obufp);
            }
        } else if (doprev) {
            tty_clear_nhwindow(WIN_MESSAGE);
            cw->maxcol = cw->maxrow;
            doprev = FALSE;
            addtopl(query);
            addtopl(" ");
            *bufp = 0;
            addtopl(obufp);
        }
        /* is there anything but ASCII typed, on more than one row? */
        wrapped = (ttyDisplay->cury > 0
                   && utf8_width(obufp) != (int) strlen(obufp));
        if (c == erase_char || c == '\b') {
            if (bufp != obufp) {
                /* back up over one character and its combining marks */
                char *prev = (char *) utf8_prev(obufp, bufp);

                w = utf8_nwidth(prev, (size_t) (bufp - prev));
                bufp = prev;
                if (wrapped) {
                    *bufp = 0;
                    getlin_redraw(query, obufp);
                    continue;
                }
#ifndef NEWAUTOCOMP
                for (k = w; k > 0; --k)
                    putsyms("\b \b"); /* putsym converts \b */
#else                             /* NEWAUTOCOMP */
                for (k = w; k > 0; --k)
                    putsyms("\b");
                /* blank the removed character and any guess after it */
                w = utf8_width(bufp);
                for (k = w; k > 0; --k)
                    putsyms(" ");
                for (k = w; k > 0; --k)
                    putsyms("\b");
                *bufp = 0;
#endif                            /* NEWAUTOCOMP */
            } else
                tty_nhbell();
        } else if (c == '\n' || c == '\r') {
#ifndef NEWAUTOCOMP
            *bufp = 0;
#endif /* not NEWAUTOCOMP */
            break;
        } else if (' ' <= (unsigned char) c && c != '\177'
                   /* avoid isprint() - some people don't have it
                      ' ' is not always a printing char */
                   && (bufp - obufp + mblen < BUFSZ
                       && utf8_nwidth(obufp, (size_t) (bufp - obufp))
                          < COLNO)) {
#ifdef NEWAUTOCOMP
            int tailw = utf8_width(bufp); /* columns of the guess */

#endif /* NEWAUTOCOMP */
            (void) memcpy(bufp, mb, (size_t) mblen);
            bufp[mblen] = 0;
            w = utf8_width(bufp);
            putsyms(bufp);
            bufp += mblen;
            if (hook && (*hook)(obufp)) {
                putsyms(bufp);
#ifndef NEWAUTOCOMP
                bufp = eos(bufp);
#else  /* NEWAUTOCOMP */
                /* pointer and cursor left where they were */
                for (k = utf8_width(bufp); k > 0; --k)
                    putsyms("\b");
            } else if (tailw > w) {
                /* erase rest of prior guess */
                for (k = tailw - w; k > 0; --k)
                    putsyms(" ");
                for (k = tailw - w; k > 0; --k)
                    putsyms("\b");
#endif /* NEWAUTOCOMP */
            }
        } else if (c == kill_char || c == '\177') { /* Robert Viduya */
            /* this test last - @ might be the kill_char */
            if (wrapped) {
                bufp = obufp;
                *bufp = 0;
                getlin_redraw(query, obufp);
                continue;
            }
#ifndef NEWAUTOCOMP
            for (k = utf8_width(obufp); k > 0; --k)
                putsyms("\b \b");
            bufp = obufp;
#else  /* NEWAUTOCOMP */
            for (k = utf8_width(bufp); k > 0; --k)
                putsyms(" ");
            for (k = utf8_width(obufp); k > 0; --k)
                putsyms("\b \b");
            bufp = obufp;
            *bufp = 0;
#endif /* NEWAUTOCOMP */
        } else
            tty_nhbell();
    }
    ttyDisplay->toplin = TOPLINE_NON_EMPTY;
    ttyDisplay->inread--;
    clear_nhwindow(WIN_MESSAGE); /* clean up after ourselves */

    if (suppress_history) {
        /* prevent next message from pushing current query+answer into
           tty message history */
        *gt.toplines = '\0';
#ifdef DUMPLOG_CORE
    } else {
        /* needed because we've bypassed pline() */
        dumplogmsg(gt.toplines);
#endif
    }
}

void
xwaitforspace(const char *s) /* chars allowed besides return */
{
    int c, x = ttyDisplay ? (int) ttyDisplay->dismiss_more : '\n';

    morc = 0;
    while (
#ifdef HANGUPHANDLING
        !program_state.done_hup &&
#endif
        (c = tty_nhgetch()) != EOF) {
        if (c == '\n' || c == '\r')
            break;

        if (iflags.cbreak) {
            if (c == '\033') {
                if (ttyDisplay)
                    ttyDisplay->dismiss_more = 1;
                morc = '\033';
                break;
            }
            if ((s && strchr(s, c)) || c == x || (x == '\n' && c == '\r')) {
                morc = (char) c;
                break;
            }
            tty_nhbell();
        }
    }
}

/*
 * Implement extended command completion by using this hook into
 * tty_getlin.  Check the characters already typed, if they uniquely
 * identify an extended command, expand the string to the whole
 * command.
 *
 * Return TRUE if we've extended the string at base.  Otherwise return FALSE.
 * Assumptions:
 *
 *      + we don't change the characters that are already in base
 *      + base has enough room to hold our string
 */
static boolean
ext_cmd_getlin_hook(char *base)
{
    int *ecmatches;
    int nmatches = extcmds_match(base, ECM_NOFLAGS, &ecmatches);

    if (nmatches == 1) {
        struct ext_func_tab *ec = extcmds_getentry(ecmatches[0]);

        Strcpy(base, ec->ef_txt);
        return TRUE;
    }

    return FALSE; /* didn't match anything */
}

/*
 * Read in an extended command, doing command line completion.  We
 * stop when we have found enough characters to make a unique command.
 */
int
tty_get_ext_cmd(void)
{
    char buf[BUFSZ];
    int nmatches;
    int *ecmatches = 0;
    boolean (*no_hook)(char *base) = (boolean (*)(char *)) 0;
    char extcmd_char[2];

    if (iflags.extmenu)
        return extcmd_via_menu();

    suppress_history = TRUE;
    /* maybe a runtime option?
     * hooked_tty_getlin("#", buf,
     *                   (flags.cmd_comp && !gi.in_doagain)
     *                      ? ext_cmd_getlin_hook
     *                      : (getlin_hook_proc) 0);
     */
    extcmd_char[0] = extcmd_initiator(), extcmd_char[1] = '\0';
    buf[0] = '\0';
    hooked_tty_getlin(extcmd_char, buf,
                      !gi.in_doagain ? ext_cmd_getlin_hook : no_hook);
    (void) mungspaces(buf);

    nmatches = (buf[0] == '\0' || buf[0] == '\033') ? -1
              : extcmds_match(buf, ECM_IGNOREAC | ECM_EXACTMATCH, &ecmatches);
    if (nmatches != 1) {
        if (nmatches != -1)
            pline("%s%.*s: unknown extended command.",
                  visctrl(extcmd_char[0]), (int) utf8_cut_bytes(buf, 60),
                  buf);
        return -1;
    }

    return ecmatches[0];
}

#endif /* TTY_GRAPHICS */

/*getline.c*/
