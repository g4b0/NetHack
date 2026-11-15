-- Copyright (c) 2026 the NetHack i18n project.
-- NetHack may be freely redistributed.  See license for details.

-- Test the core UTF-8 helpers in src/hacklib.c (utf8_decode, utf8_width,
-- utf8_len, utf8_cut_cols, utf8_cut_bytes, utf8_prev) through their
-- nh.utf8_*() bindings.  Lua strings are byte strings, so every non-ASCII
-- input is written with explicit \x escapes to keep this file pure ASCII.
-- All offsets are 0-based byte offsets, as in C.

local function show(s)
   return (s:gsub("[^\32-\126]", function(c)
                     return string.format("\\x%02x", c:byte())
                  end))
end

local function check(what, s, got, want)
   if got ~= want then
      error(what .. "(\"" .. show(s) .. "\"): got " .. tostring(got)
            .. ", want " .. tostring(want))
   end
end

local function width(s, want) check("width", s, nh.utf8_width(s), want) end
local function len(s, want) check("len", s, nh.utf8_len(s), want) end
local function cut(s, cols, want)
   check("cut(" .. cols .. ")", s, nh.utf8_cut(s, cols), want)
end
local function cutb(s, n, want)
   check("cutbytes(" .. n .. ")", s, nh.utf8_cutbytes(s, n), want)
end
local function prev(s, off, want)
   check("prev(" .. off .. ")", s, nh.utf8_prev(s, off), want)
end
local function decode(s, want_cp, want_n)
   local cp, n = nh.utf8_decode(s, 0)
   check("decode cp", s, cp, want_cp)
   check("decode n", s, n, want_n)
end

-- Pure ASCII must behave exactly like strlen: the property that makes the
-- later tty and curses changes behaviour-neutral for English.
local function test_ascii()
   local strs = { "", "a", "abc", "Hello, world!", "a b  c",
                  "0123456789012345678901234567890123456789" }
   for _, s in ipairs(strs) do
      width(s, #s)
      len(s, #s)
      for k = 0, #s + 2 do
         local want = math.min(k, #s)
         cut(s, k, want)
         cutb(s, k, want)
         if k <= #s then
            prev(s, k, math.max(k - 1, 0))
         end
      end
   end
   -- the first n bytes
   check("width(3)", "abcdef", nh.utf8_width("abcdef", 3), 3)
   check("width(0)", "abcdef", nh.utf8_width("abcdef", 0), 0)
   check("width(99)", "abcdef", nh.utf8_width("abcdef", 99), 6)
   -- every printable ASCII character is one column and one code point
   for c = 32, 126 do
      local s = string.char(c)
      width(s, 1)
      len(s, 1)
      decode(s, c, 1)
   end
   -- control characters have no width (documented; not strlen)
   width("\1", 0)
   width("\x1b", 0)
   width("\x7f", 0)
   len("\x1b[0m", 4)
end

local function test_decode()
   decode("A", 0x41, 1)
   -- 2-byte
   decode("\xc2\x80", 0x80, 2)
   decode("\xc3\xa9", 0xe9, 2)
   decode("\xdf\xbf", 0x7ff, 2)
   -- 3-byte
   decode("\xe0\xa0\x80", 0x800, 3)
   decode("\xe2\x82\xac", 0x20ac, 3)
   decode("\xed\x9f\xbf", 0xd7ff, 3)
   decode("\xee\x80\x80", 0xe000, 3)
   decode("\xef\xbf\xbd", 0xfffd, 3)
   decode("\xef\xbf\xbf", 0xffff, 3)
   -- 4-byte
   decode("\xf0\x90\x80\x80", 0x10000, 4)
   decode("\xf0\x9f\x98\x80", 0x1f600, 4)
   decode("\xf4\x8f\xbf\xbf", 0x10ffff, 4)
   -- decoding stops at the character, not the string
   decode("\xc3\xa9zzz", 0xe9, 2)
   local cp, n = nh.utf8_decode("a\xc3\xa9", 1)
   check("decode at offset", "a\xc3\xa9", cp, 0xe9)
   check("decode at offset n", "a\xc3\xa9", n, 2)
   -- at the terminating NUL: code point 0, one byte, nothing past it
   cp, n = nh.utf8_decode("ab", 2)
   check("decode at NUL", "ab", cp, 0)
   check("decode at NUL n", "ab", n, 1)
end

-- Every one of these is invalid.  Each must consume exactly one byte and
-- report U+FFFD, so that a caller walking the string always makes progress
-- and never reads past the end.
local invalid = {
   "\x80",                 -- lone continuation byte
   "\xbf",                 -- ditto, the last one
   "\xc3",                 -- 2-byte lead at end of string
   "\xe2\x82",             -- 3-byte lead, truncated
   "\xf0\x9f\x98",         -- 4-byte lead, truncated
   "\xc0\xaf",             -- overlong '/'
   "\xc1\xbf",             -- overlong, largest
   "\xe0\x80\xaf",         -- overlong 3-byte
   "\xe0\x9f\xbf",         -- overlong 3-byte, largest
   "\xf0\x80\x80\xaf",     -- overlong 4-byte
   "\xf0\x8f\xbf\xbf",     -- overlong 4-byte, largest
   "\xed\xa0\x80",         -- surrogate U+D800
   "\xed\xbf\xbf",         -- surrogate U+DFFF
   "\xf4\x90\x80\x80",     -- U+110000, out of range
   "\xf5\x80\x80\x80",     -- lead byte above F4
   "\xf7\xbf\xbf\xbf",
   "\xf8\x88\x80\x80\x80", -- 5-byte form
   "\xfb\xbf\xbf\xbf\xbf",
   "\xfc\x84\x80\x80\x80\x80", -- 6-byte form
   "\xfe",
   "\xff",
   "\xc3A",                -- lead followed by a non-continuation
   "\xe2\x82A",
   "\xc3\xc3\xa9",         -- lead followed by another lead
}

local function test_invalid()
   for _, s in ipairs(invalid) do
      decode(s, 0xfffd, 1)
      -- walk the whole string: must terminate, covering every byte once
      local off, steps = 0, 0
      while off < #s do
         local cp, n = nh.utf8_decode(s, off)
         if n < 1 or n > 4 then
            error("decode(\"" .. show(s) .. "\"): bad length " .. n)
         end
         off = off + n
         steps = steps + 1
         if steps > #s then
            error("decode(\"" .. show(s) .. "\"): no progress")
         end
      end
      check("walk end", s, off, #s)
      -- the string functions must agree with the walk
      local ncp = 0
      off = 0
      while off < #s do
         local _, n = nh.utf8_decode(s, off)
         off = off + n
         ncp = ncp + 1
      end
      len(s, ncp)
      cut(s, 1000, #s)
      cutb(s, 1000, #s)
      -- stepping back from the end also terminates
      off, steps = #s, 0
      while off > 0 do
         local p = nh.utf8_prev(s, off)
         if p >= off then
            error("prev(\"" .. show(s) .. "\", " .. off .. "): no progress")
         end
         off = p
         steps = steps + 1
         if steps > #s then
            error("prev(\"" .. show(s) .. "\"): too many steps")
         end
      end
   end
   -- invalid bytes in context
   width("a\x80b", 3)
   width("a\xc3", 2)
   width("\xc0\xaf", 2)
   len("a\x80b", 3)
   decode("\xc3\x28", 0xfffd, 1)
   cut("a\x80b", 2, 2)
   cutb("a\x80b", 2, 2)
   -- a valid sequence right after an invalid byte is still found
   local cp, n = nh.utf8_decode("\xc3\xc3\xa9", 1)
   check("resync cp", "\xc3\xc3\xa9", cp, 0xe9)
   check("resync n", "\xc3\xc3\xa9", n, 2)
end

local function test_width()
   -- 2-, 3- and 4-byte characters, one column each
   width("caf\xc3\xa9", 4)             -- U+00E9
   len("caf\xc3\xa9", 4)
   width("\xe2\x82\xac", 1)            -- U+20AC
   len("\xe2\x82\xac", 1)
   width("\xce\xb1\xce\xb2", 2)        -- alpha beta
   width("\xd0\x96", 1)                -- Zhe
   width("\xf0\x90\x80\x80", 1)        -- U+10000, Linear B
   width("\xc2\xa0", 1)                -- no-break space
   width("\xcd\xb0", 1)                -- U+0370, just past the marks
   -- precomposed vs decomposed e-acute: same width, different length
   width("\xc3\xa9", 1)
   len("\xc3\xa9", 1)
   width("e\xcc\x81", 1)
   len("e\xcc\x81", 2)
   width("cafe\xcc\x81", 4)
   width("e\xcc\x81\xcc\x82", 1)       -- two marks on one base
   -- zero width: combining marks and the common invisibles
   width("\xcc\x80", 0)                -- U+0300
   width("\xcd\xaf", 0)                -- U+036F
   width("\xe1\xaa\xb0", 0)            -- U+1AB0
   width("\xe1\xab\xbf", 0)            -- U+1AFF
   width("\xe1\xb7\x80", 0)            -- U+1DC0
   width("\xe1\xb7\xbf", 0)            -- U+1DFF
   width("\xe2\x83\x90", 0)            -- U+20D0
   width("\xe2\x83\xbf", 0)            -- U+20FF
   width("\xef\xb8\x80", 0)            -- U+FE00
   width("\xef\xb8\x8f", 0)            -- U+FE0F
   for c = 0x8b, 0x8f do               -- U+200B .. U+200F
      width("\xe2\x80" .. string.char(c), 0)
   end
   width("\xe2\x80\x8a", 1)            -- U+200A hair space
   width("\xe2\x80\x90", 1)            -- U+2010 hyphen
   -- C1 controls have no width, NBSP right after them does
   width("\xc2\x80", 0)                -- U+0080
   width("\xc2\x9f", 0)                -- U+009F
   -- wide: CJK
   width("\xe6\x97\xa5\xe6\x9c\xac", 4)   -- two ideographs
   len("\xe6\x97\xa5\xe6\x9c\xac", 2)
   width("a\xe6\x97\xa5" .. "b", 4)
   width("\xe3\x81\x82", 2)               -- hiragana A
   -- range edges
   width("\xe1\x84\x80", 2)            -- U+1100 Hangul Jamo
   width("\xe1\x85\x9f", 2)            -- U+115F
   width("\xe1\x85\xa0", 1)            -- U+1160
   width("\xe2\xba\x80", 2)            -- U+2E80
   width("\xe3\x80\xbe", 2)            -- U+303E
   width("\xe3\x80\xbf", 1)            -- U+303F is excluded
   width("\xea\x93\x8f", 2)            -- U+A4CF
   width("\xea\x93\x90", 1)            -- U+A4D0
   width("\xea\xb0\x80", 2)            -- U+AC00 Hangul syllable
   width("\xed\x9e\xa3", 2)            -- U+D7A3
   width("\xed\x9e\xa4", 1)            -- U+D7A4
   width("\xef\xa4\x80", 2)            -- U+F900
   width("\xef\xab\xbf", 2)            -- U+FAFF
   width("\xef\xb8\xb0", 2)            -- U+FE30
   width("\xef\xb9\x8f", 2)            -- U+FE4F
   width("\xef\xb9\x90", 1)            -- U+FE50
   width("\xef\xbc\xa1", 2)            -- U+FF21 fullwidth A
   width("\xef\xbd\xa0", 2)            -- U+FF60
   width("\xef\xbd\xa1", 1)            -- U+FF61 halfwidth full stop
   width("\xef\xbf\xa0", 2)            -- U+FFE0
   width("\xef\xbf\xa6", 2)            -- U+FFE6
   width("\xef\xbf\xa7", 1)            -- U+FFE7
   width("\xf0\x9f\x8c\x80", 2)        -- U+1F300
   width("\xf0\x9f\x98\x80", 2)        -- U+1F600
   width("\xf0\x9f\x99\x8f", 2)        -- U+1F64F
   width("\xf0\x9f\x99\x90", 1)        -- U+1F650
   width("\xf0\x9f\xa4\x80", 2)        -- U+1F900
   width("\xf0\x9f\xa7\xbf", 2)        -- U+1F9FF
   width("\xf0\xa0\x80\x80", 2)        -- U+20000
   width("\xf0\xbf\xbf\xbd", 2)        -- U+3FFFD
   width("\xf0\xbf\xbf\xbe", 1)        -- U+3FFFE
   -- the first n bytes only; a character cut by n is invalid bytes
   check("nwidth", "caf\xc3\xa9", nh.utf8_width("caf\xc3\xa9", 4), 4)
   check("nwidth", "caf\xc3\xa9", nh.utf8_width("caf\xc3\xa9", 5), 4)
   check("nwidth", "caf\xc3\xa9", nh.utf8_width("caf\xc3\xa9", 3), 3)
   check("nwidth", "\xe6\x97\xa5", nh.utf8_width("\xe6\x97\xa5", 2), 2)
   check("nwidth", "\xe6\x97\xa5", nh.utf8_width("\xe6\x97\xa5", 3), 2)
   -- empty
   width("", 0)
   len("", 0)
end

local function test_cut()
   -- ASCII is covered in test_ascii
   local cafes = "caf\xc3\xa9s"
   cut(cafes, 0, 0)
   cut(cafes, 3, 3)
   cut(cafes, 4, 5)
   cut(cafes, 5, 6)
   cut(cafes, 99, 6)
   -- wide characters: one that does not fit is left out whole
   local ja = "\xe6\x97\xa5\xe6\x9c\xac"
   cut(ja, 0, 0)
   cut(ja, 1, 0)
   cut(ja, 2, 3)
   cut(ja, 3, 3)
   cut(ja, 4, 6)
   cut(ja, 5, 6)
   cut("a" .. ja, 2, 1)
   cut("a" .. ja, 3, 4)
   cut("\xf0\x9f\x98\x80x", 1, 0)
   cut("\xf0\x9f\x98\x80x", 2, 4)
   cut("\xf0\x9f\x98\x80x", 3, 5)
   -- a base character keeps its combining marks
   cut("e\xcc\x81x", 0, 0)
   cut("e\xcc\x81x", 1, 3)
   cut("e\xcc\x81x", 2, 4)
   cut("e\xcc\x81\xcc\x82x", 1, 5)
   cut("ae\xcc\x81", 1, 1)
   cut("ae\xcc\x81", 2, 4)
   cut("\xe6\x97\xa5\xcc\x81x", 2, 5)
   cut("\xe6\x97\xa5\xcc\x81x", 1, 0)
   -- empty
   cut("", 0, 0)
   cut("", 5, 0)
   -- negative columns behave like zero
   cut("abc", -3, 0)
end

local function test_cutbytes()
   local s = "caf\xc3\xa9"
   cutb(s, 0, 0)
   cutb(s, 3, 3)
   cutb(s, 4, 3)           -- would split the 2-byte character
   cutb(s, 5, 5)
   cutb(s, 99, 5)
   cutb("ab\xe2\x82\xac", 3, 2)
   cutb("ab\xe2\x82\xac", 4, 2)
   cutb("ab\xe2\x82\xac", 5, 5)
   cutb("\xf0\x9f\x98\x80", 1, 0)
   cutb("\xf0\x9f\x98\x80", 3, 0)
   cutb("\xf0\x9f\x98\x80", 4, 4)
   -- base and marks stay together
   cutb("e\xcc\x81", 1, 0)
   cutb("e\xcc\x81", 2, 0)
   cutb("e\xcc\x81", 3, 3)
   cutb("ae\xcc\x81", 2, 1)
   cutb("ae\xcc\x81", 3, 1)
   cutb("ae\xcc\x81", 4, 4)
   cutb("e\xcc\x81\xcc\x82", 4, 0)
   cutb("e\xcc\x81\xcc\x82", 5, 5)
   cutb("\xcc\x81", 1, 0)
   cutb("\xcc\x81", 2, 2)
   -- empty
   cutb("", 0, 0)
   cutb("", 5, 0)
   cutb("abc", -1, 0)
end

local function test_prev()
   prev("", 0, 0)
   prev("abc", 3, 2)
   prev("caf\xc3\xa9", 5, 3)
   prev("caf\xc3\xa9", 3, 2)
   prev("\xe6\x97\xa5\xe6\x9c\xac", 6, 3)
   prev("\xe6\x97\xa5\xe6\x9c\xac", 3, 0)
   prev("a\xf0\x9f\x98\x80", 5, 1)
   -- marks go back together with their base
   prev("e\xcc\x81", 3, 0)
   prev("ae\xcc\x81", 4, 1)
   prev("e\xcc\x81\xcc\x82", 5, 0)
   prev("\xcc\x81", 2, 0)             -- a lone mark: nothing to attach to
   prev("\x80\xcc\x81", 3, 0)
   -- from the middle of a character: still moves back, never forward
   prev("caf\xc3\xa9", 4, 3)
   prev("\xf0\x9f\x98\x80", 2, 1)
   prev("\xf0\x9f\x98\x80", 3, 2)
   -- invalid bytes are one character each
   prev("a\x80", 2, 1)
   prev("a\xc3", 2, 1)
   prev("\xc0\xaf", 2, 1)
   prev("\xe2\x82", 2, 1)
   prev("\xed\xa0\x80", 3, 2)
   -- walk a mixed string back and expect each character start in turn
   local s = "a\xc3\xa9\xe6\x97\xa5\xf0\x9f\x98\x80" .. "e\xcc\x81z"
   local want = { 13, 10, 6, 3, 1, 0 }  -- character starts, from 14
   local off = #s
   check("mixed length", s, off, 14)
   for _, w in ipairs(want) do
      off = nh.utf8_prev(s, off)
      check("mixed walk", s, off, w)
   end
   check("mixed end", s, off, 0)
end

-- A queued accented string must come back from nh.getlin byte for byte.
-- nh.pushkey queues one byte per call of cmdq_add_key, and getlin() in
-- src/windows.c drains that queue itself, so this exercises the core path
-- only: the window port's getlin (win/tty/getline.c) is never reached, and
-- neither is visctrl().
local function test_getlin_roundtrip()
   local typed = { "perch\xc3\xa9", "\xe6\x97\xa5\xe6\x9c\xac",
                   "e\xcc\x81" }
   for _, s in ipairs(typed) do
      nh.pushkey(s)
      local got = nh.getlin("What?")
      check("getlin round trip", s, got, s)
   end
end

test_ascii()
test_decode()
test_invalid()
test_width()
test_cut()
test_cutbytes()
test_prev()
test_getlin_roundtrip()
