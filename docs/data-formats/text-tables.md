# Text tables & help (`lib/text/`)

**Source files:** `src/db.cpp` (`boot_db:287`, `file_to_string_alloc` calls `:214-321`),
`src/modify.cpp` (`build_help_index:723`); structs `help_index_element` (`db.h:197`),
`help_index_summary` (`db.h:202`)
**Status:** ✅ complete.

## Purpose
Files under `lib/text/` provide the player-facing reference text: the `MAN`/`HELP` pages,
login screens, policies, and the keyword-indexed help system. **Most are displayed
verbatim** — only `help_tbl` is parsed into an index.

> ⚠️ Important for a rewrite: `spel_tbl`, `pray_tbl`, `skil_tbl`, `shap_tbl`, `mudl_tbl`
> are **human-readable documentation shown by the `MAN` command — not the source of game
> mechanics.** Actual spell/skill/prayer data lives in code (`spells.h`, the `skills[]`
> array, etc.), to be covered in the systems docs and catalogs. Keeping these text files in
> sync with the code is manual.

## Category 1 — verbatim text (slurped via `file_to_string_alloc`)
The whole file is read into a buffer at boot and printed as-is when the relevant command is
used. No structure, no parsing. Reloadable at runtime via `do_reload` (`modify.cpp`/
`db.cpp:204`). Files (`db.h`):

| Constant | Path | Shown by |
|----------|------|----------|
| `HELP_PAGE_FILE` | `text/help` | `HELP` with no argument |
| `INFO_FILE` | `text/info` | `INFO` |
| `SPELL_FILE` | `text/spel_tbl` | `MAN SPELL` |
| `POWER_FILE` | `text/pray_tbl` | `MAN POWER` (mystic prayers) |
| `SKILLS_FILE` | `text/skil_tbl` | `MAN SKILL` |
| `SHAPE_FILE` | `text/shap_tbl` | `MAN SHAPE` |
| `ASIMA_FILE` | `text/mudl_tbl` | `MAN ASIMA` (mudlle scripting) |
| `MSDP_FILE` | `text/msdp_tbl` | `MAN MSDP` |
| `POLICIES_FILE` | `text/policies` | `POLICY` |
| `HANDBOOK_FILE` | `text/handbook` | immortal handbook |
| `BACKGROUND_FILE` | `text/backgr` | background story |
| (also) | `news`, `credits`, `motd`, `imotd`, `wizlist`, `immlist` | login/`CREDITS`/etc. |

These may contain `act()` color codes (`$CN`…) but are otherwise free text. A rewrite can
store them as plain files or markdown — there is no on-disk schema to match.

## Category 2 — keyword-indexed help (`text/help_tbl`) — `build_help_index:723`
`HELP_KWRD_FILE = text/help_tbl`. Parsed into an index of `{keyword, file-offset}` so
`HELP <keyword>` can seek directly to the entry. (`help_content[]` lists one or more such
help files to index — `db.cpp:272,321`.)

### Format
```
<keyword> [<keyword> ...]          <- line 1 of an entry = whitespace-separated keywords
<body line>
<body line>
...
#                                  <- a line beginning with '#' ends the entry
<keyword> [<keyword> ...]
<body...>
#
...
#~                                 <- '#~' ends the file
```
Parsing rules (`build_help_index:731-763`):
- For each entry, the file offset (`ftell`) of the **keyword line** is recorded against
  **every** keyword on that line (one entry can be reached by several keywords).
- The body is everything until a line whose first char is `#`.
- A `#` line alone separates entries; a line beginning `#~` terminates the file.
- The index is sorted alphabetically by keyword after load (`:764-775`).
- Display: `HELP foo` seeks to the recorded `pos` and shows text up to the next `#`.

### `help_index_element` (`db.h:197`)
```c
struct help_index_element { char *keyword; long pos; };
```

## Worked example (`help_tbl`)
```
flee retreat
Leave combat in a random direction.  You may lose your footing.
Syntax:  flee
#
hide sneak
Conceal yourself.  Requires the hide skill.
#
#~
```
`HELP flee`, `HELP retreat`, `HELP hide`, and `HELP sneak` all resolve via the index.

## Open questions
- `help_content[]` contents — which file(s) besides `help_tbl` are indexed, and the
  `help_index_summary` (`keyword/descr/filename`) usage.
- Whether any `MAN` page is *also* parsed elsewhere for data (none observed — they appear to
  be display-only).
