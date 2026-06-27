# Mudlle programs (`.mdl`) & scripts (`.scr`)

**Source files:** `src/db.cpp` (`load_mudlle:3047`, `boot_mudlle:3074`, `load_scripts:1022`),
`src/mudlle.cpp`/`mudlle2.cpp` (interpreter), `src/shapescript.cpp` (`.scr` OLC editor);
structs `script_head`/`script_data` (`protos.h:175-192`), `special_list` (`mudlle.h:36`)
**Status:** 🟡 on-disk formats complete. The Mudlle **language** (opcodes/stack) and the
script **command-type** enum are deferred to the systems docs.

## Overview
RotS has **two** scripting systems for NPC/room behavior, loaded from separate world
directories:
- **Mudlle** (`world/mdl`, `MDL_PREFIX`) — the older custom mob-AI language, stored as
  **source text** and compiled to an internal form at boot. Documented for builders by
  `MAN ASIMA` (`text/mudl_tbl`). The runtime is a stack machine (`special_list` in
  `mudlle.h`, executed in `mudlle*.cpp`).
- **Scripts** (`world/scr`, `SCR_PREFIX`) — a newer, **structured** command-list format
  edited via the `shapescript` OLC. Each command is a fixed row of integers plus a text
  field.

Both load before rooms/mobs (`boot_db:330,343`). Mobiles reference them by number
(`store_prog_number`, `script_number` in the `.mob` format — see `world-files.md`).

---

## Mudlle programs (`.mdl`) — `load_mudlle:3047`
```
#<zone>
<mudlle source line>
<mudlle source line>
...
#<zone>
<mudlle source ...>
#99999
```
Parsing rules:
- Each program begins with a header line `#<n>`, where `n` is parsed from the char after
  `#` and stored as the program's **zone** (`mobile_program_zone[]`, `:3057,3062`).
- The body is all following lines until the next line whose first non-blank char is `#`
  (`:3063-3069`). The body text is stored raw in `mobile_program[]`.
- The list terminates at `#99999` (`:3059`).
- At boot, `boot_mudlle` (`:3074`) replaces each raw program with the output of
  `mudlle_converter()` — i.e., **the on-disk format is Mudlle source; compilation happens
  at load**. Programs are 1-indexed (`num_of_programs`).

> The Mudlle source grammar itself (commands, operators, the stack model behind
> `special_list` / `SPECIAL_STACKLEN`) is a language spec belonging in a dedicated
> **systems/mudlle-engine** doc — not captured here. `text/mudl_tbl` (`MAN ASIMA`) is the
> builder-facing reference.

---

## Scripts (`.scr`) — `load_scripts:1022`
```
#<vnum>
<name>~
<description>~
<command_type> <number> <p0> <p1> <p2> <p3> <p4> <p5>
<text>~
<command_type> <number> <p0> <p1> <p2> <p3> <p4> <p5>
<text>~
...
999 0 0 0 0 0 0 0          <- a row whose first int is 999 ends this script's command list
#<vnum>
...
#99999                     <- end of file (also: a name string beginning with '$')
```
Per record (`load_scripts:1029-1080`):
- `#<vnum>` header; `99999` ends the file.
- `name` (tilde string); if it begins with `$`, treated as EOF.
- `description` (tilde string) — *saved back* to the file by the OLC (`script_head` comment).
- Then a **command list**, each command = **eight integers** followed by a **tilde text**
  string:
  | Int | `script_data` field | Meaning |
  |-----|---------------------|---------|
  | 1 | `command_type` | opcode (enum in `script.h`) |
  | 2 | `number` | command's ordinal/number |
  | 3–8 | `param[0..5]` | command parameters (often references to char_script variables) |
  | (string) | `text` | text payload — say/echo text, or a comment |
- A command row whose **first int is `999`** terminates the list (`:1051`); that row has no
  trailing text string.

### Structs
`script_head` (`protos.h:175`): `number`, `name`, `virt_num`, `description`, `host`
(indicates whether the script targets char/obj/room data), `script` (first command).
`script_data` (`protos.h:184`): doubly-linked command node — `room`, `number`,
`command_type`, `param[6]`, `text`.

## RotS-specific notes
- Both systems are RotS extensions (stock DikuMUD has only hardcoded C special procedures).
- The `shape*` OLC suite edits these live (`shapescript.cpp` for `.scr`,
  `shapemdl.cpp` for `.mdl`) and writes them back to the world files.

## Open questions
- **`command_type` enumeration** in `script.h` — the full opcode table and each opcode's
  param/text semantics (→ systems doc).
- **Mudlle language spec** — grammar, builtins, the `special_list` stack discipline, and how
  `mudlle_converter` compiles source (→ systems/mudlle-engine doc).
- How `script_head.host` selects char/obj/room context, and how scripts are triggered at
  runtime.
