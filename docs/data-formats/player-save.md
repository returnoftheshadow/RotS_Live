# Player save files

**Source files:** `src/db.cpp` (`save_player:2302`, `load_player:1736`,
`char_to_store:2099`, `store_to_char:1995`, `save_char:2475`, `load_char:1979`);
`struct char_file_u` (`structs.h:1783`)
**Status:** ✅ write format complete (from `save_player`). Load parser / `SAVE_VERSION`
migrations noted under Open questions.

## Purpose
Each player character is persisted to its **own text file** under `lib/players/`. The
current format is line-oriented `key value` text under a `#player` section — **not** a
binary struct dump. (`struct char_file_u` still exists as the in-memory marshalling
buffer that `char_to_store` fills before writing, but it is no longer the on-disk layout.)

> ⚠️ The Explore-phase note that players are a "binary struct dump of `char_file_u`" was
> **wrong** — that's the legacy DikuMUD format. `save_player` writes text.

## File location & naming (`save_player:2311-2472`)
The character name is lowercased and bucketed by first letter into a subdirectory:

| First letter | Directory |
|--------------|-----------|
| a–e | `players/A-E/` |
| f–j | `players/F-J/` |
| k–o | `players/K-O/` |
| p–t | `players/P-T/` |
| u–z | `players/U-Z/` |
| other | `players/ZZZ/` |

The filename **encodes index metadata** as dotted suffixes:
```
players/<bucket>/<name>.<level>.<race>.<idnum>.<log_time>.<flags>
```
Save procedure: write to `players/temp`, then `rm <bucket>/<name>.*` and `cp` temp to the
suffixed filename (via `system()` calls — `:2463-2471`). So the canonical file is found by
glob `<name>.*`, and level/race/idnum/last-logon/flags are readable without opening it.

## File body format (`save_player:2364-2461`)
```
#player
version     <SAVE_VERSION>
name        <name>
sex         <int>
prof        <int>
race        <int>
bodytype    <int>
level       <int>
language    <int>
birth       <unix_time>
played      <seconds>
weight      <int>
height      <int>
title       <string to EOL>
hometown    <int>
description
<multi-line text>~
last_logon  <unix_time>
password    <encrypted>        (see Encryption below)
host        <string>
idnum       <long>
load_room   <int>
sp_to_learn <int>
alignment   <int>              (-1000..+1000)
act         <long bitvector>
pref        <long bitvector>
wimpy       <int>
freeze_lvl  <int>
bad_pws     <int>
conditions0 <int>              (drunk)
conditions1 <int>              (full)
conditions2 <int>              (thirst)
mini_lvl    <int>
morale      <int>
owner       <int>
rerolls     <int>
max_mini_lv <int>
perception  <int>
rp_flag     <int>
retiredon   <int>
ob          <int>
damage      <int>
ENE_regen   <int>
parry       <int>
dodge       <int>
gold        <int>
exp         <int>
encumb      <int>
spec        <int>              (active specialization)
```
Then **repeated / indexed** lines (only nonzero entries are written, except where noted):
```
color       <idx> <val>        per non-default color field (MAX_COLOR_FIELDS)
talks       <idx> <val>        languages spoken, idx 0..MAX_TOUNGE-1 (3)   [all written]
skills      <idx> <val>        skill proficiency, idx 0..MAX_SKILLS-1 (256) [nonzero only]
affect      <slot> <type> <duration> <modifier> <location> <bitvector>     [duration!=0 only]
bodyparts   <idx> <val>        per-bodypart hp, idx 0..MAX_BODYPARTS-1 (11) [all written]
tmpstats    <str> <lea> <intel> <wil> <dex> <con>     (current abilities)
tmpabil     <hit> <mana> <move> <spirit>              (current hp/mana/move + spirit)
permstats   <str> <lea> <intel> <wil> <dex> <con>     (permanent/rolled abilities)
permabil    <hit> <mana> <move> 0                      (permanent maxima; 4th always 0)
prof_coef   <idx> <val>        proficiency %, idx 0..MAX_PROFS (5)
prof_level  <idx> <val>        level per profession, idx 0..MAX_PROFS (5)
prof_exp    <idx> <val>        exp per profession,  idx 0..MAX_PROFS (5)
end
```
The `end` token closes the record. `#player` is the only section today; the comment at
`:2364` notes more `#`-sections may be added later.

Note the ability-stat field order is **str, lea, intel, wil, dex, con** (matches
`char_ability_data` at `structs.h:1037`, whose declaration order is str/lea/intel/wil/dex/con).

## Encryption (`save_player:2381-2383`)
The password is copied (`MAX_PWD_LENGTH = 10`) and run through `encrypt_line()` before
writing. This is a simple reversible obfuscation (paired with `decrypt_line()`), **not** a
cryptographic hash — treat stored passwords as plaintext-equivalent for security purposes.

## Key size constants (`structs.h`)
`MAX_NAME_LENGTH=12`, `MAX_PWD_LENGTH=10`, `HOST_LEN=30`, `MAX_TOUNGE=3`, `MAX_SKILLS=256`,
`MAX_AFFECT=32`, `MAX_BODYPARTS=11`, `MAX_PROFS=4` (arrays sized `MAX_PROFS+1`).

## RotS-specific notes
- Per-character files with metadata-encoded filenames (vs. DikuMUD's single binary
  `players` file) are a RotS design.
- `affect` lines persist active spell/skill effects across logout.
- The many RotS combat/perception fields (`ob/damage/parry/dodge/ENE_regen/encumb/
  perception/rp_flag/morale/mini_lvl`) are saved inline.

## Open questions
- **Load parser** (`load_player:1736`) — confirm it tolerates missing/extra keys and the
  exact `description` multiline read. (The writer is authoritative for the format; the
  reader should be cross-checked.)
- **`SAVE_VERSION` migrations** — `load_char` calls `convert_old_colormask` (`db.cpp:1989`);
  enumerate what each version bump changed.
- **Player index** — `player_table` / `.ch_file` and any on-disk player index file that maps
  names→files at boot (separate from these per-player files).
- Whether legacy binary `char_file_u` files still need a one-time import path (likely moot —
  no player data exists in this fork).
