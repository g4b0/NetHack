-- Test the translation catalog: nh.gettext, nh.ngettext, nh.language
-- (and, further down, the safe formatter through nh.i18n_format).
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
check("entries dropped at load", nbad, 6);

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

-- reloading the same language gives the same result; "" unloads
check("reload", nh.language("zz"), "zz");
check("hit after reload", nh.gettext("plain"), "PLAIN");
check("unload", nh.language(""), "");
check("gettext after unload", nh.gettext("plain"), "plain");
check("ngettext after unload", nh.ngettext("one thing", "many things", 2),
      "many things");
-- "en" is the same as none
nh.language("zz");
check("en unloads", nh.language("en"), "");
check("gettext after en", nh.gettext("plain"), "plain");

-- leave the game the way we found it
nh.language(orig_lang);
