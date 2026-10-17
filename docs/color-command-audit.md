# Colour command & help audit

**Date:** 2026-09-03
**Branch:** `docs/color-help-mob-slot`
**Trigger:** documenting the `mob` colour slot added by PR #290 (`e6bc655`).

Two defects surfaced while verifying the help text against a booted server. One is fixed
here; one is open and needs a design call.

---

## 1. FIXED — `help color` answered the wrong page, and no page listed `mob`

**Symptom.** PR #290 added a `mob` colour slot, but the help text still listed only the
original fifteen, so the new slot was undiscoverable in game. Worse, `help color` did not
even reach the page with the slot list on it.

**Cause.** Two entries in `lib/text/help_tbl` both claimed the `COLOR` keyword:

| entry | keywords | content |
|---|---|---|
| current | `COLOR COLOUR COLORS COLOURS` | full reference: slots, ANSI, RGB/hex, fg/bg |
| legacy | `ANSI COLOR COLOUR` | predates RGB/hex; lists **no** slots |

`build_help_index()` (`src/modify.cpp:723`) creates one index entry **per keyword** on an
entry's first line, bubble-sorts them, and `do_help` (`src/act_info.cpp:2090`) binary
searches that list with a prefix compare. With a keyword duplicated across two entries,
which one answers is an artifact of where the search happens to land — it is not
first-match or last-match, and it varies per spelling:

```
help color     -> legacy ANSI page   (no slot list at all)
help colour    -> current page
help colors    -> current page
help colours   -> current page
```

**Fix.** Added `mob` to the slot list plus a line explaining how `character` / `enemy` /
`mob` now divide up creature names, and narrowed the legacy entry's keyword line to `ANSI`
so all four colour spellings reach the current page. The legacy page keeps its content and
is still reachable as `help ansi`, now with a cross-reference to `HELP COLOUR`.

**Verified** against a booted server: all four spellings show the slot list including
`mob`, `help ansi` still resolves, and the documented slots match the live `color` output
exactly (16 names).

**Note for future slot additions.** `lib/text/help_tbl` **is tracked in git**, unlike most
of `lib/` — help edits are committable in this repo. It uses plain `\n`, not the `\n\r` of
the world files. The slot list there is hand-maintained and will drift from
`color_fields[]` (`src/color.cpp:227`) unless updated deliberately.

---

## 2. OPEN — `color` is swallowed by the mail board in 45 rooms

**Symptom.** In any room containing a board, typing `color` prints

```
Send the letter to whom?
```

and never reaches `do_color`. `colour` works normally in the same room, so the two
spellings of the same command behave differently depending on where the player stands.

**Cause.** `src/interpre.h:328` defines `CMD_SEND 171`, but slot 171 in `command[]`
(`src/interpre.cpp:311`) is **`color`**. There is no `send` command in the table at all.
`gen_board` lets `CMD_SEND` through its command filter (`src/boards.cpp:221`) and then
rewrites it to `CMD_WRITE` against `mail_board` (`src/boards.cpp:284`), so the colour
command is dispatched into `mail_info_type::write_message` with an empty argument.

Command numbers are 1-based: `old_search_block` (`src/interpre.cpp:601`) returns `guess`
*after* incrementing, so a word at 0-based array position `i` yields command number `i+1`.

**Confirmed live** (2026-09-03, release-frodo): Creation Hall 1101 (has a board) swallows
`color` but honours `colour`; the Arena 1120 (no board) honours both.

**Scope.** 24 object vnums get `gen_board` in `src/spec_ass.cpp`; zone `O` commands place
them in **45 distinct rooms**:

```
1101 1102 1103 1105 1106 1107 1109 1111 1112 1113 1114 1115 1116 1117 1119
1125 1130 1131 1132 1133 1134 1197 1506 1551 3022 3039 6012 6015 6075
10105 10129 10299 13530 13738 13759 13798 14485 14488 15898 16810 16856
27533 27564 27576 32584
```

**Not fixed here** — a fix has to decide what `CMD_SEND` should mean now that no `send`
command exists. Either retire the `CMD_SEND` branch in `gen_board` (and drop the constant),
or point it at a real command. Do not simply renumber it without re-checking the constant
against `command[]`.

---

## 3. Checked and clear — the rest of the `CMD_*` block

Because #2 looked like command-table drift, all 74 `CMD_*` constants in `src/interpre.h`
were audited against `command[]` and against the `COMMANDO()` registrations.

**Result: `CMD_SEND` is the only genuine mismatch.** Every other constant lands on its own
word, and every `COMMANDO()` registration lands on the word it implements. The 59
word/handler name differences are all legitimate aliases (`north`→`do_move`,
`kill`→`do_hit`, `cls`→`do_gen_ps`, `reply`→`do_tell`, and so on).

> **Trap for anyone repeating this audit by script.** `src/interpre.cpp:531` reads
> `"trap", /* "trap",   */`. A naive regex over string literals counts the commented-out
> copy as a real array entry and reports everything past index 220 as shifted by one — a
> false alarm that looks exactly like a systematic off-by-one. Strip comments first, and
> anchor on the `/* 221 */` marker next to `"account"`.

Three constants point at deliberately empty word slots, so no player input can ever produce
them:

| constant | slot | assessment |
|---|---|---|
| `CMD_SOCIAL` 22 | `""` | **By design.** `interpre.cpp:1358` assigns it internally when a social matches; an unreachable word slot is what makes it safe as a sentinel. |
| `CMD_RECITE` 112 | `""` | **Harmless.** Sole use is a commented-out line, `shop.cpp:578`. |
| `CMD_BLOCK` 133 | `""` | **Suspect, unverified.** `spec_pro.cpp` compares `cmd == CMD_BLOCK` in 7 places (2385, 2432, 2478, 2523, 2568, 2613, 2658). With no word mapped to 133, none of those can fire from player input — likely dead code from a retired `block` command, but not traced further. |
