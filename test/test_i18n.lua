-- Test the translation catalog (nh.gettext, nh.ngettext, nh.language) and
-- the safe formatter, through nh.i18n_format(fmt, ...), which formats like
-- vpline() does: strings and integers are passed as char * and int.
-- Needs test/lang_zz.po in the game directory; tools/nhtest.py copies it.

local orig_lang = nh.language();

local function check(what, got, want)
   if got ~= want then
      error(what .. ": got \"" .. tostring(got) .. "\", wanted \""
            .. tostring(want) .. "\"", 2);
   end
end

-- no language: every function is the identity
nh.language("");
check("language() with none set", nh.language(), "");
check("gettext without language", nh.gettext("plain"), "plain");
check("gettext with ctx, no language", nh.gettext("Open", "menu"), "Open");
check("ngettext 1 without language",
      nh.ngettext("one thing", "many things", 1), "one thing");
check("ngettext 2 without language",
      nh.ngettext("one thing", "many things", 2), "many things");
check("ngettext 0 without language",
      nh.ngettext("one thing", "many things", 0), "many things");

-- the formatter is plain vsnprintf without a language
check("format, no language", nh.i18n_format("f1 %s and %s.", "a", "b"),
      "f1 a and b.");
check("format, no language, int", nh.i18n_format("f5 %d", 42), "f5 42");
check("format, no language, %%", nh.i18n_format("f6 100%%"), "f6 100%");
check("format, no language, plain", nh.i18n_format("f0 plain text"),
      "f0 plain text");
do
   local s, need = nh.i18n_format_n(8, "f1 %s and %s.", "aaaa", "bbbb");
   check("truncated, no language", s, "f1 aaaa");
   check("would-be length, no language", need, 17);
end

-- a language that does not exist leaves nothing set
check("unknown language", nh.language("nosuchlang"), "");
check("gettext after unknown language", nh.gettext("plain"), "plain");
-- so does a code that is not a plausible language code at all
check("bad code", nh.language("../etc/passwd"), "");
check("bad code, too short", nh.language("z"), "");
check("bad code, too long", nh.language("abcdefghi"), "");
check("bad code, capital first", nh.language("Zz"), "");

-- load the test catalog; the second result is the number of entries
-- dropped as malformed or unusable
local lang, nbad = nh.language("zz");
check("language(zz)", lang, "zz");
check("language() after load", nh.language(), "zz");
check("entries dropped at load", nbad, 24);

-- hits and misses
check("hit", nh.gettext("plain"), "PLAIN");
check("miss", nh.gettext("not in the catalog"), "not in the catalog");
check("case matters", nh.gettext("Plain"), "Plain");
check("empty msgstr is untranslated", nh.gettext("untranslated"),
      "untranslated");
check("utf-8", nh.gettext("coffee"), "caff\u{e8}");
check("later duplicate wins", nh.gettext("dup"), "SECOND");

-- context
check("ctx hit", nh.gettext("Open", "menu"), "OPEN-MENU");
check("same msgid, no ctx", nh.gettext("Open"), "OPEN-NOCTX");
check("ctx miss", nh.gettext("Open", "other"), "Open");
check("ctx only entry, with ctx", nh.gettext("Close", "menu"), "CLOSE-MENU");
check("ctx only entry, without ctx", nh.gettext("Close"), "Close");

-- plurals: Germanic rule, capped to the forms present
check("plural 1", nh.ngettext("one thing", "many things", 1), "ZZ-ONE");
check("plural 2", nh.ngettext("one thing", "many things", 2), "ZZ-MANY");
check("plural 0", nh.ngettext("one thing", "many things", 0), "ZZ-MANY");
check("plural 100", nh.ngettext("one thing", "many things", 100), "ZZ-MANY");
check("plural -1", nh.ngettext("one thing", "many things", -1), "ZZ-MANY");
check("three forms, 1", nh.ngettext("one cat", "many cats", 1), "ZZ-CAT1");
check("three forms, 5", nh.ngettext("one cat", "many cats", 5), "ZZ-CAT2");
check("one form, 1", nh.ngettext("one dog", "many dogs", 1), "ZZ-DOG");
check("one form, 5", nh.ngettext("one dog", "many dogs", 5), "ZZ-DOG");
check("empty form falls back, 1", nh.ngettext("one cow", "many cows", 1),
      "ZZ-COW");
check("empty form falls back, 3", nh.ngettext("one cow", "many cows", 3),
      "many cows");
check("plural miss, 1", nh.ngettext("one ant", "many ants", 1), "one ant");
check("plural miss, 2", nh.ngettext("one ant", "many ants", 2), "many ants");
check("gettext on a plural entry", nh.gettext("one thing"), "ZZ-ONE");

-- escapes and adjacent strings
check("escapes", nh.gettext("tab\there\n"), "TAB\tTHERE\n\"q\" \\ done");
check("multi-line", nh.gettext("multi line"), "MULTI LINE");
check("header is not an entry", nh.gettext(""), "");

-- malformed entries are skipped and do not hurt their neighbours
check("bad escape", nh.gettext("bad escape"), "bad escape");
check("bad quote", nh.gettext("bad quote"), "bad quote");
check("bad keyword", nh.gettext("bad keyword"), "bad keyword");
check("bad index", nh.gettext("bad index"), "bad index");
check("entry right after a bad one", nh.gettext("directly after"), "DIRECT");
check("overlong line", nh.gettext("toolong"), "toolong");
check("entry after the overlong one", nh.gettext("after bad"), "AFTER");
check("dangling msgid", nh.gettext("dangling"), "dangling");

-- formatter: no translation for the format means plain vsnprintf
check("format miss", nh.i18n_format("unknown %s %d", "x", 7), "unknown x 7");
check("format miss, %%", nh.i18n_format("50%% of %s", "x"), "50% of x");

-- formatter: translations
check("no conversions", nh.i18n_format("f0 plain text"), "F0 testo");
check("sequential", nh.i18n_format("f1 %s and %s.", "a", "b"), "F1 a e b.");
check("reordered", nh.i18n_format("f2 %s and %s.", "a", "b"),
      "F2 b, then a.");
check("first argument dropped", nh.i18n_format("f3 %s and %s.", "a", "b"),
      "F3 only b.");
check("second argument dropped", nh.i18n_format("f3b %s and %s.", "a", "b"),
      "F3B only a.");
check("sequential, fewer than English", nh.i18n_format("f10 %s %s", "a", "b"),
      "F10 a");
check("argument used twice", nh.i18n_format("f9 %s", "x"), "F9 x/x");
check("left justified", nh.i18n_format("f4 %s", "ab"), "F4 [ab    ]");
check("precision", nh.i18n_format("f4b %s", "abcdef"), "F4B [abc]");
check("positional width and precision", nh.i18n_format("f4c %s", "abcdef"),
      "F4C [      ab]");
check("zero padded int", nh.i18n_format("f5 %d", 42), "F5 00042");
check("int and string reordered", nh.i18n_format("f5b %d %s", 5, "bob"),
      "F5B bob has +5");
check("negative int", nh.i18n_format("f5 %d", -42), "F5 -0042");
check("char", nh.i18n_format("f7 %c", 65), "F7 <A>");
check("hex with flag", nh.i18n_format("f7b %x", 255), "F7B 0xff");
check("percent, no arguments", nh.i18n_format("f6 100%%"), "F6 100% done");
check("percent with argument", nh.i18n_format("f6b %d%%", 7), "F6B 7% done");
check("star width", nh.i18n_format("f8 %*s", 6, "ab"), "F8 [    ab]");
check("star precision", nh.i18n_format("f8b %.*s", 2, "abcdef"), "F8B ab.");
check("empty string argument", nh.i18n_format("f1 %s and %s.", "", ""),
      "F1  e .");

-- formatter: output limit; the result is cut, NUL-terminated, and the
-- length it would have had is returned, like snprintf
do
   local s, need = nh.i18n_format_n(8, "f1 %s and %s.", "aaaa", "bbbb");
   check("truncated", s, "F1 aaaa");
   check("would-be length", need, 15);
   s, need = nh.i18n_format_n(1, "f1 %s and %s.", "aaaa", "bbbb");
   check("size 1", s, "");
   check("size 1 length", need, 15);
   s, need = nh.i18n_format_n(0, "f1 %s and %s.", "aaaa", "bbbb");
   check("size 0 length", need, 15);
   s, need = nh.i18n_format_n(15, "f1 %s and %s.", "aaaa", "bbbb");
   check("one short", s, "F1 aaaa e bbbb");
   s, need = nh.i18n_format_n(16, "f1 %s and %s.", "aaaa", "bbbb");
   check("exact fit", s, "F1 aaaa e bbbb.");
   check("exact fit length", need, 15);
   s, need = nh.i18n_format_n(10, "f6 100%%");
   check("truncated literal", s, "F6 100% d");
   check("truncated literal length", need, 12);
   local long = string.rep("x", 400);
   s, need = nh.i18n_format("f4 %s", long);
   check("long argument", s, "F4 [" .. long .. "]");
end

-- formatter: translations that cannot be used leave English in place
check("rejected: type mismatch", nh.i18n_format("r1 %s", "x"), "r1 x");
check("rejected: index out of range", nh.i18n_format("r2 %s", "x"), "r2 x");
check("rejected: extra conversion", nh.i18n_format("r3 %s", "x"), "r3 x");
check("rejected: %n", nh.i18n_format("r4 %s", "x"), "r4 x");
check("rejected: mixed, positional last",
      nh.i18n_format("r5 %s %s", "x", "y"), "r5 x y");
check("rejected: mixed, positional first",
      nh.i18n_format("r5b %s %s", "x", "y"), "r5b x y");
check("rejected: int for string", nh.i18n_format("r6 %d", 3), "r6 3");
check("rejected: star without English star", nh.i18n_format("r7 %s", "x"),
      "r7 x");
check("rejected: positional star", nh.i18n_format("r8 %s", "x"), "r8 x");
check("rejected: conversion where English has none",
      nh.i18n_format("r9 literal"), "r9 literal");
check("rejected: unknown conversion", nh.i18n_format("r10 %s", "x"),
      "r10 x");
check("rejected: dangling percent", nh.i18n_format("r11 %s", "x"), "r11 x");
check("rejected: length modifier differs", nh.i18n_format("r12 %d", 3),
      "r12 3");
check("rejected: index 0", nh.i18n_format("r13 %s", "x"), "r13 x");
check("rejected: skips an index", nh.i18n_format("r14 %d %d", 1, 2),
      "r14 1 2");
-- and an English format we cannot read is not translatable at all
check("unreadable English", nh.gettext("u1 %f"), "u1 %f");
check("unreadable English, %zu", nh.gettext("u2 %zu"), "u2 %zu");
check("unreadable English, positional", nh.gettext("u3 %1$s"), "u3 %1$s");
-- rejected entries are no use to gettext either
check("rejected entry, gettext", nh.gettext("r1 %s"), "r1 %s");

-- reloading the same language gives the same result; "" unloads
check("reload", nh.language("zz"), "zz");
check("hit after reload", nh.gettext("plain"), "PLAIN");
check("unload", nh.language(""), "");
check("format after unload", nh.i18n_format("f1 %s and %s.", "a", "b"),
      "f1 a and b.");
check("gettext after unload", nh.gettext("plain"), "plain");
check("ngettext after unload", nh.ngettext("one thing", "many things", 2),
      "many things");
-- "en" is the same as none
nh.language("zz");
check("en unloads", nh.language("en"), "");
check("gettext after en", nh.gettext("plain"), "plain");

-- leave the game the way we found it
nh.language(orig_lang);
